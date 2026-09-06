#include "MainWindow.h"
#include "DiffModel.h"
#include "DiffView.h"
#include "HistoryView.h"
#include "OmarchyTheme.h"

#include <QAction>
#include <QButtonGroup>
#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileSystemWatcher>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMessageBox>
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
    buildUi();
    applyTheme();
    connect(OmarchyTheme::instance(), &OmarchyTheme::changed, this, &MainWindow::applyTheme);

    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    if (!settings.contains(QStringLiteral("window/geometry")))
        resize(1400, 850);
    setLeftFull(settings.value(QStringLiteral("window/leftFull"), false).toBool());

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
               kBranch = 0xF062C, kSplit = 0xF0BCC, kPilcrow = 0xF06D8, kHistory = 0xF02DA,
               kExpand = 0xF084E, kCollapse = 0xF084C;

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

QToolButton *toolButton(const QString &text, const QString &tip = QString())
{
    auto *b = new QToolButton;
    b->setText(text);
    b->setToolButtonStyle(Qt::ToolButtonTextOnly);
    b->setToolTip(tip);
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::NoFocus);
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

    // ---- Left section: mode switch + (commit dialog | history)
    auto *left = new QWidget;
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(8);

    auto *headerRow = new QHBoxLayout;
    headerRow->setSpacing(8);
    m_commitModeButton = toolButton(icon(kCommit) + tr("Commit"), tr("Pending changes and commit dialog (Ctrl+1)"));
    m_historyModeButton = toolButton(icon(kHistory) + tr("History"), tr("Commit history of the repository (Ctrl+2)"));
    auto *modes = new QButtonGroup(this);
    modes->setExclusive(true);
    for (QToolButton *b : {m_commitModeButton, m_historyModeButton}) {
        b->setCheckable(true);
        modes->addButton(b);
        headerRow->addWidget(b);
    }
    m_commitModeButton->setChecked(true);
    m_commitModeButton->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
    m_historyModeButton->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_2));
    connect(m_commitModeButton, &QToolButton::clicked, this, [this] { setMode(CommitMode); });
    connect(m_historyModeButton, &QToolButton::clicked, this, [this] { setMode(HistoryMode); });
    headerRow->addStretch();
    m_layoutButton = toolButton(QString());
    m_layoutButton->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
    connect(m_layoutButton, &QToolButton::clicked, this, [this] { setLeftFull(m_rightPane->isVisible()); });
    headerRow->addWidget(m_layoutButton);
    auto *refreshButton = toolButton(icon(kRefresh) + tr("Refresh"), tr("Re-read the repository (F5)"));
    refreshButton->setShortcut(QKeySequence::Refresh);
    connect(refreshButton, &QToolButton::clicked, this, &MainWindow::refresh);
    headerRow->addWidget(refreshButton);
    leftLayout->addLayout(headerRow);

    m_stack = new QStackedWidget;
    m_stack->addWidget(buildCommitPage());
    m_history = new HistoryView(m_repo);
    connect(m_history, &HistoryView::currentFileChanged, this, &MainWindow::showHistoryDiff);
    m_stack->addWidget(m_history);
    leftLayout->addWidget(m_stack, 1);
    new QShortcut(QKeySequence::Find, this, this, [this] {
        if (m_mode == HistoryMode)
            m_history->focusFilter();
    });

    // ---- Right pane: diff view with navigation toolbar
    m_rightPane = new QWidget;
    auto *rightLayout = new QVBoxLayout(m_rightPane);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(8);

    auto *navRow = new QHBoxLayout;
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
    splitter->setHandleWidth(8);
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(left);
    splitter->addWidget(m_rightPane);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 3);
    splitter->setSizes({620, 780});
    rootLayout->addWidget(splitter, 1);

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
    layout->addLayout(changesRow);

    m_model = new ChangesModel(this);
    auto *proxy = new UnversionedFilter(this);
    proxy->setSourceModel(m_model);
    proxy->setSortRole(Qt::DisplayRole);
    m_proxy = proxy;

    m_table = new QTableView;
    m_table->setModel(m_proxy);
    m_tableSetup = new ChangesTableSetup(m_table);
    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &MainWindow::onCurrentRowChanged);
    connect(m_table, &QTableView::doubleClicked, this, &MainWindow::openInEditor);
    connect(m_model, &ChangesModel::checkedChanged, this, &MainWindow::onCheckedChanged);
    layout->addWidget(m_table, 1);

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

    auto *buttonRow = new QHBoxLayout;
    buttonRow->setSpacing(10);
    buttonRow->addWidget(dimLabel(tr("double-click a file to open it")));
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

void MainWindow::setLeftFull(bool full, bool persist)
{
    m_rightPane->setVisible(!full);
    m_layoutButton->setText(full ? icon(kCollapse) + tr("Sidebar") : icon(kExpand) + tr("Full"));
    m_layoutButton->setToolTip(full ? tr("Show the diff pane again; the left section becomes a sidebar (Ctrl+B)")
                                    : tr("Let the left section fill the window and hide the diff pane (Ctrl+B)"));
    if (persist)
        QSettings().setValue(QStringLiteral("window/leftFull"), full);
}

void MainWindow::setAmend(bool on)
{
    if (m_amend->isEnabled())
        m_amend->setChecked(on);
}

void MainWindow::applyTheme()
{
    const OmarchyTheme *theme = OmarchyTheme::instance();
    m_diff->refreshTheme();
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

void MainWindow::openInEditor()
{
    bool ok = false;
    const FileChange c = currentChange(&ok);
    if (!ok || c.kind == FileChange::Deleted)
        return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(QDir(m_repo->root()).filePath(c.path)));
}
