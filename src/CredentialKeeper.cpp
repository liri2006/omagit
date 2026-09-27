#include "CredentialKeeper.h"

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

// One login the whole way: the helper named for its server, then the login
// handed to git. Empty when it was kept, else what went wrong.
QString keepOne(const KeptLogin &login, const QString &helper)
{
    const QString key = CredentialKeeper::configKey(login.context);
    if (key.isEmpty())
        return QObject::tr("%1 is not a URL").arg(login.context);
    // Already named there — by hand, or by an earlier sign-in whose store
    // failed after all — so there is nothing to add, and nothing to take back.
    const QStringList named = QString::fromUtf8(git({QStringLiteral("config"), QStringLiteral("--global"),
                                                     QStringLiteral("--get-all"), key})
                                                    .out)
                                  .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    bool added = false;
    if (!named.contains(helper)) {
        const Run set = git({QStringLiteral("config"), QStringLiteral("--global"), QStringLiteral("--add"), key, helper});
        if (set.code != 0)
            return QString::fromUtf8(set.err).trimmed();
        added = true;
    }
    // git's own description of a credential: the URL (which carries the
    // scheme, the host, the port and a path where there is one), then who.
    const QByteArray description = "url=" + login.context.toUtf8() + "\nusername=" + login.username.toUtf8()
        + "\npassword=" + login.password.toUtf8() + "\n\n";
    const Run approve = git({QStringLiteral("credential"), QStringLiteral("approve")}, description, kApproveTimeoutMs);
    // git hands the login to every helper and carries on whatever they say;
    // a helper that could not store it says so on stderr.
    const QString error = QString::fromUtf8(approve.err).trimmed();
    if (approve.code == 0 && error.isEmpty())
        return QString();
    if (added)
        git({QStringLiteral("config"), QStringLiteral("--global"), QStringLiteral("--fixed-value"),
             QStringLiteral("--unset"), key, helper});
    return error.isEmpty() ? QObject::tr("git credential approve failed") : error;
}
} // namespace

CredentialKeeper::CredentialKeeper(QObject *parent)
    : QObject(parent)
{
}

CredentialKeeper::~CredentialKeeper()
{
    for (QThread *thread : std::as_const(m_threads)) {
        thread->wait();
        delete thread;
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
