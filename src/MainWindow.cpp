#include "MainWindow.h"
#include "BadgeButton.h"
#include "DiffModel.h"
#include "DiffView.h"
#include "HistoryView.h"
#include "MiniRail.h"
#include "OmarchyTheme.h"
#include "Toolbar.h"

#include <QAction>
#include <QButtonGroup>
#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFileSystemWatcher>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMimeDatabase>
#include <QProcess>
#include <QStandardPaths>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTableView>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {
class UnversionedFilter : public QSortFilterProxyModel
{
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    bool showUnversioned = true;

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override
    {
        if (showUnversioned)
            return true;
        auto *m = static_cast<ChangesModel *>(sourceModel());
        Q_UNUSED(parent)
        return !m->change(row).isUntracked();
    }
};
} // namespace

MainWindow::MainWindow(GitRepo *repo, QWidget *parent)
    : QMainWindow(parent), m_repo(repo)
{
    setWindowTitle(QStringLiteral("OmaGit — %1").arg(QDir(repo->root()).dirName()));
    setWindowIcon(QIcon(QStringLiteral(":/omagit.svg")));
    m_sync = new RemoteSync(repo, this);
    buildUi();
    applyTheme();
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

namespace {
// Nerd Font (Material Design) glyphs used by the shell; empty if the font lacks them.
QString icon(uint cp, const QString &fallback = QString())
{
    const QString g = OmarchyTheme::instance()->glyph(cp);
    return g.isEmpty() ? fallback : g + QStringLiteral("  ");
}
constexpr uint kRefresh = 0xF0450, kArrowUp = 0xF005D, kArrowDown = 0xF0045, kCommit = 0xF0718,
               kBranch = 0xF062C, kSplit = 0xF0BCC, kPilcrow = 0xF06D8, kHistory = 0xF02DA;
// md-cloud_download, md-tray_arrow_down, md-tray_arrow_up, md-dock_right
constexpr uint kFetch = 0xF0162, kPull = 0xF0120, kPush = 0xF011D, kDockRight = 0xF10AB;

// The program the desktop opens a file with (what QDesktopServices::openUrl
// will use): `xdg-mime query default <mime>` names a desktop file, whose
// Name and Icon are read here. Empty when nothing is registered.
struct DefaultApp {
    QString name, icon;
};

DefaultApp defaultAppFor(const QString &filePath)
{
    static QHash<QString, DefaultApp> cache; // by MIME type; the query runs a shell script
    const QString mime = QMimeDatabase().mimeTypeForFile(filePath).name();
    const auto cached = cache.constFind(mime);
    if (cached != cache.constEnd())
        return *cached;
    DefaultApp app;
    QProcess query;
    query.start(QStringLiteral("xdg-mime"), {QStringLiteral("query"), QStringLiteral("default"), mime});
    if (query.waitForFinished(1500) && query.exitCode() == 0) {
        const QString desktopId = QString::fromUtf8(query.readAllStandardOutput()).trimmed();
        const QString file = desktopId.isEmpty() ? QString()
                                                 : QStandardPaths::locate(QStandardPaths::ApplicationsLocation, desktopId);
        QFile f(file);
        if (!file.isEmpty() && f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            bool inEntry = false;
            while (!f.atEnd()) {
                const QString line = QString::fromUtf8(f.readLine()).trimmed();
                if (line.startsWith(QLatin1Char('['))) {
                    inEntry = line == QLatin1String("[Desktop Entry]");
                    continue;
                }
                if (!inEntry)
                    continue;
                if (line.startsWith(QLatin1String("Name=")))
                    app.name = line.mid(5);
                else if (line.startsWith(QLatin1String("Icon=")))
                    app.icon = line.mid(5);
            }
        }
        if (app.name.isEmpty() && !desktopId.isEmpty()) // no desktop file found: show the id
            app.name = desktopId.endsWith(QLatin1String(".desktop")) ? desktopId.chopped(8) : desktopId;
    }
    cache.insert(mime, app);
    return app;
}

QString ago(const QDateTime &when)
{
    const qint64 secs = when.secsTo(QDateTime::currentDateTime());
    if (secs < 60)
        return QCoreApplication::translate("MainWindow", "just now");
    if (secs < 3600)
        return QCoreApplication::translate("MainWindow", "%n minute(s) ago", nullptr, int(secs / 60));
    return QCoreApplication::translate("MainWindow", "%n hour(s) ago", nullptr, int(secs / 3600));
}

QLabel *sectionLabel(const QString &text)
{
    auto *l = new QLabel(text.toUpper());
    l->setObjectName(QStringLiteral("sectionLabel"));
    l->setFont(OmarchyTheme::instance()->captionFont());
    return l;
}

QLabel *dimLabel(const QString &text = QString())
{
    auto *l = new QLabel(text);
    l->setObjectName(QStringLiteral("dimLabel"));
    l->setFont(OmarchyTheme::instance()->captionFont());
    return l;
}

template <typename Button = QToolButton>
Button *toolButton(const QString &text, const QString &tip = QString())
{
    auto *b = new Button;
    b->setText(text);
    b->setToolButtonStyle(Qt::ToolButtonTextOnly);
    b->setToolTip(tip);
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
}

// Icon-only button with less padding, for a row of labels.
QToolButton *smallButton(uint glyph, const QString &fallback, const QString &tip)
{
    auto *b = toolButton(icon(glyph, fallback).trimmed(), tip);
    b->setObjectName(QStringLiteral("smallButton"));
    return b;
}
} // namespace

void MainWindow::buildUi()
{
    const OmarchyTheme *theme = OmarchyTheme::instance();
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

    // The toolbar: Docked/Mini toggle | Commit, History | Pull, Push, Fetch.
    // Labels give way to icons, then to a "more" menu, as the pane narrows.
    m_toolbar = new Toolbar;
    m_layoutButton = toolButton(QString());
    m_layoutButton->setCheckable(true); // checked = Mini; the glyph shows the current layout
    connect(m_layoutButton, &QToolButton::clicked, this, [this](bool mini) {
        setPaneLayout(mini ? PaneLayout::Mini : PaneLayout::Docked);
    });
    m_toolbar->setLeading(m_layoutButton);

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
    new QShortcut(QKeySequence::Refresh, this, this, &MainWindow::refresh);
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
    new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F), this, m_sync, &RemoteSync::fetch);
    new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L), this, m_sync, &RemoteSync::pull);
    new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P), this, m_sync, &RemoteSync::push);
    connect(m_sync, &RemoteSync::stateChanged, this, &MainWindow::updateSyncButtons);
    connect(m_sync, &RemoteSync::finished, this, &MainWindow::onSyncFinished);
    m_toolbarRow = new QHBoxLayout;
    m_toolbarRow->setSpacing(8);
    m_toolbarRow->addWidget(m_toolbar, 1);
    leftLayout->addLayout(m_toolbarRow);

    m_stack = new QStackedWidget;
    m_stack->addWidget(buildCommitPage());
    m_history = new HistoryView(m_repo);
    connect(m_history, &HistoryView::currentFileChanged, this, &MainWindow::showHistoryDiff);
    connect(m_history, &HistoryView::refreshRequested, this, &MainWindow::refresh);
    connect(m_history->filesTable(), &QTableView::doubleClicked, this, [this] {
        if (!m_diffVisible)
            setDiffPaneVisible(true);
    });
    m_stack->addWidget(m_history);
    leftLayout->addWidget(m_stack, 1);
    new QShortcut(QKeySequence::Find, this, this, [this] {
        if (m_mode == HistoryMode)
            m_history->focusFilter();
    });

    // ---- Right pane: diff view with navigation toolbar
    m_rightPane = new QWidget;
    // Like the left section, the diff pane may be dragged as narrow as the
    // user likes; its buttons just get cut off at the edge.
    m_rightPane->setMinimumWidth(1);
    auto *rightLayout = new QVBoxLayout(m_rightPane);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(8);

    auto *navRow = new QHBoxLayout;
    m_navRow = navRow;
    navRow->setSpacing(8);
    m_prevButton = toolButton(icon(kArrowUp) + tr("Prev"), tr("Previous change (Shift+F8)"));
    m_nextButton = toolButton(icon(kArrowDown) + tr("Next"), tr("Next change (F8)"));
    m_changeLabel = dimLabel();
    // Let the label shrink instead of forcing the splitter to widen the diff pane.
    m_changeLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_changeLabel->setMinimumWidth(0);
    navRow->addWidget(m_prevButton);
    navRow->addWidget(m_nextButton);
    navRow->addSpacing(4);
    navRow->addWidget(m_changeLabel, 1);
    auto *paneButton = toolButton(icon(kSplit) + tr("Two-pane"),
                                  tr("Toggle between two-pane (side by side) and one-pane view (Ctrl+T)"));
    paneButton->setCheckable(true);
    paneButton->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
    navRow->addWidget(paneButton);
    auto *wsButton = toolButton(icon(kPilcrow) + tr("Whitespace"), tr("Show whitespace and line endings"));
    wsButton->setCheckable(true);
    navRow->addWidget(wsButton);
    // The diff toggle ends this row; while the pane is hidden it moves to the
    // end of the toolbar row, which is the same top-right spot (see applyPanes).
    m_diffToggle = toolButton(icon(kDockRight, tr("D")).trimmed(), tr("Show or hide the diff pane (Ctrl+Shift+B)"));
    m_diffToggle->setCheckable(true);
    connect(m_diffToggle, &QToolButton::clicked, this, [this](bool on) { setDiffPaneVisible(on); });
    navRow->addWidget(m_diffToggle);
    rightLayout->addLayout(navRow);

    m_diff = new DiffView;
    m_diff->setFrameShape(QFrame::NoFrame);
    rightLayout->addWidget(m_diff, 1);
    connect(m_prevButton, &QToolButton::clicked, m_diff, &DiffView::previousChange);
    connect(m_nextButton, &QToolButton::clicked, m_diff, &DiffView::nextChange);
    connect(wsButton, &QToolButton::toggled, m_diff, &DiffView::setShowWhitespace);
    {
        QSettings settings;
        const bool twoPane = settings.value(QStringLiteral("diff/twoPane"), true).toBool();
        m_diff->setMode(twoPane ? DiffView::TwoPane : DiffView::OnePane);
        paneButton->setChecked(twoPane);
    }
    connect(paneButton, &QToolButton::toggled, m_diff, &DiffView::setTwoPane);
    connect(m_diff, &DiffView::modeChanged, this, [paneButton](DiffView::Mode mode) {
        QSignalBlocker blocker(paneButton);
        paneButton->setChecked(mode == DiffView::TwoPane);
        QSettings().setValue(QStringLiteral("diff/twoPane"), mode == DiffView::TwoPane);
    });
    connect(m_diff, &DiffView::changeIndexChanged, this, [this](int index, int total) {
        m_prevButton->setEnabled(total > 0 && index > 0);
        m_nextButton->setEnabled(total > 0 && index < total - 1);
        QString text = total == 0 ? QString() : tr("Change %1 of %2").arg(index < 0 ? 0 : index + 1).arg(total);
        if (!m_diffSummary.isEmpty())
            text += (text.isEmpty() ? QString() : QStringLiteral("   ·   ")) + m_diffSummary;
        m_changeLabel->setText(text);
    });
    new QShortcut(QKeySequence(Qt::Key_F8), this, m_diff, &DiffView::nextChange);
    new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F8), this, m_diff, &DiffView::previousChange);

    auto *splitter = new QSplitter(Qt::Horizontal);
    m_splitter = splitter;
    splitter->setHandleWidth(8);
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(left);
    splitter->addWidget(m_rightPane);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1); // the diff pane takes window resizes
    // The left width is chosen on first show (see showEvent) and remembered.
    connect(splitter, &QSplitter::splitterMoved, this, [this] {
        if (m_shown && m_left->isVisible())
            QSettings().setValue(QStringLiteral("window/leftWidth"), m_splitter->sizes().first());
    });

    // ---- Mini rail: replaces the left section in the Mini layout
    m_rail = new MiniRail;
    m_rail->setSource(m_proxy, m_table->selectionModel());
    connect(m_rail, &MiniRail::commitModeRequested, this, [this] { setMode(CommitMode); });
    connect(m_rail, &MiniRail::historyModeRequested, this, [this] { setMode(HistoryMode); });
    connect(m_rail, &MiniRail::dockRequested, this, [this] { setPaneLayout(PaneLayout::Docked); });
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

    auto *body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(8);
    body->addWidget(m_rail);
    body->addWidget(splitter, 1);
    rootLayout->addLayout(body, 1);

    setCentralWidget(central);
    statusBar()->setSizeGripEnabled(false);
    statusBar()->setFont(theme->captionFont());
    statusBar()->showMessage(m_repo->root());

    // Refresh the list when the working tree changes (coarse: repo root + .git index).
    auto *watcher = new QFileSystemWatcher(this);
    watcher->addPath(m_repo->root());
    const QString indexFile = m_repo->root() + QStringLiteral("/.git/index");
    if (QFile::exists(indexFile))
        watcher->addPath(indexFile);
    auto *debounce = new QTimer(this);
    debounce->setSingleShot(true);
    debounce->setInterval(500);
    connect(debounce, &QTimer::timeout, this, &MainWindow::refresh);
    connect(watcher, &QFileSystemWatcher::directoryChanged, debounce, qOverload<>(&QTimer::start));
    connect(watcher, &QFileSystemWatcher::fileChanged, this, [watcher, debounce, indexFile] {
        if (!watcher->files().contains(indexFile) && QFile::exists(indexFile))
            watcher->addPath(indexFile);
        debounce->start();
    });
    // ... and when refs move (a fetch, pull or push, also one made in a terminal).
    connect(m_sync, &RemoteSync::repositoryChanged, debounce, qOverload<>(&QTimer::start));
}

// The commit dialog: branch, message, changes list, options, buttons.
QWidget *MainWindow::buildCommitPage()
{
    const OmarchyTheme *theme = OmarchyTheme::instance();
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    auto *branchRow = new QHBoxLayout;
    branchRow->setSpacing(8);
    branchRow->addWidget(sectionLabel(tr("Commit to")));
    m_branchLabel = new QLabel;
    m_branchLabel->setObjectName(QStringLiteral("branchLabel"));
    m_branchLabel->setFont(theme->titleFont());
    branchRow->addWidget(m_branchLabel);
    branchRow->addStretch();
    layout->addLayout(branchRow);

    layout->addWidget(sectionLabel(tr("Message")));
    m_message = new QPlainTextEdit;
    m_message->setPlaceholderText(tr("Commit message"));
    m_message->setFixedHeight(theme->fontBase() * 7);
    layout->addWidget(m_message);

    auto *changesRow = new QHBoxLayout;
    changesRow->addWidget(sectionLabel(tr("Changes")));
    changesRow->addStretch();
    m_summaryLabel = dimLabel();
    changesRow->addWidget(m_summaryLabel);
    changesRow->addSpacing(4);
    auto *refreshButton = smallButton(kRefresh, tr("R"), tr("Re-read the repository (F5)"));
    connect(refreshButton, &QToolButton::clicked, this, &MainWindow::refresh);
    changesRow->addWidget(refreshButton);
    layout->addLayout(changesRow);

    m_model = new ChangesModel(this);
    auto *proxy = new UnversionedFilter(this);
    proxy->setSourceModel(m_model);
    proxy->setSortRole(ChangesModel::SortRole);
    m_proxy = proxy;

    m_table = new QTableView;
    m_table->setModel(m_proxy);
    m_tableSetup = new ChangesTableSetup(m_table);
    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &MainWindow::onCurrentRowChanged);
    // Double-click: with the diff pane hidden, show it for the file (which the
    // click already made current); otherwise open the file in its own program.
    connect(m_table, &QTableView::doubleClicked, this, [this] {
        if (m_diffVisible)
            openInEditor();
        else
            setDiffPaneVisible(true);
    });
    connect(m_model, &ChangesModel::checkedChanged, this, &MainWindow::onCheckedChanged);
    // Right-click: "Open with <the default program>" (deleted files have nothing to open).
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QTableView::customContextMenuRequested, this, [this](const QPoint &pos) {
        const QModelIndex index = m_table->indexAt(pos);
        if (!index.isValid())
            return;
        m_table->setCurrentIndex(index);
        const FileChange &c = m_model->change(m_proxy->mapToSource(index).row());
        const QString path = QDir(m_repo->root()).filePath(c.path);
        const DefaultApp app = c.kind == FileChange::Deleted ? DefaultApp() : defaultAppFor(path);
        QMenu menu(m_table);
        QAction *open = menu.addAction(app.name.isEmpty() ? tr("Open") : tr("Open with %1").arg(app.name));
        if (!app.icon.isEmpty())
            open->setIcon(QIcon::fromTheme(app.icon));
        open->setEnabled(c.kind != FileChange::Deleted);
        open->setToolTip(c.kind == FileChange::Deleted ? tr("The file no longer exists") : path);
        connect(open, &QAction::triggered, this, &MainWindow::openInEditor);
        menu.setToolTipsVisible(true);
        menu.exec(m_table->viewport()->mapToGlobal(pos));
    });

    // Options above the list (above keeps them
    // next to the "n / m selected" count they act on).
    auto *optionsRow = new QHBoxLayout;
    optionsRow->setSpacing(16);
    m_showUnversioned = new QCheckBox(tr("Show unversioned files"));
    m_showUnversioned->setChecked(true);
    connect(m_showUnversioned, &QCheckBox::toggled, this, [this, proxy](bool on) {
        proxy->showUnversioned = on;
        proxy->invalidate();
        onCheckedChanged();
    });
    m_selectAll = new QCheckBox(tr("Select all"));
    m_selectAll->setTristate(true);
    connect(m_selectAll, &QCheckBox::clicked, this, [this](bool on) {
        m_selectAll->setTristate(false);
        m_model->setAllChecked(on);
    });
    m_amend = new QCheckBox(tr("Amend last commit"));
    connect(m_amend, &QCheckBox::toggled, this, &MainWindow::onAmendToggled);
    optionsRow->addWidget(m_selectAll);
    optionsRow->addWidget(m_showUnversioned);
    optionsRow->addWidget(m_amend);
    optionsRow->addStretch();
    layout->addLayout(optionsRow);
    layout->addWidget(m_table, 1);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->setSpacing(10);
    buttonRow->addStretch();
    m_commitButton = new QPushButton(icon(kCommit) + tr("Commit"));
    m_commitButton->setDefault(true);
    m_commitButton->setCursor(Qt::PointingHandCursor);
    m_commitButton->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return));
    m_commitButton->setToolTip(tr("Commit the checked files (Ctrl+Enter)"));
    connect(m_commitButton, &QPushButton::clicked, this, &MainWindow::commit);
    auto *closeButton = new QPushButton(tr("Close"));
    closeButton->setCursor(Qt::PointingHandCursor);
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);
    buttonRow->addWidget(m_commitButton);
    buttonRow->addWidget(closeButton);
    layout->addLayout(buttonRow);
    return page;
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
        m_rail->setSource(m_proxy, m_table->selectionModel());
    else
        m_rail->setSource(m_history->filesTable()->model(), m_history->filesTable()->selectionModel());
    if (mode == HistoryMode) {
        if (m_historyDirty) {
            m_history->reload();
            m_historyDirty = false;
        }
        showHistoryDiff();
    } else {
        const QModelIndex idx = m_table->currentIndex();
        if (idx.isValid())
            onCurrentRowChanged(idx);
        else
            m_diff->clear(tr("Working tree clean — nothing to commit."));
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
    m_rightPane->setVisible(m_diffVisible);
    // The toggle keeps its top-right spot: the end of the diff pane's nav row
    // while the pane shows, the end of the toolbar row while it is hidden.
    QHBoxLayout *home = m_diffVisible ? m_navRow : m_toolbarRow;
    if (home->indexOf(m_diffToggle) < 0) {
        (m_diffVisible ? m_toolbarRow : m_navRow)->removeWidget(m_diffToggle);
        home->addWidget(m_diffToggle);
        m_diffToggle->show();
    }
    {
        QSignalBlocker a(m_layoutButton), b(m_diffToggle);
        m_layoutButton->setChecked(mini);
        m_diffToggle->setChecked(m_diffVisible);
    }
    m_layoutButton->setText(icon(paneLayoutGlyph(m_layout), paneLayoutName(m_layout).left(1)).trimmed());
    m_layoutButton->setToolTip(tr("Layout: %1").arg(paneLayoutTip(m_layout)));
    m_diffToggle->setToolTip(m_diffVisible ? tr("Hide the diff pane so the left section fills the window (Ctrl+Shift+B)")
                                           : tr("Show the diff pane (Ctrl+Shift+B)"));
}

void MainWindow::setAmend(bool on)
{
    if (m_amend->isEnabled())
        m_amend->setChecked(on);
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
        const int rightMin = m_rightPane->isVisibleTo(this) ? 1 + m_splitter->handleWidth() : 0;
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
    m_diff->refreshTheme();
    m_toolbar->applyTheme();
    m_message->setFont(theme->uiFont());
    m_message->setFixedHeight(theme->fontBase() * 7);
    m_branchLabel->setFont(theme->titleFont());
    for (QLabel *l : findChildren<QLabel *>()) {
        if (l->objectName() == QLatin1String("sectionLabel") || l->objectName() == QLatin1String("dimLabel"))
            l->setFont(theme->captionFont());
    }
    statusBar()->setFont(theme->captionFont());
    m_tableSetup->applyTheme();
    m_history->applyTheme();
    m_rail->applyTheme();
}

void MainWindow::refresh()
{
    QString selectedPath;
    bool ok = false;
    const FileChange cur = currentChange(&ok);
    if (ok)
        selectedPath = cur.path;
    if (!m_initialSelection.isEmpty()) {
        selectedPath = m_initialSelection;
        m_initialSelection.clear();
    }

    const Commit head = m_repo->headCommit();
    m_amend->setEnabled(head.isValid());
    m_amend->setToolTip(head.isValid() ? tr("Rewrite the last commit (%1: %2) with the checked files and the message above")
                                             .arg(head.shortHash, head.subject)
                                       : tr("There is no commit to amend yet"));
    QString branch = icon(kBranch) + m_repo->branch();
    if (m_repo->amending() && head.isValid())
        branch += tr("   ·   amending %1").arg(head.shortHash);
    m_branchLabel->setText(branch);
    m_sync->refreshState();
    m_model->setChanges(m_repo->status());

    // Restore selection
    bool restored = false;
    for (int r = 0; r < m_proxy->rowCount(); ++r) {
        const int src = m_proxy->mapToSource(m_proxy->index(r, 0)).row();
        if (m_model->change(src).path == selectedPath) {
            m_table->selectRow(r);
            restored = true;
            break;
        }
    }
    if (!restored) {
        if (m_proxy->rowCount() > 0)
            m_table->selectRow(0);
        else if (m_mode == CommitMode)
            m_diff->clear(tr("Working tree clean — nothing to commit."));
    }
    onCheckedChanged();

    m_historyDirty = true;
    if (m_mode == HistoryMode) {
        m_history->reload();
        m_historyDirty = false;
    }
}

FileChange MainWindow::currentChange(bool *ok) const
{
    const QModelIndex idx = m_table->currentIndex();
    if (!idx.isValid()) {
        *ok = false;
        return FileChange();
    }
    *ok = true;
    return m_model->change(m_proxy->mapToSource(idx).row());
}

void MainWindow::onCurrentRowChanged(const QModelIndex &current)
{
    if (m_mode != CommitMode)
        return;
    if (!current.isValid()) {
        m_diff->clear();
        return;
    }
    showDiffFor(m_model->change(m_proxy->mapToSource(current).row()));
}

void MainWindow::presentDiff(const QString &unified, const FileChange &change, bool binary, const QString &leftLabel,
                             const QString &rightLabel, const QString &emptyMessage)
{
    DiffDocument doc = DiffModel::parse(unified);
    if (binary && doc.lines.isEmpty()) {
        doc.binary = true;
        doc.message = tr("Binary file — no textual diff available.");
    }
    QString subtitle = change.statusText();
    m_diffSummary = change.statusText();
    if (!doc.lines.isEmpty()) {
        subtitle += tr("   +%1  −%2").arg(doc.added).arg(doc.removed);
        m_diffSummary += tr("  +%1 −%2").arg(doc.added).arg(doc.removed);
    } else if (doc.message.isEmpty()) {
        doc.message = emptyMessage;
    }
    m_diff->setDocument(doc, change.path, subtitle, leftLabel, rightLabel);
    if (!doc.blockStarts.isEmpty())
        m_diff->firstChange();
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
        m_diffSummary.clear();
        m_diff->clear(m_history->emptyMessage());
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

void MainWindow::onCheckedChanged()
{
    const int checked = m_model->checkedCount();
    const int total = m_model->count();
    m_summaryLabel->setText(tr("%1 / %2 selected").arg(checked).arg(total));
    m_commitButton->setEnabled(checked > 0);
    QSignalBlocker blocker(m_selectAll);
    if (checked == 0)
        m_selectAll->setCheckState(Qt::Unchecked);
    else if (checked == total)
        m_selectAll->setCheckState(Qt::Checked);
    else
        m_selectAll->setCheckState(Qt::PartiallyChecked);
}

// Amend: the message box gets the last commit's message
// and the changes list is compared against the commit before it, so the files
// of the last commit show up (checked) next to the new changes.
void MainWindow::onAmendToggled(bool on)
{
    m_repo->setAmend(on);
    if (on) {
        m_headMessage = m_repo->headMessage();
        if (m_message->toPlainText().trimmed().isEmpty())
            m_message->setPlainText(m_headMessage);
    } else if (m_message->toPlainText() == m_headMessage) {
        m_message->clear();
    }
    m_commitButton->setText(icon(kCommit) + (on ? tr("Amend") : tr("Commit")));
    m_commitButton->setToolTip(on ? tr("Rewrite the last commit with the checked files (Ctrl+Enter)")
                                  : tr("Commit the checked files (Ctrl+Enter)"));
    refresh();
    if (on)
        m_model->setPathsChecked(m_repo->headPaths(), true);
}

void MainWindow::commit()
{
    const QString message = m_message->toPlainText().trimmed();
    if (message.isEmpty()) {
        QMessageBox::warning(this, tr("Commit"), tr("Please enter a commit message."));
        m_message->setFocus();
        return;
    }
    const QStringList paths = m_model->checkedPaths();
    const bool amend = m_amend->isChecked();
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
    const int count = m_model->checkedCount();
    m_message->clear();
    if (amend) {
        m_amend->setChecked(false); // also refreshes
        statusBar()->showMessage(tr("Amended the last commit on %1 with %2 file(s)").arg(m_repo->branch()).arg(count), 5000);
    } else {
        statusBar()->showMessage(tr("Committed %1 file(s) to %2").arg(count).arg(m_repo->branch()), 5000);
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
        fetchTip = tr("Fetch from all remotes (Ctrl+Shift+F)");
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
        pullTip = tr("Pull %n commit(s) from %1 into %2 (Ctrl+Shift+L)", nullptr, s.behind).arg(s.upstream, s.branch);
    else
        pullTip = tr("Pull from %1 — nothing new since the last fetch (Ctrl+Shift+L)").arg(s.upstream);

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
    m_branchLabel->setToolTip(upstream);
}

void MainWindow::onSyncFinished(RemoteSync::Op op, bool ok, bool automatic, const QString &message)
{
    if (!ok && !automatic) {
        const QString title = op == RemoteSync::Fetch ? tr("Fetch failed")
                            : op == RemoteSync::Pull  ? tr("Pull failed")
                                                      : tr("Push failed");
        QMessageBox::critical(this, title, message);
    }
    statusBar()->showMessage(message.section(QLatin1Char('\n'), 0, 0), ok ? 8000 : 15000);
    if (op != RemoteSync::Fetch || ok)
        refresh();
}

void MainWindow::openInEditor()
{
    bool ok = false;
    const FileChange c = currentChange(&ok);
    if (!ok || c.kind == FileChange::Deleted)
        return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(QDir(m_repo->root()).filePath(c.path)));
}
