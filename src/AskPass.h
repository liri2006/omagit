#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

class QIODevice;
class QLocalServer;
class QLocalSocket;

// One prompt git or ssh put to its askpass helper, taken apart so a dialog
// can say what it is asking for. Git asks in two steps ("Username for
// 'https://github.com': ", then "Password for 'https://me@github.com': ");
// ssh asks once for the passphrase of a key file. Anything else — a host key
// to confirm, a smartcard PIN — arrives as Other with git's own words.
struct AskPassRequest {
    enum Kind { Username, Password, Passphrase, Other };

    // Which asking this is: every prompt AskPass takes up gets a number of its
    // own, and an answer has to name it. A dialog whose request was dropped
    // (the helper went away while it stood) would otherwise answer whatever is
    // being asked now — another remote, another user. 0 is no request at all.
    int id = 0;
    Kind kind = Other;
    QString prompt;   // the raw text the helper was handed
    QString target;   // what stood in quotes: "https://me@github.com"
    QString host;     // "github.com", empty when the target is not a URL — the dialog's heading
    QString user;     // the user the URL already names, empty when it names none
    QString keyPath;  // the key file of a passphrase prompt
    // What an answer belongs to, and what it is remembered under: the
    // credential context git itself keys credentials by — the scheme, the
    // host, the port and, when git's prompt carries one (credential.useHttpPath
    // does that), the path — with the user stripped off, since who is signing
    // in is the `user` above. "https://example.com:8443/team/repo". The host
    // alone would not do: it would hand what was typed for an https remote to
    // an http one, which sends it in the clear. For a passphrase it is the key
    // file, and for a question of git's own the target or the prompt itself.
    QString context;
    // Answered once already in this operation and asked again, so what was
    // given was not accepted. Only ssh does this — three tries at a key
    // passphrase — and only for answers that are not kept (a passphrase, or a
    // question of git's or ssh's own): git asks its helper once per remote and
    // fails with "Authentication failed" instead of asking twice.
    bool retry = false;
};

// Takes git's and ssh's prompt strings apart. A pure function: the parsing is
// the part worth testing, and it has no business touching a socket.
AskPassRequest parseAskPassPrompt(const QString &prompt);

// The other half of the app: `omagit --askpass "<prompt>"` is what git and ssh
// run. It connects to the socket named by OMAGIT_ASKPASS_SOCKET, hands the
// prompt over, writes the answer to `out` followed by a newline and returns 0.
// Returns 1 without writing anything when there is no socket to talk to, the
// app cannot be reached, or the user cancelled the sign-in — which is how a
// helper says "no answer", and git then fails instead of hanging.
int askPassClient(const QString &socketPath, const QString &prompt, QIODevice *out);

// A sign-in the user asked to have remembered: the credential context it was
// given for (AskPassRequest::context) and the login. CredentialKeeper hands it
// to git once the operation it signed in worked.
struct KeptLogin {
    QString context, username, password;
};

// The app's end of that conversation: a local socket the helper processes of
// one git run connect to, one at a time.
//
// git asks for the username and the password separately, and asks over again
// for every remote of a `fetch --all` on the same host, so passing each prompt
// straight through would mean a dialog every time. Instead the first prompt
// for a credential context collects the whole login and every later one for it
// is answered from memory, silently, for as long as the operation lasts: one
// sign-in is one dialog. The context is git's own — scheme, host, port and
// path — and the login is kept under the user it belongs to, so a remote that
// names another user, or the same host over another scheme, asks for itself
// rather than being handed a password that was never meant for it. A login git
// turns down is not asked for again — git gives a remote up after one refusal
// and fails with its own "Authentication failed" — and whatever the user
// starts next asks afresh. Nothing is written anywhere, remembering
// credentials being the job of git's credential helper, and endOperation()
// drops the lot when the fetch, pull or push is over.
//
// A cancelled sign-in ends the whole operation's asking, not just the prompt
// on screen: `fetch --all` walks on to the next remote after a refusal, and
// the user who just closed the dialog would meet it again. Every prompt after
// a cancel() is turned down where it arrives, without a request, until
// endOperation().
//
// A prompt can also be lost while its dialog is on screen — git was killed,
// or the helper gave up waiting — and the asking moves on to the next one
// without it. That is what requestDropped() is for, and why every request
// carries an id an answer has to name: the dialog left behind is closed, and
// anything it still had to say is ignored rather than handed to whatever is
// being asked now.
//
// Core only (no QtGui): the window learns of a request through
// requestReceived() and answers with answerLogin() / answerSecret() / cancel().
class AskPass : public QObject
{
    Q_OBJECT
public:
    explicit AskPass(QObject *parent = nullptr);
    ~AskPass() override;

    // Starts listening (idempotent). False, with a reason on qWarning, when
    // the socket could not be created; the caller then simply passes no env
    // and git behaves as it did before.
    bool listen();
    bool listening() const;
    QString socketPath() const { return m_socketPath; }

    // The variables a git run needs so that its prompts come here, in the
    // "KEY=VALUE" form GitRepo::runAsync() takes. Empty while not listening.
    QStringList env() const;

    // The helper the variables point at: the running program itself, unless a
    // test (whose own binary is no helper) points them at the built app.
    void setHelperPath(const QString &path) { m_helper = path; }

    // Answers the request `id` names, and only while that one is still the
    // one being asked: an answer to a request that has since been dropped is
    // no answer to the one that took its place. answerLogin() is what a
    // username (or password) prompt is answered with: the half git asked for
    // goes back now and both halves stay for the rest of the operation, so
    // nothing asking for this user on this context asks again. answerSecret()
    // answers a passphrase, or a question of git's own, with the one thing it
    // wanted; that is not kept. `remember` marks a login the user asked to
    // have remembered past the operation (loginsToKeep()).
    void answerLogin(int id, const QString &username, const QString &password, bool remember = false);
    void answerSecret(int id, const QString &secret);
    // The user closed the dialog: the helper exits 1 and git gives up — and
    // so does every further prompt of this operation, without asking again.
    // Ignored, down to that, for a request that is no longer the current one:
    // a dialog nobody answered must not turn the next sign-in down.
    void cancel(int id);

    // Whether a sign-in was cancelled since the last endOperation(), so the
    // failure that follows can be reported quietly instead of as an error.
    bool cancelled() const { return m_cancelled; }
    // The logins of this operation the user asked to have remembered. Only
    // worth keeping once the operation worked — git keeps no login it turned
    // down — and endOperation() forgets them, so they are taken first.
    QList<KeptLogin> loginsToKeep() const { return m_keep.values(); }
    // The operation that prompted is over: forget the logins it collected,
    // what was answered and the cancelled flag. The prompt on screen goes with
    // it, and so does every helper still waiting its turn behind that one —
    // they asked for this operation, and none of them is to raise a dialog
    // after it.
    void endOperation();

signals:
    // A prompt nothing in the cache could answer: show the dialog for it.
    void requestReceived(const AskPassRequest &request);
    // Nobody is waiting for the answer to that request any more: the helper
    // that asked went away, or the operation it belonged to ended. Close its
    // dialog — but not as a refusal: the asking has moved on, and this one is
    // simply over.
    void requestDropped(int id);
    // The request was answered or cancelled — git is running again, so
    // whoever holds a timeout over it can start counting anew.
    void answered();

private:
    void onNewConnection();
    void serveNext();
    void reply(const QString &value, bool ok);
    bool isCurrent(int id) const;

    QLocalServer *m_server = nullptr;
    QString m_socketPath;
    QString m_helper;
    QList<QLocalSocket *> m_waiting; // helpers whose turn has not come yet
    QLocalSocket *m_current = nullptr;
    AskPassRequest m_request; // what m_current asked
    int m_nextId = 0;         // ids of requests, in the order they were taken up

    // What one sign-in was: both halves, so either can be handed back, and
    // the username with them — a prompt for anyone else is not this login.
    struct Login {
        QString username, password;
    };
    // context → the sign-in of this operation. One per context: signing in as
    // somebody else there replaces it, which is what git does anyway.
    QHash<QString, Login> m_logins;
    QHash<QString, KeptLogin> m_keep; // context → a login to remember
    // Questions an answer was already given for, as context and user, so that
    // being asked again can be told from being asked about somebody else.
    QSet<QString> m_answered;
    bool m_cancelled = false;
};
