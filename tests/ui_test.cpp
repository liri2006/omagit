// Build in tests: qmake6 ui.pro -o Makefile.ui && make -f Makefile.ui
// Run: QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME= QT_STYLE_OVERRIDE=Fusion ../build/tests/ui_test
// Exercises the pure logic behind the widgets: the history graph layout, the
// changes model's check marks, the toolbar's overflow, the keybindings filter,
// the theme's colors.toml parsing and the merge verdict's wording — and, with
// git itself but no network, the way a fetch signs in.
#include "../src/BranchMenu.h"
#include "../src/ChangesModel.h"
#include "../src/CommitPage.h"
#include "../src/CloneDialog.h"
#include "../src/HistoryModel.h"
#include "../src/AskPass.h"
#include "../src/KeybindingsPanel.h"
#include "../src/LoginDialog.h"
#include "../src/MergeDialog.h"
#include "../src/MessageEdit.h"
#include "../src/OmarchyTheme.h"
#include "../src/RemoteSync.h"
#include "../src/Settings.h"
#include "../src/Toolbar.h"
#include "../src/UiHelpers.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMouseEvent>
#include <QListWidget>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QSplitterHandle>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTableView>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <functional>
#include <memory>

// The theme the whole run shares; the theme test puts a fresh one here after
// it has pointed OmarchyTheme at a scratch directory of its own.
static std::unique_ptr<OmarchyTheme> g_theme;

namespace {

class ScopedEnv {
public:
    ScopedEnv(const char *key, const QByteArray &value) : key(key), old(qgetenv(key)), existed(qEnvironmentVariableIsSet(key)) { qputenv(key, value); }
    ~ScopedEnv() { if (existed) qputenv(key, old); else qunsetenv(key); }
private:
    const char *key;
    QByteArray old;
    bool existed;
};

bool writeFixture(const QString &path, const QByteArray &data, bool executable = false)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size())
        return false;
    file.close();
    return !executable || file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
}

// Committer dates decide the order `git log --date-order` returns, so every
// commit gets one of its own: 2024-01-01 01:00, 02:00, ...
QString stamp(int hour)
{
    return QStringLiteral("2024-01-01T%1:00:00+00:00").arg(hour, 2, 10, QLatin1Char('0'));
}

bool git(const QString &dir, const QStringList &args, int hour = 0)
{
    QProcess p;
    p.setWorkingDirectory(dir);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("GIT_AUTHOR_NAME"), QStringLiteral("Test"));
    env.insert(QStringLiteral("GIT_AUTHOR_EMAIL"), QStringLiteral("test@example.com"));
    env.insert(QStringLiteral("GIT_COMMITTER_NAME"), QStringLiteral("Test"));
    env.insert(QStringLiteral("GIT_COMMITTER_EMAIL"), QStringLiteral("test@example.com"));
    if (hour > 0) {
        env.insert(QStringLiteral("GIT_AUTHOR_DATE"), stamp(hour));
        env.insert(QStringLiteral("GIT_COMMITTER_DATE"), stamp(hour));
    }
    p.setProcessEnvironment(env);
    p.start(QStringLiteral("git"), QStringList{QStringLiteral("-c"), QStringLiteral("commit.gpgsign=false")} + args);
    if (!p.waitForFinished(15000) || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        qWarning("git %s failed: %s", qPrintable(args.join(' ')), p.readAllStandardError().constData());
        return false;
    }
    return true;
}

bool commit(const QString &dir, const QString &message, int hour)
{
    return git(dir, {QStringLiteral("commit"), QStringLiteral("--allow-empty"), QStringLiteral("-q"),
                     QStringLiteral("-m"), message}, hour);
}

FileChange change(const QString &path, FileChange::Kind kind)
{
    FileChange c;
    c.path = path;
    c.kind = kind;
    return c;
}

MergePreview preview(MergePreview::Outcome outcome)
{
    MergePreview p;
    p.outcome = outcome;
    p.source = QStringLiteral("feature");
    p.destination = QStringLiteral("main");
    p.commits = 2;
    p.diverged = 1;
    p.files = 3;
    p.added = 10;
    p.removed = 4;
    return p;
}

// The queued height check of the message box runs from the event loop, so
// the assertions have to let it.
void settle()
{
    QTest::qWait(30);
}

// One of each control the kit measures in scaled pixels, in a shown host so
// the stylesheet's own padding is part of the numbers. The live text-size
// test builds one, changes the desktop's text size under it, and holds it
// against a kit built fresh at the new size.
struct Kit
{
    std::unique_ptr<QWidget> host;
    QToolButton *inlineButton = nullptr;
    QToolButton *toolbarButton = nullptr;
    QToolButton *textButton = nullptr; // what a toolbar icon button has to be as tall as
    QLineEdit *promptField = nullptr;
    QWidget *promptBox = nullptr;
    QWidget *header = nullptr;

    QList<int> metrics() const
    {
        return {inlineButton->width(),       inlineButton->height(),
                toolbarButton->width(),      toolbarButton->height(),
                promptField->height(),       promptBox->sizeHint().width(),
                promptBox->sizeHint().height(), header->height()};
    }
};

Kit buildKit()
{
    Kit kit;
    kit.host.reset(new QWidget);
    auto *rows = new QVBoxLayout(kit.host.get());
    kit.inlineButton = ui::iconButton(ui::kCog, QStringLiteral("⚙"), QStringLiteral("Agent"));
    kit.toolbarButton = ui::iconButton(ui::kRefresh, QStringLiteral("R"), QStringLiteral("Refresh"),
                                       ui::IconButtonSize::Toolbar, false);
    kit.textButton = ui::toolButton(QStringLiteral("x"));
    kit.promptField = ui::promptField(QStringLiteral("Search branches…"));
    kit.promptBox = ui::promptBox(kit.promptField);
    kit.header = new QWidget;
    kit.header->setLayout(ui::sectionHeaderRow(ui::sectionLabel(QStringLiteral("MESSAGE"))));
    rows->addWidget(kit.inlineButton);
    rows->addWidget(kit.toolbarButton);
    rows->addWidget(kit.textButton);
    rows->addWidget(kit.promptBox);
    rows->addWidget(kit.header);
    rows->addStretch(1); // the leftover height is the stretch's, not a control's
    kit.host->resize(400, 400); // both kits are laid out in the same box
    kit.host->show();
    return kit;
}

// What a kit built from scratch at the text size of the moment measures.
QList<int> freshKitMetrics()
{
    Kit kit = buildKit();
    QTest::qWaitForWindowExposed(kit.host.get());
    settle();
    return kit.metrics();
}

// A commit page on a repository of its own: one commit, two files changed
// since it, and two unversioned ones beside them — so the list starts with
// two of its four files checked.
struct CommitFixture
{
    std::unique_ptr<QTemporaryDir> dir;
    std::unique_ptr<GitRepo> repo;
    std::unique_ptr<CommitPage> page;

    ChangesModel *model() const
    {
        return static_cast<ChangesModel *>(static_cast<QSortFilterProxyModel *>(page->table()->model())->sourceModel());
    }
    ChangesHeader *header() const { return qobject_cast<ChangesHeader *>(page->table()->horizontalHeader()); }
    QPushButton *commitButton() const { return page->findChild<QPushButton *>(); }
    QCheckBox *amend() const { return page->findChild<QCheckBox *>(); }
    // The only checkable square of the header row is the unversioned eye.
    QToolButton *eye() const
    {
        for (QToolButton *b : page->findChildren<QToolButton *>(QStringLiteral("iconButton")))
            if (b->isCheckable())
                return b;
        return nullptr;
    }
    QString title() const
    {
        for (const QLabel *l : page->findChildren<QLabel *>(QStringLiteral("sectionLabel")))
            if (l->text().startsWith(QStringLiteral("CHANGES")))
                return l->text();
        return QString();
    }
    // The check-all box of the header, as the header itself reads it.
    int checkAll() const
    {
        return page->table()->model()->headerData(ChangesModel::Check, Qt::Horizontal, Qt::CheckStateRole).toInt();
    }
    QPoint sectionCentre(int section) const
    {
        QHeaderView *h = page->table()->horizontalHeader();
        return QPoint(h->sectionViewportPosition(section) + h->sectionSize(section) / 2,
                      h->viewport()->height() / 2);
    }
    void clickSection(int section) const
    {
        QTest::mouseClick(page->table()->horizontalHeader()->viewport(), Qt::LeftButton, {}, sectionCentre(section));
    }
    // Two clicks fast enough for Qt to call the second one a double click.
    void doubleClickSection(int section) const
    {
        QTest::mouseDClick(page->table()->horizontalHeader()->viewport(), Qt::LeftButton, {}, sectionCentre(section));
    }
    void clickCell(int row, int column) const
    {
        QTableView *t = page->table();
        QTest::mouseClick(t->viewport(), Qt::LeftButton, {}, t->visualRect(t->model()->index(row, column)).center());
    }
    QStringList checkedPaths() const
    {
        QStringList paths = model()->checkedPaths();
        paths.sort();
        return paths;
    }
};

// The page on its own, so a second one can be built beside the fixture's.
std::unique_ptr<CommitPage> commitPage(GitRepo *repo)
{
    // The page asks the agent CLIs on PATH for their models as it is built;
    // a PATH with only the repository on it keeps those processes out of
    // these tests.
    const QByteArray env = qgetenv("PATH");
    qputenv("PATH", repo->root().toUtf8());
    auto page = std::make_unique<CommitPage>(repo);
    qputenv("PATH", env);
    page->resize(760, 600);
    page->show();
    page->reload();
    return page;
}

CommitFixture commitFixture()
{
    CommitFixture f;
    f.dir.reset(new QTemporaryDir);
    const QString path = f.dir->path();
    if (!f.dir->isValid() || !git(path, {"init", "-q", "-b", "main"}))
        return f;
    writeFixture(QDir(path).filePath(QStringLiteral("a.txt")), "a\n");
    writeFixture(QDir(path).filePath(QStringLiteral("b.txt")), "b\n");
    if (!git(path, {"add", "-A"}) || !git(path, {"commit", "-q", "-m", "first"}, 1))
        return f;
    writeFixture(QDir(path).filePath(QStringLiteral("a.txt")), "a changed\n");
    writeFixture(QDir(path).filePath(QStringLiteral("b.txt")), "b changed\n");
    writeFixture(QDir(path).filePath(QStringLiteral("u1.txt")), "u1\n");
    writeFixture(QDir(path).filePath(QStringLiteral("u2.txt")), "u2\n");
    f.repo.reset(new GitRepo(path));
    f.page = commitPage(f.repo.get());
    return f;
}

// What a commit page measures in scaled pixels, spelled out so a mismatch
// names itself: the design's checkbox column, the gaps of the section grid,
// the handle the message and the list share, and the divider of the CHANGES
// row.
QStringList pageMetrics(CommitPage *page)
{
    auto *splitter = page->findChild<QSplitter *>(QStringLiteral("commitMessageSplitter"));
    QWidget *changes = splitter ? splitter->widget(1) : nullptr;
    // The one child a single pixel wide is the divider between the eye and
    // Refresh; its height is the page's to set.
    int divider = -1;
    for (const QWidget *w : page->findChildren<QWidget *>())
        if (w->minimumWidth() == 1 && w->maximumWidth() == 1)
            divider = w->height();
    const QCheckBox *amend = page->findChild<QCheckBox *>();
    const auto entry = [](const char *name, int value) { return QStringLiteral("%1=%2").arg(QLatin1String(name)).arg(value); };
    return {entry("check", page->table()->columnWidth(ChangesModel::Check)),
            entry("row", page->table()->verticalHeader()->defaultSectionSize()),
            entry("actionBarGap", page->layout()->spacing()),
            entry("headerGap", changes ? changes->layout()->spacing() : -1),
            entry("handle", splitter ? splitter->handle(1)->height() : -1),
            entry("divider", divider),
            entry("amendWidth", amend->width()),
            QStringLiteral("amend=") + amend->text()};
}

// The narrowest page that still spells "Amend last commit" out: what the
// action bar folds at, and so the width to compare two pages at.
int amendFoldWidth(CommitPage *page)
{
    int folded = 160, whole = 900; // the label is short at one end, long at the other
    while (folded + 1 < whole) {
        const int middle = (folded + whole) / 2;
        page->resize(middle, 600);
        settle();
        if (page->findChild<QCheckBox *>()->text() == QLatin1String("Amend"))
            folded = middle;
        else
            whole = middle;
    }
    return whole;
}

// A remote git can reach but can never sign in to: every request is answered
// with the 401 and the Basic challenge that make git ask for a username and a
// password, and then the connection is closed. Nothing of git's own protocol
// is served — the sign-in never gets that far. git opens a connection per
// request, so the server goes on accepting for as long as the test lives.
std::unique_ptr<QTcpServer> unauthorizedServer()
{
    auto server = std::make_unique<QTcpServer>();
    QObject::connect(server.get(), &QTcpServer::newConnection, server.get(), [listener = server.get()] {
        while (QTcpSocket *socket = listener->nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                // Answer once the request headers are in: closing on a socket
                // with unread data would reach curl as a reset, not a 401.
                const QByteArray request = socket->property("request").toByteArray() + socket->readAll();
                if (!request.contains("\r\n\r\n")) {
                    socket->setProperty("request", request);
                    return;
                }
                socket->write("HTTP/1.1 401 Unauthorized\r\n"
                              "WWW-Authenticate: Basic realm=\"test\"\r\n"
                              "Content-Length: 0\r\n"
                              "Connection: close\r\n"
                              "\r\n");
                socket->disconnectFromHost();
            });
        }
    });
    if (!server->listen(QHostAddress::LocalHost))
        return {};
    return server;
}

// A repository whose remotes all point at that server: one commit, and a main
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

// The clone dialog's captions all carry the objectName the dim stylesheet
// rule needs, so they are found by what they are for instead.
QLabel *cloneLabel(const QDialog &dialog, const QString &accessibleName)
{
    for (QLabel *label : dialog.findChildren<QLabel *>())
        if (label->accessibleName() == accessibleName)
            return label;
    return nullptr;
}

// The buttons of a toolbar that are on screen, in the order they were added.
QList<bool> visible(const QList<QToolButton *> &buttons)
{
    QList<bool> out;
    for (const QToolButton *b : buttons)
        out << b->isVisible();
    return out;
}

} // namespace

class UiTest : public QObject
{
    Q_OBJECT
private slots:
    void cloneUrlsAndDefaults()
    {
        QCOMPARE(CloneDialog::defaultFolder(), QDir::currentPath());
        QCOMPARE(CloneDialog::defaultFolder("/tmp/projects/repo"), QString("/tmp/projects"));
        for (const QString url : {"https://github.com/owner/repo.git", "https://example.org/owner/repo/",
                                  "git@github.com:owner/repo.git", "ssh://git@example.org:2222/owner/repo.git",
                                  "work:owner/repo.git", "git@[::1]:owner/repo.git"})
            QCOMPARE(CloneDialog::repositoryName(url), QString("repo"));
        for (const QString url : {"", "--upload-pack=evil", "/tmp/repo", "file:///tmp/repo", "http://host/repo",
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
        GitRepo cloned(dialog.repositoryPath());
        QCOMPARE(cloned.headCommit().subject, QString("Cloned commit"));
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

    // --- HistoryModel -------------------------------------------------------

    void graphOfLinearHistory()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(git(dir.path(), {"init", "-q", "-b", "main"}));
        QVERIFY(commit(dir.path(), QStringLiteral("A"), 1));
        QVERIFY(commit(dir.path(), QStringLiteral("B"), 2));
        QVERIFY(commit(dir.path(), QStringLiteral("C"), 3));

        GitRepo repo(dir.path());
        HistoryModel model(&repo);
        model.reload();
        QCOMPARE(model.rowCount(), 3);
        QCOMPARE(model.laneCount(), 1);
        for (int row = 0; row < 3; ++row)
            QCOMPARE(model.graph(row).lane, 0);
        // The tip starts its line, the root ends it.
        QCOMPARE(model.graph(0).edges.size(), 1);
        QCOMPARE(model.graph(0).edges.first().from, -1);
        QCOMPARE(model.graph(0).edges.first().to, 0);
        QCOMPARE(model.graph(2).edges.size(), 1);
        QCOMPARE(model.graph(2).edges.first().from, 0);
        QCOMPARE(model.graph(2).edges.first().to, -1);
        QVERIFY(model.isHead(0));
    }

    // Two branches that are merged one after the other: the second one must
    // take the lane the first gave back instead of opening a third.
    void graphOfBranchesAndMerges()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path();
        QVERIFY(git(path, {"init", "-q", "-b", "main"}));
        QVERIFY(commit(path, QStringLiteral("A"), 1));
        QVERIFY(git(path, {"branch", "f1"}));
        QVERIFY(commit(path, QStringLiteral("B"), 2));
        QVERIFY(git(path, {"checkout", "-q", "f1"}));
        QVERIFY(commit(path, QStringLiteral("C"), 3));
        QVERIFY(git(path, {"checkout", "-q", "main"}));
        QVERIFY(git(path, {"merge", "--no-ff", "--no-edit", "-q", "-m", "M1", "f1"}, 4));
        QVERIFY(git(path, {"branch", "f2"}));
        QVERIFY(git(path, {"checkout", "-q", "f2"}));
        QVERIFY(commit(path, QStringLiteral("D"), 5));
        QVERIFY(git(path, {"checkout", "-q", "main"}));
        QVERIFY(git(path, {"merge", "--no-ff", "--no-edit", "-q", "-m", "M2", "f2"}, 6));

        GitRepo repo(dir.path());
        HistoryModel model(&repo);
        model.reload();
        QCOMPARE(model.rowCount(), 6);
        QStringList subjects;
        for (int row = 0; row < model.rowCount(); ++row)
            subjects << model.commit(row).subject;
        QCOMPARE(subjects, QStringList({"M2", "D", "M1", "C", "B", "A"}));
        // The second branch reuses lane 1, so two lanes hold the whole graph.
        QCOMPARE(model.laneCount(), 2);
        QList<int> lanes;
        for (int row = 0; row < model.rowCount(); ++row)
            lanes << model.graph(row).lane;
        QCOMPARE(lanes, QList<int>({0, 1, 0, 1, 0, 1}));
        // The merge branches out of its node into the second parent's lane.
        const GraphRow &merge = model.graph(0);
        QCOMPARE(merge.edges.size(), 2);
        QCOMPARE(merge.edges.last().from, -1);
        QCOMPARE(merge.edges.last().to, 1);
    }

    // --- ChangesModel -------------------------------------------------------

    void checkMarksSurviveARefresh()
    {
        ChangesModel model;
        model.setChanges({change("a.txt", FileChange::Modified), change("b.txt", FileChange::Untracked)});
        // A first load checks the versioned changes and leaves the rest alone.
        QCOMPARE(model.checkedCount(), 1);
        QCOMPARE(model.checkedPaths(), QStringList({"a.txt"}));

        model.setPathsChecked({QStringLiteral("b.txt")}, true);
        QCOMPARE(model.checkedCount(), 2);

        // b.txt is gone, c.txt is new: the choice for a.txt survives, the new
        // file starts unchecked and the vanished one leaves nothing behind.
        model.setChanges({change("a.txt", FileChange::Modified), change("c.txt", FileChange::Untracked)});
        QCOMPARE(model.checkedPaths(), QStringList({"a.txt"}));
        model.setChanges({change("a.txt", FileChange::Modified), change("b.txt", FileChange::Untracked),
                          change("c.txt", FileChange::Untracked)});
        QCOMPARE(model.checkedPaths(), QStringList({"a.txt"}));
    }

    void setAllCheckedOnAnEmptyModelNamesNoRow()
    {
        ChangesModel model;
        QSignalSpy checked(&model, &ChangesModel::checkedChanged);
        QSignalSpy changed(&model, &ChangesModel::dataChanged);
        model.setAllChecked(true);
        model.setUnversionedChecked(true);
        model.setPathsChecked({QStringLiteral("gone.txt")}, true);
        QCOMPARE(checked.count(), 3); // the tristate box still settles on 0 / 0
        QCOMPARE(changed.count(), 0); // but there is no row to name in dataChanged
        QCOMPARE(model.checkedCount(), 0);
        QCOMPARE(model.rowCount(), 0);
    }

    void statusSortsModifiedFirstAndUntrackedLast()
    {
        QCOMPARE(ChangesModel::statusRank(FileChange::Modified), 0);
        QVERIFY(ChangesModel::statusRank(FileChange::Modified) < ChangesModel::statusRank(FileChange::Added));
        QVERIFY(ChangesModel::statusRank(FileChange::Added) < ChangesModel::statusRank(FileChange::Deleted));
        for (int kind = FileChange::Modified; kind <= FileChange::Unknown; ++kind)
            if (kind != FileChange::Untracked)
                QVERIFY(ChangesModel::statusRank(FileChange::Kind(kind))
                        < ChangesModel::statusRank(FileChange::Untracked));
    }

    // --- Toolbar ------------------------------------------------------------

    void buttonsFoldIntoTheMoreMenuFromTheRight()
    {
        Toolbar bar;
        auto *leading = ui::toolButton(QStringLiteral("L"));
        bar.setLeading(leading);
        QList<QToolButton *> buttons;
        QWidget *separator = nullptr;
        const QStringList names{QStringLiteral("Fetch"), QStringLiteral("Pull"), QStringLiteral("Push"),
                                QStringLiteral("Merge")};
        for (int i = 0; i < names.size(); ++i) {
            auto *button = ui::toolButton(names.at(i));
            buttons << button;
            bar.addButton(button, names.at(i), names.at(i).left(1), names.at(i));
            if (i != 1)
                continue;
            // The separator between Pull and Push: whatever addSeparator() adds.
            const QList<QWidget *> before = bar.findChildren<QWidget *>();
            bar.addSeparator();
            for (QWidget *child : bar.findChildren<QWidget *>())
                if (!before.contains(child))
                    separator = child;
        }
        QVERIFY(separator);
        bar.resize(bar.sizeHint());
        bar.show();
        QVERIFY(QTest::qWaitForWindowExposed(&bar));

        const int wide = bar.sizeHint().width();
        bar.resize(wide + 40, bar.sizeHint().height());
        QCoreApplication::processEvents();
        QCOMPARE(visible(buttons), QList<bool>({true, true, true, true}));
        for (int i = 0; i < buttons.size(); ++i)
            QCOMPARE(buttons.at(i)->text(), names.at(i)); // full labels while there is room
        QVERIFY(separator->isVisible());

        // Shrinking hides the buttons from the right, never from the middle.
        int hiddenAt = -1;
        for (int width = wide + 40; width >= bar.minimumSizeHint().width(); width -= 8) {
            bar.resize(width, bar.sizeHint().height());
            QCoreApplication::processEvents();
            const QList<bool> shown = visible(buttons);
            bool seenHidden = false;
            for (int i = 0; i < shown.size(); ++i) {
                if (!shown.at(i))
                    seenHidden = true;
                else
                    QVERIFY2(!seenHidden, "a button folded away while a later one stayed");
            }
            if (seenHidden && hiddenAt < 0)
                hiddenAt = width;
            if (seenHidden) {
                QVERIFY(bar.findChild<QToolButton *>()); // the more button takes over
                if (!shown.at(2)) // Push and Merge gone: the separator has nothing to separate
                    QVERIFY(!separator->isVisible());
            }
            QVERIFY(leading->isVisible()); // the layout switcher never folds away
        }
        QVERIFY2(hiddenAt > 0, "nothing ever folded away");

        // At its narrowest only the leading button and the more menu are left.
        bar.resize(bar.minimumSizeHint().width(), bar.sizeHint().height());
        QCoreApplication::processEvents();
        QCOMPARE(visible(buttons), QList<bool>({false, false, false, false}));

        // And they all come back when the room does.
        bar.resize(wide + 40, bar.sizeHint().height());
        QCoreApplication::processEvents();
        QCOMPARE(visible(buttons), QList<bool>({true, true, true, true}));
        QVERIFY(separator->isVisible());
    }

    // --- UiHelpers: the kit primitives --------------------------------------

    // An inline one is a square of scaled pixels; a toolbar one is that wide
    // and exactly as tall as the text button beside it. The stylesheet tells
    // the kinds apart by the buttons' own properties.
    void iconButtonsAreSquaresOfTheDesignsSizes()
    {
        std::unique_ptr<QToolButton> inline_(ui::iconButton(ui::kCog, QStringLiteral("⚙"), QStringLiteral("Agent")));
        QCOMPARE(inline_->objectName(), QString("iconButton"));
        QCOMPARE(inline_->property("ghost").toBool(), true);
        QCOMPARE(inline_->property("toolbar").toBool(), false);
        QCOMPARE(inline_->size(), QSize(ui::space(24), ui::space(24)));
        QCOMPARE(inline_->minimumSize(), inline_->maximumSize()); // fixed, so the glyph stays centred

        QWidget host;
        auto *row = new QHBoxLayout(&host);
        QToolButton *toolbar = ui::iconButton(ui::kRefresh, QStringLiteral("R"), QStringLiteral("Refresh"),
                                              ui::IconButtonSize::Toolbar, false);
        QToolButton *text = ui::toolButton(QStringLiteral("x"));
        row->addWidget(toolbar);
        row->addWidget(text);
        host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        settle();

        QCOMPARE(toolbar->property("ghost").toBool(), false);
        QCOMPARE(toolbar->property("toolbar").toBool(), true);
        QCOMPARE(toolbar->width(), ui::space(28));
        QCOMPARE(toolbar->minimumWidth(), ui::space(28));
        QCOMPARE(toolbar->maximumWidth(), ui::space(28));
        // The design's 28 px is a width; the height is the one any button of
        // the row asks for, at this text size and every other.
        QCOMPARE(toolbar->sizeHint().height(), text->sizeHint().height());
        QCOMPARE(toolbar->height(), text->height());
    }

    // The popup prompt carries no box of its own — the popup's accent frame
    // is the focus cue — and its magnifier is a widget, not a prefix of the
    // placeholder, so it stays while something is typed.
    void promptFieldIsBorderlessAndKeepsItsMagnifier()
    {
        QLineEdit *field = ui::promptField(QStringLiteral("Search branches…"));
        std::unique_ptr<QWidget> box(ui::promptBox(field));
        QCOMPARE(field->objectName(), QString("promptField"));
        QCOMPARE(field->placeholderText(), QString("Search branches…"));
        QVERIFY(!field->hasFrame());
        QCOMPARE(field->height(), ui::space(28));
        QCOMPARE(box->findChild<QLineEdit *>(QStringLiteral("promptField")), field);
        // The magnifier, if the font has one...
        if (!ui::icon(ui::kMagnify).isEmpty())
            QVERIFY(box->findChild<QLabel *>(QStringLiteral("promptIcon")));
        // ...and the hairline under the row, which is part of the box so the
        // filtering below it can never take it away.
        QWidget *hair = nullptr;
        for (QWidget *child : box->findChildren<QWidget *>())
            if (child != field && !qobject_cast<QLabel *>(child))
                hair = child;
        QVERIFY(hair);
        QCOMPARE(hair->height(), 1);
    }

    // Typing in the prompt narrows the entries and leaves the headers of the
    // sections that still have one.
    void branchMenuFiltersThroughThePromptField()
    {
        BranchMenu menu;
        BranchList branches;
        branches.local = {QStringLiteral("main"), QStringLiteral("feature/tiling")};
        branches.remote = {QStringLiteral("origin/main")};
        menu.setBranches(branches, QStringLiteral("main"), true, BranchMenu::TipFunction());

        auto *field = menu.findChild<QLineEdit *>(QStringLiteral("promptField"));
        QVERIFY(field);
        const auto shown = [&menu](const QString &name) {
            for (QAction *a : menu.actions())
                if (a->text() == name)
                    return a->isVisible();
            return false;
        };
        field->setText(QStringLiteral("tiling"));
        QVERIFY(shown(QStringLiteral("feature/tiling")));
        QVERIFY(!shown(QStringLiteral("main")));
        QVERIFY(!shown(QStringLiteral("origin/main")));
        QVERIFY(!shown(QStringLiteral("No matching branch")));
        // The line between the sections goes with them: alone under the
        // prompt's own hairline it would draw that line twice.
        const auto separatorShown = [&menu] {
            for (QAction *a : menu.actions())
                if (a->isSeparator())
                    return a->isVisible();
            return false;
        };
        QVERIFY(!separatorShown());
        field->setText(QStringLiteral("origin"));
        QVERIFY(shown(QStringLiteral("origin/main")));
        QVERIFY(!separatorShown());
        field->setText(QStringLiteral("main"));
        QVERIFY(separatorShown());
        field->setText(QStringLiteral("nothing here"));
        QVERIFY(shown(QStringLiteral("No matching branch")));
        QVERIFY(!separatorShown());
        field->clear();
        QVERIFY(shown(QStringLiteral("main")) && shown(QStringLiteral("feature/tiling")));
        QVERIFY(separatorShown());
    }

    // --- KeybindingsPanel ---------------------------------------------------

    void keybindingsFilterMatchesTheSpelledOutKeys()
    {
        QWidget host;
        host.resize(800, 600);
        auto *panel = new KeybindingsPanel(&host);
        panel->setAttribute(Qt::WA_DeleteOnClose, false);
        panel->add(QStringLiteral("CTRL + F"), QStringLiteral("Fetch"));
        panel->add(QStringLiteral("CTRL SHIFT + P"), QStringLiteral("Push"), QStringLiteral("everywhere"));
        panel->add(QStringLiteral("F5 / CTRL SHIFT + R"), QStringLiteral("Refresh"));

        auto *search = panel->findChild<QLineEdit *>(QStringLiteral("keybindingsSearch"));
        auto *list = panel->findChild<QListView *>(QStringLiteral("keybindingsList"));
        QVERIFY(search);
        QVERIFY(list);
        QAbstractItemModel *model = list->model();
        QCOMPARE(model->rowCount(), 3);

        // "ctrl+f" finds the row the menu spells "CTRL + F".
        search->setText(QStringLiteral("ctrl+f"));
        QCOMPARE(model->rowCount(), 1);
        QCOMPARE(model->index(0, 0).data(Qt::DisplayRole).toString(), QStringLiteral("Fetch"));
        QCOMPARE(model->index(0, 0).data(Qt::UserRole).toString(), QStringLiteral("CTRL + F"));

        // "ctrl f" finds it too; the terms match one by one, so the rows
        // whose SHIFT carries an f are along for the ride.
        search->setText(QStringLiteral("ctrl f"));
        QVERIFY(model->rowCount() >= 1);
        bool foundFetch = false;
        for (int row = 0; row < model->rowCount(); ++row)
            foundFetch |= model->index(row, 0).data(Qt::UserRole).toString() == QStringLiteral("CTRL + F");
        QVERIFY(foundFetch);
        search->setText(QStringLiteral("ctrl shift"));
        QCOMPARE(model->rowCount(), 2);
        search->setText(QStringLiteral("push"));
        QCOMPARE(model->rowCount(), 1);
        search->setText(QStringLiteral("everywhere")); // the context counts too
        QCOMPARE(model->rowCount(), 1);
        search->setText(QStringLiteral("nothing here"));
        QCOMPARE(model->rowCount(), 0);
        search->clear();
        QCOMPARE(model->rowCount(), 3);
        panel->close();
    }

    // --- MessageEdit / CommitPage: the message box grows to fit -------------

    void messageHeightCountsWrappedLinesAndIsMeasuredOncePerBurst()
    {
        MessageEdit edit;
        edit.resize(300, 100);
        edit.show();
        QVERIFY(QTest::qWaitForWindowExposed(&edit));
        const int spacing = edit.fontMetrics().lineSpacing();

        const int oneLine = edit.contentHeight();
        QVERIFY(oneLine > spacing);
        QVERIFY(oneLine < edit.height()); // an empty box is taller than its text
        edit.setPlainText(QStringLiteral("one\ntwo\nthree"));
        QCOMPARE(edit.contentHeight(), oneLine + 2 * spacing);

        // One paragraph the box has to wrap counts as the lines it takes on
        // screen, not as the single block it is.
        QSignalSpy spy(&edit, &MessageEdit::contentHeightChanged);
        edit.setPlainText(QString(QStringLiteral("word ")).repeated(120));
        QVERIFY(edit.contentHeight() > edit.height());
        QVERIFY(edit.contentHeight() > oneLine + 8 * spacing);

        // A narrower box wraps the same text into more lines, and says so:
        // the window sizes its panes after the text has arrived (--amend).
        settle();
        const int wide = edit.contentHeight();
        spy.clear();
        edit.resize(150, 100);
        settle();
        QVERIFY(edit.contentHeight() > wide);
        QCOMPARE(spy.count(), 1);
        const auto edited = [&spy] { return spy.at(0).at(0).value<MessageEdit::Edit>(); };
        QCOMPARE(edited(), MessageEdit::Edit::Typed); // re-wrapped, not pasted

        // The agent streams a message in many partials: one measurement, and
        // a pasted one. (From a short text, so no scrollbar comes or goes:
        // that resizes the viewport, which is a measurement of its own.)
        edit.setPlainText(QStringLiteral("short"));
        settle();
        spy.clear();
        for (int i = 0; i < 5; ++i)
            edit.replaceText(QStringLiteral("partial %1").arg(i), i > 0);
        settle();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(edited(), MessageEdit::Edit::Pasted);

        // Typed text is not pasted; text from the clipboard is; taking text
        // out is a deletion.
        spy.clear();
        QTest::keyClicks(&edit, QStringLiteral("typed"));
        settle();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(edited(), MessageEdit::Edit::Typed);
        spy.clear();
        QApplication::clipboard()->setText(QStringLiteral("clip"));
        edit.paste();
        settle();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(edited(), MessageEdit::Edit::Pasted);
        spy.clear();
        QTest::keyClick(&edit, Qt::Key_Backspace);
        settle();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(edited(), MessageEdit::Edit::Deleted);
    }

    void commitMessagePaneFollowsItsText()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(git(dir.path(), {"init", "-q", "-b", "main"}));
        QVERIFY(commit(dir.path(), QStringLiteral("A"), 1));

        GitRepo repo(dir.path());
        // The page asks the agent CLIs on PATH for their models as it is
        // built; an empty PATH keeps those processes out of this test.
        const QByteArray path = qgetenv("PATH");
        qputenv("PATH", dir.path().toUtf8());
        CommitPage page(&repo);
        qputenv("PATH", path);
        page.resize(700, 800);
        page.show();
        QVERIFY(QTest::qWaitForWindowExposed(&page));
        settle();

        auto *splitter = page.findChild<QSplitter *>(QStringLiteral("commitMessageSplitter"));
        auto *message = page.findChild<MessageEdit *>();
        QVERIFY(splitter);
        QVERIFY(message);
        // The section grid: the handle between the message and the changes is
        // the 16 px the design puts between two sections (the stylesheet's
        // 8 px handle must not win), and the header rows carry 24 px squares.
        QCOMPARE(splitter->handleWidth(), ui::sectionGap());
        QCOMPARE(splitter->handle(1)->height(), ui::sectionGap());
        // ...and the action bar hangs 8 px under the list, a gap of its own.
        QCOMPARE(page.layout()->spacing(), ui::space(8));
        const QList<QToolButton *> squares = page.findChildren<QToolButton *>(QStringLiteral("iconButton"));
        QCOMPARE(squares.size(), 3); // the agent cog, the unversioned eye and Refresh
        for (const QToolButton *square : squares)
            QCOMPARE(square->size(), QSize(ui::space(24), ui::space(24)));

        const auto pane = [splitter] { return splitter->sizes().at(0); };
        const int initial = pane();
        const int total = splitter->sizes().at(0) + splitter->sizes().at(1);
        QVERIFY(initial > 0);

        // A message that fits is left alone.
        message->setPlainText(QStringLiteral("a one line subject"));
        settle();
        QCOMPARE(pane(), initial);

        // One that does not gets exactly the height it needs...
        message->setPlainText(QStringLiteral("a line of the message\n").repeated(12));
        settle();
        const int grown = pane();
        QVERIFY2(grown > initial, qPrintable(QStringLiteral("pane stayed at %1").arg(grown)));
        QCOMPARE(grown, message->contentHeight());
        QVERIFY(grown <= splitter->height() / 2);
        // The room came out of the changes list, not out of thin air.
        QCOMPARE(splitter->sizes().at(0) + splitter->sizes().at(1), total);
        QCOMPARE(splitter->sizes().at(1), total - grown);

        // ...and gives it back when the message is cut short (to the height
        // the box had at the start, not to the one line the text needs).
        message->setPlainText(QStringLiteral("short again"));
        settle();
        QCOMPARE(pane(), initial);

        // Half of the splitter is the ceiling, however long the message.
        message->setPlainText(QStringLiteral("a line of the message\n").repeated(200));
        settle();
        QCOMPARE(pane(), splitter->height() / 2);
        QVERIFY(message->contentHeight() > pane()); // it really was capped

        // Growing is not the user's choice, so it is not remembered.
        QVERIFY(!QSettings().contains(settings::kWindowCommitMessageSplitter));

        // Once the user has dragged the handle, typing leaves the size alone
        // (a box can be made smaller than its text)...
        const auto dragTo = [splitter, total](int height) {
            splitter->setSizes({height, total - height});
            emit splitter->splitterMoved(height, 1);
        };
        message->setPlainText(QStringLiteral("short"));
        settle(); // the cut lands before the drag, as it would for a person
        dragTo(initial);
        settle();
        QCOMPARE(pane(), initial);
        message->setFocus();
        for (int i = 0; i < 30; ++i)
            QTest::keyClick(message, Qt::Key_Return);
        settle();
        QCOMPARE(pane(), initial);
        QVERIFY(message->contentHeight() > initial);

        // ...but a message from the agent grows it again...
        message->replaceText(QStringLiteral("a line from the agent\n").repeated(12));
        settle();
        QCOMPARE(pane(), qMin(message->contentHeight(), splitter->height() / 2));
        QVERIFY(pane() > initial);

        // ...and so does a paste.
        dragTo(initial);
        settle();
        QCOMPARE(pane(), initial);
        QApplication::clipboard()->setText(QStringLiteral("a pasted line\n").repeated(12));
        message->paste();
        settle();
        QVERIFY(pane() > initial);
        QCOMPARE(pane(), qMin(message->contentHeight(), splitter->height() / 2));

        // Deleting lines shrinks the box back to the text, and from then on
        // typing grows it again (the dragged size is forgotten)...
        const int twelve = pane();
        QTest::keyClick(message, Qt::Key_End, Qt::ControlModifier);
        for (int i = 0; i < 4; ++i)
            QTest::keyClick(message, Qt::Key_Up, Qt::ShiftModifier);
        QTest::keyClick(message, Qt::Key_Backspace);
        settle();
        QVERIFY(pane() < twelve);
        QCOMPARE(pane(), message->contentHeight());
        const int eight = pane();
        for (int i = 0; i < 4; ++i)
            QTest::keyClick(message, Qt::Key_Return);
        settle();
        QVERIFY(pane() > eight);

        // ...and clearing it out goes back to the resting height, not lower.
        QTest::keyClick(message, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(message, Qt::Key_Backspace);
        settle();
        QCOMPARE(pane(), initial);
        QVERIFY(message->contentHeight() < initial);
    }

    // --- CommitPage: the CHANGES section and the action bar -----------------

    // The count is part of the section's title, and the Commit button says
    // how many files it would take, with the key that presses it.
    void theTitleAndTheCommitButtonCountTheCheckedFiles()
    {
        CommitFixture f = commitFixture();
        QVERIFY(f.page);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        // The button's text carries the glyph and the key that presses it;
        // its accessible name is the wording alone, and follows it.
        const auto says = [&f](const QString &label) {
            return f.commitButton()->text().endsWith(label + QStringLiteral("  ⏎"))
                && f.commitButton()->accessibleName() == label;
        };

        QCOMPARE(f.title(), QStringLiteral("CHANGES · 2/4"));
        QVERIFY(says(QStringLiteral("Commit 2 files")));
        QVERIFY(f.commitButton()->isEnabled());

        f.model()->setAllChecked(true);
        QCOMPARE(f.title(), QStringLiteral("CHANGES · 4/4"));
        QVERIFY(says(QStringLiteral("Commit 4 files")));

        // Nothing checked: no count, and nothing to press either.
        f.model()->setAllChecked(false);
        QCOMPARE(f.title(), QStringLiteral("CHANGES · 0/4"));
        QVERIFY(says(QStringLiteral("Commit")));
        QVERIFY(!f.commitButton()->isEnabled());

        // One file is a file, not "1 files".
        f.model()->setPathsChecked({QStringLiteral("a.txt")}, true);
        QCOMPARE(f.title(), QStringLiteral("CHANGES · 1/4"));
        QVERIFY(says(QStringLiteral("Commit 1 file")));

        // Amending and a merge in progress keep their own wording, count or
        // no count. (Amend first: a merge rules the checkbox out.)
        f.page->setAmendChecked(true);
        QVERIFY(says(QStringLiteral("Amend")));
        f.page->setAmendChecked(false);
        MergeState merge;
        merge.inProgress = true;
        f.page->setMergeState(merge, Commit());
        QVERIFY(says(QStringLiteral("Commit merge")));
        merge.inProgress = false;
        f.page->setMergeState(merge, Commit());
        QVERIFY(says(QStringLiteral("Commit 1 file")));

        // An empty list is the section's name on its own.
        f.model()->setChanges({});
        QCOMPARE(f.title(), QStringLiteral("CHANGES"));
        QVERIFY(says(QStringLiteral("Commit")));
    }

    // Check-all is the box in the table's own header: it follows the files,
    // a click on it ticks or unticks them, and it sorts nothing.
    void theCheckAllBoxSitsInTheTableHeader()
    {
        CommitFixture f = commitFixture();
        QVERIFY(f.page);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        QVERIFY(f.header());
        QCOMPARE(f.checkAll(), int(Qt::PartiallyChecked));
        f.model()->setAllChecked(true);
        QCOMPARE(f.checkAll(), int(Qt::Checked));
        f.model()->setAllChecked(false);
        QCOMPARE(f.checkAll(), int(Qt::Unchecked));

        // Partial or none, a click checks them all; checked, it clears them.
        f.model()->setPathsChecked({QStringLiteral("a.txt")}, true);
        QCOMPARE(f.checkAll(), int(Qt::PartiallyChecked));
        const int sorted = f.header()->sortIndicatorSection();
        f.clickSection(ChangesModel::Check);
        QCOMPARE(f.model()->checkedCount(), f.model()->count());
        QCOMPARE(f.checkAll(), int(Qt::Checked));
        f.clickSection(ChangesModel::Check);
        QCOMPARE(f.model()->checkedCount(), 0);
        QCOMPARE(f.title(), QStringLiteral("CHANGES · 0/4"));
        // A column of checkboxes is nothing to sort by; the others still are.
        QCOMPARE(f.header()->sortIndicatorSection(), sorted);

        // Qt answers the second of two fast clicks with a double click, which
        // a checkbox has to take for a click of its own: the pair ticks and
        // unticks instead of the second one reaching the header.
        f.clickSection(ChangesModel::Check);
        QCOMPARE(f.model()->checkedCount(), f.model()->count());
        f.doubleClickSection(ChangesModel::Check);
        QCOMPARE(f.model()->checkedCount(), 0);
        QCOMPARE(f.header()->sortIndicatorSection(), sorted);

        f.clickSection(ChangesModel::Name);
        QCOMPARE(f.header()->sortIndicatorSection(), int(ChangesModel::Name));

        // Space on the current row checks that one file, as before.
        f.page->selectFirstRow();
        f.page->table()->setFocus();
        QTest::keyClick(f.page->table(), Qt::Key_Space);
        QCOMPARE(f.model()->checkedCount(), 1);
        QTest::keyClick(f.page->table(), Qt::Key_Space);
        QCOMPARE(f.model()->checkedCount(), 0);

        // ...whatever cell of the row is the current one: a click on a file's
        // name or its path leaves the current index in a column that carries
        // no checkbox of its own.
        for (const int column : {int(ChangesModel::Name), int(ChangesModel::Path)}) {
            f.clickCell(0, column);
            QCOMPARE(f.page->table()->currentIndex().column(), column);
            QTest::keyClick(f.page->table(), Qt::Key_Space);
            QCOMPARE(f.model()->checkedCount(), 1);
            QTest::keyClick(f.page->table(), Qt::Key_Space);
            QCOMPARE(f.model()->checkedCount(), 0);
        }

        // ...and the keybinding's check all / none is the same two states.
        f.page->toggleAllChecked();
        QCOMPARE(f.model()->checkedCount(), f.model()->count());
        f.page->toggleAllChecked();
        QCOMPARE(f.model()->checkedCount(), 0);

        // The history's files have no checkboxes, so that column is their
        // number, as it has always been.
        ChangesModel files;
        files.setCheckable(false);
        QVERIFY(!files.headerData(ChangesModel::Check, Qt::Horizontal, Qt::CheckStateRole).isValid());
        QCOMPARE(files.headerData(ChangesModel::Check, Qt::Horizontal, Qt::DisplayRole).toString(),
                 QStringLiteral("#"));
    }

    // The eye takes the unversioned files out of the list, and the title
    // counts what is left.
    void theEyeHidesTheUnversionedFiles()
    {
        CommitFixture f = commitFixture();
        QVERIFY(f.page);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        QToolButton *eye = f.eye();
        QVERIFY(eye);
        QVERIFY(eye->isChecked()); // they are shown to begin with
        QCOMPARE(eye->accessibleName(), QStringLiteral("Show unversioned files"));
        QCOMPARE(eye->size(), QSize(ui::space(24), ui::space(24)));
        QCOMPARE(f.page->proxy()->rowCount(), 4);

        eye->click();
        QVERIFY(!eye->isChecked());
        QCOMPARE(f.page->proxy()->rowCount(), 2);
        QCOMPARE(f.title(), QStringLiteral("CHANGES · 2/2"));
        eye->click();
        QCOMPARE(f.page->proxy()->rowCount(), 4);
        QCOMPARE(f.title(), QStringLiteral("CHANGES · 2/4"));
    }

    // Nothing the eye hides is ever committed: with the unversioned files out
    // of the list, none of them is checked, and the title, the button and the
    // check-all box all count the files on show.
    void hiddenFilesAreNeverChecked()
    {
        CommitFixture f = commitFixture();
        QVERIFY(f.page);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        QToolButton *eye = f.eye();
        QVERIFY(eye);
        const auto says = [&f](const QString &label) {
            return f.commitButton()->text().endsWith(label + QStringLiteral("  ⏎"));
        };
        // The header reads the box off the table's model, so both kinds of
        // change have to reach it: the source's own (forwarded by the proxy)
        // and rows coming and going (the proxy's own).
        QSignalSpy headerChanged(f.page->proxy(), &QAbstractItemModel::headerDataChanged);

        f.model()->setAllChecked(true);
        QCOMPARE(f.checkAll(), int(Qt::Checked));
        QVERIFY(headerChanged.count() > 0);
        headerChanged.clear();

        // Off: the two unversioned files leave the list and their marks with it.
        eye->click();
        QVERIFY(headerChanged.count() > 0);
        QCOMPARE(f.page->proxy()->rowCount(), 2);
        QCOMPARE(f.title(), QStringLiteral("CHANGES · 2/2"));
        QVERIFY(says(QStringLiteral("Commit 2 files")));
        QCOMPARE(f.checkAll(), int(Qt::Checked));
        QCOMPARE(f.checkedPaths(), QStringList({QStringLiteral("a.txt"), QStringLiteral("b.txt")}));

        // Check-all, by click and by keybinding, is over the shown rows only.
        f.clickSection(ChangesModel::Check);
        QCOMPARE(f.model()->checkedCount(), 0);
        QCOMPARE(f.title(), QStringLiteral("CHANGES · 0/2"));
        f.clickSection(ChangesModel::Check);
        QCOMPARE(f.checkedPaths(), QStringList({QStringLiteral("a.txt"), QStringLiteral("b.txt")}));
        QCOMPARE(f.checkAll(), int(Qt::Checked));
        f.page->toggleAllChecked();
        QCOMPARE(f.model()->checkedCount(), 0);
        f.page->toggleAllChecked();
        QCOMPARE(f.checkedPaths(), QStringList({QStringLiteral("a.txt"), QStringLiteral("b.txt")}));

        // Check marks survive a reload by path, so a mark that reached a
        // hidden file (the way amending ticks the files of the commit) is
        // gone again after the next one.
        f.model()->setPathsChecked({QStringLiteral("u1.txt")}, true);
        QCOMPARE(f.model()->checkedCount(), 3);
        f.page->reload();
        QCOMPARE(f.checkedPaths(), QStringList({QStringLiteral("a.txt"), QStringLiteral("b.txt")}));

        // A file that turns up unversioned arrives unchecked and out of sight.
        QVERIFY(writeFixture(QDir(f.dir->path()).filePath(QStringLiteral("u3.txt")), "u3\n"));
        f.page->reload();
        QCOMPARE(f.model()->count(), 5);
        QCOMPARE(f.page->proxy()->rowCount(), 2);
        QCOMPARE(f.title(), QStringLiteral("CHANGES · 2/2"));

        // On again: all five are listed, the three unversioned ones unticked,
        // and the box is partial.
        eye->click();
        QCOMPARE(f.page->proxy()->rowCount(), 5);
        QCOMPARE(f.title(), QStringLiteral("CHANGES · 2/5"));
        QCOMPARE(f.checkAll(), int(Qt::PartiallyChecked));
        QCOMPARE(f.checkedPaths(), QStringList({QStringLiteral("a.txt"), QStringLiteral("b.txt")}));
    }

    // Amending ticks the files of HEAD by path, after the reload. HEAD may
    // have deleted a file that is back as an unversioned one: behind the eye
    // it must not come out ticked, or the amend would take a file nobody saw.
    void amendingLeavesHiddenFilesUnticked()
    {
        CommitFixture f = commitFixture();
        QVERIFY(f.page);
        const QString path = f.dir->path();
        QVERIFY(git(path, {"rm", "-q", "--cached", "b.txt"}));
        QVERIFY(git(path, {"commit", "-q", "-m", "drop b"}, 1));
        // b.txt is still on disk: deleted by HEAD, unversioned now.
        f.page->reload();
        QVERIFY(f.repo->headPaths().contains(QStringLiteral("b.txt")));

        f.eye()->click(); // off
        f.model()->setAllChecked(false);
        // The order the window amends in: reload, then the paths of HEAD.
        f.page->reload();
        f.page->checkHeadPaths();
        QVERIFY(!f.checkedPaths().contains(QStringLiteral("b.txt")));
        QCOMPARE(f.model()->checkedCount(), 0);
        QCOMPARE(f.title(), QStringLiteral("CHANGES · 0/1"));

        // With the eye on the same two steps do tick it: it is there to see.
        f.eye()->click();
        f.page->reload();
        f.page->checkHeadPaths();
        QVERIFY(f.checkedPaths().contains(QStringLiteral("b.txt")));
    }

    // The check-all box lights up under the pointer, like the boxes of the
    // rows under it.
    void theCheckAllBoxLightsUpUnderThePointer()
    {
        CommitFixture f = commitFixture();
        QVERIFY(f.page);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        QHeaderView *h = f.page->table()->horizontalHeader();
        QVERIFY(h->viewport()->hasMouseTracking()); // moves arrive with no button held down
        f.model()->setAllChecked(false); // a ticked box is the accent either way
        const auto moveTo = [h](const QPoint &pos) {
            QMouseEvent move(QEvent::MouseMove, pos, h->viewport()->mapToGlobal(pos), Qt::NoButton, {}, {});
            QApplication::sendEvent(h->viewport(), &move);
        };
        const QImage plain = h->grab().toImage();

        moveTo(f.sectionCentre(ChangesModel::Check));
        const QImage hovered = h->grab().toImage();
        QVERIFY(hovered != plain);
        // The section around the box is not the box: only the indicator does.
        moveTo(QPoint(h->sectionViewportPosition(ChangesModel::Check), h->viewport()->height() - 1));
        QCOMPARE(h->grab().toImage(), plain);
        moveTo(f.sectionCentre(ChangesModel::Check));
        QCOMPARE(h->grab().toImage(), hovered);
        // The pointer leaving the header takes the hover with it.
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(h, &leave);
        QCOMPARE(h->grab().toImage(), plain);
    }

    // The action bar holds the amend checkbox and the Commit button on one
    // line; too narrow for both, and the checkbox goes by its short name.
    void theAmendLabelShortensOnANarrowPage()
    {
        CommitFixture f = commitFixture();
        QVERIFY(f.page);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        QCOMPARE(f.amend()->text(), QStringLiteral("Amend last commit"));
        // The empty middle of the row is nobody's: the checkbox is as wide as
        // its label, not as wide as the space the button leaves.
        QCOMPARE(f.amend()->width(), f.amend()->sizeHint().width());
        f.page->resize(260, 600);
        settle();
        QCOMPARE(f.amend()->text(), QStringLiteral("Amend"));
        f.page->resize(760, 600);
        settle();
        QCOMPARE(f.amend()->text(), QStringLiteral("Amend last commit"));
    }

    // --- mergeVerdict() -----------------------------------------------------

    void verdictForEveryOutcome()
    {
        const QString main = QStringLiteral("main");

        MergeVerdict same = mergeVerdict(preview(MergePreview::Same), false, main);
        QCOMPARE(same.kind, MergeVerdict::Info);
        QCOMPARE(same.headline, QStringLiteral("Pick two different branches."));
        QCOMPARE(same.detail, QStringList({QStringLiteral("The same branch is on both sides.")}));
        QVERIFY(!same.canMerge);

        MergeVerdict upToDate = mergeVerdict(preview(MergePreview::UpToDate), false, main);
        QCOMPARE(upToDate.kind, MergeVerdict::Info);
        QCOMPARE(upToDate.headline, QStringLiteral("Nothing to merge."));
        QCOMPARE(upToDate.detail, QStringList({QStringLiteral("main already has every commit of feature.")}));
        QVERIFY(!upToDate.canMerge);

        MergeVerdict fastForward = mergeVerdict(preview(MergePreview::FastForward), false, main);
        QCOMPARE(fastForward.kind, MergeVerdict::Good);
        QVERIFY(fastForward.headline.startsWith(QStringLiteral("Fast-forward")));
        QVERIFY(fastForward.detail.first().contains(QStringLiteral("main simply moves up 2 commits")));
        QCOMPARE(fastForward.detail.size(), 2); // the move, then the file statistics
        QVERIFY(fastForward.detail.last().contains(QStringLiteral("3 files changed")));
        QVERIFY(fastForward.canMerge);
        QVERIFY(fastForward.buttonTip.contains(QStringLiteral("Fast-forward")));

        // The box below the card asks for a merge commit: no fast-forward then.
        MergeVerdict noFf = mergeVerdict(preview(MergePreview::FastForward), true, main);
        QCOMPARE(noFf.kind, MergeVerdict::Good);
        QVERIFY(noFf.headline.contains(QStringLiteral("no conflicts")));
        QVERIFY(noFf.detail.first().contains(QStringLiteral("could simply move up")));
        QVERIFY(noFf.canMerge);
        QVERIFY(noFf.buttonTip.contains(QStringLiteral("merge commit")));

        MergeVerdict clean = mergeVerdict(preview(MergePreview::Clean), false, main);
        QCOMPARE(clean.kind, MergeVerdict::Good);
        QVERIFY(clean.headline.contains(QStringLiteral("no conflicts")));
        QCOMPARE(clean.detail.size(), 2);
        QVERIFY(clean.detail.first().contains(QStringLiteral("2 commits from feature")));
        QVERIFY(clean.canMerge);
        QVERIFY(clean.files.isEmpty());

        MergePreview conflicting = preview(MergePreview::Conflicts);
        conflicting.conflicts = QStringList({QStringLiteral("a.txt"), QStringLiteral("b.txt")});
        MergeVerdict conflicts = mergeVerdict(conflicting, false, main);
        QCOMPARE(conflicts.kind, MergeVerdict::Bad);
        QCOMPARE(conflicts.headline, QStringLiteral("2 files would conflict."));
        QCOMPARE(conflicts.files, conflicting.conflicts);
        QVERIFY(conflicts.canMerge); // the merge may still be started and resolved
        QVERIFY(conflicts.buttonTip.contains(QStringLiteral("Start the merge")));
        conflicting.conflicts = QStringList({QStringLiteral("a.txt")});
        QCOMPARE(mergeVerdict(conflicting, false, main).headline, QStringLiteral("1 file would conflict."));

        MergePreview broken = preview(MergePreview::Failed);
        broken.error = QStringLiteral("unknown revision");
        MergeVerdict failed = mergeVerdict(broken, false, main);
        QCOMPARE(failed.kind, MergeVerdict::Bad);
        QCOMPARE(failed.headline, QStringLiteral("Could not check the merge."));
        QCOMPARE(failed.detail, QStringList({broken.error}));
        QVERIFY(!failed.canMerge);
    }

    void verdictWarnsAboutBlockedPathsAndACheckout()
    {
        MergePreview blocked = preview(MergePreview::Clean);
        blocked.blocked = QStringList({QStringLiteral("a.txt")});
        MergeVerdict one = mergeVerdict(blocked, false, QStringLiteral("main"));
        QVERIFY(one.warning.contains(QStringLiteral("Local changes to a.txt")));
        QVERIFY(!one.canMerge);
        QCOMPARE(one.buttonTip, QStringLiteral("Blocked by local changes — see above"));

        blocked.blocked = QStringList({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"),
                                       QStringLiteral("d"), QStringLiteral("e"), QStringLiteral("f")});
        const MergeVerdict many = mergeVerdict(blocked, false, QStringLiteral("main"));
        QVERIFY(many.warning.contains(QStringLiteral("6 files")));
        QVERIFY(many.warning.contains(QStringLiteral("a, b, c, d and 2 more")));

        // Merging into a branch that is not checked out says so, right under
        // the headline, and only when there is something to merge.
        const MergeVerdict elsewhere = mergeVerdict(preview(MergePreview::Clean), false, QStringLiteral("other"));
        QCOMPARE(elsewhere.detail.size(), 3);
        QCOMPARE(elsewhere.detail.at(1), QStringLiteral("main is checked out first"));
        const MergeVerdict same = mergeVerdict(preview(MergePreview::Same), false, QStringLiteral("other"));
        QCOMPARE(same.detail.size(), 1);
    }

    // --- The sign-in dialog and its askpass helper --------------------------

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

    // Whether any visible label of the dialog says `text`.
    static bool says(LoginDialog *dialog, const QString &text)
    {
        for (const QLabel *label : dialog->findChildren<QLabel *>())
            if (label->isVisible() && label->text().contains(text))
                return true;
        return false;
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
    }

    // The whole way round with git itself: `git credential fill` asks the
    // built binary, which is the askpass helper, which asks the server here.
    // No network is involved — git only wants the credentials.
    // The application git runs as its askpass helper — the binary itself.
    // Empty when it is not built, which is all the tests below can do about it.
    static QString helperBinary()
    {
        // ../build/tests/ui_test → the repository root next to it.
        QString binary = qEnvironmentVariable("OMAGIT_BINARY");
        if (binary.isEmpty())
            binary = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../../omagit"));
        binary = QFileInfo(binary).absoluteFilePath();
        return QFileInfo::exists(binary) ? binary : QString();
    }

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

    // --- Signing in on the way to a remote ----------------------------------
    // The whole path, end to end and without a network: RemoteSync starts a
    // `git fetch`, git meets a 401 from a server this test runs on 127.0.0.1,
    // runs the built binary as its askpass helper, and the prompt arrives here
    // as a request. What the test answers decides how the fetch ends.

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

    // --- OmarchyTheme -------------------------------------------------------
    // Last: each of them points OmarchyTheme::instance() at a theme of its
    // own, and puts the desktop's back when it is done.

    // Every measurement of the kit is in 12 px-base pixels and grows with the
    // desktop's text size.
    void spacingFollowsTheBaseFontSize()
    {
        QCOMPARE(ui::space(12), OmarchyTheme::instance()->fontBase());
        QTemporaryDir dir, home;
        QVERIFY(dir.isValid() && home.isValid());
        QVERIFY(writeFixture(QDir(dir.path()).filePath(QStringLiteral("shell.toml")), "[font]\nbase-size = 18\n"));
        qputenv("OMAGIT_THEME_DIR", dir.path().toUtf8());
        {
            // The scratch home keeps the desktop's own shell.toml, which would
            // be read after the theme's, out of the way.
            ScopedEnv scratchHome("HOME", home.path().toUtf8());
            OmarchyTheme theme;
            QCOMPARE(theme.fontBase(), 18);
            QCOMPARE(ui::space(12), 18);
            QCOMPARE(ui::space(24), 36);
            QCOMPARE(ui::headerRowHeight(), 36);
            QCOMPARE(ui::headerGap(), 9);
            QCOMPARE(ui::sectionGap(), 24);
            QCOMPARE(ui::space(1), 2);
            QCOMPARE(ui::space(0), 1); // never nothing at all
        }
        qunsetenv("OMAGIT_THEME_DIR");
        g_theme.reset(new OmarchyTheme);
        g_theme->apply(*qApp);
    }

    // `omarchy display text size` rewrites shell.toml under a running window:
    // every control already on screen has to end up where one built fresh at
    // the new size starts, and find its way back down again.
    void theKitFollowsALiveTextSizeChange()
    {
        QTemporaryDir dir, home, noPath;
        QVERIFY(dir.isValid() && home.isValid() && noPath.isValid());
        const QString toml = QDir(dir.path()).filePath(QStringLiteral("shell.toml"));
        QVERIFY(writeFixture(toml, "[font]\nbase-size = 12\n"));
        {
            ScopedEnv themeDir("OMAGIT_THEME_DIR", dir.path().toUtf8());
            // The scratch home keeps the desktop's own shell.toml, which would
            // be read after the theme's, out of the way; a PATH with nothing on
            // it hides omarchy-font-current, so a reload's font query answers
            // on the spot instead of from a process.
            ScopedEnv scratchHome("HOME", home.path().toUtf8());
            ScopedEnv emptyPath("PATH", noPath.path().toUtf8());
            OmarchyTheme theme;
            QCOMPARE(theme.fontBase(), 12);
            theme.apply(*qApp);
            QSignalSpy changed(&theme, &OmarchyTheme::changed);

            Kit live = buildKit(); // built once, at 12, and never built again
            QVERIFY(QTest::qWaitForWindowExposed(live.host.get()));
            settle();
            QCOMPARE(live.metrics(), freshKitMetrics());
            const QList<int> atTwelve = live.metrics();

            // The desktop's text size goes up under the live controls: the
            // watcher notices the rewritten file and the theme reloads.
            QVERIFY(writeFixture(toml, "[font]\nbase-size = 18\n"));
            QTRY_COMPARE_WITH_TIMEOUT(theme.fontBase(), 18, 10000);
            settle();
            QCOMPARE(changed.count(), 1);
            QCOMPARE(live.metrics(), freshKitMetrics());
            QVERIFY(live.metrics() != atTwelve); // everything measured did move
            QCOMPARE(live.inlineButton->size(), QSize(ui::space(24), ui::space(24)));
            QCOMPARE(live.toolbarButton->width(), ui::space(28));
            QCOMPARE(live.promptField->height(), ui::space(28));
            QCOMPARE(live.header->height(), ui::headerRowHeight());
            // The toolbar one is as tall as a text button, whatever that is
            // with this font, not the 28 px the design names for its width.
            QCOMPARE(live.toolbarButton->sizeHint().height(), live.textButton->sizeHint().height());
            QCOMPARE(live.toolbarButton->height(), live.textButton->height());

            // ...and back down to where it started.
            QVERIFY(writeFixture(toml, "[font]\nbase-size = 12\n"));
            QTRY_COMPARE_WITH_TIMEOUT(theme.fontBase(), 12, 10000);
            settle();
            QCOMPARE(live.metrics(), freshKitMetrics());
            QCOMPARE(live.metrics(), atTwelve);
            QCOMPARE(live.toolbarButton->height(), live.textButton->height());

            live.host.reset(); // the controls go before the theme they follow
        }
        g_theme.reset(new OmarchyTheme);
        g_theme->apply(*qApp);
        QVERIFY(OmarchyTheme::instance() == g_theme.get());
    }

    // The same live change, for the commit page: one page built at 12 and left
    // standing, held against a page built from scratch at every size the
    // desktop moves to.
    void theCommitPageFollowsALiveTextSizeChange()
    {
        QTemporaryDir dir, home, tools;
        QVERIFY(dir.isValid() && home.isValid() && tools.isValid());
        const QString toml = QDir(dir.path()).filePath(QStringLiteral("shell.toml"));
        QVERIFY(writeFixture(toml, "[font]\nbase-size = 12\n"));
        // A PATH with nothing on it but git: omarchy-font-current stays out of
        // reach, so a reload's font query answers on the spot, and no coding
        // agent is found either — while the page can still read a repository.
        const QString gitBinary = QStandardPaths::findExecutable(QStringLiteral("git"));
        QVERIFY(!gitBinary.isEmpty());
        QVERIFY(QFile::link(gitBinary, QDir(tools.path()).filePath(QStringLiteral("git"))));
        {
            ScopedEnv themeDir("OMAGIT_THEME_DIR", dir.path().toUtf8());
            ScopedEnv scratchHome("HOME", home.path().toUtf8());
            ScopedEnv onlyGit("PATH", tools.path().toUtf8());
            OmarchyTheme theme;
            QCOMPARE(theme.fontBase(), 12);
            theme.apply(*qApp);

            // MainWindow::applyTheme() is what drives a page in production: it
            // calls CommitPage::applyTheme() — once as the window is built and
            // again on every OmarchyTheme::changed — and gives every section
            // label the caption font of the moment.
            const auto applyThemeAsMainWindowDoes = [](CommitPage *p) {
                p->applyTheme();
                for (QLabel *l : p->findChildren<QLabel *>())
                    if (l->objectName() == QLatin1String("sectionLabel")
                        || l->objectName() == QLatin1String("dimLabel"))
                        l->setFont(OmarchyTheme::instance()->captionFont());
            };

            CommitFixture live = commitFixture();
            QVERIFY(live.page);
            CommitPage *page = live.page.get();
            applyThemeAsMainWindowDoes(page);
            QObject::connect(&theme, &OmarchyTheme::changed, page,
                             [page, applyThemeAsMainWindowDoes] { applyThemeAsMainWindowDoes(page); });
            QVERIFY(QTest::qWaitForWindowExposed(page));
            settle();

            // Both pages at two widths around the one the action bar folds at,
            // so the label and its box are measured where they change.
            const auto matchesAFreshPage = [&live, applyThemeAsMainWindowDoes] {
                std::unique_ptr<CommitPage> fresh = commitPage(live.repo.get());
                applyThemeAsMainWindowDoes(fresh.get());
                QVERIFY(QTest::qWaitForWindowExposed(fresh.get()));
                settle();
                const int fold = amendFoldWidth(fresh.get());
                QCOMPARE(amendFoldWidth(live.page.get()), fold);
                for (const int width : {fold - 1, fold + 1}) {
                    live.page->resize(width, 600);
                    fresh->resize(width, 600);
                    settle();
                    QCOMPARE(pageMetrics(live.page.get()), pageMetrics(fresh.get()));
                }
                live.page->resize(760, 600); // the width the page was measured at
                settle();
            };

            const QStringList atTwelve = pageMetrics(page);
            matchesAFreshPage();

            // The desktop's text size goes up under the live page.
            QVERIFY(writeFixture(toml, "[font]\nbase-size = 18\n"));
            QTRY_COMPARE_WITH_TIMEOUT(theme.fontBase(), 18, 10000);
            settle();
            matchesAFreshPage();
            QVERIFY(pageMetrics(page) != atTwelve); // the measurements did move

            // ...and back down to where it started.
            QVERIFY(writeFixture(toml, "[font]\nbase-size = 12\n"));
            QTRY_COMPARE_WITH_TIMEOUT(theme.fontBase(), 12, 10000);
            settle();
            matchesAFreshPage();
            QCOMPARE(pageMetrics(page), atTwelve);

            live.page.reset(); // the page goes before the theme it follows
        }
        g_theme.reset(new OmarchyTheme);
        g_theme->apply(*qApp);
        QVERIFY(OmarchyTheme::instance() == g_theme.get());
    }

    void themeReadsColorsTomlAndFallsBack()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile toml(QDir(dir.path()).filePath(QStringLiteral("colors.toml")));
        QVERIFY(toml.open(QIODevice::WriteOnly | QIODevice::Truncate));
        toml.write("# a scratch theme\n"
                   "mode = \"light\"\n"
                   "accent = \"#ff0000\"\n"
                   "background = \"#101010\"\n"
                   "foreground = \"#eeeeee\"\n"
                   "red = \"#c00000\"\n"
                   "green = #00ff00\n" // malformed: no quotes, so the line is ignored
                   "= \"#123456\"\n");  // malformed: no key
        toml.close();
        qputenv("OMAGIT_THEME_DIR", dir.path().toUtf8());

        {
            OmarchyTheme theme;
            QCOMPARE(theme.color(QStringLiteral("accent")), QColor(QStringLiteral("#ff0000")));
            QCOMPARE(theme.color(QStringLiteral("background")), QColor(QStringLiteral("#101010")));
            QCOMPARE(theme.color(QStringLiteral("red")), QColor(QStringLiteral("#c00000")));
            QVERIFY(!theme.isDark()); // mode = "light"
            // A key the file never names comes from Tokyo Night...
            QCOMPARE(theme.color(QStringLiteral("bright_yellow")), QColor(QStringLiteral("#ff9e64")));
            // ...and so does one whose line the parser could not read.
            QCOMPARE(theme.color(QStringLiteral("green")), QColor(QStringLiteral("#9ece6a")));
            // Shades the file leaves out are derived from what it does name.
            QVERIFY(theme.color(QStringLiteral("lighter_background")).isValid());
            QVERIFY(theme.mutedText() != theme.text());

            theme.apply(*qApp);
            const QString sheet = qApp->styleSheet();
            QVERIFY(!sheet.isEmpty());
            static const QRegularExpression token(QStringLiteral("%[A-Za-z0-9]+%"));
            const QRegularExpressionMatch leftover = token.match(sheet);
            QVERIFY2(!leftover.hasMatch(), qPrintable(QStringLiteral("stylesheet still holds ") + leftover.captured()));
            QVERIFY(sheet.contains(theme.accent().name()));
        }

        // Put the desktop's own theme back for whatever runs after this.
        qunsetenv("OMAGIT_THEME_DIR");
        g_theme.reset(new OmarchyTheme);
        g_theme->apply(*qApp);
        QVERIFY(OmarchyTheme::instance() == g_theme.get());
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setOrganizationName("omagit-tests");
    app.setApplicationName("ui-test");
    g_theme = std::make_unique<OmarchyTheme>();
    g_theme->apply(app);
    UiTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "ui_test.moc"
