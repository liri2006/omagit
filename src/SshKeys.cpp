#include "SshKeys.h"
#include "GitRepo.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocalSocket>
#include <QRegularExpression>

namespace sshkeys {

namespace {
// What a key's algorithm is called where people read it (ssh-keygen -l).
QString typeName(const QString &algorithm)
{
    if (algorithm == QLatin1String("ssh-ed25519"))
        return QStringLiteral("ED25519");
    if (algorithm == QLatin1String("sk-ssh-ed25519@openssh.com"))
        return QStringLiteral("ED25519-SK");
    if (algorithm == QLatin1String("ssh-rsa"))
        return QStringLiteral("RSA");
    if (algorithm == QLatin1String("ssh-dss"))
        return QStringLiteral("DSA");
    if (algorithm.startsWith(QLatin1String("ecdsa-sha2-")))
        return QStringLiteral("ECDSA");
    if (algorithm.startsWith(QLatin1String("sk-ecdsa-sha2-")))
        return QStringLiteral("ECDSA-SK");
    return QString();
}

// Characters a path may carry through a shell unquoted.
bool shellSafe(const QString &text)
{
    static const QRegularExpression safe(QStringLiteral("^[A-Za-z0-9_./~+:@%-]+$"));
    return safe.match(text).hasMatch();
}
} // namespace

QString Key::name() const
{
    return QFileInfo(path).fileName();
}

QString directory()
{
    return QDir::home().filePath(QStringLiteral(".ssh"));
}

bool readPublicKey(const QString &line, Key *key)
{
    const QStringList parts = line.trimmed().split(QRegularExpression(QStringLiteral("\\s+")));
    if (parts.size() < 2)
        return false;
    const QString type = typeName(parts.at(0));
    const QByteArray blob = QByteArray::fromBase64(parts.at(1).toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    if (type.isEmpty() || blob.isEmpty())
        return false;
    if (key) {
        key->type = type;
        key->comment = parts.mid(2).join(QLatin1Char(' '));
        // SHA256 of the key blob, base64 without padding: ssh-keygen -l.
        QByteArray digest = QCryptographicHash::hash(blob, QCryptographicHash::Sha256).toBase64();
        while (digest.endsWith('='))
            digest.chop(1);
        key->fingerprint = QStringLiteral("SHA256:") + QString::fromLatin1(digest);
    }
    return true;
}

QList<Key> find(const QString &dir)
{
    QList<Key> keys;
    const QDir ssh(dir);
    const QStringList publics = ssh.entryList({QStringLiteral("*.pub")}, QDir::Files | QDir::Readable, QDir::Name);
    for (const QString &name : publics) {
        const QString privatePath = ssh.filePath(name.chopped(4));
        if (!QFileInfo(privatePath).isFile())
            continue;
        QFile file(ssh.filePath(name));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        Key key;
        if (!readPublicKey(QString::fromUtf8(file.readLine()), &key))
            continue;
        key.path = privatePath;
        keys << key;
    }
    return keys;
}

bool isDefaultIdentity(const QString &path)
{
    static const QStringList names{QStringLiteral("id_rsa"), QStringLiteral("id_ecdsa"),
                                   QStringLiteral("id_ecdsa_sk"), QStringLiteral("id_ed25519"),
                                   QStringLiteral("id_ed25519_sk"), QStringLiteral("id_xmss"),
                                   QStringLiteral("id_dsa")};
    const QFileInfo info(path);
    return names.contains(info.fileName()) && QDir(info.absolutePath()) == QDir(directory());
}

QString sshCommand(const QString &keyPath)
{
    QString quoted = keyPath;
    if (!shellSafe(quoted)) {
        quoted.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
        quoted = QLatin1Char('\'') + quoted + QLatin1Char('\'');
    }
    return QStringLiteral("ssh -i ") + quoted + QStringLiteral(" -o IdentitiesOnly=yes");
}

QString keyOf(const QString &command)
{
    // -i and its argument, quoted the way sshCommand() quotes it, in double
    // quotes, or bare.
    static const QRegularExpression option(
        QStringLiteral("(?:^|\\s)-i\\s*((?:'[^']*'(?:\\\\''[^']*')*)|\"[^\"]*\"|[^\\s'\"]+)"));
    const auto match = option.match(command);
    if (!match.hasMatch())
        return QString();
    QString value = match.captured(1);
    if (value.startsWith(QLatin1Char('\''))) {
        value.replace(QStringLiteral("'\\''"), QStringLiteral("\x01"));
        value.remove(QLatin1Char('\''));
        value.replace(QChar(0x01), QLatin1Char('\''));
    } else if (value.startsWith(QLatin1Char('"'))) {
        value = value.mid(1, value.size() - 2);
    }
    return value;
}

bool isSshUrl(const QString &url)
{
    const QString value = url.trimmed();
    for (const char *scheme : {"ssh://", "git+ssh://", "ssh+git://"})
        if (value.startsWith(QLatin1String(scheme), Qt::CaseInsensitive))
            return true;
    if (value.contains(QLatin1String("://")) || value.startsWith(QLatin1Char('/')) || value.startsWith(QLatin1Char('.')))
        return false;
    // git's scp form: a colon before any slash.
    static const QRegularExpression scp(QStringLiteral("^(?:[^/@:]+@)?(?:\\[[^\\]]+\\]|[^/:]+):"));
    return scp.match(value).hasMatch();
}

bool agentReachable()
{
    const QString path = qEnvironmentVariable("SSH_AUTH_SOCK");
    if (path.isEmpty())
        return false;
    // An agent answers at once; a socket file left behind by one that is gone
    // refuses. Nothing is asked of it.
    QLocalSocket probe;
    probe.connectToServer(path);
    const bool live = probe.waitForConnected(200);
    probe.abort();
    return live;
}

QString overridingVariable()
{
    // Set at all counts, empty included: git takes an empty GIT_SSH_COMMAND
    // over core.sshCommand as well. GIT_SSH does not count — git only looks
    // at it when no command is configured, which a chosen key is.
    if (qEnvironmentVariableIsSet("GIT_SSH_COMMAND"))
        return QStringLiteral("GIT_SSH_COMMAND");
    return QString();
}

QString repoCommand(GitRepo *repo)
{
    if (!repo)
        return QString();
    return QString::fromUtf8(repo->run({QStringLiteral("config"), QStringLiteral("--get"),
                                        QStringLiteral("core.sshCommand")}))
        .trimmed();
}

bool setRepoKey(GitRepo *repo, const QString &keyPath, QString *error)
{
    if (!repo)
        return false;
    int code = 0;
    QByteArray err;
    if (keyPath.isEmpty()) {
        repo->run({QStringLiteral("config"), QStringLiteral("--unset"), QStringLiteral("core.sshCommand")}, &code, &err);
        // 5: there was nothing to remove, which is as good.
        if (code == 0 || code == 5)
            return true;
    } else {
        repo->run({QStringLiteral("config"), QStringLiteral("core.sshCommand"), sshCommand(keyPath)}, &code, &err);
        if (code == 0)
            return true;
    }
    if (error)
        *error = QString::fromUtf8(err).trimmed();
    return false;
}

} // namespace sshkeys
