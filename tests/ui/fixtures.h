// What the ui suite's test files share: git and file helpers, the fixtures
// that build a commit page, a top bar or the whole window on a scratch
// repository, and the scopes that fence a test's environment in.
#pragma once

#include "../../src/AgentPopover.h"
#include "../../src/BadgeButton.h"
#include "../../src/ChangesModel.h"
#include "../../src/ChangesTreeModel.h"
#include "../../src/CommitMessageAgent.h"
#include "../../src/CommitPage.h"
#include "../../src/CommitPopover.h"
#include "../../src/DiffView.h"
#include "../../src/GitRepo.h"
#include "../../src/MainWindow.h"
#include "../../src/MessageEdit.h"
#include "../../src/MiniRail.h"
#include "../../src/NewBranchCard.h"
#include "../../src/OmarchyTheme.h"
#include "../../src/PaneLayout.h"
#include "../../src/TopBar.h"

#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStandardPaths>
#include <QTableView>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <QTreeView>

#include <functional>
#include <memory>

class LoginDialog;

// The theme the whole run shares; the theme tests put a fresh one here after
// they have pointed OmarchyTheme at a scratch directory of their own, and
// UiTestCase::cleanup() does after any test that left another one current.
extern std::unique_ptr<OmarchyTheme> g_theme;

class ScopedEnv {
public:
    ScopedEnv(const char *key, const QByteArray &value) : key(key), old(qgetenv(key)), existed(qEnvironmentVariableIsSet(key)) { qputenv(key, value); }
    ~ScopedEnv() { if (existed) qputenv(key, old); else qunsetenv(key); }
private:
    const char *key;
    QByteArray old;
    bool existed;
};

bool writeFixture(const QString &path, const QByteArray &data, bool executable = false);

// Committer dates decide the order `git log --date-order` returns, so every
// commit gets one of its own: 2024-01-01 01:00, 02:00, ...
QString stamp(int hour);

bool git(const QString &dir, const QStringList &args, int hour = 0);

bool commit(const QString &dir, const QString &message, int hour);

// The queued height check of the message box runs from the event loop, so
// the assertions have to let it.
void settle();

// A row of the tree by its exact path, whichever kind it is: the model keeps
// files and directories apart (one name may be both), while a test that names
// a path usually knows which of the two it means.
QModelIndex treeRow(ChangesTreeModel *model, const QString &path, int column = ChangesTreeModel::Check);

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
std::unique_ptr<CommitPage> commitPage(GitRepo *repo);

CommitFixture commitFixture();

// The same page over a repository with directories in it: what the tree has
// to show — nested paths, a single-child chain (src/deep), a one-file
// directory, two directory names that differ only in case, files in the root
// and one deletion — plus an untracked file inside a directory and another
// beside them, so the eye takes something out of both.
CommitFixture nestedFixture();

// A remote git can reach but can never sign in to: every request is answered
// with the 401 and the Basic challenge that make git ask for a username and a
// password, and then the connection is closed. Nothing of git's own protocol
// is served — the sign-in never gets that far. git opens a connection per
// request, so the server goes on accepting for as long as the test lives.
std::unique_ptr<QTcpServer> unauthorizedServer();

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

    // As tall as the width makes the bar, as a window's layout would have it:
    // one row, or two where the stacked tabs take their own.
    int levelAt(int width) const
    {
        bar->resize(width, bar->heightForWidth(width));
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
                  const QString &branch = QStringLiteral("feature/askpass-login-dialog"), int count = 7);

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
    NewBranchCard *newBranchCard() const { return window->findChild<NewBranchCard *>(); }
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
// a size of its own); a `branch` is checked out after the first commit, in
// place of main; a `folder` names the repository's directory (the top bar's
// repository chip), inside the scratch one.
WindowFixture mainWindow(int extra = 0, bool mini = false,
                         const std::function<void(MainWindow *)> &beforeShow = {},
                         const QString &branch = {}, const QString &folder = {});

// What a screen reader is told about the row, in the order it reads in. None
// of it depends on what the controls are wearing at the width of the moment.
QStringList barNames(TopBar *bar);

// The names the default fixture's bar carries, at every level.
extern const QStringList kBarNames;

// A sync button wearing its glyph alone, and the more button: the design's
// 28 px square; the badge hangs over its corner, outside it.
// A sync button's icon form: the bare kit button measured without a width,
// 8 + 16 + 8 (screens.js topBar()); More keeps the 28 px square.
int iconFormWidth();

// What a top bar measures at the text size of the moment, spelled out so a
// mismatch names itself.
QStringList barMetrics(TopBar *bar);

// A menu's entries as they are once it has filled itself: what aboutToShow
// puts in it, without the menu ever being shown.
QList<QAction *> filledMenu(QMenu *menu);

// The texts of those entries, "-" for a separator.
QStringList menuTexts(const QList<QAction *> &actions);

// The window's own splitter: the left section beside the diff pane.
QSplitter *bodySplitter(const WindowFixture &f);

// The docked left section's width before the user drags the splitter: the
// design's for the window's width class (MainWindow::defaultLeftWidth()).
int designLeftWidth(const QWidget *window);

// `w`'s rectangle in `ref`'s coordinates.
QRect rectIn(const QWidget *w, const QWidget *ref);

// The commit tile's painted square, in the tile's coordinates: space(40),
// centred sideways and at the bottom of the widget (which is the square).
QRect tileSquare(const QToolButton *tile);

// The window that holds `w` becomes the active one, so focus and the
// widget-scoped shortcuts (the card's Escape) behave as on a desktop.
bool activate(QWidget *w);

// A long paragraph, wrapped at any width a message box can have.
QString longParagraph();

// What a message box built fresh at `edit`'s width and font needs for `text`:
// the height the one on screen has to agree with, however its document was
// wrapped before.
int freshContentHeight(const MessageEdit *edit, const QString &text);

bool closeTo(const QColor &a, const QColor &b);

// Presses the left button on whatever widget of `window` is at `pos`, as a
// click there would.
void clickAt(QWidget *window, const QPoint &pos, Qt::KeyboardModifiers modifiers = {});

// The separator above the commit tile.
QWidget *tileRule(const WindowFixture &f);

// The `claude --help` sample of gitrepo_test.cpp's parseClaudeHelp check: the
// aliases fable, opus and sonnet, the levels low to max.
extern const char kClaudeHelp[];

// A fake agent CLI in `dir`: asked `$1` (`probe`), it prints `answer`;
// asked anything else, it reads the diff and answers with a one-line message.
// Shell builtins only: the tests' PATH has git on it and nothing else.
bool writeFakeAgent(const QString &dir, const QString &name, const char *probe, const QByteArray &answer);

// A fake `claude`: --help prints the sample above.
bool writeFakeClaude(const QString &dir);

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

// Whether any visible label of the dialog says `text` — as it reads, that
// is, without the zero-width spaces a long path gets to wrap at.
bool says(LoginDialog *dialog, const QString &text);

// The application git runs as its askpass helper — the binary itself.
// Empty when it is not built, which is all a test can do about it.
QString helperBinary();
