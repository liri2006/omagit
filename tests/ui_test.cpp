// Build in tests: qmake6 ui.pro -o Makefile.ui && make -f Makefile.ui
// Run: QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME= QT_STYLE_OVERRIDE=Fusion ../build/tests/ui_test
// Exercises the pure logic behind the widgets: the history graph layout, the
// changes model's check marks, the toolbar's overflow, the keybindings filter,
// the theme's colors.toml parsing and the merge verdict's wording — and, with
// git itself but no network, the way a fetch signs in.
#include "../src/BadgeButton.h"
#include "../src/BranchMenu.h"
#include "../src/ChangesModel.h"
#include "../src/ChangesTreeModel.h"
#include "../src/CommitPage.h"
#include "../src/CommitPopover.h"
#include "../src/DiffPane.h"
#include "../src/DiffView.h"
#include "../src/CloneDialog.h"
#include "../src/HistoryModel.h"
#include "../src/HistoryView.h"
#include "../src/AgentPopover.h"
#include "../src/AskPass.h"
#include "../src/KeybindingsPanel.h"
#include "../src/LoginDialog.h"
#include "../src/MainWindow.h"
#include "../src/MergeDialog.h"
#include "../src/MessageEdit.h"
#include "../src/MiniRail.h"
#include "../src/OmarchyTheme.h"
#include "../src/RemoteSync.h"
#include "../src/Segmented.h"
#include "../src/Settings.h"
#include "../src/TickMenu.h"
#include "../src/TopBar.h"
#include "../src/UiHelpers.h"

#include <QAbstractItemModelTester>
#include <QApplication>
#include <QButtonGroup>
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
#include <QMenu>
#include <QMessageBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPushButton>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSettings>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QSplitterHandle>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QStyleOptionViewItem>
#include <QTableView>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>

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

// A row of the tree by its exact path, whichever kind it is: the model keeps
// files and directories apart (one name may be both), while a test that names
// a path usually knows which of the two it means.
QModelIndex treeRow(ChangesTreeModel *model, const QString &path, int column = ChangesTreeModel::Check)
{
    const QModelIndex file = model->indexForPath(path);
    return (file.isValid() ? file : model->indexForDirectory(path)).siblingAtColumn(column);
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
    ChangesHeader *treeHeader() const { return qobject_cast<ChangesHeader *>(page->tree()->header()); }
    ChangesTreeModel *treeModel() const { return qobject_cast<ChangesTreeModel *>(page->tree()->model()); }
    QTreeView *tree() const { return page->tree(); }
    QPushButton *commitButton() const { return page->findChild<QPushButton *>(); }
    QCheckBox *amend() const { return page->findChild<QCheckBox *>(); }
    // The eye by name: the files-view switcher next to it is checkable too.
    QToolButton *eye() const { return page->unversionedButton(); }
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

    // ---- the tree presentation
    QModelIndex treeIndex(const QString &path, int column = ChangesTreeModel::Check) const
    {
        return treeRow(treeModel(), path, column);
    }
    int treeCheck(const QString &path) const
    {
        return treeIndex(path).data(Qt::CheckStateRole).toInt();
    }
    // The tree as one list of "depth path" lines, in the order it shows them,
    // so a mismatch names itself.
    QStringList treeRows() const
    {
        QStringList rows;
        const std::function<void(const QModelIndex &)> walk = [&](const QModelIndex &parent) {
            ChangesTreeModel *m = treeModel();
            for (int row = 0, count = m->rowCount(parent); row < count; ++row) {
                const QModelIndex index = m->index(row, ChangesTreeModel::Check, parent);
                rows << QStringLiteral("%1 %2").arg(m->depth(index)).arg(m->path(index));
                walk(index);
            }
        };
        walk(QModelIndex());
        return rows;
    }
    void clickTreeCell(const QString &path, int column, const QPoint &offset = QPoint()) const
    {
        QTreeView *t = page->tree();
        const QRect rect = t->visualRect(treeIndex(path, column));
        QTest::mouseClick(t->viewport(), Qt::LeftButton, {},
                          offset.isNull() ? rect.center() : rect.topLeft() + offset);
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

// The same page over a repository with directories in it: what the tree has
// to show — nested paths, a single-child chain (src/deep), a one-file
// directory, two directory names that differ only in case, files in the root
// and one deletion — plus an untracked file inside a directory and another
// beside them, so the eye takes something out of both.
CommitFixture nestedFixture()
{
    CommitFixture f;
    f.dir.reset(new QTemporaryDir);
    const QString path = f.dir->path();
    const QDir root(path);
    if (!f.dir->isValid() || !git(path, {"init", "-q", "-b", "main"}))
        return f;
    for (const QString folder : {"Beta", "alpha", "src", "src/deep", "src/Deep", "tests", "single"})
        if (!root.mkpath(folder))
            return f;
    const QStringList tracked{"Beta/x.txt",  "alpha/y.txt",    "src/a.txt",    "src/deep/b.txt",
                              "src/Deep/c.txt", "tests/remove.txt", "single/one.txt", "root.txt"};
    for (const QString &file : tracked)
        writeFixture(root.filePath(file), "one\n");
    if (!git(path, {"add", "-A"}) || !git(path, {"commit", "-q", "-m", "first"}, 1))
        return f;
    for (const QString &file : tracked)
        if (file != QLatin1String("tests/remove.txt"))
            writeFixture(root.filePath(file), "two\n");
    QFile::remove(root.filePath(QStringLiteral("tests/remove.txt")));
    writeFixture(root.filePath(QStringLiteral("tests/new.txt")), "u\n");
    writeFixture(root.filePath(QStringLiteral("top.txt")), "u\n");
    f.repo.reset(new GitRepo(path));
    f.page = commitPage(f.repo.get());
    return f;
}

// What a commit page measures in scaled pixels, spelled out so a mismatch
// names itself: the design's checkbox column, the gaps of the section grid,
// the handle the message and the list share, the dividers of the CHANGES row
// and the gaps between its buttons, and the tree's own two narrow columns.
QStringList pageMetrics(CommitPage *page)
{
    auto *splitter = page->findChild<QSplitter *>(QStringLiteral("commitMessageSplitter"));
    QWidget *changes = splitter ? splitter->widget(1) : nullptr;
    // The children a single pixel wide are the two dividers of the CHANGES
    // row (switcher | eye | Refresh); their heights are the page's to set.
    QStringList dividers;
    for (const QWidget *w : page->findChildren<QWidget *>())
        if (w->minimumWidth() == 1 && w->maximumWidth() == 1)
            dividers << QString::number(w->height());
    // The gaps the switcher keeps: between the three buttons, and around the
    // dividers. Measured off the laid-out row, not off the spacers.
    QStringList gaps;
    const QList<QToolButton *> row{page->compactButton(), page->treeButton(), page->tableButton(),
                                   page->unversionedButton()};
    for (int i = 1; i < row.size(); ++i)
        gaps << QString::number(row.at(i)->mapTo(page, QPoint(0, 0)).x()
                                - (row.at(i - 1)->mapTo(page, QPoint(0, 0)).x() + row.at(i - 1)->width()));
    const QCheckBox *amend = page->findChild<QCheckBox *>();
    const auto entry = [](const char *name, int value) { return QStringLiteral("%1=%2").arg(QLatin1String(name)).arg(value); };
    return {entry("check", page->table()->columnWidth(ChangesModel::Check)),
            entry("row", page->table()->verticalHeader()->defaultSectionSize()),
            entry("actionBarGap", page->layout()->spacing()),
            entry("headerGap", changes ? changes->layout()->spacing() : -1),
            entry("handle", splitter ? splitter->handle(1)->height() : -1),
            QStringLiteral("dividers=") + dividers.join(QLatin1Char(',')),
            QStringLiteral("switcherGaps=") + gaps.join(QLatin1Char(',')),
            entry("treeCheck", page->tree()->columnWidth(ChangesTreeModel::Check)),
            entry("treeStatus", page->tree()->columnWidth(ChangesTreeModel::Status)),
            entry("switcher", page->treeButton()->width()),
            entry("amendWidth", amend->width()),
            QStringLiteral("amend=") + amend->text()};
}

// What the tree measures at the text size of the moment: its row height and
// where the depth geometry of a nested row lands.
QStringList treeMetrics(CommitPage *page)
{
    QTreeView *tree = page->tree();
    auto *model = qobject_cast<ChangesTreeModel *>(tree->model());
    const auto entry = [](const char *name, int value) { return QStringLiteral("%1=%2").arg(QLatin1String(name)).arg(value); };
    QStringList out{entry("check", tree->columnWidth(ChangesTreeModel::Check)),
                    entry("status", tree->columnWidth(ChangesTreeModel::Status))};
    for (const QString &path : {QStringLiteral("src"), QStringLiteral("src/deep"), QStringLiteral("src/a.txt")}) {
        const QModelIndex index = treeRow(model, path);
        if (!index.isValid())
            continue;
        const QRect rect = tree->visualRect(index.siblingAtColumn(ChangesTreeModel::Name));
        out << QStringLiteral("%1=%2,%3,%4").arg(path).arg(rect.left()).arg(rect.height()).arg(model->depth(index));
    }
    return out;
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

// The buttons of a row that are on screen, in the order they stand in.
QList<bool> visible(const QList<QToolButton *> &buttons)
{
    QList<bool> out;
    for (const QToolButton *b : buttons)
        out << b->isVisible();
    return out;
}

// A top bar in a host of its own, placed by hand so a resize of the bar is
// the bar's width and nothing else's. The names are long enough for every
// fold level to show up between the widest and the narrowest width.
struct BarFixture
{
    std::unique_ptr<QWidget> host;
    TopBar *bar = nullptr;

    // Pull, Push, Fetch, Merge: the order they fold away in, from the right.
    QList<QToolButton *> sync() const
    {
        return {bar->pullButton(), bar->pushButton(), bar->fetchButton(), bar->mergeButton()};
    }
    // The frame the two segments share.
    QWidget *tabs() const { return bar->changesTab()->parentWidget(); }
    QRect rectOf(QWidget *w) const { return QRect(w->mapTo(bar, QPoint(0, 0)), w->size()); }

    int levelAt(int width) const
    {
        bar->resize(width, bar->sizeHint().height());
        QCoreApplication::processEvents();
        return bar->foldLevel();
    }
    // The widest width the bar folds to `level` at, or -1 if it never does.
    int widthForLevel(int level) const
    {
        for (int width = bar->sizeHint().width(); width >= bar->minimumSizeHint().width(); --width)
            if (levelAt(width) == level)
                return width;
        return -1;
    }
};

BarFixture topBar(const QString &repository = QStringLiteral("omagit-workspace"),
                  const QString &branch = QStringLiteral("feature/askpass-login-dialog"), int count = 7)
{
    BarFixture f;
    f.host.reset(new QWidget);
    f.host->resize(1600, 200);
    f.bar = new TopBar(f.host.get());
    f.bar->setRepositoryName(repository);
    f.bar->setBranchLabel(branch);
    f.bar->setChangesCount(count);
    f.bar->move(0, 0);
    f.bar->resize(f.bar->sizeHint());
    f.host->show();
    return f;
}

// The whole window on a scratch repository of its own, built the way the
// application builds it: one commit, a file changed since and an unversioned
// one beside it.
struct WindowFixture
{
    std::unique_ptr<QTemporaryDir> dir;
    std::unique_ptr<QTemporaryDir> tools;
    std::unique_ptr<GitRepo> repo;
    std::unique_ptr<MainWindow> window;

    TopBar *bar() const { return window->findChild<TopBar *>(); }
    CommitPage *page() const { return window->findChild<CommitPage *>(); }
    MiniRail *rail() const { return window->findChild<MiniRail *>(); }
    CommitPopover *popover() const { return window->findChild<CommitPopover *>(); }
    AgentPopover *agentCard() const { return window->findChild<AgentPopover *>(); }
    QToolButton *tile() const { return rail()->commitTile(); }
    QWidget *host() const { return window->centralWidget(); }
    DiffView *diff() const { return window->findChild<DiffView *>(); }
    // The page's own message box; the card's is a child of the card.
    MessageEdit *pageEditor() const { return page()->findChild<MessageEdit *>(); }
    QString head() const { return repo->headCommit().hash; }
    // The rail's Refresh: its one tool button besides the commit tile.
    QToolButton *railRefresh() const
    {
        for (QToolButton *b : rail()->findChildren<QToolButton *>())
            if (b != tile())
                return b;
        return nullptr;
    }
    // The Mini layout with the commit popover open, the way main() opens it.
    bool openCard() const
    {
        window->setPaneLayout(PaneLayout::Mini, false);
        settle();
        QMetaObject::invokeMethod(window.get(), "showCommitPopover");
        settle();
        return popover()->isVisible();
    }
};

// `extra` more unversioned files (f01.txt, f02.txt, ...) beside the two, and
// `mini` starts the window in the Mini layout before its first show;
// `beforeShow` does whatever else main() would do before showing it (flags,
// a size of its own).
WindowFixture mainWindow(int extra = 0, bool mini = false,
                         const std::function<void(MainWindow *)> &beforeShow = {})
{
    WindowFixture f;
    f.dir.reset(new QTemporaryDir);
    f.tools.reset(new QTemporaryDir);
    const QString path = f.dir->path();
    const QString gitBinary = QStandardPaths::findExecutable(QStringLiteral("git"));
    if (!f.dir->isValid() || !f.tools->isValid() || gitBinary.isEmpty())
        return f;
    // A PATH with nothing on it but git: the window's commit page asks the
    // coding-agent CLIs for their models as it is built, and no such process
    // belongs in a test.
    if (!QFile::link(gitBinary, QDir(f.tools->path()).filePath(QStringLiteral("git"))))
        return f;
    if (!git(path, {"init", "-q", "-b", "main"}))
        return f;
    writeFixture(QDir(path).filePath(QStringLiteral("a.txt")), "a\n");
    if (!git(path, {"add", "-A"}) || !git(path, {"commit", "-q", "-m", "first"}, 1))
        return f;
    writeFixture(QDir(path).filePath(QStringLiteral("a.txt")), "a changed\n");
    writeFixture(QDir(path).filePath(QStringLiteral("u1.txt")), "u1\n");
    for (int i = 1; i <= extra; ++i)
        writeFixture(QDir(path).filePath(QStringLiteral("f%1.txt").arg(i, 2, 10, QLatin1Char('0'))), "f\n");
    // The window commits with git's own configuration: an identity of the
    // repository's own, and no signing, whatever the desktop's git says.
    if (!git(path, {"config", "user.name", "Test"}) || !git(path, {"config", "user.email", "test@example.com"})
        || !git(path, {"config", "commit.gpgsign", "false"}))
        return f;
    f.repo.reset(new GitRepo(path));
    const QByteArray env = qgetenv("PATH");
    qputenv("PATH", f.tools->path().toUtf8());
    f.window.reset(new MainWindow(f.repo.get()));
    qputenv("PATH", env);
    f.window->setAutoFetchEnabled(false);
    if (mini)
        f.window->setPaneLayout(PaneLayout::Mini, false);
    f.window->resize(1200, 800);
    if (beforeShow)
        beforeShow(f.window.get());
    f.window->show();
    return f;
}

// What a screen reader is told about the row, in the order it reads in. None
// of it depends on what the controls are wearing at the width of the moment.
QStringList barNames(TopBar *bar)
{
    QStringList out;
    for (const QWidget *w : QList<const QWidget *>{bar->repoButton(), bar->branchButton(), bar->changesTab(),
                                                   bar->historyTab(), bar->pullButton(), bar->pushButton(),
                                                   bar->fetchButton(), bar->mergeButton(), bar->moreButton(),
                                                   bar->layoutButton(), bar->diffToggle()})
        out << w->accessibleName();
    return out;
}

// The names the default fixture's bar carries, at every level.
const QStringList kBarNames{QStringLiteral("omagit-workspace"),
                            QStringLiteral("feature/askpass-login-dialog"),
                            QStringLiteral("Changes"),
                            QStringLiteral("History"),
                            QStringLiteral("Pull"),
                            QStringLiteral("Push"),
                            QStringLiteral("Fetch"),
                            QStringLiteral("Merge"),
                            QStringLiteral("More"),
                            QStringLiteral("Mini layout"),
                            QStringLiteral("Diff pane")};

// A sync button wearing its glyph alone, and the more button: the design's
// 28 px square with the badge's reserve beside it.
int iconFormWidth()
{
    return ui::space(28) + BadgeButton::kBadgeReserve;
}

// What a top bar measures at the text size of the moment, spelled out so a
// mismatch names itself.
QStringList barMetrics(TopBar *bar)
{
    const auto entry = [](const char *name, int value) { return QStringLiteral("%1=%2").arg(QLatin1String(name)).arg(value); };
    return {entry("hint", bar->sizeHint().width()),
            entry("hintHeight", bar->sizeHint().height()),
            entry("min", bar->minimumSizeHint().width()),
            entry("minHeight", bar->minimumSizeHint().height()),
            entry("changesTab", bar->changesTab()->sizeHint().width()),
            entry("historyTab", bar->historyTab()->sizeHint().width()),
            entry("tabs", bar->changesTab()->parentWidget()->sizeHint().width())};
}

// ---- The stacked layouts

// A menu's entries as they are once it has filled itself: what aboutToShow
// puts in it, without the menu ever being shown.
QList<QAction *> filledMenu(QMenu *menu)
{
    emit menu->aboutToShow();
    return menu->actions();
}

// The texts of those entries, "-" for a separator.
QStringList menuTexts(const QList<QAction *> &actions)
{
    QStringList out;
    for (const QAction *a : actions)
        out << (a->isSeparator() ? QStringLiteral("-") : a->text());
    return out;
}

// Whether `shot` has a pixel of `colour` in the columns from `fromX` up to
// (not including) `toX`, all of them by default.
bool imagePaints(const QImage &shot, const QColor &colour, int fromX = 0, int toX = INT_MAX)
{
    const QRgb wanted = colour.rgb() | 0xff000000;
    for (int y = 0; y < shot.height(); ++y)
        for (int x = qMax(0, fromX); x < qMin(shot.width(), toX); ++x)
            if ((shot.pixel(x, y) | 0xff000000) == wanted)
                return true;
    return false;
}

// Whether `w` paints `colour` anywhere, read off its own pixels.
bool paints(QWidget *w, const QColor &colour)
{
    return imagePaints(w->grab().toImage(), colour);
}

// The window's own splitter: the left section beside the diff pane.
QSplitter *bodySplitter(const WindowFixture &f)
{
    return qobject_cast<QSplitter *>(f.window->findChild<DiffPane *>()->parentWidget());
}

// What the window keeps of the user's layout choices, presence included: the
// stacked presentation must leave every one of them as it found them.
QStringList windowPrefs()
{
    QSettings conf;
    QStringList out;
    for (const QLatin1StringView key : {settings::kWindowLayout, settings::kWindowDiffPane, settings::kWindowFilesView,
                                        settings::kWindowLeftWidth})
        out << QString(key) + QLatin1Char('=')
                + (conf.contains(key) ? conf.value(key).toString() : QStringLiteral("<absent>"));
    return out;
}

// One pixel under the stacking width, or back out of it.
void stack(const WindowFixture &f)
{
    f.window->resize(ui::space(700) - 1, f.window->height());
    settle();
}

void unstack(const WindowFixture &f)
{
    f.window->resize(qMax(1200, ui::space(700) + 200), f.window->height());
    settle();
}

// The stacked row's controls, where they stand in the bar.
QStringList stackedRow(const BarFixture &f)
{
    QStringList out;
    for (QWidget *w : QList<QWidget *>{f.bar->repoButton(), f.bar->branchButton(), f.tabs(), f.bar->syncDropdown(),
                                       f.bar->moreButton()}) {
        const QRect r = f.rectOf(w);
        out << QStringLiteral("%1,%2 %3x%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
    }
    return out;
}

// ---- The Mini layout's commit tile and popover

// MiniRail.cpp's badge geometry, in physical pixels like the miniatures'
// badges: the tile widget keeps kTileBadgeRise above its square, where the
// badge overhangs the square's top-right corner by that much.
constexpr int kTileBadgeRise = 5, kTileBadgeSize = 13, kTileBadgeInsetX = 8;

// `w`'s rectangle in `ref`'s coordinates.
QRect rectIn(const QWidget *w, const QWidget *ref)
{
    return QRect(w->mapTo(ref, QPoint(0, 0)), w->size());
}

// The commit tile's painted square, in the tile's coordinates: space(40),
// centred sideways and at the bottom of the widget.
QRect tileSquare(const QToolButton *tile)
{
    const int side = ui::space(40);
    return QRect((tile->width() - side) / 2, tile->height() - side, side, side);
}

// The badge the tile paints over its square's corner.
QRect tileBadge(const QToolButton *tile)
{
    const QRect square = tileSquare(tile);
    return QRect(square.right() - kTileBadgeInsetX, square.top() - kTileBadgeRise, kTileBadgeSize, kTileBadgeSize);
}

// The window that holds `w` becomes the active one, so focus and the
// widget-scoped shortcuts (the card's Escape) behave as on a desktop.
bool activate(QWidget *w)
{
    w->activateWindow();
    return QTest::qWaitForWindowActive(w);
}

// A long paragraph, wrapped at any width a message box can have.
QString longParagraph()
{
    return QStringLiteral("Make the toolbar tiling aware so that the labels fold first, then the icons and then "
                          "the buttons themselves, and keep the branch name whole for as long as the row allows "
                          "it, eliding only at the very end when nothing else is left to fold away.");
}

// What a message box built fresh at `edit`'s width and font needs for `text`:
// the height the one on screen has to agree with, however its document was
// wrapped before.
int freshContentHeight(const MessageEdit *edit, const QString &text)
{
    MessageEdit fresh;
    fresh.setFont(edit->font());
    // The corner button's glyph decides how much of the width the text gets.
    fresh.cornerButton()->setText(edit->cornerButton()->text());
    fresh.applyTheme();
    fresh.resize(edit->size());
    fresh.setPlainText(text);
    fresh.show();
    if (!QTest::qWaitForWindowExposed(&fresh))
        return -1;
    QTest::qWait(30);
    return fresh.contentHeight();
}

// A model whose connections can be counted, for the badge's own.
class CountedModel : public QStandardItemModel
{
public:
    using QStandardItemModel::QStandardItemModel;
    int dataReceivers() const
    {
        return receivers(SIGNAL(dataChanged(QModelIndex, QModelIndex, QList<int>)));
    }
    int layoutReceivers() const
    {
        return receivers(SIGNAL(layoutChanged(QList<QPersistentModelIndex>, QAbstractItemModel::LayoutChangeHint)));
    }
};

// A checkable row of the counted model.
QList<QStandardItem *> checkRow(const QString &name, bool checked)
{
    auto *item = new QStandardItem(name);
    item->setCheckable(true);
    item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    return {item};
}

// The colour a fill of `color` at `alpha` leaves on `background`.
QColor over(const QColor &background, const QColor &color, qreal alpha)
{
    return QColor::fromRgbF(background.redF() * (1 - alpha) + color.redF() * alpha,
                            background.greenF() * (1 - alpha) + color.greenF() * alpha,
                            background.blueF() * (1 - alpha) + color.blueF() * alpha);
}

bool closeTo(const QColor &a, const QColor &b)
{
    return qAbs(a.red() - b.red()) <= 3 && qAbs(a.green() - b.green()) <= 3 && qAbs(a.blue() - b.blue()) <= 3;
}

// Presses the left button on whatever widget of `window` is at `pos`, as a
// click there would.
void clickAt(QWidget *window, const QPoint &pos, Qt::KeyboardModifiers modifiers = {})
{
    QWidget *target = window->childAt(pos);
    if (!target)
        target = window;
    QTest::mouseClick(target, Qt::LeftButton, modifiers, target->mapFrom(window, pos));
}

// Clicks the first button of the next modal message box, from inside the
// event loop its exec() runs. `clicked` says whether one came.
void clickNextMessageBox(bool *clicked)
{
    auto *timer = new QTimer;
    timer->setInterval(20);
    QObject::connect(timer, &QTimer::timeout, timer, [timer, clicked] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!box)
            return;
        timer->stop();
        timer->deleteLater();
        *clicked = true;
        QAbstractButton *button = box->buttons().value(0);
        // A real click, in the box's own window: the card must not take it
        // for a press outside itself.
        QTest::mouseClick(button, Qt::LeftButton);
    });
    timer->start();
}

// The separator above the commit tile.
QWidget *tileRule(const WindowFixture &f)
{
    for (QWidget *w : f.tile()->parentWidget()->findChildren<QWidget *>())
        if (w != f.tile() && w->height() == 1)
            return w;
    return nullptr;
}

// What the rail's commit section and the card measure at the text size of
// the moment, spelled out so a mismatch names itself.
QStringList popoverMetrics(const WindowFixture &f)
{
    MiniRail *rail = f.rail();
    QToolButton *tile = f.tile();
    CommitPopover *card = f.popover();
    QWidget *host = f.host();
    QWidget *rule = tileRule(f);
    const QRect square = tileSquare(tile).translated(rectIn(tile, rail).topLeft());
    const QRect ruleRect = rule ? rectIn(rule, rail) : QRect();
    const QRect cardRect = card->geometry();
    const QRect tileRect = rectIn(tile, host);
    const QRect railRect = rail->geometry();
    const QRect editor = rectIn(card->editor(), card);
    const QMargins m = card->layout()->contentsMargins();
    const QFont hint = card->hintLabel()->font();
    const auto entry = [](const char *name, int value) { return QStringLiteral("%1=%2").arg(QLatin1String(name)).arg(value); };
    return {entry("rail", rail->width()),
            QStringLiteral("tile=%1x%2").arg(tile->width()).arg(tile->height()),
            entry("square", square.width()),
            entry("ruleGap", ruleRect.top() - (rectIn(f.railRefresh(), rail).bottom() + 1)),
            entry("tileGap", square.top() - (ruleRect.bottom() + 1)),
            entry("cardGap", cardRect.x() - (railRect.x() + railRect.width())),
            entry("cardWidth", cardRect.width()),
            entry("anchor", (cardRect.y() + cardRect.height()) - (tileRect.y() + tileRect.height())),
            QStringLiteral("margins=%1,%2,%3,%4").arg(m.left()).arg(m.top()).arg(m.right()).arg(m.bottom()),
            entry("editorTop", editor.top()),
            entry("editorLeft", editor.left()),
            entry("editorHeight", editor.height()),
            entry("hintHeight", card->hintLabel()->height()),
            entry("hintPx", hint.pixelSize()),
            entry("hintBold", hint.bold()),
            entry("cardHeight", cardRect.height())};
}

// The `claude --help` sample of gitrepo_test.cpp's parseClaudeHelp check: the
// aliases fable, opus and sonnet, the levels low to max.
const char kClaudeHelp[] =
    "Options:\n"
    "  --effort <level>                      Effort level for the current session\n"
    "                                        (low, medium, high, xhigh, max)\n"
    "  --environment <environment_id>        Create a new cloud session\n"
    "  --model <model>                       Model for the current session. Provide\n"
    "                                        an alias for the latest model (e.g.\n"
    "                                        'fable', 'opus', or 'sonnet') or a\n"
    "                                        model's full name (e.g.\n"
    "                                        'claude-fable-5').\n"
    "  --no-chrome                           Disable Claude in Chrome integration\n";

// The parseCodexModels sample: GPT-6-Astra with six levels, GPT-5.5 with two.
const char kCodexModels[] =
    "{\"models\":[{\"slug\":\"gpt-5.5\",\"display_name\":\"GPT-5.5\",\"visibility\":\"list\",\"priority\":12,"
    "\"default_reasoning_level\":\"medium\",\"supported_reasoning_levels\":[{\"effort\":\"low\"},{\"effort\":\"high\"}]},"
    "{\"slug\":\"gpt-reserve\",\"display_name\":\"GPT-Reserve\",\"visibility\":\"hide\",\"priority\":3},"
    "{\"slug\":\"gpt-6-astra\",\"display_name\":\"GPT-6-Astra\",\"visibility\":\"list\",\"priority\":1,"
    "\"default_reasoning_level\":\"medium\",\"supported_reasoning_levels\":[{\"effort\":\"low\"},{\"effort\":\"medium\"},"
    "{\"effort\":\"high\"},{\"effort\":\"xhigh\"},{\"effort\":\"max\"},{\"effort\":\"ultra\"}]}]}";

// A fake agent CLI in `dir`: asked `$1` (`probe`), it prints `answer`;
// asked anything else, it reads the diff and answers with a one-line message.
// Shell builtins only: the tests' PATH has git on it and nothing else.
bool writeFakeAgent(const QString &dir, const QString &name, const char *probe, const QByteArray &answer)
{
    const QByteArray script = QByteArray("#!/bin/sh\nif [ \"$1\" = ") + probe
        + " ]; then\nwhile IFS= read -r line; do printf '%s\\n' \"$line\"; done <<'EOF'\n" + answer
        + "\nEOF\nelse\nwhile IFS= read -r line; do :; done\necho 'Fake subject'\nfi\n";
    return writeFixture(QDir(dir).filePath(name), script, true);
}

// A fake `claude`: --help prints the sample above.
bool writeFakeClaude(const QString &dir)
{
    return writeFakeAgent(dir, QStringLiteral("claude"), "--help", kClaudeHelp);
}

// A fake `codex`: `debug models` prints the catalog sample.
bool writeFakeCodex(const QString &dir)
{
    return writeFakeAgent(dir, QStringLiteral("codex"), "debug", kCodexModels);
}

// For the length of a test of the agent settings: PATH is `tools` (git and
// whatever fake agents a test put there), nothing is saved under agent/*, the
// catalogs are read afresh from what is on that PATH, and Omarchy's default
// agent is `omarchyDefault` (none when empty).
class AgentScope
{
public:
    explicit AgentScope(const QString &tools, const QString &omarchyDefault = QString())
        : m_path("PATH", tools.toUtf8())
    {
        QSettings().remove(QStringLiteral("agent"));
        const QString defaults = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + QStringLiteral("/omarchy/defaults");
        m_defaultFile = defaults + QStringLiteral("/agent");
        QFile::remove(m_defaultFile);
        if (!omarchyDefault.isEmpty() && QDir().mkpath(defaults))
            writeFixture(m_defaultFile, omarchyDefault.toUtf8() + '\n');
        for (const AgentSpec &agent : CommitMessageAgent::agents())
            CommitMessageAgent::catalog(agent.id, true);
    }
    ~AgentScope()
    {
        QSettings().remove(QStringLiteral("agent"));
        QFile::remove(m_defaultFile);
    }

private:
    ScopedEnv m_path;
    QString m_defaultFile;
};

// The model rows as the card shows them: name and id, the chosen one marked.
QStringList rowTexts(const AgentPopover *card)
{
    QStringList out;
    for (const QAbstractButton *row : card->modelRows())
        out << row->text() + QLatin1Char('|') + row->accessibleDescription() + (row->isChecked() ? QStringLiteral("|*") : QString());
    return out;
}

// The model row named `name`, or null.
QAbstractButton *modelRow(const AgentPopover *card, const QString &name)
{
    for (QAbstractButton *row : card->modelRows())
        if (row->text() == name)
            return row;
    return nullptr;
}

QString savedAgent(const char *key)
{
    return QSettings().value(QLatin1String(key)).toString();
}

// The visible labels of the card, their texts.
QStringList cardLabels(const AgentPopover *card)
{
    QStringList out;
    for (const QLabel *l : card->findChildren<QLabel *>())
        if (l->isVisible())
            out << l->text();
    return out;
}

// What the agent card measures at the text size of the moment, spelled out
// so a mismatch names itself: the installed state's parts, or the command
// rows of the none-installed one.
QStringList agentCardMetrics(const AgentPopover *card)
{
    QStringList out;
    const auto entry = [&out](const char *name, int value) { out << QStringLiteral("%1=%2").arg(QLatin1String(name)).arg(value); };
    const QMargins m = card->layout()->contentsMargins();
    out << QStringLiteral("margins=%1,%2,%3,%4").arg(m.left()).arg(m.top()).arg(m.right()).arg(m.bottom());
    entry("width", card->width());
    entry("height", card->height());
    if (card->agentPicker()) {
        entry("picker", card->agentPicker()->height());
        entry("pickerTop", card->agentPicker()->y());
    }
    for (const QAbstractButton *row : card->modelRows()) {
        entry("row", row->height());
        entry("rowBold", row->font().bold());
    }
    if (card->otherModelButton()) {
        entry("otherTop", card->otherModelButton()->y());
        entry("other", card->otherModelButton()->height());
        entry("otherWidth", card->otherModelButton()->width());
    }
    if (const LevelTrack *track = card->levelTrack()) {
        entry("track", track->height());
        entry("trackTop", track->y());
        entry("stop0", qRound(track->stopCentre(0).x()));
        entry("stopY", qRound(track->stopCentre(0).y()));
    }
    for (const QLabel *l : card->findChildren<QLabel *>()) {
        if (!l->isVisible())
            continue;
        if (l->objectName() == QLatin1String("agentPopoverNote") || l->objectName() == QLatin1String("agentPopoverSmall")) {
            entry("notePx", l->font().pixelSize());
            entry("noteBold", l->font().bold());
        }
    }
    for (const QFrame *row : card->findChildren<QFrame *>(QStringLiteral("commandRow"))) {
        if (!row->isVisible())
            continue;
        entry("command", row->height());
        entry("commandTop", row->y());
        const QMargins rm = row->layout()->contentsMargins();
        entry("commandPad", rm.left());
    }
    for (const QAbstractButton *copy : card->copyButtons())
        entry("copy", copy->width());
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

    // --- TopBar -------------------------------------------------------------

    // The row folds in seven steps as the window narrows, in the order the
    // design names: the sync labels, then the sync buttons, then the
    // repository label, then the tab labels, and the branch name last.
    void theTopBarFoldsInSevenStepsAsItNarrows()
    {
        BarFixture f = topBar();
        TopBar *bar = f.bar;
        QVERIFY(QTest::qWaitForWindowExposed(f.host.get()));
        settle();

        const int wide = bar->sizeHint().width();
        QCOMPARE(f.levelAt(wide + 200), 0);
        QCOMPARE(f.levelAt(wide), 0);     // the size hint is exactly what level 0 takes
        QVERIFY(f.levelAt(wide - 1) > 0); // one pixel under it, something folds

        // Every level shows up, in order, as the bar narrows...
        QList<int> seen;
        QHash<int, int> widest; // level -> the widest width it appears at
        for (int width = wide; width >= bar->minimumSizeHint().width(); --width) {
            const int level = f.levelAt(width);
            QVERIFY2(seen.isEmpty() || level >= seen.last(), "the bar unfolded while it narrowed");
            if (seen.isEmpty() || level != seen.last()) {
                seen << level;
                widest[level] = width;
            }
        }
        QCOMPARE(seen, QList<int>({0, 1, 2, 3, 4, 5, 6}));
        // ...and one pixel wider than a level starts is the level before it.
        for (int level = 1; level <= 6; ++level)
            QCOMPARE(f.levelAt(widest.value(level) + 1), level - 1);

        // What each level shows. The repository chip and the two toggles are
        // there at every one of them.
        const QHash<int, QList<bool>> syncShown{
            {0, {true, true, true, true}},    {1, {true, true, true, true}},  {2, {true, true, false, false}},
            {3, {true, true, false, false}},  {4, {true, true, false, false}}, {5, {false, false, false, false}},
            {6, {false, false, false, false}}};
        QList<int> repoWidths, tabWidths, branchWidths;
        for (int level = 0; level <= 6; ++level) {
            QCOMPARE(f.levelAt(widest.value(level, wide)), level);
            QCOMPARE(visible(f.sync()), syncShown.value(level));
            QCOMPARE(bar->moreButton()->isVisible(), level >= 2);
            QVERIFY(bar->repoButton()->isVisible());
            QVERIFY(bar->layoutButton()->isVisible());
            QVERIFY(bar->diffToggle()->isVisible());
            // Only the widest level spells the sync buttons out.
            QCOMPARE(bar->pullButton()->text().contains(QLatin1String("Pull")), level == 0);
            repoWidths << f.rectOf(bar->repoButton()).width();
            tabWidths << f.rectOf(f.tabs()).width();
            branchWidths << f.rectOf(bar->branchButton()).width();
        }
        QVERIFY(repoWidths.at(2) > repoWidths.at(3)); // the repository label goes at level 3
        for (int level = 3; level <= 6; ++level)
            QCOMPARE(repoWidths.at(level), ui::space(28)); // and the bare folder is 28 px wide
        QVERIFY(tabWidths.at(3) > tabWidths.at(4));   // the tab labels go at level 4
        QCOMPARE(tabWidths.at(5), tabWidths.at(4));
        QCOMPARE(tabWidths.at(6), tabWidths.at(4));
        QVERIFY(branchWidths.at(6) <= branchWidths.at(5)); // the last level is never the wider one

        // At its narrowest the branch name is elided — and only it: the glyph
        // and the chevron stay where they were.
        QCOMPARE(f.levelAt(bar->minimumSizeHint().width()), 6);
        const QString elided = bar->branchButton()->text();
        QVERIFY2(elided.contains(QChar(0x2026)), qPrintable(elided));
        QVERIFY(elided.startsWith(ui::icon(ui::kBranch, QStringLiteral("b"))));
        QVERIFY(elided.endsWith(ui::chevron()));

        // And the room coming back spells everything out again.
        QCOMPARE(f.levelAt(wide), 0);
        QCOMPARE(bar->repoButton()->text(), ui::icon(ui::kFolder) + QStringLiteral("omagit-workspace") + ui::chevron());
        QCOMPARE(bar->branchButton()->text(),
                 ui::icon(ui::kBranch) + QStringLiteral("feature/askpass-login-dialog") + ui::chevron());
    }

    // The tabs follow the middle of the whole bar and stop 16 px clear of
    // either group; the groups themselves stand against their own edges.
    void theTopBarCentresTheTabsBetweenItsGroups()
    {
        BarFixture f = topBar();
        TopBar *bar = f.bar;
        QVERIFY(QTest::qWaitForWindowExposed(f.host.get()));
        settle();

        const int wide = bar->sizeHint().width() + 400;
        QCOMPARE(f.levelAt(wide), 0);
        const QRect tabs = f.rectOf(f.tabs());
        QVERIFY2(qAbs(tabs.x() + tabs.width() / 2.0 - wide / 2.0) <= 1.0, "the tabs are not in the middle of the bar");

        // The left group against the left edge, the right group against the
        // right one, with the design's gaps inside them.
        const QRect repo = f.rectOf(bar->repoButton()), branch = f.rectOf(bar->branchButton());
        QCOMPARE(repo.x(), 0);
        QCOMPARE(branch.x() - (repo.x() + repo.width()), ui::space(4));
        QCOMPARE(f.rectOf(bar->diffToggle()).x() + bar->diffToggle()->width(), wide);
        QCOMPARE(f.rectOf(bar->diffToggle()).x() - (f.rectOf(bar->layoutButton()).x() + bar->layoutButton()->width()),
                 ui::space(4));
        QCOMPARE(f.rectOf(bar->pushButton()).x() - (f.rectOf(bar->pullButton()).x() + bar->pullButton()->width()),
                 ui::space(6));

        // At the narrowest width the middle is taken, so the clamp decides:
        // the tabs sit 16 px off both groups at once.
        QCOMPARE(f.levelAt(bar->minimumSizeHint().width()), 6);
        const QRect tight = f.rectOf(f.tabs());
        QCOMPARE(tight.x(), f.rectOf(bar->branchButton()).x() + bar->branchButton()->width() + ui::space(16));
        QCOMPARE(tight.x() + tight.width() + ui::space(16), f.rectOf(bar->moreButton()).x());
    }

    // A short branch name is never elided, and the widths of the two chips do
    // not become a minimum the window has to honour.
    void theTopBarKeepsAShortBranchWholeAtEveryWidth()
    {
        BarFixture f = topBar(QStringLiteral("omagit"), QStringLiteral("main"), 3);
        TopBar *bar = f.bar;
        QVERIFY(QTest::qWaitForWindowExposed(f.host.get()));
        settle();
        const QString canonical = ui::icon(ui::kBranch) + QStringLiteral("main") + ui::chevron();

        // A name this short is under the allowance the last level keeps, so
        // eliding would buy nothing and the level before it already fits.
        QCOMPARE(f.levelAt(bar->minimumSizeHint().width()), 5);
        QCOMPARE(bar->branchButton()->text(), canonical);
        QVERIFY(bar->minimumSizeHint().width() < bar->sizeHint().width());

        // Folding and unfolding again, several times over, leaves the
        // canonical text — never a shortened one shortened once more.
        for (int i = 0; i < 3; ++i) {
            f.levelAt(bar->minimumSizeHint().width());
            f.levelAt(bar->sizeHint().width());
        }
        QCOMPARE(bar->branchButton()->text(), canonical);
        QCOMPARE(bar->repoButton()->text(), ui::icon(ui::kFolder) + QStringLiteral("omagit") + ui::chevron());

        // However long the branch name, the last level keeps 72 px of it at
        // most, so the minimum hardly moves.
        BarFixture longName = topBar(QStringLiteral("omagit"), QString(120, QLatin1Char('x')), 3);
        QVERIFY(longName.bar->minimumSizeHint().width() - bar->minimumSizeHint().width() <= ui::space(72));
    }

    // The two tabs are one exclusive switch: they ask the window for a mode
    // instead of changing it, carry the changes count and nothing else.
    void theTopBarTabsCarryTheCountAndAskForTheMode()
    {
        BarFixture f = topBar();
        TopBar *bar = f.bar;
        QVERIFY(QTest::qWaitForWindowExposed(f.host.get()));
        settle();

        QCOMPARE(bar->changesTab()->accessibleName(), QStringLiteral("Changes"));
        QCOMPARE(bar->historyTab()->accessibleName(), QStringLiteral("History"));
        QVERIFY(bar->changesTab()->toolTip().contains(QLatin1String("Ctrl+1")));
        QVERIFY(bar->historyTab()->toolTip().contains(QLatin1String("Ctrl+2")));

        // The pill: gone at nothing to commit, wider with more digits, and
        // never on the History tab.
        const int history = bar->historyTab()->sizeHint().width();
        const int seven = bar->changesTab()->sizeHint().width();
        bar->setChangesCount(0);
        const int none = bar->changesTab()->sizeHint().width();
        bar->setChangesCount(128);
        QCOMPARE(bar->changesCount(), 128);
        const int many = bar->changesTab()->sizeHint().width();
        QVERIFY(none < seven);
        QVERIFY(seven < many);
        QCOMPARE(bar->historyTab()->sizeHint().width(), history);
        bar->setChangesCount(-4); // no such thing as a negative count
        QCOMPARE(bar->changesCount(), 0);

        QSignalSpy requests(bar, &TopBar::tabRequested);
        QTest::mouseClick(bar->historyTab(), Qt::LeftButton);
        QCOMPARE(requests.count(), 1);
        QCOMPARE(requests.at(0).at(0).value<TopBar::Tab>(), TopBar::Tab::History);
        QVERIFY(bar->historyTab()->isChecked());
        QVERIFY(!bar->changesTab()->isChecked());
        // The window putting the state back asks for nothing.
        bar->setCurrentTab(TopBar::Tab::Changes);
        QVERIFY(bar->changesTab()->isChecked());
        QVERIFY(!bar->historyTab()->isChecked());
        QCOMPARE(bar->currentTab(), TopBar::Tab::Changes);
        QCOMPARE(requests.count(), 1);
        QTest::mouseClick(bar->changesTab(), Qt::LeftButton);
        QCOMPARE(requests.count(), 2);
        QCOMPARE(requests.at(1).at(0).value<TopBar::Tab>(), TopBar::Tab::Changes);
    }

    // The more menu is exactly the buttons folding put in it, in the order
    // they stand in the row, with their counts spelled out.
    void theTopBarMoreMenuCarriesTheFoldedSyncButtons()
    {
        BarFixture f = topBar();
        TopBar *bar = f.bar;
        QVERIFY(QTest::qWaitForWindowExposed(f.host.get()));
        settle();
        bar->pullButton()->setCount(2);
        bar->pushButton()->setCount(1);
        bar->fetchButton()->setCount(4);
        bar->fetchButton()->setToolTip(QStringLiteral("Fetch from all remotes (Ctrl+F)"));
        bar->mergeButton()->setEnabled(false);

        // What the menu holds once it has filled itself, the way a click on
        // the more button fills it.
        const auto entries = [bar] {
            QMenu *menu = bar->moreButton()->menu();
            menu->popup(QPoint(0, 0));
            QCoreApplication::processEvents();
            const QList<QAction *> actions = menu->actions();
            menu->hide();
            return actions;
        };
        // The accent dot BadgeButton paints for a folded count; the button
        // keeps no getter for it, so it is read off its own pixels.
        const auto hasDot = [](BadgeButton *button) {
            const QImage shot = button->grab().toImage();
            const QRgb accent = OmarchyTheme::instance()->accent().rgb() | 0xff000000;
            for (int y = 0; y < shot.height(); ++y)
                for (int x = 0; x < shot.width(); ++x)
                    if ((shot.pixel(x, y) | 0xff000000) == accent)
                        return true;
            return false;
        };

        // The menu's own entries after the folded ones: a separator, Refresh,
        // Open repository…, Clone…, a separator and Keybindings.
        constexpr int kOwnEntries = 6;

        // Wide: nothing is folded, so there is no button.
        QCOMPARE(f.levelAt(bar->sizeHint().width()), 0);
        QVERIFY(!bar->moreButton()->isVisible());
        QCOMPARE(entries().size(), kOwnEntries - 1); // no sync group, so no separator after it

        // Narrow enough for Fetch and Merge to fold.
        QVERIFY(f.widthForLevel(2) > 0);
        QCOMPARE(f.levelAt(f.widthForLevel(2)), 2);
        QList<QAction *> actions = entries();
        QCOMPARE(actions.size(), 2 + kOwnEntries);
        QVERIFY(actions.at(2)->isSeparator());
        QVERIFY2(actions.at(0)->text().endsWith(QStringLiteral("Fetch  (4)")), qPrintable(actions.at(0)->text()));
        QVERIFY(actions.at(0)->text().startsWith(ui::icon(ui::kFetch, QStringLiteral("F")).trimmed()));
        QCOMPARE(actions.at(0)->toolTip(), QStringLiteral("Fetch from all remotes (Ctrl+F)"));
        QVERIFY(actions.at(0)->isEnabled());
        QVERIFY2(actions.at(1)->text().endsWith(QStringLiteral("Merge")), qPrintable(actions.at(1)->text()));
        QVERIFY(!actions.at(1)->isEnabled()); // as disabled as the button it stands for

        // An entry is the button's own click: whatever the window connected
        // to it happens.
        QSignalSpy fetched(bar->fetchButton(), &QToolButton::clicked);
        actions.at(0)->trigger();
        QCOMPARE(fetched.count(), 1);

        // All four fold at the narrowest levels, in the same order.
        QCOMPARE(f.levelAt(bar->minimumSizeHint().width()), 6);
        actions = entries();
        QCOMPARE(actions.size(), 4 + kOwnEntries);
        QStringList labels;
        for (const QAction *a : actions.mid(0, 4))
            labels << a->text().section(QStringLiteral("  "), 1, 1);
        QCOMPARE(labels, QStringList({QStringLiteral("Pull"), QStringLiteral("Push"), QStringLiteral("Fetch"),
                                      QStringLiteral("Merge")}));

        // The dot stands for the counts in the menu — the explicit folded set,
        // never what happens to be on screen. With the whole bar hidden every
        // button is invisible, and the dot still only counts those two.
        QCOMPARE(f.levelAt(f.widthForLevel(2)), 2);
        bar->fetchButton()->setCount(0);
        bar->mergeButton()->setCount(0);
        f.host->hide();
        QVERIFY2(!hasDot(bar->moreButton()), "the dot counted a button that is still on the row");
        bar->fetchButton()->setCount(3);
        QVERIFY(hasDot(bar->moreButton()));
    }

    // A Nerd Font glyph's ink hangs over the advance its metrics report, so a
    // tab segment gives it a box of the design's width at the least — and
    // measures itself on that very box, labelled and folded alike.
    void theTopBarTabGlyphsGetABoxOfTheirOwn()
    {
        BarFixture f = topBar();
        TopBar *bar = f.bar;
        QVERIFY(QTest::qWaitForWindowExposed(f.host.get()));
        settle();

        const OmarchyTheme *theme = OmarchyTheme::instance();
        const QFontMetrics plain(theme->uiFont());
        QFont boldFont = theme->uiFont();
        boldFont.setBold(true);
        const QFontMetrics bold(boldFont);
        QFont pillFont = theme->captionFont();
        pillFont.setBold(true);
        // The count pill: never narrower than it is tall.
        const int pill = qMax(ui::space(14),
                              QFontMetrics(pillFont).horizontalAdvance(QStringLiteral("7")) + 2 * ui::space(4));
        const auto box = [&plain](uint glyph, const QString &fallback) {
            return qMax(plain.horizontalAdvance(ui::icon(glyph, fallback).trimmed()), ui::space(14));
        };
        const int changesBox = box(ui::kCommit, QStringLiteral("C"));
        const int historyBox = box(ui::kHistory, QStringLiteral("H"));
        const int pad = 2 * ui::space(12);

        // Spelled out: padding, the glyph's box, the label and the pill, with
        // the design's gap between them. The label is measured bold, the
        // weight it wears while selected.
        QCOMPARE(f.levelAt(bar->sizeHint().width()), 0);
        QCOMPARE(bar->changesTab()->sizeHint().width(),
                 pad + changesBox + ui::space(6) + bold.horizontalAdvance(QStringLiteral("Changes")) + ui::space(6)
                     + pill);
        QCOMPARE(bar->historyTab()->sizeHint().width(),
                 pad + historyBox + ui::space(6) + bold.horizontalAdvance(QStringLiteral("History")));

        // Folded: the labels go, the box stays exactly as wide.
        QCOMPARE(f.levelAt(bar->minimumSizeHint().width()), 6);
        QCOMPARE(bar->changesTab()->sizeHint().width(), pad + changesBox + ui::space(6) + pill);
        QCOMPARE(bar->historyTab()->sizeHint().width(), pad + historyBox);
        // And the box is the design's width, not what the glyph happens to
        // advance by — otherwise the clock would be drawn half outside it.
        QCOMPARE(historyBox, ui::space(14));
    }

    // The icon form of a sync button is the design's square with the badge's
    // reserve beside it, not the size hint of a text button around a glyph —
    // and that width is what the levels are folded on.
    void theTopBarSyncButtonsFoldToTheDesignsSquare()
    {
        BarFixture f = topBar();
        TopBar *bar = f.bar;
        QVERIFY(QTest::qWaitForWindowExposed(f.host.get()));
        settle();

        // Level 0 spells them out; level 1 is the icon form of all four.
        const int wide = bar->sizeHint().width();
        QCOMPARE(f.levelAt(wide), 0);
        QList<int> labelled;
        for (QToolButton *b : f.sync())
            labelled << f.rectOf(b).width();
        const int labelledHeight = f.rectOf(bar->pullButton()).height();

        int saved = 0;
        QCOMPARE(f.levelAt(wide - 1), 1);
        for (int i = 0; i < f.sync().size(); ++i) {
            QCOMPARE(f.rectOf(f.sync().at(i)).width(), iconFormWidth());
            saved += labelled.at(i) - iconFormWidth();
        }
        QVERIFY2(saved > 0, "the icon form is no narrower than the labelled one");
        // Both forms are one height: the compact padding only takes from the
        // sides, so the icons line up with the chips and the tabs.
        QCOMPARE(f.rectOf(bar->pullButton()).height(), labelledHeight);
        QCOMPARE(f.rectOf(bar->moreButton()).height(), labelledHeight);

        // The thresholds are those widths and nothing else: level 1 stops
        // fitting exactly where the four labels' extra width runs out, and
        // level 2 trades Fetch and Merge for the more button, which is the
        // same square again.
        QCOMPARE(f.levelAt(wide - saved), 1);
        QCOMPARE(f.levelAt(wide - saved - 1), 2);
        QCOMPARE(f.rectOf(bar->moreButton()).width(), iconFormWidth());
        const int levelTwo = wide - saved - iconFormWidth() - ui::space(6);
        QCOMPARE(f.levelAt(levelTwo), 2);
        QCOMPARE(f.levelAt(levelTwo - 1), 3);
    }

    // What the bar is called stays what it is at every width: the names are
    // the canonical text, never the folded or elided one. And the widths the
    // levels are weighed on are measured on probes nobody ever sees.
    void theTopBarNamesItsControlsWhateverItIsWearing()
    {
        BarFixture f = topBar();
        TopBar *bar = f.bar;
        QVERIFY(QTest::qWaitForWindowExposed(f.host.get()));
        settle();

        for (int level = 0; level <= 6; ++level) {
            QCOMPARE(f.levelAt(f.widthForLevel(level)), level);
            QCOMPARE(barNames(bar), kBarNames);
        }
        bar->setChangesCount(42); // a remeasure changes nothing about them
        settle();
        QCOMPARE(barNames(bar), kBarNames);

        // The probes are the bar's own children, outside its layout and its
        // placement, and showing the bar leaves them behind.
        QList<QToolButton *> probes;
        for (QObject *child : bar->children())
            if (auto *b = qobject_cast<QToolButton *>(child))
                probes << b;
        QCOMPARE(probes.size(), 6); // the two chips' and one per sync button
        for (QToolButton *b : std::as_const(probes))
            QVERIFY2(!b->isVisible(), qPrintable(b->objectName()));
        // A probe's text follows the label it stands for and nothing else: a
        // remeasure that changes no label leaves every one of them as it was,
        // so the accessibility bridge hears of no name that is not news.
        const auto probeTexts = [&probes] {
            QStringList out;
            for (const QToolButton *b : std::as_const(probes))
                out << b->text();
            return out;
        };
        const QStringList measured = probeTexts();
        QVERIFY(!measured.contains(QString()));
        bar->setChangesCount(7);
        settle();
        QCOMPARE(probeTexts(), measured);
        QCOMPARE(bar->layout()->count(), 2); // the row and the hairline

        // A resize that stays inside one level leaves every live text alone:
        // no candidate a measurement tried on ever reaches the screen.
        const auto texts = [bar] {
            QStringList out;
            for (const QToolButton *b : QList<const QToolButton *>{bar->repoButton(), bar->branchButton(),
                                                                   bar->pullButton(), bar->pushButton(),
                                                                   bar->fetchButton(), bar->mergeButton(),
                                                                   bar->moreButton()})
                out << b->text();
            return out;
        };
        const int widest = f.widthForLevel(2);
        QCOMPARE(f.levelAt(widest), 2);
        const QStringList atTwo = texts();
        int narrowest = widest;
        for (int width = widest - 1; width >= bar->minimumSizeHint().width(); --width) {
            if (f.levelAt(width) != 2)
                break;
            narrowest = width;
            QCOMPARE(texts(), atTwo);
        }
        QVERIFY2(narrowest < widest, "level 2 is one width wide");
    }

    // --- The stacked top bar --------------------------------------------------

    // Stacked, the row has three levels of its own: the tab labels go, then
    // the branch name elides. The repository is the bare folder, the right
    // group the sync dropdown and More, and the toggles are gone.
    void theStackedTopBarFoldsInThreeSteps()
    {
        BarFixture f = topBar();
        TopBar *bar = f.bar;
        QVERIFY(QTest::qWaitForWindowExposed(f.host.get()));
        settle();
        const int ordinary = bar->sizeHint().width();
        const int ordinaryMin = bar->minimumSizeHint().width();
        QVERIFY(bar->diffTab()->isHidden());

        bar->setStacked(true);
        QVERIFY(bar->isStacked());
        QVERIFY(!bar->diffTab()->isHidden());
        const int wide = bar->sizeHint().width();
        QCOMPARE(f.levelAt(wide + 200), 0);
        QCOMPARE(f.levelAt(wide), 0);     // the size hint is exactly what S0 takes
        const int labels = f.rectOf(f.tabs()).width();
        QCOMPARE(f.levelAt(wide - 1), 1); // one pixel under it, the labels go

        QList<int> seen;
        QHash<int, int> widest;
        for (int width = wide; width >= bar->minimumSizeHint().width(); --width) {
            const int level = f.levelAt(width);
            QVERIFY2(seen.isEmpty() || level >= seen.last(), "the bar unfolded while it narrowed");
            if (seen.isEmpty() || level != seen.last()) {
                seen << level;
                widest[level] = width;
            }
        }
        QCOMPARE(seen, QList<int>({0, 1, 2}));
        for (int level = 1; level <= 2; ++level)
            QCOMPARE(f.levelAt(widest.value(level) + 1), level - 1);
        // S1 is S0 less what the labels took: its exact fit, and a pixel less.
        QCOMPARE(f.levelAt(widest.value(1)), 1);
        const int glyphs = f.rectOf(f.tabs()).width();
        QVERIFY(glyphs < labels);
        QCOMPARE(f.levelAt(wide - (labels - glyphs)), 1);
        QCOMPARE(f.levelAt(wide - (labels - glyphs) - 1), 2);

        // What every level shows.
        for (int level = 0; level <= 2; ++level) {
            QCOMPARE(f.levelAt(widest.value(level, wide)), level);
            QCOMPARE(visible(f.sync()), QList<bool>({false, false, false, false}));
            QVERIFY(!bar->layoutButton()->isVisible());
            QVERIFY(!bar->diffToggle()->isVisible());
            for (const QWidget *w : bar->findChildren<QWidget *>())
                QVERIFY2(!(w->isVisible() && w->minimumWidth() == 1 && w->maximumWidth() == 1 && w->height() > 1),
                         "the divider before the toggles is still there");
            QVERIFY(bar->syncDropdown()->isVisible());
            QVERIFY(bar->moreButton()->isVisible());
            QCOMPARE(bar->diffTab()->isVisible(), true);
            // The dropdown is the design's 92 px whatever its hint, the row's
            // height; More keeps its one width.
            QCOMPARE(f.rectOf(bar->syncDropdown()).width(), ui::space(92));
            QCOMPARE(f.rectOf(bar->syncDropdown()).height(), bar->syncDropdown()->parentWidget()->height());
            QCOMPARE(f.rectOf(bar->moreButton()).width(), iconFormWidth());
            // The gaps: the bare folder, 4 to the branch, 6 between the two
            // controls on the right, More against the right edge.
            const QRect repo = f.rectOf(bar->repoButton()), branch = f.rectOf(bar->branchButton());
            const QRect sync = f.rectOf(bar->syncDropdown()), more = f.rectOf(bar->moreButton());
            QCOMPARE(repo.x(), 0);
            QCOMPARE(repo.width(), ui::space(28));
            QCOMPARE(bar->repoButton()->text(), ui::icon(ui::kFolder, QStringLiteral("…")).trimmed());
            QCOMPARE(branch.x() - (repo.x() + repo.width()), ui::space(4));
            QCOMPARE(more.x() - (sync.x() + sync.width()), ui::space(6));
            QCOMPARE(more.x() + more.width(), bar->width());
            QCOMPARE(static_cast<SegmentButton *>(bar->changesTab())->isLabelled(), level == 0);
            QCOMPARE(static_cast<SegmentButton *>(bar->diffTab())->isLabelled(), level == 0);
            QVERIFY(bar->changesCount() > 0); // the pill stays at every level
        }

        // Wide, the tabs sit in the middle; at the narrowest the clamp keeps
        // them 16 px clear of both groups.
        QCOMPARE(f.levelAt(wide + 400), 0);
        const QRect middle = f.rectOf(f.tabs());
        QVERIFY(qAbs(middle.x() + middle.width() / 2.0 - (wide + 400) / 2.0) <= 1.0);
        QCOMPARE(f.levelAt(bar->minimumSizeHint().width()), 2);
        const QRect tight = f.rectOf(f.tabs());
        const QRect branch = f.rectOf(bar->branchButton());
        QCOMPARE(tight.x(), branch.x() + branch.width() + ui::space(16));
        QCOMPARE(tight.x() + tight.width() + ui::space(16), f.rectOf(bar->syncDropdown()).x());
        // The branch floor: 72 px of the name, the glyph and chevron kept.
        const int name = bar->branchButton()->fontMetrics().horizontalAdvance(bar->branchLabel());
        QVERIFY(name > ui::space(72));
        QCOMPARE(f.levelAt(widest.value(1)), 1);
        const int unelided = f.rectOf(bar->branchButton()).width();
        QCOMPARE(f.levelAt(bar->minimumSizeHint().width()), 2);
        QCOMPARE(f.rectOf(bar->branchButton()).width(), unelided - (name - ui::space(72)));
        const QString elided = bar->branchButton()->text();
        QVERIFY2(elided.contains(QChar(0x2026)), qPrintable(elided));
        QVERIFY(elided.endsWith(ui::chevron()));
        QVERIFY(bar->minimumSizeHint().width() < bar->sizeHint().width());

        // Unstacked at a wide width: level 0 of the ordinary row, every text
        // canonical again, the Diff tab gone and the ordinary hints back.
        bar->setStacked(false);
        QVERIFY(bar->diffTab()->isHidden());
        QCOMPARE(bar->sizeHint().width(), ordinary);
        QCOMPARE(bar->minimumSizeHint().width(), ordinaryMin);
        QCOMPARE(f.levelAt(ordinary), 0);
        QCOMPARE(bar->repoButton()->text(), ui::icon(ui::kFolder) + QStringLiteral("omagit-workspace") + ui::chevron());
        QCOMPARE(bar->branchButton()->text(),
                 ui::icon(ui::kBranch) + QStringLiteral("feature/askpass-login-dialog") + ui::chevron());
        QCOMPARE(visible(f.sync()), QList<bool>({true, true, true, true}));
        QVERIFY(bar->pullButton()->text().contains(QLatin1String("Pull")));
        QVERIFY(!bar->syncDropdown()->isVisible());
        QVERIFY(!bar->moreButton()->isVisible());
        QVERIFY(bar->layoutButton()->isVisible());
        QVERIFY(bar->diffToggle()->isVisible());
        QVERIFY(!bar->diffTab()->isVisible());
        QCOMPARE(barNames(bar), kBarNames);
    }

    // The Diff tab between the other two: one exclusive switch of three, that
    // asks and is told.
    void theStackedTabsAskForTheirPresentation()
    {
        BarFixture f = topBar();
        TopBar *bar = f.bar;
        bar->setStacked(true);
        bar->resize(bar->sizeHint());
        QVERIFY(QTest::qWaitForWindowExposed(f.host.get()));
        settle();

        QCOMPARE(bar->diffTab()->accessibleName(), QStringLiteral("Diff"));
        QCOMPARE(bar->diffTab()->toolTip(), QStringLiteral("The diff of the current file, with the file rail (Ctrl+Shift+B)"));
        QCOMPARE(bar->syncDropdown()->accessibleName(), QStringLiteral("Sync"));
        QCOMPARE(bar->syncDropdown()->toolTip(),
                 QStringLiteral("Pull, push, fetch or merge (Ctrl+P, Ctrl+Shift+P, Ctrl+F, Ctrl+Shift+M)"));
        QVERIFY(bar->changesTab()->x() < bar->diffTab()->x());
        QVERIFY(bar->diffTab()->x() < bar->historyTab()->x());

        QSignalSpy requests(bar, &TopBar::tabRequested);
        const auto checked = [bar] {
            return QList<bool>{bar->changesTab()->isChecked(), bar->diffTab()->isChecked(), bar->historyTab()->isChecked()};
        };
        QTest::mouseClick(bar->diffTab(), Qt::LeftButton);
        QCOMPARE(requests.count(), 1);
        QCOMPARE(requests.last().at(0).value<TopBar::Tab>(), TopBar::Tab::Diff);
        QCOMPARE(checked(), QList<bool>({false, true, false}));
        QCOMPARE(bar->currentTab(), TopBar::Tab::Diff);

        bar->setCurrentTab(TopBar::Tab::History); // the window's word asks nothing back
        QCOMPARE(requests.count(), 1);
        QCOMPARE(checked(), QList<bool>({false, false, true}));
        bar->setCurrentTab(TopBar::Tab::Diff);
        QCOMPARE(checked(), QList<bool>({false, true, false}));
        QCOMPARE(requests.count(), 1);

        QTest::mouseClick(bar->changesTab(), Qt::LeftButton);
        QCOMPARE(requests.last().at(0).value<TopBar::Tab>(), TopBar::Tab::Changes);
        QTest::mouseClick(bar->historyTab(), Qt::LeftButton);
        QCOMPARE(requests.last().at(0).value<TopBar::Tab>(), TopBar::Tab::History);
        QCOMPARE(requests.count(), 3);
        QCOMPARE(checked(), QList<bool>({false, false, true}));
    }

    // A segment leaves the strip and comes back through setSegmentVisible():
    // the hint, the placement and the dividers follow at once, even though
    // the strip's own rectangle does not move.
    void segmentStripHidesAndShowsASegment()
    {
        QWidget host;
        QList<SegmentButton *> segments;
        for (const QString &name : {QStringLiteral("One"), QStringLiteral("Two"), QStringLiteral("Three")}) {
            auto *s = ui::toolButton<SegmentButton>(name);
            s->setGlyph(ui::kCommit, QStringLiteral("C"));
            segments << s;
        }
        auto *strip = new SegmentStrip(segments, &host);
        const QSize all = strip->sizeHint();
        const int middle = segments.at(1)->sizeHint().width();
        strip->setGeometry(0, 0, all.width(), all.height());
        host.resize(all.width() + 40, all.height() + 40);
        host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        settle();

        // The columns painted in the frame's colour between the segments.
        const auto dividers = [strip] {
            const QImage shot = strip->grab().toImage();
            const QRgb line = OmarchyTheme::instance()->normalBorder().rgb() | 0xff000000;
            QList<int> out;
            for (int x = 1; x < strip->width() - 1; ++x) {
                bool inside = false;
                for (const SegmentButton *s : strip->segments())
                    if (!s->isHidden() && x >= s->x() && x < s->x() + s->width())
                        inside = true;
                if (!inside && (shot.pixel(x, strip->height() / 2) | 0xff000000) == line)
                    out << x;
            }
            return out;
        };
        QCOMPARE(dividers(), QList<int>({segments.at(1)->x() - 1, segments.at(2)->x() - 1}));

        const QRect frame = strip->geometry();
        strip->setSegmentVisible(segments.at(1), false);
        QCOMPARE(strip->geometry(), frame);
        QVERIFY(segments.at(1)->isHidden());
        QCOMPARE(strip->sizeHint(), QSize(all.width() - middle - 1, all.height()));
        QCOMPARE(segments.at(0)->x(), 1);
        QCOMPARE(segments.at(2)->x(), segments.at(0)->x() + segments.at(0)->width() + 1);
        QCOMPARE(segments.at(2)->x() + segments.at(2)->width(), frame.width() - 1); // the last takes the rest
        settle();
        QCOMPARE(dividers(), QList<int>({segments.at(2)->x() - 1}));

        // A hidden ancestor changes nothing about the hint.
        const QSize two = strip->sizeHint();
        host.hide();
        QCOMPARE(strip->sizeHint(), two);
        host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));

        // Stretch: the two taking part share the width.
        strip->setStretch(true);
        settle();
        const int each = frame.width() / 2;
        QCOMPARE(segments.at(0)->geometry(), QRect(1, 1, each - 1, frame.height() - 2));
        QCOMPARE(segments.at(2)->geometry(), QRect(each + 1, 1, frame.width() - 1 - (each + 1), frame.height() - 2));

        // And back: three again, in their order, one divider per pair.
        strip->setStretch(false);
        strip->setSegmentVisible(segments.at(1), true);
        QCOMPARE(strip->sizeHint(), all);
        QCOMPARE(strip->geometry(), frame);
        QVERIFY(segments.at(0)->x() < segments.at(1)->x());
        QVERIFY(segments.at(1)->x() < segments.at(2)->x());
        settle();
        QCOMPARE(dividers(), QList<int>({segments.at(1)->x() - 1, segments.at(2)->x() - 1}));
    }

    // The dropdown reads the two counts, the busy state and Merge's mark off
    // the buttons it stands for, and wears no badge of its own.
    void theSyncDropdownMirrorsTheButtons()
    {
        BarFixture f = topBar();
        TopBar *bar = f.bar;
        bar->setStacked(true);
        bar->resize(bar->sizeHint());
        QVERIFY(QTest::qWaitForWindowExposed(f.host.get()));
        settle();
        BadgeButton *sync = bar->syncDropdown();
        const QColor accent = OmarchyTheme::instance()->accent();
        const auto shot = [sync] { return sync->grab().toImage(); };

        // Zero on both sides: plain 0s, nothing in the accent colour.
        QVERIFY(sync->text().isEmpty());
        QVERIFY(!paints(sync, accent));
        const QImage zero = shot();
        bar->pullButton()->setCount(2);
        QVERIFY(paints(sync, accent)); // a positive count is accent text
        bar->pullButton()->setCount(0);
        QCOMPARE(shot(), zero);

        // Set while hidden, shown as it is.
        f.host->hide();
        bar->pullButton()->setCount(3);
        bar->pushButton()->setCount(1);
        f.host->show();
        QVERIFY(QTest::qWaitForWindowExposed(f.host.get()));
        settle();
        const QImage counted = shot();
        QVERIFY(counted != zero);
        BarFixture fresh = topBar();
        fresh.bar->setStacked(true);
        fresh.bar->resize(fresh.bar->sizeHint());
        fresh.bar->pullButton()->setCount(3);
        fresh.bar->pushButton()->setCount(1);
        QVERIFY(QTest::qWaitForWindowExposed(fresh.host.get()));
        settle();
        QCOMPARE(fresh.bar->syncDropdown()->grab().toImage(), counted);
        // Two digits fit; a hundred and up is 99+ whatever the number.
        bar->pullButton()->setCount(99);
        const QImage ninetyNine = shot();
        bar->pullButton()->setCount(100);
        const QImage hundred = shot();
        QVERIFY(hundred != ninetyNine);
        bar->pullButton()->setCount(250);
        QCOMPARE(shot(), hundred);
        bar->pullButton()->setCount(3);
        QCOMPARE(shot(), counted);

        // Busy: Pull alone, Push alone, then both, then neither; each side's
        // field is watched on its own, so one side's dots never stand in for
        // the other's. A busy Pull pushes Push's count along, so a side that
        // is not busy is known by its field holding still while the other's
        // dots walk, the count's accent in it.
        const auto field = [&shot](int from, int to) {
            const QImage all = shot();
            return all.copy(ui::space(from), 0, ui::space(to) - ui::space(from), all.height());
        };
        const auto pullField = [&field] { return field(24, 38); };
        const auto pushField = [&field] { return field(54, 68); };
        const QImage pullCounted = pullField(), pushCounted = pushField();

        bar->pullButton()->setBusy(true);
        QImage pullBusy = pullField();
        const QImage pushStill = pushField();
        QVERIFY(pullBusy != pullCounted);
        QTRY_VERIFY_WITH_TIMEOUT(pullField() != pullBusy, 2000);
        QCOMPARE(pushField(), pushStill);
        QVERIFY(imagePaints(pushStill, accent));
        bar->pullButton()->setBusy(false);
        QCOMPARE(shot(), counted);

        bar->pushButton()->setBusy(true);
        QImage pushBusy = pushField();
        QVERIFY(pushBusy != pushCounted);
        QTRY_VERIFY_WITH_TIMEOUT(pushField() != pushBusy, 2000);
        QCOMPARE(pullField(), pullCounted);
        bar->pushButton()->setBusy(false);
        QCOMPARE(shot(), counted);

        bar->pullButton()->setBusy(true);
        bar->pushButton()->setBusy(true);
        pullBusy = pullField();
        pushBusy = pushField();
        QTRY_VERIFY_WITH_TIMEOUT(pullField() != pullBusy && pushField() != pushBusy, 2000);
        bar->pullButton()->setBusy(false);
        bar->pushButton()->setBusy(false);
        QCOMPARE(pullField(), pullCounted);
        QCOMPARE(pushField(), pushCounted);
        QCOMPARE(shot(), counted); // the counts are back
        QTest::qWait(400);
        QCOMPARE(shot(), counted); // and nothing walks any more

        // Merge's mark, and its colour; none of the dropdown's own.
        const QColor red = OmarchyTheme::instance()->color(QStringLiteral("red"));
        bar->mergeButton()->setMark(QStringLiteral("!"), red);
        QCOMPARE(sync->markText(), QStringLiteral("!"));
        QCOMPARE(sync->markColor(), red);
        bar->mergeButton()->setMark(QString(), red);
        QVERIFY(sync->markText().isEmpty());
        QCOMPARE(shot(), counted);
        QCOMPARE(sync->count(), 0);
        QVERIFY(!sync->isBusy());
    }

    // The design's positions are the least each field gets: a count wider
    // than two digits pushes the rest along and widens the dropdown, and
    // Merge's corner mark gets room of its own, so neither lands on the
    // chevron. At the design's text size and a larger one.
    void theSyncDropdownMakesRoomForItsContent()
    {
        for (const int base : {12, 16}) {
            QTemporaryDir dir, home;
            QVERIFY(dir.isValid() && home.isValid());
            QVERIFY(writeFixture(QDir(dir.path()).filePath(QStringLiteral("shell.toml")),
                                 QByteArray("[font]\nbase-size = ") + QByteArray::number(base) + '\n'));
            {
                ScopedEnv themeDir("OMAGIT_THEME_DIR", dir.path().toUtf8());
                ScopedEnv scratchHome("HOME", home.path().toUtf8());
                OmarchyTheme theme;
                QCOMPARE(theme.fontBase(), base);
                theme.apply(*qApp);

                BarFixture f = topBar();
                TopBar *bar = f.bar;
                bar->setStacked(true);
                bar->resize(bar->sizeHint());
                QVERIFY(QTest::qWaitForWindowExposed(f.host.get()));
                settle();
                BadgeButton *sync = bar->syncDropdown();
                const QColor accent = theme.accent();
                const QColor red = theme.color(QStringLiteral("red"));
                // The chevron's box, the dropdown's last field, space(10) from
                // its right edge once the content decides the width; the glyph
                // is centred in it.
                const int chevronAdvance = QFontMetrics(theme.uiFont()).horizontalAdvance(ui::chevron().trimmed());
                const int chevronBox = qMax(chevronAdvance, ui::space(14));
                const auto width = [&f, sync] { return f.rectOf(sync).width(); };

                bar->pullButton()->setCount(2);
                bar->pushButton()->setCount(1);
                QCOMPARE(width(), ui::space(92));

                // 99+ in accent, the chevron (foreground) clear of it.
                bar->pullButton()->setCount(120);
                const int wide = width();
                QVERIFY2(wide > ui::space(92), qPrintable(QString::number(wide)));
                QImage grab = sync->grab().toImage();
                QVERIFY(imagePaints(grab, accent));
                QVERIFY(!imagePaints(grab, accent, wide - ui::space(10) - chevronBox, wide - ui::space(10)));

                // The mark widens it by its own room and stays off the chevron
                // glyph. (The badge may take the box's last column: at base 12
                // it starts one pixel inside, well clear of the glyph.)
                bar->mergeButton()->setMark(QStringLiteral("!"), red);
                QCOMPARE(width(), wide + ui::space(6));
                grab = sync->grab().toImage();
                QVERIFY(imagePaints(grab, red));
                const int glyphLeft = wide - ui::space(10) - chevronBox + (chevronBox - chevronAdvance) / 2;
                QVERIFY(!imagePaints(grab, red, glyphLeft, glyphLeft + chevronAdvance));

                // Back to the design's width.
                bar->mergeButton()->setMark(QString(), red);
                bar->pullButton()->setCount(2);
                QCOMPARE(width(), ui::space(92));

                f.host.reset();
            }
            g_theme.reset(new OmarchyTheme);
            g_theme->apply(*qApp);
            QVERIFY(OmarchyTheme::instance() == g_theme.get());
        }
    }

    // The dropdown's menu: the four sync buttons, spelled out, doing what the
    // buttons do.
    void theSyncMenuMirrorsTheButtons()
    {
        BarFixture f = topBar();
        TopBar *bar = f.bar;
        bar->setStacked(true);
        bar->resize(bar->sizeHint());
        QVERIFY(QTest::qWaitForWindowExposed(f.host.get()));
        settle();
        QToolButton *sync = bar->syncDropdown();
        QCOMPARE(sync->popupMode(), QToolButton::InstantPopup);
        QVERIFY(qobject_cast<TickMenu *>(sync->menu()));
        bar->pullButton()->setCount(2);
        bar->pullButton()->setToolTip(QStringLiteral("Pull 2 commits (Ctrl+P)"));
        bar->pushButton()->setToolTip(QStringLiteral("Push to origin/main (Ctrl+Shift+P)"));
        bar->fetchButton()->setEnabled(false);
        bar->mergeButton()->setToolTip(QStringLiteral("Merge another branch (Ctrl+Shift+M)"));

        const QList<QAction *> actions = filledMenu(sync->menu());
        const auto entry = [](uint glyph, const QString &fallback, const QString &label) {
            return ui::icon(glyph, fallback).trimmed() + QStringLiteral("  ") + label;
        };
        QCOMPARE(menuTexts(actions),
                 QStringList({entry(ui::kPull, QStringLiteral("↓"), QStringLiteral("Pull  (2)")),
                              entry(ui::kPush, QStringLiteral("↑"), QStringLiteral("Push")),
                              entry(ui::kFetch, QStringLiteral("F"), QStringLiteral("Fetch")), QStringLiteral("-"),
                              entry(ui::kMerge, QStringLiteral("M"), QStringLiteral("Merge…"))}));
        QCOMPARE(actions.at(0)->toolTip(), QStringLiteral("Pull 2 commits (Ctrl+P)"));
        QCOMPARE(actions.at(1)->toolTip(), QStringLiteral("Push to origin/main (Ctrl+Shift+P)"));
        QCOMPARE(actions.at(4)->toolTip(), QStringLiteral("Merge another branch (Ctrl+Shift+M)"));
        QVERIFY(actions.at(0)->isEnabled());
        QVERIFY(!actions.at(2)->isEnabled());

        const QList<QToolButton *> sources{bar->pullButton(), bar->pushButton(), bar->fetchButton(), bar->mergeButton()};
        const QList<int> rows{0, 1, 2, 4};
        bar->fetchButton()->setEnabled(true);
        const QList<QAction *> again = filledMenu(sync->menu());
        for (int i = 0; i < sources.size(); ++i) {
            QSignalSpy clicked(sources.at(i), &QToolButton::clicked);
            again.at(rows.at(i))->trigger();
            QCOMPARE(clicked.count(), 1);
        }
    }

    // More carries the window's own commands after whatever sync buttons are
    // folded into it; stacked, it is always there, with no sync entry and no
    // dot.
    void theMoreMenuCarriesTheWindowsCommands()
    {
        BarFixture f = topBar();
        TopBar *bar = f.bar;
        QVERIFY(QTest::qWaitForWindowExposed(f.host.get()));
        settle();
        const QStringList own{ui::icon(ui::kRefresh) + QStringLiteral("Refresh"),
                              ui::icon(ui::kFolderOpen) + QStringLiteral("Open repository…"),
                              ui::icon(ui::kFetch) + QStringLiteral("Clone…"), QStringLiteral("-"),
                              ui::icon(ui::kInfo) + QStringLiteral("Keybindings")};

        // Folded: the sync entries, a separator, then the window's.
        QCOMPARE(f.levelAt(f.widthForLevel(2)), 2);
        QList<QAction *> actions = filledMenu(bar->moreButton()->menu());
        QCOMPARE(menuTexts(actions).mid(2), QStringList({QStringLiteral("-")}) + own);
        QCOMPARE(actions.at(3)->toolTip(), QStringLiteral("Re-read the repository (F5)"));
        QCOMPARE(actions.at(4)->toolTip(), QStringLiteral("Pick a folder inside a git repository (Ctrl+O)"));
        QCOMPARE(actions.at(5)->toolTip(), QStringLiteral("Download a repository from a URL or GitHub (Ctrl+Shift+O)"));
        QCOMPARE(actions.at(7)->toolTip(), QStringLiteral("Every keyboard shortcut (Ctrl+K)"));

        // Stacked: only the window's, with no separator in front.
        bar->pullButton()->setCount(4);
        bar->fetchButton()->setCount(2);
        bar->setStacked(true);
        f.levelAt(bar->sizeHint().width());
        QVERIFY(bar->moreButton()->isVisible());
        QCOMPARE(f.rectOf(bar->moreButton()).width(), iconFormWidth());
        actions = filledMenu(bar->moreButton()->menu());
        QCOMPARE(menuTexts(actions), own);
        // No dot: the sync counts are the dropdown's to show.
        QVERIFY(bar->moreButton()->markText().isEmpty());
        QVERIFY(!paints(bar->moreButton(), OmarchyTheme::instance()->accent()));

        QSignalSpy refresh(bar, &TopBar::refreshRequested), open(bar, &TopBar::openRepositoryRequested),
            clone(bar, &TopBar::cloneRequested), keys(bar, &TopBar::keybindingsRequested);
        actions.at(0)->trigger();
        actions.at(1)->trigger();
        actions.at(2)->trigger();
        actions.at(4)->trigger();
        QCOMPARE(QList<int>({int(refresh.count()), int(open.count()), int(clone.count()), int(keys.count())}),
                 QList<int>({1, 1, 1, 1}));

        // Back on the ordinary row at level 0 there is no More at all, and the
        // dot follows the folded set again.
        bar->setStacked(false);
        QCOMPARE(f.levelAt(bar->sizeHint().width()), 0);
        QVERIFY(!bar->moreButton()->isVisible());
        QCOMPARE(f.levelAt(f.widthForLevel(2)), 2);
        QCOMPARE(bar->moreButton()->markText(), QStringLiteral("•")); // Fetch's 2, folded
    }

    // --- The stacked window -----------------------------------------------------

    // One pixel under the scaled 700 px the body stacks, at 700 it does not;
    // what it shows follows the preferences without writing any of them.
    void theWindowStacksBelowItsStackingWidth()
    {
        QSettings().remove(settings::kWindowLeftWidth);
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        settle();
        MainWindow *w = f.window.get();
        auto *diffPane = w->findChild<DiffPane *>();
        auto *history = w->findChild<HistoryView *>();
        const QStringList prefs = windowPrefs(); // after the constructor's own migration
        QVERIFY(!w->isStacked());

        w->resize(ui::space(700), 800);
        settle();
        QVERIFY(!w->isStacked());
        QVERIFY(!f.bar()->isStacked());
        w->resize(ui::space(700) - 1, 800);
        settle();
        QVERIFY(w->isStacked());
        QVERIFY(f.bar()->isStacked());
        QVERIFY(f.page()->isStacked());

        // Docked, Commit: the Changes tab, the page filling the body.
        QCOMPARE(f.bar()->currentTab(), TopBar::Tab::Changes);
        QVERIFY(f.page()->isVisible());
        QVERIFY(!f.rail()->isVisible());
        QVERIFY(!diffPane->isVisible());
        QCOMPARE(w->paneLayout(), PaneLayout::Docked);
        QVERIFY(w->diffPaneVisible());
        // The toggles still say what the preferences are; the bar hides them.
        QVERIFY(!f.bar()->layoutButton()->isVisible());
        QVERIFY(!f.bar()->layoutButton()->isChecked());
        QVERIFY(f.bar()->diffToggle()->isChecked());
        QCOMPARE(windowPrefs(), prefs);

        // The preference setters, stacked: Mini is the Diff tab, Docked the
        // page; hiding the diff leaves both Diff and Mini; showing it again
        // leaves the tab alone.
        const auto diffShown = [&] {
            return w->diffTab() && f.rail()->isVisible() && diffPane->isVisible() && !f.page()->isVisible()
                && f.bar()->currentTab() == TopBar::Tab::Diff;
        };
        w->setPaneLayout(PaneLayout::Mini, false);
        settle();
        QVERIFY(diffShown());
        w->setDiffPaneVisible(true, false);
        QVERIFY(diffShown());
        w->setPaneLayout(PaneLayout::Docked, false);
        settle();
        QVERIFY(!w->diffTab());
        QVERIFY(f.page()->isVisible());
        QCOMPARE(f.bar()->currentTab(), TopBar::Tab::Changes);
        w->setPaneLayout(PaneLayout::Mini, false);
        w->setDiffPaneVisible(false, false);
        settle();
        QVERIFY(!w->diffTab());
        QCOMPARE(w->paneLayout(), PaneLayout::Docked); // the existing coupling
        QVERIFY(!w->diffPaneVisible());
        w->setDiffPaneVisible(true, false);
        QVERIFY(!w->diffTab());
        QVERIFY(f.page()->isVisible());
        QCOMPARE(windowPrefs(), prefs);
        // ...and the persisting ones still save, as they always did.
        w->setPaneLayout(PaneLayout::Mini);
        QCOMPARE(QSettings().value(settings::kWindowLayout).toString(), QStringLiteral("mini"));
        QVERIFY(w->diffTab());
        w->setPaneLayout(PaneLayout::Docked);
        QCOMPARE(QSettings().value(settings::kWindowLayout).toString(), QStringLiteral("docked"));
        QCOMPARE(windowPrefs(), prefs);

        // Out of it: Docked beside the diff, at the remembered left width...
        QSettings().setValue(settings::kWindowLeftWidth, 300);
        unstack(f);
        QVERIFY(!w->isStacked());
        QVERIFY(f.page()->isVisible());
        QVERIFY(diffPane->isVisible());
        QVERIFY(!f.rail()->isVisible());
        QVERIFY(f.bar()->layoutButton()->isVisible());
        QTRY_COMPARE(bodySplitter(f)->sizes().first(), 300);
        // ...or at 45% of it with none, and the stacked page's width is not
        // what gets remembered.
        stack(f);
        QSettings().remove(settings::kWindowLeftWidth);
        unstack(f);
        const QMargins margins = f.host()->layout()->contentsMargins();
        const int total = w->width() - margins.left() - margins.right();
        QTRY_COMPARE(bodySplitter(f)->sizes().first(), total * 45 / 100);
        QVERIFY(!QSettings().contains(settings::kWindowLeftWidth));

        // Mini and the history: the Diff tab, the rail on the commit's files.
        w->setPaneLayout(PaneLayout::Mini, false);
        w->setMode(MainWindow::HistoryMode);
        settle();
        stack(f);
        QVERIFY(w->diffTab());
        QCOMPARE(f.bar()->currentTab(), TopBar::Tab::Diff);
        QCOMPARE(w->mode(), MainWindow::HistoryMode);
        QVERIFY(f.rail()->isVisible());
        QCOMPARE(f.rail()->list()->model(), history->filesTable()->model());
        // ...and out of it, Mini again.
        unstack(f);
        QVERIFY(f.rail()->isVisible());
        QVERIFY(!f.page()->isVisible() && !history->isVisible());
        QVERIFY(diffPane->isVisible());
        QCOMPARE(w->paneLayout(), PaneLayout::Mini);
        QCOMPARE(f.bar()->currentTab(), TopBar::Tab::History);
        QCOMPARE(windowPrefs(), prefs);
    }

    // The first classification comes with the first show, after everything
    // main() applies: the flags pick the tab.
    void theStackedWindowStartsOnItsPreferredTab()
    {
        const QSize narrow(627, 612);
        QVERIFY(narrow.width() < ui::space(700));
        {
            // --mini --screenshot-size 627x612
            WindowFixture f = mainWindow(0, true, [narrow](MainWindow *w) { w->resize(narrow); });
            QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
            settle();
            QVERIFY(f.window->isStacked());
            QCOMPARE(f.bar()->currentTab(), TopBar::Tab::Diff);
            QVERIFY(f.rail()->isVisible());
        }
        {
            // Docked at the same size.
            WindowFixture f = mainWindow(0, false, [narrow](MainWindow *w) { w->resize(narrow); });
            QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
            settle();
            QCOMPARE(f.bar()->currentTab(), TopBar::Tab::Changes);
            QVERIFY(f.page()->isVisible());
        }
        {
            // --history
            WindowFixture f = mainWindow(0, false, [narrow](MainWindow *w) {
                w->setMode(MainWindow::HistoryMode);
                w->resize(narrow);
            });
            QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
            settle();
            QCOMPARE(f.bar()->currentTab(), TopBar::Tab::History);
            QVERIFY(f.window->findChild<HistoryView *>()->isVisible());
        }
        QByteArray geometry;
        {
            // --full: the hidden diff is still the preference once it widens.
            WindowFixture f = mainWindow(0, false, [narrow](MainWindow *w) {
                w->setDiffPaneVisible(false, false);
                w->resize(narrow);
            });
            QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
            settle();
            QCOMPARE(f.bar()->currentTab(), TopBar::Tab::Changes);
            geometry = f.window->saveGeometry();
            unstack(f);
            QVERIFY(!f.window->diffPaneVisible());
            QVERIFY(!f.window->findChild<DiffPane *>()->isVisible());
            QVERIFY(f.page()->isVisible());
        }
        {
            // A narrow geometry restored with a saved Mini layout.
            QSettings().setValue(settings::kWindowGeometry, geometry);
            QSettings().setValue(settings::kWindowLayout, QStringLiteral("mini"));
            WindowFixture f = mainWindow(0, false, [&geometry](MainWindow *w) { w->restoreGeometry(geometry); });
            QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
            settle();
            QVERIFY(f.window->width() < ui::space(700));
            QVERIFY(f.window->isStacked());
            QCOMPARE(f.bar()->currentTab(), TopBar::Tab::Diff);
            QVERIFY(f.rail()->isVisible());
        }
        QSettings().remove(settings::kWindowGeometry);
        QSettings().setValue(settings::kWindowLayout, QStringLiteral("docked"));
    }

    // The tabs, Ctrl+1 / Ctrl+2 and the two layout keys move between the
    // presentations; only a real change of mode reads anything again.
    void theStackedTabsAndKeysMoveBetweenPresentations()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(activate(f.window.get()));
        settle();
        MainWindow *w = f.window.get();
        TopBar *bar = f.bar();
        auto *history = w->findChild<HistoryView *>();
        stack(f);
        const QStringList prefs = windowPrefs();

        QTest::mouseClick(bar->diffTab(), Qt::LeftButton);
        settle();
        QVERIFY(w->diffTab());
        QCOMPARE(w->mode(), MainWindow::CommitMode);
        QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget *>(f.rail()->list()));
        QTest::mouseClick(bar->historyTab(), Qt::LeftButton);
        settle();
        QCOMPARE(w->mode(), MainWindow::HistoryMode);
        QVERIFY(!w->diffTab());
        QVERIFY(history->isVisible());
        QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget *>(history->filesTable()));
        QTest::keyClick(w, Qt::Key_1, Qt::ControlModifier);
        settle();
        QCOMPARE(w->mode(), MainWindow::CommitMode);
        QCOMPARE(bar->currentTab(), TopBar::Tab::Changes);
        QTest::keyClick(w, Qt::Key_2, Qt::ControlModifier);
        settle();
        QCOMPARE(bar->currentTab(), TopBar::Tab::History);
        QTest::keyClick(w, Qt::Key_1, Qt::ControlModifier);
        settle();
        QCOMPARE(bar->currentTab(), TopBar::Tab::Changes);

        // Both layout keys are the Diff tab here, and save nothing.
        for (const auto modifiers : {Qt::KeyboardModifiers(Qt::ControlModifier), Qt::ControlModifier | Qt::ShiftModifier}) {
            QTest::keyClick(w, Qt::Key_B, modifiers);
            settle();
            QCOMPARE(bar->currentTab(), TopBar::Tab::Diff);
            QVERIFY(f.rail()->isVisible());
            QTest::keyClick(w, Qt::Key_B, modifiers);
            settle();
            QCOMPARE(bar->currentTab(), TopBar::Tab::Changes);
            QTRY_VERIFY(QApplication::focusWidget() && QApplication::focusWidget()->isVisible());
        }
        QCOMPARE(w->paneLayout(), PaneLayout::Docked);
        QVERIFY(w->diffPaneVisible());
        QCOMPARE(windowPrefs(), prefs);

        // The tab of the mode already on reads nothing: the diff on screen is
        // the one read before the file changed. A real change of mode reads it.
        const auto diffText = [&f] {
            QStringList lines;
            for (const DiffLine &l : f.diff()->document().lines)
                lines << l.text;
            return lines.join(QLatin1Char('\n'));
        };
        QVERIFY(diffText().contains(QLatin1String("a changed")));
        QVERIFY(writeFixture(QDir(f.dir->path()).filePath(QStringLiteral("a.txt")), "a changed again\n"));
        QTest::mouseClick(bar->changesTab(), Qt::LeftButton);
        QTest::keyClick(w, Qt::Key_1, Qt::ControlModifier);
        QVERIFY(!diffText().contains(QLatin1String("again")));
        QTest::keyClick(w, Qt::Key_2, Qt::ControlModifier);
        QTest::keyClick(w, Qt::Key_1, Qt::ControlModifier);
        QVERIFY(diffText().contains(QLatin1String("again")));

        // Outside the stacked width there is no Diff tab to show.
        unstack(f);
        w->setDiffTab(true);
        QVERIFY(!w->diffTab());
        QVERIFY(!f.rail()->isVisible());

        // A double-click on a file shows its diff: the Diff tab, from the
        // changes list and from the history's files. (Taken at the signal:
        // the synthetic events QtTest sends never reach an item view's own
        // double-click handling.)
        stack(f);
        QTableView *table = f.page()->table();
        QVERIFY(table->isVisible());
        QMetaObject::invokeMethod(table, "doubleClicked", Q_ARG(QModelIndex, table->model()->index(0, ChangesModel::Name)));
        settle();
        QVERIFY(w->diffTab());
        QCOMPARE(w->mode(), MainWindow::CommitMode);
        QTest::mouseClick(bar->historyTab(), Qt::LeftButton);
        settle();
        QTableView *files = history->filesTable();
        QVERIFY(files->isVisible());
        QVERIFY(files->model()->rowCount() > 0);
        files->selectRow(0);
        QMetaObject::invokeMethod(files, "doubleClicked", Q_ARG(QModelIndex, files->model()->index(0, 1)));
        settle();
        QVERIFY(w->diffTab());
        QCOMPARE(w->mode(), MainWindow::HistoryMode);
        QCOMPARE(bar->currentTab(), TopBar::Tab::Diff);
        QCOMPARE(windowPrefs(), prefs);
    }

    // The Diff tab has the rail's commit tile and so the commit card; the
    // Changes tab commits from the page; the history has no card.
    void theStackedDiffTabCarriesTheCommitCard()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(activate(f.window.get()));
        settle();
        MainWindow *w = f.window.get();
        CommitPopover *card = f.popover();
        AgentPopover *agent = f.agentCard();
        stack(f);

        // Changes: Ctrl+Return presses the page's Commit, no card.
        const QString head = f.head();
        MessageEdit *editor = f.pageEditor();
        editor->setPlainText(QStringLiteral("Stacked commit"));
        editor->setFocus();
        QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget *>(editor));
        QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier);
        settle();
        QVERIFY(!card->isVisible());
        QVERIFY(f.head() != head);

        // Diff: the tile and Ctrl+Return open the card.
        QTest::mouseClick(f.bar()->diffTab(), Qt::LeftButton);
        settle();
        QVERIFY(f.tile()->isVisible());
        QTest::mouseClick(f.tile(), Qt::LeftButton);
        settle();
        QVERIFY(card->isVisible());
        QTest::mouseClick(f.tile(), Qt::LeftButton);
        settle();
        QVERIFY(!card->isVisible());
        QTest::keyClick(w, Qt::Key_Return, Qt::ControlModifier);
        settle();
        QVERIFY(card->isVisible());

        // The agent card hangs from the card's cog there; leaving Diff closes
        // both, and the keyboard lands on something on screen.
        QMetaObject::invokeMethod(w, "showAgentMenu");
        settle();
        QVERIFY(agent->isVisible());
        QCOMPARE(agent->anchor(), static_cast<QWidget *>(card->agentButton()));
        QTest::mouseClick(f.bar()->changesTab(), Qt::LeftButton);
        settle();
        QVERIFY(!card->isVisible());
        QVERIFY(!agent->isVisible());
        QTRY_VERIFY(QApplication::focusWidget() && QApplication::focusWidget()->isVisible());
        QVERIFY(QApplication::focusWidget() != f.rail()->list());
        // On the page, the page's cog.
        QMetaObject::invokeMethod(w, "showAgentMenu");
        settle();
        QVERIFY(agent->isVisible());
        QCOMPARE(agent->anchor(), static_cast<QWidget *>(f.page()->agentButton()));
        QVERIFY(!card->isVisible());
        agent->dismiss();

        // Mini, set without saving, then the slot: the card, narrow or not.
        w->setPaneLayout(PaneLayout::Mini, false);
        QMetaObject::invokeMethod(w, "showCommitPopover");
        settle();
        QVERIFY(card->isVisible());
        QVERIFY(w->diffTab());
        // Widening closes both cards.
        QMetaObject::invokeMethod(w, "showAgentMenu");
        settle();
        QVERIFY(agent->isVisible());
        unstack(f);
        QVERIFY(!card->isVisible());
        QVERIFY(!agent->isVisible());

        // The history has no card, on the Diff tab or anywhere.
        stack(f);
        w->setMode(MainWindow::HistoryMode);
        settle();
        QVERIFY(w->diffTab());
        QMetaObject::invokeMethod(w, "showCommitPopover");
        QTest::keyClick(w, Qt::Key_Return, Qt::ControlModifier);
        settle();
        QVERIFY(!card->isVisible());
        w->setPaneLayout(PaneLayout::Docked, false);
    }

    // The menus --screenshot-menu opens, each only where its button is on
    // screen.
    void theScreenshotSlotsOpenTheStackedMenus()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(activate(f.window.get()));
        settle();
        MainWindow *w = f.window.get();
        // The slot runs the menu's own event loop; a timer inside it notes the
        // popup and closes it. A slot that opens nothing returns at once.
        const auto opens = [w](const char *slot) -> QMenu * {
            QMenu *seen = nullptr;
            QTimer::singleShot(150, w, [&seen] {
                seen = qobject_cast<QMenu *>(QApplication::activePopupWidget());
                if (seen)
                    seen->close();
            });
            QMetaObject::invokeMethod(w, slot);
            QTest::qWait(250);
            return seen;
        };

        // Wide: no dropdown, no More at level 0, no options.
        QCOMPARE(f.bar()->foldLevel(), 0);
        QVERIFY(!opens("showSyncMenu"));
        QVERIFY(!opens("showMoreMenu"));
        QVERIFY(!opens("showOptionsMenu"));

        stack(f);
        QCOMPARE(opens("showSyncMenu"), f.bar()->syncDropdown()->menu());
        QCOMPARE(opens("showMoreMenu"), f.bar()->moreButton()->menu());
        QCOMPARE(opens("showOptionsMenu"), f.page()->optionsButton()->menu());
        // The page behind the history has no options on screen.
        QTest::mouseClick(f.bar()->historyTab(), Qt::LeftButton);
        settle();
        QVERIFY(!opens("showOptionsMenu"));
        QCOMPARE(opens("showSyncMenu"), f.bar()->syncDropdown()->menu());
    }

    // A menu that would run off the window opens where it stays inside it:
    // the options menu above the action bar at the bottom, the sync menu
    // pulled left of the window's right edge.
    void theStackedMenusStayInsideTheWindow()
    {
        WindowFixture f = mainWindow(0, false, [](MainWindow *w) { w->resize(627, 612); });
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        settle();
        QVERIFY(f.window->isStacked());
        const QRect window(f.window->mapToGlobal(QPoint(0, 0)), f.window->size());
        const auto global = [](const QWidget *w) { return QRect(w->mapToGlobal(QPoint(0, 0)), w->size()); };

        QToolButton *options = f.page()->optionsButton();
        QVERIFY(options->isVisible());
        QMenu *menu = options->menu();
        menu->popup(options->mapToGlobal(QPoint(0, options->height())));
        QTRY_VERIFY(menu->isVisible());
        QCOMPARE(QApplication::activePopupWidget(), menu);
        QVERIFY2(menu->geometry().bottom() < global(options).top(),
                 qPrintable(QStringLiteral("menu %1..%2, button top %3")
                                .arg(menu->geometry().top()).arg(menu->geometry().bottom())
                                .arg(global(options).top())));
        QVERIFY(menu->geometry().left() >= window.left());
        menu->close();
        QTRY_VERIFY(!menu->isVisible());

        QToolButton *sync = f.bar()->syncDropdown();
        QVERIFY(sync->isVisible());
        menu = sync->menu();
        menu->popup(sync->mapToGlobal(QPoint(0, sync->height())));
        QTRY_VERIFY(menu->isVisible());
        QCOMPARE(QApplication::activePopupWidget(), menu);
        QVERIFY2(menu->geometry().right() <= window.right(),
                 qPrintable(QStringLiteral("menu right %1, window right %2")
                                .arg(menu->geometry().right()).arg(window.right())));
        menu->close();
        QTRY_VERIFY(!menu->isVisible());
    }

    // --- The stacked commit page ------------------------------------------------

    // Options at the left, 6 px, and Commit through the rest; Amend goes into
    // the menu. The ordinary row comes back exactly.
    void theStackedActionBarFoldsAmendIntoOptions()
    {
        CommitFixture f = commitFixture();
        QVERIFY(f.page);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        CommitPage *page = f.page.get();
        QPushButton *commit = f.commitButton();
        QCheckBox *amend = f.amend();
        QToolButton *options = page->optionsButton();
        const QRect commitBefore = rectIn(commit, page), amendBefore = rectIn(amend, page);
        const QString amendLabel = amend->text();
        QVERIFY(!options->isVisible());
        QCOMPARE(options->accessibleName(), QStringLiteral("Options"));
        QCOMPARE(options->popupMode(), QToolButton::InstantPopup);
        QVERIFY(qobject_cast<TickMenu *>(options->menu()));
        QCOMPARE(options->text(), ui::icon(ui::kDotsHorizontal, QStringLiteral("…")).trimmed());

        const QString key = QStringLiteral("  ⏎");
        const auto wording = [&](const QString &what, bool stacked) {
            QCOMPARE(commit->text(), ui::icon(ui::kCommit) + what + (stacked ? QString() : key));
            QCOMPARE(commit->accessibleName(), what);
            QCOMPARE(page->commitControls().commitText, ui::icon(ui::kCommit) + what + key);
            QCOMPARE(page->commitControls().commitName, what);
        };
        wording(QStringLiteral("Commit 2 files"), false);

        page->setStacked(true);
        settle();
        QVERIFY(options->isVisible());
        QVERIFY(!amend->isVisible());
        const QRect o = rectIn(options, page), c = rectIn(commit, page);
        QCOMPARE(o.x(), 0);
        QCOMPARE(o.width(), ui::space(28));
        QCOMPARE(c.x() - (o.x() + o.width()), ui::space(6));
        QCOMPARE(c.x() + c.width(), page->width());
        QVERIFY(c.width() > commitBefore.width());
        wording(QStringLiteral("Commit 2 files"), true);
        page->toggleAllChecked(); // 2 of 4 checked: now all four
        wording(QStringLiteral("Commit 4 files"), true);
        page->setAmendChecked(true);
        wording(QStringLiteral("Amend"), true);
        page->setAmendChecked(false);
        MergeState merge;
        merge.inProgress = true;
        merge.source = QStringLiteral("feature");
        page->setMergeState(merge, f.repo->headCommit());
        wording(QStringLiteral("Commit merge"), true);
        page->setMergeState(MergeState(), f.repo->headCommit());
        page->toggleAllChecked(); // all checked: none
        wording(QStringLiteral("Commit"), true);
        page->toggleAllChecked();

        page->setStacked(false);
        settle();
        QVERIFY(!options->isVisible());
        QVERIFY(amend->isVisible());
        wording(QStringLiteral("Commit 4 files"), false);
        page->setMergeState(merge, f.repo->headCommit());
        wording(QStringLiteral("Commit merge"), false);
        page->setMergeState(MergeState(), f.repo->headCommit());
        // The ordinary row, exactly as it was (the count back at two).
        const QStringList paths = f.checkedPaths();
        f.model()->setPathsChecked(QStringList({QStringLiteral("u1.txt"), QStringLiteral("u2.txt")}), false);
        settle();
        wording(QStringLiteral("Commit 2 files"), false);
        QVERIFY(paths.size() == 4);
        QCOMPARE(rectIn(commit, page), commitBefore);
        QCOMPARE(rectIn(amend, page), amendBefore);
        QCOMPARE(amend->text(), amendLabel);
    }

    // The options menu says what the page's controls say at the moment it
    // opens.
    void theOptionsMenuMirrorsThePage()
    {
        const QString checkTip =
            QStringLiteral("Check every file for the commit, or none when all are checked (Ctrl+Shift+Space)");
        {
            // Nothing to commit: nothing to check.
            QTemporaryDir dir;
            QVERIFY(dir.isValid());
            QVERIFY(git(dir.path(), {"init", "-q", "-b", "main"}));
            QVERIFY(commit(dir.path(), QStringLiteral("A"), 1));
            GitRepo repo(dir.path());
            auto page = commitPage(&repo);
            page->setStacked(true);
            const QList<QAction *> actions = filledMenu(page->optionsButton()->menu());
            QCOMPARE(actions.size(), 5);
            QCOMPARE(actions.at(0)->text(), ui::icon(ui::kCheck) + QStringLiteral("Check all"));
            QVERIFY(!actions.at(0)->isEnabled());
            QCOMPARE(actions.at(0)->toolTip(), checkTip);
        }
        CommitFixture f = commitFixture();
        QVERIFY(f.page);
        CommitPage *page = f.page.get();
        page->setStacked(true);
        QMenu *menu = page->optionsButton()->menu();

        // Some checked.
        QList<QAction *> actions = filledMenu(menu);
        QCOMPARE(menuTexts(actions),
                 QStringList({ui::icon(ui::kCheck) + QStringLiteral("Check all"),
                              ui::icon(ui::kEye) + QStringLiteral("Show unversioned files"),
                              ui::icon(ui::kUndo, QStringLiteral("A  ")) + QStringLiteral("Amend last commit"),
                              QStringLiteral("-"),
                              ui::icon(ui::kSparkle) + QStringLiteral("Generate message")}));
        QVERIFY(actions.at(0)->isEnabled());
        QCOMPARE(actions.at(0)->toolTip(), checkTip);
        QVERIFY(actions.at(1)->isCheckable() && actions.at(1)->isChecked());
        QCOMPARE(actions.at(1)->toolTip(), f.eye()->toolTip());
        QVERIFY(actions.at(2)->isCheckable() && !actions.at(2)->isChecked());
        QVERIFY(actions.at(2)->isEnabled());
        QCOMPARE(actions.at(4)->toolTip(), page->commitControls().generateTip);
        QVERIFY(!actions.at(4)->isCheckable());

        // All checked.
        page->toggleAllChecked();
        actions = filledMenu(menu);
        QCOMPARE(actions.at(0)->text(), ui::icon(ui::kCheck) + QStringLiteral("Uncheck all"));

        // The unversioned files hidden.
        f.eye()->click();
        actions = filledMenu(menu);
        QVERIFY(!actions.at(1)->isChecked());
        QCOMPARE(actions.at(0)->text(), ui::icon(ui::kCheck) + QStringLiteral("Uncheck all")); // the two shown

        // A merge in progress: no amending.
        MergeState merge;
        merge.inProgress = true;
        merge.source = QStringLiteral("feature");
        page->setMergeState(merge, f.repo->headCommit());
        actions = filledMenu(menu);
        QVERIFY(!actions.at(2)->isEnabled());
        QCOMPARE(actions.at(2)->toolTip(), QStringLiteral("Not while a merge is in progress"));
        QCOMPARE(actions.at(2)->toolTip(), f.amend()->toolTip());
    }

    // Each entry takes the path its control takes.
    void theOptionsMenuActsThroughThePage()
    {
        QTemporaryDir tools;
        QVERIFY(tools.isValid());
        const QString gitBinary = QStandardPaths::findExecutable(QStringLiteral("git"));
        QVERIFY(QFile::link(gitBinary, QDir(tools.path()).filePath(QStringLiteral("git"))));
        CommitFixture f = commitFixture();
        QVERIFY(f.page);
        CommitPage *page = f.page.get();
        page->setStacked(true);
        QMenu *menu = page->optionsButton()->menu();

        filledMenu(menu).at(0)->trigger();
        QCOMPARE(f.checkedPaths().size(), 4);
        filledMenu(menu).at(0)->trigger();
        QCOMPARE(f.checkedPaths().size(), 0);
        filledMenu(menu).at(0)->trigger();

        filledMenu(menu).at(1)->trigger();
        QVERIFY(!f.eye()->isChecked());
        QCOMPARE(page->proxy()->rowCount(), 2);
        filledMenu(menu).at(1)->trigger();
        QVERIFY(f.eye()->isChecked());

        QSignalSpy amended(page, &CommitPage::amendToggled);
        filledMenu(menu).at(2)->trigger();
        QVERIFY(f.amend()->isChecked());
        QCOMPARE(amended.count(), 1);
        QVERIFY(filledMenu(menu).at(2)->isChecked());
        filledMenu(menu).at(2)->trigger();
        QVERIFY(!f.amend()->isChecked());

        // Generate, and Stop while the agent writes.
        QVERIFY(writeFakeClaude(tools.path()));
        AgentScope scope(tools.path());
        filledMenu(menu).at(4)->trigger();
        QVERIFY(page->commitControls().generating);
        QList<QAction *> actions = filledMenu(menu);
        QCOMPARE(actions.at(4)->text(), ui::icon(ui::kSparkle) + QStringLiteral("Stop generating"));
        QCOMPARE(actions.at(4)->toolTip(), page->commitControls().generateTip);
        actions.at(4)->trigger();
        QVERIFY(!page->commitControls().generating);
        QCOMPARE(filledMenu(menu).at(4)->text(), ui::icon(ui::kSparkle) + QStringLiteral("Generate message"));
    }

    // Nobody has picked a files view: the stacked width lists the files
    // compact, the ordinary one as the table, and none of it is saved. A
    // choice — saved, unreadable or on the command line — stays.
    void theFilesViewFollowsTheWidthUntilChosen()
    {
        QSettings().remove(settings::kWindowFilesView);
        {
            CommitFixture f = commitFixture();
            QVERIFY(f.page);
            CommitPage *page = f.page.get();
            page->table()->selectRow(1);
            const QString current = page->table()->currentIndex().data(ChangesModel::PathRole).toString();
            QSignalSpy rows(page, &CommitPage::currentRowChanged);
            QSignalSpy resets(page->proxy(), &QAbstractItemModel::modelReset);
            page->setStacked(true);
            QCOMPARE(page->filesView(), CommitPage::FilesView::Compact);
            QVERIFY(page->compactButton()->isChecked());
            page->setStacked(false);
            QCOMPARE(page->filesView(), CommitPage::FilesView::Table);
            QVERIFY(!QSettings().contains(settings::kWindowFilesView));
            QCOMPARE(rows.count(), 0);
            QCOMPARE(resets.count(), 0);
            QCOMPARE(page->table()->currentIndex().data(ChangesModel::PathRole).toString(), current);

            // The one already on is no choice at all; another one is, and
            // from then on the width leaves it alone.
            page->setStacked(true);
            page->compactButton()->click();
            QVERIFY(!QSettings().contains(settings::kWindowFilesView));
            page->tableButton()->click();
            QCOMPARE(QSettings().value(settings::kWindowFilesView).toString(), QStringLiteral("table"));
            page->setStacked(false);
            page->setStacked(true);
            QCOMPARE(page->filesView(), CommitPage::FilesView::Table);
        }
        // Saved, even unreadably: left alone and never rewritten.
        QSettings().setValue(settings::kWindowFilesView, QStringLiteral("sideways"));
        {
            CommitFixture f = commitFixture();
            QVERIFY(f.page);
            f.page->setStacked(true);
            QCOMPARE(f.page->filesView(), CommitPage::FilesView::Table);
            QCOMPARE(QSettings().value(settings::kWindowFilesView).toString(), QStringLiteral("sideways"));
        }
        QSettings().remove(settings::kWindowFilesView);
        // --files-view: the run's own, and it saves nothing whatever is clicked.
        {
            CommitFixture f = commitFixture();
            QVERIFY(f.page);
            f.page->setFilesViewOverride(CommitPage::FilesView::Tree);
            f.page->setStacked(true);
            QCOMPARE(f.page->filesView(), CommitPage::FilesView::Tree);
            f.page->setStacked(false);
            QCOMPARE(f.page->filesView(), CommitPage::FilesView::Tree);
            f.page->compactButton()->click();
            QCOMPARE(f.page->filesView(), CommitPage::FilesView::Compact);
            QVERIFY(!QSettings().contains(settings::kWindowFilesView));
        }
    }

    // Stacking, the Diff tab, the page again and unstacking are presentation
    // only: the current file, the models, the list's offset and the place in
    // the diff all stay; so does a refresh on the Diff tab. In History the
    // current commit and the commit list's offset stay too.
    void stackingKeepsTheSelectionTheScrollAndTheDiff_data()
    {
        QTest::addColumn<bool>("inHistory");
        QTest::newRow("commit") << false;
        QTest::newRow("history") << true;
    }

    void stackingKeepsTheSelectionTheScrollAndTheDiff()
    {
        QFETCH(bool, inHistory);
        QSettings().remove(settings::kWindowFilesView);
        WindowFixture f = mainWindow(30);
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        settle();
        MainWindow *w = f.window.get();
        auto *history = w->findChild<HistoryView *>();
        // A long file, committed and then changed all through: both the
        // commit's diff and the working tree's have somewhere to scroll to.
        const QString path = f.dir->path();
        QByteArray before, after;
        for (int i = 0; i < 400; ++i) {
            before += QByteArray("line ") + QByteArray::number(i) + '\n';
            after += QByteArray(i % 3 ? "line " : "changed ") + QByteArray::number(i) + '\n';
        }
        QVERIFY(writeFixture(QDir(path).filePath(QStringLiteral("long.txt")), before));
        if (inHistory) {
            // Enough commits for the commit list to scroll, and the fixture's
            // thirty extra files in the long commit for its files list to.
            for (int i = 0; i < 40; ++i)
                QVERIFY(commit(path, QStringLiteral("filler %1").arg(i), 1));
            QVERIFY(git(path, {"add", "long.txt", "f*.txt"}));
        } else {
            QVERIFY(git(path, {"add", "long.txt"}));
        }
        QVERIFY(git(path, {"commit", "-q", "-m", "long"}, 2));
        QVERIFY(writeFixture(QDir(path).filePath(QStringLiteral("long.txt")), after));
        w->refresh();
        settle();

        QAbstractItemView *list = nullptr;
        QTableView *commits = nullptr;
        if (inHistory) {
            w->setMode(MainWindow::HistoryMode);
            settle();
            for (QTableView *t : history->findChildren<QTableView *>())
                if (t != history->filesTable())
                    commits = t;
            QVERIFY(commits);
            list = history->filesTable();
            // long.txt among the long commit's thirty-one files.
            const int files = list->model()->rowCount();
            QCOMPARE(files, 31);
            for (int row = 0; row < files; ++row) {
                history->filesTable()->selectRow(row);
                Commit c;
                FileChange file;
                if (history->currentFile(&c, &file) && file.path == QLatin1String("long.txt"))
                    break;
            }
        } else {
            QVERIFY(f.page()->selectPath(QStringLiteral("long.txt")));
            list = f.page()->table();
        }
        settle();
        const auto currentPath = [&] {
            if (!inHistory) {
                bool ok = false;
                const FileChange c = f.page()->currentChange(&ok);
                return ok ? c.path : QString();
            }
            Commit c;
            FileChange file;
            return history->currentFile(&c, &file) ? file.path : QString();
        };
        QCOMPARE(currentPath(), QStringLiteral("long.txt"));
        const auto currentHash = [&] {
            bool ok = false;
            const Commit c = history->currentCommit(&ok);
            return ok ? c.hash : QString();
        };
        if (!inHistory) {
            f.page()->setScrollOffset(QPoint(0, 3));
        } else {
            QCOMPARE(currentHash(), f.head());
            // Both lists off their top, by little enough that neither runs
            // out of range whatever the resizes do to the viewports.
            for (QAbstractItemView *view : QList<QAbstractItemView *>{commits, list}) {
                QScrollBar *bar = view->verticalScrollBar();
                QVERIFY(bar->maximum() >= 2);
                bar->setValue(2);
            }
            settle();
        }
        DiffView::ViewState at;
        at.row = 120;
        at.block = 1;
        f.diff()->restoreViewState(at);
        const auto state = [&] {
            const DiffView::ViewState d = f.diff()->viewState();
            QStringList out{currentPath(), QString::number(list->verticalScrollBar()->value()),
                            QStringLiteral("%1,%2,%3").arg(d.row).arg(d.column).arg(d.block)};
            if (inHistory)
                out << currentHash() << QString::number(commits->verticalScrollBar()->value());
            return out;
        };
        const QStringList start = state();
        QVERIFY(start.at(2).startsWith(QLatin1String("120,")));
        QCOMPARE(start.at(1), inHistory ? QStringLiteral("2") : QStringLiteral("3"));
        if (inHistory)
            QCOMPARE(start.at(4), QStringLiteral("2"));
        QSignalSpy listResets(list->model(), &QAbstractItemModel::modelReset);
        QSignalSpy railResets(f.rail()->list()->model(), &QAbstractItemModel::modelReset);
        // The commit list's own model: the files' spies leave it unwatched.
        std::unique_ptr<QSignalSpy> commitResets;
        if (inHistory)
            commitResets.reset(new QSignalSpy(commits->model(), &QAbstractItemModel::modelReset));

        stack(f);
        QCOMPARE(state(), start);
        w->setDiffTab(true);
        settle();
        QCOMPARE(state(), start);
        w->setDiffTab(false);
        settle();
        QCOMPARE(state(), start);
        unstack(f);
        QCOMPARE(state(), start);
        QCOMPARE(listResets.count(), 0);
        QCOMPARE(railResets.count(), 0);
        if (commitResets)
            QCOMPARE(commitResets->count(), 0);

        if (!inHistory) {
            // F5 on the Diff tab: the file, the rail's offset and the place
            // in the diff are put back.
            stack(f);
            w->setDiffTab(true);
            settle();
            QScrollBar *rail = f.rail()->list()->verticalScrollBar();
            QVERIFY(rail->maximum() > 0);
            rail->setValue(rail->maximum() / 2);
            const int railOffset = rail->value();
            QTest::keyClick(w, Qt::Key_F5);
            settle();
            QCOMPARE(state().at(0), start.at(0));
            QCOMPARE(state().at(2), start.at(2));
            QCOMPARE(rail->value(), railOffset);
            QVERIFY(w->diffTab());
        }
    }

    // A live text size moves the stacked row like a fresh one, and moves the
    // stacking width with it: 800 px is narrow at 16 and wide at 12.
    void theStackedPresentationFollowsALiveTextSizeChange()
    {
        QTemporaryDir dir, home, tools;
        QVERIFY(dir.isValid() && home.isValid() && tools.isValid());
        const QString toml = QDir(dir.path()).filePath(QStringLiteral("shell.toml"));
        QVERIFY(writeFixture(toml, "[font]\nbase-size = 12\n"));
        const QString gitBinary = QStandardPaths::findExecutable(QStringLiteral("git"));
        QVERIFY(QFile::link(gitBinary, QDir(tools.path()).filePath(QStringLiteral("git"))));
        {
            ScopedEnv themeDir("OMAGIT_THEME_DIR", dir.path().toUtf8());
            ScopedEnv scratchHome("HOME", home.path().toUtf8());
            ScopedEnv onlyGit("PATH", tools.path().toUtf8());
            OmarchyTheme theme;
            QCOMPARE(theme.fontBase(), 12);
            theme.apply(*qApp);

            BarFixture live = topBar();
            live.bar->setStacked(true);
            QObject::connect(&theme, &OmarchyTheme::changed, live.bar, [bar = live.bar] { bar->applyTheme(); });
            QVERIFY(QTest::qWaitForWindowExposed(live.host.get()));
            settle();

            WindowFixture window = mainWindow(0, false, [](MainWindow *w) { w->resize(800, 800); });
            QVERIFY(window.window);
            QVERIFY(QTest::qWaitForWindowExposed(window.window.get()));
            settle();
            QVERIFY(!window.window->isStacked());
            const QStringList prefs = windowPrefs();

            const auto matchesAFreshBar = [&live] {
                BarFixture fresh = topBar();
                fresh.bar->setStacked(true);
                QVERIFY(QTest::qWaitForWindowExposed(fresh.host.get()));
                settle();
                // Both spelled out, so the segments' hints are the labelled ones.
                live.levelAt(live.bar->sizeHint().width());
                fresh.levelAt(fresh.bar->sizeHint().width());
                QCOMPARE(barMetrics(live.bar), barMetrics(fresh.bar));
                for (const int width : {fresh.bar->sizeHint().width(), fresh.bar->sizeHint().width() - 1,
                                        fresh.bar->minimumSizeHint().width()}) {
                    QCOMPARE(live.levelAt(width), fresh.levelAt(width));
                    QCOMPARE(stackedRow(live), stackedRow(fresh));
                    QCOMPARE(live.rectOf(live.bar->syncDropdown()).width(), ui::space(92));
                }
                live.levelAt(live.bar->sizeHint().width());
            };
            const QStringList atTwelve = barMetrics(live.bar);
            matchesAFreshBar();

            QVERIFY(writeFixture(toml, "[font]\nbase-size = 16\n"));
            QTRY_COMPARE_WITH_TIMEOUT(theme.fontBase(), 16, 10000);
            settle();
            matchesAFreshBar();
            QVERIFY(barMetrics(live.bar) != atTwelve);
            QVERIFY(ui::space(700) > 800);
            QTRY_VERIFY(window.window->isStacked());
            QCOMPARE(window.bar()->currentTab(), TopBar::Tab::Changes);
            QCOMPARE(windowPrefs(), prefs);

            QVERIFY(writeFixture(toml, "[font]\nbase-size = 12\n"));
            QTRY_COMPARE_WITH_TIMEOUT(theme.fontBase(), 12, 10000);
            settle();
            matchesAFreshBar();
            QCOMPARE(barMetrics(live.bar), atTwelve);
            QTRY_VERIFY(!window.window->isStacked());
            QVERIFY(window.window->findChild<DiffPane *>()->isVisible());
            QCOMPARE(windowPrefs(), prefs);

            window.window.reset();
            live.host.reset();
        }
        g_theme.reset(new OmarchyTheme);
        g_theme->apply(*qApp);
        QVERIFY(OmarchyTheme::instance() == g_theme.get());
    }

    // --- MainWindow ---------------------------------------------------------

    // One top bar, above everything, in every layout — and the controls the
    // Mini rail used to double are gone from it.
    void theWindowPutsOneTopBarAboveTheBody()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        settle();

        QCOMPARE(f.window->findChildren<TopBar *>().size(), 1);
        QLayout *root = f.window->centralWidget()->layout();
        QCOMPARE(root->itemAt(0)->widget(), static_cast<QWidget *>(f.bar()));
        QVERIFY(f.bar()->isVisible());

        f.window->setPaneLayout(PaneLayout::Mini, false);
        settle();
        QVERIFY(f.bar()->isVisible());
        QVERIFY(f.bar()->layoutButton()->isChecked());
        // The rail is the miniatures and Refresh; its sync buttons moved out.
        QVERIFY(f.rail()->findChildren<BadgeButton *>().isEmpty());

        f.window->setDiffPaneVisible(false, false);
        settle();
        QVERIFY(f.bar()->isVisible());
        QCOMPARE(f.window->paneLayout(), PaneLayout::Docked); // hiding the diff leaves Mini
        QVERIFY(!f.bar()->diffToggle()->isChecked());
        f.window->setDiffPaneVisible(true, false);
        settle();

        // The tab counts what the changes list shows, and switches the page.
        QVERIFY(f.page()->proxy()->rowCount() > 0);
        QCOMPARE(f.bar()->changesCount(), f.page()->proxy()->rowCount());
        QTest::mouseClick(f.bar()->historyTab(), Qt::LeftButton);
        settle();
        QCOMPARE(f.window->mode(), MainWindow::HistoryMode);
        QVERIFY(!f.page()->isVisible());
        QTest::mouseClick(f.bar()->changesTab(), Qt::LeftButton);
        settle();
        QCOMPARE(f.window->mode(), MainWindow::CommitMode);
        QVERIFY(f.page()->isVisible());
    }

    // The count follows the proxy's own notifications, and the update does
    // nothing else: it writes to no model and leaves the current row alone.
    void theTabCountFollowsTheChangesProxyWithoutWritingToIt()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        settle();
        QSortFilterProxyModel *const proxy = f.page()->proxy();
        QAbstractItemModel *const source = proxy->sourceModel();
        QSignalSpy dataChanged(source, &QAbstractItemModel::dataChanged);
        QSignalSpy layoutChanged(source, &QAbstractItemModel::layoutChanged);
        QSignalSpy modelReset(source, &QAbstractItemModel::modelReset);

        // Two snapshots as close around the callback as the signals allow:
        // the first is the last thing the structural change does before it,
        // the second the first thing after it — so the change's own effects
        // are not laid at the top bar's door.
        struct Snapshot {
            int dataChanged = -1, layoutChanged = -1, reset = -1;
            QString current;
        };
        Snapshot before, after;
        const auto take = [&](Snapshot &s) {
            s.dataChanged = dataChanged.count();
            s.layoutChanged = layoutChanged.count();
            s.reset = modelReset.count();
            s.current = f.page()->table()->currentIndex().data(ChangesModel::PathRole).toString();
        };
        // The eye's filter change reaches the proxy as a layout change; both
        // connections are made after the window's, so `after` is taken once
        // the window's own slot has run.
        QObject::connect(proxy, &QAbstractItemModel::layoutAboutToBeChanged, f.window.get(), [&] { take(before); });
        QObject::connect(proxy, &QAbstractItemModel::layoutChanged, f.window.get(), [&] { take(after); });

        // The eye hides the unversioned files: rows leave the proxy without
        // the source model hearing a thing about it.
        QToolButton *eye = nullptr;
        for (QToolButton *b : f.page()->findChildren<QToolButton *>(QStringLiteral("iconButton")))
            if (b->isCheckable())
                eye = b;
        QVERIFY(eye);
        const int listed = proxy->rowCount();
        f.page()->selectPath(QStringLiteral("a.txt")); // a versioned row: the eye leaves it listed
        settle();
        eye->click();
        settle();
        QVERIFY(proxy->rowCount() < listed);
        QCOMPARE(f.bar()->changesCount(), proxy->rowCount());
        QCOMPARE(after.dataChanged, before.dataChanged);
        QCOMPARE(after.layoutChanged, before.layoutChanged);
        QCOMPARE(after.reset, before.reset);
        QCOMPARE(after.current, before.current);
        QVERIFY(!after.current.isEmpty());

        // And back: the rows come again, and so does the count.
        eye->click();
        settle();
        QCOMPARE(f.bar()->changesCount(), listed);
        QCOMPARE(f.bar()->changesCount(), proxy->rowCount());

        // A whole reload of the list is a reset, and the count follows that too.
        writeFixture(QDir(f.repo->root()).filePath(QStringLiteral("c.txt")), "c\n");
        f.page()->reload();
        settle();
        QCOMPARE(f.bar()->changesCount(), proxy->rowCount());
        QCOMPARE(f.bar()->changesCount(), listed + 1);
    }

    // --- The Mini layout's commit tile and popover ---------------------------

    // The tile, its separator and its gaps are the commit view's: gone in the
    // history, and with the whole rail in the Docked layout.
    void theCommitTileShowsInTheMiniCommitViewOnly()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        f.window->setPaneLayout(PaneLayout::Mini, false);
        settle();
        QToolButton *tile = f.tile();
        QWidget *section = tile->parentWidget();
        QVERIFY(tile->isVisible());
        QCOMPARE(tile->objectName(), QStringLiteral("commitTile"));
        QCOMPARE(tile->accessibleName(), QStringLiteral("Commit"));
        QCOMPARE(tile->toolTip(), QStringLiteral("Commit the checked files (Ctrl+Enter)"));
        QCOMPARE(tile->focusPolicy(), Qt::NoFocus);
        QCOMPARE(tile->cursor().shape(), Qt::PointingHandCursor);
        // The tile is the rail's last thing, its bottom the rail's.
        QCOMPARE(rectIn(tile, f.rail()).bottom(), f.rail()->height() - 1);

        f.window->setMode(MainWindow::HistoryMode);
        settle();
        QVERIFY(!tile->isVisible());
        QVERIFY(!section->isVisible()); // the separator and both gaps with it
        // Nothing of it is left under Refresh, not even the rail's spacing.
        QCOMPARE(rectIn(f.railRefresh(), f.rail()).bottom(), f.rail()->height() - 1);

        f.window->setMode(MainWindow::CommitMode);
        settle();
        QVERIFY(tile->isVisible());

        f.window->setPaneLayout(PaneLayout::Docked, false);
        settle();
        QVERIFY(!f.rail()->isVisible());
        QVERIFY(!tile->isVisible());
    }

    // One widget, the rail's width and a badge's rise taller than its square:
    // the square at its bottom and centred, the design's 8 px under the
    // separator, which is 8 px under Refresh; the badge inside the widget.
    void theCommitTileKeepsItsSquareAndBadgeInside()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        f.window->setPaneLayout(PaneLayout::Mini, false);
        settle();
        MiniRail *rail = f.rail();
        QToolButton *tile = f.tile();
        QCOMPARE(rail->width(), MiniRail::railWidth());
        QCOMPARE(MiniRail::railWidth(), ui::space(MiniRail::kWidth));
        QCOMPARE(tile->size(), QSize(MiniRail::railWidth(), ui::space(40) + kTileBadgeRise));
        QCOMPARE(rectIn(tile, rail).x(), 0);

        const QRect square = tileSquare(tile).translated(rectIn(tile, rail).topLeft());
        QCOMPARE(square.bottom(), rail->height() - 1); // bottom-aligned
        // Centred: the room on the left is the room on the right, give or take
        // the odd pixel.
        QVERIFY(qAbs(square.left() - (rail->width() - 1 - square.right())) <= 1);

        QWidget *rule = tileRule(f);
        QVERIFY(rule);
        const QRect ruleRect = rectIn(rule, rail);
        QCOMPARE(square.top() - (ruleRect.bottom() + 1), ui::space(8));
        QCOMPARE(ruleRect.top() - (rectIn(f.railRefresh(), rail).bottom() + 1), ui::space(8));
        QVERIFY(tile->rect().contains(tileBadge(tile)));

        // A long hash is elided to the rail's scaled width.
        f.window->setMode(MainWindow::HistoryMode);
        rail->setCommitLabel(QStringLiteral("0123456789abcdef0123456789abcdef"), QStringLiteral("subject"));
        settle();
        QLabel *hash = rail->findChild<QLabel *>(QStringLiteral("dimLabel"));
        QVERIFY(hash);
        QVERIFY(hash->fontMetrics().horizontalAdvance(hash->text()) <= MiniRail::railWidth() - 2);
        QVERIFY(hash->text().endsWith(QStringLiteral("…")));
    }

    // Check-all, Ctrl+click, Space, the eye, a reload, sorting and another
    // source: the badge counts what the page counts, whatever changed it.
    void theCommitTileBadgeCountsTheCheckedFiles()
    {
        WindowFixture f = mainWindow(10); // a.txt, u1.txt and ten more: twelve files
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        f.window->setPaneLayout(PaneLayout::Mini, false);
        settle();
        MiniRail *rail = f.rail();
        CommitPage *page = f.page();
        const auto agrees = [&] { return rail->checkedCount() == page->commitControls().checked; };
        QCOMPARE(page->proxy()->rowCount(), 12);
        QCOMPARE(rail->checkedCount(), 1); // the modified file
        QVERIFY(agrees());

        // Check-all through the window's own shortcut: two digits, and the
        // page's button says the same number.
        QTest::keyClick(rail->list(), Qt::Key_Space, Qt::ControlModifier | Qt::ShiftModifier);
        settle();
        QCOMPARE(rail->checkedCount(), 12);
        QVERIFY(agrees());
        QCOMPARE(page->commitControls().commitName, QStringLiteral("Commit 12 files"));
        QTest::keyClick(rail->list(), Qt::Key_Space, Qt::ControlModifier | Qt::ShiftModifier);
        settle();
        QCOMPARE(rail->checkedCount(), 0);
        QVERIFY(agrees());

        // Ctrl+click one miniature, Space another.
        QListView *list = rail->list();
        const QModelIndex first = list->model()->index(0, ChangesModel::Check);
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::ControlModifier, list->visualRect(first).center());
        settle();
        QCOMPARE(rail->checkedCount(), 1);
        QVERIFY(agrees());
        list->setCurrentIndex(list->model()->index(1, ChangesModel::Check));
        QTest::keyClick(list, Qt::Key_Space);
        settle();
        QCOMPARE(rail->checkedCount(), 2);
        QVERIFY(agrees());

        // The eye hides the unversioned files and unticks them; showing them
        // again leaves them unticked.
        page->unversionedButton()->click();
        settle();
        QCOMPARE(rail->checkedCount(), 1);
        QVERIFY(agrees());
        page->unversionedButton()->click();
        settle();
        QCOMPARE(rail->checkedCount(), 1);
        QVERIFY(agrees());

        // A reload with a new file: the ticks stay, the newcomer is not ticked.
        page->toggleAllChecked();
        settle();
        QCOMPARE(rail->checkedCount(), 12);
        QVERIFY(writeFixture(QDir(f.repo->root()).filePath(QStringLiteral("zz.txt")), "zz\n"));
        page->reload();
        settle();
        QCOMPARE(page->proxy()->rowCount(), 13);
        QCOMPARE(rail->checkedCount(), 12);
        QVERIFY(agrees());

        // Sorting is a layout change; the count stays what it is.
        page->proxy()->sort(ChangesModel::Name, Qt::DescendingOrder);
        settle();
        QCOMPARE(rail->checkedCount(), 12);
        QVERIFY(agrees());

        // The history's files are another source, with no check marks.
        f.window->setMode(MainWindow::HistoryMode);
        settle();
        QCOMPARE(rail->checkedCount(), 0);
        f.window->setMode(MainWindow::CommitMode);
        settle();
        QCOMPARE(rail->checkedCount(), 12);
        QVERIFY(agrees());
    }

    // The badge listens to the five notifications that can change a count,
    // once each however often the same source is set, and to the source of
    // the moment only.
    void theCommitTileBadgeFollowsItsSourceAlone()
    {
        MiniRail rail;
        CountedModel model;
        model.appendRow(checkRow(QStringLiteral("a"), true));
        model.appendRow(checkRow(QStringLiteral("b"), false));
        QItemSelectionModel selection(&model);
        const int dataBefore = model.dataReceivers();
        const int layoutBefore = model.layoutReceivers();

        rail.setSource(&model, &selection);
        QCOMPARE(rail.checkedCount(), 1);
        const int dataWith = model.dataReceivers();
        const int layoutWith = model.layoutReceivers();
        QVERIFY(dataWith > dataBefore);
        rail.setSource(&model, &selection);
        QCOMPARE(model.dataReceivers(), dataWith);
        QCOMPARE(model.layoutReceivers(), layoutWith);

        model.item(1)->setCheckState(Qt::Checked); // dataChanged
        QCOMPARE(rail.checkedCount(), 2);
        model.appendRow(checkRow(QStringLiteral("c"), true)); // rowsInserted
        QCOMPARE(rail.checkedCount(), 3);
        model.removeRow(0); // rowsRemoved
        QCOMPARE(rail.checkedCount(), 2);
        {
            // A tick nobody announced, then a layout change: the count is read again.
            QSignalBlocker quiet(&model);
            model.item(0)->setCheckState(Qt::Unchecked);
        }
        QCOMPARE(rail.checkedCount(), 2);
        emit model.layoutChanged();
        QCOMPARE(rail.checkedCount(), 1);
        model.clear(); // modelReset
        QCOMPARE(rail.checkedCount(), 0);

        // Another source: the old one's notifications no longer reach the badge.
        model.appendRow(checkRow(QStringLiteral("d"), true));
        QCOMPARE(rail.checkedCount(), 1);
        CountedModel other;
        other.appendRow(checkRow(QStringLiteral("x"), true));
        other.appendRow(checkRow(QStringLiteral("y"), true));
        QItemSelectionModel otherSelection(&other);
        rail.setSource(&other, &otherSelection);
        QCOMPARE(rail.checkedCount(), 2);
        // The badge's connections went with the old source. (The list view
        // keeps a layout connection of Qt's own there; it is not the badge's,
        // which the notifications below prove.)
        QCOMPARE(model.dataReceivers(), dataBefore);
        QVERIFY(model.layoutReceivers() < layoutWith);
        QVERIFY(model.layoutReceivers() >= layoutBefore);
        model.appendRow(checkRow(QStringLiteral("e"), true));
        QCOMPARE(rail.checkedCount(), 2);
        model.item(0)->setCheckState(Qt::Unchecked);
        emit model.layoutChanged();
        model.clear();
        QCOMPARE(rail.checkedCount(), 2);
    }

    // One digit fits the miniatures' circle; two and three widen it into a
    // pill that keeps the circle's right edge and grows over the tile, as
    // painted and as the rail reports it.
    void theCommitTileBadgeWidensIntoAPillForLongerCounts()
    {
        MiniRail rail;
        QStandardItemModel model;
        QItemSelectionModel selection(&model);
        rail.setSource(&model, &selection);
        QToolButton *tile = rail.commitTile();
        QCOMPARE(rail.commitBadgeRect(), QRect()); // nothing checked, no badge

        const QRect circle = tileBadge(tile);
        const QColor accent = OmarchyTheme::instance()->accent();
        // The painted badge's run of accent pixels one row under its top, which
        // no digit reaches: [first, last].
        const auto paintedRun = [&] {
            const QImage image = tile->grab().toImage();
            const int y = circle.top() + 1;
            int first = -1, last = -1;
            for (int x = 0; x < image.width(); ++x)
                if (closeTo(image.pixelColor(x, y), accent)) {
                    if (first < 0)
                        first = x;
                    last = x;
                }
            return std::make_pair(first, last);
        };
        const auto checkRows = [&](int count) {
            while (model.rowCount() < count)
                model.appendRow(checkRow(QStringLiteral("f%1").arg(model.rowCount()), true));
            QCOMPARE(rail.checkedCount(), count);
        };

        checkRows(1);
        QCOMPARE(rail.commitBadgeRect(), circle);
        const auto one = paintedRun();
        QVERIFY(one.first >= 0);
        QVERIFY(circle.contains(QPoint(one.first, circle.top() + 1)));

        auto previous = one;
        for (const int count : {12, 123}) {
            checkRows(count);
            const QRect badge = rail.commitBadgeRect();
            QVERIFY2(badge.width() > kTileBadgeSize, qPrintable(QString::number(badge.width())));
            QCOMPARE(badge.height(), kTileBadgeSize);
            QCOMPARE(badge.right(), circle.right());
            QCOMPARE(badge.top(), circle.top());
            QVERIFY(tile->rect().contains(badge));
            const auto run = paintedRun();
            QCOMPARE(run.second, one.second); // the right edge stays put...
            QVERIFY(run.first < previous.first); // ...and the pill grows leftwards
            QVERIFY(badge.contains(QPoint(run.first, badge.top() + 1)));
            previous = run;
        }
    }

    // Ctrl+click ticks the clicked miniature and hands the list the keyboard,
    // leaving the current file where it was; Space goes on from there.
    void aCtrlClickOnTheRailFocusesItsList()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        f.window->setPaneLayout(PaneLayout::Mini, false);
        settle();
        QVERIFY(activate(f.window.get()));
        QListView *list = f.rail()->list();
        QAbstractItemModel *model = list->model();
        list->setCurrentIndex(model->index(0, ChangesModel::Check));
        f.diff()->setFocus();
        QTRY_VERIFY(f.diff()->hasFocus());
        const QModelIndex second = model->index(1, ChangesModel::Check);
        const int before = second.data(Qt::CheckStateRole).toInt();
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::ControlModifier, list->visualRect(second).center());
        settle();
        QTRY_VERIFY(list->hasFocus());
        QCOMPARE(list->currentIndex().row(), 0);
        QVERIFY(second.data(Qt::CheckStateRole).toInt() != before);

        const QModelIndex current = model->index(0, ChangesModel::Check);
        const int currentBefore = current.data(Qt::CheckStateRole).toInt();
        QTest::keyClick(list, Qt::Key_Space);
        settle();
        QVERIFY(current.data(Qt::CheckStateRole).toInt() != currentBefore);
        QVERIFY(second.data(Qt::CheckStateRole).toInt() != before); // the other one stays ticked
    }

    // The tile, Ctrl+Return, the keypad's Ctrl+Enter and the slot by name all
    // open the card with the keyboard in its message box and the tile lit;
    // the slot a second time changes nothing but the focus.
    void theCommitPopoverOpensFromTheTileTheKeysAndItsSlot()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        f.window->setPaneLayout(PaneLayout::Mini, false);
        settle();
        QVERIFY(activate(f.window.get()));
        CommitPopover *card = f.popover();
        QVERIFY(card);
        QVERIFY(!card->isVisible());
        QCOMPARE(card->parentWidget(), f.host());
        QVERIFY(!card->isWindow());

        const OmarchyTheme *theme = OmarchyTheme::instance();
        const auto tileFill = [&f] {
            const QImage image = f.rail()->grab().toImage();
            const QRect square = tileSquare(f.tile()).translated(rectIn(f.tile(), f.rail()).topLeft());
            return image.pixelColor(square.left() + 4, square.bottom() - 4);
        };
        const QColor resting = over(theme->window(), theme->accent(), 0.08);
        const QColor lit = over(theme->window(), theme->accent(), 0.18);
        QVERIFY2(closeTo(tileFill(), resting), qPrintable(tileFill().name()));

        const auto isOpen = [&] {
            return card->isVisible() && f.window->focusWidget() == card->editor();
        };

        // The keys, from the rail.
        QTest::keyClick(f.rail()->list(), Qt::Key_Return, Qt::ControlModifier);
        settle();
        QTRY_VERIFY(isOpen());
        QVERIFY2(closeTo(tileFill(), lit), qPrintable(tileFill().name()));
        card->dismiss();
        settle();
        QVERIFY(!card->isVisible());
        QVERIFY2(closeTo(tileFill(), resting), qPrintable(tileFill().name()));

        QTest::keyClick(f.rail()->list(), Qt::Key_Enter, Qt::ControlModifier | Qt::KeypadModifier);
        settle();
        QTRY_VERIFY(isOpen());
        card->dismiss();
        settle();

        // The tile.
        QTest::mouseClick(f.tile(), Qt::LeftButton);
        settle();
        QTRY_VERIFY(isOpen());
        card->dismiss();
        settle();

        // The slot, by name as main() calls it, twice.
        QVERIFY(QMetaObject::invokeMethod(f.window.get(), "showCommitPopover"));
        settle();
        QTRY_VERIFY(isOpen());
        const QRect geometry = card->geometry();
        f.rail()->list()->setFocus();
        QVERIFY(QMetaObject::invokeMethod(f.window.get(), "showCommitPopover"));
        settle();
        QTRY_VERIFY(isOpen());
        QCOMPARE(card->geometry(), geometry);
        QCOMPARE(f.window->findChildren<CommitPopover *>().size(), 1);
    }

    // Beside the rail, 8 px clear of it, as wide as the design allows or the
    // window's right margin leaves; its bottom on the tile's bottom however
    // the window is resized or the text grows.
    void theCommitPopoverHangsBesideTheTile()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(f.openCard());
        CommitPopover *card = f.popover();
        QWidget *host = f.host();
        const QMargins hostMargins = host->layout()->contentsMargins();
        const auto anchored = [&] {
            const QRect cardRect = card->geometry();
            const QRect tileRect = rectIn(f.tile(), host);
            const QRect rail = f.rail()->geometry();
            const int left = rail.x() + rail.width() + ui::space(8);
            return cardRect.x() == left && cardRect.y() + cardRect.height() == tileRect.y() + tileRect.height()
                && cardRect.width() == qMin(ui::space(360), host->width() - hostMargins.right() - left);
        };
        QVERIFY(anchored());
        QCOMPARE(card->width(), ui::space(360));
        // The frame's coordinates: the message box's top and left edges.
        QCOMPARE(rectIn(card->editor(), card).topLeft(), QPoint(ui::space(10), ui::space(32)));
        QCOMPARE(card->editor()->height(), ui::space(84));

        // A narrow window clamps the width; the hint is elided rather than
        // making the card wider. The stacked top bar keeps the window wider
        // than that; a minimum of the test's own lets it be squeezed anyway
        // (the Mini layout is the stacked Diff tab there, rail and card kept).
        f.window->setMinimumSize(1, 1);
        f.window->resize(420, 800);
        settle();
        QTRY_VERIFY(anchored());
        QVERIFY(card->width() < ui::space(360));
        QVERIFY(card->hintLabel()->text() != card->hintText());
        QVERIFY(card->hintLabel()->text().endsWith(QStringLiteral("…")));
        QVERIFY(card->commitButton()->geometry().right() < card->width());
        // Leaving the stacked width closes the card; the Mini layout opens it
        // again.
        f.window->resize(1200, 640);
        settle();
        QVERIFY(!card->isVisible());
        QMetaObject::invokeMethod(f.window.get(), "showCommitPopover");
        settle();
        QVERIFY(card->isVisible());
        QTRY_VERIFY(anchored());
        // Elided to the label's width again, however much of it fits.
        QCOMPARE(card->hintLabel()->text(),
                 card->hintLabel()->fontMetrics().elidedText(card->hintText(), Qt::ElideRight,
                                                             card->hintLabel()->width()));

        // Growth moves the top up and leaves the bottom where it is.
        const QRect before = card->geometry();
        for (int line = 0; line < 8; ++line) {
            QTest::keyClicks(card->editor(), QStringLiteral("line %1").arg(line));
            QTest::keyClick(card->editor(), Qt::Key_Return);
        }
        settle();
        QTRY_VERIFY(card->editor()->height() > ui::space(84));
        QTRY_VERIFY(anchored());
        QVERIFY(card->y() < before.y());
        QCOMPARE(card->geometry().bottom(), before.bottom());
        // ...up to a third of the window.
        for (int line = 0; line < 30; ++line)
            QTest::keyClick(card->editor(), Qt::Key_Return);
        settle();
        QTRY_COMPARE(card->editor()->height(), qMax(ui::space(84), host->height() / 3));
        QTRY_VERIFY(anchored());
    }

    // One document for both boxes: typed text, the agent's streamed text and
    // the undo stack are the same wherever they are looked at.
    void theCommitPopoverSharesThePagesMessage()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(f.openCard());
        MessageEdit *card = f.popover()->editor();
        MessageEdit *page = f.pageEditor();
        QVERIFY(card != page);
        QCOMPARE(card->document(), f.page()->messageDocument());
        QCOMPARE(page->document(), f.page()->messageDocument());

        QTest::keyClicks(card, QStringLiteral("Typed"));
        QCOMPARE(page->toPlainText(), QStringLiteral("Typed"));
        page->replaceText(QStringLiteral("Streamed answer"));
        QCOMPARE(card->toPlainText(), QStringLiteral("Streamed answer"));
        card->undo();
        QCOMPARE(page->toPlainText(), QStringLiteral("Typed"));
        page->redo();
        QCOMPARE(card->toPlainText(), QStringLiteral("Streamed answer"));
    }

    // The document wraps for the box on screen: the page's in the Docked
    // layout, the card's in the Mini one, each as a box built fresh at its
    // width would — also after the hidden one's text was replaced, after a
    // theme change and after coming back.
    void theSharedMessageWrapsForTheBoxOnScreen()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        MessageEdit *page = f.pageEditor();
        const QString text = longParagraph() + QLatin1Char(' ') + longParagraph();
        page->replaceText(text);
        settle();
        QCOMPARE(page->contentHeight(), freshContentHeight(page, text));
        const int docked = page->contentHeight();

        QVERIFY(f.openCard());
        MessageEdit *card = f.popover()->editor();
        QVERIFY(card->width() < page->width());
        settle();
        QTRY_COMPARE(card->contentHeight(), freshContentHeight(card, text));
        QVERIFY(card->contentHeight() > docked); // narrower, so more lines

        // The hidden box's edit, and a theme change that reaches both boxes.
        const QString longer = text + QLatin1Char(' ') + longParagraph();
        page->replaceText(longer);
        settle();
        QTRY_COMPARE(card->contentHeight(), freshContentHeight(card, longer));
        emit OmarchyTheme::instance()->changed();
        settle();
        QTRY_COMPARE(card->contentHeight(), freshContentHeight(card, longer));

        f.window->setPaneLayout(PaneLayout::Docked, false);
        settle();
        QTRY_COMPARE(page->contentHeight(), freshContentHeight(page, longer));
        page->replaceText(text);
        settle();
        QTRY_COMPARE(page->contentHeight(), docked);
    }

    // The card says what the page's controls say: the Commit button's
    // wording, name, tip and state, the amend box both ways, the hint's
    // counts, and a merge ruling amending out.
    void theCommitPopoverMirrorsThePagesControls()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(activate(f.window.get()));
        QVERIFY(f.openCard());
        CommitPopover *card = f.popover();
        CommitPage *page = f.page();
        const auto mirrors = [&] {
            const CommitPage::CommitControls c = page->commitControls();
            QPushButton *commit = card->commitButton();
            QCheckBox *amend = card->amendBox();
            QToolButton *generate = card->editor()->cornerButton();
            return commit->text() == c.commitText && commit->accessibleName() == c.commitName
                && commit->toolTip() == c.commitTip && commit->isEnabled() == c.commitEnabled
                && amend->isChecked() == c.amendChecked && amend->isEnabled() == c.amendEnabled
                && amend->toolTip() == c.amendTip && generate->text() == c.generateText
                && generate->toolTip() == c.generateTip;
        };
        const QString space = QStringLiteral(" · Space on a tile toggles it");
        QVERIFY(mirrors());
        QCOMPARE(card->commitButton()->accessibleName(), QStringLiteral("Commit 1 file"));
        QCOMPARE(card->hintText(), QStringLiteral("1 / 2 files selected") + space);

        page->toggleAllChecked();
        settle();
        QVERIFY(mirrors());
        QCOMPARE(card->commitButton()->accessibleName(), QStringLiteral("Commit 2 files"));
        QCOMPARE(card->hintText(), QStringLiteral("2 / 2 files selected") + space);
        page->toggleAllChecked();
        settle();
        QVERIFY(mirrors());
        QVERIFY(!card->commitButton()->isEnabled());
        QCOMPARE(card->hintText(), QStringLiteral("0 / 2 files selected") + space);
        page->unversionedButton()->click();
        settle();
        QCOMPARE(card->hintText(), QStringLiteral("0 / 1 file selected") + space);
        page->unversionedButton()->click();
        page->toggleAllChecked();
        settle();

        // Amend from the card, then back from the window's shortcut.
        QTest::mouseClick(card->amendBox(), Qt::LeftButton, {}, QPoint(6, card->amendBox()->height() / 2));
        settle();
        QVERIFY(page->commitControls().amendChecked);
        QVERIFY(mirrors());
        QCOMPARE(card->commitButton()->accessibleName(), QStringLiteral("Amend"));
        QVERIFY(card->isVisible());
        QTest::keyClick(card->editor(), Qt::Key_A, Qt::ControlModifier | Qt::ShiftModifier);
        settle();
        QVERIFY(!page->commitControls().amendChecked);
        QVERIFY(!card->amendBox()->isChecked());
        QVERIFY(mirrors());

        // A merge in progress rules amending out.
        const Commit head = f.repo->headCommit();
        MergeState merge;
        merge.inProgress = true;
        merge.source = QStringLiteral("feature");
        page->setMergeState(merge, head);
        settle();
        QVERIFY(mirrors());
        QVERIFY(!card->amendBox()->isEnabled());
        QCOMPARE(card->amendBox()->toolTip(), QStringLiteral("Not while a merge is in progress"));
        QCOMPARE(card->commitButton()->accessibleName(), QStringLiteral("Commit merge"));
        page->setMergeState(MergeState(), head);
        settle();
        QVERIFY(mirrors());
        QVERIFY(card->amendBox()->isEnabled());
    }

    // The generate button in the card's corner wears what the page's wears,
    // when the card opens and after every change the page announces — also
    // after asking for a message with no agent installed.
    void theCommitPopoverMirrorsTheGenerateButton()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(f.openCard());
        CommitPage *page = f.page();
        QToolButton *generate = f.popover()->editor()->cornerButton();
        const auto mirrors = [&] {
            const CommitPage::CommitControls c = page->commitControls();
            return generate->text() == c.generateText && generate->toolTip() == c.generateTip
                && c.generateText == f.pageEditor()->cornerButton()->text()
                && c.generateTip == f.pageEditor()->cornerButton()->toolTip();
        };
        QVERIFY(mirrors());
        QCOMPARE(generate->focusPolicy(), Qt::NoFocus);
        QCOMPARE(f.popover()->agentButton()->focusPolicy(), Qt::NoFocus);
        QCOMPARE(f.popover()->agentButton()->toolTip(), CommitPage::agentButtonTip());

        QSignalSpy announced(page, &CommitPage::commitControlsChanged);
        page->toggleAllChecked();
        settle();
        QVERIFY(announced.count() > 0);
        QVERIFY(mirrors());

        {
            // Only git on PATH: no agent is found, so nothing is started.
            ScopedEnv onlyGit("PATH", f.tools->path().toUtf8());
            QSignalSpy status(page, &CommitPage::statusMessage);
            page->generateMessage();
            settle();
            QCOMPARE(status.count(), 1);
            QVERIFY(status.first().first().toString().startsWith(QStringLiteral("Neither claude nor codex")));
        }
        QVERIFY(mirrors());
        QCOMPARE(generate->text(), ui::icon(ui::kSparkle, QStringLiteral("✨")).trimmed());
    }

    // Commit from the card's button and from Ctrl+Return: HEAD moves, the
    // shared message is gone, the card closes and the rail has the keyboard.
    // An amend closes it too; with nothing left the button is off.
    void theCommitPopoverCommits()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(activate(f.window.get()));
        CommitPopover *card = f.popover();
        QListView *list = f.rail()->list();
        QVERIFY(f.openCard());

        QString head = f.head();
        QTest::keyClicks(card->editor(), QStringLiteral("Commit from the card"));
        QTest::mouseClick(card->commitButton(), Qt::LeftButton);
        settle();
        QVERIFY(!card->isVisible());
        QVERIFY(f.head() != head);
        QCOMPARE(f.repo->headCommit().subject, QStringLiteral("Commit from the card"));
        QVERIFY(f.page()->messageDocument()->isEmpty());
        QTRY_COMPARE(f.window->focusWidget(), static_cast<QWidget *>(list));

        // Ctrl+Return opens the card from the rail, and commits from it.
        head = f.head();
        QTest::keyClick(list, Qt::Key_Return, Qt::ControlModifier);
        settle();
        QVERIFY(card->isVisible());
        f.page()->toggleAllChecked(); // the unversioned file
        QTest::keyClicks(card->editor(), QStringLiteral("Second from the keys"));
        QTest::keyClick(card->editor(), Qt::Key_Return, Qt::ControlModifier);
        settle();
        QVERIFY(!card->isVisible());
        QCOMPARE(f.repo->headCommit().subject, QStringLiteral("Second from the keys"));
        QVERIFY(f.page()->messageDocument()->isEmpty());
        QTRY_COMPARE(f.window->focusWidget(), static_cast<QWidget *>(list));

        // Nothing left to commit: the button is off and the keys do nothing.
        head = f.head();
        QVERIFY(QMetaObject::invokeMethod(f.window.get(), "showCommitPopover"));
        settle();
        QCOMPARE(card->hintText(), QStringLiteral("No changes to commit"));
        QVERIFY(!card->commitButton()->isEnabled());
        QVERIFY(!card->commit());
        QTest::keyClick(card->editor(), Qt::Key_Return, Qt::ControlModifier);
        settle();
        QVERIFY(card->isVisible());
        QCOMPARE(f.head(), head);

        // Amending the last commit closes the card as well.
        QTest::mouseClick(card->amendBox(), Qt::LeftButton, {}, QPoint(6, card->amendBox()->height() / 2));
        settle();
        QTRY_VERIFY(card->commitButton()->isEnabled()); // the amended commit's files are ticked
        QCOMPARE(card->editor()->toPlainText(), QStringLiteral("Second from the keys"));
        QTest::keyClick(card->editor(), Qt::Key_End, Qt::ControlModifier);
        QTest::keyClicks(card->editor(), QStringLiteral(", amended"));
        QTest::keyClick(card->editor(), Qt::Key_Return, Qt::ControlModifier);
        settle();
        QVERIFY(!card->isVisible());
        QVERIFY(f.head() != head);
        QCOMPARE(f.repo->headCommit().subject, QStringLiteral("Second from the keys, amended"));
        QVERIFY(!f.page()->commitControls().amendChecked);
        QTRY_COMPARE(f.window->focusWidget(), static_cast<QWidget *>(list));
    }

    // No message: the page's warning comes up and is answered in its own
    // window, and the card stays with the keyboard back in its box.
    void theCommitPopoverStaysWhenThePageDoesNotCommit()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(activate(f.window.get()));
        QVERIFY(f.openCard());
        CommitPopover *card = f.popover();
        const QString head = f.head();
        bool answered = false;
        clickNextMessageBox(&answered);
        QVERIFY(!card->commit());
        QVERIFY(answered);
        settle();
        QVERIFY(card->isVisible());
        QCOMPARE(f.head(), head);
        QTRY_COMPARE(f.window->focusWidget(), static_cast<QWidget *>(card->editor()));
    }

    // Escape from anything on the card, a second click on the tile, a press
    // outside it, the history, Ctrl+B and another repository close it —
    // with the rail's list taking the keyboard where the layout stays Mini.
    void theCommitPopoverCloses()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(activate(f.window.get()));
        CommitPopover *card = f.popover();
        QListView *list = f.rail()->list();
        QVERIFY(f.openCard());
        const auto reopen = [&] {
            QVERIFY(QMetaObject::invokeMethod(f.window.get(), "showCommitPopover"));
            settle();
            QVERIFY(card->isVisible());
        };

        for (QWidget *w : QList<QWidget *>{card->editor(), card->amendBox(), card->commitButton()}) {
            reopen();
            w->setFocus();
            QTRY_COMPARE(f.window->focusWidget(), w);
            QTest::keyClick(w, Qt::Key_Escape);
            settle();
            QVERIFY2(!card->isVisible(), w->metaObject()->className());
            QTRY_COMPARE(f.window->focusWidget(), static_cast<QWidget *>(list));
        }

        // The tile toggles.
        QTest::mouseClick(f.tile(), Qt::LeftButton);
        settle();
        QVERIFY(card->isVisible());
        QTest::mouseClick(f.tile(), Qt::LeftButton);
        settle();
        QVERIFY(!card->isVisible());
        QTRY_COMPARE(f.window->focusWidget(), static_cast<QWidget *>(list));

        // A press on the diff closes it, and still reaches the diff.
        reopen();
        DiffView *diff = f.diff();
        clickAt(f.window.get(), diff->mapTo(f.window.get(), diff->rect().center()));
        settle();
        QVERIFY(!card->isVisible());
        QTRY_VERIFY(diff->hasFocus() || diff->isAncestorOf(f.window->focusWidget()));

        // The history.
        reopen();
        QTest::mouseClick(f.bar()->historyTab(), Qt::LeftButton);
        settle();
        QVERIFY(!card->isVisible());
        QCOMPARE(f.window->mode(), MainWindow::HistoryMode);
        // Neither the slot nor the keys open it there.
        QVERIFY(QMetaObject::invokeMethod(f.window.get(), "showCommitPopover"));
        settle();
        QVERIFY(!card->isVisible());
        QTest::keyClick(list, Qt::Key_Return, Qt::ControlModifier);
        settle();
        QVERIFY(!card->isVisible());
        f.window->setMode(MainWindow::CommitMode);
        settle();

        // Ctrl+B: the Docked layout, where the keys press the page's button.
        reopen();
        QTest::keyClick(card->editor(), Qt::Key_B, Qt::ControlModifier);
        settle();
        QVERIFY(!card->isVisible());
        QCOMPARE(f.window->paneLayout(), PaneLayout::Docked);
        QVERIFY(QMetaObject::invokeMethod(f.window.get(), "showCommitPopover"));
        settle();
        QVERIFY(!card->isVisible());
        f.window->setPaneLayout(PaneLayout::Mini, false);
        settle();

        // The same repository again is no switch; one that is not a
        // repository is none either; another one closes the card.
        reopen();
        QVERIFY(f.window->openRepository(f.repo->root()));
        settle();
        QVERIFY(card->isVisible());
        QTemporaryDir plain;
        QVERIFY(plain.isValid());
        bool answered = false;
        clickNextMessageBox(&answered);
        QVERIFY(!f.window->openRepository(plain.path()));
        QVERIFY(answered);
        settle();
        QVERIFY(card->isVisible());
        QTemporaryDir other;
        QVERIFY(other.isValid());
        QVERIFY(git(other.path(), {"init", "-q", "-b", "main"}));
        QVERIFY(commit(other.path(), QStringLiteral("other"), 1));
        QVERIFY(f.window->openRepository(other.path()));
        settle();
        QVERIFY(!card->isVisible());
    }

    // Working the rail, the wheel over the diff, Refresh and another window
    // leave the card open.
    void theCommitPopoverStaysOpenWhileTheRailIsWorked()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(activate(f.window.get()));
        QVERIFY(f.openCard());
        CommitPopover *card = f.popover();
        QListView *list = f.rail()->list();
        QAbstractItemModel *model = list->model();
        QTest::keyClicks(card->editor(), QStringLiteral("Kept"));

        // Selecting another miniature shows its diff.
        const QModelIndex second = model->index(1, ChangesModel::Check);
        const QString path = second.data(ChangesModel::PathRole).toString();
        const QImage diffBefore = f.diff()->grab().toImage();
        QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, list->visualRect(second).center());
        settle();
        QVERIFY(card->isVisible());
        bool ok = false;
        QCOMPARE(f.page()->currentChange(&ok).path, path);
        QVERIFY(f.diff()->grab().toImage() != diffBefore); // another file's diff

        // Ticking a miniature updates the hint.
        const QString hint = card->hintText();
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::ControlModifier, list->visualRect(second).center());
        settle();
        QVERIFY(card->isVisible());
        QVERIFY(card->hintText() != hint);

        // The wheel over the diff.
        QWidget *viewport = f.diff()->viewport();
        const QPoint centre = viewport->rect().center();
        QWheelEvent wheel(centre, viewport->mapToGlobal(centre), QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
                          Qt::NoScrollPhase, false);
        QApplication::sendEvent(viewport, &wheel);
        settle();
        QVERIFY(card->isVisible());

        // The rail's Refresh.
        QTest::mouseClick(f.railRefresh(), Qt::LeftButton);
        settle();
        QVERIFY(card->isVisible());
        QCOMPARE(card->editor()->toPlainText(), QStringLiteral("Kept"));

        // Another window, and a dialog of this one: separate windows both.
        QWidget other;
        other.resize(200, 100);
        other.show();
        QVERIFY(QTest::qWaitForWindowExposed(&other));
        QTest::mouseClick(&other, Qt::LeftButton);
        settle();
        QVERIFY(card->isVisible());
        QDialog dialog(f.window.get());
        dialog.resize(200, 100);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        QTest::mouseClick(&dialog, Qt::LeftButton);
        settle();
        QVERIFY(card->isVisible());
        QCOMPARE(card->editor()->toPlainText(), QStringLiteral("Kept"));
    }

    // Plain Return is a new line in either box; Ctrl+Return in the Docked
    // layout still commits, from the page's own message box.
    void returnIsANewLineAndCtrlReturnCommitsInTheDockedLayout()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(activate(f.window.get()));
        const QString head = f.head();

        QVERIFY(f.openCard());
        MessageEdit *card = f.popover()->editor();
        QTest::keyClicks(card, QStringLiteral("Subject"));
        QTest::keyClick(card, Qt::Key_Return);
        QTest::keyClicks(card, QStringLiteral("Body"));
        QCOMPARE(card->toPlainText(), QStringLiteral("Subject\nBody"));
        QVERIFY(f.popover()->isVisible());
        QCOMPARE(f.head(), head);

        f.window->setPaneLayout(PaneLayout::Docked, false);
        settle();
        MessageEdit *page = f.pageEditor();
        page->setFocus();
        QTRY_COMPARE(f.window->focusWidget(), static_cast<QWidget *>(page));
        QTest::keyClick(page, Qt::Key_End, Qt::ControlModifier);
        QTest::keyClick(page, Qt::Key_Return);
        QTest::keyClicks(page, QStringLiteral("More"));
        QCOMPARE(page->toPlainText(), QStringLiteral("Subject\nBody\nMore"));
        QCOMPARE(f.head(), head);
        QTest::keyClick(page, Qt::Key_Return, Qt::ControlModifier);
        settle();
        QVERIFY(f.head() != head);
        QCOMPARE(f.repo->headMessage().trimmed(), QStringLiteral("Subject\nBody\nMore"));
        QVERIFY(f.page()->messageDocument()->isEmpty());
    }

    // F5 with the card open, over the same files and over changed ones: the
    // card, its text, the keyboard, the current file, the rail's scroll and
    // the place in the diff all stay; unchanged files reset nothing.
    void theCommitPopoverSurvivesARefresh()
    {
        WindowFixture f = mainWindow(30);
        QVERIFY(f.window);
        f.window->resize(1200, 560);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(activate(f.window.get()));
        f.window->setPaneLayout(PaneLayout::Mini, false);
        settle();

        // A tracked file of 120 lines with three changes far apart: a diff
        // that scrolls and has more than one change to be at, so a position
        // reset to the start cannot pass for one that survived.
        const QString root = f.repo->root();
        QByteArray lines;
        for (int i = 1; i <= 120; ++i)
            lines += "line " + QByteArray::number(i) + '\n';
        QVERIFY(writeFixture(QDir(root).filePath(QStringLiteral("long.txt")), lines));
        QVERIFY(git(root, {"add", "long.txt"}));
        QVERIFY(git(root, {"commit", "-q", "-m", "long"}, 2));
        for (const int n : {10, 60, 110})
            lines.replace("line " + QByteArray::number(n) + '\n', "line " + QByteArray::number(n) + " changed\n");
        QVERIFY(writeFixture(QDir(root).filePath(QStringLiteral("long.txt")), lines));
        QListView *list = f.rail()->list();
        QTest::keyClick(list, Qt::Key_F5);
        settle();

        int longRow = -1;
        for (int row = 0; row < list->model()->rowCount(); ++row)
            if (list->model()->index(row, 0).data(ChangesModel::PathRole).toString() == QStringLiteral("long.txt"))
                longRow = row;
        QCOMPARE(longRow, 1); // the tracked files first, a.txt and then it
        list->setCurrentIndex(list->model()->index(longRow, ChangesModel::Check));
        settle();
        // Scrolled a little, the current miniature still in view.
        QScrollBar *scroll = list->verticalScrollBar();
        const int listScroll = 20;
        QVERIFY(scroll->maximum() > listScroll);
        scroll->setValue(listScroll);
        QVERIFY(list->viewport()->rect().contains(list->visualRect(list->currentIndex())));

        // A later change and a scrolled diff: none of it where the diff opens.
        const DiffView::ViewState initial = f.diff()->viewState();
        QVERIFY(f.diff()->verticalScrollBar()->maximum() > 0);
        DiffPane *pane = f.window->findChild<DiffPane *>();
        QVERIFY(pane);
        pane->nextChange();
        pane->nextChange();
        settle();
        const DiffView::ViewState moved = f.diff()->viewState();
        QVERIFY2(moved.block > 0 && moved.block != initial.block,
                 qPrintable(QStringLiteral("block %1 -> %2").arg(initial.block).arg(moved.block)));
        QVERIFY2(moved.row > 0 && moved.row != initial.row,
                 qPrintable(QStringLiteral("row %1 -> %2").arg(initial.row).arg(moved.row)));
        QVERIFY(f.openCard());
        CommitPopover *card = f.popover();
        QTest::keyClicks(card->editor(), QStringLiteral("Refreshed around"));
        const QString current = list->currentIndex().data(ChangesModel::PathRole).toString();
        QSignalSpy resets(f.page()->proxy()->sourceModel(), &QAbstractItemModel::modelReset);
        const auto unchanged = [&](const DiffView::ViewState &diff) {
            const DiffView::ViewState now = f.diff()->viewState();
            return card->isVisible() && card->editor()->toPlainText() == QStringLiteral("Refreshed around")
                && f.window->focusWidget() == card->editor()
                && list->currentIndex().data(ChangesModel::PathRole).toString() == current
                && scroll->value() == listScroll && now.row == diff.row && now.column == diff.column && now.block == diff.block;
        };

        DiffView::ViewState diff = f.diff()->viewState();
        QCOMPARE(diff.block, moved.block); // opening the card left the diff alone
        QCOMPARE(diff.row, moved.row);
        QTest::keyClick(card->editor(), Qt::Key_F5);
        settle();
        QVERIFY(unchanged(diff));
        QCOMPARE(resets.count(), 0);

        QVERIFY(writeFixture(QDir(f.repo->root()).filePath(QStringLiteral("zz-new.txt")), "new\n"));
        diff = f.diff()->viewState();
        QCOMPARE(diff.block, moved.block);
        QCOMPARE(diff.row, moved.row);
        QTest::keyClick(card->editor(), Qt::Key_F5);
        settle();
        QCOMPARE(resets.count(), 1);
        QVERIFY(unchanged(diff));
        QCOMPARE(card->hintText(), QStringLiteral("1 / 34 files selected · Space on a tile toggles it"));
    }

    // One row for Ctrl+Return, saying where it works; the keypad's Enter is
    // the same binding, unlisted.
    void theKeybindingsListCtrlReturnOnce()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(QMetaObject::invokeMethod(f.window.get(), "showKeybindings"));
        auto *panel = f.window->findChild<KeybindingsPanel *>();
        QVERIFY(panel);
        auto *list = panel->findChild<QListView *>(QStringLiteral("keybindingsList"));
        QVERIFY(list);
        QAbstractItemModel *model = list->model();
        int rows = 0, commits = 0;
        for (int row = 0; row < model->rowCount(); ++row) {
            const QModelIndex index = model->index(row, 0);
            const QString keys = index.data(Qt::UserRole).toString();
            QVERIFY(keys != QStringLiteral("CTRL + ENTER"));
            if (index.data(Qt::DisplayRole).toString() == QStringLiteral("Commit checked files"))
                ++commits;
            if (keys != QStringLiteral("CTRL + RETURN"))
                continue;
            ++rows;
            QCOMPARE(index.data(Qt::UserRole + 2).toString(), QStringLiteral("Commit view, Mini rail"));
        }
        QCOMPARE(rows, 1);
        QCOMPARE(commits, 1);
        panel->close();
    }

    // `--mini --screenshot-menu commit` opens the card in the picture; the
    // Docked layout and the history do not; and no setting comes of it.
    void theScreenshotMenuOpensTheCommitPopover()
    {
        const QString binary = helperBinary();
        if (binary.isEmpty())
            QSKIP("omagit is not built (qmake6 omagit.pro && make)");
        QTemporaryDir repoDir, themeDir, home, tools, work;
        QVERIFY(repoDir.isValid() && themeDir.isValid() && home.isValid() && tools.isValid() && work.isValid());
        const QString repo = repoDir.path();
        QVERIFY(git(repo, {"init", "-q", "-b", "main"}));
        QVERIFY(writeFixture(QDir(repo).filePath(QStringLiteral("a.txt")), "a\n"));
        QVERIFY(git(repo, {"add", "-A"}));
        QVERIFY(git(repo, {"commit", "-q", "-m", "first"}, 1));
        QVERIFY(writeFixture(QDir(repo).filePath(QStringLiteral("a.txt")), "a changed\n"));
        // A theme of known colours at the design's text size.
        QVERIFY(writeFixture(QDir(themeDir.path()).filePath(QStringLiteral("colors.toml")),
                             "accent = \"#ff00ff\"\nbackground = \"#101010\"\nforeground = \"#eeeeee\"\n"));
        QVERIFY(writeFixture(QDir(themeDir.path()).filePath(QStringLiteral("shell.toml")), "[font]\nbase-size = 12\n"));
        const QString gitBinary = QStandardPaths::findExecutable(QStringLiteral("git"));
        QVERIFY(QFile::link(gitBinary, QDir(tools.path()).filePath(QStringLiteral("git"))));

        int run = 0;
        // The picture of one run, and the settings it left behind.
        const auto shoot = [&](const QStringList &flags, QStringList *keys) {
            const QString config = QDir(work.path()).filePath(QStringLiteral("config%1").arg(++run));
            const QString png = QDir(work.path()).filePath(QStringLiteral("shot%1.png").arg(run));
            QProcess p;
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert(QStringLiteral("HOME"), home.path());
            env.insert(QStringLiteral("XDG_CONFIG_HOME"), config);
            env.insert(QStringLiteral("OMAGIT_THEME_DIR"), themeDir.path());
            env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
            env.insert(QStringLiteral("PATH"), tools.path());
            p.setProcessEnvironment(env);
            p.start(binary, QStringList{QStringLiteral("--no-fetch"), QStringLiteral("--screenshot"), png,
                                        QStringLiteral("--screenshot-size"), QStringLiteral("945x612")}
                                + flags + QStringList{repo});
            if (!p.waitForFinished(30000) || p.exitCode() != 0)
                return QImage();
            *keys = QSettings(QDir(config).filePath(QStringLiteral("omagit/omagit.conf")), QSettings::IniFormat).allKeys();
            keys->sort();
            return QImage(png);
        };
        // The card's left frame: the longest run of accent pixels down the
        // column where the card starts (rail 14 + 52 + 8 at a 12 px text).
        const auto frameRun = [](const QImage &image) {
            int longest = 0, runLength = 0;
            for (int y = 0; y < image.height(); ++y) {
                runLength = image.pixelColor(74, y) == QColor(QStringLiteral("#ff00ff")) ? runLength + 1 : 0;
                longest = qMax(longest, runLength);
            }
            return longest;
        };
        const QString menu = QStringLiteral("--screenshot-menu"), commitMenu = QStringLiteral("commit");
        QStringList plainKeys, cardKeys, keys;
        QImage plain = shoot({QStringLiteral("--mini")}, &plainKeys);
        QVERIFY(!plain.isNull());
        QImage card = shoot({QStringLiteral("--mini"), menu, commitMenu}, &cardKeys);
        QVERIFY(!card.isNull());
        QVERIFY2(frameRun(card) >= 100, qPrintable(QString::number(frameRun(card))));
        QVERIFY(frameRun(plain) < 50);
        QCOMPARE(cardKeys, plainKeys);

        QImage docked = shoot({menu, commitMenu}, &keys);
        QVERIFY(!docked.isNull());
        QVERIFY(frameRun(docked) < 50);
        QImage history = shoot({QStringLiteral("--mini"), QStringLiteral("--history"), menu, commitMenu}, &keys);
        QVERIFY(!history.isNull());
        QVERIFY(frameRun(history) < 50);
    }

    // --- The agent settings popover -----------------------------------------

    // The segmented control the top bar and the agent picker share: at its
    // hints the first segment takes its own width and the last the rest; in
    // stretch mode each gets floor(width / n) and the last the remainder,
    // with its content centred; a single segment fills the strip.
    void segmentStripLaysOutItsSegments()
    {
        const auto segment = [](const QString &label) {
            auto *s = ui::toolButton<SegmentButton>(label);
            s->setGlyph(ui::kRobot, QStringLiteral("R"));
            s->setCheckable(true);
            return s;
        };
        QWidget host;
        host.resize(600, 100);
        auto *first = segment(QStringLiteral("Claude Code"));
        auto *second = segment(QStringLiteral("Codex"));
        auto *strip = new SegmentStrip({first, second}, &host);
        host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        QCOMPARE(strip->sizeHint().width(), first->sizeHint().width() + second->sizeHint().width() + 3);
        QCOMPARE(strip->sizeHint().height(), qMax(first->sizeHint().height(), second->sizeHint().height()) + 2);
        strip->setGeometry(0, 0, strip->sizeHint().width() + 40, 30);
        QCOMPARE(first->geometry(), QRect(1, 1, first->sizeHint().width(), 28));
        QCOMPARE(second->geometry(), QRect(2 + first->width(), 1, strip->width() - 3 - first->width(), 28));

        strip->setStretch(true);
        QVERIFY(first->isCentred() && second->isCentred());
        strip->setGeometry(0, 0, 301, 30);
        const int each = 301 / 2;
        QCOMPARE(first->geometry(), QRect(1, 1, each - 1, 28));
        QCOMPARE(second->geometry(), QRect(each + 1, 1, 301 - 1 - (each + 1), 28));
        // The ink of a centred segment keeps the same distance from either side.
        const auto inkMargins = [](QWidget *w) {
            const QImage image = w->grab().toImage();
            const QColor fill = image.pixelColor(0, 0);
            int left = image.width(), right = -1;
            for (int x = 0; x < image.width(); ++x)
                for (int y = 0; y < image.height(); ++y)
                    if (!closeTo(image.pixelColor(x, y), fill)) {
                        left = qMin(left, x);
                        right = qMax(right, x);
                    }
            return qMakePair(left, image.width() - 1 - right);
        };
        const auto margins = inkMargins(second);
        // The glyph's ink is narrower than the box it is laid out in.
        QVERIFY2(qAbs(margins.first - margins.second) <= ui::space(14) / 2, qPrintable(QStringLiteral("%1 %2").arg(margins.first).arg(margins.second)));

        auto *third = segment(QStringLiteral("Three"));
        SegmentStrip three({segment(QStringLiteral("One")), segment(QStringLiteral("Two")), third});
        three.setStretch(true);
        three.resize(302, 30);
        three.show();
        QVERIFY(QTest::qWaitForWindowExposed(&three));
        QCOMPARE(three.segments().size(), 3);
        QCOMPARE(three.segments().at(0)->geometry(), QRect(1, 1, 99, 28));
        QCOMPARE(three.segments().at(1)->geometry(), QRect(101, 1, 99, 28));
        QCOMPARE(third->geometry(), QRect(201, 1, 100, 28));

        auto *only = segment(QStringLiteral("Claude Code"));
        SegmentStrip one({only});
        one.resize(250, 30);
        one.show();
        QVERIFY(QTest::qWaitForWindowExposed(&one));
        QCOMPARE(only->geometry(), QRect(1, 1, 248, 28));
        one.setStretch(true);
        QCOMPARE(only->geometry(), QRect(1, 1, 248, 28));
    }

    // Nothing on PATH but git: the card says so, offers the two commands and
    // copies them; none of the settings' parts is there.
    void theAgentCardSaysWhenNoAgentIsInstalled()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(activate(f.window.get()));
        AgentScope scope(f.tools->path());
        AgentPopover *card = f.agentCard();
        QVERIFY(card);
        QVERIFY(!card->isVisible());
        QCOMPARE(card->parentWidget(), f.host());

        QTest::mouseClick(f.page()->agentButton(), Qt::LeftButton);
        settle();
        QVERIFY(card->isVisible());
        const QStringList labels = cardLabels(card);
        for (const QString &text : {QStringLiteral("No coding agent installed"), QStringLiteral("Claude Code or Codex writes it for you."),
                                    QStringLiteral("INSTALL ONE"), QStringLiteral("$ omarchy default agent claude"),
                                    QStringLiteral("$ omarchy default agent codex"),
                                    QStringLiteral("Reopen this menu once one is installed.")})
            QVERIFY2(labels.contains(text), qPrintable(text + QStringLiteral(" in ") + labels.join(QLatin1Char('/'))));
        QVERIFY(!card->agentPicker());
        QVERIFY(card->modelRows().isEmpty());
        QVERIFY(!card->otherModelButton() && !card->otherModelField());
        QVERIFY(!card->levelTrack());
        QVERIFY(!card->generateButton());
        // The robot: muted ink at the top left, in the design's 20 px glyph.
        const QImage image = card->grab().toImage();
        bool robot = false;
        const QColor muted = OmarchyTheme::instance()->mutedText();
        for (int x = ui::space(10); x < ui::space(10) + ui::space(32); ++x)
            for (int y = ui::space(10); y < ui::space(10) + ui::space(24); ++y)
                robot = robot || closeTo(image.pixelColor(x, y), muted);
        QVERIFY(robot);

        QCOMPARE(card->copyButtons().size(), 2);
        QSignalSpy status(f.page(), &CommitPage::statusMessage);
        QApplication::clipboard()->clear();
        QTest::mouseClick(card->copyButtons().first(), Qt::LeftButton);
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("omarchy default agent claude"));
        QCOMPARE(status.count(), 1);
        QCOMPARE(status.first().first().toString(), QStringLiteral("Copied"));
        QTest::mouseClick(card->copyButtons().last(), Qt::LeftButton);
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("omarchy default agent codex"));
        QVERIFY(card->isVisible()); // copying is no reason to close
        QVERIFY(QSettings().childKeys().filter(QStringLiteral("agent")).isEmpty());
        QVERIFY(!QSettings().childGroups().contains(QStringLiteral("agent")));
    }

    // A fake claude on PATH: its name on the picker, the models its --help
    // names, and its levels on the track; Default chosen while nothing is saved.
    void theAgentCardListsTheAgentsModelsAndLevels()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(writeFakeClaude(f.tools->path()));
        AgentScope scope(f.tools->path(), QStringLiteral("claude"));
        AgentPopover *card = f.agentCard();
        QTest::mouseClick(f.page()->agentButton(), Qt::LeftButton);
        settle();
        QVERIFY(card->isVisible());

        SegmentStrip *picker = card->agentPicker();
        QVERIFY(picker);
        QVERIFY(picker->isStretch());
        QCOMPARE(picker->segments().size(), 1);
        QCOMPARE(picker->segments().first()->text(), QStringLiteral("Claude Code"));
        QVERIFY(picker->segments().first()->isChecked());
        QCOMPARE(picker->segments().first()->width(), picker->width() - 2); // one agent, the whole width
        QCOMPARE(picker->height(), ui::space(28));

        QCOMPARE(rowTexts(card), QStringList({QStringLiteral("Default|whatever claude uses|*"), QStringLiteral("Fable|fable"),
                                              QStringLiteral("Opus|opus"), QStringLiteral("Sonnet|sonnet")}));
        QVERIFY(card->levelTrack());
        QCOMPARE(card->levelTrack()->labels(), QStringList({QStringLiteral("Default"), QStringLiteral("Low"), QStringLiteral("Medium"),
                                                            QStringLiteral("High"), QStringLiteral("Xhigh"), QStringLiteral("Max")}));
        QCOMPARE(card->levelTrack()->selected(), 0);
        const QStringList labels = cardLabels(card);
        for (const QString &text : {QStringLiteral("AGENT"), QStringLiteral("claude is the Omarchy default"), QStringLiteral("MODEL"),
                                    QStringLiteral("from claude --help"), QStringLiteral("REASONING"),
                                    QStringLiteral("more thinking, slower answer")})
            QVERIFY2(labels.contains(text), qPrintable(text + QStringLiteral(" in ") + labels.join(QLatin1Char('/'))));
        QVERIFY(card->otherModelButton()->isVisible());
        QVERIFY(!card->otherModelField()->isVisible());
        QVERIFY(card->generateButton()->isVisible());
        QVERIFY(card->generateButton()->text().endsWith(QStringLiteral("Generate now  Ctrl+G")));
        // Opening it saves nothing.
        QVERIFY(!QSettings().childGroups().contains(QStringLiteral("agent")));
    }

    // Every choice is saved the moment it is made, the page's tooltip follows,
    // and a level the new model does not have goes.
    void theAgentCardSavesEachChoice()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(activate(f.window.get()));
        QVERIFY(writeFakeClaude(f.tools->path()));
        AgentScope scope(f.tools->path());
        AgentPopover *card = f.agentCard();
        QTest::mouseClick(f.page()->agentButton(), Qt::LeftButton);
        settle();
        QVERIFY(card->isVisible());

        const QRect before = card->geometry();
        QTest::mouseClick(modelRow(card, QStringLiteral("Opus")), Qt::LeftButton);
        settle();
        QCOMPARE(savedAgent("agent/name"), QStringLiteral("claude"));
        QCOMPARE(savedAgent("agent/model"), QStringLiteral("opus"));
        QVERIFY(card->isVisible()); // a choice is no reason to close
        QCOMPARE(card->geometry(), before);
        QAbstractButton *opus = modelRow(card, QStringLiteral("Opus"));
        QVERIFY(opus->isChecked());
        QVERIFY(opus->font().bold());
        QVERIFY(!modelRow(card, QStringLiteral("Default"))->isChecked());
        QVERIFY(!modelRow(card, QStringLiteral("Default"))->font().bold());
        // The name in the accent: some of its ink is the accent itself.
        {
            const QImage image = opus->grab().toImage();
            bool accent = false;
            for (int x = ui::space(10); x < ui::space(60); ++x)
                for (int y = 0; y < image.height(); ++y)
                    accent = accent || closeTo(image.pixelColor(x, y), OmarchyTheme::instance()->accent());
            QVERIFY(accent);
        }
        QVERIFY(f.page()->commitControls().generateTip.contains(QStringLiteral("(opus)")));
        QVERIFY(f.pageEditor()->cornerButton()->toolTip().contains(QStringLiteral("(opus)")));
        // A second click on the chosen row changes nothing.
        QTest::mouseClick(modelRow(card, QStringLiteral("Opus")), Qt::LeftButton);
        settle();
        QVERIFY(modelRow(card, QStringLiteral("Opus"))->isChecked());

        // The track: a click on High, then the keys.
        LevelTrack *track = card->levelTrack();
        QTest::mouseClick(track, Qt::LeftButton, {}, track->stopCentre(3).toPoint());
        settle();
        QCOMPARE(savedAgent("agent/effort"), QStringLiteral("high"));
        track = card->levelTrack();
        QCOMPARE(track->selected(), 3);
        track->setFocus();
        QTRY_VERIFY(track->hasFocus());
        QTest::keyClick(track, Qt::Key_Left);
        settle();
        QCOMPARE(savedAgent("agent/effort"), QStringLiteral("medium"));
        QTRY_VERIFY(card->levelTrack()->hasFocus()); // the keyboard stays on the (new) track
        QTest::keyClick(card->levelTrack(), Qt::Key_Right);
        QTest::keyClick(card->levelTrack(), Qt::Key_Right);
        settle();
        QCOMPARE(savedAgent("agent/effort"), QStringLiteral("xhigh"));
        QCOMPARE(card->levelTrack()->selected(), 4);
        QTest::keyClick(card->levelTrack(), Qt::Key_Left);
        settle();
        QCOMPARE(savedAgent("agent/effort"), QStringLiteral("high"));

        // A level Sonnet has stays; one it has not goes.
        QTest::mouseClick(modelRow(card, QStringLiteral("Sonnet")), Qt::LeftButton);
        settle();
        QCOMPARE(savedAgent("agent/model"), QStringLiteral("sonnet"));
        QCOMPARE(savedAgent("agent/effort"), QStringLiteral("high"));

        // A saved level the track does not have shows as Default, and a click
        // on Default, the stop already lit, clears it.
        CommitMessageAgent::saveChoice(AgentChoice{QStringLiteral("claude"), QStringLiteral("opus"), QStringLiteral("ultra")});
        card->dismiss();
        QTest::mouseClick(f.page()->agentButton(), Qt::LeftButton);
        settle();
        QVERIFY(card->isVisible());
        QCOMPARE(card->levelTrack()->selected(), 0);
        QTest::mouseClick(card->levelTrack(), Qt::LeftButton, {}, card->levelTrack()->stopCentre(0).toPoint());
        settle();
        QCOMPARE(savedAgent("agent/effort"), QString());
        QCOMPARE(savedAgent("agent/model"), QStringLiteral("opus"));
        QCOMPARE(CommitMessageAgent::savedChoice().effort, QString());
        QCOMPARE(card->levelTrack()->selected(), 0);
        QVERIFY(f.page()->commitControls().generateTip.contains(QStringLiteral("(opus)")));

        CommitMessageAgent::saveChoice(AgentChoice{QStringLiteral("claude"), QStringLiteral("opus"), QStringLiteral("ultra")});
        card->dismiss();
        QTest::mouseClick(f.page()->agentButton(), Qt::LeftButton);
        settle();
        QCOMPARE(card->levelTrack()->selected(), 0); // a level the track does not have is its Default
        QTest::mouseClick(modelRow(card, QStringLiteral("Sonnet")), Qt::LeftButton);
        settle();
        QCOMPARE(savedAgent("agent/model"), QStringLiteral("sonnet"));
        QCOMPARE(savedAgent("agent/effort"), QString());
        // Default is a model like the others.
        QTest::mouseClick(modelRow(card, QStringLiteral("Default")), Qt::LeftButton);
        settle();
        QCOMPARE(savedAgent("agent/model"), QString());
        QVERIFY(f.page()->commitControls().generateTip.contains(QStringLiteral("(default model)")));
    }

    // A model by name: the field opens on demand, Return saves and shows the
    // name as a row, Escape closes the field before the card.
    void theAgentCardTakesAModelByName()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(activate(f.window.get()));
        QVERIFY(writeFakeClaude(f.tools->path()));
        AgentScope scope(f.tools->path());
        AgentPopover *card = f.agentCard();
        QTest::mouseClick(f.page()->agentButton(), Qt::LeftButton);
        settle();
        QVERIFY(card->isVisible());
        const int height = card->height();

        QTest::mouseClick(card->otherModelButton(), Qt::LeftButton);
        settle();
        QLineEdit *field = card->otherModelField();
        QVERIFY(field->isVisible());
        QVERIFY(!card->otherModelButton()->isVisible());
        QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget *>(field));
        QCOMPARE(field->text(), QString());
        QCOMPARE(field->placeholderText(), QStringLiteral("Model name, as claude --model takes it"));
        QCOMPARE(field->width(), card->generateButton()->width()); // the inner width
        QCOMPARE(card->height(), height); // the field takes the button's row
        QTest::keyClicks(field, QStringLiteral("claude-x"));
        QTest::keyClick(field, Qt::Key_Return);
        settle();
        QCOMPARE(savedAgent("agent/model"), QStringLiteral("claude-x"));
        QVERIFY(card->isVisible());
        QCOMPARE(rowTexts(card), QStringList({QStringLiteral("Default|whatever claude uses"), QStringLiteral("Claude-x|claude-x|*"),
                                              QStringLiteral("Fable|fable"), QStringLiteral("Opus|opus"),
                                              QStringLiteral("Sonnet|sonnet")}));
        QVERIFY(!card->otherModelField()->isVisible());
        QVERIFY(card->otherModelButton()->isVisible());
        QTRY_VERIFY(card->hasFocus());

        // Open again: the name is there to edit. Escape: back, nothing changed.
        QTest::mouseClick(card->otherModelButton(), Qt::LeftButton);
        settle();
        field = card->otherModelField();
        QCOMPARE(field->text(), QStringLiteral("claude-x"));
        QCOMPARE(field->selectedText(), QStringLiteral("claude-x"));
        QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget *>(field));
        QTest::keyClicks(field, QStringLiteral("other"));
        QTest::keyClick(field, Qt::Key_Escape);
        settle();
        QVERIFY(card->isVisible());
        QVERIFY(!field->isVisible());
        QVERIFY(card->otherModelButton()->isVisible());
        QCOMPARE(savedAgent("agent/model"), QStringLiteral("claude-x"));
        QTRY_VERIFY(card->hasFocus());
        QTest::keyClick(card, Qt::Key_Escape);
        settle();
        QVERIFY(!card->isVisible());

        // An empty name is Default.
        QTest::mouseClick(f.page()->agentButton(), Qt::LeftButton);
        settle();
        QTest::mouseClick(card->otherModelButton(), Qt::LeftButton);
        settle();
        card->otherModelField()->clear();
        QTest::keyClick(card->otherModelField(), Qt::Key_Return);
        settle();
        QCOMPARE(savedAgent("agent/model"), QString());
        QVERIFY(modelRow(card, QStringLiteral("Default"))->isChecked());
        QVERIFY(!modelRow(card, QStringLiteral("Claude-x")));
    }

    // Generate now closes the card and asks the agent, as the page's own
    // button does.
    void theAgentCardGeneratesNow()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(activate(f.window.get()));
        QVERIFY(writeFakeClaude(f.tools->path()));
        AgentScope scope(f.tools->path());
        AgentPopover *card = f.agentCard();
        QTest::mouseClick(f.page()->agentButton(), Qt::LeftButton);
        settle();
        QVERIFY(card->isVisible());
        QVERIFY(card->generateButton()->isDefault());
        QTest::mouseClick(card->generateButton(), Qt::LeftButton);
        QVERIFY(!card->isVisible());
        QTRY_COMPARE_WITH_TIMEOUT(f.pageEditor()->toPlainText(), QStringLiteral("Fake subject"), 10000);
    }

    // While a run is going, Generate now is off, saying why, and the rest of
    // the card, an open other-model field with it, stays as it was; once the
    // run is over it is on again.
    void theAgentCardWaitsForARunningAgent()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(activate(f.window.get()));
        QVERIFY(writeFakeClaude(f.tools->path()));
        AgentScope scope(f.tools->path());
        AgentPopover *card = f.agentCard();
        QTest::mouseClick(f.page()->agentButton(), Qt::LeftButton);
        settle();
        QVERIFY(card->isVisible());
        QVERIFY(!f.page()->commitControls().generating);
        QPushButton *generate = card->generateButton();
        QVERIFY(generate->isEnabled());
        const QString tip = generate->toolTip();
        QTest::mouseClick(card->otherModelButton(), Qt::LeftButton);
        settle();
        QLineEdit *field = card->otherModelField();
        QTest::keyClicks(field, QStringLiteral("claude-x"));

        // Checked before the event loop runs again, so before the fake can
        // have answered.
        f.page()->generateMessage();
        QVERIFY(f.page()->commitControls().generating);
        QVERIFY(card->isVisible());
        QCOMPARE(card->generateButton(), generate); // followed, not rebuilt
        QVERIFY(!generate->isEnabled());
        QCOMPARE(generate->toolTip(), QStringLiteral("The agent is writing the message — the sparkle stops it"));
        QCOMPARE(card->otherModelField(), field);
        QVERIFY(field->isVisible());
        QCOMPARE(field->text(), QStringLiteral("claude-x"));

        QTRY_COMPARE_WITH_TIMEOUT(f.pageEditor()->toPlainText(), QStringLiteral("Fake subject"), 10000);
        QVERIFY(!f.page()->commitControls().generating);
        QVERIFY(generate->isEnabled());
        QCOMPARE(generate->toolTip(), tip);
        QCOMPARE(card->otherModelField(), field);
        QCOMPARE(field->text(), QStringLiteral("claude-x"));
        QVERIFY(QSettings().value(QStringLiteral("agent/model")).toString().isEmpty());
    }

    // The cogs open it — the page's in Docked, the commit card's in Mini —
    // and close it again; the slot main() calls opens the one of the layout
    // of the moment, and none in the history.
    void theAgentCardOpensFromTheCogs()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(activate(f.window.get()));
        AgentScope scope(f.tools->path());
        AgentPopover *card = f.agentCard();
        QSignalSpy opened(card, &AgentPopover::opened);
        QToolButton *pageCog = f.page()->agentButton();

        QTest::mouseClick(pageCog, Qt::LeftButton);
        settle();
        QVERIFY(card->isVisible());
        QCOMPARE(card->anchor(), static_cast<QWidget *>(pageCog));
        QCOMPARE(opened.count(), 1);
        QTest::mouseClick(pageCog, Qt::LeftButton); // the cog toggles
        settle();
        QVERIFY(!card->isVisible());

        QVERIFY(QMetaObject::invokeMethod(f.window.get(), "showAgentMenu"));
        settle();
        QVERIFY(card->isVisible());
        QCOMPARE(card->anchor(), static_cast<QWidget *>(pageCog));
        const QRect geometry = card->geometry();
        QVERIFY(QMetaObject::invokeMethod(f.window.get(), "showAgentMenu")); // again: stays, same place
        settle();
        QVERIFY(card->isVisible());
        QCOMPARE(card->geometry(), geometry);
        QCOMPARE(f.window->findChildren<AgentPopover *>().size(), 1);
        card->dismiss();

        // Mini: from the commit card's cog.
        QVERIFY(f.openCard());
        QToolButton *cardCog = f.popover()->agentButton();
        QTest::mouseClick(cardCog, Qt::LeftButton);
        settle();
        QVERIFY(card->isVisible());
        QVERIFY(f.popover()->isVisible());
        QCOMPARE(card->anchor(), static_cast<QWidget *>(cardCog));
        QTest::mouseClick(cardCog, Qt::LeftButton);
        settle();
        QVERIFY(!card->isVisible());
        QVERIFY(f.popover()->isVisible());
        // The slot in Mini opens the commit card too.
        f.popover()->dismiss();
        settle();
        QVERIFY(QMetaObject::invokeMethod(f.window.get(), "showAgentMenu"));
        settle();
        QVERIFY(f.popover()->isVisible());
        QVERIFY(card->isVisible());
        QCOMPARE(card->anchor(), static_cast<QWidget *>(cardCog));
        QCOMPARE(card->x(), f.popover()->geometry().right() + 1 + ui::space(8)); // beside the commit card

        // The history has no cog.
        f.window->setMode(MainWindow::HistoryMode);
        settle();
        QVERIFY(!card->isVisible());
        QVERIFY(QMetaObject::invokeMethod(f.window.get(), "showAgentMenu"));
        settle();
        QVERIFY(!card->isVisible());
        QVERIFY(!f.popover()->isVisible());
    }

    // Under the cog, its right edge on the cog's, 360 wide where there is
    // room, clamped by the window's margins where there is not, moved up in a
    // short window, and placed again when its height changes.
    void theAgentCardHangsUnderItsCog()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(writeFakeClaude(f.tools->path()));
        QVERIFY(writeFakeCodex(f.tools->path()));
        AgentScope scope(f.tools->path(), QStringLiteral("claude"));
        AgentPopover *card = f.agentCard();
        QWidget *host = f.host();
        const QMargins margins = host->layout()->contentsMargins();
        QToolButton *cog = f.page()->agentButton();
        const auto cogRect = [&] { return rectIn(cog, host); };

        f.window->resize(945, 1234);
        settle();
        QTest::mouseClick(cog, Qt::LeftButton);
        settle();
        QVERIFY(card->isVisible());
        QCOMPARE(card->width(), ui::space(360));
        QCOMPARE(card->geometry().right(), cogRect().right());
        QCOMPARE(card->y(), cogRect().bottom() + 1 + ui::space(6));
        QCOMPARE(card->height(), card->sizeHint().height());

        // Another agent, another height; the top stays under the cog.
        const int claudeHeight = card->height();
        QVERIFY(card->agentPicker());
        QCOMPARE(card->agentPicker()->segments().size(), 2);
        QTest::mouseClick(card->agentPicker()->segments().last(), Qt::LeftButton);
        settle();
        QCOMPARE(savedAgent("agent/name"), QStringLiteral("codex"));
        QCOMPARE(rowTexts(card), QStringList({QStringLiteral("Default|whatever codex uses|*"), QStringLiteral("GPT-6-Astra|gpt-6-astra"),
                                              QStringLiteral("GPT-5.5|gpt-5.5")}));
        QCOMPARE(card->levelTrack()->labels().size(), 7);
        QVERIFY(card->height() != claudeHeight);
        QCOMPARE(card->height(), card->sizeHint().height());
        QCOMPARE(card->y(), cogRect().bottom() + 1 + ui::space(6));
        QCOMPARE(card->geometry().right(), cogRect().right());
        // A model with fewer levels, a shorter track; none, no track at all.
        QTest::mouseClick(modelRow(card, QStringLiteral("GPT-5.5")), Qt::LeftButton);
        settle();
        QCOMPARE(card->levelTrack()->labels(), QStringList({QStringLiteral("Default"), QStringLiteral("Low"), QStringLiteral("High")}));

        // Narrow: as wide as the margins allow, from the left margin. The top
        // bar keeps the window wider than that; a minimum of the test's own
        // lets it be squeezed anyway.
        f.window->setMinimumSize(1, 1);
        f.window->resize(300, 1234);
        settle();
        QTRY_COMPARE(card->width(), qMin(ui::space(360), host->width() - margins.left() - margins.right()));
        QCOMPARE(card->x(), qMax(margins.left(), cogRect().right() + 1 - card->width()));
        QVERIFY(host->width() < 360 + margins.left() + margins.right());
        QCOMPARE(card->x(), margins.left());

        // Short: moved up to fit, never above the top margin. Widening out of
        // the stacked width closes the card, so it is opened again first.
        f.window->resize(945, 360);
        settle();
        QVERIFY(!card->isVisible());
        QTest::mouseClick(cog, Qt::LeftButton);
        settle();
        QVERIFY(card->isVisible());
        QTRY_VERIFY(card->geometry().bottom() + 1 <= host->height() - margins.bottom()
                    || card->y() == margins.top());
        QVERIFY(card->y() < cogRect().bottom() + 1 + ui::space(6));
        QVERIFY(card->y() >= margins.top());
    }

    // Mini, from the commit card's cog: beside that card, 8 to its right and
    // level with it, never over it; where the room right of it is under 240,
    // under the cog again.
    void theAgentCardSitsBesideTheCommitCard()
    {
        QTemporaryDir dir, home;
        QVERIFY(dir.isValid() && home.isValid());
        QVERIFY(writeFixture(QDir(dir.path()).filePath(QStringLiteral("shell.toml")), "[font]\nbase-size = 12\n"));
        {
            ScopedEnv themeDir("OMAGIT_THEME_DIR", dir.path().toUtf8());
            ScopedEnv scratchHome("HOME", home.path().toUtf8());
            OmarchyTheme theme;
            QCOMPARE(theme.fontBase(), 12);
            theme.apply(*qApp);
            {
                WindowFixture f = mainWindow();
                QVERIFY(f.window);
                QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
                QVERIFY(writeFakeClaude(f.tools->path()));
                AgentScope scope(f.tools->path(), QStringLiteral("claude"));
                f.window->resize(945, 1234);
                settle();
                QVERIFY(f.openCard());
                CommitPopover *commitCard = f.popover();
                AgentPopover *card = f.agentCard();
                QWidget *host = f.host();
                const QMargins margins = host->layout()->contentsMargins();
                QToolButton *cog = commitCard->agentButton();
                QTest::mouseClick(cog, Qt::LeftButton);
                settle();
                QVERIFY(card->isVisible());
                QVERIFY(commitCard->isVisible());
                const QRect commitRect = commitCard->geometry();
                QCOMPARE(card->x(), commitRect.right() + 1 + ui::space(8));
                QVERIFY(card->y() <= commitRect.y());
                QVERIFY2(!card->geometry().intersects(commitRect),
                         qPrintable(QStringLiteral("%1,%2 %3x%4 / %5,%6 %7x%8")
                                        .arg(card->x()).arg(card->y()).arg(card->width()).arg(card->height())
                                        .arg(commitRect.x()).arg(commitRect.y()).arg(commitRect.width()).arg(commitRect.height())));
                QCOMPARE(card->width(), ui::space(360));
                QCOMPARE(card->height(), card->sizeHint().height());

                // Narrower, until the room right of the commit card is under
                // 240: under the cog, its right edge on the cog's, and moved
                // up as far as the window's bottom margin asks.
                f.window->setMinimumSize(1, 1);
                const auto room = [&] {
                    return host->width() - margins.right() - (commitCard->geometry().right() + 1 + ui::space(8));
                };
                for (int width = 945; width > 300 && room() >= ui::space(240); width -= 10) {
                    f.window->resize(width, 1234);
                    settle();
                }
                QVERIFY(room() < ui::space(240));
                QVERIFY(commitCard->isVisible());
                QVERIFY(card->isVisible());
                const QRect cogRect = rectIn(cog, host);
                const int under = cogRect.bottom() + 1 + ui::space(6);
                const int bottom = host->height() - margins.bottom();
                QTRY_COMPARE(card->y(), under + card->height() > bottom ? qMax(margins.top(), bottom - card->height()) : under);
                QCOMPARE(card->x(), qMax(margins.left(), cogRect.right() + 1 - card->width()));
                QCOMPARE(card->width(), qMin(ui::space(360), host->width() - margins.left() - margins.right()));
            }
        }
        g_theme.reset(new OmarchyTheme);
        g_theme->apply(*qApp);
        QVERIFY(OmarchyTheme::instance() == g_theme.get());
    }

    // The two cards together: a press on the agent card leaves the commit
    // card open; whatever closes the commit card, the history, the layout
    // switch and another repository close the agent card; a press on the
    // message box closes it and goes on to the box.
    void theAgentCardLivesWithTheCommitCard()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(activate(f.window.get()));
        AgentScope scope(f.tools->path());
        AgentPopover *card = f.agentCard();
        CommitPopover *commitCard = f.popover();
        const auto openBoth = [&] {
            QVERIFY(f.openCard());
            QTest::mouseClick(commitCard->agentButton(), Qt::LeftButton);
            settle();
            QVERIFY(card->isVisible());
        };

        openBoth();
        // A press on the agent card, where nothing but the card is.
        const QPoint inside = card->mapTo(f.window.get(), QPoint(card->width() - 4, card->height() - 4));
        clickAt(f.window.get(), inside);
        settle();
        QVERIFY(card->isVisible());
        QVERIFY(commitCard->isVisible());
        QTest::mouseClick(card->copyButtons().first(), Qt::LeftButton);
        settle();
        QVERIFY(commitCard->isVisible());

        // Escape from the commit card's editor closes both.
        commitCard->editor()->setFocus();
        QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget *>(commitCard->editor()));
        QVERIFY(card->isVisible());
        QTest::keyClick(commitCard->editor(), Qt::Key_Escape);
        settle();
        QVERIFY(!commitCard->isVisible());
        QVERIFY(!card->isVisible());
        QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget *>(f.rail()->list()));

        // The tile.
        openBoth();
        QTest::mouseClick(f.tile(), Qt::LeftButton);
        settle();
        QVERIFY(!commitCard->isVisible());
        QVERIFY(!card->isVisible());

        // The history.
        openBoth();
        QTest::mouseClick(f.bar()->historyTab(), Qt::LeftButton);
        settle();
        QVERIFY(!card->isVisible());
        f.window->setMode(MainWindow::CommitMode);
        settle();

        // Ctrl+B, from Mini to Docked and back.
        openBoth();
        QTest::keyClick(commitCard->editor(), Qt::Key_B, Qt::ControlModifier);
        settle();
        QCOMPARE(f.window->paneLayout(), PaneLayout::Docked);
        QVERIFY(!card->isVisible());
        QTest::mouseClick(f.page()->agentButton(), Qt::LeftButton);
        settle();
        QVERIFY(card->isVisible());
        QTest::keyClick(card, Qt::Key_B, Qt::ControlModifier);
        settle();
        QCOMPARE(f.window->paneLayout(), PaneLayout::Mini);
        QVERIFY(!card->isVisible());
        // Saved, as the keys save it, so the windows of the tests after this
        // one start Docked again.
        f.window->setPaneLayout(PaneLayout::Docked);
        settle();

        // A press on the message box: closed, and the box has the keyboard.
        QTest::mouseClick(f.page()->agentButton(), Qt::LeftButton);
        settle();
        QVERIFY(card->isVisible());
        MessageEdit *box = f.pageEditor();
        clickAt(f.window.get(), box->mapTo(f.window.get(), QPoint(ui::space(20), box->height() - ui::space(10))));
        settle();
        QVERIFY(!card->isVisible());
        QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget *>(box));

        // Another repository.
        QTest::mouseClick(f.page()->agentButton(), Qt::LeftButton);
        settle();
        QVERIFY(card->isVisible());
        QTemporaryDir other;
        QVERIFY(other.isValid());
        QVERIFY(git(other.path(), {"init", "-q", "-b", "main"}));
        QVERIFY(commit(other.path(), QStringLiteral("other"), 1));
        QVERIFY(f.window->openRepository(other.path()));
        settle();
        QVERIFY(!card->isVisible());
    }

    // A refresh under an open agent card — F5, and the file watcher after an
    // edit — in Docked and in Mini: the card (and the commit card) stay, the
    // other-model field keeps its unsaved text and the keyboard, nothing is
    // saved, and the list did refresh.
    void theAgentCardSurvivesARefresh()
    {
        for (const bool mini : {false, true}) {
            WindowFixture f = mainWindow();
            QVERIFY(f.window);
            QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
            QVERIFY(activate(f.window.get()));
            QVERIFY(writeFakeClaude(f.tools->path()));
            AgentScope scope(f.tools->path());
            AgentPopover *card = f.agentCard();
            if (mini) {
                QVERIFY(f.openCard());
                QTest::mouseClick(f.popover()->agentButton(), Qt::LeftButton);
            } else {
                QTest::mouseClick(f.page()->agentButton(), Qt::LeftButton);
            }
            settle();
            QVERIFY(card->isVisible());
            QTest::mouseClick(card->otherModelButton(), Qt::LeftButton);
            settle();
            QLineEdit *field = card->otherModelField();
            QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget *>(field));
            QTest::keyClicks(field, QStringLiteral("claude-x"));
            const auto unchanged = [&] {
                return card->isVisible() && (!mini || f.popover()->isVisible()) && card->otherModelField() == field
                    && field->isVisible() && field->text() == QStringLiteral("claude-x")
                    && QApplication::focusWidget() == field && !QSettings().childGroups().contains(QStringLiteral("agent"));
            };
            QVERIFY(unchanged());
            QAbstractItemModel *rows = f.page()->proxy();
            const int before = rows->rowCount();

            // F5, over a file that was not there.
            QVERIFY(writeFixture(QDir(f.repo->root()).filePath(QStringLiteral("zz-new.txt")), "new\n"));
            QTest::keyClick(field, Qt::Key_F5);
            settle();
            QCOMPARE(rows->rowCount(), before + 1);
            QVERIFY2(unchanged(), mini ? "Mini, F5" : "Docked, F5");

            // The watcher: a.txt written back as committed drops out of the
            // list once the debounce is over.
            QVERIFY(writeFixture(QDir(f.repo->root()).filePath(QStringLiteral("a.txt")), "a\n"));
            QTRY_COMPARE_WITH_TIMEOUT(rows->rowCount(), before, 10000);
            settle();
            QVERIFY2(unchanged(), mini ? "Mini, watcher" : "Docked, watcher");
            if (mini) {
                // Saved as Docked again for the tests after this one.
                f.window->setPaneLayout(PaneLayout::Docked);
                settle();
            }
        }
    }

    // The keyboard is on the card the moment it opens, so Escape closes it at
    // once; closing gives the keyboard back to where it was.
    void theAgentCardTakesAndReturnsTheKeyboard()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(activate(f.window.get()));
        AgentScope scope(f.tools->path());
        AgentPopover *card = f.agentCard();
        QCOMPARE(f.window->paneLayout(), PaneLayout::Docked);

        f.pageEditor()->setFocus();
        QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget *>(f.pageEditor()));
        QTest::mouseClick(f.page()->agentButton(), Qt::LeftButton);
        settle();
        QVERIFY(card->isVisible());
        QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget *>(card));
        QTest::keyClick(card, Qt::Key_Escape);
        settle();
        QVERIFY(!card->isVisible());
        QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget *>(f.pageEditor()));

        QVERIFY(f.openCard());
        QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget *>(f.popover()->editor()));
        QTest::mouseClick(f.popover()->agentButton(), Qt::LeftButton);
        settle();
        QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget *>(card));
        QTest::keyClick(card, Qt::Key_Escape);
        settle();
        QVERIFY(!card->isVisible());
        QVERIFY(f.popover()->isVisible()); // one Escape, one card
        QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget *>(f.popover()->editor()));
    }

    // --screenshot-menu agent opens the card under the cog of the layout it
    // pictures, and writes no agent setting.
    void theScreenshotMenuOpensTheAgentCard()
    {
        const QString binary = helperBinary();
        if (binary.isEmpty())
            QSKIP("omagit is not built (qmake6 omagit.pro && make)");
        QTemporaryDir repoDir, themeDir, home, tools, work;
        QVERIFY(repoDir.isValid() && themeDir.isValid() && home.isValid() && tools.isValid() && work.isValid());
        const QString repo = repoDir.path();
        QVERIFY(git(repo, {"init", "-q", "-b", "main"}));
        QVERIFY(writeFixture(QDir(repo).filePath(QStringLiteral("a.txt")), "a\n"));
        QVERIFY(git(repo, {"add", "-A"}));
        QVERIFY(git(repo, {"commit", "-q", "-m", "first"}, 1));
        QVERIFY(writeFixture(QDir(repo).filePath(QStringLiteral("a.txt")), "a changed\n"));
        QVERIFY(writeFixture(QDir(themeDir.path()).filePath(QStringLiteral("colors.toml")),
                             "accent = \"#ff00ff\"\nbackground = \"#101010\"\nforeground = \"#eeeeee\"\n"));
        const QString gitBinary = QStandardPaths::findExecutable(QStringLiteral("git"));
        QVERIFY(QFile::link(gitBinary, QDir(tools.path()).filePath(QStringLiteral("git"))));
        QVERIFY(writeFakeClaude(tools.path()));

        int run = 0;
        const auto shoot = [&](const QStringList &flags, QStringList *keys) {
            const QString config = QDir(work.path()).filePath(QStringLiteral("config%1").arg(++run));
            const QString png = QDir(work.path()).filePath(QStringLiteral("shot%1.png").arg(run));
            QProcess p;
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert(QStringLiteral("HOME"), home.path());
            env.insert(QStringLiteral("XDG_CONFIG_HOME"), config);
            env.insert(QStringLiteral("OMAGIT_THEME_DIR"), themeDir.path());
            env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
            env.insert(QStringLiteral("PATH"), tools.path());
            p.setProcessEnvironment(env);
            p.start(binary, QStringList{QStringLiteral("--no-fetch"), QStringLiteral("--screenshot"), png,
                                        QStringLiteral("--screenshot-size"), QStringLiteral("945x1234")}
                                + flags + QStringList{repo});
            if (!p.waitForFinished(30000) || p.exitCode() != 0)
                return QImage();
            *keys = QSettings(QDir(config).filePath(QStringLiteral("omagit/omagit.conf")), QSettings::IniFormat).allKeys();
            keys->sort();
            return QImage(png);
        };
        // The columns an accent frame runs down for 150 px or more: two per
        // side of each open card.
        const auto frameColumns = [](const QImage &image) {
            int columns = 0;
            for (int x = 0; x < image.width(); ++x) {
                int longest = 0, runLength = 0;
                for (int y = 0; y < image.height(); ++y) {
                    runLength = image.pixelColor(x, y) == QColor(QStringLiteral("#ff00ff")) ? runLength + 1 : 0;
                    longest = qMax(longest, runLength);
                }
                if (longest >= 150)
                    ++columns;
            }
            return columns;
        };
        const QString menu = QStringLiteral("--screenshot-menu");
        QStringList plainKeys, keys;
        const QImage plain = shoot({}, &plainKeys);
        QVERIFY(!plain.isNull());
        QCOMPARE(frameColumns(plain), 0);
        const QImage docked = shoot({menu, QStringLiteral("agent")}, &keys);
        QVERIFY(!docked.isNull());
        QCOMPARE(frameColumns(docked), 4);
        QCOMPARE(keys, plainKeys);
        QVERIFY(keys.filter(QStringLiteral("agent")).isEmpty());

        const QImage commitOnly = shoot({QStringLiteral("--mini"), menu, QStringLiteral("commit")}, &keys);
        QVERIFY(!commitOnly.isNull());
        const QImage mini = shoot({QStringLiteral("--mini"), menu, QStringLiteral("agent")}, &keys);
        QVERIFY(!mini.isNull());
        QVERIFY2(frameColumns(mini) > frameColumns(commitOnly),
                 qPrintable(QStringLiteral("%1 %2").arg(frameColumns(mini)).arg(frameColumns(commitOnly))));
        QVERIFY(keys.filter(QStringLiteral("agent")).isEmpty());
        const QImage history = shoot({QStringLiteral("--history"), menu, QStringLiteral("agent")}, &keys);
        QVERIFY(!history.isNull());
        QCOMPARE(frameColumns(history), 0);
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
        QList<QToolButton *> squares = page.findChildren<QToolButton *>(QStringLiteral("iconButton"));
        // the agent cog, the three files-view buttons, the unversioned eye and
        // Refresh; the stacked action bar's options button is a toolbar one
        squares.removeOne(page.optionsButton());
        QCOMPARE(squares.size(), 6);
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

    // --- the files-view switcher --------------------------------------------

    // The tree is an index of the flat list, not a list of its own: the same
    // files, under their directories, in the order the proxy has them.
    void theFilesTreeMirrorsTheFlatList()
    {
        CommitFixture f = nestedFixture();
        QVERIFY(f.page);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        ChangesTreeModel *tree = f.treeModel();
        QVERIFY(tree);
        QAbstractItemModelTester tester(tree, QAbstractItemModelTester::FailureReportingMode::Warning);

        // Directories first and alphabetically whatever their case, then the
        // files of that level in the flat order (Status: modified first,
        // untracked last). A chain of single children is not compressed.
        QCOMPARE(f.treeRows(),
                 QStringList({"0 alpha", "1 alpha/y.txt", "0 Beta", "1 Beta/x.txt", "0 single",
                              "1 single/one.txt", "0 src", "1 src/Deep", "2 src/Deep/c.txt", "1 src/deep",
                              "2 src/deep/b.txt", "1 src/a.txt", "0 tests", "1 tests/remove.txt",
                              "1 tests/new.txt", "0 root.txt", "0 top.txt"}));
        QCOMPARE(tree->columnCount(), int(ChangesTreeModel::ColumnCount));
        QCOMPARE(tree->headerData(ChangesTreeModel::Name, Qt::Horizontal, Qt::DisplayRole).toString(),
                 QStringLiteral("Name"));
        QCOMPARE(tree->headerData(ChangesTreeModel::Status, Qt::Horizontal, Qt::DisplayRole).toString(),
                 QStringLiteral("St"));

        // A file row is its flat row, both ways round; a directory is never
        // any file, however its path is spelled.
        for (const QString path : {"src/a.txt", "src/deep/b.txt", "root.txt", "tests/new.txt"}) {
            const QModelIndex index = tree->indexForPath(path);
            QVERIFY2(index.isValid(), qPrintable(path));
            QVERIFY(!tree->isDirectory(index));
            const QModelIndex flat = tree->mapToSource(index);
            QVERIFY(flat.isValid());
            QCOMPARE(flat.data(ChangesModel::PathRole).toString(), path);
            QCOMPARE(tree->mapFromSource(flat), index);
            QCOMPARE(index.data(ChangesModel::PathRole).toString(), path);
            QCOMPARE(index.siblingAtColumn(ChangesTreeModel::Name).data(Qt::DisplayRole).toString(),
                     QString(path).section(QLatin1Char('/'), -1));
        }
        const QModelIndex dir = tree->indexForDirectory(QStringLiteral("src"));
        QVERIFY(tree->isDirectory(dir));
        QVERIFY(!tree->indexForPath(QStringLiteral("src")).isValid()); // no file spells it
        QVERIFY(!tree->mapToSource(dir).isValid());
        QCOMPARE(tree->fileCount(dir), 3);
        QCOMPARE(tree->fileCount(tree->indexForDirectory(QStringLiteral("single"))), 1);
        // Status carries no text and no kind on a directory, so no pill.
        QVERIFY(!dir.siblingAtColumn(ChangesTreeModel::Status).data(ChangesModel::KindRole).isValid());
        QVERIFY(dir.siblingAtColumn(ChangesTreeModel::Status).data(Qt::DisplayRole).toString().isEmpty());
        QVERIFY(!dir.siblingAtColumn(ChangesTreeModel::Status).data(Qt::CheckStateRole).isValid());
        // The status letters both new presentations paint.
        QCOMPARE(ChangesModel::statusLetter(FileChange::Deleted), QChar('D'));
        QCOMPARE(ChangesModel::statusLetter(FileChange::Untracked), QChar('?'));
        QCOMPARE(ChangesModel::statusLetter(FileChange::Unmerged), QChar('!'));

        // Sorting is the flat list's: entering the tree leaves the table's own
        // sort alone, even on a column the tree has not got.
        f.page->proxy()->sort(ChangesModel::Size, Qt::DescendingOrder);
        f.page->setFilesView(CommitPage::FilesView::Tree, false);
        settle();
        QCOMPARE(f.page->proxy()->sortColumn(), int(ChangesModel::Size));
        QCOMPARE(f.page->proxy()->sortOrder(), Qt::DescendingOrder);

        // The tree's own sections route to the flat columns behind them, and
        // clicking one again turns the order around.
        QHeaderView *header = f.treeHeader();
        QVERIFY(header);
        const auto clickTreeSection = [header](int section) {
            const QPoint centre(header->sectionViewportPosition(section) + header->sectionSize(section) / 2,
                                header->viewport()->height() / 2);
            QTest::mouseClick(header->viewport(), Qt::LeftButton, {}, centre);
        };
        clickTreeSection(ChangesTreeModel::Name);
        QCOMPARE(f.page->proxy()->sortColumn(), int(ChangesModel::Name));
        QCOMPARE(f.page->proxy()->sortOrder(), Qt::AscendingOrder);
        clickTreeSection(ChangesTreeModel::Name);
        QCOMPARE(f.page->proxy()->sortOrder(), Qt::DescendingOrder);
        clickTreeSection(ChangesTreeModel::Status);
        QCOMPARE(f.page->proxy()->sortColumn(), int(ChangesModel::Status));

        // The sort a tree section makes is the table's own, indicator and all:
        // back in the table, the same section goes on from there rather than
        // starting again at the top.
        clickTreeSection(ChangesTreeModel::Name);
        QCOMPARE(f.header()->sortIndicatorSection(), int(ChangesModel::Name));
        QCOMPARE(f.header()->sortIndicatorOrder(), Qt::AscendingOrder);
        f.page->setFilesView(CommitPage::FilesView::Table, false);
        settle();
        f.clickSection(ChangesModel::Name);
        QCOMPARE(f.page->proxy()->sortColumn(), int(ChangesModel::Name));
        QCOMPARE(f.page->proxy()->sortOrder(), Qt::DescendingOrder);
        f.page->setFilesView(CommitPage::FilesView::Tree, false);
        settle();

        // ...and the column of checkboxes sorts nothing; it checks everything.
        f.model()->setAllChecked(false);
        clickTreeSection(ChangesTreeModel::Check);
        QCOMPARE(f.page->proxy()->sortColumn(), int(ChangesModel::Name));
        QCOMPARE(f.model()->checkedCount(), f.page->proxy()->rowCount());
    }

    // A directory's box stands for every file under it, open or folded away,
    // and writing it goes through the one list of check marks there is.
    void theFilesTreeChecksWholeDirectories()
    {
        CommitFixture f = nestedFixture();
        QVERIFY(f.page);
        f.page->setFilesView(CommitPage::FilesView::Tree, false);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        ChangesTreeModel *tree = f.treeModel();

        // All three of src's files are modified, so they start checked; tests
        // has a deletion (checked) beside an untracked file (not).
        QCOMPARE(f.treeCheck("src"), int(Qt::Checked));
        QCOMPARE(f.treeCheck("tests"), int(Qt::PartiallyChecked));
        QCOMPARE(f.treeCheck("tests/new.txt"), int(Qt::Unchecked));

        // Partial checks everything under it; checked clears it again.
        QVERIFY(tree->setData(f.treeIndex("tests"), Qt::Checked, Qt::CheckStateRole));
        QCOMPARE(f.treeCheck("tests"), int(Qt::Checked));
        QVERIFY(f.checkedPaths().contains(QStringLiteral("tests/new.txt")));
        QVERIFY(tree->setData(f.treeIndex("tests"), Qt::Unchecked, Qt::CheckStateRole));
        QCOMPARE(f.treeCheck("tests"), int(Qt::Unchecked));
        QVERIFY(!f.checkedPaths().contains(QStringLiteral("tests/remove.txt")));

        // A directory deeper down takes its ancestors with it, as far as the
        // state goes: src becomes partial the moment one branch of it is.
        QVERIFY(tree->setData(f.treeIndex("src/deep"), Qt::Unchecked, Qt::CheckStateRole));
        QCOMPARE(f.treeCheck("src/deep"), int(Qt::Unchecked));
        QCOMPARE(f.treeCheck("src"), int(Qt::PartiallyChecked));
        QCOMPARE(f.treeCheck("src/Deep"), int(Qt::Checked));

        // Folded away is still under it: collapsing changes no count and no
        // check, and checking the parent reaches the hidden files all the same.
        const QString title = f.title();
        f.tree()->collapse(f.treeIndex("src"));
        QCOMPARE(f.title(), title);
        QVERIFY(tree->setData(f.treeIndex("src"), Qt::Checked, Qt::CheckStateRole));
        QCOMPARE(f.treeCheck("src/deep"), int(Qt::Checked));
        QVERIFY(f.checkedPaths().contains(QStringLiteral("src/deep/b.txt")));
        f.tree()->expand(f.treeIndex("src"));

        // A file goes through the flat proxy's own check index.
        QVERIFY(tree->setData(f.treeIndex("src/a.txt"), Qt::Unchecked, Qt::CheckStateRole));
        QVERIFY(!f.checkedPaths().contains(QStringLiteral("src/a.txt")));
        QCOMPARE(f.treeCheck("src"), int(Qt::PartiallyChecked));

        // Space acts on the row, from whichever column the keyboard is in.
        f.tree()->setFocus();
        for (const int column : {int(ChangesTreeModel::Name), int(ChangesTreeModel::Status)}) {
            f.tree()->setCurrentIndex(f.treeIndex("alpha", column));
            const int before = f.model()->checkedCount();
            QTest::keyClick(f.tree(), Qt::Key_Space);
            QCOMPARE(f.model()->checkedCount(), before - 1);
            QTest::keyClick(f.tree(), Qt::Key_Space);
            QCOMPARE(f.model()->checkedCount(), before);
        }

        // Check-all is the proxy's, over the files it is showing, and the
        // tree only puts the box in a section of its own.
        f.model()->setAllChecked(false);
        QCOMPARE(tree->headerData(ChangesTreeModel::Check, Qt::Horizontal, Qt::CheckStateRole).toInt(),
                 int(Qt::Unchecked));
        QVERIFY(tree->setHeaderData(ChangesTreeModel::Check, Qt::Horizontal, int(Qt::Checked), Qt::CheckStateRole));
        QCOMPARE(f.model()->checkedCount(), f.page->proxy()->rowCount());
        QVERIFY(!tree->headerData(ChangesTreeModel::Name, Qt::Horizontal, Qt::CheckStateRole).isValid());
        QVERIFY(tree->headerData(ChangesTreeModel::Check, Qt::Horizontal, Qt::DisplayRole).toString().isEmpty());

        // Nothing the eye hides is in the tree at all, so nothing a directory
        // of it checks can be hidden either.
        f.model()->setAllChecked(false);
        f.eye()->click();
        settle();
        QVERIFY(!tree->indexForPath(QStringLiteral("tests/new.txt")).isValid());
        QVERIFY(tree->setData(f.treeIndex("tests"), Qt::Checked, Qt::CheckStateRole));
        QCOMPARE(f.checkedPaths(), QStringList({QStringLiteral("tests/remove.txt")}));

        // An empty list is an empty tree, with nothing to check.
        f.model()->setChanges({});
        settle();
        QCOMPARE(tree->rowCount(), 0);
    }

    // Zero indentation and no root decoration do not stop Qt from walking the
    // tree: it works on the row, whichever column the keyboard is in.
    void theFilesTreeWalksWithTheKeyboard()
    {
        CommitFixture f = nestedFixture();
        QVERIFY(f.page);
        f.page->setFilesView(CommitPage::FilesView::Tree, false);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        QTreeView *tree = f.tree();
        QCOMPARE(tree->indentation(), 0);
        QVERIFY(!tree->rootIsDecorated());
        QCOMPARE(tree->objectName(), QStringLiteral("changesTree"));
        tree->setFocus();

        for (const int column : {int(ChangesTreeModel::Check), int(ChangesTreeModel::Status)}) {
            const QModelIndex src = f.treeIndex("src");
            tree->setCurrentIndex(f.treeIndex("src", column));
            QVERIFY(tree->isExpanded(src));
            QTest::keyClick(tree, Qt::Key_Left);
            QVERIFY(!tree->isExpanded(src));
            QTest::keyClick(tree, Qt::Key_Right);
            QVERIFY(tree->isExpanded(src));
        }

        // Down walks into the branch, Left from a leaf comes back to it.
        tree->setCurrentIndex(f.treeIndex("src"));
        QTest::keyClick(tree, Qt::Key_Down);
        QCOMPARE(f.treeModel()->path(tree->currentIndex()), QStringLiteral("src/Deep"));
        QTest::keyClick(tree, Qt::Key_Left); // an open directory folds first
        QTest::keyClick(tree, Qt::Key_Left); // ...and then hands over to its parent
        QCOMPARE(f.treeModel()->path(tree->currentIndex()), QStringLiteral("src"));

        // Space on a directory in the status column checks what is under it
        // and leaves the canonical current file where it was.
        QVERIFY(f.page->selectPath(QStringLiteral("root.txt")));
        const QString canonical = f.page->table()->currentIndex().data(ChangesModel::PathRole).toString();
        QSignalSpy current(f.page.get(), &CommitPage::currentRowChanged);
        tree->setCurrentIndex(f.treeIndex("alpha", ChangesTreeModel::Status));
        QTest::keyClick(tree, Qt::Key_Space);
        QVERIFY(!f.checkedPaths().contains(QStringLiteral("alpha/y.txt")));
        QCOMPARE(f.page->table()->currentIndex().data(ChangesModel::PathRole).toString(), canonical);
        QCOMPARE(current.count(), 0);
    }

    // The chevron and the folder open the branch; the box beside them makes
    // check marks. Neither ever does the other's job.
    void theFilesTreeOpensOnItsChevronAndChecksOnItsBox()
    {
        CommitFixture f = nestedFixture();
        QVERIFY(f.page);
        f.page->setFilesView(CommitPage::FilesView::Tree, false);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        const QModelIndex src = f.treeIndex("src");
        const int checked = f.model()->checkedCount();

        // 6..34 design px into the Name cell is the chevron and the folder.
        f.clickTreeCell(QStringLiteral("src"), ChangesTreeModel::Name,
                        QPoint(ui::space(12), f.tree()->visualRect(src).height() / 2));
        QVERIFY(!f.tree()->isExpanded(src));
        QCOMPARE(f.model()->checkedCount(), checked);
        f.clickTreeCell(QStringLiteral("src"), ChangesTreeModel::Name,
                        QPoint(ui::space(12), f.tree()->visualRect(src).height() / 2));
        QVERIFY(f.tree()->isExpanded(src));
        QCOMPARE(f.model()->checkedCount(), checked);

        // Only the left button works the chevron; the others are Qt's, and a
        // directory neither opens nor closes under them.
        for (const Qt::MouseButton button : {Qt::RightButton, Qt::MiddleButton}) {
            const QRect cell = f.tree()->visualRect(f.treeIndex("src", ChangesTreeModel::Name));
            const QPoint on(cell.left() + ui::space(12), cell.center().y());
            QTest::mousePress(f.tree()->viewport(), button, {}, on);
            QTest::mouseRelease(f.tree()->viewport(), button, {}, on);
            QVERIFY2(f.tree()->isExpanded(src), button == Qt::RightButton ? "right button" : "middle button");
            QCOMPARE(f.model()->checkedCount(), checked);
        }

        // The box: the marks move and the branch stays as it was.
        f.clickTreeCell(QStringLiteral("src"), ChangesTreeModel::Check);
        QVERIFY(f.tree()->isExpanded(src));
        QCOMPARE(f.model()->checkedCount(), checked - 3);
        f.clickTreeCell(QStringLiteral("src"), ChangesTreeModel::Check);
        QCOMPARE(f.model()->checkedCount(), checked);

        // A gesture that starts on the chevron opens the branch once, not
        // twice: the press has already done it, so the double click that
        // follows is eaten rather than closing it again.
        QSignalSpy opened(f.page.get(), &CommitPage::openRequested);
        QSignalSpy diff(f.page.get(), &CommitPage::showDiffPaneRequested);
        const QRect name = f.tree()->visualRect(f.treeIndex("src", ChangesTreeModel::Name));
        const QPoint chevron(name.left() + ui::space(12), name.center().y());
        QTest::mousePress(f.tree()->viewport(), Qt::LeftButton, {}, chevron);
        QTest::mouseRelease(f.tree()->viewport(), Qt::LeftButton, {}, chevron);
        QTest::mouseDClick(f.tree()->viewport(), Qt::LeftButton, {}, chevron);
        QVERIFY(!f.tree()->isExpanded(src));
        QCOMPARE(f.model()->checkedCount(), checked);

        for (const QPoint pos : {name.center(), chevron,
                                 QPoint(f.tree()->visualRect(src).right() - 2, name.center().y())}) {
            QTest::mouseDClick(f.tree()->viewport(), Qt::LeftButton, {}, pos);
            QCOMPARE(f.model()->checkedCount(), checked);
        }
        f.tree()->expand(src);

        // What a double-click means, taken at the signal: opening and closing
        // the branch is all a directory's ever does, while a file's is the
        // table's rule — open it, or show the diff where the pane is hidden.
        // (The synthetic events QtTest sends never reach QTreeView's own
        // double-click handling, in this tree or in a plain one.)
        const auto doubleClick = [&f](const QString &path) {
            QMetaObject::invokeMethod(f.tree(), "doubleClicked",
                                      Q_ARG(QModelIndex, f.treeIndex(path, ChangesTreeModel::Name)));
        };
        doubleClick(QStringLiteral("src"));
        QCOMPARE(opened.count(), 0);
        QCOMPARE(diff.count(), 0);
        doubleClick(QStringLiteral("root.txt"));
        QCOMPARE(opened.count(), 1);
        QCOMPARE(diff.count(), 0);
        f.page->setDiffPaneVisible(false);
        doubleClick(QStringLiteral("root.txt"));
        QCOMPARE(opened.count(), 1);
        QCOMPARE(diff.count(), 1);
        f.page->setDiffPaneVisible(true);
    }

    // A rename's source path may spell another file's current path: checking
    // a directory must take the files in it and nothing else.
    void checkingADirectoryLeavesARenamedFileAlone()
    {
        CommitFixture f = commitFixture();
        QVERIFY(f.page);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        // b/f.txt was renamed from a/f.txt, and an untracked file of its own
        // now stands at that very path. The overlap is what matters here, so
        // the list is given to the model outright.
        FileChange rename = change(QStringLiteral("b/f.txt"), FileChange::Renamed);
        rename.oldPath = QStringLiteral("a/f.txt");
        f.model()->setChanges({change(QStringLiteral("a/keep.txt"), FileChange::Modified),
                               change(QStringLiteral("a/f.txt"), FileChange::Untracked), rename});
        settle();
        QCOMPARE(f.page->proxy()->rowCount(), 3);
        QVERIFY(f.treeModel()->indexForPath(QStringLiteral("b/f.txt")).isValid());

        // The directory takes the two files in it, not the one whose old path
        // one of them happens to spell.
        f.model()->setAllChecked(false);
        QVERIFY(f.treeModel()->setData(f.treeIndex("a"), Qt::Checked, Qt::CheckStateRole));
        QCOMPARE(f.checkedPaths(), QStringList({QStringLiteral("a/f.txt"), QStringLiteral("a/keep.txt")}));
        QCOMPARE(f.treeCheck("b"), int(Qt::Unchecked));

        // The check-all box over the shown rows is exact in the same way: the
        // eye takes the untracked a/f.txt out, and nothing puts it back.
        f.model()->setAllChecked(false);
        f.eye()->click();
        settle();
        QCOMPARE(f.page->proxy()->rowCount(), 2);
        f.page->toggleAllChecked();
        QCOMPARE(f.model()->checkedCount(), 2);
        for (const FileChange &c : f.model()->checkedChanges())
            QVERIFY2(!c.isUntracked(), qPrintable(c.path));

        // Amending still matches the source of a rename on purpose: HEAD knows
        // the file by the path it had before, so both rows answer to it.
        f.eye()->click();
        settle();
        f.model()->setAllChecked(false);
        f.model()->setPathsChecked({QStringLiteral("a/f.txt")}, true);
        QCOMPARE(f.checkedPaths(), QStringList({QStringLiteral("a/f.txt"), QStringLiteral("a/f.txt"),
                                                QStringLiteral("b/f.txt")}));
    }

    // Check marks, colours and a theme change are repaints, not rebuilds: only
    // rows coming, going or being sorted make the tree again.
    void theFilesTreeIsRebuiltOnlyByTheShapeOfTheList()
    {
        CommitFixture f = nestedFixture();
        QVERIFY(f.page);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        QSignalSpy rebuilt(f.treeModel(), &QAbstractItemModel::modelReset);
        QSignalSpy changed(f.treeModel(), &QAbstractItemModel::dataChanged);

        f.model()->setAllChecked(true);
        QCOMPARE(rebuilt.count(), 0);
        QVERIFY(changed.count() > 0);
        f.model()->setPathsChecked({QStringLiteral("src/a.txt")}, false);
        QCOMPARE(rebuilt.count(), 0);

        // An unchanged reload changes nothing at all, eye or no eye.
        f.page->reload();
        QCOMPARE(rebuilt.count(), 0);
        f.eye()->click(); // off: the filter is a layout change, so one rebuild
        settle();
        QCOMPARE(rebuilt.count(), 1);
        f.page->reload();
        QCOMPARE(rebuilt.count(), 1);
        f.eye()->click();
        settle();
        QCOMPARE(rebuilt.count(), 2);

        // Sorting is a layout change of the proxy: the files come back in the
        // new order, under the same directories.
        f.clickSection(ChangesModel::Name);
        QVERIFY(rebuilt.count() > 2);
        QVERIFY(f.treeRows().contains(QStringLiteral("0 src")));

        // Rows really coming and going do rebuild it.
        const int before = rebuilt.count();
        QVERIFY(writeFixture(QDir(f.dir->path()).filePath(QStringLiteral("late.txt")), "late\n"));
        f.page->reload();
        QVERIFY(rebuilt.count() > before);
        QVERIFY(f.treeModel()->indexForPath(QStringLiteral("late.txt")).isValid());
    }

    // Which directories are folded away is this session's, by path: it
    // survives sorting, filtering and reloads, and counts nothing.
    void collapsedDirectoriesSurviveAndCountTheirFiles()
    {
        CommitFixture f = nestedFixture();
        QVERIFY(f.page);
        f.page->setFilesView(CommitPage::FilesView::Tree, false);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        // Everything starts open.
        for (const QString dir : {"alpha", "src", "src/deep", "tests"})
            QVERIFY2(f.tree()->isExpanded(f.treeIndex(dir)), qPrintable(dir));
        QCOMPARE(f.treeModel()->fileCount(f.treeIndex("single")), 1);
        QCOMPARE(f.treeModel()->fileCount(f.treeIndex("tests")), 2);

        const QString title = f.title();
        const QStringList checked = f.checkedPaths();
        f.tree()->collapse(f.treeIndex("src/deep"));
        f.tree()->collapse(f.treeIndex("tests"));
        QCOMPARE(f.title(), title);
        QCOMPARE(f.checkedPaths(), checked);
        QCOMPARE(f.page->proxy()->rowCount(), 10); // collapse hides nothing from the counts

        const auto stillFolded = [&f] {
            QVERIFY(!f.tree()->isExpanded(f.treeIndex("src/deep")));
            QVERIFY(!f.tree()->isExpanded(f.treeIndex("tests")));
            QVERIFY(f.tree()->isExpanded(f.treeIndex("src")));
        };
        f.page->proxy()->sort(ChangesModel::Name, Qt::AscendingOrder); // sorting
        stillFolded();
        f.eye()->click();                   // filtering
        settle();
        stillFolded();
        f.eye()->click();
        settle();
        stillFolded();
        f.page->reload();                   // a real reload
        stillFolded();
        // ...and a directory that leaves the list and comes back with it.
        QVERIFY(writeFixture(QDir(f.dir->path()).filePath(QStringLiteral("tests/remove.txt")), "one\n"));
        QVERIFY(writeFixture(QDir(f.dir->path()).filePath(QStringLiteral("tests/new.txt")), "u\n"));
        f.page->reload();
        QVERIFY(f.treeModel()->indexForDirectory(QStringLiteral("tests")).isValid());
        QVERIFY(!f.tree()->isExpanded(f.treeIndex("tests")));
        // ...and a switch away and back.
        f.page->setFilesView(CommitPage::FilesView::Table, false);
        f.page->setFilesView(CommitPage::FilesView::Tree, false);
        settle();
        stillFolded();
    }

    // Whichever list the user is looking at, the current file is one file:
    // the flat table's row, which is what the window reads the diff from.
    void theFilesViewsShareOneCurrentFile()
    {
        CommitFixture f = nestedFixture();
        QVERIFY(f.page);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        QVERIFY(f.page->selectPath(QStringLiteral("root.txt")));
        QSignalSpy current(f.page.get(), &CommitPage::currentRowChanged);

        // The canonical row reaches the tree, ancestors opened, whoever set it.
        f.tree()->collapse(f.treeIndex("src"));
        QVERIFY(f.page->selectPath(QStringLiteral("src/deep/b.txt")));
        QCOMPARE(current.count(), 1);
        QVERIFY(f.tree()->isExpanded(f.treeIndex("src")));
        QVERIFY(f.tree()->isExpanded(f.treeIndex("src/deep")));
        QCOMPARE(f.treeModel()->path(f.tree()->currentIndex()), QStringLiteral("src/deep/b.txt"));

        // Selecting the same file again is nobody's news.
        QVERIFY(f.page->selectPath(QStringLiteral("src/deep/b.txt")));
        QCOMPARE(current.count(), 1);

        // A file becoming current in the tree makes the flat row current, once.
        f.page->setFilesView(CommitPage::FilesView::Tree, false);
        settle();
        QCOMPARE(current.count(), 1); // switching reveals; it does not re-announce
        f.tree()->setCurrentIndex(f.treeIndex("alpha/y.txt", ChangesTreeModel::Name));
        QCOMPARE(current.count(), 2);
        bool ok = false;
        QCOMPARE(f.page->currentChange(&ok).path, QStringLiteral("alpha/y.txt"));
        QVERIFY(ok);
        // Another column of the same file is the same file.
        f.tree()->setCurrentIndex(f.treeIndex("alpha/y.txt", ChangesTreeModel::Status));
        QCOMPARE(current.count(), 2);

        // A directory is a place in the tree: the canonical file stays put.
        f.tree()->setCurrentIndex(f.treeIndex("tests"));
        QCOMPARE(current.count(), 2);
        QCOMPARE(f.page->currentChange(&ok).path, QStringLiteral("alpha/y.txt"));

        // Switching presentations reveals the canonical file and announces
        // nothing; the flat proxy and the selection model stay the ones they
        // have always been.
        QSortFilterProxyModel *const proxy = f.page->proxy();
        QItemSelectionModel *const selection = f.page->table()->selectionModel();
        for (const CommitPage::FilesView view : {CommitPage::FilesView::Table, CommitPage::FilesView::Compact,
                                                 CommitPage::FilesView::Tree}) {
            f.page->setFilesView(view, false);
            settle();
            QCOMPARE(f.page->activeListView(),
                     view == CommitPage::FilesView::Tree ? static_cast<QAbstractItemView *>(f.tree())
                                                         : f.page->table());
            QCOMPARE(f.page->proxy(), proxy);
            QCOMPARE(f.page->table()->selectionModel(), selection);
        }
        QCOMPARE(current.count(), 2);
        QCOMPARE(f.treeModel()->path(f.tree()->currentIndex()), QStringLiteral("alpha/y.txt"));

        // selectFirstRow() is still the flat list's first file, not the tree's
        // first directory.
        f.page->setFilesView(CommitPage::FilesView::Tree, false);
        QVERIFY(f.page->selectFirstRow());
        QCOMPARE(f.page->currentChange(&ok).path,
                 f.page->proxy()->index(0, 0).data(ChangesModel::PathRole).toString());
    }

    // The refresh sequence the window runs — remember, reload, select, scroll
    // back — has to land in the same place whichever list is on show.
    void theFilesViewsSurviveARefresh()
    {
        CommitFixture f = nestedFixture();
        QVERIFY(f.page);
        f.page->setFilesView(CommitPage::FilesView::Tree, false);
        f.page->resize(760, 300); // short enough that the list scrolls
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        QVERIFY(f.page->selectPath(QStringLiteral("top.txt")));
        f.page->tree()->scrollToBottom();
        settle();
        const QPoint offset = f.page->scrollOffset();
        QVERIFY(offset.y() > 0); // a tree only this tall needs its ancestors open

        // The window's own order: the offset first, then the reload, then the
        // selection, then the offset back.
        const QPoint before = f.page->scrollOffset();
        f.page->reload();
        QVERIFY(f.page->selectPath(QStringLiteral("top.txt")));
        f.page->setScrollOffset(before);
        QCOMPARE(f.page->scrollOffset(), before);
        bool ok = false;
        QCOMPARE(f.page->currentChange(&ok).path, QStringLiteral("top.txt"));

        // The same after the list really changes.
        QVERIFY(writeFixture(QDir(f.dir->path()).filePath(QStringLiteral("late.txt")), "late\n"));
        f.page->reload();
        QVERIFY(f.page->selectPath(QStringLiteral("top.txt")));
        f.page->setScrollOffset(before);
        QCOMPARE(f.page->scrollOffset(), before);

        // An unchanged refresh with a directory current leaves the keyboard on
        // that directory: nothing was rebuilt, so nothing moved.
        f.tree()->setCurrentIndex(f.treeIndex("src"));
        f.page->reload();
        QVERIFY(f.page->selectPath(QStringLiteral("top.txt")));
        QCOMPARE(f.treeModel()->path(f.tree()->currentIndex()), QStringLiteral("src"));

        // A refresh that really changes the list leaves it there too: the file
        // the window selects again is the file the tree was already showing,
        // so nothing drags the keyboard off the directory the user walked to.
        f.tree()->collapse(f.treeIndex("tests"));
        f.tree()->setCurrentIndex(f.treeIndex("src", ChangesTreeModel::Name));
        QVERIFY(writeFixture(QDir(f.dir->path()).filePath(QStringLiteral("later.txt")), "later\n"));
        f.page->reload();
        QVERIFY(f.page->selectPath(QStringLiteral("top.txt")));
        f.page->setScrollOffset(before);
        settle();
        QCOMPARE(f.treeModel()->path(f.tree()->currentIndex()), QStringLiteral("src"));
        QVERIFY(f.treeModel()->isDirectory(f.tree()->currentIndex()));
        QVERIFY(!f.tree()->isExpanded(f.treeIndex("tests")));
        QCOMPARE(f.page->currentChange(&ok).path, QStringLiteral("top.txt"));
        f.tree()->expand(f.treeIndex("tests"));

        // The table keeps its own offset the same way.
        f.page->setFilesView(CommitPage::FilesView::Table, false);
        settle();
        f.page->table()->scrollToBottom();
        const QPoint tableOffset = f.page->scrollOffset();
        f.page->reload();
        QVERIFY(f.page->selectPath(QStringLiteral("top.txt")));
        f.page->setScrollOffset(tableOffset);
        QCOMPARE(f.page->scrollOffset(), tableOffset);
    }

    // A file that leaves the list and comes back is a new file to the tree:
    // what it was synchronised to went with the file.
    void aFileThatComesBackIsRevealedAgain()
    {
        CommitFixture f = nestedFixture();
        QVERIFY(f.page);
        f.page->setFilesView(CommitPage::FilesView::Tree, false);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        const QString file = QDir(f.dir->path()).filePath(QStringLiteral("back.txt"));
        QVERIFY(writeFixture(file, "here\n"));
        f.page->reload();
        QVERIFY(f.page->selectPath(QStringLiteral("back.txt")));
        QCOMPARE(f.treeModel()->path(f.tree()->currentIndex()), QStringLiteral("back.txt"));

        QVERIFY(QFile::remove(file));
        f.page->reload();
        // Nothing else is selected in between, as in a list that went empty.
        QVERIFY(!f.page->selectPath(QStringLiteral("back.txt")));

        QVERIFY(writeFixture(file, "here again\n"));
        f.page->reload();
        QVERIFY(f.page->selectPath(QStringLiteral("back.txt")));
        QCOMPARE(f.treeModel()->path(f.tree()->currentIndex()), QStringLiteral("back.txt"));
    }

    // Putting the row the keyboard was on back must not scroll to it: a view
    // scrolling to a row opens every directory above it, and the one the user
    // had just folded away is one of them.
    void aFoldedAncestorSurvivesTheCurrentRowComingBack()
    {
        CommitFixture f = nestedFixture();
        QVERIFY(f.page);
        f.page->setFilesView(CommitPage::FilesView::Tree, false);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        QVERIFY(f.page->selectPath(QStringLiteral("src/deep/b.txt")));
        QCOMPARE(f.treeModel()->path(f.tree()->currentIndex()), QStringLiteral("src/deep/b.txt"));

        // Folded by its chevron, the way the user folds it: the current row is
        // now under a directory that is not showing it.
        const QModelIndex src = f.treeIndex("src");
        f.clickTreeCell(QStringLiteral("src"), ChangesTreeModel::Name,
                        QPoint(ui::space(12), f.tree()->visualRect(src).height() / 2));
        QVERIFY(!f.tree()->isExpanded(src));
        QCOMPARE(f.treeModel()->path(f.tree()->currentIndex()), QStringLiteral("src/deep/b.txt"));

        const auto stillFolded = [&f] {
            QVERIFY(!f.tree()->isExpanded(f.treeIndex("src")));
            QCOMPARE(f.treeModel()->path(f.tree()->currentIndex()), QStringLiteral("src/deep/b.txt"));
        };
        f.page->proxy()->sort(ChangesModel::Name, Qt::AscendingOrder); // one rebuild...
        settle();
        stillFolded();
        // ...and another, after a reload that really changes the list. The
        // second one proves the collapsed set itself came through the first:
        // an ancestor unfolded by a scroll would have dropped out of it.
        QVERIFY(writeFixture(QDir(f.dir->path()).filePath(QStringLiteral("late.txt")), "late\n"));
        f.page->reload();
        QVERIFY(f.page->selectPath(QStringLiteral("src/deep/b.txt")));
        settle();
        stillFolded();
    }

    // One name can be a file and a directory at once — a repository may list
    // `a` beside `a/b` — so the two are looked up apart, whichever order the
    // flat list is in.
    void aFileAndADirectoryOfOneNameStayApart()
    {
        CommitFixture f = commitFixture();
        QVERIFY(f.page);
        f.page->setFilesView(CommitPage::FilesView::Tree, false);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        // Given to the model outright: no working tree holds both at once.
        f.model()->setChanges({change(QStringLiteral("a"), FileChange::Modified),
                               change(QStringLiteral("a/b"), FileChange::Modified),
                               change(QStringLiteral("z.txt"), FileChange::Modified)});
        settle();

        for (const Qt::SortOrder order : {Qt::AscendingOrder, Qt::DescendingOrder}) {
            f.page->proxy()->sort(ChangesModel::Name, order);
            settle();
            ChangesTreeModel *tree = f.treeModel();
            const QModelIndex file = tree->indexForPath(QStringLiteral("a"));
            QVERIFY(file.isValid());
            QVERIFY(!tree->isDirectory(file));
            QCOMPARE(tree->mapToSource(file).data(ChangesModel::PathRole).toString(), QStringLiteral("a"));
            const QModelIndex dir = tree->indexForDirectory(QStringLiteral("a"));
            QVERIFY(dir.isValid());
            QVERIFY(tree->isDirectory(dir));
            QVERIFY(tree->indexForPath(QStringLiteral("a/b")).isValid());
            QVERIFY(!tree->indexForDirectory(QStringLiteral("a/b")).isValid());

            // Revealing the canonical file lands on the file...
            QVERIFY(f.page->selectPath(QStringLiteral("z.txt")));
            QVERIFY(f.page->selectPath(QStringLiteral("a")));
            QCOMPARE(tree->path(f.tree()->currentIndex()), QStringLiteral("a"));
            QVERIFY(!tree->isDirectory(f.tree()->currentIndex()));

            // ...while the directory the keyboard was on comes back as the
            // directory, over a rebuild of every node.
            f.tree()->setCurrentIndex(dir.siblingAtColumn(ChangesTreeModel::Name));
            QVERIFY(f.treeModel()->isDirectory(f.tree()->currentIndex()));
            f.page->proxy()->sort(ChangesModel::Status, order);
            settle();
            QCOMPARE(f.treeModel()->path(f.tree()->currentIndex()), QStringLiteral("a"));
            QVERIFY(f.treeModel()->isDirectory(f.tree()->currentIndex()));
        }
    }

    // Loading a choice is not making one, and neither is asking for the
    // presentation already on: only a real switch writes window/filesView.
    void readingAFilesViewNeverWritesIt()
    {
        QSettings().remove(settings::kWindowFilesView);
        CommitFixture f = commitFixture();
        QVERIFY(f.page);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        f.page->setFilesView(CommitPage::FilesView::Tree, false);
        QCOMPARE(f.page->filesView(), CommitPage::FilesView::Tree);
        QVERIFY(f.page->treeButton()->isChecked());
        QVERIFY(!QSettings().contains(settings::kWindowFilesView));
        // The one already on, this time asked to save: a complete no-op.
        f.page->setFilesView(CommitPage::FilesView::Tree);
        QCOMPARE(f.page->filesView(), CommitPage::FilesView::Tree);
        QVERIFY(!QSettings().contains(settings::kWindowFilesView));
        // A real switch is the user's choice and is written.
        f.page->setFilesView(CommitPage::FilesView::Compact);
        QCOMPARE(QSettings().value(settings::kWindowFilesView).toString(), QStringLiteral("compact"));
        QSettings().remove(settings::kWindowFilesView);
    }

    // The name delegates paint the row's own font: a conflicted file is bold
    // because the model says so, not because the option the view handed over
    // happened to be.
    void theNameDelegatesPaintTheRowsOwnFont()
    {
        CommitFixture f = commitFixture();
        QVERIFY(f.page);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        f.model()->setChanges({change(QStringLiteral("src/clash.txt"), FileChange::Unmerged),
                               change(QStringLiteral("src/plain.txt"), FileChange::Modified)});
        settle();
        const QFont plain = OmarchyTheme::instance()->uiFont();
        QFont bold = plain;
        bold.setBold(true);
        // One cell, painted by the delegate itself, with the font the view
        // would have handed it.
        const auto render = [](QAbstractItemDelegate *delegate, const QModelIndex &index, const QFont &font) {
            QStyleOptionViewItem option;
            option.rect = QRect(0, 0, 200, ui::tableRowHeight());
            option.font = font;
            option.fontMetrics = QFontMetrics(font);
            option.state = QStyle::State_Enabled;
            QImage image(option.rect.size(), QImage::Format_ARGB32);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            delegate->paint(&painter, option, index);
            return image;
        };

        // The tree's Name column: the conflicted row is the same picture
        // whichever font comes in, because the model's one wins...
        f.page->setFilesView(CommitPage::FilesView::Tree, false);
        settle();
        QAbstractItemDelegate *treeName = f.tree()->itemDelegateForColumn(ChangesTreeModel::Name);
        QVERIFY(treeName);
        const QModelIndex conflict = f.treeIndex("src/clash.txt", ChangesTreeModel::Name);
        QVERIFY(conflict.isValid());
        QCOMPARE(render(treeName, conflict, plain), render(treeName, conflict, bold));
        // ...while a row with no font of its own paints the one it is given,
        // so the two really are different pictures.
        const QModelIndex ordinary = f.treeIndex("src/plain.txt", ChangesTreeModel::Name);
        QVERIFY(render(treeName, ordinary, plain) != render(treeName, ordinary, bold));

        // The compact table's Name column, the same way.
        f.page->setFilesView(CommitPage::FilesView::Compact, false);
        settle();
        QAbstractItemDelegate *compactName = f.page->table()->itemDelegateForColumn(ChangesModel::Name);
        QVERIFY(compactName);
        const auto flatName = [&f](const QString &path) {
            QSortFilterProxyModel *proxy = f.page->proxy();
            for (int row = 0, rows = proxy->rowCount(); row < rows; ++row)
                if (proxy->index(row, ChangesModel::Check).data(ChangesModel::PathRole).toString() == path)
                    return proxy->index(row, ChangesModel::Name);
            return QModelIndex();
        };
        const QModelIndex flatConflict = flatName(QStringLiteral("src/clash.txt"));
        QVERIFY(flatConflict.isValid());
        QCOMPARE(render(compactName, flatConflict, plain), render(compactName, flatConflict, bold));
        const QModelIndex flatOrdinary = flatName(QStringLiteral("src/plain.txt"));
        QVERIFY(render(compactName, flatOrdinary, plain) != render(compactName, flatOrdinary, bold));
    }

    // Compact is the same table with its columns down to three, and going
    // back to the table gives the user's widths back.
    void theCompactPresentationIsTheTableWithThreeColumns()
    {
        CommitFixture f = nestedFixture();
        QVERIFY(f.page);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        QTableView *table = f.page->table();
        table->setColumnWidth(ChangesModel::Name, 313); // a width the user chose
        const int sorted = f.header()->sortIndicatorSection();
        QList<int> widths;
        for (int c = 0; c < ChangesModel::ColumnCount; ++c)
            widths << table->columnWidth(c);

        f.page->setFilesView(CommitPage::FilesView::Compact, false);
        settle();
        QCOMPARE(f.page->activeListView(), table); // compact is the table itself
        QList<int> shown;
        for (int c = 0; c < ChangesModel::ColumnCount; ++c)
            if (!table->isColumnHidden(c))
                shown << c;
        QCOMPARE(shown, QList<int>({int(ChangesModel::Check), int(ChangesModel::Name), int(ChangesModel::Status)}));
        QCOMPARE(table->columnWidth(ChangesModel::Check), ui::space(30));
        QCOMPARE(table->columnWidth(ChangesModel::Status), ui::space(30));
        QVERIFY(table->columnWidth(ChangesModel::Name) > 2 * ui::space(30)); // Name has the rest
        QCOMPARE(f.header()->sortIndicatorSection(), sorted);
        // The "St" heading is this presentation's alone: the shared model and
        // the proxy still call the column what they always did.
        QCOMPARE(f.page->proxy()->headerData(ChangesModel::Status, Qt::Horizontal, Qt::DisplayRole).toString(),
                 QStringLiteral("Status"));
        QCOMPARE(f.model()->headerData(ChangesModel::Status, Qt::Horizontal, Qt::DisplayRole).toString(),
                 QStringLiteral("Status"));

        // A text size change while compact keeps the presentation and leaves
        // the widths put away for the table alone.
        f.page->applyTheme();
        settle();
        QCOMPARE(table->columnWidth(ChangesModel::Status), ui::space(30));
        QVERIFY(table->isColumnHidden(ChangesModel::Path));

        f.page->setFilesView(CommitPage::FilesView::Table, false);
        settle();
        for (int c = 0; c < ChangesModel::ColumnCount; ++c)
            QVERIFY2(!table->isColumnHidden(c), qPrintable(QString::number(c)));
        QCOMPARE(table->columnWidth(ChangesModel::Name), widths.at(ChangesModel::Name));
        QCOMPARE(table->columnWidth(ChangesModel::Check), ui::space(30)); // the design's, not the user's
        QCOMPARE(f.header()->sortIndicatorSection(), sorted);
        // Path absorbs the viewport again.
        QVERIFY(table->columnWidth(ChangesModel::Path) > 0);

        // ...and again, and again: the widths do not drift.
        for (int round = 0; round < 3; ++round) {
            f.page->setFilesView(CommitPage::FilesView::Compact, false);
            f.page->setFilesView(CommitPage::FilesView::Table, false);
        }
        settle();
        QCOMPARE(table->columnWidth(ChangesModel::Name), widths.at(ChangesModel::Name));
    }

    // One of the three is always on, the choice is remembered, and a run
    // started with --files-view never writes it.
    void theFilesViewButtonsRememberTheChoice()
    {
        QSettings().remove(settings::kWindowFilesView);
        {
            CommitFixture f = commitFixture();
            QVERIFY(f.page);
            QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
            settle();
            const QList<QToolButton *> buttons{f.page->compactButton(), f.page->treeButton(), f.page->tableButton()};
            QStringList names;
            for (QToolButton *b : buttons) {
                names << b->accessibleName();
                QVERIFY(b->isCheckable());
                QCOMPARE(b->toolTip(), b->accessibleName());
                QCOMPARE(b->objectName(), QStringLiteral("iconButton"));
                QCOMPARE(b->size(), QSize(ui::space(24), ui::space(24)));
            }
            QCOMPARE(names, QStringList({"Compact list", "Tree", "Table"}));
            // Nothing saved is the table, and reading a choice never writes one.
            QCOMPARE(f.page->filesView(), CommitPage::FilesView::Table);
            QVERIFY(f.page->tableButton()->isChecked());
            QVERIFY(!QSettings().contains(settings::kWindowFilesView));

            // Exactly one at a time, and the saved choice follows the clicks.
            f.page->treeButton()->click();
            QCOMPARE(f.page->filesView(), CommitPage::FilesView::Tree);
            QVERIFY(!f.page->tableButton()->isChecked() && !f.page->compactButton()->isChecked());
            QCOMPARE(QSettings().value(settings::kWindowFilesView).toString(), QStringLiteral("tree"));
            f.page->compactButton()->click();
            QCOMPARE(QSettings().value(settings::kWindowFilesView).toString(), QStringLiteral("compact"));
            // Clicking the one already on changes nothing.
            f.page->compactButton()->click();
            QCOMPARE(f.page->filesView(), CommitPage::FilesView::Compact);
        }
        // A saved choice comes back; something unreadable is the table.
        {
            CommitFixture f = commitFixture();
            QVERIFY(f.page);
            QCOMPARE(f.page->filesView(), CommitPage::FilesView::Compact);
            QVERIFY(f.page->compactButton()->isChecked());
        }
        QSettings().setValue(settings::kWindowFilesView, QStringLiteral("sideways"));
        {
            CommitFixture f = commitFixture();
            QVERIFY(f.page);
            QCOMPARE(f.page->filesView(), CommitPage::FilesView::Table);
            QCOMPARE(QSettings().value(settings::kWindowFilesView).toString(), QStringLiteral("sideways"));
        }
        // The command line's spellings, and nothing else.
        bool ok = false;
        QCOMPARE(CommitPage::viewFromKey(QStringLiteral("tree"), &ok), CommitPage::FilesView::Tree);
        QVERIFY(ok);
        for (const QString bad : {"", "TREE", "list", "Table "}) {
            CommitPage::viewFromKey(bad, &ok);
            QVERIFY2(!ok, qPrintable(bad));
        }
        QCOMPARE(CommitPage::viewKey(CommitPage::FilesView::Compact), QStringLiteral("compact"));

        // An override run lists the files that way and saves nothing — not
        // even after the user clicks another view.
        QSettings().setValue(settings::kWindowFilesView, QStringLiteral("table"));
        {
            CommitFixture f = commitFixture();
            QVERIFY(f.page);
            f.page->setFilesViewOverride(CommitPage::FilesView::Tree);
            QCOMPARE(f.page->filesView(), CommitPage::FilesView::Tree);
            f.page->compactButton()->click();
            QCOMPARE(f.page->filesView(), CommitPage::FilesView::Compact);
            QCOMPARE(QSettings().value(settings::kWindowFilesView).toString(), QStringLiteral("table"));
        }
        QSettings().remove(settings::kWindowFilesView);
    }

    // File actions belong to the file that was clicked, whichever list it was
    // clicked in; a directory has no file menu at all.
    void theFileMenuFollowsTheClickedFile()
    {
        CommitFixture f = nestedFixture();
        QVERIFY(f.page);
        f.page->setFilesView(CommitPage::FilesView::Tree, false);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        const auto menuAt = [](QAbstractItemView *view, const QPoint &pos) {
            QStringList entries;
            QTimer::singleShot(50, [&entries] {
                auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
                if (!menu)
                    return;
                for (const QAction *a : menu->actions())
                    entries << (a->isSeparator() ? QStringLiteral("—")
                                                 : a->text() + (a->isEnabled() ? QString() : QStringLiteral(" (off)")));
                menu->close();
            });
            QContextMenuEvent event(QContextMenuEvent::Mouse, pos, view->viewport()->mapToGlobal(pos));
            QApplication::sendEvent(view->viewport(), &event);
            QTest::qWait(150);
            return entries;
        };

        // A deleted file cannot be opened, and the click makes it current.
        const QStringList deleted = menuAt(f.tree(), f.tree()->visualRect(f.treeIndex("tests/remove.txt")).center());
        QCOMPARE(deleted.size(), 3);
        QVERIFY2(deleted.first().endsWith(QStringLiteral("(off)")), qPrintable(deleted.first()));
        QCOMPARE(deleted.last(), QStringLiteral("Discard changes"));
        bool ok = false;
        QCOMPARE(f.page->currentChange(&ok).path, QStringLiteral("tests/remove.txt"));

        // A file that is there can be, and it becomes the current one too.
        const QStringList live = menuAt(f.tree(), f.tree()->visualRect(f.treeIndex("root.txt")).center());
        QCOMPARE(live.size(), 3);
        QVERIFY(!live.first().endsWith(QStringLiteral("(off)")));
        QCOMPARE(f.page->currentChange(&ok).path, QStringLiteral("root.txt"));

        // A directory and the empty space below the rows have none.
        QVERIFY(menuAt(f.tree(), f.tree()->visualRect(f.treeIndex("src")).center()).isEmpty());
        for (const QString dir : {"alpha", "Beta", "single", "src", "tests"})
            f.tree()->collapse(f.treeIndex(dir)); // leaves room under the rows
        settle();
        const QRect last = f.tree()->visualRect(f.treeIndex("top.txt"));
        QVERIFY(last.bottom() + 4 < f.tree()->viewport()->height());
        QVERIFY(!f.tree()->indexAt(QPoint(last.center().x(), last.bottom() + 4)).isValid());
        QVERIFY(menuAt(f.tree(), QPoint(last.center().x(), last.bottom() + 4)).isEmpty());
        QCOMPARE(f.page->currentChange(&ok).path, QStringLiteral("root.txt"));

        // The table's own menu is unchanged.
        f.page->setFilesView(CommitPage::FilesView::Table, false);
        settle();
        QVERIFY(f.page->selectPath(QStringLiteral("root.txt")));
        const QModelIndex row = f.page->table()->currentIndex();
        QCOMPARE(menuAt(f.page->table(), f.page->table()->visualRect(row).center()).size(), 3);
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

    // The same live change, for the top bar: one bar built at 12 and left
    // standing has to measure, fold and spell itself out like a bar built
    // fresh at every size the desktop moves to.
    void theTopBarFollowsALiveTextSizeChange()
    {
        QTemporaryDir dir, home, noPath;
        QVERIFY(dir.isValid() && home.isValid() && noPath.isValid());
        const QString toml = QDir(dir.path()).filePath(QStringLiteral("shell.toml"));
        QVERIFY(writeFixture(toml, "[font]\nbase-size = 12\n"));
        {
            ScopedEnv themeDir("OMAGIT_THEME_DIR", dir.path().toUtf8());
            ScopedEnv scratchHome("HOME", home.path().toUtf8());
            ScopedEnv emptyPath("PATH", noPath.path().toUtf8());
            OmarchyTheme theme;
            QCOMPARE(theme.fontBase(), 12);
            theme.apply(*qApp);

            // TopBar::applyTheme() on every change is what MainWindow does.
            BarFixture live = topBar();
            QObject::connect(&theme, &OmarchyTheme::changed, live.bar, [bar = live.bar] { bar->applyTheme(); });
            QVERIFY(QTest::qWaitForWindowExposed(live.host.get()));
            settle();

            const auto matchesAFreshBar = [&live] {
                BarFixture fresh = topBar();
                QVERIFY(QTest::qWaitForWindowExposed(fresh.host.get()));
                settle();
                QCOMPARE(barMetrics(live.bar), barMetrics(fresh.bar));
                // ...and both fold at the same width, wherever that is.
                for (const int width : {760, 430})
                    QCOMPARE(live.levelAt(width), fresh.levelAt(width));
                // The icon form is the design's square at whatever text size
                // this is, and the names never follow the folding at all.
                QCOMPARE(live.levelAt(live.widthForLevel(2)), 2);
                for (QToolButton *b : {live.bar->pullButton(), live.bar->pushButton(), live.bar->moreButton()})
                    QCOMPARE(live.rectOf(b).width(), iconFormWidth());
                QCOMPARE(barNames(live.bar), kBarNames);
                QCOMPARE(barNames(fresh.bar), kBarNames);
                live.levelAt(live.bar->sizeHint().width());
            };

            const QStringList atTwelve = barMetrics(live.bar);
            matchesAFreshBar();

            // The desktop's text size goes up under the live bar.
            QVERIFY(writeFixture(toml, "[font]\nbase-size = 16\n"));
            QTRY_COMPARE_WITH_TIMEOUT(theme.fontBase(), 16, 10000);
            settle();
            matchesAFreshBar();
            QVERIFY(barMetrics(live.bar) != atTwelve); // the measurements did move

            // ...and back down to where it started.
            QVERIFY(writeFixture(toml, "[font]\nbase-size = 12\n"));
            QTRY_COMPARE_WITH_TIMEOUT(theme.fontBase(), 12, 10000);
            settle();
            matchesAFreshBar();
            QCOMPARE(barMetrics(live.bar), atTwelve);

            live.host.reset(); // the bar goes before the theme it follows
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

            CommitFixture live = nestedFixture(); // directories, so the tree has depth to measure
            QVERIFY(live.page);
            CommitPage *page = live.page.get();
            applyThemeAsMainWindowDoes(page);
            QObject::connect(&theme, &OmarchyTheme::changed, page,
                             [page, applyThemeAsMainWindowDoes] { applyThemeAsMainWindowDoes(page); });
            QVERIFY(QTest::qWaitForWindowExposed(page));
            settle();

            // Both pages at two widths around the one the action bar folds at,
            // so the label and its box are measured where they change — and in
            // all three presentations, so the tree's depth geometry and the
            // compact table's narrow columns are measured too.
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
                fresh->resize(760, 600);
                settle();
                for (const CommitPage::FilesView view : {CommitPage::FilesView::Tree,
                                                         CommitPage::FilesView::Compact,
                                                         CommitPage::FilesView::Table}) {
                    live.page->setFilesView(view, false);
                    fresh->setFilesView(view, false);
                    settle();
                    QCOMPARE(treeMetrics(live.page.get()), treeMetrics(fresh.get()));
                    QCOMPARE(pageMetrics(live.page.get()), pageMetrics(fresh.get()));
                }
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

    // The same live change, for the Mini rail's commit section and the card:
    // a window built at 12 and left standing measures like one built fresh at
    // 16 and back — the rail, the tile and its square, the gaps around the
    // separator, the card's width, margins, gaps, editor offsets and hint,
    // its anchor, the wrapping of the shared message, and where the body
    // puts the splitter.
    void theCommitPopoverFollowsALiveTextSizeChange()
    {
        QTemporaryDir dir, home, tools;
        QVERIFY(dir.isValid() && home.isValid() && tools.isValid());
        const QString toml = QDir(dir.path()).filePath(QStringLiteral("shell.toml"));
        QVERIFY(writeFixture(toml, "[font]\nbase-size = 12\n"));
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

            WindowFixture live = mainWindow();
            QVERIFY(live.window);
            QVERIFY(QTest::qWaitForWindowExposed(live.window.get()));
            QVERIFY(live.openCard());
            const QString text = longParagraph() + QLatin1Char(' ') + longParagraph();
            live.pageEditor()->replaceText(text);
            settle();

            const auto matchesAFreshWindow = [&live, &text] {
                WindowFixture fresh = mainWindow(0, true); // Mini from its first show
                QVERIFY(fresh.window);
                QVERIFY(QTest::qWaitForWindowExposed(fresh.window.get()));
                QVERIFY(fresh.openCard());
                fresh.pageEditor()->replaceText(text);
                settle();
                QTRY_COMPARE(popoverMetrics(live), popoverMetrics(fresh));
                // The design's exact offsets, whatever the text size.
                const QStringList metrics = popoverMetrics(live);
                QVERIFY(metrics.contains(QStringLiteral("editorTop=%1").arg(ui::space(32))));
                QVERIFY(metrics.contains(QStringLiteral("editorLeft=%1").arg(ui::space(10))));
                QVERIFY(metrics.contains(QStringLiteral("rail=%1").arg(MiniRail::railWidth())));
                QVERIFY(metrics.contains(QStringLiteral("square=%1").arg(ui::space(40))));
                QVERIFY(metrics.contains(QStringLiteral("hintPx=%1").arg(qRound(OmarchyTheme::instance()->fontBase() * 11 / 12.0))));
                QVERIFY(metrics.contains(QStringLiteral("hintBold=0")));
                MessageEdit *editor = live.popover()->editor();
                QTRY_COMPARE(editor->contentHeight(), freshContentHeight(editor, text));
                // The body puts the splitter after the rail's scaled width.
                QSplitter *splitter = nullptr;
                for (QSplitter *s : fresh.window->findChildren<QSplitter *>())
                    if (s->orientation() == Qt::Horizontal && s->objectName().isEmpty())
                        splitter = s;
                QVERIFY(splitter);
                QCOMPARE(rectIn(splitter, fresh.host()).x(),
                         fresh.rail()->geometry().x() + MiniRail::railWidth() + 8);
                // ...and the left section's first width is its share of what
                // the window leaves beside that rail.
                const QMargins margins = fresh.host()->layout()->contentsMargins();
                const int total = fresh.window->width() - margins.left() - margins.right() - MiniRail::railWidth() - 8;
                fresh.window->setPaneLayout(PaneLayout::Docked, false);
                settle();
                QCOMPARE(splitter->sizes().first(), total * 45 / 100);
                fresh.window.reset();
            };

            matchesAFreshWindow();
            const QStringList atTwelve = popoverMetrics(live);
            QVERIFY(atTwelve.contains(QStringLiteral("rail=52")));
            QVERIFY(atTwelve.contains(QStringLiteral("editorTop=32")));

            QVERIFY(writeFixture(toml, "[font]\nbase-size = 16\n"));
            QTRY_COMPARE_WITH_TIMEOUT(theme.fontBase(), 16, 10000);
            settle();
            matchesAFreshWindow();
            const QStringList atSixteen = popoverMetrics(live);
            QVERIFY(atSixteen.contains(QStringLiteral("rail=69")));
            QVERIFY(atSixteen.contains(QStringLiteral("square=53")));
            QVERIFY(atSixteen.contains(QStringLiteral("tile=69x58")));
            QVERIFY(atSixteen.contains(QStringLiteral("editorTop=43")));
            QVERIFY(atSixteen.contains(QStringLiteral("editorLeft=13")));
            QVERIFY(live.popover()->isVisible()); // a text size is no reason to close

            QVERIFY(writeFixture(toml, "[font]\nbase-size = 12\n"));
            QTRY_COMPARE_WITH_TIMEOUT(theme.fontBase(), 12, 10000);
            settle();
            matchesAFreshWindow();
            QCOMPARE(popoverMetrics(live), atTwelve);

            live.window.reset(); // the window goes before the theme it follows
        }
        g_theme.reset(new OmarchyTheme);
        g_theme->apply(*qApp);
        QVERIFY(OmarchyTheme::instance() == g_theme.get());
    }

    // The agent card after a live text-size change measures like one built
    // at the new size: its width, margins, rows, track, notes and — with no
    // agent installed — its command rows.
    void theAgentCardFollowsALiveTextSizeChange()
    {
        QTemporaryDir dir, home, tools, bare;
        QVERIFY(dir.isValid() && home.isValid() && tools.isValid() && bare.isValid());
        const QString toml = QDir(dir.path()).filePath(QStringLiteral("shell.toml"));
        QVERIFY(writeFixture(toml, "[font]\nbase-size = 12\n"));
        const QString gitBinary = QStandardPaths::findExecutable(QStringLiteral("git"));
        QVERIFY(!gitBinary.isEmpty());
        QVERIFY(QFile::link(gitBinary, QDir(bare.path()).filePath(QStringLiteral("git"))));
        QVERIFY(writeFakeClaude(tools.path()));
        QVERIFY(QFile::link(gitBinary, QDir(tools.path()).filePath(QStringLiteral("git"))));
        {
            ScopedEnv themeDir("OMAGIT_THEME_DIR", dir.path().toUtf8());
            ScopedEnv scratchHome("HOME", home.path().toUtf8());
            OmarchyTheme theme;
            QCOMPARE(theme.fontBase(), 12);
            theme.apply(*qApp);
            AgentScope scope(tools.path());
            CommitMessageAgent::saveChoice(AgentChoice{QStringLiteral("claude"), QStringLiteral("opus"), QStringLiteral("high")});

            // A card open in either state: the installed one with the fake
            // claude, the other with git alone on PATH.
            const auto open = [&](WindowFixture &f, bool installed) {
                ScopedEnv path("PATH", (installed ? tools : bare).path().toUtf8());
                f.window->resize(945, 1234);
                QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
                QVERIFY(QMetaObject::invokeMethod(f.window.get(), "showAgentMenu"));
                settle();
                QVERIFY(f.agentCard()->isVisible());
            };
            WindowFixture live = mainWindow();
            QVERIFY(live.window);
            open(live, true);
            WindowFixture liveBare = mainWindow();
            QVERIFY(liveBare.window);
            open(liveBare, false);

            const auto matchesAFreshWindow = [&] {
                for (const bool installed : {true, false}) {
                    WindowFixture fresh = mainWindow();
                    QVERIFY(fresh.window);
                    open(fresh, installed);
                    const AgentPopover *card = (installed ? live : liveBare).agentCard();
                    QTRY_COMPARE(agentCardMetrics(card), agentCardMetrics(fresh.agentCard()));
                    QCOMPARE(card->geometry(), fresh.agentCard()->geometry());
                    const QStringList metrics = agentCardMetrics(card);
                    const int pad = ui::space(10) - 2;
                    QVERIFY(metrics.contains(QStringLiteral("margins=%1,%1,%1,%1").arg(pad)));
                    QVERIFY(metrics.contains(QStringLiteral("width=%1").arg(ui::space(360))));
                    QVERIFY(metrics.contains(QStringLiteral("notePx=%1").arg(OmarchyTheme::instance()->captionFont().pixelSize()))
                            || !installed);
                    QVERIFY(metrics.contains(QStringLiteral("noteBold=0")));
                    if (installed) {
                        QVERIFY(metrics.contains(QStringLiteral("row=%1").arg(ui::space(28))));
                        QVERIFY(metrics.contains(QStringLiteral("picker=%1").arg(ui::space(28))));
                        QVERIFY(metrics.contains(QStringLiteral("pickerTop=%1").arg(ui::space(10) + ui::space(22))));
                        QVERIFY(metrics.contains(QStringLiteral("track=%1").arg(ui::space(44))));
                        QVERIFY(metrics.contains(QStringLiteral("stop0=%1").arg(ui::space(28))));
                        QVERIFY(metrics.contains(QStringLiteral("stopY=%1").arg(ui::space(12))));
                    } else {
                        QVERIFY(metrics.contains(QStringLiteral("notePx=%1").arg(qRound(OmarchyTheme::instance()->fontBase() * 11 / 12.0))));
                        QVERIFY(metrics.contains(QStringLiteral("command=%1").arg(ui::space(30))));
                        QVERIFY(metrics.contains(QStringLiteral("copy=%1").arg(ui::space(24))));
                    }
                    fresh.window.reset();
                }
            };

            matchesAFreshWindow();
            const QStringList atTwelve = agentCardMetrics(live.agentCard());
            const QStringList bareAtTwelve = agentCardMetrics(liveBare.agentCard());
            QVERIFY(atTwelve.contains(QStringLiteral("width=360")));

            QVERIFY(writeFixture(toml, "[font]\nbase-size = 16\n"));
            QTRY_COMPARE_WITH_TIMEOUT(theme.fontBase(), 16, 10000);
            settle();
            matchesAFreshWindow();
            QVERIFY(agentCardMetrics(live.agentCard()).contains(QStringLiteral("width=480")));
            QVERIFY(agentCardMetrics(live.agentCard()).contains(QStringLiteral("row=37")));
            QVERIFY(live.agentCard()->isVisible()); // a text size is no reason to close

            QVERIFY(writeFixture(toml, "[font]\nbase-size = 12\n"));
            QTRY_COMPARE_WITH_TIMEOUT(theme.fontBase(), 12, 10000);
            settle();
            matchesAFreshWindow();
            QCOMPARE(agentCardMetrics(live.agentCard()), atTwelve);
            QCOMPARE(agentCardMetrics(liveBare.agentCard()), bareAtTwelve);

            live.window.reset(); // the windows go before the theme they follow
            liveBare.window.reset();
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
