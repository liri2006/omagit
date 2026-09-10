#include "MainWindow.h"
#include "BadgeButton.h"
#include "BranchMenu.h"
#include "ChangesModel.h"
#include "CommitPage.h"
#include "DesktopExec.h"
#include "DiffPane.h"
#include "Footer.h"
#include "MergeDialog.h"
#include "DiffModel.h"
#include "DiffView.h"
#include "HistoryView.h"
#include "KeybindingsPanel.h"
#include "MessageEdit.h"
#include "MiniRail.h"
#include "OmarchyTheme.h"
#include "TickMenu.h"
#include "Toolbar.h"
#include "UiHelpers.h"

#include <QAction>
#include <QButtonGroup>
#include <QCheckBox>
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

    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    if (!settings.contains(QStringLiteral("window/geometry")))
        resize(1400, 850);
    // "full" (pre-0.4) and window/leftFull (pre-0.3) meant the left section
    // alone; both become Docked with the diff pane hidden (and are re-saved so).
    const QString layoutKey = settings.value(QStringLiteral("window/layout")).toString();
    const bool legacyFull = layoutKey == QLatin1String("full")
        || (layoutKey.isEmpty() && settings.value(QStringLiteral("window/leftFull"), false).toBool());
    setPaneLayout(paneLayoutFromKey(layoutKey));
    setDiffPaneVisible(!legacyFull && settings.value(QStringLiteral("window/diffPane"), true).toBool());
    m_sync->setAutoFetchInterval(settings.value(QStringLiteral("remote/autoFetchSeconds"), 180).toInt());

    QTimer::singleShot(0, this, &MainWindow::refresh);
}

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(14, 12, 14, 8);
    rootLayout->setSpacing(8);

    // ---- Left section: toolbar above (commit dialog | history)
    auto *left = new QWidget;
    m_left = left;
    // The pane may be dragged as narrow as the user likes: the toolbar folds
    // its buttons away, everything else just gets cut off at the edge.
    left->setMinimumWidth(1);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(8);

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
    // Shortcuts live on the window, not the buttons: a hidden button's shortcut
    // is inactive, and the toolbar is hidden in the Mini layout.
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_1), this, this, [this] { setMode(CommitMode); });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_2), this, this, [this] { setMode(HistoryMode); });
    // The letters follow lazygit, with Ctrl in front (Ctrl+Shift for its
    // capitals): R refresh, f fetch, p pull, P push, M merge, a stage all,
    // A amend, e edit, d discard, Ctrl+R recent repositories, Ctrl+S filter,
    // Ctrl+W whitespace, Ctrl+L syntax colours, q quit.
    // F5 by name: the platform's Refresh sequence includes Ctrl+R, which is the repositories.
    new QShortcut(QKeySequence(Qt::Key_F5), this, this, &MainWindow::refresh);
    new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R), this, this, &MainWindow::refresh);
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Q), this, this, &QWidget::close);
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_B), this, this, [this] {
        setPaneLayout(m_layout == PaneLayout::Mini ? PaneLayout::Docked : PaneLayout::Mini);
    });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B), this, this, [this] {
        setDiffPaneVisible(!m_diffVisible);
    });

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
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_F), this, m_sync, &RemoteSync::fetch);
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_P), this, m_sync, &RemoteSync::pull);
    new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P), this, m_sync, &RemoteSync::push);
    connect(m_sync, &RemoteSync::stateChanged, this, &MainWindow::updateSyncButtons);
    connect(m_sync, &RemoteSync::finished, this, &MainWindow::onSyncFinished);
    // Merge opens the merge view; its badge says when a merge waits with conflicts.
    m_toolbar->addSeparator();
    auto *mergeButton = toolButton<BadgeButton>(QString());
    m_toolbar->addButton(mergeButton, icon(kMerge) + tr("Merge"), icon(kMerge, tr("M")).trimmed(), tr("Merge"));
    m_mergeButtons << mergeButton;
    new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M), this, this, &MainWindow::showMergeDialog);
    m_toolbarRow = new QHBoxLayout;
    m_toolbarRow->setSpacing(8);
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
    new QShortcut(QKeySequence::Open, this, this, &MainWindow::openRepositoryDialog);
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_R), this, this, &MainWindow::showRepoMenu);
    connect(m_footer->branchButton(), &QToolButton::clicked, this, &MainWindow::showBranchMenu);
    // Ctrl+1 and Ctrl+2 are the views; the branches are the third "panel".
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_3), this, this, &MainWindow::showBranchMenu);
    connect(m_footer->keybindingsButton(), &QToolButton::clicked, this, &MainWindow::showKeybindings);
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_K), this, this, &MainWindow::showKeybindings);

    m_stack = new QStackedWidget;
    m_commitPage = new CommitPage(m_repo);
    connect(m_commitPage, &CommitPage::currentRowChanged, this, &MainWindow::onCurrentRowChanged);
    connect(m_commitPage, &CommitPage::openRequested, this, &MainWindow::openInEditor);
    connect(m_commitPage, &CommitPage::showDiffPaneRequested, this, [this] { setDiffPaneVisible(true); });
    connect(m_commitPage, &CommitPage::discardRequested, this, &MainWindow::discardChange);
    connect(m_commitPage, &CommitPage::refreshRequested, this, &MainWindow::refresh);
    connect(m_commitPage, &CommitPage::commitRequested, this, &MainWindow::commit);
    connect(m_commitPage, &CommitPage::amendToggled, this, &MainWindow::onAmendToggled);
    connect(m_commitPage, &CommitPage::modeRequested, this, [this] {
        if (m_mode != CommitMode)
            setMode(CommitMode);
    });
    connect(m_commitPage, &CommitPage::statusMessage, this, &MainWindow::showStatus);
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_G), this, this, &MainWindow::generateMessage);
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
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_S), this, this, &MainWindow::focusHistoryFilter);

    // ---- Right pane: diff view with navigation toolbar
    m_diffPane = new DiffPane;
    connect(m_diffPane->diffToggle(), &QToolButton::clicked, this, [this](bool on) { setDiffPaneVisible(on); });
    // On the window, not the pane's buttons: they may be hidden with it.
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_T), this, m_diffPane, &DiffPane::togglePaneMode);
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_W), this, m_diffPane, &DiffPane::toggleWhitespace);
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_L), this, m_diffPane, &DiffPane::toggleSyntax);
    new QShortcut(QKeySequence(Qt::Key_F8), this, m_diffPane, &DiffPane::nextChange);
    new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F8), this, m_diffPane, &DiffPane::previousChange);
    for (const Qt::Key key : {Qt::Key_Plus, Qt::Key_Equal})
        new QShortcut(QKeySequence(Qt::CTRL | key), this, m_diffPane, [this] { m_diffPane->zoomBy(1); });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus), this, m_diffPane, [this] { m_diffPane->zoomBy(-1); });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_0), this, m_diffPane, &DiffPane::resetZoom);

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
            QSettings().setValue(QStringLiteral("window/leftWidth"), m_splitter->sizes().first());
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
    body->setSpacing(8);
    body->addWidget(m_rail);
    body->addWidget(splitter, 1);
    rootLayout->addLayout(body, 1);

    // ---- Footer: sidebar toggle, repository and branch selectors, path and messages
    rootLayout->addWidget(m_footer);

    // The commit view's file actions. Ctrl+A checks the files rather than
    // selecting rows, so it sits on the two lists, where a text field never
    // sees it.
    for (QWidget *list : {static_cast<QWidget *>(m_commitPage->table()), static_cast<QWidget *>(m_rail->list())})
        new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_A), list, this, &MainWindow::toggleAllChecked, Qt::WidgetShortcut);
    new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A), this, this, &MainWindow::toggleAmend);
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_E), this, this, [this] {
        if (m_mode == CommitMode)
            openInEditor();
    });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_D), this, this, &MainWindow::discardCurrent);

    setCentralWidget(central);

    // Refresh the list when the working tree changes (coarse: repo root + .git index).
    m_watcher = new QFileSystemWatcher(this);
    auto *debounce = new QTimer(this);
    debounce->setSingleShot(true);
    debounce->setInterval(500);
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

void MainWindow::showKeybindings()
{
    auto *panel = new KeybindingsPanel(this);
    const QString commit = tr("Commit view"), changes = tr("Changes list"), history = tr("History"),
                  diff = tr("Diff"), merge = tr("Merge view"), text = tr("Text fields");
    // The application
    panel->add(QStringLiteral("CTRL + K"), tr("Keybindings"));
    panel->add(QStringLiteral("CTRL + 1"), tr("Commit view"), QString(), [this] { setMode(CommitMode); });
    panel->add(QStringLiteral("CTRL + 2"), tr("History view"), QString(), [this] { setMode(HistoryMode); });
    panel->add(QStringLiteral("CTRL + 3"), tr("Branches"), QString(), [this] { showBranchMenu(); });
    panel->add(QStringLiteral("CTRL + R"), tr("Recent repositories"), QString(), [this] { showRepoMenu(); });
    panel->add(QStringLiteral("CTRL + O"), tr("Open repository…"), QString(), [this] { openRepositoryDialog(); });
    panel->add(QStringLiteral("F5 / CTRL SHIFT + R"), tr("Refresh"), QString(), [this] { refresh(); });
    panel->add(QStringLiteral("CTRL + F"), tr("Fetch"), QString(), [this] { m_sync->fetch(); });
    panel->add(QStringLiteral("CTRL + P"), tr("Pull"), QString(), [this] { m_sync->pull(); });
    panel->add(QStringLiteral("CTRL SHIFT + P"), tr("Push"), QString(), [this] { m_sync->push(); });
    panel->add(QStringLiteral("CTRL SHIFT + M"), tr("Merge branches"), QString(), [this] { showMergeDialog(); });
    panel->add(QStringLiteral("CTRL + B"), tr("Docked / Mini layout"), QString(), [this] {
        setPaneLayout(m_layout == PaneLayout::Mini ? PaneLayout::Docked : PaneLayout::Mini);
    });
    panel->add(QStringLiteral("CTRL SHIFT + B"), tr("Show / hide diff pane"), QString(),
               [this] { setDiffPaneVisible(!m_diffVisible); });
    panel->add(QStringLiteral("CTRL + Q"), tr("Quit"), QString(), [this] { close(); });
    // The commit view
    panel->add(QStringLiteral("CTRL + RETURN"), tr("Commit checked files"), commit, [this] {
        if (m_mode == CommitMode)
            m_commitPage->commitButton()->click();
    });
    panel->add(QStringLiteral("CTRL + G"), tr("Generate commit message"), commit, [this] {
        setMode(CommitMode);
        generateMessage();
    });
    panel->add(QStringLiteral("CTRL SHIFT + A"), tr("Amend last commit"), commit, [this] { toggleAmend(); });
    panel->add(QStringLiteral("CTRL + A"), tr("Check all / none"), changes, [this] { toggleAllChecked(); });
    panel->add(QStringLiteral("SPACE"), tr("Check / uncheck file"), tr("Changes list, Mini rail"));
    panel->add(QStringLiteral("CTRL + CLICK"), tr("Check / uncheck file"), tr("Mini rail"));
    panel->add(QStringLiteral("CTRL + E"), tr("Open file in its program"), commit, [this] {
        if (m_mode == CommitMode)
            openInEditor();
    });
    panel->add(QStringLiteral("CTRL + D"), tr("Discard file changes"), commit, [this] { discardCurrent(); });
    // History
    panel->add(QStringLiteral("CTRL + S"), tr("Filter commits"), history, [this] { focusHistoryFilter(); });
    // The diff pane
    panel->add(QStringLiteral("F8"), tr("Next change"), diff, [this] { m_diffPane->nextChange(); });
    panel->add(QStringLiteral("SHIFT + F8"), tr("Previous change"), diff, [this] { m_diffPane->previousChange(); });
    panel->add(QStringLiteral("CTRL + T"), tr("One / two panes"), diff, [this] { m_diffPane->togglePaneMode(); });
    panel->add(QStringLiteral("CTRL + W"), tr("Show whitespace"), diff, [this] { m_diffPane->toggleWhitespace(); });
    panel->add(QStringLiteral("CTRL + L"), tr("Syntax highlighting"), diff, [this] { m_diffPane->toggleSyntax(); });
    panel->add(QStringLiteral("CTRL + PLUS"), tr("Zoom in"), diff, [this] { m_diffPane->zoomBy(1); });
    panel->add(QStringLiteral("CTRL + MINUS"), tr("Zoom out"), diff, [this] { m_diffPane->zoomBy(-1); });
    panel->add(QStringLiteral("CTRL + 0"), tr("Reset zoom"), diff, [this] { m_diffPane->resetZoom(); });
    panel->add(QStringLiteral("CTRL + WHEEL"), tr("Zoom"), diff);
    panel->add(QStringLiteral("CTRL + C"), tr("Copy selection"), diff);
    panel->add(QStringLiteral("CTRL + A"), tr("Select all"), diff);
    panel->add(QStringLiteral("ESCAPE"), tr("Clear selection"), diff);
    panel->add(QStringLiteral("UP / DOWN"), tr("Scroll a line"), diff);
    panel->add(QStringLiteral("LEFT / RIGHT"), tr("Scroll sideways"), diff);
    panel->add(QStringLiteral("PAGE DOWN / SPACE"), tr("Scroll a page down"), diff);
    panel->add(QStringLiteral("PAGE UP"), tr("Scroll a page up"), diff);
    panel->add(QStringLiteral("CTRL + HOME / CTRL + END"), tr("Beginning / end"), diff);
    // The merge view
    panel->add(QStringLiteral("CTRL + S"), tr("Swap the two sides"), merge);
    panel->add(QStringLiteral("RETURN"), tr("Merge"), merge);
    panel->add(QStringLiteral("ESCAPE"), tr("Close"), merge);
    // Lists, menus, text
    panel->add(QStringLiteral("UP / DOWN"), tr("Move between items"), tr("Lists, menus"));
    panel->add(QStringLiteral("RETURN"), tr("Choose item"), tr("Menus"));
    panel->add(QStringLiteral("ESCAPE"), tr("Close menu or panel"), tr("Menus, panels"));
    panel->add(QStringLiteral("TAB / SHIFT + TAB"), tr("Next / previous control"));
    panel->add(QStringLiteral("CTRL + Z / CTRL SHIFT + Z"), tr("Undo / redo"), text);
    panel->add(QStringLiteral("CTRL + C / X / V"), tr("Copy / cut / paste"), text);
    panel->add(QStringLiteral("CTRL + LEFT / RIGHT"), tr("Move by word"), text);
    panel->add(QStringLiteral("CTRL + BACKSPACE / DELETE"), tr("Delete previous / next word"), text);
    panel->add(QStringLiteral("HOME / END"), tr("Line beginning / end"), text);
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
    m_stack->setCurrentIndex(mode == CommitMode ? 0 : 1);
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
        const QModelIndex idx = m_commitPage->table()->currentIndex();
        if (idx.isValid())
            onCurrentRowChanged(idx);
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
        QSettings().setValue(QStringLiteral("window/layout"), paneLayoutKey(layout));
}

void MainWindow::setDiffPaneVisible(bool on, bool persist)
{
    m_diffVisible = on;
    if (!on && m_layout == PaneLayout::Mini)
        setPaneLayout(PaneLayout::Docked, persist);
    applyPanes();
    if (persist)
        QSettings().setValue(QStringLiteral("window/diffPane"), on);
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
    QCheckBox *const amend = m_commitPage->amendBox();
    if (amend->isEnabled())
        amend->setChecked(on);
}

void MainWindow::setAutoFetchEnabled(bool on)
{
    m_sync->setAutoFetchInterval(on ? QSettings().value(QStringLiteral("remote/autoFetchSeconds"), 180).toInt() : 0);
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
        const int total = width() - m.left() - m.right() - (m_rail->isVisibleTo(this) ? MiniRail::kWidth + 8 : 0);
        const int rightMin = m_diffPane->isVisibleTo(this) ? 1 + m_splitter->handleWidth() : 0;
        const int wanted = QSettings().value(QStringLiteral("window/leftWidth"), total * 45 / 100).toInt();
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
    for (QLabel *l : findChildren<QLabel *>()) {
        if (l->objectName() == QLatin1String("sectionLabel") || l->objectName() == QLatin1String("dimLabel"))
            l->setFont(theme->captionFont());
    }
    m_history->applyTheme();
    m_rail->applyTheme();
}

void MainWindow::refresh()
{
    ChangesModel *const model = m_commitPage->model();
    QSortFilterProxyModel *const proxy = m_commitPage->proxy();
    QTableView *const table = m_commitPage->table();
    MessageEdit *const message = m_commitPage->message();
    QCheckBox *const amend = m_commitPage->amendBox();
    DiffView *const diff = m_diffPane->view();

    QString selectedPath;
    bool ok = false;
    const FileChange cur = m_commitPage->currentChange(&ok);
    if (ok)
        selectedPath = cur.path;
    if (!m_initialSelection.isEmpty()) {
        selectedPath = m_initialSelection;
        m_initialSelection.clear();
    }

    const Commit head = m_repo->headCommit();
    const MergeState merge = m_repo->mergeState();
    m_merging = merge.inProgress;
    amend->setEnabled(head.isValid() && !merge.inProgress);
    amend->setToolTip(merge.inProgress ? tr("Not while a merge is in progress")
                      : head.isValid() ? tr("Rewrite the last commit (%1: %2) with the checked files and the message above")
                                             .arg(head.shortHash, head.subject)
                                       : tr("There is no commit to amend yet"));
    QString branch = icon(kBranch) + m_repo->branch() + chevron();
    if (m_repo->amending() && head.isValid())
        branch += tr("   ·   amending %1").arg(head.shortHash);
    if (merge.inProgress)
        branch += tr("   ·   merging %1").arg(merge.source);
    m_footer->branchButton()->setText(branch);
    updateMergeButtons(merge);
    updateCommitButton();
    // Git's own message for the merge commit goes in the box while it is
    // empty (or still holds the previous proposal) and leaves with the merge.
    if (merge.inProgress) {
        const QString text = message->toPlainText();
        if ((text.trimmed().isEmpty() || text == m_mergeMessage) && text != merge.message)
            message->setPlainText(merge.message);
        m_mergeMessage = merge.message;
    } else if (!m_mergeMessage.isEmpty()) {
        if (message->toPlainText() == m_mergeMessage)
            message->clear();
        m_mergeMessage.clear();
    }
    m_sync->refreshState();
    // Re-selecting the row below scrolls the views to it and reloads the
    // diff from its first change; the user may have scrolled either on
    // purpose, so put the scroll offsets (and the current change) back after.
    QScrollBar *const tableBar = table->verticalScrollBar();
    QScrollBar *const tableHBar = table->horizontalScrollBar();
    QScrollBar *const railBar = m_rail->list()->verticalScrollBar();
    const int tableScroll = tableBar->value();
    const int tableHScroll = tableHBar->value();
    const int railScroll = railBar->value();
    const DiffView::ViewState diffState = diff->viewState();
    m_refreshing = true;
    model->setChanges(m_repo->status());
    m_refreshing = false;
    watchChangedFiles();

    // Restore selection
    bool restored = false;
    for (int r = 0; r < proxy->rowCount(); ++r) {
        const int src = proxy->mapToSource(proxy->index(r, 0)).row();
        if (model->change(src).path == selectedPath) {
            if (table->currentIndex().row() != r) // unchanged rows keep their current cell
                table->selectRow(r);
            restored = true;
            break;
        }
    }
    if (!restored) {
        if (proxy->rowCount() > 0)
            table->selectRow(0);
        else if (m_mode == CommitMode)
            diff->clear(tr("Working tree clean — nothing to commit."));
    }
    if (restored) {
        tableBar->setValue(tableScroll);
        tableHBar->setValue(tableHScroll);
        railBar->setValue(railScroll);
        if (ok && cur.path == selectedPath && m_mode == CommitMode) {
            // The row may still be current (no reset happened), so show the
            // diff again by hand: identical content is left alone, changed
            // content is put back where the user was reading.
            showDiffFor(model->change(proxy->mapToSource(table->currentIndex()).row()));
            diff->restoreViewState(diffState);
        }
    }
    m_commitPage->onCheckedChanged();

    m_historyDirty = true;
    if (m_mode == HistoryMode) {
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
            railBar->setValue(railScroll);
            diff->restoreViewState(diffState);
        }
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
    showDiffFor(m_commitPage->model()->change(m_commitPage->proxy()->mapToSource(current).row()));
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

void MainWindow::showDiffFor(const FileChange &change)
{
    bool binary = change.binary;
    const QString unified = m_repo->diff(change, &binary);
    const QString base = m_repo->amending() ? tr("HEAD~1") : tr("HEAD");
    QString leftLabel = base;
    if (change.kind == FileChange::Untracked || (change.kind == FileChange::Added && change.oldPath.isEmpty()))
        leftLabel = tr("(new file)");
    else if (!change.oldPath.isEmpty())
        leftLabel = tr("%1: %2").arg(base, change.oldPath);
    const QString rightLabel = change.kind == FileChange::Deleted ? tr("(deleted)") : tr("Working Tree");
    QString emptyMessage;
    if (!change.oldPath.isEmpty())
        emptyMessage = tr("Renamed from %1 — contents unchanged.").arg(change.oldPath);
    else if (change.isStaged() && change.worktree == ' ')
        emptyMessage = tr("Staged — identical to %1.").arg(base);
    presentDiff(unified, change, binary, leftLabel, rightLabel, emptyMessage);
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
    QString leftLabel = parent.isEmpty() ? tr("(empty tree)") : parent;
    if (f.kind == FileChange::Added && f.oldPath.isEmpty())
        leftLabel = tr("(new file)");
    else if (!f.oldPath.isEmpty())
        leftLabel = tr("%1: %2").arg(parent, f.oldPath);
    const QString rightLabel = f.kind == FileChange::Deleted ? tr("(deleted)") : c.shortHash;
    const QString emptyMessage = f.oldPath.isEmpty() ? tr("No textual changes.")
                                                     : tr("Renamed from %1 — contents unchanged.").arg(f.oldPath);
    presentDiff(unified, f, binary, leftLabel, rightLabel, emptyMessage);
}

// The commit page has ticked or unticked the amend box: the button text, the
// changes list against the commit before, and its files checked.
void MainWindow::onAmendToggled(bool on)
{
    updateCommitButton();
    refresh();
    if (on)
        m_commitPage->checkHeadPaths();
}

void MainWindow::commit()
{
    MessageEdit *const messageEdit = m_commitPage->message();
    ChangesModel *const model = m_commitPage->model();
    QCheckBox *const amendBox = m_commitPage->amendBox();
    const QString message = messageEdit->toPlainText().trimmed();
    if (message.isEmpty()) {
        QMessageBox::warning(this, tr("Commit"), tr("Please enter a commit message."));
        messageEdit->setFocus();
        return;
    }
    const QStringList paths = model->checkedPaths();
    const bool amend = amendBox->isChecked();
    if (amend) {
        const QStringList published = m_repo->remoteBranchesContainingHead();
        if (!published.isEmpty()) {
            const auto answer = QMessageBox::warning(
                this, tr("Amend last commit"),
                tr("The last commit is already part of %1.\n\nAmending it rewrites published history; "
                   "you will have to force-push, and others who have it must rebase.\n\nAmend anyway?")
                    .arg(published.join(QStringLiteral(", "))),
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
            if (answer != QMessageBox::Yes)
                return;
        }
    }
    QString error;
    const bool ok = amend ? m_repo->amendCommit(message, paths, &error) : m_repo->commit(message, paths, &error);
    if (!ok) {
        QMessageBox::critical(this, amend ? tr("Amend failed") : tr("Commit failed"),
                              error.isEmpty() ? tr("git commit failed.") : error);
        return;
    }
    const int count = model->checkedCount();
    const bool merged = m_merging;
    messageEdit->clear();
    if (amend) {
        amendBox->setChecked(false); // also refreshes
        showStatus(tr("Amended the last commit on %1 with %2 file(s)").arg(m_repo->branch()).arg(count), 5000);
    } else if (merged) {
        showStatus(tr("Merge committed on %1").arg(m_repo->branch()), 5000);
        refresh();
    } else {
        showStatus(tr("Committed %1 file(s) to %2").arg(count).arg(m_repo->branch()), 5000);
        refresh();
    }
}

void MainWindow::updateSyncButtons()
{
    const OmarchyTheme *theme = OmarchyTheme::instance();
    const UpstreamState &s = m_sync->state();
    const RemoteSync::Op op = m_sync->runningOp();

    QString fetchTip;
    if (s.remotes.isEmpty()) {
        fetchTip = tr("No remote configured — nothing to fetch from");
    } else {
        fetchTip = tr("Fetch from all remotes (Ctrl+F)");
        if (op == RemoteSync::Fetch)
            fetchTip += tr("\nFetching…");
        else if (m_sync->lastFetch().isValid() && m_sync->lastFetchOk())
            fetchTip += tr("\nLast fetched %1").arg(ago(m_sync->lastFetch()));
        else if (m_sync->lastFetch().isValid())
            fetchTip += tr("\nLast fetch failed %1: %2").arg(ago(m_sync->lastFetch()), m_sync->lastFetchError());
        const int every = m_sync->autoFetchInterval();
        if (every > 0)
            fetchTip += tr("\nFetches by itself every %n minute(s) while the window is open", nullptr, qMax(1, every / 60));
        else
            fetchTip += tr("\nAutomatic fetching is off (remote/autoFetchSeconds in omagit.conf)");
    }

    QString pullTip;
    if (s.detached)
        pullTip = tr("HEAD is detached — check out a branch to pull");
    else if (s.branch.isEmpty())
        pullTip = tr("Nothing to pull into yet");
    else if (s.upstreamGone)
        pullTip = tr("%1 no longer exists on the remote").arg(s.upstream);
    else if (!s.hasUpstream())
        pullTip = tr("%1 has no upstream branch to pull from").arg(s.branch);
    else if (op == RemoteSync::Fetch)
        pullTip = tr("Checking %1 for new commits…").arg(s.upstream);
    else if (op == RemoteSync::Pull)
        pullTip = tr("Pulling from %1…").arg(s.upstream);
    else if (s.behind > 0)
        pullTip = tr("Pull %n commit(s) from %1 into %2 (Ctrl+P)", nullptr, s.behind).arg(s.upstream, s.branch);
    else
        pullTip = tr("Pull from %1 — nothing new since the last fetch (Ctrl+P)").arg(s.upstream);

    QString pushTip;
    if (s.detached)
        pushTip = tr("HEAD is detached — check out a branch to push");
    else if (s.branch.isEmpty())
        pushTip = tr("Nothing to push yet");
    else if (s.remote.isEmpty())
        pushTip = tr("No remote to push to");
    else if (op == RemoteSync::Push)
        pushTip = tr("Pushing to %1…").arg(s.remote);
    else if (m_sync->pushPublishes())
        pushTip = tr("Publish %1 on %2 and track it from now on — git %3 (Ctrl+Shift+P)")
                      .arg(s.branch, s.remote, m_sync->pushArgs().join(QLatin1Char(' ')));
    else if (s.ahead > 0)
        pushTip = tr("Push %n commit(s) from %1 to %2 (Ctrl+Shift+P)", nullptr, s.ahead).arg(s.branch, s.upstream);
    else
        pushTip = tr("Push to %1 — nothing to push (Ctrl+Shift+P)").arg(s.upstream);

    const QColor red = theme->color(QStringLiteral("red"));
    for (const SyncButtons &b : std::as_const(m_syncButtons)) {
        b.fetch->setEnabled(m_sync->canFetch());
        b.fetch->setToolTip(fetchTip);
        b.fetch->setMark(m_sync->lastFetch().isValid() && !m_sync->lastFetchOk() ? QStringLiteral("!") : QString(), red);
        b.pull->setEnabled(m_sync->canPull());
        b.pull->setToolTip(pullTip);
        b.pull->setBusy(op == RemoteSync::Fetch || op == RemoteSync::Pull);
        b.pull->setCount(s.hasUpstream() ? s.behind : 0);
        b.pull->setMark(s.upstreamGone ? QStringLiteral("!") : QString(), red);
        b.push->setEnabled(m_sync->canPush());
        b.push->setToolTip(pushTip);
        b.push->setBusy(op == RemoteSync::Push);
        b.push->setCount(s.hasUpstream() ? s.ahead : 0);
    }

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
    m_footer->branchButton()->setToolTip(upstream + tr("Click or Ctrl+3 to switch to another branch"));
}

void MainWindow::onSyncFinished(RemoteSync::Op op, bool ok, bool automatic, const QString &message)
{
    if (!ok && !automatic) {
        const QString title = op == RemoteSync::Fetch ? tr("Fetch failed")
                            : op == RemoteSync::Pull  ? tr("Pull failed")
                                                      : tr("Push failed");
        QMessageBox::critical(this, title, message);
    }
    showStatus(message.section(QLatin1Char('\n'), 0, 0), ok ? 8000 : 15000);
    if (op != RemoteSync::Fetch || ok)
        refresh();
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
    showStatus(tr("Switched to %1").arg(m_repo->branch()), 5000);
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
                    ChangesModel *const model = m_commitPage->model();
                    QSortFilterProxyModel *const proxy = m_commitPage->proxy();
                    for (int r = 0; r < proxy->rowCount(); ++r) {
                        if (model->change(proxy->mapToSource(proxy->index(r, 0)).row()).kind == FileChange::Unmerged) {
                            m_commitPage->table()->selectRow(r);
                            break;
                        }
                    }
                    const QString count = conflicts == 1 ? tr("1 conflicted file") : tr("%1 conflicted files").arg(conflicts);
                    showStatus(tr("Merging %1 into %2 — %3 to resolve, then Commit merge").arg(source, destination, count), 20000);
                } else if (fastForward) {
                    showStatus(tr("Fast-forwarded %2 to %1").arg(source, destination), 8000);
                } else {
                    showStatus(tr("Merged %1 into %2").arg(source, destination), 8000);
                }
            });
    connect(dialog, &MergeDialog::mergeAborted, this, [this](const QString &source, const QString &destination) {
        refresh();
        showStatus(tr("Aborted the merge of %1 into %2").arg(source, destination), 8000);
    });
    dialog->show();
}

void MainWindow::updateCommitButton()
{
    const bool amend = m_commitPage->amendBox()->isChecked();
    QPushButton *const button = m_commitPage->commitButton();
    button->setText(icon(kCommit) + (amend ? tr("Amend") : m_merging ? tr("Commit merge") : tr("Commit")));
    button->setToolTip(amend ? tr("Rewrite the last commit with the checked files (Ctrl+Enter)")
                       : m_merging ? tr("Finish the merge: commit the checked (resolved) files together with what git merged on its own (Ctrl+Enter)")
                                   : tr("Commit the checked files (Ctrl+Enter)"));
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

namespace {
const auto kRecentKey = QStringLiteral("repos/recent");
constexpr int kRecentMax = 15;
} // namespace

QStringList MainWindow::recentRepositories()
{
    QSettings settings;
    const QStringList stored = settings.value(kRecentKey).toStringList();
    QStringList list;
    for (const QString &root : stored) {
        if (QFileInfo(root).isDir() && !list.contains(root))
            list << root;
    }
    if (list != stored)
        settings.setValue(kRecentKey, list);
    return list;
}

void MainWindow::rememberRepository(const QString &root)
{
    QStringList list = recentRepositories();
    list.removeAll(root);
    list.prepend(root);
    while (list.size() > kRecentMax)
        list.removeLast();
    QSettings().setValue(kRecentKey, list);
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
    const int menuY = -menu.sizeHint().height();
    menu.exec(m_footer->repoButton()->mapToGlobal(QPoint(0, menuY)));
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
    {
        // The amend state belonged to the old repository; onAmendToggled(false)
        // also drops its message from the box and refreshes.
        QSignalBlocker blocker(m_commitPage->amendBox());
        m_commitPage->amendBox()->setChecked(false);
    }
    m_commitPage->onAmendToggled(false);
    watchWorkingTree();
    updateRepoLabels();
    rememberRepository(root);
    showStatus(tr("Opened %1").arg(tildePath(root)), 5000);
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
    ChangesModel *const model = m_commitPage->model();
    QStringList wanted;
    if (QFile::exists(m_indexFile))
        wanted << m_indexFile;
    const QDir root(m_repo->root());
    for (int i = 0; i < model->rowCount() && wanted.size() <= kMaxWatchedFiles; ++i) {
        const QString path = root.filePath(model->change(i).path);
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
