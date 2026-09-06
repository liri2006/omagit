#include "MainWindow.h"
#include "DiffModel.h"
#include "DiffView.h"
#include "OmarchyTheme.h"

#include <QAction>
#include <QCheckBox>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFileSystemWatcher>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSortFilterProxyModel>
#include <QSplitter>
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
    setWindowTitle(QStringLiteral("Omagit — %1").arg(QDir(repo->root()).dirName()));
    setWindowIcon(QIcon(QStringLiteral(":/omagit.svg")));
    buildUi();
    applyTheme();
    connect(OmarchyTheme::instance(), &OmarchyTheme::changed, this, &MainWindow::applyTheme);

    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    if (!settings.contains(QStringLiteral("window/geometry")))
        resize(1400, 850);

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
               kBranch = 0xF062C, kSplit = 0xF0C51, kPilcrow = 0xF09EE;

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

    // ---- Left pane: commit message + changes list (commit dialog)
    auto *left = new QWidget;
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(8);

    auto *branchRow = new QHBoxLayout;
    branchRow->setSpacing(8);
    branchRow->addWidget(sectionLabel(tr("Commit to")));
    m_branchLabel = new QLabel;
    m_branchLabel->setObjectName(QStringLiteral("branchLabel"));
    m_branchLabel->setFont(theme->titleFont());
    branchRow->addWidget(m_branchLabel);
    branchRow->addStretch();
    auto *refreshButton = toolButton(icon(kRefresh) + tr("Refresh"), tr("Re-read the working tree (F5)"));
    refreshButton->setShortcut(QKeySequence::Refresh);
    connect(refreshButton, &QToolButton::clicked, this, &MainWindow::refresh);
    branchRow->addWidget(refreshButton);
    leftLayout->addLayout(branchRow);

    leftLayout->addWidget(sectionLabel(tr("Message")));
    m_message = new QPlainTextEdit;
    m_message->setPlaceholderText(tr("Commit message"));
    m_message->setFixedHeight(theme->fontBase() * 7);
    leftLayout->addWidget(m_message);

    auto *changesRow = new QHBoxLayout;
    changesRow->addWidget(sectionLabel(tr("Changes")));
    changesRow->addStretch();
    m_summaryLabel = dimLabel();
    changesRow->addWidget(m_summaryLabel);
    leftLayout->addLayout(changesRow);

    m_model = new ChangesModel(this);
    auto *proxy = new UnversionedFilter(this);
    proxy->setSourceModel(m_model);
    proxy->setSortRole(Qt::DisplayRole);
    m_proxy = proxy;

    m_table = new QTableView;
    m_table->setModel(m_proxy);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(false);
    m_table->setSortingEnabled(true);
    m_table->setShowGrid(false);
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(qRound(theme->fontBase() * 2.33));
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setMinimumSectionSize(40);
    m_table->horizontalHeader()->setHighlightSections(false);
    m_table->setWordWrap(false);
    m_table->setColumnWidth(ChangesModel::Extension, 64);
    m_table->setColumnWidth(ChangesModel::Status, 104);
    m_table->setColumnWidth(ChangesModel::LinesAdded, 76);
    m_table->setColumnWidth(ChangesModel::LinesRemoved, 92);
    m_table->setTextElideMode(Qt::ElideMiddle);
    // Path takes whatever is left, but never less than 240px (then the view scrolls).
    m_table->horizontalHeader()->installEventFilter(this);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->sortByColumn(ChangesModel::Path, Qt::AscendingOrder);
    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &MainWindow::onCurrentRowChanged);
    connect(m_table, &QTableView::doubleClicked, this, &MainWindow::openInEditor);
    connect(m_model, &ChangesModel::checkedChanged, this, &MainWindow::onCheckedChanged);
    leftLayout->addWidget(m_table, 1);

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
    optionsRow->addWidget(m_selectAll);
    optionsRow->addWidget(m_showUnversioned);
    optionsRow->addStretch();
    leftLayout->addLayout(optionsRow);

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
    leftLayout->addLayout(buttonRow);

    // ---- Right pane: diff view with navigation toolbar
    auto *right = new QWidget;
    auto *rightLayout = new QVBoxLayout(right);
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
    splitter->addWidget(right);
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

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_table->horizontalHeader() && event->type() == QEvent::Resize) {
        int others = 0;
        for (int c = ChangesModel::Extension; c < ChangesModel::ColumnCount; ++c)
            others += m_table->columnWidth(c);
        m_table->setColumnWidth(ChangesModel::Path, qMax(240, m_table->viewport()->width() - others));
    }
    return QMainWindow::eventFilter(watched, event);
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
    m_table->verticalHeader()->setDefaultSectionSize(qRound(theme->fontBase() * 2.33));
    m_table->viewport()->update();
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

    m_branchLabel->setText(icon(kBranch) + m_repo->branch());
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
        else
            m_diff->clear(tr("Working tree clean — nothing to commit."));
    }
    onCheckedChanged();
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
    if (!current.isValid()) {
        m_diff->clear();
        return;
    }
    showDiffFor(m_model->change(m_proxy->mapToSource(current).row()));
}

void MainWindow::showDiffFor(const FileChange &change)
{
    bool binary = change.binary;
    const QString unified = m_repo->diff(change, &binary);
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
    }
    else if (doc.message.isEmpty() && !change.oldPath.isEmpty())
        doc.message = tr("Renamed from %1 — contents unchanged.").arg(change.oldPath);
    else if (doc.message.isEmpty() && change.isStaged() && change.worktree == ' ')
        doc.message = tr("Staged — identical to HEAD.");
    QString leftLabel = tr("HEAD");
    if (change.kind == FileChange::Untracked || (change.kind == FileChange::Added && change.oldPath.isEmpty()))
        leftLabel = tr("(new file)");
    else if (!change.oldPath.isEmpty())
        leftLabel = tr("HEAD: %1").arg(change.oldPath);
    const QString rightLabel = change.kind == FileChange::Deleted ? tr("(deleted)") : tr("Working Tree");
    m_diff->setDocument(doc, change.path, subtitle, leftLabel, rightLabel);
    if (!doc.blockStarts.isEmpty())
        m_diff->firstChange();
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

void MainWindow::commit()
{
    const QString message = m_message->toPlainText().trimmed();
    if (message.isEmpty()) {
        QMessageBox::warning(this, tr("Commit"), tr("Please enter a commit message."));
        m_message->setFocus();
        return;
    }
    const QStringList paths = m_model->checkedPaths();
    QString error;
    if (!m_repo->commit(message, paths, &error)) {
        QMessageBox::critical(this, tr("Commit failed"), error.isEmpty() ? tr("git commit failed.") : error);
        return;
    }
    statusBar()->showMessage(tr("Committed %1 file(s) to %2").arg(m_model->checkedCount()).arg(m_repo->branch()), 5000);
    m_message->clear();
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
