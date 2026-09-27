#include "AgentKeeper.h"
#include "ProcessUtil.h"

#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

namespace {
// ssh-add answers in milliseconds once it has the passphrase; an agent that
// wants a confirmation of its own gets this long.
constexpr int kAddTimeoutMs = 30000;
} // namespace

AgentKeeper::AgentKeeper(QObject *parent)
    : QObject(parent), m_askPass(new AskPass(this))
{
    // ssh-add asks for the passphrase once; asked again, the one it got did
    // not open the key after all, and it is not tried a second time.
    connect(m_askPass, &AskPass::requestReceived, this, [this](const AskPassRequest &request) {
        if (m_asked) {
            m_askPass->cancel(request.id);
            return;
        }
        m_asked = true;
        m_askPass->answerSecret(request.id, m_passphrase);
    });
}

AgentKeeper::~AgentKeeper()
{
    abandonProcess(m_process, this);
}

void AgentKeeper::setHelperPath(const QString &path)
{
    m_askPass->setHelperPath(path);
}

void AgentKeeper::add(const QList<AgentKey> &keys)
{
    if (keys.isEmpty())
        return;
    const bool idle = m_queue.isEmpty() && !m_process;
    m_queue << keys;
    if (idle)
        next();
}

void AgentKeeper::next()
{
    if (m_queue.isEmpty()) {
        const QStringList errors = m_errors;
        m_errors.clear();
        emit finished(errors);
        return;
    }
    const AgentKey key = m_queue.takeFirst();
    const QString sshAdd = QStandardPaths::findExecutable(QStringLiteral("ssh-add"));
    if (sshAdd.isEmpty() || !m_askPass->listen()) {
        m_errors << (sshAdd.isEmpty() ? tr("ssh-add was not found") : tr("no askpass socket for ssh-add"));
        next();
        return;
    }
    m_passphrase = key.passphrase;
    m_asked = false;

    auto *process = new QProcess(this);
    m_process = process;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const QString &entry : m_askPass->env()) {
        const int eq = entry.indexOf(QLatin1Char('='));
        env.insert(entry.left(eq), entry.mid(eq + 1));
    }
    process->setProcessEnvironment(env);
    connect(process, &QProcess::finished, this, [this, process, key](int code, QProcess::ExitStatus status) {
        m_askPass->endOperation();
        m_passphrase.clear();
        if (status != QProcess::NormalExit || code != 0) {
            const QString said = QString::fromUtf8(process->readAllStandardError()).trimmed();
            m_errors << (said.isEmpty() ? tr("ssh-add could not add %1").arg(key.path) : said);
        }
        process->deleteLater();
        m_process = nullptr;
        next();
    });
    // No finished() comes for a program that never started.
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_askPass->endOperation();
        m_passphrase.clear();
        m_errors << tr("ssh-add could not be started: %1").arg(process->errorString());
        process->deleteLater();
        m_process = nullptr;
        next();
    });
    QTimer::singleShot(kAddTimeoutMs, process, [process] { process->kill(); });
    process->start(sshAdd, {key.path});
}
