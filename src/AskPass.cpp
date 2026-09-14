#include "AskPass.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QIODevice>
#include <QLocalServer>
#include <QLocalSocket>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QUuid>

namespace {

// The wire: one length-prefixed UTF-8 frame each way. A prompt may carry
// anything (ssh's host-key question runs over several lines), so nothing is
// delimited by newlines. The answer's first character says whether there is
// one: 'A' and the value, or 'C' for a cancelled sign-in.
constexpr int kHeader = 4;
constexpr int kMaxFrame = 64 * 1024;
constexpr char kAnswer = 'A', kCancelled = 'C';

// How long the helper waits: connecting is a local matter and must not hold
// git up when the app is gone, while the answer waits on a person, who may
// well be looking a token up somewhere. It gives up in the end so that no
// git process is left hanging for good.
constexpr int kConnectTimeoutMs = 5000;
constexpr int kAnswerTimeoutMs = 10 * 60 * 1000;
constexpr int kPollMs = 250;

QByteArray frame(const QString &text)
{
    const QByteArray payload = text.toUtf8();
    QByteArray out;
    QDataStream stream(&out, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << quint32(payload.size());
    out += payload;
    return out;
}

// Takes the first complete frame out of `buffer`; false while one is still
// arriving. A frame beyond the limit is a wrong protocol, not a prompt:
// `text` stays empty and the buffer is dropped.
bool takeFrame(QByteArray &buffer, QString *text)
{
    if (buffer.size() < kHeader)
        return false;
    QDataStream stream(buffer);
    stream.setByteOrder(QDataStream::BigEndian);
    quint32 length = 0;
    stream >> length;
    if (length > kMaxFrame) {
        buffer.clear();
        *text = QString();
        return true;
    }
    if (quint32(buffer.size()) < kHeader + length)
        return false;
    *text = QString::fromUtf8(buffer.mid(kHeader, int(length)));
    buffer.remove(0, kHeader + int(length));
    return true;
}

// The directory the socket lives in: the session's runtime directory, which
// is private to the user and cleaned up at logout, or /tmp when there is none.
QString socketDir()
{
    const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    return runtime.isEmpty() ? QDir::tempPath() : runtime;
}

QString socketPrefix()
{
    return QStringLiteral("omagit-askpass-");
}

// Sockets of earlier runs that ended without cleaning up (a crash, a kill).
// A file named after a pid that is gone is nobody's socket any more.
void removeStaleSockets(const QString &dir, const QString &keep)
{
    const QStringList names = QDir(dir).entryList({socketPrefix() + QStringLiteral("*")}, QDir::System | QDir::Files);
    for (const QString &name : names) {
        const QString path = dir + QLatin1Char('/') + name;
        if (path == keep)
            continue;
        bool isPid = false;
        const qint64 pid = name.mid(socketPrefix().size()).section(QLatin1Char('-'), 0, 0).toLongLong(&isPid);
        if (isPid && !QFile::exists(QStringLiteral("/proc/%1").arg(pid)))
            QLocalServer::removeServer(path);
    }
}

// What "this was asked before" is measured by: the credential context and the
// user it was about. A password prompt for another user of the same host is a
// question of its own, not the same one put twice.
QString answeredKey(const AskPassRequest &request)
{
    return request.context + QLatin1Char('\n') + request.user;
}

} // namespace

// --- What was asked --------------------------------------------------------

AskPassRequest parseAskPassPrompt(const QString &prompt)
{
    AskPassRequest r;
    r.prompt = prompt;
    // Until something better turns up below, a prompt stands for itself: two
    // questions worded alike are the same question, and nothing else is.
    r.context = prompt;
    const QString text = prompt.trimmed();
    const QString lower = text.toLower();
    if (lower.startsWith(QLatin1String("username")))
        r.kind = AskPassRequest::Username;
    else if (lower.startsWith(QLatin1String("password")))
        r.kind = AskPassRequest::Password;
    else if (lower.contains(QLatin1String("passphrase")))
        r.kind = AskPassRequest::Passphrase;
    else
        return r; // a question of git's or ssh's own: it is shown as it stands

    // What stands between the first and the last quote: the URL git asks
    // about, or the key file ssh wants unlocked. Both use single quotes.
    const int open = text.indexOf(QLatin1Char('\''));
    const int close = text.lastIndexOf(QLatin1Char('\''));
    if (open >= 0 && close > open)
        r.target = text.mid(open + 1, close - open - 1);

    if (r.kind == AskPassRequest::Passphrase) {
        r.keyPath = r.target;
        r.context = r.target;
        return r;
    }
    if (r.target.isEmpty())
        return r;
    r.context = r.target;
    const QUrl url(r.target);
    if (url.isValid() && !url.host().isEmpty()) {
        r.host = url.host();
        r.user = url.userName();
        // Everything of the URL but who is signing in: what git would hand a
        // credential helper, and what one answer may be reused for.
        r.context = url.toString(QUrl::RemoveUserInfo | QUrl::RemoveQuery | QUrl::RemoveFragment);
    }
    return r;
}

// --- The helper process ----------------------------------------------------

int askPassClient(const QString &socketPath, const QString &prompt, QIODevice *out)
{
    if (socketPath.isEmpty() || !out)
        return 1;
    QLocalSocket socket;
    socket.connectToServer(socketPath);
    if (!socket.waitForConnected(kConnectTimeoutMs))
        return 1;
    socket.write(frame(prompt));
    if (!socket.waitForBytesWritten(kConnectTimeoutMs))
        return 1;

    QByteArray buffer;
    QString reply;
    QElapsedTimer clock;
    clock.start();
    forever {
        if (takeFrame(buffer, &reply))
            break;
        if (clock.elapsed() >= kAnswerTimeoutMs)
            return 1;
        if (!socket.waitForReadyRead(kPollMs)) {
            // The app went away (or was killed) with the dialog open.
            if (socket.state() != QLocalSocket::ConnectedState)
                return 1;
            continue;
        }
        buffer += socket.readAll();
    }
    if (reply.isEmpty() || reply.at(0) != QLatin1Char(kAnswer))
        return 1;
    out->write(reply.mid(1).toUtf8());
    out->write("\n");
    return 0;
}

// --- The app's end ---------------------------------------------------------

AskPass::AskPass(QObject *parent)
    : QObject(parent), m_helper(QCoreApplication::applicationFilePath())
{
    // Clone dialogs and remote sync can coexist in one process. Each owner
    // needs its own socket, so neither steals the other's credential prompts.
    m_socketPath = socketDir() + QLatin1Char('/') + socketPrefix()
        + QString::number(QCoreApplication::applicationPid()) + QLatin1Char('-')
        + QUuid::createUuid().toString(QUuid::Id128);
}

AskPass::~AskPass()
{
    // Only remove a socket this object successfully opened.
    if (!listening())
        return;
    m_server->close();
    QLocalServer::removeServer(m_socketPath);
}

bool AskPass::listening() const
{
    return m_server && m_server->isListening();
}

bool AskPass::listen()
{
    if (listening())
        return true;
    removeStaleSockets(socketDir(), m_socketPath);
    QLocalServer::removeServer(m_socketPath); // an earlier failed listen by this instance
    if (!m_server) {
        m_server = new QLocalServer(this);
        m_server->setSocketOptions(QLocalServer::UserAccessOption);
        connect(m_server, &QLocalServer::newConnection, this, &AskPass::onNewConnection);
    }
    if (!m_server->listen(m_socketPath)) {
        qWarning("omagit: cannot listen on %s: %s", qPrintable(m_socketPath),
                 qPrintable(m_server->errorString()));
        return false;
    }
    return true;
}

QStringList AskPass::env() const
{
    if (!listening())
        return {};
    // SSH_ASKPASS_REQUIRE=force: ssh asks us whatever else it has to hand.
    // "prefer" would not do — it still wants DISPLAY set, and under Wayland
    // there is none — and the terminal the app inherits is no place to ask:
    // nobody is looking at it, so the prompt would go unanswered.
    return {QStringLiteral("GIT_ASKPASS=") + m_helper,
            QStringLiteral("SSH_ASKPASS=") + m_helper,
            QStringLiteral("SSH_ASKPASS_REQUIRE=force"),
            QStringLiteral("OMAGIT_ASKPASS_SOCKET=") + m_socketPath};
}

void AskPass::onNewConnection()
{
    while (QLocalSocket *socket = m_server->nextPendingConnection()) {
        // The helper went away before it was answered — git was killed, or it
        // gave up waiting. Its question is moot and the next one may go ahead.
        // Whoever is showing that question is told so by id: the dialog for it
        // goes, and an answer given to it after this names a request that is
        // over and is turned away rather than handed to the next one.
        connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
            if (socket == m_current) {
                const int dropped = m_request.id;
                m_current = nullptr;
                emit requestDropped(dropped);
                emit answered();
            }
            m_waiting.removeAll(socket);
            socket->deleteLater();
            QTimer::singleShot(0, this, &AskPass::serveNext);
        });
        connect(socket, &QLocalSocket::readyRead, this, &AskPass::serveNext);
        m_waiting.append(socket);
    }
    serveNext();
}

// One prompt at a time: git asks for the username and the password one after
// the other, and a second helper (a submodule's fetch, say) waits its turn
// rather than opening a dialog on top of the one on screen.
void AskPass::serveNext()
{
    if (m_current || m_waiting.isEmpty())
        return;
    QLocalSocket *socket = m_waiting.constFirst();
    QByteArray buffer = socket->peek(kHeader + kMaxFrame);
    const qint64 peeked = buffer.size();
    QString prompt;
    if (!takeFrame(buffer, &prompt))
        return; // the frame is still arriving; readyRead brings us back
    socket->read(peeked - buffer.size()); // take the frame out, leave the rest
    m_waiting.removeFirst();
    m_current = socket;
    m_request = parseAskPassPrompt(prompt);
    m_request.id = ++m_nextId; // what an answer to this one, and no other, names

    // The user turned this operation's sign-in down: git carries on to the
    // next remote regardless and asks again, and it is not the user's business
    // to say no twice. Turned down where it arrives, until endOperation().
    if (m_cancelled) {
        reply(QString(), false);
        return;
    }

    // Signed in to this context already in this operation: git's next question
    // (the password, the moment the username comes back) and the prompts of
    // every further remote sharing the context in a `fetch --all` are all
    // answered from that one sign-in, without a dialog. The login stays until
    // the operation ends — git never asks twice for credentials it refused.
    //
    // Only for the user it was given for, though: a prompt that names somebody
    // else is a sign-in of its own, and answering it out of this one would put
    // the wrong person's password on the wire.
    const auto login = m_logins.constFind(m_request.context);
    const bool isLogin = m_request.kind == AskPassRequest::Username || m_request.kind == AskPassRequest::Password;
    const bool ours = login != m_logins.constEnd()
        && (m_request.user.isEmpty() || m_request.user == login->username);
    if (isLogin && ours) {
        reply(m_request.kind == AskPassRequest::Username ? login->username : login->password, true);
        return;
    }
    // Nothing kept answers this one. Asked again for something already
    // answered, then, means the answer was not accepted — ssh's second and
    // third try at a key passphrase — and the dialog says so.
    m_request.retry = m_answered.contains(answeredKey(m_request));
    emit requestReceived(m_request);
}

void AskPass::reply(const QString &value, bool ok)
{
    QLocalSocket *socket = m_current;
    m_current = nullptr;
    if (socket) {
        disconnect(socket, nullptr, this, nullptr);
        socket->write(frame(QString(QLatin1Char(ok ? kAnswer : kCancelled)) + value));
        socket->flush();
        socket->disconnectFromServer();
        socket->deleteLater();
    }
    emit answered();
    // Not from inside this call: an answer given straight from the
    // requestReceived handler would otherwise nest one request in the next.
    QTimer::singleShot(0, this, &AskPass::serveNext);
}

// Whether `id` still names the question on the table. A dialog outlives its
// request when the helper behind it goes away, and what is typed into it
// afterwards belongs to nothing: it is not an answer to whatever git has
// asked since, which may be another remote or another user altogether.
bool AskPass::isCurrent(int id) const
{
    return m_current && id == m_request.id;
}

void AskPass::answerLogin(int id, const QString &username, const QString &password)
{
    if (!isCurrent(id))
        return;
    // Whose sign-in this is: the name typed at a username prompt, or the user
    // a password prompt's URL named — the dialog shows that one read-only and
    // hands it straight back, and falling back on the request covers a dialog
    // that does not. Kept with the password, so that a prompt for anyone else
    // on this context asks instead of being given it.
    const QString user = username.isEmpty() ? m_request.user : username;
    // A login without a username to go with it would answer the next username
    // prompt with nothing at all; the password alone still gets git going.
    if (!user.isEmpty())
        m_logins.insert(m_request.context, {user, password});
    m_answered.insert(answeredKey(m_request));
    reply(m_request.kind == AskPassRequest::Password ? password : username, true);
}

void AskPass::answerSecret(int id, const QString &secret)
{
    if (!isCurrent(id))
        return;
    m_answered.insert(answeredKey(m_request));
    reply(secret, true);
}

void AskPass::cancel(int id)
{
    // A refusal of a question nobody is asking any more refuses nothing: the
    // dialog it comes from was left over from a request that was dropped, and
    // the sign-in git is waiting on now is not the one the user said no to.
    if (!isCurrent(id))
        return;
    // Otherwise it holds for the rest of the operation, not just for the
    // prompt on screen: git moves on to the next remote of a `fetch --all`
    // after a refusal and asks there too, and serveNext() turns that down on
    // its own rather than opening a second dialog on a user who has just said
    // no. Set before the reply, so a helper already queued behind this one is
    // turned away too; endOperation() lifts it.
    m_cancelled = true;
    reply(QString(), false);
}

void AskPass::endOperation()
{
    // The fetch, pull or push that was asking is over. A prompt still standing
    // belongs to it and to nothing that follows — git is on its way out and
    // has stopped waiting — so the helper is let go and the dialog with it.
    if (m_current) {
        const int dropped = m_request.id;
        reply(QString(), false);
        emit requestDropped(dropped);
    }
    // So do the helpers queued behind it — the other remotes of a parallel
    // fetch, a submodule's — whose turn would otherwise come after this: a
    // dialog for an operation that is over, asked after the cancelled flag and
    // the cache were cleared, and answered into the cache of whatever runs
    // next. One whose frame has not arrived yet asked for this operation just
    // the same and goes with them. Their handlers are taken off first, as
    // reply() does for the one on screen: a disconnected() left connected
    // would reach into the list being emptied and set serveNext() going again.
    const QList<QLocalSocket *> waiting = m_waiting;
    m_waiting.clear();
    for (QLocalSocket *socket : waiting) {
        disconnect(socket, nullptr, this, nullptr);
        socket->write(frame(QString(QLatin1Char(kCancelled))));
        socket->flush();
        socket->disconnectFromServer();
        socket->deleteLater();
    }
    m_logins.clear();
    m_answered.clear();
    m_cancelled = false;
}
