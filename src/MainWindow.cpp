#include "MainWindow.h"
#include "BadgeButton.h"
#include "BranchMenu.h"
#include "CommitPage.h"
#include "DesktopExec.h"
#include "DiffPane.h"
#include "Footer.h"
#include "MergeDialog.h"
#include "DiffModel.h"
#include "DiffView.h"
#include "HistoryView.h"
#include "KeybindingsPanel.h"
#include "LoginDialog.h"
#include "CloneDialog.h"
#include "MiniRail.h"
#include "OmarchyTheme.h"
#include "Settings.h"
#include "TickMenu.h"
#include "Toolbar.h"
#include "UiHelpers.h"

#include <QAction>
#include <QButtonGroup>
#include <QDir>
#include <QDialog>
#include <QFileInfo>
#include <QEvent>
#include <QFileDialog>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableView>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

using namespace ui;

namespace {
constexpr int kRecentMax = 15;
constexpr int kDefaultWidth = 1400, kDefaultHeight = 850;
constexpr int kBodySpacing = 8;         // between the rail and the splitter, and everywhere else
constexpr int kLeftSharePercent = 45;   // of the window, before the user drags the splitter
constexpr int kWatchDebounceMs = 500;   // a burst of file changes ends in one refresh
// How long the footer keeps a message: a done deed, something that took a
// while, a failure, and a job still waiting for the user.
constexpr int kShortStatusMs = 5000, kMediumStatusMs = 8000, kErrorStatusMs = 15000, kPendingStatusMs = 20000;

// The keybindings panel's spelling of a shortcut: "CTRL SHIFT + M", "SHIFT + F8".
QString keysText(const QKeySequence &keys)
{
    const QKeyCombination combo = keys[0];
    const Qt::KeyboardModifiers mods = combo.keyboardModifiers();
    QStringList parts;
    if (mods & Qt::ControlModifier)
        parts << QStringLiteral("CTRL");
    if (mods & Qt::ShiftModifier)
        parts << QStringLiteral("SHIFT");
    if (mods & Qt::AltModifier)
        parts << QStringLiteral("ALT");
    if (mods & Qt::MetaModifier)
        parts << QStringLiteral("META");
    const QString key = QKeySequence(QKeyCombination(Qt::NoModifier, combo.key())).toString(QKeySequence::PortableText).toUpper();
    return parts.isEmpty() ? key : parts.join(QLatin1Char(' ')) + QStringLiteral(" + ") + key;
}
} // namespace

MainWindow::MainWindow(GitRepo *repo, QWidget *parent)
    : QMainWindow(parent), m_repo(repo)
{
    setWindowIcon(QIcon(QStringLiteral(":/omagit.svg")));
    m_sync = new RemoteSync(repo, this);
    buildUi();
    applyTheme();
    updateRepoLabels();
    rememberRepository(repo->root());
    connect(OmarchyTheme::instance(), &OmarchyTheme::changed, this, &MainWindow::applyTheme);

    QSettings conf;
    restoreGeometry(conf.value(settings::kWindowGeometry).toByteArray());
    if (!conf.contains(settings::kWindowGeometry))
        resize(kDefaultWidth, kDefaultHeight);
    migrateLayoutSettings();
    m_sync->setAutoFetchInterval(autoFetchSecondsSetting());

    QTimer::singleShot(0, this, &MainWindow::refresh);
}

// "full" (pre-0.4) and window/leftFull (pre-0.3) meant the left section
// alone; both become Docked with the diff pane hidden (and are re-saved so).
void MainWindow::migrateLayoutSettings()
{
    QSettings conf;
    const QString layoutKey = conf.value(settings::kWindowLayout).toString();
    const bool legacyFull = layoutKey == QLatin1String("full")
        || (layoutKey.isEmpty() && conf.value(settings::kWindowLeftFull, false).toBool());
    setPaneLayout(paneLayoutFromKey(layoutKey));
    setDiffPaneVisible(!legacyFull && conf.value(settings::kWindowDiffPane, true).toBool());
}

// Unset, the setting says no more than where RemoteSync would have started anyway.
static_assert(settings::kAutoFetchSecondsDefault == RemoteSync::kDefaultInterval);

int MainWindow::autoFetchSecondsSetting()
{
    return QSettings().value(settings::kAutoFetchSeconds, settings::kAutoFetchSecondsDefault).toInt();
}

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(14, 12, 14, 8);
    rootLayout->setSpacing(kBodySpacing);

    // ---- Left section: toolbar above (commit dialog | history)
    auto *left = new QWidget;
    m_left = left;
    // The pane may be dragged as narrow as the user likes: the toolbar folds
    // its buttons away, everything else just gets cut off at the edge.
    left->setMinimumWidth(1);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(kBodySpacing);

    // The toolbar: Commit, History | Pull, Push, Fetch.
    // Labels give way to icons, then to a "more" menu, as the pane narrows.
    m_toolbar = new Toolbar;
    m_commitModeButton = toolButton(QString(), tr("Pending changes and commit dialog (Ctrl+1)"));
    m_historyModeButton = toolButton(QString(), tr("Commit history of the repository (Ctrl+2)"));
    auto *modes = new QButtonGroup(this);
    modes->setExclusive(true);
    for (QToolButton *b : {m_commitModeButton, m_historyModeButton}) {
        b->setCheckable(true);
        modes->addButton(b);
    }
    m_commitModeButton->setChecked(true);
    m_toolbar->addButton(m_commitModeButton, icon(kCommit) + tr("Commit"), icon(kCommit, tr("C")).trimmed(), tr("Commit"));
    m_toolbar->addButton(m_historyModeButton, icon(kHistory) + tr("History"), icon(kHistory, tr("H")).trimmed(), tr("History"));
    connect(m_commitModeButton, &QToolButton::clicked, this, [this] { setMode(CommitMode); });
    connect(m_historyModeButton, &QToolButton::clicked, this, [this] { setMode(HistoryMode); });

    // Pull / Push / Fetch act on the whole repository, so they are the same
    // in both modes. The Pull badge is the number of commits waiting on the
    // upstream, the Push badge the number not pushed yet. Buttons fold into
    // the more menu from the right, so Pull is the last of the three to go
    // and Fetch (which happens by itself anyway) the first.
    m_toolbar->addSeparator();
    SyncButtons bar{toolButton<BadgeButton>(QString()), toolButton<BadgeButton>(QString()), toolButton<BadgeButton>(QString())};
    m_toolbar->addButton(bar.pull, icon(kPull) + tr("Pull"), icon(kPull, QStringLiteral("↓")).trimmed(), tr("Pull"));
    m_toolbar->addButton(bar.push, icon(kPush) + tr("Push"), icon(kPush, QStringLiteral("↑")).trimmed(), tr("Push"));
    m_toolbar->addButton(bar.fetch, icon(kFetch) + tr("Fetch"), icon(kFetch, tr("F")).trimmed(), tr("Fetch"));
    m_syncButtons << bar;
    connect(m_sync, &RemoteSync::stateChanged, this, &MainWindow::updateSyncButtons);
    connect(m_sync, &RemoteSync::finished, this, &MainWindow::onSyncFinished);
    // git or ssh asked the app (its own askpass helper) for a login: the
    // dialog answers, RemoteSync hands the answer back to the waiting git.
    connect(m_sync->askPass(), &AskPass::requestReceived, this, &MainWindow::onAskPassRequest);
    // Merge opens the merge view; its badge says when a merge waits with conflicts.
    m_toolbar->addSeparator();
    auto *mergeButton = toolButton<BadgeButton>(QString());
    m_toolbar->addButton(mergeButton, icon(kMerge) + tr("Merge"), icon(kMerge, tr("M")).trimmed(), tr("Merge"));
    m_mergeButtons << mergeButton;
    m_toolbarRow = new QHBoxLayout;
    m_toolbarRow->setSpacing(kBodySpacing);
    m_toolbarRow->addWidget(m_toolbar, 1);
    leftLayout->addLayout(m_toolbarRow);

    // The footer (added to the window at the end): the layout toggle, the
    // repository and branch selectors, the path and messages, the keybindings.
    m_footer = new Footer;
    connect(m_footer->layoutButton(), &QToolButton::clicked, this, [this](bool mini) {
        setPaneLayout(mini ? PaneLayout::Mini : PaneLayout::Docked);
    });
    // Repository and branch selectors stay available in both modes.
    connect(m_footer->repoButton(), &QToolButton::clicked, this, &MainWindow::showRepoMenu);
    connect(m_footer->branchButton(), &QToolButton::clicked, this, &MainWindow::showBranchMenu);
    connect(m_footer->keybindingsButton(), &QToolButton::clicked, this, &MainWindow::showKeybindings);

    m_stack = new QStackedWidget;
    m_commitPage = new CommitPage(m_repo);
    connect(m_commitPage, &CommitPage::currentRowChanged, this, &MainWindow::onCurrentRowChanged);
    connect(m_commitPage, &CommitPage::openRequested, this, &MainWindow::openInEditor);
    connect(m_commitPage, &CommitPage::showDiffPaneRequested, this, [this] { setDiffPaneVisible(true); });
    connect(m_commitPage, &CommitPage::discardRequested, this, &MainWindow::discardChange);
    connect(m_commitPage, &CommitPage::refreshRequested, this, &MainWindow::refresh);
    connect(m_commitPage, &CommitPage::amendToggled, this, &MainWindow::onAmendToggled);
    connect(m_commitPage, &CommitPage::modeRequested, this, [this] {
        if (m_mode != CommitMode)
            setMode(CommitMode);
    });
    connect(m_commitPage, &CommitPage::statusMessage, this, &MainWindow::showStatus);
    m_stack->addWidget(m_commitPage);
    m_history = new HistoryView(m_repo);
    connect(m_history, &HistoryView::currentFileChanged, this, &MainWindow::showHistoryDiff);
    connect(m_history, &HistoryView::refreshRequested, this, &MainWindow::refresh);
    connect(m_history->filesTable(), &QTableView::doubleClicked, this, [this] {
        if (!m_diffVisible)
            setDiffPaneVisible(true);
    });
    m_stack->addWidget(m_history);
    leftLayout->addWidget(m_stack, 1);

    // ---- Right pane: diff view with navigation toolbar
    m_diffPane = new DiffPane;
    connect(m_diffPane->diffToggle(), &QToolButton::clicked, this, [this](bool on) { setDiffPaneVisible(on); });

    auto *splitter = new QSplitter(Qt::Horizontal);
    m_splitter = splitter;
    splitter->setHandleWidth(8);
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(left);
    splitter->addWidget(m_diffPane);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1); // the diff pane takes window resizes
    // The left width is chosen on first show (see showEvent) and remembered.
    connect(splitter, &QSplitter::splitterMoved, this, [this] {
        if (m_shown && m_left->isVisible())
            QSettings().setValue(settings::kWindowLeftWidth, m_splitter->sizes().first());
    });

    // ---- Mini rail: replaces the left section in the Mini layout
    m_rail = new MiniRail;
    m_rail->setSource(m_commitPage->proxy(), m_commitPage->table()->selectionModel());
    connect(m_rail, &MiniRail::commitModeRequested, this, [this] { setMode(CommitMode); });
    connect(m_rail, &MiniRail::historyModeRequested, this, [this] { setMode(HistoryMode); });
    connect(m_rail, &MiniRail::refreshRequested, this, &MainWindow::refresh);
    connect(m_rail, &MiniRail::activated, this, [this] {
        if (m_mode == CommitMode)
            openInEditor();
    });
    m_syncButtons << SyncButtons{m_rail->fetchButton(), m_rail->pullButton(), m_rail->pushButton()};
    for (const SyncButtons &set : std::as_const(m_syncButtons)) {
        connect(set.fetch, &QToolButton::clicked, m_sync, &RemoteSync::fetch);
        connect(set.pull, &QToolButton::clicked, m_sync, &RemoteSync::pull);
        connect(set.push, &QToolButton::clicked, m_sync, &RemoteSync::push);
    }
    updateSyncButtons();
    m_mergeButtons << m_rail->mergeButton();
    for (BadgeButton *b : std::as_const(m_mergeButtons))
        connect(b, &QToolButton::clicked, this, &MainWindow::showMergeDialog);
    updateMergeButtons(MergeState());

    auto *body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(kBodySpacing);
    body->addWidget(m_rail);
    body->addWidget(splitter, 1);
    rootLayout->addLayout(body, 1);

    // ---- Footer: sidebar toggle, repository and branch selectors, path and messages
    rootLayout->addWidget(m_footer);

    // Every keybinding at once, now that the widgets they belong to exist.
    installShortcuts();

    setCentralWidget(central);

    // Refresh the list when the working tree changes (coarse: repo root + .git index).
    m_watcher = new QFileSystemWatcher(this);
    auto *debounce = new QTimer(this);
    debounce->setSingleShot(true);
    debounce->setInterval(kWatchDebounceMs);
    connect(debounce, &QTimer::timeout, this, &MainWindow::refresh);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, debounce, qOverload<>(&QTimer::start));
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this, debounce](const QString &path) {
        // A file written through a rename (most editors, git itself for the
        // index) drops out of the watch: put it back.
        if (!m_watcher->files().contains(path) && QFile::exists(path))
            m_watcher->addPath(path);
        debounce->start();
    });
    watchWorkingTree();
    // ... and when refs move (a fetch, pull or push, also one made in a terminal).
    connect(m_sync, &RemoteSync::repositoryChanged, debounce, qOverload<>(&QTimer::start));
}

// ---------------------------------------------------------------------------
// Keybindings
//
// The one place a binding is written down: installShortcuts() makes the
// QShortcuts, showKeybindings() lists the same rows in the same order.
//
// The letters follow lazygit, with Ctrl in front (Ctrl+Shift for its
// capitals): R refresh, f fetch, p pull, P push, M merge, A amend, e edit,
// d discard, Ctrl+R recent repositories, Ctrl+S filter,
// Ctrl+W whitespace, Ctrl+L syntax colours, q quit.
// F5 by name: the platform's Refresh sequence includes Ctrl+R, which is the repositories.

QList<MainWindow::Binding> MainWindow::bindings()
{
    const QString commit = tr("Commit view"), history = tr("History"),
                  diff = tr("Diff"), merge = tr("Merge view");
    // Keys too familiar to write down: the shortcut stays, the panel row goes.
    const auto unlisted = [](Binding b) {
        b.listed = false;
        return b;
    };
    QList<Binding> list;

    // The application. The view shortcuts live on the window, not on the
    // toolbar buttons: a hidden button's shortcut is inactive, and the
    // toolbar is gone in the Mini layout.
    list << Binding{{QKeySequence(Qt::CTRL | Qt::Key_K)}, {}, tr("Keybindings"), {},
                    [this] { showKeybindings(); }, {}, nullptr, false}
         << Binding{{QKeySequence(Qt::CTRL | Qt::Key_1)}, {}, tr("Commit view"), {}, [this] { setMode(CommitMode); }}
         << Binding{{QKeySequence(Qt::CTRL | Qt::Key_2)}, {}, tr("History view"), {}, [this] { setMode(HistoryMode); }}
         // Ctrl+1 and Ctrl+2 are the views; the branches are the third "panel".
         << Binding{{QKeySequence(Qt::CTRL | Qt::Key_3)}, {}, tr("Branches"), {}, [this] { showBranchMenu(); }}
         << Binding{{QKeySequence(Qt::CTRL | Qt::Key_R)}, {}, tr("Recent repositories"), {}, [this] { showRepoMenu(); }}
         << Binding{{QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O)}, {}, tr("Clone repository…"), {},
                    [this] { showCloneDialog(); }}
         << Binding{{QKeySequence(QKeySequence::Open)}, {}, tr("Open repository…"), {},
                    [this] { openRepositoryDialog(); }}
         << Binding{{QKeySequence(Qt::Key_F5), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R)},
                    QStringLiteral("F5 / CTRL SHIFT + R"), tr("Refresh"), {}, [this] { refresh(); }}
         << Binding{{QKeySequence(Qt::CTRL | Qt::Key_F)}, {}, tr("Fetch"), {}, [this] { m_sync->fetch(); }, {}, m_sync}
         << Binding{{QKeySequence(Qt::CTRL | Qt::Key_P)}, {}, tr("Pull"), {}, [this] { m_sync->pull(); }, {}, m_sync}
         << Binding{{QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P)}, {}, tr("Push"), {}, [this] { m_sync->push(); }, {}, m_sync}
         << Binding{{QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M)}, {}, tr("Merge branches"), {},
                    [this] { showMergeDialog(); }}
         << Binding{{QKeySequence(Qt::CTRL | Qt::Key_B)}, {}, tr("Docked / Mini layout"), {}, [this] {
                        setPaneLayout(m_layout == PaneLayout::Mini ? PaneLayout::Docked : PaneLayout::Mini);
                    }}
         << Binding{{QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B)}, {}, tr("Show / hide diff pane"), {},
                    [this] { setDiffPaneVisible(!m_diffVisible); }}
         << Binding{{QKeySequence(Qt::CTRL | Qt::Key_Q)}, {}, tr("Quit"), {}, [this] { close(); }};

    // The commit view. Ctrl+Return belongs to the Commit button itself.
    // Space checks one file, so Ctrl+Shift+Space checks them all: a window
    // shortcut, unlike lazygit's Ctrl+A, which is select-all in every text
    // field and the diff. (Ctrl+Space is fcitx's input-method trigger.)
    list << Binding{{}, QStringLiteral("CTRL + RETURN"), tr("Commit checked files"), commit, [this] {
                        if (m_mode == CommitMode)
                            m_commitPage->clickCommit();
                    }}
         << Binding{{QKeySequence(Qt::CTRL | Qt::Key_G)}, {}, tr("Generate commit message"), commit,
                    [this] { generateMessage(); }}
         << Binding{{QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A)}, {}, tr("Amend last commit"), commit,
                    [this] { toggleAmend(); }}
         << Binding{{QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Space)}, QStringLiteral("CTRL SHIFT + SPACE"),
                    tr("Check all / none"), commit, [this] { toggleAllChecked(); }}
         << Binding{{}, QStringLiteral("SPACE"), tr("Check / uncheck file"), tr("Changes list, Mini rail")}
         << Binding{{}, QStringLiteral("CTRL + CLICK"), tr("Check / uncheck file"), tr("Mini rail")}
         << Binding{{QKeySequence(Qt::CTRL | Qt::Key_E)}, {}, tr("Open file in its program"), commit, [this] {
                        if (m_mode == CommitMode)
                            openInEditor();
                    }}
         << Binding{{QKeySequence(Qt::CTRL | Qt::Key_D)}, {}, tr("Discard file changes"), commit,
                    [this] { discardCurrent(); }};

    // History
    list << Binding{{QKeySequence(Qt::CTRL | Qt::Key_S)}, {}, tr("Filter commits"), history,
                    [this] { focusHistoryFilter(); }};

    // The diff pane. Its shortcuts are the window's too: the pane can be
    // hidden, and a hidden widget's shortcut does not fire.
    list << Binding{{QKeySequence(Qt::Key_F8)}, {}, tr("Next change"), diff,
                    [this] { m_diffPane->nextChange(); }, {}, m_diffPane}
         << Binding{{QKeySequence(Qt::SHIFT | Qt::Key_F8)}, {}, tr("Previous change"), diff,
                    [this] { m_diffPane->previousChange(); }, {}, m_diffPane}
         << Binding{{QKeySequence(Qt::CTRL | Qt::Key_T)}, {}, tr("One / two panes"), diff,
                    [this] { m_diffPane->togglePaneMode(); }, {}, m_diffPane}
         << Binding{{QKeySequence(Qt::CTRL | Qt::Key_W)}, {}, tr("Show whitespace"), diff,
                    [this] { m_diffPane->toggleWhitespace(); }, {}, m_diffPane}
         << Binding{{QKeySequence(Qt::CTRL | Qt::Key_L)}, {}, tr("Syntax highlighting"), diff,
                    [this] { m_diffPane->toggleSyntax(); }, {}, m_diffPane}
         << unlisted({{QKeySequence(Qt::CTRL | Qt::Key_Plus), QKeySequence(Qt::CTRL | Qt::Key_Equal)}, {},
                      tr("Zoom in"), diff, [this] { m_diffPane->zoomBy(1); }, {}, m_diffPane})
         << unlisted({{QKeySequence(Qt::CTRL | Qt::Key_Minus)}, {}, tr("Zoom out"), diff,
                      [this] { m_diffPane->zoomBy(-1); }, {}, m_diffPane})
         << unlisted({{QKeySequence(Qt::CTRL | Qt::Key_0)}, {}, tr("Reset zoom"), diff,
                      [this] { m_diffPane->resetZoom(); }, {}, m_diffPane});

    // The merge view handles these itself, so the panel only writes them down.
    list << Binding{{}, QStringLiteral("CTRL + S"), tr("Swap the two sides"), merge}
         << Binding{{}, QStringLiteral("RETURN"), tr("Merge"), merge};

    return list;
}

void MainWindow::installShortcuts()
{
    const QList<Binding> all = bindings();
    for (const Binding &b : all) {
        if (b.keys.isEmpty())
            continue;
        QObject *const receiver = b.receiver ? b.receiver : this;
        const QList<QWidget *> hosts = b.hosts.isEmpty() ? QList<QWidget *>{this} : b.hosts;
        // A binding with hosts of its own is meant for those widgets alone.
        const Qt::ShortcutContext scope = b.hosts.isEmpty() ? Qt::WindowShortcut : Qt::WidgetShortcut;
        for (QWidget *host : hosts) {
            for (const QKeySequence &keys : b.keys)
                new QShortcut(keys, host, receiver, b.run, scope);
        }
    }
}

void MainWindow::showKeybindings()
{
    auto *panel = new KeybindingsPanel(this);
    const QList<Binding> all = bindings();
    for (const Binding &b : all) {
        if (!b.listed)
            continue;
        panel->add(b.display.isEmpty() ? keysText(b.keys.first()) : b.display, b.action, b.context,
                   b.panelRuns ? b.run : std::function<void()>());
    }
    panel->popup();
}

void MainWindow::focusHistoryFilter()
{
    setMode(HistoryMode);
    m_history->focusFilter();
}

// Checks every file for the commit, or none when all are checked (lazygit's "a").
void MainWindow::toggleAllChecked()
{
    if (m_mode != CommitMode)
        return;
    m_commitPage->toggleAllChecked();
}

void MainWindow::toggleAmend()
{
    if (m_mode == CommitMode)
        m_commitPage->toggleAmend();
}

void MainWindow::setMode(Mode mode)
{
    m_mode = mode;
    m_stack->setCurrentWidget(mode == CommitMode ? static_cast<QWidget *>(m_commitPage) : m_history);
    {
        QSignalBlocker a(m_commitModeButton), b(m_historyModeButton);
        m_commitModeButton->setChecked(mode == CommitMode);
        m_historyModeButton->setChecked(mode == HistoryMode);
    }
    m_rail->setCommitMode(mode == CommitMode);
    if (mode == CommitMode)
        m_rail->setSource(m_commitPage->proxy(), m_commitPage->table()->selectionModel());
    else
        m_rail->setSource(m_history->filesTable()->model(), m_history->filesTable()->selectionModel());
    if (mode == HistoryMode) {
        if (m_historyDirty) {
            m_history->reload();
            m_historyDirty = false;
        }
        showHistoryDiff();
    } else {
        bool ok = false;
        const FileChange change = m_commitPage->currentChange(&ok);
        if (ok)
            showDiffFor(change);
        else
            m_diffPane->view()->clear(tr("Working tree clean — nothing to commit."));
    }
}

void MainWindow::setPaneLayout(PaneLayout layout, bool persist)
{
    m_layout = layout;
    if (layout == PaneLayout::Mini && !m_diffVisible)
        setDiffPaneVisible(true, persist); // the rail only makes sense next to the diff
    applyPanes();
    if (layout == PaneLayout::Mini)
        m_rail->list()->setFocus();
    if (persist)
        QSettings().setValue(settings::kWindowLayout, paneLayoutKey(layout));
}

void MainWindow::setDiffPaneVisible(bool on, bool persist)
{
    m_diffVisible = on;
    if (!on && m_layout == PaneLayout::Mini)
        setPaneLayout(PaneLayout::Docked, persist);
    applyPanes();
    if (persist)
        QSettings().setValue(settings::kWindowDiffPane, on);
}

// Shows the panes and buttons the current layout and diff toggle call for.
void MainWindow::applyPanes()
{
    const bool mini = m_layout == PaneLayout::Mini;
    m_left->setVisible(!mini);
    m_rail->setVisible(mini);
    m_diffPane->setVisible(m_diffVisible);
    m_commitPage->setDiffPaneVisible(m_diffVisible);
    // The toggle keeps its top-right spot: the end of the diff pane's nav row
    // while the pane shows, the end of the toolbar row while it is hidden.
    QToolButton *const diffToggle = m_diffPane->diffToggle();
    QHBoxLayout *home = m_diffVisible ? m_diffPane->navRow() : m_toolbarRow;
    if (home->indexOf(diffToggle) < 0) {
        (m_diffVisible ? m_toolbarRow : m_diffPane->navRow())->removeWidget(diffToggle);
        home->addWidget(diffToggle);
        diffToggle->show();
    }
    QToolButton *const layoutButton = m_footer->layoutButton();
    {
        QSignalBlocker a(layoutButton), b(diffToggle);
        layoutButton->setChecked(mini);
        diffToggle->setChecked(m_diffVisible);
    }
    layoutButton->setText(icon(paneLayoutGlyph(m_layout), paneLayoutName(m_layout).left(1)).trimmed());
    layoutButton->setToolTip(tr("Layout: %1").arg(paneLayoutTip(m_layout)));
    diffToggle->setToolTip(m_diffVisible ? tr("Hide the diff pane so the left section fills the window (Ctrl+Shift+B)")
                                         : tr("Show the diff pane (Ctrl+Shift+B)"));
}

void MainWindow::setAmend(bool on)
{
    m_commitPage->setAmendChecked(on);
}

void MainWindow::setAutoFetchEnabled(bool on)
{
    m_sync->setAutoFetchInterval(on ? autoFetchSecondsSetting() : 0);
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange)
        m_sync->setActive(isVisible() && !isMinimized());
    else if (event->type() == QEvent::ActivationChange && isActiveWindow())
        m_sync->nudge();
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    if (!m_shown) {
        m_shown = true;
        // Neither pane has a minimum width of its own, so the splitter's first
        // layout would split the window evenly; give the left section its
        // remembered width (or 45%) instead. The splitter has not been laid
        // out yet, so its width comes from the window's, less the rail.
        const QMargins m = centralWidget()->layout()->contentsMargins();
        const int total = width() - m.left() - m.right()
            - (m_rail->isVisibleTo(this) ? MiniRail::kWidth + kBodySpacing : 0);
        const int rightMin = m_diffPane->isVisibleTo(this) ? 1 + m_splitter->handleWidth() : 0;
        const int wanted = QSettings().value(settings::kWindowLeftWidth, total * kLeftSharePercent / 100).toInt();
        const int left = qBound(1, wanted, qMax(1, total - rightMin));
        m_splitter->setSizes({left, qMax(1, total - left)});
    }
    m_sync->setActive(!isMinimized());
}

void MainWindow::hideEvent(QHideEvent *event)
{
    QMainWindow::hideEvent(event);
    m_sync->setActive(false);
}

void MainWindow::applyTheme()
{
    const OmarchyTheme *theme = OmarchyTheme::instance();
    m_diffPane->applyTheme();
    m_toolbar->applyTheme();
    m_commitPage->applyTheme();
    m_footer->applyTheme();
    m_history->applyTheme();
    m_rail->applyTheme();
    // The captions of every section, wherever they were built.
    for (QLabel *l : findChildren<QLabel *>()) {
        if (l->objectName() == QLatin1String("sectionLabel") || l->objectName() == QLatin1String("dimLabel"))
            l->setFont(theme->captionFont());
    }
}

// ---------------------------------------------------------------------------
// Refresh

void MainWindow::refresh()
{
    bool hadCurrent = false;
    const FileChange current = m_commitPage->currentChange(&hadCurrent);
    QString selectedPath = hadCurrent ? current.path : QString();
    if (!m_initialSelection.isEmpty()) {
        selectedPath = m_initialSelection;
        m_initialSelection.clear();
    }

    updateHeader();
    // Re-selecting the row below scrolls the views to it and reloads the
    // diff from its first change; the user may have scrolled either on
    // purpose, so put the scroll offsets (and the current change) back after.
    const ViewState state = viewState();
    reloadChanges();
    restoreSelection(selectedPath, hadCurrent && current.path == selectedPath, state);
    reloadHistory(state);
}

// The branch label, the merge buttons, what a merge in progress does to the
// commit page, and the sync counts.
void MainWindow::updateHeader()
{
    const Commit head = m_repo->headCommit();
    const MergeState merge = m_repo->mergeState();
    QString branch = icon(kBranch) + m_repo->branch() + chevron();
    if (m_repo->amending() && head.isValid())
        branch += tr("   ·   amending %1").arg(head.shortHash);
    if (merge.inProgress)
        branch += tr("   ·   merging %1").arg(merge.source);
    m_footer->branchButton()->setText(branch);
    updateMergeButtons(merge);
    m_commitPage->setMergeState(merge, head);
    m_sync->refreshState();
}

MainWindow::ViewState MainWindow::viewState() const
{
    ViewState state;
    state.changes = m_commitPage->scrollOffset();
    state.rail = m_rail->list()->verticalScrollBar()->value();
    state.diff = m_diffPane->view()->viewState();
    return state;
}

void MainWindow::reloadChanges()
{
    m_refreshing = true;
    m_commitPage->reload();
    m_refreshing = false;
    watchChangedFiles();
}

void MainWindow::restoreSelection(const QString &path, bool sameFile, const ViewState &state)
{
    if (m_commitPage->selectPath(path)) {
        m_commitPage->setScrollOffset(state.changes);
        m_rail->list()->verticalScrollBar()->setValue(state.rail);
        if (sameFile && m_mode == CommitMode) {
            // The row may still be current (no reset happened), so show the
            // diff again by hand: identical content is left alone, changed
            // content is put back where the user was reading.
            showCurrentDiff();
            m_diffPane->view()->restoreViewState(state.diff);
        }
    } else if (!m_commitPage->selectFirstRow() && m_mode == CommitMode) {
        m_diffPane->view()->clear(tr("Working tree clean — nothing to commit."));
    }
    m_commitPage->onCheckedChanged();
}

void MainWindow::reloadHistory(const ViewState &state)
{
    m_historyDirty = true;
    if (m_mode != HistoryMode)
        return;
    Commit commit;
    FileChange file;
    const bool hadFile = m_history->currentFile(&commit, &file);
    m_history->reload();
    m_historyDirty = false;
    Commit newCommit;
    FileChange newFile;
    if (hadFile && m_history->currentFile(&newCommit, &newFile) && newCommit.hash == commit.hash
        && newFile.path == file.path) {
        showHistoryDiff();
        m_rail->list()->verticalScrollBar()->setValue(state.rail);
        m_diffPane->view()->restoreViewState(state.diff);
    }
}

void MainWindow::discardCurrent()
{
    bool ok = false;
    const FileChange c = m_commitPage->currentChange(&ok);
    if (ok && m_mode == CommitMode)
        discardChange(c);
}

// Restores the file to the latest commit (deletes it when git does not know
// it), after asking: there is no way back.
void MainWindow::discardChange(const FileChange &change)
{
    const QString question = change.isUntracked()
        ? tr("Delete %1?\n\nThe file is not in git, so this cannot be undone.").arg(change.path)
        : tr("Discard the changes of %1?\n\nThe file goes back to the latest commit, staged changes included; this cannot be undone.")
              .arg(change.path);
    QMessageBox box(QMessageBox::Question, tr("Discard changes"), question, QMessageBox::Cancel, this);
    QPushButton *discard = box.addButton(tr("Discard"), QMessageBox::DestructiveRole);
    box.setDefaultButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() != discard)
        return;
    QString error;
    if (!m_repo->discardChanges(change, &error))
        QMessageBox::critical(this, tr("Discard changes failed"), error);
    refresh();
}

void MainWindow::onCurrentRowChanged(const QModelIndex &current)
{
    if (m_mode != CommitMode)
        return;
    if (!current.isValid()) {
        if (!m_refreshing) // refresh() selects a row again right after the reset
            m_diffPane->view()->clear();
        return;
    }
    showCurrentDiff();
}

void MainWindow::presentDiff(const QString &unified, const FileChange &change, bool binary, const QString &leftLabel,
                             const QString &rightLabel, const QString &emptyMessage)
{
    DiffView *const diff = m_diffPane->view();
    const QString key = QStringList{change.path, change.statusText(), leftLabel, rightLabel, emptyMessage,
                                    binary ? QStringLiteral("1") : QStringLiteral("0"), unified}
                            .join(QChar(0));
    if (key == m_shownDiffKey && !diff->document().lines.isEmpty())
        return; // the same document is on screen: keep the selection and the scroll position
    m_shownDiffKey = key;
    DiffDocument doc = DiffModel::parse(unified);
    if (binary && doc.lines.isEmpty()) {
        doc.binary = true;
        doc.message = tr("Binary file — no textual diff available.");
    }
    QString subtitle = change.statusText();
    QString summary = change.statusText();
    if (!doc.lines.isEmpty()) {
        subtitle += tr("   +%1  −%2").arg(doc.added).arg(doc.removed);
        summary += tr("  +%1 −%2").arg(doc.added).arg(doc.removed);
    } else if (doc.message.isEmpty()) {
        doc.message = emptyMessage;
    }
    m_diffPane->setSummary(summary);
    diff->setDocument(doc, change.path, subtitle, leftLabel, rightLabel);
    if (!doc.blockStarts.isEmpty())
        diff->firstChange();
}

// `base` names the left-hand side (HEAD, HEAD~1 or the parent commit) and
// `baseLabel` stands in for it where it has no name (the empty tree under a
// root commit); `unchanged` is the line for a diff without textual changes.
MainWindow::DiffLabels MainWindow::diffLabels(const FileChange &change, const QString &base, const QString &baseLabel,
                                              const QString &right, const QString &unchanged)
{
    DiffLabels labels;
    labels.left = baseLabel;
    if (change.isUntracked() || (change.kind == FileChange::Added && change.oldPath.isEmpty()))
        labels.left = tr("(new file)");
    else if (!change.oldPath.isEmpty())
        labels.left = tr("%1: %2").arg(base, change.oldPath);
    labels.right = change.kind == FileChange::Deleted ? tr("(deleted)") : right;
    labels.empty = change.oldPath.isEmpty() ? unchanged
                                            : tr("Renamed from %1 — contents unchanged.").arg(change.oldPath);
    return labels;
}

void MainWindow::showCurrentDiff()
{
    bool ok = false;
    const FileChange change = m_commitPage->currentChange(&ok);
    if (ok)
        showDiffFor(change);
}

void MainWindow::showDiffFor(const FileChange &change)
{
    bool binary = change.binary;
    const QString unified = m_repo->diff(change, &binary);
    const QString base = m_repo->amending() ? tr("HEAD~1") : tr("HEAD");
    const QString staged = change.isStaged() && change.worktree == ' ' ? tr("Staged — identical to %1.").arg(base)
                                                                      : QString();
    const DiffLabels labels = diffLabels(change, base, base, tr("Working Tree"), staged);
    presentDiff(unified, change, binary, labels.left, labels.right, labels.empty);
}

void MainWindow::showHistoryDiff()
{
    if (m_mode != HistoryMode)
        return;
    {
        bool ok = false;
        const Commit cur = m_history->currentCommit(&ok);
        m_rail->setCommitLabel(ok ? cur.shortHash : QString(), ok ? cur.subject : QString());
    }
    Commit c;
    FileChange f;
    if (!m_history->currentFile(&c, &f)) {
        m_diffPane->setSummary(QString());
        m_diffPane->view()->clear(m_history->emptyMessage());
        return;
    }
    bool binary = f.binary;
    const QString unified = m_repo->commitDiff(c, f, &binary);
    const QString parent = c.parents.isEmpty() ? QString() : c.parents.first().left(c.shortHash.size());
    const DiffLabels labels = diffLabels(f, parent, parent.isEmpty() ? tr("(empty tree)") : parent, c.shortHash,
                                         tr("No textual changes."));
    presentDiff(unified, f, binary, labels.left, labels.right, labels.empty);
}

// The commit page has ticked or unticked the amend box: the changes list
// against the commit before, and its files checked.
void MainWindow::onAmendToggled(bool on)
{
    refresh();
    if (on)
        m_commitPage->checkHeadPaths();
}

// ---------------------------------------------------------------------------
// Fetch, pull and push

// The tooltips of the three sync buttons and the upstream line of the branch
// button, from the state alone so the slot below only hands them out.
MainWindow::SyncTips MainWindow::syncTips(const UpstreamState &s, RemoteSync::Op op, const FetchHistory &fetches,
                                          bool pushPublishes, const QStringList &pushArgs)
{
    SyncTips tips;

    if (s.remotes.isEmpty()) {
        tips.fetch = tr("No remote configured — nothing to fetch from");
    } else {
        tips.fetch = tr("Fetch from all remotes (Ctrl+F)");
        if (op == RemoteSync::Fetch)
            tips.fetch += tr("\nFetching…");
        else if (fetches.last.isValid() && fetches.ok)
            tips.fetch += tr("\nLast fetched %1").arg(ago(fetches.last));
        else if (fetches.last.isValid())
            tips.fetch += tr("\nLast fetch failed %1: %2").arg(ago(fetches.last), fetches.error);
        if (fetches.interval > 0)
            tips.fetch += tr("\nFetches by itself every %n minute(s) while the window is open", nullptr,
                             qMax(1, fetches.interval / 60));
        else
            tips.fetch += tr("\nAutomatic fetching is off (remote/autoFetchSeconds in omagit.conf)");
    }

    if (s.detached)
        tips.pull = tr("HEAD is detached — check out a branch to pull");
    else if (s.branch.isEmpty())
        tips.pull = tr("Nothing to pull into yet");
    else if (s.upstreamGone)
        tips.pull = tr("%1 no longer exists on the remote").arg(s.upstream);
    else if (!s.hasUpstream())
        tips.pull = tr("%1 has no upstream branch to pull from").arg(s.branch);
    else if (op == RemoteSync::Fetch)
        tips.pull = tr("Checking %1 for new commits…").arg(s.upstream);
    else if (op == RemoteSync::Pull)
        tips.pull = tr("Pulling from %1…").arg(s.upstream);
    else if (s.behind > 0)
        tips.pull = tr("Pull %n commit(s) from %1 into %2 (Ctrl+P)", nullptr, s.behind).arg(s.upstream, s.branch);
    else
        tips.pull = tr("Pull from %1 — nothing new since the last fetch (Ctrl+P)").arg(s.upstream);

    if (s.detached)
        tips.push = tr("HEAD is detached — check out a branch to push");
    else if (s.branch.isEmpty())
        tips.push = tr("Nothing to push yet");
    else if (s.remote.isEmpty())
        tips.push = tr("No remote to push to");
    else if (op == RemoteSync::Push)
        tips.push = tr("Pushing to %1…").arg(s.remote);
    else if (pushPublishes)
        tips.push = tr("Publish %1 on %2 and track it from now on — git %3 (Ctrl+Shift+P)")
                        .arg(s.branch, s.remote, pushArgs.join(QLatin1Char(' ')));
    else if (s.ahead > 0)
        tips.push = tr("Push %n commit(s) from %1 to %2 (Ctrl+Shift+P)", nullptr, s.ahead).arg(s.branch, s.upstream);
    else
        tips.push = tr("Push to %1 — nothing to push (Ctrl+Shift+P)").arg(s.upstream);

    // The branch name tells about its upstream on hover.
    QString upstream;
    if (s.upstreamGone)
        upstream = tr("Upstream %1 no longer exists on the remote").arg(s.upstream);
    else if (s.hasUpstream())
        upstream = tr("Upstream %1 — %2 to push, %3 to pull").arg(s.upstream).arg(qMax(0, s.ahead)).arg(qMax(0, s.behind));
    else if (!s.branch.isEmpty() && !s.remote.isEmpty())
        upstream = tr("No upstream — Push publishes %1 on %2").arg(s.branch, s.remote);
    else if (!s.branch.isEmpty() && s.remotes.isEmpty())
        upstream = tr("No remote configured");
    if (!upstream.isEmpty())
        upstream += QLatin1Char('\n');
    tips.upstream = upstream + tr("Click or Ctrl+3 to switch to another branch");
    return tips;
}

void MainWindow::updateSyncButtons()
{
    const OmarchyTheme *theme = OmarchyTheme::instance();
    const UpstreamState &s = m_sync->state();
    const RemoteSync::Op op = m_sync->runningOp();
    const FetchHistory fetches{m_sync->lastFetch(), m_sync->lastFetchOk(), m_sync->lastFetchError(),
                               m_sync->autoFetchInterval()};
    const SyncTips tips = syncTips(s, op, fetches, m_sync->pushPublishes(), m_sync->pushArgs());

    const QColor red = theme->color(QStringLiteral("red"));
    for (const SyncButtons &b : std::as_const(m_syncButtons)) {
        b.fetch->setEnabled(m_sync->canFetch());
        b.fetch->setToolTip(tips.fetch);
        b.fetch->setMark(m_sync->lastFetch().isValid() && !m_sync->lastFetchOk() ? QStringLiteral("!") : QString(), red);
        b.pull->setEnabled(m_sync->canPull());
        b.pull->setToolTip(tips.pull);
        b.pull->setBusy(op == RemoteSync::Fetch || op == RemoteSync::Pull);
        b.pull->setCount(s.hasUpstream() ? s.behind : 0);
        b.pull->setMark(s.upstreamGone ? QStringLiteral("!") : QString(), red);
        b.push->setEnabled(m_sync->canPush());
        b.push->setToolTip(tips.push);
        b.push->setBusy(op == RemoteSync::Push);
        b.push->setCount(s.hasUpstream() ? s.ahead : 0);
    }
    m_footer->branchButton()->setToolTip(tips.upstream);
}

void MainWindow::onSyncFinished(RemoteSync::Op op, bool ok, bool automatic, const QString &message)
{
    // A sign-in the user closed is not something to be told off about: the
    // footer says it quietly and the operation is simply not done.
    if (!ok && !automatic && !m_sync->signInCancelled()) {
        const QString title = op == RemoteSync::Fetch ? tr("Fetch failed")
                            : op == RemoteSync::Pull  ? tr("Pull failed")
                                                      : tr("Push failed");
        QMessageBox::critical(this, title, message);
    }
    showStatus(message.section(QLatin1Char('\n'), 0, 0), ok ? kMediumStatusMs : kErrorStatusMs);
    if (op != RemoteSync::Fetch || ok)
        refresh();
}

// ---------------------------------------------------------------------------
// Signing in

void MainWindow::onAskPassRequest(const AskPassRequest &request)
{
    AskPass *askPass = m_sync->askPass();
    auto *dialog = new LoginDialog(request, m_repo, this);
    // Which asking this dialog belongs to travels with it, and every word it
    // has for AskPass names it: what is typed here answers this request or
    // nothing.
    const int id = request.id;
    connect(dialog, &QDialog::accepted, askPass, [askPass, dialog, id] {
        // A sign-in to a host is given once: the dialog collected both halves,
        // and AskPass answers the rest of the operation's prompts with them.
        // A passphrase (or any other question) is answered as it was asked.
        const AskPassRequest::Kind kind = dialog->request().kind;
        if (kind == AskPassRequest::Username || kind == AskPassRequest::Password)
            askPass->answerLogin(id, dialog->username(), dialog->password());
        else
            askPass->answerSecret(id, dialog->password());
    });
    connect(dialog, &QDialog::rejected, askPass, [askPass, id] { askPass->cancel(id); });
    // Nobody is waiting for this one any more: git let it go, or the operation
    // ended under it. The dialog goes without a word — closing it is what ends
    // it, not a refusal, and AskPass hears nothing of either.
    connect(askPass, &AskPass::requestDropped, dialog, [askPass, dialog](int dropped) {
        if (dropped != dialog->request().id)
            return;
        disconnect(dialog, nullptr, askPass, nullptr);
        dialog->close();
    });
    dialog->show();
}

void MainWindow::showLoginDialog()
{
    const AskPassRequest sample = parseAskPassPrompt(QStringLiteral("Username for 'https://github.com': "));
    (new LoginDialog(sample, m_repo, this))->show();
}

void MainWindow::openInEditor()
{
    bool ok = false;
    const FileChange c = m_commitPage->currentChange(&ok);
    if (!ok || c.kind == FileChange::Deleted)
        return;
    const QString path = QDir(m_repo->root()).filePath(c.path);
    QString error;
    if (!openWithDefaultApp(path, m_repo->root(), &error, c.path))
        QMessageBox::critical(this, tr("Open failed"), error);
}

// ---------------------------------------------------------------------------
// Branches

void MainWindow::showBranchMenu()
{
    const BranchList branches = m_repo->branches();
    BranchMenu menu(this);
    menu.setBranches(branches, branches.current, true, [&branches](const QString &name, bool remote) {
        if (!remote)
            return tr("Switch to %1").arg(name);
        const QString local = name.section(QLatin1Char('/'), 1);
        return branches.local.contains(local) ? tr("Switch to the local branch %1").arg(local)
                                              : tr("Create the local branch %1 tracking %2 and switch to it").arg(local, name);
    });
    connect(&menu, &BranchMenu::picked, this, &MainWindow::checkoutBranch);
    menu.popupAt(m_footer->branchButton(), true);
}

void MainWindow::checkoutBranch(const QString &name)
{
    if (name == m_repo->branches().current)
        return;
    QString error;
    if (!m_repo->checkout(name, &error)) {
        QMessageBox::critical(this, tr("Switch branch"), tr("Could not switch to %1.\n\n%2").arg(name, error));
        return;
    }
    refresh();
    showStatus(tr("Switched to %1").arg(m_repo->branch()), kShortStatusMs);
}

// ---------------------------------------------------------------------------
// Merging

void MainWindow::showMergeDialog()
{
    auto *dialog = new MergeDialog(m_repo, this);
    connect(dialog, &MergeDialog::merged, this,
            [this](const QString &source, const QString &destination, int conflicts, bool fastForward) {
                refresh();
                if (conflicts > 0) {
                    // The conflicted files wait in the Changes list, red, with
                    // git's message in the box; the first of them is selected.
                    setMode(CommitMode);
                    m_commitPage->selectFirstConflict();
                    const QString count = conflicts == 1 ? tr("1 conflicted file") : tr("%1 conflicted files").arg(conflicts);
                    showStatus(tr("Merging %1 into %2 — %3 to resolve, then Commit merge").arg(source, destination, count),
                               kPendingStatusMs);
                } else if (fastForward) {
                    showStatus(tr("Fast-forwarded %2 to %1").arg(source, destination), kMediumStatusMs);
                } else {
                    showStatus(tr("Merged %1 into %2").arg(source, destination), kMediumStatusMs);
                }
            });
    connect(dialog, &MergeDialog::mergeAborted, this, [this](const QString &source, const QString &destination) {
        refresh();
        showStatus(tr("Aborted the merge of %1 into %2").arg(source, destination), kMediumStatusMs);
    });
    dialog->show();
}

void MainWindow::updateMergeButtons(const MergeState &merge)
{
    const OmarchyTheme *theme = OmarchyTheme::instance();
    QString tip;
    if (merge.inProgress) {
        tip = merge.conflicts.isEmpty()
            ? tr("A merge of %1 is in progress — Commit merge finishes it; click to abort instead (Ctrl+Shift+M)").arg(merge.source)
            : tr("A merge of %1 is in progress with %n conflicted file(s) — resolve them and Commit merge, or click to abort (Ctrl+Shift+M)",
                 nullptr, merge.conflicts.size())
                  .arg(merge.source);
    } else {
        tip = tr("Merge another branch into this one — with a look at what it would do first (Ctrl+Shift+M)");
    }
    for (BadgeButton *b : std::as_const(m_mergeButtons)) {
        b->setToolTip(tip);
        b->setMark(merge.inProgress ? QStringLiteral("!") : QString(), theme->color(QStringLiteral("red")));
    }
}

// ---------------------------------------------------------------------------
// Repositories

QStringList MainWindow::recentRepositories()
{
    QSettings conf;
    const QStringList stored = conf.value(settings::kRecentRepositories).toStringList();
    QStringList list;
    for (const QString &root : stored) {
        if (QFileInfo(root).isDir() && !list.contains(root))
            list << root;
    }
    if (list != stored)
        conf.setValue(settings::kRecentRepositories, list);
    return list;
}

void MainWindow::rememberRepository(const QString &root)
{
    QStringList list = recentRepositories();
    list.removeAll(root);
    list.prepend(root);
    while (list.size() > kRecentMax)
        list.removeLast();
    QSettings().setValue(settings::kRecentRepositories, list);
}

void MainWindow::showRepoMenu()
{
    TickMenu menu(this);
    menu.setToolTipsVisible(true);
    const QStringList recent = recentRepositories();
    QHash<QString, int> names; // two repositories called "app" are told apart by their parent
    for (const QString &root : recent)
        ++names[QDir(root).dirName()];
    for (const QString &root : recent) {
        const QString name = QDir(root).dirName();
        QString text = icon(kFolder) + name;
        if (names.value(name) > 1)
            text += QStringLiteral("   ·   ") + tildePath(QFileInfo(root).path());
        QAction *a = menu.addAction(text);
        a->setCheckable(true);
        a->setChecked(root == m_repo->root());
        a->setToolTip(root);
        connect(a, &QAction::triggered, this, [this, root] { openRepository(root); });
    }
    if (!recent.isEmpty())
        menu.addSeparator();
    QAction *open = menu.addAction(icon(kFolderOpen) + tr("Open…"));
    open->setToolTip(tr("Pick a folder inside a git repository (Ctrl+O)"));
    connect(open, &QAction::triggered, this, &MainWindow::openRepositoryDialog);
    QAction *clone = menu.addAction(icon(kFetch) + tr("Clone…"));
    clone->setToolTip(tr("Download a repository from a URL or GitHub (Ctrl+Shift+O)"));
    connect(clone, &QAction::triggered, this, &MainWindow::showCloneDialog);
    const int menuY = -menu.sizeHint().height();
    menu.exec(m_footer->repoButton()->mapToGlobal(QPoint(0, menuY)));
}

void MainWindow::showCloneDialog()
{
    auto *dialog = new CloneDialog(CloneDialog::defaultFolder(m_repo->root()), this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QDialog::accepted, this, [this, dialog] { openRepository(dialog->repositoryPath()); });
    dialog->show();
}

void MainWindow::openRepositoryDialog()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Open repository"), QFileInfo(m_repo->root()).path(),
                                                          QFileDialog::ShowDirsOnly);
    if (!dir.isEmpty())
        openRepository(dir);
}

bool MainWindow::openRepository(const QString &path)
{
    QString error;
    const QString root = GitRepo::findRoot(path, &error);
    if (root.isEmpty()) {
        QMessageBox::warning(this, tr("Open repository"),
                             tr("%1 is not inside a git repository.\n\n%2").arg(tildePath(path), error));
        return false;
    }
    if (root == m_repo->root())
        return true;

    m_initialSelection.clear();
    m_repo->setRoot(root); // RemoteSync follows through rootChanged
    // The amend state belonged to the old repository.
    m_commitPage->resetAmend();
    watchWorkingTree();
    updateRepoLabels();
    rememberRepository(root);
    showStatus(tr("Opened %1").arg(tildePath(root)), kShortStatusMs);
    return true;
}

void MainWindow::watchWorkingTree()
{
    if (!m_watcher->directories().isEmpty())
        m_watcher->removePaths(m_watcher->directories());
    if (!m_watcher->files().isEmpty())
        m_watcher->removePaths(m_watcher->files());
    m_watcher->addPath(m_repo->root());
    m_indexFile = m_repo->gitDir() + QStringLiteral("/index");
    if (QFile::exists(m_indexFile))
        m_watcher->addPath(m_indexFile);
}

// The root directory watch only sees files appearing, disappearing or being
// renamed; an edit written in place shows up only through the file itself.
// Watching every file of the tree is out of the question, but the changed
// ones (the ones whose diff is on screen) are few.
void MainWindow::watchChangedFiles()
{
    constexpr int kMaxWatchedFiles = 500;
    QStringList wanted;
    if (QFile::exists(m_indexFile))
        wanted << m_indexFile;
    const QDir root(m_repo->root());
    const QStringList changed = m_commitPage->paths();
    for (const QString &relative : changed) {
        if (wanted.size() > kMaxWatchedFiles)
            break;
        const QString path = root.filePath(relative);
        if (QFileInfo(path).isFile())
            wanted << path;
    }
    QStringList stale;
    const QStringList watched = m_watcher->files();
    for (const QString &p : watched)
        if (!wanted.contains(p))
            stale << p;
    if (!stale.isEmpty())
        m_watcher->removePaths(stale);
    QStringList fresh;
    for (const QString &p : std::as_const(wanted))
        if (!watched.contains(p))
            fresh << p;
    if (!fresh.isEmpty())
        m_watcher->addPaths(fresh);
}

void MainWindow::updateRepoLabels()
{
    const QString name = QDir(m_repo->root()).dirName();
    setWindowTitle(QStringLiteral("Omagit — %1").arg(name));
    m_footer->repoButton()->setText(icon(kFolder) + name + chevron());
    m_footer->repoButton()->setToolTip(tr("%1\nClick or Ctrl+R for the repositories opened lately, Ctrl+O to open another one")
                                           .arg(m_repo->root()));
    m_footer->setIdleText(tildePath(m_repo->root()));
}

// ---- Commit message from a coding agent ------------------------------------
// The flow lives on the commit page; these keep the names main.cpp and the
// window's shortcuts reach it by.

void MainWindow::generateMessage()
{
    m_commitPage->generateMessage();
}

void MainWindow::showAgentMenu()
{
    m_commitPage->showAgentMenu();
}

void MainWindow::showStatus(const QString &text, int ms)
{
    m_footer->showStatus(text, ms);
}
