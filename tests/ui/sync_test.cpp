// Signing in on the way to a remote, end to end and without a network:
// RemoteSync starts a `git fetch`, git meets a 401 from a server the test runs
// on 127.0.0.1, runs the built binary as its askpass helper, and the prompt
// arrives here as a request. What the test answers decides how it ends.
#include "UiTestCase.h"
#include "fixtures.h"

#include "../../src/AskPass.h"
#include "../../src/MessageDialog.h"
#include "../../src/RemoteSync.h"
#include "../../src/SshKeyDialog.h"
#include "../../src/SshKeys.h"

#include <QApplication>
#include <QSignalSpy>
#include <QTimer>

namespace {

// A repository whose remotes all point at the 401 server: one commit, and a main
// branch that names the first of them as its upstream, so the state has
// something to fetch. credential.helper= empties the helper list, so no
// credential helper of this machine answers before the sign-in dialog does.
// A `users` entry puts a user into the URL of the remote of the same index,
// the way a remote that says who to sign in as does; git then asks only for
// that user's password.
bool signInRepo(const QString &dir, quint16 port, const QStringList &remotes,
                const QStringList &users = {})
{
    if (!git(dir, {"init", "-q", "-b", "main"}) || !commit(dir, QStringLiteral("A"), 1)
        || !git(dir, {"config", "credential.helper", ""}))
        return false;
    for (qsizetype i = 0; i < remotes.size(); ++i) {
        const QString name = remotes.at(i);
        const QString user = users.value(i);
        const QString credentials = user.isEmpty() ? QString() : user + QLatin1Char('@');
        const QString url = QStringLiteral("http://") + credentials
            + QStringLiteral("127.0.0.1:%1/%2.git").arg(port).arg(name);
        if (!git(dir, {QStringLiteral("remote"), QStringLiteral("add"), name, url}))
            return false;
    }
    return git(dir, {QStringLiteral("config"), QStringLiteral("branch.main.remote"), remotes.constFirst()})
        && git(dir, {"config", "branch.main.merge", "refs/heads/main"});
}

// What one RemoteSync::finished() carried, kept for the assertions after it.
struct SyncOutcome {
    bool done = false;
    RemoteSync::Op op = RemoteSync::None;
    bool ok = false;
    bool automatic = false;
    bool signInCancelled = false; // as reported while the signal was delivered
    QString message;
};

// Connects `sync` up so every request lands in `seen` and is answered by
// `answer`, and every finished() lands in `outcome`.
void watchSignIn(RemoteSync *sync, QList<AskPassRequest> *seen, SyncOutcome *outcome,
                 const std::function<void(const AskPassRequest &)> &answer)
{
    QObject::connect(sync->askPass(), &AskPass::requestReceived, sync->askPass(),
                     [seen, answer](const AskPassRequest &request) {
                         *seen << request;
                         answer(request);
                     });
    QObject::connect(sync, &RemoteSync::finished, sync,
                     [sync, outcome](RemoteSync::Op op, bool ok, bool automatic, const QString &message) {
                         outcome->op = op;
                         outcome->ok = ok;
                         outcome->automatic = automatic;
                         outcome->message = message;
                         outcome->signInCancelled = sync->signInCancelled();
                         outcome->done = true;
                     });
}

} // namespace

class SyncTest : public UiTestCase
{
    Q_OBJECT
private slots:
    // A fetch the server turns the key down for offers to choose another:
    // the error's "Choose SSH key…" opens the key picker, the choice becomes
    // the repository's core.sshCommand, and the fetch runs again with it —
    // ssh here being a script that refuses every key and notes what it got.
    void aRefusedKeyCanBeSwappedAndTriedAgain()
    {
        if (!sshkeys::overridingVariable().isEmpty())
            QSKIP("GIT_SSH_COMMAND is set, which outranks core.sshCommand");
        QTemporaryDir home, bin;
        QVERIFY(home.isValid() && bin.isValid());
        const QString ssh = QDir(home.path()).filePath(QStringLiteral(".ssh"));
        QVERIFY(QDir().mkpath(ssh));
        const QString gitea = QDir(ssh).filePath(QStringLiteral("gitea"));
        QVERIFY(writeFixture(gitea, "private"));
        QVERIFY(writeFixture(gitea + QStringLiteral(".pub"),
                             "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIJs+UX4FrcSY4qShNMKUNPQ9OiF4dYT6FglkbmVSUK6z test-key\n"));
        const QString log = bin.filePath(QStringLiteral("ssh.log"));
        QVERIFY(writeFixture(bin.filePath(QStringLiteral("ssh")),
                             "#!/bin/sh\necho \"$@\" >> '" + log.toUtf8() + "'\n"
                             "echo 'git@example.invalid: Permission denied (publickey).' >&2\nexit 255\n"));
        QVERIFY(QFile::setPermissions(bin.filePath(QStringLiteral("ssh")),
                                      QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        ScopedEnv homeEnv("HOME", home.path().toUtf8());
        ScopedEnv path("PATH", bin.path().toUtf8() + ':' + qgetenv("PATH"));

        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(git(f.repo->root(), {"remote", "add", "origin", "ssh://git@example.invalid/team/repo.git"}));
        auto *sync = f.window->findChild<RemoteSync *>();
        QVERIFY(sync);
        sync->refreshState();

        // Through the dialogs as they come: the first error's "Choose SSH
        // key…", the picker's key, and the second error's OK.
        int errors = 0;
        bool picked = false;
        QTimer driver;
        driver.setInterval(20);
        connect(&driver, &QTimer::timeout, &driver, [&] {
            QWidget *modal = QApplication::activeModalWidget();
            if (auto *box = qobject_cast<MessageDialog *>(modal)) {
                ++errors;
                QPushButton *target = box->defaultButton();
                if (errors == 1)
                    for (QPushButton *b : box->findChildren<QPushButton *>())
                        if (b->text() == QStringLiteral("Choose SSH key…"))
                            target = b;
                target->click();
            } else if (auto *keys = qobject_cast<SshKeyDialog *>(modal)) {
                keys->selectKey(gitea);
                picked = true;
                keys->accept();
            }
        });
        driver.start();
        sync->fetch();
        QTRY_VERIFY_WITH_TIMEOUT(errors >= 2, 30000);
        driver.stop();
        QVERIFY(picked);
        QCOMPARE(sshkeys::repoCommand(f.repo.get()), sshkeys::sshCommand(gitea));
        QFile calls(log);
        QVERIFY(calls.open(QIODevice::ReadOnly));
        const QStringList lines = QString::fromUtf8(calls.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QVERIFY2(lines.size() >= 2, qPrintable(lines.join(QLatin1Char('\n'))));
        QVERIFY(!lines.constFirst().contains(QStringLiteral("-i ")));
        QVERIFY2(lines.constLast().contains(QStringLiteral("-i ") + gitea), qPrintable(lines.constLast()));
    }

    // git's words for a credential prompt it could not put to anyone, and the
    // host they name — a remote's URL without its path.
    void signInPromptsAreRecognizedInGitsErrors()
    {
        QString where;
        QVERIFY(RemoteSync::needsSignIn(
            "fatal: could not read Username for 'https://example.com': terminal prompts disabled\n", &where));
        QCOMPARE(where, QStringLiteral("example.com"));
        QVERIFY(RemoteSync::needsSignIn("fatal: could not read Password for 'http://alice@localhost:3300': "
                                        "terminal prompts disabled\nerror: could not fetch origin\n",
                                        &where));
        QCOMPARE(where, QStringLiteral("localhost:3300"));
        QVERIFY(!RemoteSync::needsSignIn("fatal: Authentication failed for 'https://example.com/x.git/'\n"));
        // ssh with nobody to ask: a key it could not unlock, a host it has no
        // key for. A host whose key changed is an alarm, not a sign-in.
        QVERIFY(RemoteSync::needsSignIn("git@localhost: Permission denied (publickey).\n"
                                        "fatal: Could not read from remote repository.\n",
                                        &where));
        QCOMPARE(where, QStringLiteral("localhost"));
        QVERIFY(RemoteSync::needsSignIn("Host key verification failed.\n"
                                        "fatal: Could not read from remote repository.\n",
                                        &where));
        QVERIFY(where.isEmpty());
        QVERIFY(!RemoteSync::needsSignIn(
            "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n"
            "@    WARNING: REMOTE HOST IDENTIFICATION HAS CHANGED!     @\n"
            "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n"
            "IT IS POSSIBLE THAT SOMEONE IS DOING SOMETHING NASTY!\n"
            "Host key verification failed.\nfatal: Could not read from remote repository.\n"));
        QVERIFY(!RemoteSync::needsSignIn(
            "fatal: unable to access 'https://example.com/': Could not resolve host: example.com\n"));
        // A hard failure is the error, whatever sign-in words come with it: a
        // changed host key before the key refusal, a host that does not
        // resolve after git's prompt, a connection that was refused.
        QVERIFY(!RemoteSync::needsSignIn(
            "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n"
            "@    WARNING: REMOTE HOST IDENTIFICATION HAS CHANGED!     @\n"
            "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n"
            "git@localhost: Permission denied (publickey).\n"
            "fatal: Could not read from remote repository.\n",
            &where));
        QVERIFY(where.isEmpty());
        QVERIFY(!RemoteSync::needsSignIn(
            "fatal: could not read Username for 'https://x': terminal prompts disabled\n"
            "fatal: unable to access 'https://x/': Could not resolve host: x\n"));
        QVERIFY(!RemoteSync::needsSignIn("ssh: connect to host x port 22: Connection refused\n"
                                         "git@x: Permission denied (publickey).\n"
                                         "fatal: Could not read from remote repository.\n"));
    }

    // A failed pull or push is said once: the line of git's output that names
    // the error, and all of the output under it only when there is more to it
    // than that line. A rejected push names its error after the "To <url>".
    void failedPullsAndPushesSayItOnce()
    {
        const QByteArray ssl = "fatal: unable to access 'https://localhost:3443/tester/demo.git/': "
                               "SSL certificate OpenSSL verify result: self-signed certificate (18)\n";
        QCOMPARE(RemoteSync::failureText(ssl),
                 QStringLiteral("unable to access 'https://localhost:3443/tester/demo.git/': "
                                "SSL certificate OpenSSL verify result: self-signed certificate (18)"));
        const QByteArray rejected = "To https://example.com/team/repo.git\n"
                                    " ! [rejected]        main -> main (fetch first)\n"
                                    "error: failed to push some refs to 'https://example.com/team/repo.git'\n"
                                    "hint: Updates were rejected because the remote contains work that you do not\n";
        const QString text = RemoteSync::failureText(rejected);
        QVERIFY2(text.startsWith(QStringLiteral("failed to push some refs to 'https://example.com/team/repo.git'\n\n"
                                                "To https://example.com/team/repo.git\n")),
                 qPrintable(text));
        QVERIFY(text.contains(QStringLiteral("hint: Updates were rejected")));
        QVERIFY(RemoteSync::failureText("").isEmpty());
        // When ssh gave up, git only says it could not read from the remote;
        // what ssh said is the headline, and git's lines follow with the rest.
        const QString ssh = RemoteSync::failureText("git@localhost: Permission denied (publickey).\n"
                                                    "fatal: Could not read from remote repository.\n\n"
                                                    "Please make sure you have the correct access rights\n");
        QVERIFY2(ssh.startsWith(QStringLiteral("git@localhost: Permission denied (publickey).\n\n")), qPrintable(ssh));
        QVERIFY(ssh.contains(QStringLiteral("correct access rights")));
        // Said once: the headline was git's first line, which is not repeated.
        QCOMPARE(ssh.count(QStringLiteral("Permission denied")), 1);
        const QString https = RemoteSync::failureText("fatal: unable to access 'https://x/': SSL problem\n"
                                                      "hint: see git help config\n");
        QCOMPARE(https, QStringLiteral("unable to access 'https://x/': SSL problem\n\nhint: see git help config"));
    }

    // An automatic fetch has no dialog to ask with, so a remote that wants a
    // password stops it — as a sign-in still to do, not as a failure of the
    // remote: the message points at Fetch, and the history knows why, which
    // keeps the error mark off the Fetch button.
    void automaticFetchStopsAtASignInWithoutAnError()
    {
        std::unique_ptr<QTcpServer> server = unauthorizedServer();
        QVERIFY(server);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(signInRepo(dir.path(), server->serverPort(), {QStringLiteral("origin")}));

        // An askpass the session has set up is not the automatic fetch's to
        // run either: it would put somebody else's dialog on screen.
        QTemporaryDir tools;
        QVERIFY(tools.isValid());
        const QString asked = tools.filePath(QStringLiteral("asked"));
        QVERIFY(writeFixture(tools.filePath(QStringLiteral("askpass")),
                             "#!/bin/sh\ntouch '" + asked.toUtf8() + "'\necho nobody\n"));
        QVERIFY(QFile::setPermissions(tools.filePath(QStringLiteral("askpass")),
                                      QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        ScopedEnv sessionAskPass("GIT_ASKPASS", tools.filePath(QStringLiteral("askpass")).toUtf8());
        ScopedEnv sessionSshAskPass("SSH_ASKPASS", tools.filePath(QStringLiteral("askpass")).toUtf8());

        GitRepo repo(dir.path());
        RemoteSync sync(&repo);
        QList<AskPassRequest> seen;
        SyncOutcome outcome;
        watchSignIn(&sync, &seen, &outcome,
                    [&](const AskPassRequest &request) { sync.askPass()->cancel(request.id); });
        sync.setActive(true); // the first automatic fetch follows shortly
        QTRY_VERIFY_WITH_TIMEOUT(outcome.done, 30000);

        QVERIFY(seen.isEmpty());
        QVERIFY(outcome.automatic);
        QVERIFY(!outcome.ok);
        QCOMPARE(outcome.message, QStringLiteral("Sign-in needed for 127.0.0.1:%1 — Fetch (Ctrl+F) to sign in")
                                      .arg(server->serverPort()));
        QVERIFY(sync.lastFetchNeedsSignIn());
        QVERIFY(!sync.lastFetchOk());
        QVERIFY(!QFile::exists(asked));
    }

    // ssh in an automatic fetch asks nobody either — not a window, and not
    // the terminal Omagit may have been started from: it is told to use an
    // askpass whatever else it has (SSH_ASKPASS_REQUIRE=force), and that
    // askpass is `false`, which says no to everything. ssh here is a script
    // that notes what it was given and turns the key down, as a server does
    // when the key ssh would have had to unlock was skipped.
    void automaticFetchOverSshAsksNobody()
    {
        if (qEnvironmentVariableIsSet("GIT_SSH_COMMAND"))
            QSKIP("GIT_SSH_COMMAND is set, so git would not run the ssh on PATH");
        QTemporaryDir dir, bin, config;
        QVERIFY(dir.isValid() && bin.isValid() && config.isValid());
        // No core.sshCommand of this machine's between git and the ssh below.
        QVERIFY(writeFixture(config.filePath(QStringLiteral("gitconfig")), QByteArray()));
        ScopedEnv global("GIT_CONFIG_GLOBAL", config.filePath(QStringLiteral("gitconfig")).toUtf8());
        ScopedEnv system("GIT_CONFIG_NOSYSTEM", "1");
        const QString log = bin.filePath(QStringLiteral("ssh.log"));
        QVERIFY(writeFixture(bin.filePath(QStringLiteral("ssh")),
                             "#!/bin/sh\necho \"$SSH_ASKPASS_REQUIRE|$SSH_ASKPASS\" >> '" + log.toUtf8() + "'\n"
                             "echo 'git@example.invalid: Permission denied (publickey).' >&2\nexit 255\n",
                             true));
        ScopedEnv path("PATH", bin.path().toUtf8() + ':' + qgetenv("PATH"));

        QVERIFY(git(dir.path(), {"init", "-q", "-b", "main"}));
        QVERIFY(commit(dir.path(), QStringLiteral("A"), 1));
        QVERIFY(git(dir.path(), {"remote", "add", "origin", "ssh://git@example.invalid/team/repo.git"}));
        QVERIFY(git(dir.path(), {"config", "branch.main.remote", "origin"}));
        QVERIFY(git(dir.path(), {"config", "branch.main.merge", "refs/heads/main"}));

        GitRepo repo(dir.path());
        RemoteSync sync(&repo);
        QList<AskPassRequest> seen;
        SyncOutcome outcome;
        watchSignIn(&sync, &seen, &outcome,
                    [&](const AskPassRequest &request) { sync.askPass()->cancel(request.id); });
        sync.setActive(true); // the first automatic fetch follows shortly
        QTRY_VERIFY_WITH_TIMEOUT(outcome.done, 30000);
        sync.setActive(false); // no fetch of the backoff outliving the test

        QVERIFY(outcome.automatic);
        QVERIFY(!outcome.ok);
        QVERIFY2(sync.lastFetchNeedsSignIn(), qPrintable(outcome.message));
        QVERIFY(seen.isEmpty());
        QFile calls(log);
        QVERIFY(calls.open(QIODevice::ReadOnly));
        const QStringList lines = QString::fromUtf8(calls.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QVERIFY(!lines.isEmpty());
        for (const QString &line : lines) {
            QVERIFY2(line.startsWith(QStringLiteral("force|")) && line.endsWith(QStringLiteral("/false")),
                     qPrintable(line));
        }
    }

    // A clone is as fresh as a fetch: the window opened on one waits a whole
    // interval before it fetches by itself, instead of asking straight away a
    // remote the clone has just signed in to.
    void aFreshCloneIsNotFetchedAgainAtOnce()
    {
        std::unique_ptr<QTcpServer> server = unauthorizedServer();
        QVERIFY(server);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(signInRepo(dir.path(), server->serverPort(), {QStringLiteral("origin")}));

        GitRepo repo(dir.path());
        RemoteSync sync(&repo);
        QList<AskPassRequest> seen;
        SyncOutcome outcome;
        watchSignIn(&sync, &seen, &outcome, [](const AskPassRequest &) {});
        sync.markFetched();
        QVERIFY(sync.lastFetch().isValid());
        QVERIFY(sync.lastFetchOk());
        sync.setActive(true);
        QTest::qWait(2500); // past the 1.5 s the first automatic fetch waits otherwise
        QVERIFY(!sync.busy());
        QVERIFY(!outcome.done);
    }

    // Closing the dialog ends the fetch quietly, and leaves the fetch history
    // as it was: nothing was tried, so the Fetch button carries no error mark.
    void fetchCancelledAtTheSignInDialogIsNotAFailedFetch()
    {
        const QString binary = helperBinary();
        if (binary.isEmpty())
            QSKIP("omagit is not built (qmake6 omagit.pro && make); the askpass helper is the binary itself");
        std::unique_ptr<QTcpServer> server = unauthorizedServer();
        QVERIFY(server);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(signInRepo(dir.path(), server->serverPort(), {QStringLiteral("origin")}));

        GitRepo repo(dir.path());
        RemoteSync sync(&repo);
        sync.askPass()->setHelperPath(binary);
        QList<AskPassRequest> seen;
        SyncOutcome outcome;
        watchSignIn(&sync, &seen, &outcome,
                    [&](const AskPassRequest &request) { sync.askPass()->cancel(request.id); });

        QVERIFY(sync.canFetch());
        sync.fetch();
        // git, the helper it starts and the server here: seconds, not milliseconds.
        QTRY_VERIFY_WITH_TIMEOUT(outcome.done, 30000);

        QCOMPARE(seen.size(), 1);
        QCOMPARE(seen.constFirst().kind, AskPassRequest::Username);
        QCOMPARE(seen.constFirst().host, QStringLiteral("127.0.0.1"));
        QVERIFY(!seen.constFirst().retry);
        QCOMPARE(outcome.op, RemoteSync::Fetch);
        QVERIFY(!outcome.ok);
        QVERIFY(!outcome.automatic);
        QVERIFY(outcome.signInCancelled);
        QCOMPARE(outcome.message, QStringLiteral("Fetch cancelled — not signed in"));
        QVERIFY(sync.lastFetchOk());
        QVERIFY(sync.lastFetch().isNull());
    }

    // One sign-in covers the whole fetch: `git fetch --all` asks for the
    // username and the password of each remote in turn, and after the dialog
    // that answered the first prompt every other one is answered from memory.
    // The credentials are wrong — the server here refuses every one — so the
    // fetch ends as git's own authentication failure, which is a failed fetch
    // and not a cancelled sign-in.
    void fetchOverTwoRemotesOnOneHostAsksOnce()
    {
        const QString binary = helperBinary();
        if (binary.isEmpty())
            QSKIP("omagit is not built (qmake6 omagit.pro && make); the askpass helper is the binary itself");
        std::unique_ptr<QTcpServer> server = unauthorizedServer();
        QVERIFY(server);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // Two remotes on the one host, both of them fetched by `fetch --all`.
        QVERIFY(signInRepo(dir.path(), server->serverPort(),
                           {QStringLiteral("origin"), QStringLiteral("mirror")}));

        GitRepo repo(dir.path());
        RemoteSync sync(&repo);
        sync.askPass()->setHelperPath(binary);
        QList<AskPassRequest> seen;
        SyncOutcome outcome;
        watchSignIn(&sync, &seen, &outcome, [&](const AskPassRequest &request) {
            sync.askPass()->answerLogin(request.id, QStringLiteral("alice"), QStringLiteral("wrong"));
        });
        // Every prompt answered, dialog or not: two per remote, a username
        // and a password.
        QSignalSpy answers(sync.askPass(), &AskPass::answered);

        QVERIFY(sync.canFetch());
        sync.fetch();
        QTRY_VERIFY_WITH_TIMEOUT(outcome.done, 60000);

        // One dialog for the pair of remotes, and it was not a second try —
        // the prompts of the second remote were answered from that sign-in.
        QCOMPARE(seen.size(), 1);
        QCOMPARE(answers.count(), 4); // a username and a password for each remote
        QCOMPARE(seen.constFirst().kind, AskPassRequest::Username);
        QCOMPARE(seen.constFirst().host, QStringLiteral("127.0.0.1"));
        QVERIFY(!seen.constFirst().retry);
        QCOMPARE(outcome.op, RemoteSync::Fetch);
        QVERIFY(!outcome.ok);
        QVERIFY(!outcome.signInCancelled);
        // git was signed in and turned down, so it says so itself.
        QVERIFY2(outcome.message.contains(QStringLiteral("Authentication failed"), Qt::CaseInsensitive),
                 qPrintable(outcome.message));
        QVERIFY(!sync.lastFetchOk());
    }

    // Saying no once says it for the whole fetch: `fetch --all` walks on to
    // the second remote after git gives the first one up, and the prompt that
    // arrives from there is turned down where it lands instead of putting a
    // second dialog in front of a user who has just closed one.
    void fetchCancelledAtTheFirstOfTwoRemotesAsksOnce()
    {
        const QString binary = helperBinary();
        if (binary.isEmpty())
            QSKIP("omagit is not built (qmake6 omagit.pro && make); the askpass helper is the binary itself");
        std::unique_ptr<QTcpServer> server = unauthorizedServer();
        QVERIFY(server);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(signInRepo(dir.path(), server->serverPort(),
                           {QStringLiteral("origin"), QStringLiteral("mirror")}));

        GitRepo repo(dir.path());
        RemoteSync sync(&repo);
        sync.askPass()->setHelperPath(binary);
        QList<AskPassRequest> seen;
        SyncOutcome outcome;
        watchSignIn(&sync, &seen, &outcome,
                    [&](const AskPassRequest &request) { sync.askPass()->cancel(request.id); });

        QVERIFY(sync.canFetch());
        sync.fetch();
        QTRY_VERIFY_WITH_TIMEOUT(outcome.done, 60000);

        QCOMPARE(seen.size(), 1); // the second remote never got as far as asking
        QCOMPARE(seen.constFirst().kind, AskPassRequest::Username);
        QCOMPARE(outcome.op, RemoteSync::Fetch);
        QVERIFY(!outcome.ok);
        QVERIFY(outcome.signInCancelled);
        QCOMPARE(outcome.message, QStringLiteral("Fetch cancelled — not signed in"));
    }

    // Two remotes on the one host whose URLs name two different users: one
    // sign-in is not the other's, so each is asked for. Handing bob's prompt
    // what alice typed would send her password where it does not belong.
    void fetchOverTwoRemotesWithDifferentUsersAsksForEach()
    {
        const QString binary = helperBinary();
        if (binary.isEmpty())
            QSKIP("omagit is not built (qmake6 omagit.pro && make); the askpass helper is the binary itself");
        std::unique_ptr<QTcpServer> server = unauthorizedServer();
        QVERIFY(server);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // http://alice@127.0.0.1:port/a.git and http://bob@…/b.git, fetched in
        // the order `git fetch --all` takes the remotes in — the order git
        // lists them, which is by name.
        QVERIFY(signInRepo(dir.path(), server->serverPort(),
                           {QStringLiteral("a"), QStringLiteral("b")},
                           {QStringLiteral("alice"), QStringLiteral("bob")}));

        GitRepo repo(dir.path());
        RemoteSync sync(&repo);
        sync.askPass()->setHelperPath(binary);
        QList<AskPassRequest> seen;
        SyncOutcome outcome;
        watchSignIn(&sync, &seen, &outcome, [&](const AskPassRequest &request) {
            // What the dialog does: the user comes back from its read-only
            // field, with the password typed for that user.
            sync.askPass()->answerLogin(request.id, request.user, QStringLiteral("wrong"));
        });

        QVERIFY(sync.canFetch());
        sync.fetch();
        QTRY_VERIFY_WITH_TIMEOUT(outcome.done, 60000);

        // The URL names who to sign in as, so git asks for the password only.
        QCOMPARE(seen.size(), 2);
        QCOMPARE(seen.at(0).kind, AskPassRequest::Password);
        QCOMPARE(seen.at(0).user, QStringLiteral("alice"));
        QCOMPARE(seen.at(1).kind, AskPassRequest::Password);
        QCOMPARE(seen.at(1).user, QStringLiteral("bob"));
        QVERIFY(!seen.at(1).retry); // a question about somebody else, not the same one again
        QVERIFY(!outcome.ok);
        QVERIFY(!outcome.signInCancelled);
        QVERIFY2(outcome.message.contains(QStringLiteral("Authentication failed"), Qt::CaseInsensitive),
                 qPrintable(outcome.message));
    }

    // Nothing the user did not ask for opens a dialog: the automatic fetch
    // runs without the askpass variables and fails silently, as it did before
    // there was a dialog to open.
    void automaticFetchNeverAsksToSignIn()
    {
        const QString binary = helperBinary();
        if (binary.isEmpty())
            QSKIP("omagit is not built (qmake6 omagit.pro && make); the askpass helper is the binary itself");
        std::unique_ptr<QTcpServer> server = unauthorizedServer();
        QVERIFY(server);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(signInRepo(dir.path(), server->serverPort(), {QStringLiteral("origin")}));

        GitRepo repo(dir.path());
        RemoteSync sync(&repo);
        sync.askPass()->setHelperPath(binary); // a request would reach us if one were made
        QList<AskPassRequest> seen;
        SyncOutcome outcome;
        watchSignIn(&sync, &seen, &outcome,
                    [&](const AskPassRequest &request) { sync.askPass()->cancel(request.id); });

        sync.setAutoFetchInterval(RemoteSync::kDefaultInterval);
        sync.setActive(true); // the window is on screen: the first fetch follows shortly
        QTRY_VERIFY_WITH_TIMEOUT(outcome.done, 30000);

        QVERIFY(seen.isEmpty());
        QCOMPARE(outcome.op, RemoteSync::Fetch);
        QVERIFY(!outcome.ok);
        QVERIFY(outcome.automatic);
        QVERIFY(!outcome.signInCancelled);
        // Unlike a cancelled sign-in, this one was tried and did fail.
        QVERIFY(!sync.lastFetchOk());
        QVERIFY(!sync.lastFetch().isNull());
        sync.setActive(false); // no fetch of the backoff outliving the test
    }
};

UI_TEST(SyncTest);

#include "sync_test.moc"
