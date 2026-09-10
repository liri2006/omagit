#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringConverter>
#include <QStringList>

class QProcess;
class QTimer;

// A coding agent CLI that can be asked for a commit message without a
// terminal. Two are known: Claude Code and Codex.
struct AgentSpec {
    QString id;     // "claude", as Omarchy names it
    QString name;   // "Claude Code"
    QString binary; // the executable

    bool isValid() const { return !id.isEmpty(); }
};

// A model the agent's CLI offers.
struct AgentModel {
    QString id;          // what --model / -m takes: "opus", "gpt-6-astra"
    QString name;        // "Opus", "GPT-6-Astra"
    QStringList efforts; // reasoning levels for this model (Codex has them per model); empty: the agent's
    QString defaultEffort;
};

// What the CLI told about itself; models and levels come from the CLI, not
// from a list of our own: `claude --help` names the model aliases and the
// --effort levels, `codex debug models` prints the model catalog with the
// reasoning levels of each model.
struct AgentCatalog {
    QList<AgentModel> models;
    QStringList efforts; // the agent-wide levels (Claude), or those of the first model (Codex)
    QString error;       // why the probe brought nothing, if it did

    bool isEmpty() const { return models.isEmpty() && efforts.isEmpty(); }
    // The levels that apply with `model` chosen (empty: the agent's default model).
    QStringList effortsFor(const QString &model) const;
};

// What the user picked in the settings menu. An empty model or effort means
// the agent's own default.
struct AgentChoice {
    QString agent;
    QString model;
    QString effort;
};

// The command line for a choice. `stdinText` is what to feed the process,
// `outputFile` a file the agent writes its answer to (empty when it comes
// on stdout).
struct AgentCommand {
    QString program;
    QStringList args;
    QString stdinText;
    QString outputFile;
};

// Everything about the agent CLIs that is not a process of our own to
// watch: which agents there are, what they say when asked about
// themselves, what to ask them for a commit message, and how to read the
// answer. All static and free of any object's state, so it can be had (and
// tested) without one.
class AgentCli
{
public:
    static QList<AgentSpec> agents();
    static AgentSpec spec(const QString &id);
    // The agents whose executable is on PATH, in agents() order.
    static QList<AgentSpec> installedAgents();
    // Omarchy's default agent (~/.config/omarchy/defaults/agent).
    static QString omarchyDefaultAgent();

    // The CLI's models and levels, asked of the CLI once per run of the
    // program and kept (`refresh` asks again). Takes up to a second the
    // first time; probeAsync() does it ahead of time.
    static AgentCatalog catalog(const QString &agent, bool refresh = false);
    static void probeAsync(const QString &agent, QObject *context);
    // The parsers behind catalog(), on the CLI's output.
    static AgentCatalog parseClaudeHelp(const QString &help);
    static AgentCatalog parseCodexModels(const QByteArray &json);

    // The saved choice, with the agent filled in from Omarchy's default (or
    // the first installed one) when none was saved or the saved one is gone.
    static AgentChoice savedChoice();
    static void saveChoice(const AgentChoice &choice);

    // The instructions sent along with the diff.
    static QString instructions();
    static AgentCommand command(const AgentChoice &choice, const QString &diff, const QString &outputFile);

    // The agent's answer without the trimmings agents add: colour codes,
    // code fences, quotes, a label, a subject wrapped over several lines.
    static QString cleanMessage(const QString &raw);

private:
    static QStringList probeArgs(const QString &agent);
    static AgentCatalog parseProbe(const QString &agent, const QByteArray &out, const QByteArray &err, int exitCode);
};

// Writes a commit message from a diff with one of the coding agents installed
// on the machine, the way an editor's "generate commit message" button does.
// It runs the process and reads what comes back; everything it needs to know
// about the CLIs themselves it asks AgentCli, which the static members below
// forward to unchanged.
class CommitMessageAgent : public QObject
{
    Q_OBJECT
public:
    explicit CommitMessageAgent(QObject *parent = nullptr);
    ~CommitMessageAgent() override;

    using Command = AgentCommand;

    static QList<AgentSpec> agents() { return AgentCli::agents(); }
    static AgentSpec spec(const QString &id) { return AgentCli::spec(id); }
    static QList<AgentSpec> installedAgents() { return AgentCli::installedAgents(); }
    static QString omarchyDefaultAgent() { return AgentCli::omarchyDefaultAgent(); }
    static AgentCatalog catalog(const QString &agent, bool refresh = false) { return AgentCli::catalog(agent, refresh); }
    static void probeAsync(const QString &agent, QObject *context) { AgentCli::probeAsync(agent, context); }
    static AgentCatalog parseClaudeHelp(const QString &help) { return AgentCli::parseClaudeHelp(help); }
    static AgentCatalog parseCodexModels(const QByteArray &json) { return AgentCli::parseCodexModels(json); }
    static AgentChoice savedChoice() { return AgentCli::savedChoice(); }
    static void saveChoice(const AgentChoice &choice) { AgentCli::saveChoice(choice); }
    static QString instructions() { return AgentCli::instructions(); }
    static Command command(const AgentChoice &choice, const QString &diff, const QString &outputFile)
    {
        return AgentCli::command(choice, diff, outputFile);
    }
    static QString cleanMessage(const QString &raw) { return AgentCli::cleanMessage(raw); }

    bool running() const { return m_process != nullptr; }
    // Starts the agent in `workDir`; finished() reports the outcome. A run
    // already going is cancelled first.
    void generate(const AgentChoice &choice, const QString &workDir, const QString &diff);
    void cancel();

signals:
    // The message so far (agents that stream print it as it comes).
    void partial(const QString &text);
    // `text` is the message, or the reason when `ok` is false. Not emitted for a cancel.
    void finished(bool ok, const QString &text);

private:
    void onFinished(int exitCode, bool crashed);
    void readMore(QProcess *process);
    void showPartial();
    // Keeps what the process itself reported: our own note goes after it.
    void addError(const QString &text);

    QPointer<QProcess> m_process;
    QString m_outputFile;
    QStringDecoder m_decoder{QStringDecoder::Utf8};
    QString m_stdout; // decoded as it arrives, so the preview costs one pass
    QByteArray m_stderr;
    QTimer *m_previewTimer = nullptr; // coalesces a burst of output into one preview
    QString m_agentName;
};
