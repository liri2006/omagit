// The sign-in dialog and its askpass helper: the prompts git and ssh put,
// credential helpers named and matched the way git reads them, remembering
// a sign-in, SSH keys and host keys.
#include "UiTestCase.h"
#include "fixtures.h"

#include "../../src/AgentKeeper.h"
#include "../../src/AskPass.h"
#include "../../src/CredentialKeeper.h"
#include "../../src/LoginDialog.h"
#include "../../src/MessageDialog.h"
#include "../../src/SshKeyDialog.h"
#include "../../src/SshKeys.h"
#include "../../src/UiHelpers.h"

#include <QFileInfo>
#include <QLineEdit>
#include <QListWidget>
#include <QLocalServer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QScrollBar>
#include <QSignalSpy>
#include <QUrl>

class SignInTest : public UiTestCase
{
    Q_OBJECT
private slots:
    // The dialog for a request, shown offscreen and kept alive by the caller.
    // `repo` is what it asks about credential helpers; without one the note
    // has nothing to go on and says so.
    static LoginDialog *login(const QString &prompt, bool retry = false, GitRepo *repo = nullptr)
    {
        AskPassRequest request = parseAskPassPrompt(prompt);
        request.retry = retry;
        auto *dialog = new LoginDialog(request, repo);
        dialog->setAttribute(Qt::WA_DeleteOnClose, false);
        dialog->show();
        return dialog;
    }

    static QPushButton *signInButton(LoginDialog *dialog)
    {
        for (QPushButton *button : dialog->findChildren<QPushButton *>())
            if (button->isDefault())
                return button;
        return nullptr;
    }

    void loginDialogAsksForBothAndSubmitsOnReturn()
    {
        std::unique_ptr<LoginDialog> dialog(login(QStringLiteral("Username for 'https://github.com': ")));
        auto *user = dialog->findChild<QLineEdit *>(QStringLiteral("usernameEdit"));
        auto *secret = dialog->findChild<QLineEdit *>(QStringLiteral("secretEdit"));
        auto *reveal = dialog->findChild<QToolButton *>(QStringLiteral("revealButton"));
        QPushButton *signIn = signInButton(dialog.get());
        QVERIFY(user && secret && reveal && signIn);
        QVERIFY(says(dialog.get(), QStringLiteral("Sign in to github.com")));
        QVERIFY(user->isVisible() && !user->isReadOnly());
        QVERIFY(secret->isVisible());
        // Nothing to sign in with yet, and no talk of an earlier try.
        QVERIFY(!signIn->isEnabled());
        QVERIFY(!says(dialog.get(), QStringLiteral("Asked again")));
        user->setText(QStringLiteral("alice"));
        QVERIFY(!signIn->isEnabled());
        secret->setText(QStringLiteral("s3cret"));
        QVERIFY(signIn->isEnabled());

        // The password is hidden until the eye is pressed.
        QCOMPARE(secret->echoMode(), QLineEdit::Password);
        reveal->click();
        QCOMPARE(secret->echoMode(), QLineEdit::Normal);
        reveal->click();
        QCOMPARE(secret->echoMode(), QLineEdit::Password);

        QTest::keyClick(secret, Qt::Key_Return);
        QCOMPARE(dialog->result(), int(QDialog::Accepted));
        QCOMPARE(dialog->username(), QStringLiteral("alice"));
        QCOMPARE(dialog->password(), QStringLiteral("s3cret"));
    }

    void loginDialogShowsTheUserGitAlreadyKnows()
    {
        std::unique_ptr<LoginDialog> dialog(login(QStringLiteral("Password for 'https://andras@github.com': ")));
        auto *user = dialog->findChild<QLineEdit *>(QStringLiteral("usernameEdit"));
        auto *secret = dialog->findChild<QLineEdit *>(QStringLiteral("secretEdit"));
        QVERIFY(user->isVisible());
        QVERIFY(user->isReadOnly());
        QCOMPARE(user->text(), QStringLiteral("andras"));
        // The focus starts on the only field left to fill in. (Offscreen no
        // window is active, so the dialog's focus widget is what to look at.)
        QCOMPARE(dialog->focusWidget(), static_cast<QWidget *>(secret));
        QVERIFY(!signInButton(dialog.get())->isEnabled());
        secret->setText(QStringLiteral("s3cret"));
        QVERIFY(signInButton(dialog.get())->isEnabled());
    }

    void loginDialogAsksOnlyForAPassphrase()
    {
        std::unique_ptr<LoginDialog> dialog(login(
            QStringLiteral("Enter passphrase for key '%1/.ssh/id_ed25519': ").arg(QDir::homePath())));
        QVERIFY(!dialog->findChild<QLineEdit *>(QStringLiteral("usernameEdit"))->isVisible());
        QVERIFY(dialog->findChild<QLineEdit *>(QStringLiteral("secretEdit"))->isVisible());
        QVERIFY(says(dialog.get(), QStringLiteral("Unlock ~/.ssh/id_ed25519")));
        QVERIFY(says(dialog.get(), QStringLiteral("PASSPHRASE")));
    }

    // ssh asks up to three times for the same key, so the second dialog can
    // say that the passphrase given to the first one did not work. (An https
    // sign-in is never asked for twice: git gives the remote up instead.)
    void loginDialogSaysWhenThePassphraseWasNotAccepted()
    {
        const QString key = QStringLiteral("/x/id_ed25519");
        std::unique_ptr<LoginDialog> plain(login(QStringLiteral("Enter passphrase for key '%1': ").arg(key)));
        QVERIFY(!says(plain.get(), QStringLiteral("try again")));
        std::unique_ptr<LoginDialog> again(
            login(QStringLiteral("Bad passphrase, try again for key '%1': ").arg(key), true));
        QVERIFY(says(again.get(), QStringLiteral("That passphrase did not unlock the key")));
    }

    // The note is about this remote, not about credential helpers in general:
    // a helper set for one URL — `[credential "https://example.com"] helper =
    // store`, which a plain `--get credential.helper` never sees — is what
    // will keep the password, and a note saying otherwise promises a secret
    // is forgotten while git stores it.
    void loginDialogNamesTheCredentialHelperOfThisRemote()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(git(dir.path(), {"init", "-q", "-b", "main"}));
        // Empties the helper list of whatever this machine configures, so the
        // note answers for this repository and nothing else.
        QVERIFY(git(dir.path(), {"config", "credential.helper", ""}));
        GitRepo repo(dir.path());
        const QString userPrompt = QStringLiteral("Username for 'https://example.com': ");

        // The unqualified key is all there is, and it is empty: nothing keeps
        // what is typed here.
        std::unique_ptr<LoginDialog> plain(login(userPrompt, false, &repo));
        QVERIFY(says(plain.get(), QStringLiteral("Not remembered")));

        QVERIFY(git(dir.path(), {"config", "credential.https://example.com.helper", "store"}));
        std::unique_ptr<LoginDialog> stored(login(userPrompt, false, &repo));
        QVERIFY(says(stored.get(), QStringLiteral("Remembered by git's credential helper (store)")));

        // The password half of the same sign-in asks about a URL naming the
        // user; the helper of that remote is found all the same.
        std::unique_ptr<LoginDialog> password(
            login(QStringLiteral("Password for 'https://alice@example.com': "), false, &repo));
        QVERIFY(says(password.get(), QStringLiteral("(store)")));

        // Another host is another matter: no helper is configured for it, and
        // the note is not to claim one.
        std::unique_ptr<LoginDialog> elsewhere(
            login(QStringLiteral("Username for 'https://other.example': "), false, &repo));
        QVERIFY(says(elsewhere.get(), QStringLiteral("Not remembered")));
    }

    // Git matches `credential.<url>.helper` against the URL the remote is
    // configured with, path and all, and only drops the path afterwards — which
    // is why the prompt has none. A helper set for one path of a host is
    // therefore invisible from the prompt alone: the remote it belongs to has
    // to be found again, or the note calls a stored password forgotten.
    void loginDialogMatchesTheHelperAgainstTheRemotesPath()
    {
        const QString userPrompt = QStringLiteral("Username for 'https://example.com': ");
        // A repository with one remote and a helper configured for `path` of
        // that same host. The empty unqualified entry, written first, empties
        // whatever this machine configures.
        const auto repoWith = [&](QTemporaryDir &dir, const QString &remote, const QString &path) {
            if (!dir.isValid() || !git(dir.path(), {"init", "-q", "-b", "main"}))
                return false;
            return git(dir.path(), {"config", "credential.helper", ""})
                && git(dir.path(), {"remote", "add", "origin", remote})
                && git(dir.path(), {"config", QStringLiteral("credential.%1.helper").arg(path), "store"});
        };

        // The remote lies under the path the helper is configured for: git
        // stores this password, and the note is to say so.
        QTemporaryDir under;
        QVERIFY(repoWith(under, QStringLiteral("https://example.com/team/repo.git"),
                         QStringLiteral("https://example.com/team")));
        GitRepo stored(under.path());
        std::unique_ptr<LoginDialog> kept(login(userPrompt, false, &stored));
        QVERIFY(says(kept.get(), QStringLiteral("Remembered by git's credential helper (store)")));

        // The password half of the sign-in asks about a URL naming the user;
        // the remote names none, and is still the remote this is about.
        std::unique_ptr<LoginDialog> password(
            login(QStringLiteral("Password for 'https://alice@example.com': "), false, &stored));
        QVERIFY(says(password.get(), QStringLiteral("(store)")));

        // The same host, the same helper entry, another path: nothing keeps
        // this one, and matching the pathless prompt would claim otherwise.
        QTemporaryDir beside;
        QVERIFY(repoWith(beside, QStringLiteral("https://example.com/other/repo.git"),
                         QStringLiteral("https://example.com/team")));
        GitRepo elsewhere(beside.path());
        std::unique_ptr<LoginDialog> plain(login(userPrompt, false, &elsewhere));
        QVERIFY(says(plain.get(), QStringLiteral("Not remembered")));

        // Two remotes of that host, one under the path and one beside it: the
        // prompt does not say which of them git is signing in to, so the note
        // names the helper without promising it.
        QVERIFY(git(beside.path(), {"remote", "add", "inside", "https://example.com/team/repo.git"}));
        std::unique_ptr<LoginDialog> both(login(userPrompt, false, &elsewhere));
        QVERIFY(says(both.get(), QStringLiteral("Remembered if git's credential helper covers this remote (store)")));

        // A prompt no remote of this repository answers for — a submodule's, a
        // URL git rewrote, a `git credential fill` of its own — is matched
        // against the prompt's URL, as it was before there were remotes to ask.
        QTemporaryDir apart;
        QVERIFY(repoWith(apart, QStringLiteral("https://example.com/team/repo.git"),
                         QStringLiteral("https://other.example")));
        GitRepo unknown(apart.path());
        std::unique_ptr<LoginDialog> fallback(
            login(QStringLiteral("Username for 'https://other.example': "), false, &unknown));
        QVERIFY(says(fallback.get(), QStringLiteral("Remembered by git's credential helper (store)")));
    }

    // A dialog is never wider than the window it opens over: the compositor
    // centres it on that window, and a window tiled at the screen's edge would
    // leave the rest of a wider one off the screen. The text breaks inside a
    // URL too long for the width instead of widening the dialog, and the
    // dialog is as tall as the lines it wraps into.
    void dialogsFitANarrowWindow()
    {
        QWidget host;
        host.resize(401, 462);
        host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        const int room = host.width() - 2 * ui::windowMargin(&host);

        const QString url = QStringLiteral("https://localhost:3443/") + QString(60, QLatin1Char('x'))
            + QStringLiteral(".git/");
        MessageDialog box(MessageDialog::Error, QStringLiteral("Pull failed"),
                          QStringLiteral("unable to access '%1': SSL certificate problem").arg(url), &host);
        box.show();
        QVERIFY(QTest::qWaitForWindowExposed(&box));
        QVERIFY2(box.width() <= room, qPrintable(QStringLiteral("%1 > %2").arg(box.width()).arg(room)));
        auto *text = box.findChild<QTextEdit *>(QStringLiteral("messageText"));
        QVERIFY(text);
        QVERIFY(text->document()->size().height() <= text->height() + 1);
        QVERIFY(!text->verticalScrollBar()->isVisible());

        std::unique_ptr<LoginDialog> login(new LoginDialog(
            parseAskPassPrompt(QStringLiteral("Username for 'https://example.com': ")), nullptr, &host));
        login->show();
        QVERIFY(QTest::qWaitForWindowExposed(login.get()));
        QVERIFY2(login->width() <= room, qPrintable(QStringLiteral("%1 > %2").arg(login->width()).arg(room)));

        // A window with room for it gets the design's width.
        QWidget wide;
        wide.resize(1200, 800);
        wide.show();
        MessageDialog roomy(MessageDialog::Error, QStringLiteral("Push failed"), QStringLiteral("rejected"), &wide);
        roomy.show();
        QVERIFY(QTest::qWaitForWindowExposed(&roomy));
        QCOMPARE(roomy.width(), ui::space(480));
    }

    // What a question asks (throwing changes away, rewriting published
    // history) is not easily taken back: Return means Cancel, and only the
    // question's own button says yes.
    void aQuestionDefaultsToCancel()
    {
        MessageDialog box(MessageDialog::Question, QStringLiteral("Discard changes"), QStringLiteral("Discard a.txt?"));
        box.setAcceptText(QStringLiteral("Discard"));
        box.show();
        QVERIFY(QTest::qWaitForWindowExposed(&box));
        QCOMPARE(box.defaultButton()->text(), QStringLiteral("Cancel"));
        QTest::keyClick(&box, Qt::Key_Return);
        QCOMPARE(box.result(), int(QDialog::Rejected));

        MessageDialog again(MessageDialog::Question, QStringLiteral("Discard changes"), QStringLiteral("Discard a.txt?"));
        again.setAcceptText(QStringLiteral("Discard"));
        again.show();
        QVERIFY(QTest::qWaitForWindowExposed(&again));
        QPushButton *discard = nullptr;
        for (QPushButton *button : again.findChildren<QPushButton *>())
            if (button->text() == QStringLiteral("Discard"))
                discard = button;
        QVERIFY(discard);
        discard->click();
        QCOMPARE(again.result(), int(QDialog::Accepted));
    }

    // Nothing would keep the login, so the dialog offers to have git keep it
    // — ticked from the start, saying where it goes and that a token is the
    // better thing to keep there. A remote a helper keeps already, a remote
    // the configuration turns the helpers off for, a key's passphrase and a
    // git without the helper get no such box.
    void loginDialogOffersToRememberTheSignIn()
    {
        QTemporaryDir dir, bin, config;
        QVERIFY(dir.isValid() && bin.isValid() && config.isValid());
        // Nothing of this machine's configuration: no helper configured at
        // all, rather than one turned off.
        QVERIFY(writeFixture(config.filePath("gitconfig"), QByteArray()));
        ScopedEnv global("GIT_CONFIG_GLOBAL", config.filePath("gitconfig").toUtf8());
        ScopedEnv system("GIT_CONFIG_NOSYSTEM", "1");
        QVERIFY(git(dir.path(), {"init", "-q", "-b", "main"}));
        QVERIFY(writeFixture(bin.filePath("git-credential-omagittest"), "#!/bin/sh\nexit 0\n"));
        QVERIFY(QFile::setPermissions(bin.filePath("git-credential-omagittest"),
                                      QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        ScopedEnv path("PATH", bin.path().toUtf8() + ':' + qgetenv("PATH"));
        const QString before = CredentialKeeper::helper();
        CredentialKeeper::setHelper(QStringLiteral("omagittest"));
        const auto restore = qScopeGuard([before] { CredentialKeeper::setHelper(before); });
        GitRepo repo(dir.path());
        const QString userPrompt = QStringLiteral("Username for 'https://example.com': ");

        std::unique_ptr<LoginDialog> offered(login(userPrompt, false, &repo));
        auto *box = offered->findChild<QCheckBox *>(QStringLiteral("rememberSignIn"));
        QVERIFY(box && box->isVisible());
        QVERIFY(box->isChecked());
        QVERIFY(offered->remember());
        QVERIFY(says(offered.get(), QStringLiteral("Kept in your keyring by git's omagittest helper")));
        QVERIFY(says(offered.get(), QStringLiteral("personal access token")));
        QVERIFY(box->toolTip().contains(QStringLiteral("credential.https://example.com.helper")));
        box->setChecked(false);
        QVERIFY(!offered->remember());
        QVERIFY(says(offered.get(), QStringLiteral("Not remembered")));

        std::unique_ptr<LoginDialog> passphrase(login(QStringLiteral("Enter passphrase for key '/x/id_ed25519': "), false, &repo));
        QVERIFY(!passphrase->findChild<QCheckBox *>(QStringLiteral("rememberSignIn")));
        QVERIFY(!passphrase->remember());

        // A repository that turns the helpers off on purpose: nothing keeps
        // the login, and nothing is to be offered to keep it either.
        QTemporaryDir offDir;
        QVERIFY(offDir.isValid());
        QVERIFY(git(offDir.path(), {"init", "-q", "-b", "main"}));
        QVERIFY(git(offDir.path(), {"config", "credential.helper", ""}));
        GitRepo off(offDir.path());
        std::unique_ptr<LoginDialog> turnedOff(login(userPrompt, false, &off));
        QVERIFY(!turnedOff->findChild<QCheckBox *>(QStringLiteral("rememberSignIn")));
        QVERIFY(!turnedOff->remember());
        QVERIFY(says(turnedOff.get(), QStringLiteral("Not remembered")));
        QVERIFY(says(turnedOff.get(), QStringLiteral("turned off")));

        QVERIFY(git(dir.path(), {"config", "credential.https://example.com.helper", "store"}));
        std::unique_ptr<LoginDialog> kept(login(userPrompt, false, &repo));
        QVERIFY(!kept->findChild<QCheckBox *>(QStringLiteral("rememberSignIn")));
        QVERIFY(says(kept.get(), QStringLiteral("Remembered by git's credential helper (store)")));

        CredentialKeeper::setHelper(QStringLiteral("omagit-no-such-helper"));
        std::unique_ptr<LoginDialog> without(
            login(QStringLiteral("Username for 'https://other.example': "), false, &repo));
        QVERIFY(!without->findChild<QCheckBox *>(QStringLiteral("rememberSignIn")));
        QVERIFY(says(without.get(), QStringLiteral("Not remembered")));
    }

    // The note names a helper by its program, not by its whole command line:
    // `store --file=/somewhere/long` would otherwise fill the dialog.
    void credentialHelpersAreNamedShort()
    {
        QCOMPARE(credentialHelperName(QStringLiteral("store")), QStringLiteral("store"));
        QCOMPARE(credentialHelperName(QStringLiteral("store --file=/x/y")), QStringLiteral("store"));
        QCOMPARE(credentialHelperName(QStringLiteral("cache --timeout=300")), QStringLiteral("cache"));
        QCOMPARE(credentialHelperName(QStringLiteral("/usr/lib/git-core/git-credential-libsecret")),
                 QStringLiteral("libsecret"));
        QCOMPARE(credentialHelperName(QStringLiteral("!gh auth git-credential")), QStringLiteral("gh"));
        QCOMPARE(credentialHelperName(QStringLiteral("!/usr/bin/gh auth git-credential")), QStringLiteral("gh"));
        QCOMPARE(credentialHelperName(QStringLiteral("manager")), QStringLiteral("manager"));
        QCOMPARE(credentialHelperName(QStringLiteral("git-credential-")), QStringLiteral("git-credential-"));
    }

    // A way on from an error: a button of its own in front of OK, whose
    // number exec() gives back when it is the one clicked.
    void messageDialogOffersAChoice()
    {
        MessageDialog box(MessageDialog::Error, QStringLiteral("Fetch failed"), QStringLiteral("Permission denied"));
        const int choose = box.addChoice(QStringLiteral("Choose SSH key…"));
        QVERIFY(choose > int(QDialog::Accepted));
        box.show();
        QVERIFY(QTest::qWaitForWindowExposed(&box));
        QCOMPARE(box.defaultButton()->text(), QStringLiteral("OK"));
        QPushButton *button = nullptr;
        for (QPushButton *b : box.findChildren<QPushButton *>())
            if (b->text() == QStringLiteral("Choose SSH key…"))
                button = b;
        QVERIFY(button);
        button->click();
        QCOMPARE(box.result(), choose);
    }

    // Two key pairs in a home of the test's own: the key picker lists ssh's
    // own choice and then each pair, comes up on the key the repository's
    // core.sshCommand names, and says so when the environment has the last word.
    void sshKeyDialogListsTheKeyPairs()
    {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        const QString ssh = QDir(home.path()).filePath(QStringLiteral(".ssh"));
        QVERIFY(QDir().mkpath(ssh));
        const QByteArray pub = "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIJs+UX4FrcSY4qShNMKUNPQ9OiF4dYT6FglkbmVSUK6z test-key\n";
        for (const QString name : {"id_ed25519", "gitea"}) {
            QVERIFY(writeFixture(QDir(ssh).filePath(name), "private"));
            QVERIFY(writeFixture(QDir(ssh).filePath(name + QStringLiteral(".pub")), pub));
        }
        ScopedEnv homeEnv("HOME", home.path().toUtf8());
        const QString gitea = QDir(ssh).filePath(QStringLiteral("gitea"));

        SshKeyDialog fresh(QStringLiteral("demo"), QString());
        auto *list = fresh.findChild<QListWidget *>(QStringLiteral("sshKeys"));
        QVERIFY(list);
        QCOMPARE(list->count(), 3);
        QVERIFY(list->item(0)->text().startsWith(QStringLiteral("ssh's choice")));
        QVERIFY(list->item(1)->text().startsWith(QStringLiteral("gitea · ED25519 · test-key")));
        QVERIFY(fresh.chosenKey().isEmpty());
        list->setCurrentRow(1);
        QCOMPARE(fresh.chosenKey(), gitea);

        SshKeyDialog set(QStringLiteral("demo"), sshkeys::sshCommand(gitea));
        QCOMPARE(set.chosenKey(), gitea);

        // A key from elsewhere gets a row of its own.
        set.selectKey(QStringLiteral("/somewhere/else/deploy"));
        QCOMPARE(set.chosenKey(), QStringLiteral("/somewhere/else/deploy"));

        const auto says = [](const SshKeyDialog &dialog, const QString &text) {
            for (const QLabel *label : dialog.findChildren<QLabel *>())
                if (!label->isHidden() && label->text().contains(text))
                    return true;
            return false;
        };
        SshKeyDialog custom(QStringLiteral("demo"), QStringLiteral("ssh -o ProxyJump=bastion"));
        QVERIFY(says(custom, QStringLiteral("This replaces the repository's core.sshCommand")));
        ScopedEnv overriding("GIT_SSH_COMMAND", "ssh -i /x");
        SshKeyDialog overridden(QStringLiteral("demo"), QString());
        QVERIFY(says(overridden, QStringLiteral("GIT_SSH_COMMAND is set")));
    }

    // A key's passphrase gets the agent's offer only where an agent answers:
    // then "Keep unlocked until logout", ticked, and a note saying how long;
    // without one, the note says plainly that the passphrase is asked for
    // every time. A path ssh cut short (it names 100 characters at most) is
    // no path ssh-add could use, so it gets no offer.
    void loginDialogOffersToKeepAKeyUnlocked()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString socket = dir.filePath(QStringLiteral("agent"));
        QLocalServer agent;
        QVERIFY(agent.listen(socket));
        const QString prompt = QStringLiteral("Enter passphrase for key '/home/x/.ssh/id_ed25519': ");
        {
            ScopedEnv sock("SSH_AUTH_SOCK", socket.toUtf8());
            QVERIFY(sshkeys::agentReachable());
            std::unique_ptr<LoginDialog> offered(login(prompt));
            auto *box = offered->findChild<QCheckBox *>(QStringLiteral("keepUnlocked"));
            QVERIFY(box && box->isVisible() && box->isChecked());
            QCOMPARE(box->text(), QStringLiteral("Keep unlocked until logout"));
            QVERIFY(offered->keepUnlocked());
            QVERIFY(!offered->remember());
            QVERIFY(says(offered.get(), QStringLiteral("ssh-agent keeps the key unlocked until you log out")));
            box->setChecked(false);
            QVERIFY(!offered->keepUnlocked());
            QVERIFY(says(offered.get(), QStringLiteral("you'll be asked for this passphrase every time")));

            const QString cut = QStringLiteral("/tmp/") + QString(95, QLatin1Char('k'));
            std::unique_ptr<LoginDialog> truncated(login(QStringLiteral("Enter passphrase for key '%1': ").arg(cut)));
            QVERIFY(!truncated->findChild<QCheckBox *>(QStringLiteral("keepUnlocked")));

            // ssh told to use another agent never asks the one at
            // SSH_AUTH_SOCK, so a key handed to that one would not be found.
            ScopedEnv command("GIT_SSH_COMMAND", "ssh -o IdentityAgent=none");
            std::unique_ptr<LoginDialog> elsewhere(login(prompt));
            QVERIFY(!elsewhere->findChild<QCheckBox *>(QStringLiteral("keepUnlocked")));
            QVERIFY(!elsewhere->keepUnlocked());
        }
        ScopedEnv none("SSH_AUTH_SOCK", QByteArray());
        QVERIFY(!sshkeys::agentReachable());
        std::unique_ptr<LoginDialog> plain(login(prompt));
        QVERIFY(!plain->findChild<QCheckBox *>(QStringLiteral("keepUnlocked")));
        QVERIFY(!plain->keepUnlocked());
        QVERIFY(says(plain.get(), QStringLiteral("you'll be asked for this passphrase every time")));
        QVERIFY(says(plain.get(), QStringLiteral("no ssh-agent is running")));
    }

    // ssh-add hands a key to an agent of the test's own, the passphrase coming
    // from memory through Omagit's own askpass; a passphrase that does not
    // open the key is turned down once and reported, not tried again.
    void agentKeeperKeepsAKeyUnlocked()
    {
        const QString binary = helperBinary();
        if (binary.isEmpty())
            QSKIP("omagit is not built (qmake6 omagit.pro && make); the askpass helper is the binary itself");
        const QString sshAgent = QStandardPaths::findExecutable(QStringLiteral("ssh-agent"));
        const QString sshAdd = QStandardPaths::findExecutable(QStringLiteral("ssh-add"));
        const QString sshKeygen = QStandardPaths::findExecutable(QStringLiteral("ssh-keygen"));
        if (sshAgent.isEmpty() || sshAdd.isEmpty() || sshKeygen.isEmpty())
            QSKIP("ssh-agent, ssh-add or ssh-keygen is missing");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto keygen = [&](const QString &name, const QString &passphrase) {
            return QProcess::execute(sshKeygen, {QStringLiteral("-q"), QStringLiteral("-t"), QStringLiteral("ed25519"),
                                                 QStringLiteral("-N"), passphrase, QStringLiteral("-C"), name,
                                                 QStringLiteral("-f"), dir.filePath(name)}) == 0;
        };
        QVERIFY(keygen(QStringLiteral("good"), QStringLiteral("pass-123")));
        QVERIFY(keygen(QStringLiteral("other"), QStringLiteral("right-one")));

        const QString socket = dir.filePath(QStringLiteral("agent.sock"));
        QProcess agent;
        agent.start(sshAgent, {QStringLiteral("-D"), QStringLiteral("-a"), socket});
        QVERIFY(agent.waitForStarted());
        const auto stop = qScopeGuard([&agent] {
            agent.kill();
            agent.waitForFinished(2000);
        });
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(socket), 5000);
        ScopedEnv sock("SSH_AUTH_SOCK", socket.toUtf8());
        QVERIFY(sshkeys::agentReachable());

        AgentKeeper keeper;
        keeper.setHelperPath(binary);
        QSignalSpy finished(&keeper, &AgentKeeper::finished);
        keeper.add({AgentKey{dir.filePath(QStringLiteral("good")), QStringLiteral("pass-123")}});
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 30000);
        QVERIFY2(finished.first().first().toStringList().isEmpty(),
                 qPrintable(finished.first().first().toStringList().join(QLatin1Char('\n'))));
        const auto listed = [&] {
            QProcess list;
            list.start(sshAdd, {QStringLiteral("-l")});
            list.waitForFinished();
            return QString::fromUtf8(list.readAllStandardOutput());
        };
        QVERIFY(listed().contains(QStringLiteral("good")));

        keeper.add({AgentKey{dir.filePath(QStringLiteral("other")), QStringLiteral("wrong")}});
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 30000);
        QVERIFY(!finished.at(1).first().toStringList().isEmpty());
        QVERIFY(!listed().contains(QStringLiteral("other")));
    }

    // ssh asking whether to trust a host it has no key for is a question to
    // confirm, not to type an answer to: the dialog shows the fingerprint and
    // its button says "yes". It is nobody's credential, so there is no note
    // about helpers and no offer to remember it; and a key file's long path
    // may wrap after any slash instead of being cut off.
    void loginDialogConfirmsAHostKeyAndWrapsKeyPaths()
    {
        std::unique_ptr<LoginDialog> host(login(QStringLiteral(
            "The authenticity of host '[localhost]:2222 ([127.0.0.1]:2222)' can't be established.\n"
            "ED25519 key fingerprint is:\nSHA256:6SmzKAmTTz9KYV4ztf1H346dOHXEy3Kl1yrujM+HiBM\n"
            "Are you sure you want to continue connecting (yes/no/[fingerprint])? ")));
        QVERIFY(says(host.get(), QStringLiteral("Trust localhost:2222?")));
        QVERIFY(says(host.get(), QStringLiteral("SHA256:6SmzKAmTTz9KYV4ztf1H346dOHXEy3Kl1yrujM+HiBM")));
        QVERIFY(says(host.get(), QStringLiteral("ED25519")));
        QVERIFY(!host->findChild<QLineEdit *>(QStringLiteral("secretEdit"))->isVisible());
        QVERIFY(!host->findChild<QLineEdit *>(QStringLiteral("usernameEdit"))->isVisible());
        QVERIFY(!host->findChild<QCheckBox *>(QStringLiteral("rememberSignIn")));
        QVERIFY(!says(host.get(), QStringLiteral("credential helper")));
        QPushButton *trust = signInButton(host.get());
        QVERIFY(trust && trust->isEnabled());
        QCOMPARE(trust->text(), QStringLiteral("Trust and connect"));
        QCOMPARE(host->answer(), QStringLiteral("yes"));

        std::unique_ptr<LoginDialog> pin(login(QStringLiteral("Enter PIN for 'PIV Card Holder pin': ")));
        QVERIFY(!says(pin.get(), QStringLiteral("credential helper")));

        const QString key = QStringLiteral("/tmp/some/quite/long/directory/structure/for/keys/id_test");
        std::unique_ptr<LoginDialog> passphrase(login(QStringLiteral("Enter passphrase for key '%1': ").arg(key)));
        QLabel *heading = nullptr;
        for (QLabel *label : passphrase->findChildren<QLabel *>())
            if (label->text().startsWith(QStringLiteral("Unlock")))
                heading = label;
        QVERIFY(heading);
        QVERIFY(heading->text().contains(QStringLiteral("/​id_test")));
        QVERIFY(QString(heading->text()).remove(QChar(0x200B)).endsWith(key));
        QVERIFY(heading->width() <= passphrase->width());
        // Nothing to select and copy the invisible break points out of; the
        // path as it stands is on the tooltip.
        QCOMPARE(heading->textInteractionFlags(), Qt::NoTextInteraction);
        QCOMPARE(heading->toolTip(), QStringLiteral("Unlock ") + key);
        // ssh names at most 100 characters of the path; one that long was cut.
        const QString cut = QStringLiteral("/tmp/") + QString(95, QLatin1Char('k'));
        std::unique_ptr<LoginDialog> truncated(login(QStringLiteral("Enter passphrase for key '%1': ").arg(cut)));
        QVERIFY(says(truncated.get(), cut + QStringLiteral("…")));
    }

    // A password for a plain-http remote crosses the network as it is typed,
    // and the dialog says so — except for this machine's own loopback, which
    // nothing leaves.
    void loginDialogWarnsThatPlainHttpIsUnencrypted()
    {
        std::unique_ptr<LoginDialog> user(login(QStringLiteral("Username for 'http://example.com': ")));
        QVERIFY(says(user.get(), QStringLiteral("Plain HTTP")));
        std::unique_ptr<LoginDialog> password(login(QStringLiteral("Password for 'http://alice@example.com:8080': ")));
        QVERIFY(says(password.get(), QStringLiteral("Plain HTTP")));
        for (const QString prompt : {"Username for 'https://example.com': ", "Username for 'http://localhost:3300': ",
                                     "Username for 'http://127.0.0.1:8080': ", "Username for 'http://[::1]:8080': "}) {
            std::unique_ptr<LoginDialog> dialog(login(prompt));
            QVERIFY2(!says(dialog.get(), QStringLiteral("Plain HTTP")), qPrintable(prompt));
        }
    }

    // Which remote a prompt is about, without git in the way.
    void remoteUrlsStandForThePromptsTheyAnswer()
    {
        const QUrl target(QStringLiteral("https://example.com"));
        QVERIFY(remoteUrlMatchesTarget(QStringLiteral("https://example.com/team/repo.git"), target));
        QVERIFY(remoteUrlMatchesTarget(QStringLiteral("https://EXAMPLE.com/team/repo.git"), target));
        QVERIFY(!remoteUrlMatchesTarget(QStringLiteral("https://other.example/team/repo.git"), target));

        // The scheme's own port is filled in on either side, and another
        // scheme is another place — a password meant for https is not to be
        // matched against a remote that sends it in the clear.
        QVERIFY(remoteUrlMatchesTarget(QStringLiteral("https://example.com:443/x.git"), target));
        QVERIFY(!remoteUrlMatchesTarget(QStringLiteral("https://example.com:8443/x.git"), target));
        QVERIFY(!remoteUrlMatchesTarget(QStringLiteral("http://example.com/x.git"), target));
        QVERIFY(remoteUrlMatchesTarget(QStringLiteral("https://example.com:8443/x.git"),
                                       QUrl(QStringLiteral("https://example.com:8443"))));

        // Git names the user in the prompt of a password; the remote may name
        // it too, or (the username was asked for a moment ago) name none.
        const QUrl alice(QStringLiteral("https://alice@example.com"));
        QVERIFY(remoteUrlMatchesTarget(QStringLiteral("https://alice@example.com/x.git"), alice));
        QVERIFY(remoteUrlMatchesTarget(QStringLiteral("https://example.com/x.git"), alice));
        QVERIFY(!remoteUrlMatchesTarget(QStringLiteral("https://bob@example.com/x.git"), alice));

        // An ssh remote written the scp way is no URL, and a passphrase or a
        // question of git's own has no target for a remote to answer for.
        QVERIFY(!remoteUrlMatchesTarget(QStringLiteral("git@example.com:team/repo.git"), target));
        QVERIFY(!remoteUrlMatchesTarget(QStringLiteral("https://example.com/x.git"), QUrl()));
    }

    // Which helper keeps the password is a matter of the order git reads the
    // configuration in, not of how closely an entry matches: git appends every
    // helper that applies and empties the list again on every empty value. So
    // an unqualified `helper = store` written after `[credential
    // "https://example.com"] helper =` stores the password all the same, and
    // written before it does not — a difference `--get-urlmatch`, which
    // answers with the best match alone, cannot report. Neither repository
    // below needs the machine's own helpers cleared first: the empty entry
    // does that where it stands.
    void loginDialogFollowsTheOrderGitReadsCredentialHelpersIn()
    {
        const QString userPrompt = QStringLiteral("Username for 'https://example.com': ");

        // The empty entry for this remote comes first — it empties whatever
        // the machine configures — and `store` is appended after it.
        QTemporaryDir stores;
        QVERIFY(stores.isValid());
        QVERIFY(git(stores.path(), {"init", "-q", "-b", "main"}));
        QVERIFY(git(stores.path(), {"config", "credential.https://example.com.helper", ""}));
        QVERIFY(git(stores.path(), {"config", "--add", "credential.helper", "store"}));
        GitRepo storing(stores.path());
        std::unique_ptr<LoginDialog> stored(login(userPrompt, false, &storing));
        QVERIFY(says(stored.get(), QStringLiteral("Remembered by git's credential helper (store)")));

        // The other way round the empty entry comes last and empties the list
        // `store` was in: this remote is kept by nothing.
        QTemporaryDir forgets;
        QVERIFY(forgets.isValid());
        QVERIFY(git(forgets.path(), {"init", "-q", "-b", "main"}));
        QVERIFY(git(forgets.path(), {"config", "credential.helper", "store"}));
        QVERIFY(git(forgets.path(), {"config", "credential.https://example.com.helper", ""}));
        GitRepo forgetting(forgets.path());
        std::unique_ptr<LoginDialog> plain(login(userPrompt, false, &forgetting));
        QVERIFY(says(plain.get(), QStringLiteral("Not remembered")));
    }

    // The matching behind that note, without git in the way: git's urlmatch
    // rules, as far as a credential URL uses them.
    void credentialHelpersMatchTheUrlTheWayGitDoes()
    {
        // One entry as `git config -z --get-regexp` writes it: the key, a
        // newline, then the value.
        const auto entry = [](const QString &url, const QString &helper) {
            return (url.isEmpty() ? QStringLiteral("credential.helper")
                                  : QStringLiteral("credential.%1.helper").arg(url))
                + QLatin1Char('\n') + helper;
        };
        // Whether a helper configured for `url` alone would keep `target`.
        const auto keeps = [&entry](const QString &url, const QString &target) {
            return credentialHelpersFor({entry(url, QStringLiteral("store"))}, QUrl(target))
                == QStringList{QStringLiteral("store")};
        };

        // `*.` stands for one or more whole components in front of the host,
        // and never for none of them.
        QVERIFY(keeps("https://*.example.com", "https://code.example.com"));
        QVERIFY(keeps("https://*.example.com", "https://code.eu.example.com"));
        QVERIFY(!keeps("https://*.example.com", "https://example.com"));
        QVERIFY(!keeps("https://*.example.com", "https://notexample.com"));

        // The scheme's own port is filled in on both sides before they are
        // compared, so :443 and no port at all are the same https host — and
        // another port is another place.
        QVERIFY(keeps("https://example.com:443", "https://example.com"));
        QVERIFY(keeps("https://example.com", "https://example.com:443"));
        QVERIFY(!keeps("https://example.com:8443", "https://example.com"));
        QVERIFY(!keeps("http://example.com", "https://example.com"));

        // A path covers what lies under it, by whole components.
        QVERIFY(keeps("https://example.com/team", "https://example.com/team/repo"));
        QVERIFY(keeps("https://example.com/team/", "https://example.com/team"));
        QVERIFY(!keeps("https://example.com/team", "https://example.com/teamwork"));
        QVERIFY(!keeps("https://example.com/team", "https://example.com"));

        // A user in the entry has to be the user signing in; an entry naming
        // none is about everybody.
        QVERIFY(keeps("https://alice@example.com", "https://alice@example.com"));
        QVERIFY(!keeps("https://alice@example.com", "https://bob@example.com"));
        QVERIFY(!keeps("https://alice@example.com", "https://example.com"));
        QVERIFY(keeps("https://example.com", "https://bob@example.com"));

        // Order decides between two entries, not closeness of match.
        const QStringList clearThenStore{entry(QStringLiteral("https://example.com"), QString()),
                                         entry(QString(), QStringLiteral("store"))};
        const QStringList storeThenClear{clearThenStore.at(1), clearThenStore.at(0)};
        const QUrl remote(QStringLiteral("https://example.com"));
        QCOMPARE(credentialHelpersFor(clearThenStore, remote), QStringList{QStringLiteral("store")});
        QVERIFY(credentialHelpersFor(storeThenClear, remote).isEmpty());
        // The empty entry only empties the list where it applies.
        QCOMPARE(credentialHelpersFor(storeThenClear, QUrl(QStringLiteral("https://other.example"))),
                 QStringList{QStringLiteral("store")});

        // A passphrase, or a question of git's own, has no URL: nothing
        // qualified covers it and the unqualified entries are all it is told.
        QCOMPARE(credentialHelpersFor(storeThenClear, QUrl()), QStringList{QStringLiteral("store")});
        QVERIFY(credentialHelpersFor({entry(QStringLiteral("https://example.com"),
                                            QStringLiteral("store"))},
                                     QUrl())
                    .isEmpty());

        // Turned off is an empty list the configuration asked for: a reset
        // that applies with nothing after it. A helper after the reset, or no
        // entry at all, is something else.
        bool turnedOff = false;
        credentialHelpersFor(storeThenClear, remote, &turnedOff);
        QVERIFY(turnedOff);
        credentialHelpersFor(clearThenStore, remote, &turnedOff);
        QVERIFY(!turnedOff);
        turnedOff = true;
        credentialHelpersFor({}, remote, &turnedOff);
        QVERIFY(!turnedOff);
        // A reset for another host leaves this one as it was.
        turnedOff = true;
        credentialHelpersFor(storeThenClear, QUrl(QStringLiteral("https://other.example")), &turnedOff);
        QVERIFY(!turnedOff);
    }

    // The whole way round with git itself: `git credential fill` asks the
    // built binary, which is the askpass helper, which asks the server here.
    // No network is involved — git only wants the credentials.
    void askPassAnswersGitCredentialFill()
    {
        const QString binary = helperBinary();
        if (binary.isEmpty())
            QSKIP("omagit is not built (qmake6 omagit.pro && make); the askpass helper is the binary itself");

        AskPass askPass;
        askPass.setHelperPath(binary);
        QVERIFY(askPass.listen());
        int requests = 0;
        connect(&askPass, &AskPass::requestReceived, &askPass, [&](const AskPassRequest &request) {
            ++requests;
            if (request.kind == AskPassRequest::Username)
                askPass.answerLogin(request.id, QStringLiteral("alice"), QStringLiteral("s3cret"));
            else
                askPass.answerSecret(request.id, QStringLiteral("s3cret"));
        });

        QProcess git;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        for (const QString &entry : askPass.env())
            environment.insert(entry.section(QLatin1Char('='), 0, 0), entry.section(QLatin1Char('='), 1));
        environment.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
        git.setProcessEnvironment(environment);
        // credential.helper= empties the list, so no helper of this machine
        // answers before ours does.
        git.start(QStringLiteral("git"), {QStringLiteral("-c"), QStringLiteral("credential.helper="),
                                          QStringLiteral("credential"), QStringLiteral("fill")});
        QVERIFY(git.waitForStarted());
        git.write("protocol=https\nhost=example.com\n\n");
        git.closeWriteChannel();
        QTRY_VERIFY_WITH_TIMEOUT(git.state() == QProcess::NotRunning, 30000);
        const QString answer = QString::fromUtf8(git.readAllStandardOutput());
        QVERIFY2(answer.contains(QStringLiteral("username=alice")), qPrintable(answer));
        QVERIFY2(answer.contains(QStringLiteral("password=s3cret")), qPrintable(answer));
        // One dialog for the two questions: the password came from the cache.
        QCOMPARE(requests, 1);
    }
};

UI_TEST(SignInTest);

#include "signin_test.moc"
