#include "CredentialKeeper.h"

#include <QDeadlineTimer>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>

#include <memory>

namespace {
// Config writes and a helper's store answer in milliseconds; a keyring that
// wants unlocking waits for the user, which is what the longer one allows.
constexpr int kConfigTimeoutMs = 5000;
constexpr int kApproveTimeoutMs = 120000;
// How long the keeper, going away, waits for logins still being handed over.
constexpr int kShutdownWaitMs = 3000;

QString &helperName()
{
    static QString name = QStringLiteral("libsecret");
    return name;
}

QString gitExecutable()
{
    const QString git = QStandardPaths::findExecutable(QStringLiteral("git"));
    return git.isEmpty() ? QStringLiteral("git") : git;
}

struct Run {
    int code = -1;
    QByteArray out, err;
};

// git with nothing to ask anybody, from the file system's root so that no
// repository's configuration comes into it: only the global one is written.
Run git(const QStringList &args, const QByteArray &input = QByteArray(), int timeoutMs = kConfigTimeoutMs)
{
    QProcess p;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    for (const char *name : {"GIT_ASKPASS", "SSH_ASKPASS", "OMAGIT_ASKPASS_SOCKET"})
        env.remove(QString::fromLatin1(name));
    p.setProcessEnvironment(env);
    p.setWorkingDirectory(QDir::rootPath());
    p.start(gitExecutable(), args);
    Run run;
    if (!p.waitForStarted(kConfigTimeoutMs)) {
        run.err = p.errorString().toUtf8();
        return run;
    }
    if (!input.isEmpty())
        p.write(input);
    p.closeWriteChannel();
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(1000);
        run.err = "timed out";
        return run;
    }
    run.code = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
    run.out = p.readAllStandardOutput();
    run.err = p.readAllStandardError();
    return run;
}

// `git credential <action>` (approve or reject) with `helper` as the only
// helper there is. Command-line entries are read after every file, so the
// empty one resets whatever the user configures — for every URL — and only
// `helper` is handed the login. A context with a path is stored under it,
// which git only does with credential.useHttpPath on; that context came from
// a prompt that carried the path, so git asks for it with the path again.
// Empty when the helper took it, else what went wrong.
QString handOver(const QString &action, const KeptLogin &login, const QString &helper)
{
    QStringList args{QStringLiteral("-c"), QStringLiteral("credential.helper="),
                     QStringLiteral("-c"), QStringLiteral("credential.helper=") + helper};
    const QString path = QUrl(login.context).path();
    if (!path.isEmpty() && path != QLatin1String("/"))
        args << QStringLiteral("-c") << QStringLiteral("credential.useHttpPath=true");
    args << QStringLiteral("credential") << action;
    // git's own description of a credential: the URL (which carries the
    // scheme, the host, the port and a path where there is one), then who.
    const QByteArray description = "url=" + login.context.toUtf8() + "\nusername=" + login.username.toUtf8()
        + "\npassword=" + login.password.toUtf8() + "\n\n";
    const Run run = git(args, description, kApproveTimeoutMs);
    // git hands the login to the helper and carries on whatever it says; a
    // helper that could not do as asked says so on stderr.
    const QString error = QString::fromUtf8(run.err).trimmed();
    if (run.code == 0 && error.isEmpty())
        return QString();
    return error.isEmpty() ? QObject::tr("git credential %1 failed").arg(action) : error;
}

// One login the whole way: stored by the helper first, then the helper named
// for its server. In that order no entry ever points at a helper that has
// nothing for it: a store that fails leaves the configuration as it was, and
// an entry that cannot be written takes the stored login back out.
//
// A keyring item the helper already had for exactly this server and user is
// replaced by the store; should the entry then fail, the reject removes the
// replacement and the old item is not put back. That is accepted: the box is
// only offered when no helper covers the remote, so that item was not in use,
// and the password just typed is the one that worked.
//
// Empty when it was kept, else what went wrong.
QString keepOne(const KeptLogin &login, const QString &helper)
{
    const QString key = CredentialKeeper::configKey(login.context);
    if (key.isEmpty())
        return QObject::tr("%1 is not a URL").arg(login.context);
    const QString stored = handOver(QStringLiteral("approve"), login, helper);
    if (!stored.isEmpty())
        return stored; // nothing configured, nothing to take back
    // Already named there — by hand, or by an earlier sign-in to the server —
    // so there is nothing to add.
    const QStringList named = QString::fromUtf8(git({QStringLiteral("config"), QStringLiteral("--global"),
                                                     QStringLiteral("--get-all"), key})
                                                    .out)
                                  .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (named.contains(helper))
        return QString();
    const Run set = git({QStringLiteral("config"), QStringLiteral("--global"), QStringLiteral("--add"), key, helper});
    if (set.code == 0)
        return QString();
    QString error = QString::fromUtf8(set.err).trimmed();
    if (error.isEmpty())
        error = QObject::tr("git config --global --add %1 failed").arg(key);
    // Git would never ask the helper for it, so the keyring is not to keep it.
    if (!handOver(QStringLiteral("reject"), login, helper).isEmpty())
        error += QObject::tr("; the login stored in the keyring could not be taken back out");
    return error;
}
} // namespace

CredentialKeeper::CredentialKeeper(QObject *parent)
    : QObject(parent)
{
}

CredentialKeeper::~CredentialKeeper()
{
    // One deadline for all of them: a window closing waits seconds, not
    // seconds per login.
    const QDeadlineTimer deadline(kShutdownWaitMs);
    for (QThread *thread : std::as_const(m_threads)) {
        if (thread->wait(deadline)) {
            delete thread;
            continue;
        }
        // Still at it — a keyring waiting to be unlocked, most likely. It
        // finishes on its own, tells nobody, and deletes itself; the work it
        // runs holds nothing of this object's.
        disconnect(thread, nullptr, this, nullptr);
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    }
}

QString CredentialKeeper::helper()
{
    return helperName();
}

void CredentialKeeper::setHelper(const QString &name)
{
    helperName() = name;
}

bool CredentialKeeper::helperAvailable()
{
    const QString program = QStringLiteral("git-credential-") + helper();
    const QString execPath = QString::fromUtf8(git({QStringLiteral("--exec-path")}).out).trimmed();
    if (!execPath.isEmpty() && QFileInfo(QDir(execPath).filePath(program)).isExecutable())
        return true;
    return !QStandardPaths::findExecutable(program).isEmpty();
}

QString CredentialKeeper::configKey(const QString &context)
{
    const QUrl url(context);
    if (!url.isValid() || url.scheme().isEmpty() || url.host().isEmpty())
        return QString();
    QUrl server;
    server.setScheme(url.scheme());
    server.setHost(url.host());
    server.setPort(url.port());
    return QStringLiteral("credential.") + server.toString() + QStringLiteral(".helper");
}

QStringList CredentialKeeper::keepNow(const QList<KeptLogin> &logins)
{
    return keepAll(logins, helper());
}

QStringList CredentialKeeper::keepAll(const QList<KeptLogin> &logins, const QString &name)
{
    QStringList errors;
    for (const KeptLogin &login : logins) {
        const QString error = keepOne(login, name);
        if (!error.isEmpty())
            errors << error;
    }
    return errors;
}

void CredentialKeeper::keep(const QList<KeptLogin> &logins)
{
    if (logins.isEmpty())
        return;
    auto errors = std::make_shared<QStringList>();
    const QString name = helper(); // read here: a test may change it meanwhile
    QThread *thread = QThread::create([logins, name, errors] { *errors = keepAll(logins, name); });
    m_threads << thread;
    connect(thread, &QThread::finished, this, [this, thread, errors] {
        m_threads.removeAll(thread);
        thread->deleteLater();
        emit finished(*errors);
    });
    thread->start();
}
