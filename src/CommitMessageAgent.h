#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

class QProcess;

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

// Writes a commit message from a diff with one of the coding agents installed
// on the machine, the way an editor's "generate commit message" button does.
class CommitMessageAgent : public QObject
{
    Q_OBJECT
public:
    explicit CommitMessageAgent(QObject *parent = nullptr);
    ~CommitMessageAgent() override;

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

    // The command line for `choice`. `stdinText` is what to feed the process,
    // `outputFile` a file the agent writes its answer to (empty when it
    // comes on stdout).
    struct Command {
        QString program;
        QStringList args;
        QString stdinText;
        QString outputFile;
    };
    static Command command(const AgentChoice &choice, const QString &diff, const QString &outputFile);

    // The agent's answer without the trimmings agents add: colour codes,
    // code fences, quotes, a label, a subject wrapped over several lines.
    static QString cleanMessage(const QString &raw);

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
    static QStringList probeArgs(const QString &agent);
    static AgentCatalog parseProbe(const QString &agent, const QByteArray &out, const QByteArray &err, int exitCode);

    QPointer<QProcess> m_process;
    QString m_outputFile;
    QByteArray m_stdout, m_stderr;
    QString m_agentName;
};
