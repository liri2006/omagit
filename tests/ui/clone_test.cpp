// The clone dialog: the URLs it takes and the folder it proposes, real clones
// over http and ssh with their sign-ins, the GitHub source (sign-in, pages,
// filter), failure and cancel, and the dialog's shape. No network is used.
#include "UiTestCase.h"
#include "fixtures.h"

#include "../../src/AskPass.h"
#include "../../src/BranchPicker.h"
#include "../../src/CloneDialog.h"
#include "../../src/LoginDialog.h"
#include "../../src/SshKeys.h"

#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QListWidget>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QStackedWidget>

namespace {

// The clone dialog's captions all carry the objectName the dim stylesheet
// rule needs, so they are found by what they are for instead.
QLabel *cloneLabel(const QDialog &dialog, const QString &accessibleName)
{
    for (QLabel *label : dialog.findChildren<QLabel *>())
        if (label->accessibleName() == accessibleName)
            return label;
    return nullptr;
}

} // namespace

class CloneTest : public UiTestCase
{
    Q_OBJECT
private slots:
    void cloneUrlsAndDefaults()
    {
        QCOMPARE(CloneDialog::defaultFolder(), QDir::currentPath());
        QCOMPARE(CloneDialog::defaultFolder("/tmp/projects/repo"), QString("/tmp/projects"));
        for (const QString url : {"https://github.com/owner/repo.git", "https://example.org/owner/repo/",
                                  "git@github.com:owner/repo.git", "ssh://git@example.org:2222/owner/repo.git",
                                  "work:owner/repo.git", "git@[::1]:owner/repo.git", "http://host/repo",
                                  "http://127.0.0.1:3300/owner/repo.git"})
            QCOMPARE(CloneDialog::repositoryName(url), QString("repo"));
        for (const QString url : {"", "--upload-pack=evil", "/tmp/repo", "file:///tmp/repo", "ftp://host/repo.git",
                                  "https://host", "https://host/..", "https://host/%2e%2e.git", "git@host:.git",
                                  "https://user:secret@host/repo", "https://host/repo?x", "git@host:repo\nother"})
            QVERIFY2(CloneDialog::repositoryName(url).isEmpty(), qPrintable(url));
    }

    void cloneDestinationValidation()
    {
        QTemporaryDir dir;
        CloneDialog dialog(dir.path());
        auto *url = dialog.findChild<QLineEdit *>("cloneUrl");
        auto *folder = dialog.findChild<QLineEdit *>("cloneFolder");
        auto *accept = dialog.findChild<QPushButton *>("cloneAccept");
        QVERIFY(!accept->isEnabled());
        url->setText("git@github.com:owner/repo.git");
        QVERIFY(accept->isEnabled());
        QCOMPARE(dialog.findChild<QLineEdit *>("cloneName")->text(), QString("repo"));
        QVERIFY(cloneLabel(dialog, "Destination")->text().contains(dir.path()));
        QVERIFY(QDir(dir.path()).mkdir("repo"));
        url->setText("https://github.com/owner/repo.git");
        QVERIFY(!accept->isEnabled());
        url->setText("https://github.com/owner/new.git");
        folder->clear();
        QVERIFY(!accept->isEnabled());
        folder->setText(dir.filePath("missing"));
        QVERIFY(!accept->isEnabled());
        folder->setText(dir.path());
        QVERIFY(accept->isEnabled());
        auto *name = dialog.findChild<QLineEdit *>("cloneName");
        for (const QString invalid : {"", ".", "..", "../outside", "/absolute", "nested/name", "bad\\name", " trailing "}) {
            name->setText(invalid);
            QVERIFY(!accept->isEnabled());
        }
        name->setText("repo");
        QVERIFY(!accept->isEnabled()); // existing destination
        name->setText("custom folder");
        QVERIFY(accept->isEnabled());
        url->setText("https://github.com/owner/another.git");
        QCOMPARE(name->text(), QString("custom folder"));
        QVERIFY(accept->isEnabled());
    }

    void cloneRealRepository()
    {
        QTemporaryDir source, destination, config;
        QVERIFY(git(source.path(), {"init", "-q", "-b", "main"}));
        QVERIFY(commit(source.path(), "Cloned commit", 1));
        // Git's URL rewrite keeps this a real clone with no network or real credentials.
        const QByteArray gitConfig = "[url \"" + source.path().toUtf8() + "\"]\n    insteadOf = https://clone.invalid/team/repo.git\n";
        QVERIFY(writeFixture(config.filePath("gitconfig"), gitConfig));
        ScopedEnv global("GIT_CONFIG_GLOBAL", config.filePath("gitconfig").toUtf8());
        ScopedEnv system("GIT_CONFIG_NOSYSTEM", "1");
        CloneDialog dialog(destination.path());
        dialog.findChild<QLineEdit *>("cloneUrl")->setText("https://clone.invalid/team/repo.git");
        dialog.findChild<QLineEdit *>("cloneName")->setText("custom folder");
        QSignalSpy accepted(&dialog, &QDialog::accepted);
        dialog.findChild<QPushButton *>("cloneAccept")->click();
        QTRY_COMPARE_WITH_TIMEOUT(accepted.size(), 1, 10000);
        QCOMPARE(dialog.repositoryPath(), destination.filePath("custom folder"));
        QVERIFY(dialog.cloned());
        GitRepo cloned(dialog.repositoryPath());
        QCOMPARE(cloned.headCommit().subject, QString("Cloned commit"));
    }

    // Plain http is somewhere to clone from too, and the sign-in of a clone
    // speaks of git's global configuration — a clone reads no repository's —
    // naming its helper by the helper's short name, the whole command on hover.
    void cloneOverHttpSignsInWithTheGlobalHelperNamed()
    {
        const QString binary = helperBinary();
        if (binary.isEmpty())
            QSKIP("omagit is not built (qmake6 omagit.pro && make); the askpass helper is the binary itself");
        std::unique_ptr<QTcpServer> server = unauthorizedServer();
        QVERIFY(server);
        QTemporaryDir destination, config;
        const QString helper = QStringLiteral("store --file=%1").arg(config.filePath("credentials"));
        QVERIFY(writeFixture(config.filePath("gitconfig"), "[credential]\n    helper = " + helper.toUtf8() + "\n"));
        ScopedEnv global("GIT_CONFIG_GLOBAL", config.filePath("gitconfig").toUtf8());
        ScopedEnv system("GIT_CONFIG_NOSYSTEM", "1");

        CloneDialog dialog(destination.path());
        dialog.show();
        dialog.findChild<AskPass *>()->setHelperPath(binary);
        dialog.findChild<QLineEdit *>("cloneUrl")->setText(
            QStringLiteral("http://127.0.0.1:%1/team/repo.git").arg(server->serverPort()));
        auto *accept = dialog.findChild<QPushButton *>("cloneAccept");
        QVERIFY(accept->isEnabled());
        accept->click();

        LoginDialog *login = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((login = dialog.findChild<LoginDialog *>()) && login->isVisible(), 30000);
        QVERIFY(says(login, QStringLiteral("Remembered by git's credential helper (store)")));
        bool tipped = false;
        for (const QLabel *label : login->findChildren<QLabel *>())
            tipped = tipped || label->toolTip() == helper;
        QVERIFY(tipped);
        // 127.0.0.1 is this machine: nothing crosses a network to warn about.
        QVERIFY(!says(login, QStringLiteral("Plain HTTP")));

        login->reject();
        QTRY_VERIFY_WITH_TIMEOUT(cloneLabel(dialog, "Status")->text().contains("Clone cancelled"), 30000);
        QVERIFY(!dialog.cloned());
    }

    void githubCloneKeepsCredentialsForLaterGitCommands()
    {
        QTemporaryDir source, destination, config, bin;
        QVERIFY(git(source.path(), {"init", "-q", "-b", "main"}));
        QVERIFY(commit(source.path(), "Private repository", 1));
        const QByteArray gitConfig = "[url \"" + source.path().toUtf8() + "\"]\n"
            "    insteadOf = https://github.com/fixture/repo.git\n"
            "[credential]\n    helper =\n";
        const QString configPath = config.filePath("gitconfig");
        QVERIFY(writeFixture(configPath, gitConfig));
        ScopedEnv global("GIT_CONFIG_GLOBAL", configPath.toUtf8());
        ScopedEnv system("GIT_CONFIG_NOSYSTEM", "1");
        ScopedEnv path("PATH", bin.path().toUtf8() + ':' + qgetenv("PATH"));
        QVERIFY(writeFixture(bin.filePath("gh"), R"(#!/bin/sh
if [ "$1" = auth ] && [ "$2" = git-credential ]; then
    if [ "$3" = get ]; then
        printf 'username=fixture-user\npassword=fixture-token\n'
    fi
    exit 0
fi
case "$4" in
user) printf '{"login":"fixture-user"}' ;;
*) printf '[{"full_name":"fixture/repo","clone_url":"https://github.com/fixture/repo.git"}]' ;;
esac
)", true));
        CloneDialog dialog(destination.path());
        dialog.findChild<QPushButton *>("cloneGitHubTab")->click();
        auto *list = dialog.findChild<QListWidget *>("cloneRepositories");
        QTRY_COMPARE(list->count(), 1);
        list->setCurrentRow(0);
        QSignalSpy accepted(&dialog, &QDialog::accepted);
        dialog.findChild<QPushButton *>("cloneAccept")->click();
        QTRY_COMPARE_WITH_TIMEOUT(accepted.size(), 1, 10000);
        GitRepo cloned(dialog.repositoryPath());
        QCOMPARE(cloned.run({"config", "--local", "--get-all", "credential.https://github.com.helper"}),
                 QByteArray("\n!gh auth git-credential\n"));
        QFile globalFile(configPath);
        QVERIFY(globalFile.open(QIODevice::ReadOnly));
        QCOMPARE(globalFile.readAll(), gitConfig);

        // A fresh Git process, with no clone-time overrides or askpass, must
        // still be able to retrieve credentials as an automatic fetch does.
        QProcess credential;
        credential.setWorkingDirectory(dialog.repositoryPath());
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("GIT_TERMINAL_PROMPT", "0");
        env.insert("GIT_ASKPASS", "/bin/false");
        credential.setProcessEnvironment(env);
        credential.start("git", {"credential", "fill"});
        QVERIFY(credential.waitForStarted());
        credential.write("protocol=https\nhost=github.com\n\n");
        credential.closeWriteChannel();
        QVERIFY(credential.waitForFinished(5000));
        QCOMPARE(credential.exitCode(), 0);
        const QByteArray answer = credential.readAllStandardOutput();
        QVERIFY(answer.contains("username=fixture-user"));
        QVERIFY(answer.contains("password=fixture-token"));
    }

    void cloneFailureAndCancellation()
    {
        QTemporaryDir bin, destination;
        QVERIFY(writeFixture(bin.filePath("git"), "#!/bin/sh\nprintf 'fatal: fixture failure\\n' >&2\nexit 1\n", true));
        ScopedEnv path("PATH", bin.path().toUtf8() + ':' + qgetenv("PATH"));
        CloneDialog dialog(destination.path());
        auto *accept = dialog.findChild<QPushButton *>("cloneAccept");
        auto *status = cloneLabel(dialog, "Status");
        dialog.findChild<QLineEdit *>("cloneUrl")->setText("git@host:repo.git");
        QSignalSpy accepted(&dialog, &QDialog::accepted);
        accept->click();
        QTRY_VERIFY(status->text().contains("fixture failure"));
        QVERIFY(accept->isEnabled());
        QCOMPARE(accepted.size(), 0);
        QVERIFY(writeFixture(bin.filePath("git"), "#!/bin/sh\nexec /bin/sleep 30\n", true));
        dialog.show();
        accept->click();
        QVERIFY(!accept->isEnabled());
        dialog.reject();
        QVERIFY(dialog.isVisible());
        QVERIFY(accept->isEnabled());
        QVERIFY(status->text().contains("stopped"));
        dialog.reject();
        QVERIFY(!dialog.isVisible());
        QCOMPARE(accepted.size(), 0);
    }

    void cloneGitHubPaginationAndFilter()
    {
        QTemporaryDir bin, destination;
        QJsonArray page;
        for (int i = 0; i < 100; ++i)
            page.append(QJsonObject{{"full_name", QString("team/repo%1").arg(i)},
                {"clone_url", QString("https://github.com/team/repo%1.git").arg(i)}, {"private", true}});
        QVERIFY(writeFixture(bin.filePath("page1"), QJsonDocument(page).toJson()));
        QVERIFY(writeFixture(bin.filePath("gh"), R"(#!/bin/sh
case "$4" in
user) printf '{"login":"fixture-user"}' ;;
*page=1) cat "$FIXTURE_DIR/page1" ;;
*page=2) printf '[{"full_name":"shared/last","clone_url":"https://github.com/shared/last.git"}]' ;;
*) exit 1 ;;
esac
)", true));
        ScopedEnv path("PATH", bin.path().toUtf8() + ':' + qgetenv("PATH"));
        ScopedEnv fixture("FIXTURE_DIR", bin.path().toUtf8());
        CloneDialog dialog(destination.path());
        dialog.show();
        dialog.findChild<QPushButton *>("cloneGitHubTab")->click();
        auto *list = dialog.findChild<QListWidget *>("cloneRepositories");
        auto *accept = dialog.findChild<QPushButton *>("cloneAccept");
        QTRY_COMPARE(list->count(), 101);
        QTRY_VERIFY(list->isEnabled());
        QVERIFY(!accept->isEnabled());
        auto *filter = dialog.findChild<QLineEdit *>("cloneSearch");
        filter->setFocus();
        QTRY_VERIFY(filter->hasFocus());
        QTest::keyClick(filter, Qt::Key_Down);
        QCOMPARE(list->currentRow(), 0);
        QTest::keyClick(filter, Qt::Key_Up);
        QCOMPARE(list->currentRow(), 100);
        QVERIFY(accept->isEnabled());
        auto *name = dialog.findChild<QLineEdit *>("cloneName");
        QCOMPARE(name->text(), QString("last"));
        name->setText("custom folder");
        filter->setText("TEAM/REPO");
        QVERIFY(list->item(100)->isHidden());
        QVERIFY(!accept->isEnabled());
        QTest::keyClick(filter, Qt::Key_Down);
        QCOMPARE(list->currentRow(), 0);
        QCOMPARE(name->text(), QString("repo0")); // the pick wins over the typed name
        QVERIFY(accept->isEnabled());
        filter->setText("repo1");
        QTest::keyClick(filter, Qt::Key_Down);
        QCOMPARE(list->currentRow(), 1);
        QTest::keyClick(filter, Qt::Key_Down);
        QCOMPARE(list->currentRow(), 10); // skip hidden rows 2–9
        QTest::keyClick(filter, Qt::Key_Up);
        QCOMPARE(list->currentRow(), 1);
        QTest::keyClick(filter, Qt::Key_Up);
        QCOMPARE(list->currentRow(), 19); // wrap among matches only
        QVERIFY(filter->hasFocus());
        QCOMPARE(filter->text(), QString("repo1"));
        QTest::keyClicks(filter, "9"); // typing still refines the filter
        QCOMPARE(filter->text(), QString("repo19"));
        QTest::keyClick(filter, Qt::Key_Down);
        QCOMPARE(list->currentRow(), 19);
        filter->setText("no matches");
        QTest::keyClick(filter, Qt::Key_Down);
        QTest::keyClick(filter, Qt::Key_Up);
        QCOMPARE(list->currentRow(), 19);
        QVERIFY(!accept->isEnabled());
        QVERIFY(cloneLabel(dialog, "Repository list state")->text().contains("No repositories match"));
        dialog.findChild<QPushButton *>("cloneUrlTab")->click();
        dialog.findChild<QLineEdit *>("cloneUrl")->setText("ssh://git@host/other.git");
        QVERIFY(accept->isEnabled());
    }

    void cloneGitHubSignInAndRetry()
    {
        QTemporaryDir bin, destination;
        QVERIFY(writeFixture(bin.filePath("gh"), R"(#!/bin/sh
if [ "$1" = auth ]; then
    printf 'Your one-time code: TEST-CODE\n' >&2
    touch "$FIXTURE_DIR/signed-in"
    exit 0
fi
if [ ! -f "$FIXTURE_DIR/signed-in" ]; then
    printf 'Not logged in\n' >&2
    exit 1
fi
case "$4" in
user) printf '{"login":"fixture-user"}' ;;
*) printf '[]' ;;
esac
)", true));
        ScopedEnv path("PATH", bin.path().toUtf8() + ':' + qgetenv("PATH"));
        ScopedEnv fixture("FIXTURE_DIR", bin.path().toUtf8());
        CloneDialog dialog(destination.path());
        dialog.show();
        dialog.findChild<QPushButton *>("cloneGitHubTab")->click();
        auto *login = dialog.findChild<QPushButton *>("cloneLogin");
        QTRY_VERIFY(login->isVisible() && login->isEnabled());
        QVERIFY(!dialog.findChild<QPushButton *>("cloneAccept")->isEnabled());
        login->click();
        QTRY_VERIFY(cloneLabel(dialog, "Repository list state")->text().contains("No repositories available"));
        QVERIFY(!login->isVisible());
    }

    // Nothing the dialog can say moves anything else about: the width is
    // fixed and the height follows whichever source page is on screen.
    void cloneDialogKeepsItsShape()
    {
        QTemporaryDir bin, destination;
        QJsonArray repos;
        for (const QString name : {"one", "two", "three"})
            repos.append(QJsonObject{{"full_name", "team/" + name},
                {"clone_url", QString("https://github.com/team/%1.git").arg(name)}});
        QVERIFY(writeFixture(bin.filePath("repos"), QJsonDocument(repos).toJson()));
        QVERIFY(writeFixture(bin.filePath("gh"), R"(#!/bin/sh
case "$4" in
user) printf '{"login":"fixture-user"}' ;;
*) cat "$FIXTURE_DIR/repos" ;;
esac
)", true));
        ScopedEnv path("PATH", bin.path().toUtf8() + ':' + qgetenv("PATH"));
        ScopedEnv fixture("FIXTURE_DIR", bin.path().toUtf8());
        QVERIFY(QDir(destination.path()).mkdir("taken"));
        CloneDialog dialog(destination.path());
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        // The refit is coalesced into the end of the event loop pass.
        const auto settle = [] { QTest::qWait(30); };
        settle();
        const int height = dialog.height();
        QCOMPARE(dialog.width(), 640);

        auto *url = dialog.findChild<QLineEdit *>("cloneUrl");
        url->setText("https://github.com/owner/taken.git");
        settle();
        QCOMPARE(dialog.height(), height); // the "Creates …" row was always there
        QVERIFY(cloneLabel(dialog, "Status")->text().contains("Already exists")); // two lines
        settle();
        QCOMPARE(dialog.height(), height);
        url->clear();
        settle();
        QCOMPARE(dialog.height(), height);

        QStackedWidget *sources = nullptr, *listArea = nullptr;
        for (auto *stack : dialog.findChildren<QStackedWidget *>())
            (stack->parentWidget() == &dialog ? sources : listArea) = stack;
        QVERIFY(sources && listArea);
        dialog.findChild<QPushButton *>("cloneGitHubTab")->click();
        auto *list = dialog.findChild<QListWidget *>("cloneRepositories");
        QTRY_COMPARE(list->count(), 3);
        settle();
        // The stack is as tall as the page on screen, not as the taller one.
        QCOMPARE(sources->height(), sources->currentWidget()->sizeHint().height());
        QCOMPARE(dialog.width(), 640);
        QCOMPARE(listArea->currentWidget(), static_cast<QWidget *>(list));
        dialog.findChild<QLineEdit *>("cloneSearch")->setText("no such repository");
        settle();
        QCOMPARE(listArea->currentWidget()->objectName(), QString("clonePlaceholder"));
        QCOMPARE(listArea->currentWidget()->height(), list->height());

        dialog.findChild<QPushButton *>("cloneUrlTab")->click();
        settle();
        QCOMPARE(dialog.height(), height);
    }

    // Every caption of the dialog is a dim one: an objectName of its own
    // would drop the stylesheet's QLabel#dimLabel rule.
    void cloneLabelsKeepTheDimStyle()
    {
        QTemporaryDir destination;
        CloneDialog dialog(destination.path());
        for (const QString name : {"Destination", "Status", "GitHub account", "Repository count",
                                   "Repository list state"}) {
            QLabel *label = cloneLabel(dialog, name);
            QVERIFY2(label, qPrintable(name));
            QCOMPARE(label->objectName(), QString("dimLabel"));
        }
    }

    // The clone dialog's key field: there for an ssh URL when ssh would not
    // find the key by itself, and what it holds goes into the new repository
    // as core.sshCommand (the clone -c saves it before the first fetch).
    void cloneOffersAnSshKeyForSshUrls()
    {
        QTemporaryDir home, source, destination, config;
        QVERIFY(home.isValid() && source.isValid() && destination.isValid() && config.isValid());
        const QString ssh = QDir(home.path()).filePath(QStringLiteral(".ssh"));
        QVERIFY(QDir().mkpath(ssh));
        const QByteArray pub = "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIJs+UX4FrcSY4qShNMKUNPQ9OiF4dYT6FglkbmVSUK6z test-key\n";
        QVERIFY(writeFixture(QDir(ssh).filePath(QStringLiteral("id_ed25519")), "private"));
        QVERIFY(writeFixture(QDir(ssh).filePath(QStringLiteral("id_ed25519.pub")), pub));
        ScopedEnv homeEnv("HOME", home.path().toUtf8());
        if (!sshkeys::overridingVariable().isEmpty())
            QSKIP("GIT_SSH_COMMAND is set, which outranks core.sshCommand");

        // One key under ssh's default name: ssh finds it anyway, no field.
        {
            CloneDialog dialog(destination.path());
            dialog.show();
            dialog.findChild<QLineEdit *>("cloneUrl")->setText("ssh://git@example.com/team/repo.git");
            QVERIFY(!dialog.findChild<BranchPicker *>("cloneSshKey")->isVisible());
        }

        const QString gitea = QDir(ssh).filePath(QStringLiteral("gitea"));
        QVERIFY(writeFixture(gitea, "private"));
        QVERIFY(writeFixture(gitea + QStringLiteral(".pub"), pub));
        QVERIFY(git(source.path(), {"init", "-q", "-b", "main"}));
        QVERIFY(commit(source.path(), "Cloned commit", 1));
        // Rewritten to a local path, so the clone needs no network — and no ssh.
        const QByteArray gitConfig = "[url \"" + source.path().toUtf8() + "\"]\n    insteadOf = ssh://git@clone.invalid/team/repo.git\n";
        QVERIFY(writeFixture(config.filePath("gitconfig"), gitConfig));
        ScopedEnv global("GIT_CONFIG_GLOBAL", config.filePath("gitconfig").toUtf8());
        ScopedEnv system("GIT_CONFIG_NOSYSTEM", "1");

        CloneDialog dialog(destination.path());
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        auto *url = dialog.findChild<QLineEdit *>("cloneUrl");
        auto *picker = dialog.findChild<BranchPicker *>("cloneSshKey");
        url->setText("https://example.com/team/repo.git");
        QVERIFY(!picker->isVisible());
        url->setText("ssh://git@clone.invalid/team/repo.git");
        QVERIFY(picker->isVisible());
        QCOMPARE(picker->branch(), QStringLiteral("ssh's choice"));
        // Tab goes from the URL to the key that goes with it.
        QVERIFY(activate(&dialog));
        url->setFocus();
        QTest::keyClick(url, Qt::Key_Tab);
        QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget *>(picker));
        dialog.setSshKey(gitea);
        QCOMPARE(picker->branch(), QStringLiteral("gitea"));

        QSignalSpy accepted(&dialog, &QDialog::accepted);
        dialog.findChild<QPushButton *>("cloneAccept")->click();
        QTRY_COMPARE_WITH_TIMEOUT(accepted.size(), 1, 10000);
        GitRepo cloned(dialog.repositoryPath());
        QCOMPARE(sshkeys::repoCommand(&cloned), sshkeys::sshCommand(gitea));
    }

    // A clone the server turns the key down for brings the key field up,
    // even with no key pair in ~/.ssh to list — its menu still has "Other
    // key file…" — and the status says where to look. ssh here is a script
    // that refuses every key.
    void aCloneRefusedForItsKeyShowsTheKeyField()
    {
        if (!sshkeys::overridingVariable().isEmpty())
            QSKIP("GIT_SSH_COMMAND is set, so git would not run the ssh on PATH");
        QTemporaryDir home, bin, destination, config;
        QVERIFY(home.isValid() && bin.isValid() && destination.isValid() && config.isValid());
        QVERIFY(QDir().mkpath(QDir(home.path()).filePath(QStringLiteral(".ssh"))));
        QVERIFY(writeFixture(bin.filePath(QStringLiteral("ssh")),
                             "#!/bin/sh\necho 'git@example.invalid: Permission denied (publickey).' >&2\nexit 255\n",
                             true));
        QVERIFY(writeFixture(config.filePath(QStringLiteral("gitconfig")), QByteArray()));
        ScopedEnv homeEnv("HOME", home.path().toUtf8());
        ScopedEnv path("PATH", bin.path().toUtf8() + ':' + qgetenv("PATH"));
        ScopedEnv global("GIT_CONFIG_GLOBAL", config.filePath(QStringLiteral("gitconfig")).toUtf8());
        ScopedEnv system("GIT_CONFIG_NOSYSTEM", "1");

        CloneDialog dialog(destination.path());
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        auto *picker = dialog.findChild<BranchPicker *>(QStringLiteral("cloneSshKey"));
        QVERIFY(picker);
        dialog.findChild<QLineEdit *>(QStringLiteral("cloneUrl"))->setText(
            QStringLiteral("ssh://git@example.invalid/team/repo.git"));
        QVERIFY(!picker->isVisible()); // nothing to choose from yet
        auto *accept = dialog.findChild<QPushButton *>(QStringLiteral("cloneAccept"));
        QVERIFY(accept->isEnabled());
        accept->click();
        QLabel *status = cloneLabel(dialog, QStringLiteral("Status"));
        QVERIFY(status);
        QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(QStringLiteral("SSH key")), 30000);
        QVERIFY2(status->text().startsWith(QStringLiteral("Clone failed")), qPrintable(status->text()));
        QVERIFY(picker->isVisible());
        QVERIFY(!dialog.cloned());
    }
};

UI_TEST(CloneTest);

#include "clone_test.moc"
