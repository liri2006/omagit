#include "HistoryView.h"
#include "ChangesModel.h"
#include "HistoryModel.h"
#include "OmarchyTheme.h"

#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

constexpr uint kMagnify = 0xF0349, kBranch = 0xF062C, kMore = 0xF0140;

QString icon(uint cp)
{
    const QString g = OmarchyTheme::instance()->glyph(cp);
    return g.isEmpty() ? QString() : g + QStringLiteral("  ");
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

// Matches the filter text against subject, body, author and hash.
class CommitFilter : public QSortFilterProxyModel
{
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    QString text;

protected:
    bool filterAcceptsRow(int row, const QModelIndex &) const override
    {
        if (text.isEmpty())
            return true;
        const Commit &c = static_cast<HistoryModel *>(sourceModel())->commit(row);
        return c.subject.contains(text, Qt::CaseInsensitive) || c.body.contains(text, Qt::CaseInsensitive)
            || c.author.contains(text, Qt::CaseInsensitive) || c.email.contains(text, Qt::CaseInsensitive)
            || c.hash.startsWith(text, Qt::CaseInsensitive);
    }
};

// Paints the graph column and the message column (ref chips + subject); the
// other columns use the default delegate.
class CommitDelegate : public QStyledItemDelegate
{
public:
    CommitDelegate(HistoryModel *model, QSortFilterProxyModel *proxy, QObject *parent)
        : QStyledItemDelegate(parent), m_model(model), m_proxy(proxy)
    {
    }

    int laneWidth() const { return OmarchyTheme::instance()->fontBase() + 2; }

    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const int col = index.column();
        if (col != HistoryModel::Graph && col != HistoryModel::Message) {
            QStyledItemDelegate::paint(p, option, index);
            return;
        }
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        opt.text.clear();
        const QWidget *w = opt.widget;
        QStyle *style = w ? w->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, p, w);

        const int row = m_proxy->mapToSource(index).row();
        p->save();
        p->setClipRect(opt.rect);
        if (col == HistoryModel::Graph)
            drawGraph(p, opt.rect, row);
        else
            drawMessage(p, opt, row);
        p->restore();
    }

private:
    QColor laneColor(int i) const
    {
        static const char *const keys[] = {"blue", "magenta", "cyan", "green", "yellow", "red",
                                           "bright_blue", "bright_magenta", "bright_cyan", "bright_green"};
        return OmarchyTheme::instance()->color(QLatin1String(keys[i % 10]));
    }

    void drawGraph(QPainter *p, const QRect &r, int row) const
    {
        const OmarchyTheme *t = OmarchyTheme::instance();
        const GraphRow &g = m_model->graph(row);
        const int lw = laneWidth();
        const int x0 = r.left() + 6;
        auto laneX = [&](int lane) { return qreal(x0 + lane * lw + lw / 2); };
        const qreal top = r.top(), bottom = r.bottom() + 1, mid = r.top() + r.height() / 2.0;
        const qreal nodeX = laneX(g.lane);

        p->setRenderHint(QPainter::Antialiasing, true);
        for (const GraphEdge &e : g.edges) {
            p->setPen(QPen(laneColor(e.color), 2));
            const QPointF a = e.from >= 0 ? QPointF(laneX(e.from), top) : QPointF(nodeX, mid);
            const QPointF b = e.to >= 0 ? QPointF(laneX(e.to), bottom) : QPointF(nodeX, mid);
            if (qFuzzyCompare(a.x(), b.x())) {
                p->drawLine(a, b);
            } else {
                const qreal my = (a.y() + b.y()) / 2;
                QPainterPath path(a);
                path.cubicTo(QPointF(a.x(), my), QPointF(b.x(), my), b);
                p->drawPath(path);
            }
        }
        const qreal rad = lw * 0.27;
        const Commit &c = m_model->commit(row);
        if (m_model->isHead(row)) {
            p->setPen(QPen(t->accent(), 2));
            p->setBrush(laneColor(g.color));
            p->drawEllipse(QPointF(nodeX, mid), rad + 1, rad + 1);
        } else if (c.parents.size() > 1) {
            p->setPen(QPen(laneColor(g.color), 2));
            p->setBrush(t->window());
            p->drawEllipse(QPointF(nodeX, mid), rad, rad);
        } else {
            p->setPen(Qt::NoPen);
            p->setBrush(laneColor(g.color));
            p->drawEllipse(QPointF(nodeX, mid), rad, rad);
        }
    }

    void drawMessage(QPainter *p, const QStyleOptionViewItem &opt, int row) const
    {
        const OmarchyTheme *t = OmarchyTheme::instance();
        const QRect r = opt.rect.adjusted(10, 0, -10, 0);
        int x = r.left();
        const QFont chipFont = t->captionFont();
        const QFontMetrics cfm(chipFont);
        p->setRenderHint(QPainter::Antialiasing, false);
        for (const RefLabel &label : m_model->labels(row)) {
            const int w = cfm.horizontalAdvance(label.name) + 12;
            const int h = cfm.height() + 4;
            if (x + w > r.right() - 60)
                break;
            const QRect chip(x, r.top() + (r.height() - h) / 2, w, h);
            QColor bg, border, fg;
            if (label.head) {
                bg = t->accent();
                border = t->accent();
                fg = t->window();
            } else if (label.type == RefLabel::Branch) {
                bg = t->selectedFill();
                border = t->accent();
                fg = t->accent();
            } else if (label.type == RefLabel::Tag) {
                const QColor y = t->color(QStringLiteral("yellow"));
                bg = OmarchyTheme::mix(t->window(), y, 0.18);
                border = y;
                fg = y;
            } else {
                bg = t->normalFill();
                border = t->normalBorder();
                fg = t->mutedText();
            }
            p->fillRect(chip, bg);
            p->setPen(border);
            p->drawRect(chip.adjusted(0, 0, -1, -1));
            p->setFont(chipFont);
            p->setPen(fg);
            p->drawText(chip, Qt::AlignCenter, label.name);
            x += w + 6;
        }
        p->setFont(opt.font);
        p->setPen((opt.state & QStyle::State_Selected) ? t->accent() : t->text());
        const QRect textRect(x, r.top(), r.right() - x, r.height());
        const QString subject = opt.fontMetrics.elidedText(m_model->commit(row).subject, Qt::ElideRight, textRect.width());
        p->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, subject);
    }

    HistoryModel *m_model;
    QSortFilterProxyModel *m_proxy;
};

} // namespace

HistoryView::HistoryView(GitRepo *repo, QWidget *parent)
    : QWidget(parent), m_repo(repo)
{
    const OmarchyTheme *theme = OmarchyTheme::instance();
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    // ---- Filter row
    auto *filterRow = new QHBoxLayout;
    filterRow->setSpacing(8);
    m_filter = new QLineEdit;
    m_filter->setPlaceholderText(icon(kMagnify) + tr("Filter by message, author or SHA"));
    m_filter->setClearButtonEnabled(true);
    filterRow->addWidget(m_filter, 1);
    m_allRefs = toolButton(icon(kBranch) + tr("All branches"), tr("Show the commits of every branch and tag, not just the current branch"));
    m_allRefs->setCheckable(true);
    filterRow->addWidget(m_allRefs);
    layout->addLayout(filterRow);

    // ---- Commit list
    m_model = new HistoryModel(repo, this);
    auto *proxy = new CommitFilter(this);
    proxy->setSourceModel(m_model);
    m_proxy = proxy;

    m_table = new QTableView;
    m_table->setModel(m_proxy);
    m_table->setItemDelegate(new CommitDelegate(m_model, m_proxy, m_table));
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setShowGrid(false);
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->setSortingEnabled(false);
    m_table->setWordWrap(false);
    m_table->setMouseTracking(true);
    m_table->setTextElideMode(Qt::ElideRight);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setMinimumSectionSize(40);
    m_table->horizontalHeader()->setHighlightSections(false);
    m_table->setColumnWidth(HistoryModel::Author, 150);
    m_table->setColumnWidth(HistoryModel::Date, 130);
    m_table->setColumnWidth(HistoryModel::Hash, 96);
    m_table->horizontalHeader()->installEventFilter(this);
    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &HistoryView::onCommitChanged);
    connect(m_table, &QTableView::customContextMenuRequested, this, &HistoryView::showContextMenu);
    connect(m_table->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        if (value >= m_table->verticalScrollBar()->maximum() - 1 && !m_model->exhausted())
            QTimer::singleShot(0, this, &HistoryView::loadMore);
    });

    // ---- Details of the selected commit
    m_details = new QPlainTextEdit;
    m_details->setReadOnly(true);
    m_details->setPlaceholderText(tr("Select a commit to see its details"));

    // ---- Files of the selected commit
    m_files = new ChangesModel(this);
    m_files->setCheckable(false);
    auto *filesProxy = new QSortFilterProxyModel(this);
    filesProxy->setSourceModel(m_files);
    filesProxy->setSortRole(Qt::DisplayRole);
    m_filesTable = new QTableView;
    m_filesTable->setModel(filesProxy);
    m_filesSetup = new ChangesTableSetup(m_filesTable);
    connect(m_filesTable->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this] { emit currentFileChanged(); });

    auto *splitter = new QSplitter(Qt::Vertical);
    splitter->setHandleWidth(8);
    splitter->setChildrenCollapsible(false);
    splitter->addWidget(m_table);
    splitter->addWidget(m_details);
    splitter->addWidget(m_filesTable);
    splitter->setStretchFactor(0, 5);
    splitter->setStretchFactor(1, 2);
    splitter->setStretchFactor(2, 3);
    splitter->setSizes({400, 150, 220});
    layout->addWidget(splitter, 1);

    // ---- Footer
    auto *footer = new QHBoxLayout;
    footer->setSpacing(10);
    m_countLabel = dimLabel();
    footer->addWidget(m_countLabel);
    footer->addStretch();
    m_moreButton = toolButton(icon(kMore) + tr("Load more"), tr("Load the next 500 commits"));
    connect(m_moreButton, &QToolButton::clicked, this, &HistoryView::loadMore);
    footer->addWidget(m_moreButton);
    layout->addLayout(footer);

    auto *filterDebounce = new QTimer(this);
    filterDebounce->setSingleShot(true);
    filterDebounce->setInterval(200);
    connect(filterDebounce, &QTimer::timeout, this, &HistoryView::onFilterChanged);
    connect(m_filter, &QLineEdit::textChanged, filterDebounce, qOverload<>(&QTimer::start));
    connect(m_allRefs, &QToolButton::toggled, this, [this](bool on) {
        m_model->setAllRefs(on);
        m_table->setColumnWidth(HistoryModel::Graph, 0);
        fitColumns();
        selectFirstCommit();
        updateFooter();
    });

    m_emptyMessage = tr("No commit selected.");
    Q_UNUSED(theme)
    applyTheme();
}

bool HistoryView::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_table->horizontalHeader() && event->type() == QEvent::Resize)
        fitColumns();
    return QWidget::eventFilter(watched, event);
}

// Graph column sized to the lanes in use, Message takes the rest (min 240 px).
void HistoryView::fitColumns()
{
    const int lw = OmarchyTheme::instance()->fontBase() + 2;
    const bool filtering = !static_cast<CommitFilter *>(m_proxy)->text.isEmpty();
    const int graph = filtering ? 0 : qMin(m_model->laneCount(), 12) * lw + 12;
    m_table->setColumnWidth(HistoryModel::Graph, graph);
    m_table->setColumnHidden(HistoryModel::Graph, filtering || m_model->laneCount() == 0);
    int others = graph;
    for (int c = HistoryModel::Author; c < HistoryModel::ColumnCount; ++c)
        others += m_table->columnWidth(c);
    m_table->setColumnWidth(HistoryModel::Message, qMax(240, m_table->viewport()->width() - others));
}

void HistoryView::applyTheme()
{
    const OmarchyTheme *theme = OmarchyTheme::instance();
    m_table->verticalHeader()->setDefaultSectionSize(qRound(theme->fontBase() * 2.33));
    m_details->setFont(theme->uiFont());
    m_countLabel->setFont(theme->captionFont());
    m_filesSetup->applyTheme();
    fitColumns();
    m_table->viewport()->update();
}

void HistoryView::focusFilter()
{
    m_filter->setFocus();
    m_filter->selectAll();
}

void HistoryView::reload()
{
    bool ok = false;
    const Commit current = currentCommit(&ok);
    if (ok)
        m_pendingHash = current.hash;
    m_model->reload();
    fitColumns();
    updateFooter();

    if (!m_pendingHash.isEmpty()) {
        for (int r = 0; r < m_proxy->rowCount(); ++r) {
            const int src = m_proxy->mapToSource(m_proxy->index(r, 0)).row();
            if (m_model->commit(src).hash == m_pendingHash) {
                m_table->selectRow(r);
                m_pendingHash.clear();
                onCommitChanged();
                return;
            }
        }
    }
    m_pendingHash.clear();
    selectFirstCommit();
}

void HistoryView::selectFirstCommit()
{
    if (m_proxy->rowCount() > 0) {
        m_table->selectRow(0);
    } else {
        m_files->setChanges({});
        m_details->clear();
        m_emptyMessage = m_model->failed() ? tr("No commits yet.") : tr("No commits match the filter.");
        emit currentFileChanged();
    }
}

void HistoryView::updateFooter()
{
    const int n = m_model->rowCount();
    if (m_model->failed())
        m_countLabel->setText(tr("No commits yet"));
    else if (m_model->exhausted())
        m_countLabel->setText(n == 1 ? tr("1 commit") : tr("%1 commits").arg(n));
    else
        m_countLabel->setText(tr("%1 commits loaded").arg(n));
    m_moreButton->setVisible(!m_model->exhausted() && !m_model->failed());
}

void HistoryView::loadMore()
{
    if (!m_model->loadMore())
        return;
    fitColumns();
    updateFooter();
    if (!m_table->currentIndex().isValid())
        selectFirstCommit();
}

void HistoryView::onFilterChanged()
{
    auto *filter = static_cast<CommitFilter *>(m_proxy);
    filter->text = m_filter->text().trimmed();
    filter->invalidate();
    fitColumns();
    if (!m_table->currentIndex().isValid())
        selectFirstCommit();
}

Commit HistoryView::currentCommit(bool *ok) const
{
    const QModelIndex idx = m_table->currentIndex();
    if (!idx.isValid()) {
        *ok = false;
        return Commit();
    }
    *ok = true;
    return m_model->commit(m_proxy->mapToSource(idx).row());
}

bool HistoryView::currentFile(Commit *commit, FileChange *change) const
{
    bool ok = false;
    const Commit c = currentCommit(&ok);
    const QModelIndex idx = m_filesTable->currentIndex();
    if (!ok || !idx.isValid())
        return false;
    auto *proxy = static_cast<QSortFilterProxyModel *>(m_filesTable->model());
    *commit = c;
    *change = m_files->change(proxy->mapToSource(idx).row());
    return true;
}

QString HistoryView::emptyMessage() const
{
    return m_emptyMessage;
}

void HistoryView::onCommitChanged()
{
    bool ok = false;
    const Commit c = currentCommit(&ok);
    if (!ok) {
        m_files->setChanges({});
        m_details->clear();
        m_emptyMessage = tr("No commit selected.");
        emit currentFileChanged();
        return;
    }

    QString text;
    text += tr("SHA:      %1\n").arg(c.hash);
    text += tr("Author:   %1 <%2>\n").arg(c.author, c.email);
    text += tr("Date:     %1\n").arg(c.date.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    if (!c.parents.isEmpty()) {
        QStringList shorts;
        for (const QString &p : c.parents)
            shorts << p.left(c.shortHash.size());
        text += tr("Parents:  %1\n").arg(shorts.join(QStringLiteral(", ")));
    }
    const int row = m_proxy->mapToSource(m_table->currentIndex()).row();
    QStringList refs;
    for (const RefLabel &l : m_model->labels(row))
        refs << l.name;
    if (!refs.isEmpty())
        text += tr("Refs:     %1\n").arg(refs.join(QStringLiteral(", ")));
    text += QStringLiteral("\n") + c.subject;
    if (!c.body.isEmpty())
        text += QStringLiteral("\n\n") + c.body;
    m_details->setPlainText(text);

    m_files->setChanges(m_repo->commitChanges(c));
    if (m_filesTable->model()->rowCount() > 0) {
        m_filesTable->selectRow(0); // emits currentFileChanged via the selection model
    } else {
        m_emptyMessage = tr("This commit changes no files.");
        emit currentFileChanged();
    }
}

void HistoryView::showContextMenu(const QPoint &pos)
{
    bool ok = false;
    const Commit c = currentCommit(&ok);
    if (!ok)
        return;
    QMenu menu(this);
    menu.addAction(tr("Copy SHA"), this, [c] { QApplication::clipboard()->setText(c.hash); });
    menu.addAction(tr("Copy short SHA"), this, [c] { QApplication::clipboard()->setText(c.shortHash); });
    menu.addAction(tr("Copy message"), this, [c] {
        QApplication::clipboard()->setText(c.body.isEmpty() ? c.subject : c.subject + QStringLiteral("\n\n") + c.body);
    });
    menu.exec(m_table->viewport()->mapToGlobal(pos));
}
