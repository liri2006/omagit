#include "CommitMessageAgent.h"
#include "ProcessUtil.h"
#include "Settings.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTimer>

#include <algorithm>

namespace {
constexpr int kTimeoutMs = 180000;
constexpr int kProbeTimeoutMs = 8000;
// Agents write their answer a token at a time. Tidying the whole message
// again per token costs more the longer it gets, and no eye follows it that
// closely, so the preview is refreshed on a beat instead.
constexpr int kPreviewIntervalMs = 50;

QHash<QString, AgentCatalog> &catalogCache()
{
    static QHash<QString, AgentCatalog> cache;
    return cache;
}

QString firstLine(const QString &text)
{
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &l : lines) {
        const QString t = l.trimmed();
        if (!t.isEmpty())
            return t;
    }
    return QString();
}

QString stripAnsi(QString s)
{
    static const QRegularExpression csi(QStringLiteral("\x1b\\[[0-9;?]*[ -/]*[@-~]"));
    static const QRegularExpression osc(QStringLiteral("\x1b\\][^\x07\x1b]*(\x07|\x1b\\\\)"));
    s.remove(csi);
    s.remove(osc);
    s.remove(QLatin1Char('\r'));
    return s;
}

QProcessEnvironment quietEnvironment()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("NO_COLOR"), QStringLiteral("1"));
    env.insert(QStringLiteral("TERM"), QStringLiteral("dumb"));
    env.remove(QStringLiteral("CLAUDECODE")); // a Claude Code session refuses to nest otherwise
    return env;
}

// The paragraph of `--option` in a --help text: its own line and the
// more-indented lines that follow, joined with spaces.
QString helpParagraph(const QString &help, const QString &option)
{
    const QStringList lines = help.split(QLatin1Char('\n'));
    static const QRegularExpression optionStart(QStringLiteral("^\\s+-"));
    QString out;
    bool inside = false;
    for (const QString &line : lines) {
        if (inside) {
            if (line.trimmed().isEmpty() || line.contains(optionStart))
                break;
            out += QLatin1Char(' ') + line.trimmed();
            continue;
        }
        const QString t = line.trimmed();
        if (t.startsWith(option + QLatin1Char(' ')) || t.startsWith(option + QLatin1Char(','))
            || t.startsWith(option + QLatin1Char('<'))) {
            inside = true;
            out = t;
        }
    }
    return out;
}
} // namespace

QStringList AgentCatalog::effortsFor(const QString &model) const
{
    for (const AgentModel &m : models)
        if (m.id == model && !m.efforts.isEmpty())
            return m.efforts;
    return efforts;
}

QList<AgentSpec> AgentCli::agents()
{
    static const QList<AgentSpec> list{
        AgentSpec{QStringLiteral("claude"), QStringLiteral("Claude Code"), QStringLiteral("claude")},
        AgentSpec{QStringLiteral("codex"), QStringLiteral("Codex"), QStringLiteral("codex")},
    };
    return list;
}

AgentSpec AgentCli::spec(const QString &id)
{
    const QList<AgentSpec> all = agents();
    for (const AgentSpec &a : all)
        if (a.id == id)
            return a;
    return AgentSpec();
}

QList<AgentSpec> AgentCli::installedAgents()
{
    QList<AgentSpec> out;
    const QList<AgentSpec> all = agents();
    for (const AgentSpec &a : all)
        if (!QStandardPaths::findExecutable(a.binary).isEmpty())
            out << a;
    return out;
}

QString AgentCli::omarchyDefaultAgent()
{
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    QFile f(config + QStringLiteral("/omarchy/defaults/agent"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(f.readLine()).trimmed();
}

// ---- The CLIs' own word on their models and levels ------------------------

QStringList AgentCli::probeArgs(const QString &agent)
{
    if (agent == QLatin1String("claude"))
        return {QStringLiteral("--help")};
    if (agent == QLatin1String("codex"))
        return {QStringLiteral("debug"), QStringLiteral("models")};
    return {};
}

AgentCatalog AgentCli::parseProbe(const QString &agent, const QByteArray &out, const QByteArray &err, int exitCode)
{
    AgentCatalog c;
    if (agent == QLatin1String("claude"))
        c = parseClaudeHelp(QString::fromUtf8(out));
    else if (agent == QLatin1String("codex"))
        c = parseCodexModels(out);
    if (c.isEmpty() && c.error.isEmpty()) {
        const QString command = spec(agent).binary + QLatin1Char(' ') + probeArgs(agent).join(QLatin1Char(' '));
        const QString reason = firstLine(stripAnsi(QString::fromUtf8(err)));
        if (!reason.isEmpty())
            c.error = reason;
        else if (exitCode != 0)
            c.error = QStringLiteral("%1 failed (exit code %2)").arg(command).arg(exitCode);
        else
            c.error = QStringLiteral("%1 named no models").arg(command);
    }
    return c;
}

// `claude --help` says, of --model: "Provide an alias for the latest model
// (e.g. 'fable', 'opus', or 'sonnet') or a model's full name (e.g.
// 'claude-fable-5')", and of --effort: "(low, medium, high, xhigh, max)".
AgentCatalog AgentCli::parseClaudeHelp(const QString &help)
{
    AgentCatalog c;
    const QString text = stripAnsi(help);
    QString model = helpParagraph(text, QStringLiteral("--model"));
    const int fullName = model.indexOf(QLatin1String("full name"));
    if (fullName > 0)
        model.truncate(fullName);
    static const QRegularExpression quoted(QStringLiteral("'([a-z0-9][a-z0-9.-]*)'"));
    for (auto it = quoted.globalMatch(model); it.hasNext();) {
        const QString id = it.next().captured(1);
        AgentModel m;
        m.id = id;
        m.name = id.at(0).toUpper() + id.mid(1);
        c.models << m;
    }
    const QString effort = helpParagraph(text, QStringLiteral("--effort"));
    static const QRegularExpression levels(QStringLiteral("\\(([a-z]+(?:,\\s*[a-z]+)+)\\)"));
    const QRegularExpressionMatch m = levels.match(effort);
    if (m.hasMatch()) {
        const QStringList parts = m.captured(1).split(QLatin1Char(','));
        for (const QString &p : parts)
            c.efforts << p.trimmed();
    }
    for (AgentModel &am : c.models)
        am.efforts = c.efforts;
    return c;
}

// `codex debug models`: {"models":[{"slug","display_name","visibility":
// "list"|"hide","default_reasoning_level","supported_reasoning_levels":
// [{"effort","description"}],"priority"}]}.
AgentCatalog AgentCli::parseCodexModels(const QByteArray &json)
{
    AgentCatalog c;
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (doc.isNull()) {
        c.error = QStringLiteral("codex debug models: %1").arg(error.errorString());
        return c;
    }
    const QJsonArray models = doc.isArray() ? doc.array() : doc.object().value(QStringLiteral("models")).toArray();
    QList<QPair<int, AgentModel>> listed;
    for (const QJsonValue &v : models) {
        const QJsonObject o = v.toObject();
        const QString visibility = o.value(QStringLiteral("visibility")).toString();
        if (!visibility.isEmpty() && visibility != QLatin1String("list"))
            continue;
        if (o.value(QStringLiteral("hidden")).toBool())
            continue;
        AgentModel m;
        m.id = o.value(QStringLiteral("slug")).toString();
        if (m.id.isEmpty())
            continue;
        m.name = o.value(QStringLiteral("display_name")).toString(m.id);
        m.defaultEffort = o.value(QStringLiteral("default_reasoning_level")).toString();
        const QJsonArray levels = o.value(QStringLiteral("supported_reasoning_levels")).toArray();
        for (const QJsonValue &l : levels) {
            const QString effort = l.isObject() ? l.toObject().value(QStringLiteral("effort")).toString() : l.toString();
            if (!effort.isEmpty())
                m.efforts << effort;
        }
        listed.append({o.value(QStringLiteral("priority")).toInt(1 << 20), m});
    }
    std::stable_sort(listed.begin(), listed.end(),
                     [](const QPair<int, AgentModel> &a, const QPair<int, AgentModel> &b) { return a.first < b.first; });
    for (const auto &p : listed)
        c.models << p.second;
    if (!c.models.isEmpty())
        c.efforts = c.models.first().efforts; // the default model's, for "Default"
    return c;
}

AgentCatalog AgentCli::catalog(const QString &agent, bool refresh)
{
    auto &cache = catalogCache();
    if (!refresh && cache.contains(agent))
        return cache.value(agent);
    const AgentSpec a = spec(agent);
    if (!a.isValid())
        return AgentCatalog();
    QProcess p;
    p.setProcessEnvironment(quietEnvironment());
    p.start(a.binary, probeArgs(agent));
    p.closeWriteChannel();
    AgentCatalog c;
    if (!p.waitForFinished(kProbeTimeoutMs)) {
        p.kill();
        c.error = QStringLiteral("%1 %2 did not answer").arg(a.binary, probeArgs(agent).join(QLatin1Char(' ')));
    } else {
        c = parseProbe(agent, p.readAllStandardOutput(), p.readAllStandardError(), p.exitCode());
    }
    cache.insert(agent, c);
    return c;
}

void AgentCli::probeAsync(const QString &agent, QObject *context)
{
    if (catalogCache().contains(agent))
        return;
    const AgentSpec a = spec(agent);
    if (!a.isValid())
        return;
    // Both of the ways this can come to nothing are remembered, so that
    // catalog() does not fall back to the same attempt on the UI thread,
    // where it waits whole seconds for an answer that will not come.
    if (QStandardPaths::findExecutable(a.binary).isEmpty()) {
        AgentCatalog missing;
        missing.error = QStringLiteral("%1 is not on PATH").arg(a.binary);
        catalogCache().insert(agent, missing);
        return;
    }
    auto *p = new QProcess(context);
    p->setProcessEnvironment(quietEnvironment());
    QObject::connect(p, &QProcess::finished, p, [p, agent](int code, QProcess::ExitStatus) {
        // A menu opened meanwhile may have asked itself; its answer stands.
        if (!catalogCache().contains(agent))
            catalogCache().insert(agent, parseProbe(agent, p->readAllStandardOutput(), p->readAllStandardError(), code));
        p->deleteLater();
    });
    QObject::connect(p, &QProcess::errorOccurred, p, [p, agent](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        if (!catalogCache().contains(agent)) {
            AgentCatalog failed;
            failed.error = QStringLiteral("%1 could not be started").arg(p->program());
            catalogCache().insert(agent, failed);
        }
        p->deleteLater();
    });
    p->start(a.binary, probeArgs(agent));
    p->closeWriteChannel();
}

// ---- The choice -------------------------------------------------------------

AgentChoice AgentCli::savedChoice()
{
    QSettings conf;
    AgentChoice c;
    c.agent = conf.value(settings::kAgentName).toString();
    c.model = conf.value(settings::kAgentModel).toString();
    c.effort = conf.value(settings::kAgentEffort).toString();
    const QList<AgentSpec> installed = installedAgents();
    const auto isInstalled = [&](const QString &id) {
        return std::any_of(installed.begin(), installed.end(), [&](const AgentSpec &a) { return a.id == id; });
    };
    if (c.agent.isEmpty() || !isInstalled(c.agent)) {
        // Omarchy's default agent, else whatever is installed: the model and
        // the effort were meant for another agent, so they go.
        const QString omarchy = omarchyDefaultAgent();
        c.agent = isInstalled(omarchy) ? omarchy : installed.isEmpty() ? QString() : installed.first().id;
        c.model.clear();
        c.effort.clear();
    }
    return c;
}

void AgentCli::saveChoice(const AgentChoice &choice)
{
    QSettings conf;
    conf.setValue(settings::kAgentName, choice.agent);
    conf.setValue(settings::kAgentModel, choice.model);
    conf.setValue(settings::kAgentEffort, choice.effort);
}

// ---- The run ----------------------------------------------------------------

QString AgentCli::instructions()
{
    return QStringLiteral(
        "Write the git commit message for the change below. Reply with the message only: "
        "no preamble, no explanation, no quotes, no code fences, no Markdown headings.\n"
        "Format:\n"
        "- Line 1, the subject: imperative mood, as if completing \"This commit will ...\" "
        "(for example \"Add a Generate button to the commit message box\"), at most 72 characters, "
        "no trailing period. It names the essence of the whole change.\n"
        "- Then, when the change delivers more than one thing of value, a blank line and a body: "
        "one \"- \" bullet per thing of value (a feature, a fix, a changed behaviour), each one "
        "short sentence saying what it does and, only if it is not obvious, why. As many bullets "
        "as there are such things, and none for details, for the files touched, or for work that "
        "merely serves another bullet. A change that delivers one thing needs no body.\n"
        "Everything needed is in the diff; do not inspect the repository.\n"
        "The diff:");
}

AgentCommand AgentCli::command(const AgentChoice &choice, const QString &diff, const QString &outputFile)
{
    AgentCommand cmd;
    const AgentSpec agent = spec(choice.agent);
    if (!agent.isValid())
        return cmd;
    cmd.program = agent.binary;
    const QString prompt = instructions();
    if (agent.id == QLatin1String("claude")) {
        // Print mode with no tools: the answer comes on stdout and nothing
        // is saved. Not --bare: that skips reading the saved sign-in too, and
        // answers "Not logged in". `--tools` is variadic, so "--" keeps it
        // from swallowing the prompt.
        cmd.args << QStringLiteral("-p") << QStringLiteral("--no-session-persistence") << QStringLiteral("--output-format")
                 << QStringLiteral("text");
        if (!choice.model.isEmpty())
            cmd.args << QStringLiteral("--model") << choice.model;
        if (!choice.effort.isEmpty())
            cmd.args << QStringLiteral("--effort") << choice.effort;
        cmd.args << QStringLiteral("--tools") << QString() << QStringLiteral("--") << prompt;
        cmd.stdinText = diff;
    } else if (agent.id == QLatin1String("codex")) {
        // A piped stdin is appended to the prompt as a <stdin> block; the
        // final answer lands in the output file (stdout has the log).
        cmd.args << QStringLiteral("exec") << QStringLiteral("--skip-git-repo-check") << QStringLiteral("--ephemeral")
                 << QStringLiteral("--sandbox") << QStringLiteral("read-only") << QStringLiteral("--color")
                 << QStringLiteral("never");
        if (!choice.model.isEmpty())
            cmd.args << QStringLiteral("-m") << choice.model;
        if (!choice.effort.isEmpty())
            cmd.args << QStringLiteral("-c") << QStringLiteral("model_reasoning_effort=\"%1\"").arg(choice.effort);
        if (!outputFile.isEmpty())
            cmd.args << QStringLiteral("-o") << outputFile;
        cmd.args << QStringLiteral("--") << prompt;
        cmd.stdinText = diff;
        cmd.outputFile = outputFile;
    }
    return cmd;
}

QString AgentCli::cleanMessage(const QString &raw)
{
    QStringList lines = stripAnsi(raw).split(QLatin1Char('\n'));
    for (QString &l : lines) {
        // Some CLIs box their answer with a bar down the left.
        static const QRegularExpression bar(QStringLiteral("^[│|]\\s?"));
        l.remove(bar);
        while (!l.isEmpty() && l.back().isSpace())
            l.chop(1);
    }
    while (!lines.isEmpty() && lines.first().trimmed().isEmpty())
        lines.removeFirst();
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
        lines.removeLast();
    if (lines.size() >= 2 && lines.first().startsWith(QLatin1String("```")) && lines.last().startsWith(QLatin1String("```"))) {
        lines.removeFirst();
        lines.removeLast();
    }
    if (!lines.isEmpty()) {
        static const QRegularExpression label(QStringLiteral("^(commit message|message)\\s*:\\s*"),
                                              QRegularExpression::CaseInsensitiveOption);
        lines.first().remove(label);
        if (lines.first().trimmed().isEmpty())
            lines.removeFirst();
    }
    // A subject wrapped over several lines (no blank line before the rest)
    // is one line; a list under it is left alone.
    if (lines.size() >= 2 && !lines.at(1).trimmed().isEmpty()) {
        int end = 1;
        static const QRegularExpression bullet(QStringLiteral("^\\s*([-*•]|\\d+[.)])\\s"));
        while (end < lines.size() && !lines.at(end).trimmed().isEmpty() && !lines.at(end).contains(bullet))
            ++end;
        if (end > 1) {
            QString subject;
            for (int i = 0; i < end; ++i)
                subject += (i ? QStringLiteral(" ") : QString()) + lines.at(i).trimmed();
            lines.erase(lines.begin(), lines.begin() + end);
            lines.prepend(subject);
        }
    }
    QString text = lines.join(QLatin1Char('\n')).trimmed();
    for (const QChar q : {QLatin1Char('"'), QLatin1Char('\''), QLatin1Char('`')}) {
        if (text.size() >= 2 && text.startsWith(q) && text.endsWith(q) && text.count(q) == 2)
            text = text.mid(1, text.size() - 2).trimmed();
    }
    static const QRegularExpression manyBlank(QStringLiteral("\n{3,}"));
    text.replace(manyBlank, QStringLiteral("\n\n"));
    return text;
}

CommitMessageAgent::CommitMessageAgent(QObject *parent)
    : QObject(parent)
{
}

CommitMessageAgent::~CommitMessageAgent()
{
    cancel();
}

void CommitMessageAgent::cancel()
{
    if (!m_process)
        return;
    QProcess *p = m_process;
    m_process = nullptr;
    if (m_previewTimer)
        m_previewTimer->stop(); // no preview may land after the stop
    abandonProcess(p, this);
    p->deleteLater();
    if (!m_outputFile.isEmpty())
        QFile::remove(m_outputFile);
    m_outputFile.clear();
}

// Decode what has arrived, once and for all: a chunk may end in the middle
// of a character, which the decoder carries over into the next one.
void CommitMessageAgent::readMore(QProcess *process)
{
    m_stdout += m_decoder.decode(process->readAllStandardOutput());
}

void CommitMessageAgent::showPartial()
{
    const QString text = AgentCli::cleanMessage(m_stdout);
    if (!text.isEmpty())
        emit partial(text);
}

void CommitMessageAgent::addError(const QString &text)
{
    if (!m_stderr.isEmpty() && !m_stderr.endsWith('\n'))
        m_stderr += '\n';
    m_stderr += text.toUtf8();
}

void CommitMessageAgent::generate(const AgentChoice &choice, const QString &workDir, const QString &diff)
{
    cancel();
    const AgentSpec agent = spec(choice.agent);
    if (!agent.isValid()) {
        emit finished(false, tr("No coding agent is installed (claude or codex)"));
        return;
    }
    m_agentName = agent.name;
    m_stdout.clear();
    m_stderr.clear();
    m_decoder = QStringDecoder(QStringDecoder::Utf8);

    QString outputFile;
    if (agent.id == QLatin1String("codex")) {
        QTemporaryFile tmp(QDir::tempPath() + QStringLiteral("/omagit-message-XXXXXX.txt"));
        tmp.setAutoRemove(false);
        if (!tmp.open()) {
            // Without -o the answer would have to be picked out of codex's
            // own log, which is guesswork; better to say what went wrong.
            emit finished(false, tr("Could not write to %1: %2").arg(QDir::tempPath(), tmp.errorString()));
            return;
        }
        outputFile = tmp.fileName();
    }
    const AgentCommand cmd = AgentCli::command(choice, diff, outputFile);
    m_outputFile = cmd.outputFile;

    auto *p = new QProcess(this);
    m_process = p;
    p->setWorkingDirectory(workDir);
    p->setProcessEnvironment(quietEnvironment());
    if (!m_previewTimer) {
        m_previewTimer = new QTimer(this);
        m_previewTimer->setSingleShot(true);
        m_previewTimer->setInterval(kPreviewIntervalMs);
        connect(m_previewTimer, &QTimer::timeout, this, &CommitMessageAgent::showPartial);
    }
    connect(p, &QProcess::readyReadStandardOutput, this, [this, p] {
        readMore(p);
        // Codex prints its log here, not the message: nothing to preview.
        if (m_outputFile.isEmpty() && !m_previewTimer->isActive())
            m_previewTimer->start();
    });
    connect(p, &QProcess::readyReadStandardError, this, [this, p] { m_stderr += p->readAllStandardError(); });
    connect(p, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        onFinished(code, status == QProcess::CrashExit);
    });
    connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            addError(tr("%1 could not be started").arg(p->program()));
            onFinished(-1, true);
        }
    });
    auto *timeout = new QTimer(p);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, this, [this, p] {
        if (m_process == p) {
            // Replaces, not appends: the first stderr line is what gets shown.
            m_stderr = tr("%1 took longer than %2 seconds").arg(m_agentName).arg(kTimeoutMs / 1000).toUtf8();
            p->kill();
        }
    });
    timeout->start(kTimeoutMs);

    p->start(cmd.program, cmd.args);
    if (!cmd.stdinText.isEmpty())
        p->write(cmd.stdinText.toUtf8());
    p->closeWriteChannel();
}

void CommitMessageAgent::onFinished(int exitCode, bool crashed)
{
    QProcess *p = m_process;
    if (!p)
        return;
    m_process = nullptr;
    if (m_previewTimer)
        m_previewTimer->stop();
    readMore(p);
    m_stderr += p->readAllStandardError();
    p->deleteLater();

    QString text;
    if (!m_outputFile.isEmpty()) {
        QFile f(m_outputFile);
        if (f.open(QIODevice::ReadOnly))
            text = QString::fromUtf8(f.readAll());
        QFile::remove(m_outputFile);
        m_outputFile.clear();
    }
    if (text.trimmed().isEmpty())
        text = m_stdout;
    text = AgentCli::cleanMessage(text);

    // A CLI that reports a problem on stdout and exits 0 all the same.
    static const QRegularExpression complaint(QStringLiteral("^(Not logged in|Please run /login|API Error|Error:|error:)"),
                                              QRegularExpression::MultilineOption);
    const bool ok = exitCode == 0 && !crashed && !text.contains(complaint) && !text.contains(QLatin1String("run /login"));
    if (ok && !text.isEmpty()) {
        emit finished(true, text);
        return;
    }
    QString reason = firstLine(stripAnsi(QString::fromUtf8(m_stderr)));
    if (reason.isEmpty() && !text.isEmpty())
        reason = firstLine(text); // the complaint itself
    if (reason.isEmpty())
        reason = ok ? tr("%1 returned no message").arg(m_agentName)
                    : tr("%1 failed (exit code %2)").arg(m_agentName).arg(exitCode);
    emit finished(false, reason);
}
