#include "MainWindow.h"
#include "BadgeButton.h"
#include "BranchMenu.h"
#include "DesktopExec.h"
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

#include <QAction>
#include <QButtonGroup>
#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QDialog>
#include <QHeaderView>
#include <QFileInfo>
#include <QEvent>
#include <QFileDialog>
#include <QFileSystemWatcher>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeySequence>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeDatabase>
#include <QProcess>
#include <QStandardPaths>
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
#include <QUrl>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <algorithm>

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
QStringList spinnerFrames(const QFont &font);
} // namespace

MainWindow::MainWindow(GitRepo *repo, QWidget *parent)
    : QMainWindow(parent), m_repo(repo)
{
    setWindowIcon(QIcon(QStringLiteral(":/omagit.svg")));
    m_sync = new RemoteSync(repo, this);
    m_agent = new CommitMessageAgent(this);
    connect(m_agent, &CommitMessageAgent::partial, this, [this](const QString &text) {
        m_message->replaceText(text, m_streaming);
        m_streaming = true;
    });
    connect(m_agent, &CommitMessageAgent::finished, this, &MainWindow::onMessageGenerated);
    // The CLIs are asked for their models and levels ahead of the cog menu.
    for (const AgentSpec &agent : CommitMessageAgent::installedAgents())
        CommitMessageAgent::probeAsync(agent.id, this);
    m_spinner = new QTimer(this);
    m_spinner->setInterval(80);
    connect(m_spinner, &QTimer::timeout, this, [this] {
        const QStringList frames = spinnerFrames(m_message->font());
        m_spinnerFrame = (m_spinnerFrame + 1) % frames.size();
        m_message->cornerButton()->setText(frames.at(m_spinnerFrame));
    });
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

namespace {
// Nerd Font (Material Design) glyphs used by the shell; empty if the font lacks them.
QString icon(uint cp, const QString &fallback = QString())
{
    const QString g = OmarchyTheme::instance()->glyph(cp);
    return g.isEmpty() ? fallback : g + QStringLiteral("  ");
}
constexpr uint kRefresh = 0xF0450, kArrowUp = 0xF005D, kArrowDown = 0xF0045, kCommit = 0xF0718,
               kBranch = 0xF062C, kSplit = 0xF0BCC, kPilcrow = 0xF06D8, kHistory = 0xF02DA;
// md-cloud_download, md-tray_arrow_down, md-tray_arrow_up, md-dock_right, md-source_merge
constexpr uint kFetch = 0xF0162, kPull = 0xF0120, kPush = 0xF011D, kDockRight = 0xF10AB, kMerge = 0xF062D;
// md-chevron_down, md-folder, md-folder_open
constexpr uint kChevron = 0xF0140, kFolder = 0xF024B, kFolderOpen = 0xF0770, kMagnify = 0xF0349;
// md-creation (the sparkle of "generate"), md-cog, md-robot
constexpr uint kSparkle = 0xF0674, kCog = 0xF0493, kRobot = 0xF06A9;

// The frames of the generate button while an agent thinks: a braille spinner
// when the font has one, a turning circle otherwise.
QStringList spinnerFrames(const QFont &font)
{
    const QFontMetrics fm(font);
    if (fm.inFont(QChar(0x280B)))
        return {QStringLiteral("⠋"), QStringLiteral("⠙"), QStringLiteral("⠹"), QStringLiteral("⠸"), QStringLiteral("⠼"),
                QStringLiteral("⠴"), QStringLiteral("⠦"), QStringLiteral("⠧"), QStringLiteral("⠇"), QStringLiteral("⠏")};
    if (fm.inFont(QChar(0x25D0)))
        return {QStringLiteral("◐"), QStringLiteral("◓"), QStringLiteral("◑"), QStringLiteral("◒")};
    return {QStringLiteral("|"), QStringLiteral("/"), QStringLiteral("-"), QStringLiteral("\\")};
}

// The "this opens a list" mark at the end of a dropdown button's text.
QString chevron()
{
    const QString g = OmarchyTheme::instance()->glyph(kChevron);
    return QStringLiteral("  ") + (g.isEmpty() ? QStringLiteral("▾") : g);
}

// "/home/me/Projects/x" → "~/Projects/x"
QString tildePath(const QString &path)
{
    const QString home = QDir::homePath();
    if (path == home)
        return QStringLiteral("~");
    if (path.startsWith(home + QLatin1Char('/')))
        return QStringLiteral("~") + path.mid(home.size());
    return path;
}

// Resolve the default application's label and launch information.
struct DefaultApp {
    QString name, icon;
    QString exec, desktopFile, workingDirectory;
    bool terminal = false;
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
            app.desktopFile = file;
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
                else if (line.startsWith(QLatin1String("Exec=")))
                    app.exec = line.mid(5);
                else if (line.startsWith(QLatin1String("Path=")))
                    app.workingDirectory = line.mid(5);
                else if (line == QLatin1String("Terminal=true"))
                    app.terminal = true;
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

// A borderless button that reads like a label and drops a menu down on click.
QToolButton *dropdownButton(const QString &objectName)
{
    auto *b = toolButton(QString());
    b->setObjectName(objectName);
    b->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    return b;
}

QWidget *hairline()
{
    auto *w = new QWidget;
    w->setFixedHeight(1);
    w->setAutoFillBackground(true);
    return w;
}

// A dim caption inside a menu, like the section labels of the dialog.
QAction *addMenuHeader(QMenu *menu, const QString &text)
{
    auto *action = new QWidgetAction(menu);
    QLabel *label = sectionLabel(text);
    label->setContentsMargins(14, 6, 14, 3);
    action->setDefaultWidget(label);
    menu->addAction(action);
    return action;
}
} // namespace

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
    m_layoutButton = dropdownButton(QStringLiteral("layoutButton"));
    m_layoutButton->setCheckable(true); // checked = Mini; the glyph shows the current layout
    connect(m_layoutButton, &QToolButton::clicked, this, [this](bool mini) {
        setPaneLayout(mini ? PaneLayout::Mini : PaneLayout::Docked);
    });

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
    // Ctrl+W whitespace, q quit.
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

    // Repository and branch selectors stay available in both modes.
    m_repoButton = dropdownButton(QStringLiteral("repoButton"));
    connect(m_repoButton, &QToolButton::clicked, this, &MainWindow::showRepoMenu);
    new QShortcut(QKeySequence::Open, this, this, &MainWindow::openRepositoryDialog);
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_R), this, this, &MainWindow::showRepoMenu);
    m_branchButton = dropdownButton(QStringLiteral("branchButton"));
    m_branchButton->setFont(OmarchyTheme::instance()->uiFont());
    connect(m_branchButton, &QToolButton::clicked, this, &MainWindow::showBranchMenu);
    // Ctrl+1 and Ctrl+2 are the views; the branches are the third "panel".
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_3), this, this, &MainWindow::showBranchMenu);

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
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_S), this, this, &MainWindow::focusHistoryFilter);

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
    QToolButton *paneButton = m_paneButton = toolButton(icon(kSplit) + tr("Two-pane"),
                                  tr("Toggle between two-pane (side by side) and one-pane view (Ctrl+T)"));
    paneButton->setCheckable(true);
    navRow->addWidget(paneButton);
    QToolButton *wsButton = m_wsButton = toolButton(icon(kPilcrow) + tr("Whitespace"), tr("Show whitespace and line endings (Ctrl+W)"));
    wsButton->setCheckable(true);
    navRow->addWidget(wsButton);
    // On the window, not the buttons: they may be hidden with the diff pane.
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_T), this, paneButton, &QToolButton::toggle);
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_W), this, wsButton, &QToolButton::toggle);
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
    for (const Qt::Key key : {Qt::Key_Plus, Qt::Key_Equal})
        new QShortcut(QKeySequence(Qt::CTRL | key), this, m_diff, [this] { m_diff->zoomBy(1); });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus), this, m_diff, [this] { m_diff->zoomBy(-1); });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_0), this, m_diff, &DiffView::resetZoom);

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
    m_footerLine = hairline();
    rootLayout->addWidget(m_footerLine);
    auto *footer = new QHBoxLayout;
    footer->setSpacing(8);
    footer->addWidget(m_layoutButton);
    footer->addWidget(m_repoButton);
    footer->addWidget(m_branchButton);
    m_statusLabel = dimLabel();
    m_statusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_statusLabel->setMinimumWidth(0);
    m_statusTimer = new QTimer(this);
    m_statusTimer->setSingleShot(true);
    connect(m_statusTimer, &QTimer::timeout, this, [this] { m_statusLabel->setText(tildePath(m_repo->root())); });
    footer->addWidget(m_statusLabel, 1);
    auto *infoButton = smallButton(0xF02FC, tr("i"), tr("Keybindings (Ctrl+K)"));
    infoButton->setObjectName(QStringLiteral("keybindingsButton"));
    infoButton->setAccessibleName(tr("Keybindings"));
    connect(infoButton, &QToolButton::clicked, this, &MainWindow::showKeybindings);
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_K), this, this, &MainWindow::showKeybindings);
    footer->addWidget(infoButton);
    rootLayout->addLayout(footer);

    // The commit view's file actions. Ctrl+A checks the files rather than
    // selecting rows, so it sits on the two lists, where a text field never
    // sees it.
    for (QWidget *list : {static_cast<QWidget *>(m_table), static_cast<QWidget *>(m_rail->list())})
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

// The commit dialog: message, changes list, options, buttons.
QWidget *MainWindow::buildCommitPage()
{
    const OmarchyTheme *theme = OmarchyTheme::instance();
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    // MESSAGE, with the agent settings at the far right; the message box has
    // the generate button in its top right corner.
    auto *messageRow = new QHBoxLayout;
    messageRow->addWidget(sectionLabel(tr("Message")));
    messageRow->addStretch();
    m_agentButton = smallButton(kCog, tr("⚙"), tr("Which coding agent writes the commit message, with which model and reasoning level"));
    connect(m_agentButton, &QToolButton::clicked, this, &MainWindow::showAgentMenu);
    messageRow->addWidget(m_agentButton);
    layout->addLayout(messageRow);

    m_message = new MessageEdit;
    m_message->setPlaceholderText(tr("Commit message"));
    m_message->setMinimumHeight(theme->fontBase() * 3);

    auto *messageSplitter = new QSplitter(Qt::Vertical);
    messageSplitter->setObjectName(QStringLiteral("commitMessageSplitter"));
    messageSplitter->setHandleWidth(8);
    messageSplitter->setChildrenCollapsible(false);
    messageSplitter->addWidget(m_message);
    auto *changes = new QWidget;
    auto *changesLayout = new QVBoxLayout(changes);
    changesLayout->setContentsMargins(0, 0, 0, 0);
    changesLayout->setSpacing(8);
    messageSplitter->addWidget(changes);
    messageSplitter->setStretchFactor(0, 0);
    messageSplitter->setStretchFactor(1, 1); // the changes list takes window resizes
    connect(messageSplitter, &QSplitter::splitterMoved, this, [messageSplitter] {
        QSettings().setValue(QStringLiteral("window/commitMessageSplitter"), messageSplitter->saveState());
    });
    layout->addWidget(messageSplitter, 1);
    QToolButton *generate = m_message->cornerButton();
    generate->setText(icon(kSparkle, QStringLiteral("✨")).trimmed());
    connect(generate, &QToolButton::clicked, this, &MainWindow::generateMessage);
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_G), this, this, &MainWindow::generateMessage);
    setGenerating(false);

    auto *changesRow = new QHBoxLayout;
    changesRow->addWidget(sectionLabel(tr("Changes")));
    changesRow->addStretch();
    m_summaryLabel = dimLabel();
    changesRow->addWidget(m_summaryLabel);
    changesRow->addSpacing(4);
    auto *refreshButton = smallButton(kRefresh, tr("R"), tr("Re-read the repository (F5)"));
    connect(refreshButton, &QToolButton::clicked, this, &MainWindow::refresh);
    changesRow->addWidget(refreshButton);
    changesLayout->addLayout(changesRow);

    m_model = new ChangesModel(this);
    auto *proxy = new UnversionedFilter(this);
    proxy->setSourceModel(m_model);
    proxy->setSortRole(ChangesModel::SortRole);
    proxy->setSortCaseSensitivity(Qt::CaseInsensitive);
    m_proxy = proxy;

    m_table = new QTableView;
    m_table->setObjectName(QStringLiteral("changesTable"));
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
    // File actions apply to the clicked row, independent of checked files.
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
        menu.addSeparator();
        QAction *discard = menu.addAction(tr("Discard changes"));
        discard->setToolTip(c.isUntracked() ? tr("Delete this untracked file")
                                          : tr("Restore this file to the latest commit, including staged changes"));
        connect(discard, &QAction::triggered, this, [this, change = c] { discardChange(change); });
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
    changesLayout->addLayout(optionsRow);
    changesLayout->addWidget(m_table, 1);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->setSpacing(10);
    buttonRow->addStretch();
    m_commitButton = new QPushButton(icon(kCommit) + tr("Commit"));
    m_commitButton->setDefault(true);
    m_commitButton->setCursor(Qt::PointingHandCursor);
    m_commitButton->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return));
    m_commitButton->setToolTip(tr("Commit the checked files (Ctrl+Enter)"));
    connect(m_commitButton, &QPushButton::clicked, this, &MainWindow::commit);
    buttonRow->addWidget(m_commitButton);
    layout->addLayout(buttonRow);
    messageSplitter->setSizes({theme->fontBase() * 7, changes->sizeHint().height()});
    messageSplitter->restoreState(QSettings().value(QStringLiteral("window/commitMessageSplitter")).toByteArray());
    return page;
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
            m_commitButton->click();
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
    panel->add(QStringLiteral("F8"), tr("Next change"), diff, [this] { m_diff->nextChange(); });
    panel->add(QStringLiteral("SHIFT + F8"), tr("Previous change"), diff, [this] { m_diff->previousChange(); });
    panel->add(QStringLiteral("CTRL + T"), tr("One / two panes"), diff, [this] { m_paneButton->toggle(); });
    panel->add(QStringLiteral("CTRL + W"), tr("Show whitespace"), diff, [this] { m_wsButton->toggle(); });
    panel->add(QStringLiteral("CTRL + PLUS"), tr("Zoom in"), diff, [this] { m_diff->zoomBy(1); });
    panel->add(QStringLiteral("CTRL + MINUS"), tr("Zoom out"), diff, [this] { m_diff->zoomBy(-1); });
    panel->add(QStringLiteral("CTRL + 0"), tr("Reset zoom"), diff, [this] { m_diff->resetZoom(); });
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
    const bool on = m_selectAll->checkState() != Qt::Checked;
    m_selectAll->setTristate(false);
    m_model->setAllChecked(on);
}

void MainWindow::toggleAmend()
{
    if (m_mode == CommitMode && m_amend->isEnabled())
        m_amend->click();
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
    m_message->setMinimumHeight(theme->fontBase() * 3);
    m_message->applyTheme();
    m_branchButton->setFont(theme->uiFont());
    m_repoButton->setFont(theme->uiFont());
    for (QLabel *l : findChildren<QLabel *>()) {
        if (l->objectName() == QLatin1String("sectionLabel") || l->objectName() == QLatin1String("dimLabel"))
            l->setFont(theme->captionFont());
    }
    {
        QPalette pal = m_footerLine->palette();
        pal.setColor(QPalette::Window, theme->border());
        m_footerLine->setPalette(pal);
    }
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
    const MergeState merge = m_repo->mergeState();
    m_merging = merge.inProgress;
    m_amend->setEnabled(head.isValid() && !merge.inProgress);
    m_amend->setToolTip(merge.inProgress ? tr("Not while a merge is in progress")
                        : head.isValid() ? tr("Rewrite the last commit (%1: %2) with the checked files and the message above")
                                               .arg(head.shortHash, head.subject)
                                         : tr("There is no commit to amend yet"));
    QString branch = icon(kBranch) + m_repo->branch() + chevron();
    if (m_repo->amending() && head.isValid())
        branch += tr("   ·   amending %1").arg(head.shortHash);
    if (merge.inProgress)
        branch += tr("   ·   merging %1").arg(merge.source);
    m_branchButton->setText(branch);
    updateMergeButtons(merge);
    updateCommitButton();
    // Git's own message for the merge commit goes in the box while it is
    // empty (or still holds the previous proposal) and leaves with the merge.
    if (merge.inProgress) {
        const QString text = m_message->toPlainText();
        if ((text.trimmed().isEmpty() || text == m_mergeMessage) && text != merge.message)
            m_message->setPlainText(merge.message);
        m_mergeMessage = merge.message;
    } else if (!m_mergeMessage.isEmpty()) {
        if (m_message->toPlainText() == m_mergeMessage)
            m_message->clear();
        m_mergeMessage.clear();
    }
    m_sync->refreshState();
    // Re-selecting the row below scrolls the views to it and reloads the
    // diff from its first change; the user may have scrolled either on
    // purpose, so put the scroll offsets (and the current change) back after.
    QScrollBar *const tableBar = m_table->verticalScrollBar();
    QScrollBar *const tableHBar = m_table->horizontalScrollBar();
    QScrollBar *const railBar = m_rail->list()->verticalScrollBar();
    const int tableScroll = tableBar->value();
    const int tableHScroll = tableHBar->value();
    const int railScroll = railBar->value();
    const DiffView::ViewState diffState = m_diff->viewState();
    m_refreshing = true;
    m_model->setChanges(m_repo->status());
    m_refreshing = false;
    watchChangedFiles();

    // Restore selection
    bool restored = false;
    for (int r = 0; r < m_proxy->rowCount(); ++r) {
        const int src = m_proxy->mapToSource(m_proxy->index(r, 0)).row();
        if (m_model->change(src).path == selectedPath) {
            if (m_table->currentIndex().row() != r) // unchanged rows keep their current cell
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
    if (restored) {
        tableBar->setValue(tableScroll);
        tableHBar->setValue(tableHScroll);
        railBar->setValue(railScroll);
        if (ok && cur.path == selectedPath && m_mode == CommitMode) {
            // The row may still be current (no reset happened), so show the
            // diff again by hand: identical content is left alone, changed
            // content is put back where the user was reading.
            showDiffFor(m_model->change(m_proxy->mapToSource(m_table->currentIndex()).row()));
            m_diff->restoreViewState(diffState);
        }
    }
    onCheckedChanged();

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
            m_diff->restoreViewState(diffState);
        }
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

void MainWindow::discardCurrent()
{
    bool ok = false;
    const FileChange c = currentChange(&ok);
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
            m_diff->clear();
        return;
    }
    showDiffFor(m_model->change(m_proxy->mapToSource(current).row()));
}

void MainWindow::presentDiff(const QString &unified, const FileChange &change, bool binary, const QString &leftLabel,
                             const QString &rightLabel, const QString &emptyMessage)
{
    const QString key = QStringList{change.path, change.statusText(), leftLabel, rightLabel, emptyMessage,
                                    binary ? QStringLiteral("1") : QStringLiteral("0"), unified}
                            .join(QChar(0));
    if (key == m_shownDiffKey && !m_diff->document().lines.isEmpty())
        return; // the same document is on screen: keep the selection and the scroll position
    m_shownDiffKey = key;
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
    updateCommitButton();
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
    const bool merged = m_merging;
    m_message->clear();
    if (amend) {
        m_amend->setChecked(false); // also refreshes
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
    m_branchButton->setToolTip(upstream + tr("Click or Ctrl+3 to switch to another branch"));
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
    const FileChange c = currentChange(&ok);
    if (!ok || c.kind == FileChange::Deleted)
        return;
    const QString path = QDir(m_repo->root()).filePath(c.path);
    const DefaultApp app = defaultAppFor(path);
    if (!app.exec.isEmpty()) {
        QStringList args = desktopExecArguments(app.exec, path, app.name, app.icon, app.desktopFile);
        if (args.isEmpty()) {
            QMessageBox::critical(this, tr("Open failed"),
                                  tr("The application command for %1 is empty.").arg(app.name));
            return;
        }
        QString program = args.takeFirst();
        if (app.terminal) {
            if (QStandardPaths::findExecutable(QStringLiteral("xdg-terminal-exec")).isEmpty()) {
                QMessageBox::critical(this, tr("Open failed"),
                                      tr("Opening %1 requires xdg-terminal-exec.").arg(app.name));
                return;
            }
            args.prepend(program);
            args.prepend(QStringLiteral("--"));
            program = QStringLiteral("xdg-terminal-exec");
        }
        // Keep the editor alive when OmaGit closes.
        QProcess process;
        process.setWorkingDirectory(app.workingDirectory.isEmpty() ? m_repo->root() : app.workingDirectory);
        process.setProgram(program);
        process.setArguments(args);
        if (!process.startDetached())
            QMessageBox::critical(this, tr("Open failed"), process.errorString());
    } else if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
        QMessageBox::critical(this, tr("Open failed"), tr("Could not open %1 with its default application.").arg(c.path));
    }
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
    menu.popupAt(m_branchButton, true);
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
                    for (int r = 0; r < m_proxy->rowCount(); ++r) {
                        if (m_model->change(m_proxy->mapToSource(m_proxy->index(r, 0)).row()).kind == FileChange::Unmerged) {
                            m_table->selectRow(r);
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
    const bool amend = m_amend->isChecked();
    m_commitButton->setText(icon(kCommit) + (amend ? tr("Amend") : m_merging ? tr("Commit merge") : tr("Commit")));
    m_commitButton->setToolTip(amend ? tr("Rewrite the last commit with the checked files (Ctrl+Enter)")
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
    menu.exec(m_repoButton->mapToGlobal(QPoint(0, menuY)));
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
        QSignalBlocker blocker(m_amend);
        m_amend->setChecked(false);
    }
    onAmendToggled(false);
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
    QStringList wanted;
    if (QFile::exists(m_indexFile))
        wanted << m_indexFile;
    const QDir root(m_repo->root());
    for (int i = 0; i < m_model->rowCount() && wanted.size() <= kMaxWatchedFiles; ++i) {
        const QString path = root.filePath(m_model->change(i).path);
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
    setWindowTitle(QStringLiteral("OmaGit — %1").arg(name));
    m_repoButton->setText(icon(kFolder) + name + chevron());
    m_repoButton->setToolTip(tr("%1\nClick or Ctrl+R for the repositories opened lately, Ctrl+O to open another one")
                                 .arg(m_repo->root()));
    m_statusTimer->stop();
    m_statusLabel->setText(tildePath(m_repo->root()));
}

// ---- Commit message from a coding agent -----------------------------------

void MainWindow::setGenerating(bool on)
{
    QToolButton *b = m_message->cornerButton();
    const AgentChoice choice = CommitMessageAgent::savedChoice();
    const AgentSpec agent = CommitMessageAgent::spec(choice.agent);
    if (on) {
        m_spinnerFrame = 0;
        b->setText(spinnerFrames(m_message->font()).first());
        b->setToolTip(tr("%1 is writing the message… click to stop").arg(agent.name));
        m_spinner->start();
    } else {
        m_spinner->stop();
        b->setText(icon(kSparkle, QStringLiteral("✨")).trimmed());
        if (agent.isValid()) {
            const QString model = choice.model.isEmpty() ? tr("default model") : choice.model;
            b->setToolTip(tr("Let %1 (%2) write a commit message for the checked changes (Ctrl+G)").arg(agent.name, model));
        } else {
            b->setToolTip(tr("Write a commit message with a coding agent — none is installed (Ctrl+G)"));
        }
    }
}

void MainWindow::generateMessage()
{
    if (m_agent->running()) {
        m_agent->cancel();
        setGenerating(false);
        showStatus(tr("Stopped"), 3000);
        return;
    }
    if (m_mode != CommitMode)
        setMode(CommitMode);
    const AgentChoice choice = CommitMessageAgent::savedChoice();
    if (choice.agent.isEmpty()) {
        showStatus(tr("Neither claude nor codex is installed — `omarchy default agent claude` sets one up"), 8000);
        return;
    }
    // The checked files are what the message is for; with nothing checked,
    // everything in the list (as an editor describes all changes when
    // nothing is staged).
    QList<FileChange> changes = m_model->checkedChanges();
    if (changes.isEmpty()) {
        for (int i = 0; i < m_model->count(); ++i) {
            const FileChange &c = m_model->change(i);
            if (m_showUnversioned->isChecked() || !c.isUntracked())
                changes << c;
        }
    }
    if (changes.isEmpty()) {
        showStatus(tr("No changes to describe"), 4000);
        return;
    }
    QString diff = tr("Branch: %1\n").arg(m_repo->branch());
    if (m_amend->isChecked() && !m_headMessage.isEmpty())
        diff += tr("The message of the commit being amended (rewrite it to cover the whole change):\n%1\n").arg(m_headMessage);
    diff += QLatin1Char('\n') + m_repo->patch(changes);
    m_streaming = false;
    m_messageBefore = m_message->toPlainText();
    setGenerating(true);
    showStatus(tr("Asking %1 for a commit message…").arg(CommitMessageAgent::spec(choice.agent).name));
    m_agent->generate(choice, m_repo->root(), diff);
}

void MainWindow::onMessageGenerated(bool ok, const QString &text)
{
    setGenerating(false);
    if (!ok) {
        // Whatever the agent streamed before failing is not a message.
        if (m_streaming)
            m_message->replaceText(m_messageBefore, true);
        showStatus(tr("No commit message: %1").arg(text), 12000);
        return;
    }
    m_message->replaceText(text, m_streaming);
    m_message->setFocus();
    showStatus(tr("Commit message written by %1").arg(CommitMessageAgent::spec(CommitMessageAgent::savedChoice().agent).name),
               4000);
}

// The cog's menu: AGENT (Claude Code and Codex, whichever is installed),
// MODEL and REASONING as the chosen agent's CLI names them (`claude --help`,
// `codex debug models`; Codex has levels per model), or any model by name.
void MainWindow::showAgentMenu()
{
    TickMenu menu(this);
    menu.setToolTipsVisible(true);
    AgentChoice choice = CommitMessageAgent::savedChoice();
    const auto save = [this](const AgentChoice &c) {
        CommitMessageAgent::saveChoice(c);
        setGenerating(m_agent->running());
    };

    addMenuHeader(&menu, tr("Agent"));
    const QList<AgentSpec> installed = CommitMessageAgent::installedAgents();
    if (installed.isEmpty()) {
        QAction *none = menu.addAction(tr("None installed"));
        none->setEnabled(false);
        none->setToolTip(tr("`omarchy default agent claude` (or codex) installs one"));
    }
    const QString omarchyDefault = CommitMessageAgent::omarchyDefaultAgent();
    for (const AgentSpec &agent : installed) {
        QAction *a = menu.addAction(icon(kRobot) + agent.name);
        a->setCheckable(true);
        a->setChecked(agent.id == choice.agent);
        a->setToolTip(agent.id == omarchyDefault ? tr("%1 — Omarchy's default agent").arg(agent.binary) : agent.binary);
        connect(a, &QAction::triggered, this, [save, agent] {
            // A model and a level belong to the agent they were picked for.
            save(AgentChoice{agent.id, QString(), QString()});
        });
    }

    const AgentSpec current = CommitMessageAgent::spec(choice.agent);
    if (current.isValid()) {
        const AgentCatalog catalog = CommitMessageAgent::catalog(choice.agent);
        menu.addSeparator();
        addMenuHeader(&menu, tr("Model"));
        QAction *def = menu.addAction(tr("Default"));
        def->setCheckable(true);
        def->setChecked(choice.model.isEmpty());
        def->setToolTip(tr("Whatever %1 is set to use").arg(current.name));
        connect(def, &QAction::triggered, this, [save, choice] { save(AgentChoice{choice.agent, QString(), choice.effort}); });
        QList<AgentModel> models = catalog.models;
        const bool known = std::any_of(models.cbegin(), models.cend(), [&](const AgentModel &m) { return m.id == choice.model; });
        if (!choice.model.isEmpty() && !known)
            models.prepend(AgentModel{choice.model, choice.model, {}, {}});
        for (const AgentModel &m : std::as_const(models)) {
            QAction *a = menu.addAction(m.name);
            a->setCheckable(true);
            a->setChecked(m.id == choice.model);
            a->setToolTip(m.id);
            connect(a, &QAction::triggered, this, [save, choice, m] {
                // A level the new model does not have goes back to its default.
                const QString effort = m.efforts.isEmpty() || m.efforts.contains(choice.effort) ? choice.effort : QString();
                save(AgentChoice{choice.agent, m.id, effort});
            });
        }
        if (models.isEmpty() && !catalog.error.isEmpty()) {
            QAction *err = menu.addAction(tr("Could not read the models"));
            err->setEnabled(false);
            err->setToolTip(catalog.error);
        }
        QAction *other = menu.addAction(tr("Other…"));
        other->setToolTip(tr("A model by name, as %1 --model takes it").arg(current.binary));
        connect(other, &QAction::triggered, this, [this, save, choice, current] {
            bool ok = false;
            const QString id = QInputDialog::getText(this, tr("Model"), tr("Model name for %1:").arg(current.name),
                                                     QLineEdit::Normal, choice.model, &ok)
                                   .trimmed();
            if (ok)
                save(AgentChoice{choice.agent, id, choice.effort});
        });

        const QStringList efforts = catalog.effortsFor(choice.model);
        if (!efforts.isEmpty()) {
            menu.addSeparator();
            addMenuHeader(&menu, tr("Reasoning"));
            QAction *defEffort = menu.addAction(tr("Default"));
            defEffort->setCheckable(true);
            defEffort->setChecked(choice.effort.isEmpty());
            connect(defEffort, &QAction::triggered, this,
                    [save, choice] { save(AgentChoice{choice.agent, choice.model, QString()}); });
            for (const QString &level : efforts) {
                QAction *a = menu.addAction(level.at(0).toUpper() + level.mid(1));
                a->setCheckable(true);
                a->setChecked(level == choice.effort);
                connect(a, &QAction::triggered, this,
                        [save, choice, level] { save(AgentChoice{choice.agent, choice.model, level}); });
            }
        }
    }
    // The cog sits at the right edge, so the menu hangs from its right corner.
    menu.exec(m_agentButton->mapToGlobal(QPoint(m_agentButton->width() - menu.sizeHint().width(), m_agentButton->height())));
}

void MainWindow::showStatus(const QString &text, int ms)
{
    m_statusLabel->setText(text);
    if (ms > 0)
        m_statusTimer->start(ms);
    else
        m_statusTimer->stop();
}
