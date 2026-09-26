#include "HistoryView.h"
#include "ChangesModel.h"
#include "CommitDetails.h"
#include "HistoryModel.h"
#include "OmarchyTheme.h"
#include "RefChip.h"
#include "UiHelpers.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStyleOptionFrame>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <iterator>
#include <memory>

using namespace ui;

namespace {

// screens.js historyPage(), on the grid of Grid.h: the filter row is a
// control row (the field, an item gap, All branches, an item gap, Refresh),
// the commit list 4 under it — the same band as the diff toolbar's, so the
// list starts 32 under the page's top, level with the diff; the list, the
// details card and the commit's files a block gap apart (the window's height
// class); and the 24 px row with the count of commits straight after the last
// of them. Sizes the design gives the page alone: the card 152 tall (132
// stacked); the files table a header and three rows.
constexpr int kDetailsHeight = 152, kStackedDetailsHeight = 132, kFilesRows = 3;
// The field (kit.js field()): its magnifier's 16 px box 8 in, the text 4
// after it, and the clear glyph's box the same at the other end; the glyphs
// at their 14 px text size. Under 200 px the placeholder is one word, under
// 300 two.
constexpr int kFieldGlyph = 14;
constexpr int kShortFilterWidth = 200, kMediumFilterWidth = 300;
// What QLineEdit keeps between its contents rectangle and the text on its
// own (QLineEditPrivate::horizontalMargin), under any style.
constexpr int kLineEditMargin = 2;

// The cells: text 8 in from either side (pad::control), the small 11 px text
// of the author and the date; the ref chips a cluster apart and the subject
// the same after the last, never squeezed under 60 px.
constexpr int kSmallText = 11, kMinSubject = 60;
// The least a column of the commit list may be dragged to: the 40 px the
// tables have always kept, or less where the design's narrowest column (the
// stacked graph, 32 px) is narrower than that — a floor above a column's
// width would widen it.
constexpr int kMinColumn = 32, kMinDragColumn = 40;

// The graph (screens.js commitsTable()): lanes 12 px apart, lines 2 px wide
// at 90 %, and every node the same 4 px disc with a 1.5 px ring of the
// window's background round it. The column holds up to twelve lanes.
constexpr int kLanePitch = 12, kMaxGraphLanes = 12;
constexpr qreal kLineWidth = 2, kLineAlpha = 0.9, kNodeRadius = 4, kNodeRing = 1.5;

// The commit list's columns by the window's width class, in design pixels;
// 0 is a column the class does not show. Message takes the rest. Author
// shows in every class, the user's rule (2026-09-25), where the design's
// narrower frames leave it out.
struct CommitColumns {
    int graph, author, date;
};

CommitColumns commitColumns(WidthClass widthClass)
{
    switch (widthClass) {
    case WidthClass::Wide: return {40, 88, 128};
    case WidthClass::Large: return {40, 88, 120};
    case WidthClass::Medium: return {36, 88, 88};
    case WidthClass::Stacked: break;
    }
    return {32, 72, 0};
}

QFont smallFont()
{
    QFont font = OmarchyTheme::instance()->uiFont();
    font.setPixelSize(fontPx(kSmallText));
    return font;
}

// The cell's own background — hover, selection — as the style paints it for
// the row, without the text: everything on it the delegates paint themselves.
void paintCellBackground(QPainter *p, QStyleOptionViewItem opt)
{
    opt.text.clear();
    const QWidget *w = opt.widget;
    QStyle *style = w ? w->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, p, w);
}

// Paints every column of the commit list but the SHA, which it never shows:
// the graph, the message (ref chips + subject), and the author and the date
// in the small dim text — dim on the selected row too, where only the
// subject turns to the accent.
class CommitDelegate : public QStyledItemDelegate
{
public:
    CommitDelegate(HistoryModel *model, QObject *parent)
        : QStyledItemDelegate(parent), m_model(model)
    {
    }

    // `leading`: the pixels of the table's frame the column's design box
    // starts with and its cells do not — the lanes stand where the design
    // puts them from the box's edge.
    void setGraph(const HistoryView::GraphGeometry &graph, int leading)
    {
        m_graph = graph;
        m_leading = leading;
    }
    // The narrowest widths leave the remote branches out of the rows.
    void setRemoteChips(bool on) { m_remoteChips = on; }

    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const int col = index.column();
        if (col == HistoryModel::Hash) {
            QStyledItemDelegate::paint(p, option, index);
            return;
        }
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        paintCellBackground(p, opt);

        const int row = index.row();
        p->save();
        p->setClipRect(opt.rect);
        switch (col) {
        case HistoryModel::Graph:
            // The matches of a filter have no graph; their column is hidden.
            if (!m_model->filtering())
                drawGraph(p, opt.rect, row);
            break;
        case HistoryModel::Message: drawMessage(p, opt, row); break;
        case HistoryModel::Author: drawSmall(p, opt.rect, m_model->commit(row).author); break;
        case HistoryModel::Date: drawSmall(p, opt.rect, dateText(opt.rect, m_model->commit(row))); break;
        }
        p->restore();
    }

private:
    // The lanes in the order they are handed out: the first branch of the
    // list wears the accent, the next magenta, as in the design.
    static QColor laneColor(int i)
    {
        static const char *const keys[] = {nullptr, "magenta", "blue", "cyan", "yellow", "green", "red",
                                           "bright_magenta", "bright_blue", "bright_cyan"};
        const OmarchyTheme *t = OmarchyTheme::instance();
        const char *key = keys[i % int(std::size(keys))];
        return key ? t->color(QLatin1String(key)) : t->accent();
    }

    // The lanes' lines, each edge of the row's own (a lane passing through,
    // a curve from one lane into another where a branch forks or merges, a
    // line into or out of the node), and the node over them.
    void drawGraph(QPainter *p, const QRect &r, int row) const
    {
        const OmarchyTheme *t = OmarchyTheme::instance();
        const qreal scale = t->fontBase() / 12.0;
        const GraphRow &g = m_model->graph(row);
        auto laneX = [&](int lane) { return qreal(r.left() - m_leading + m_graph.laneCentre(lane)); };
        const qreal top = r.top(), bottom = r.bottom() + 1, mid = r.top() + r.height() / 2.0;
        const qreal nodeX = laneX(g.lane);

        p->setRenderHint(QPainter::Antialiasing, true);
        p->setBrush(Qt::NoBrush);
        for (const GraphEdge &e : g.edges) {
            QColor colour = laneColor(e.color);
            colour.setAlphaF(kLineAlpha);
            // Flat ends, so a lane's lines meet from one row to the next
            // without overlapping into a darker seam.
            p->setPen(QPen(colour, kLineWidth * scale, Qt::SolidLine, Qt::FlatCap));
            const QPointF a = e.from >= 0 ? QPointF(laneX(e.from), top) : QPointF(nodeX, mid);
            const QPointF b = e.to >= 0 ? QPointF(laneX(e.to), bottom) : QPointF(nodeX, mid);
            QPainterPath path(a);
            if (qFuzzyCompare(a.x(), b.x())) {
                path.lineTo(b);
            } else {
                const qreal my = (a.y() + b.y()) / 2;
                path.cubicTo(QPointF(a.x(), my), QPointF(b.x(), my), b);
            }
            p->drawPath(path);
        }
        p->setPen(QPen(t->window(), kNodeRing * scale));
        p->setBrush(laneColor(g.color));
        p->drawEllipse(QPointF(nodeX, mid), kNodeRadius * scale, kNodeRadius * scale);
    }

    void drawMessage(QPainter *p, const QStyleOptionViewItem &opt, int row) const
    {
        const OmarchyTheme *t = OmarchyTheme::instance();
        const QRect r = opt.rect.adjusted(space(pad::control), 0, -space(pad::control), 0);
        int x = r.left();
        const int chipTop = r.top() + (r.height() - refChipHeight()) / 2;
        for (const RefLabel &label : m_model->labels(row)) {
            if (!m_remoteChips && label.type == RefLabel::Remote)
                continue;
            const int w = refChipWidth(label);
            if (x + w > r.right() + 1 - space(kMinSubject))
                break;
            paintRefChip(p, QPoint(x, chipTop), label);
            x += w + space(gap::cluster);
        }
        p->setFont(opt.font);
        p->setPen((opt.state & QStyle::State_Selected) ? t->accent() : t->text());
        const QRect textRect(x, r.top(), r.right() + 1 - x, r.height());
        const QString subject = opt.fontMetrics.elidedText(m_model->commit(row).subject, Qt::ElideRight, textRect.width());
        p->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, subject);
    }

    // The date and the time where the column holds them, the date alone
    // where it does not (the design's 88 px column). The next column's text
    // keeps 8 clear of its own edge, so this one may run up to it.
    static QString dateText(const QRect &cell, const Commit &commit)
    {
        const QDateTime when = commit.date.toLocalTime();
        const QString full = when.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
        const int room = cell.width() - space(pad::control);
        return QFontMetrics(smallFont()).horizontalAdvance(full) <= room
            ? full
            : when.toString(QStringLiteral("yyyy-MM-dd"));
    }

    static void drawSmall(QPainter *p, const QRect &cell, const QString &text)
    {
        const QFont font = smallFont();
        const QRect r = cell.adjusted(space(pad::control), 0, 0, 0);
        p->setFont(font);
        p->setPen(OmarchyTheme::instance()->mutedText());
        p->drawText(r, Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(font).elidedText(text, Qt::ElideRight, r.width()));
    }

    HistoryModel *m_model;
    HistoryView::GraphGeometry m_graph;
    int m_leading = 0;
    bool m_remoteChips = true;
};

// A glyph of the filter's: dim, centred by its ink in the kit's 16 px icon
// box. The field's child, so it stays whatever is typed.
class FilterGlyph : public QWidget
{
public:
    FilterGlyph(uint glyph, const QString &fallback, QWidget *parent)
        : QWidget(parent), m_glyph(glyph), m_fallback(fallback)
    {
    }

protected:
    virtual QColor color() const { return OmarchyTheme::instance()->mutedText(); }

    void paintEvent(QPaintEvent *) override
    {
        const QString glyph = icon(m_glyph, m_fallback).trimmed();
        if (glyph.isEmpty())
            return;
        QPainter p(this);
        QFont font = OmarchyTheme::instance()->uiFont();
        font.setPixelSize(fontPx(kFieldGlyph));
        p.setFont(font);
        p.setPen(color());
        p.drawText(QRectF(rect()).center() - inkRect(font, glyph).center(), glyph);
    }

private:
    uint m_glyph;
    QString m_fallback;
};

// The magnifier in front of the text; a click on it is the field's.
class FilterMagnifier : public FilterGlyph
{
public:
    explicit FilterMagnifier(QWidget *parent)
        : FilterGlyph(kMagnify, QString(), parent)
    {
        setObjectName(QStringLiteral("filterIcon"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }
};

// The clear button at the end of the text (kit.js field()'s trailing icon):
// there while the field holds text, the text's own colour under the mouse,
// and a click empties the field and leaves the focus where it was.
class FilterClear : public FilterGlyph
{
public:
    explicit FilterClear(QLineEdit *field)
        : FilterGlyph(kClose, QStringLiteral("×"), field), m_field(field)
    {
        setObjectName(QStringLiteral("filterClear"));
        setAccessibleName(QObject::tr("Clear filter"));
        setAttribute(Qt::WA_Hover);
        setCursor(Qt::ArrowCursor);
        setVisible(false);
    }

protected:
    QColor color() const override
    {
        return underMouse() ? OmarchyTheme::instance()->text() : FilterGlyph::color();
    }

    void mousePressEvent(QMouseEvent *event) override { event->setAccepted(event->button() == Qt::LeftButton); }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()))
            m_field->clear();
    }

private:
    QLineEdit *m_field;
};

// The kit's field (kit.js field()) as the filter: [8][magnifier 16][4][text]
// [4][clear 16][8], and a placeholder that says as much as the field's width
// holds.
class FilterField : public QLineEdit
{
public:
    FilterField()
    {
        setObjectName(QStringLiteral("historyFilter"));
        setAccessibleName(tr("Filter commits"));
        m_magnifier = new FilterMagnifier(this);
        m_clear = new FilterClear(this);
        connect(this, &QLineEdit::textChanged, m_clear, [this](const QString &text) {
            m_clear->setVisible(!text.isEmpty());
        });
    }

    void applyTheme()
    {
        setFixedHeight(space(box::control));
        // The placeholder is as dim as the magnifier in front of it.
        QPalette pal = palette();
        pal.setColor(QPalette::PlaceholderText, OmarchyTheme::instance()->mutedText());
        setPalette(pal);
        fit();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLineEdit::resizeEvent(event);
        fit();
    }

private:
    // The glyphs' boxes, the text's ends and the placeholder, for the width
    // and the text size of the moment. The text keeps clear of the style's
    // border and padding and QLineEdit's own margin, which the text margins
    // make up to the design's 8 + 16 + 4 at either end.
    void fit()
    {
        const int side = space(box::icon);
        const int top = (height() - side) / 2;
        m_magnifier->setGeometry(space(pad::control), top, side, side);
        m_clear->setGeometry(width() - space(pad::control) - side, top, side, side);
        QStyleOptionFrame option;
        initStyleOption(&option);
        const QRect contents = style()->subElementRect(QStyle::SE_LineEditContents, &option, this);
        const int text = space(pad::control) + side + space(gap::icon);
        const QMargins margins(qMax(0, text - contents.left() - kLineEditMargin), 0,
                               qMax(0, text - (width() - 1 - contents.right()) - kLineEditMargin), 0);
        if (textMargins() != margins)
            setTextMargins(margins);
        const QString placeholder = width() < space(kShortFilterWidth) ? tr("Filter")
            : width() < space(kMediumFilterWidth)                    ? tr("Filter commits")
                                                                      : tr("Filter by message, author or SHA");
        if (placeholderText() != placeholder)
            setPlaceholderText(placeholder);
    }

    FilterMagnifier *m_magnifier;
    FilterClear *m_clear;
};

} // namespace

HistoryView::GraphGeometry HistoryView::graphGeometry(WidthClass widthClass, int lanes)
{
    const int design = commitColumns(widthClass).graph;
    GraphGeometry g;
    g.pitch = space(kLanePitch);
    // Half a pitch left of the column's middle: two lanes sit symmetric in
    // the design's width (14 and 26 px of 40, 10 and 22 of 32).
    g.firstLane = space(design / 2 - kLanePitch / 2);
    const int used = qBound(1, lanes, kMaxGraphLanes);
    g.width = qMax(space(design), 2 * g.firstLane + (used - 1) * g.pitch);
    return g;
}

HistoryView::HistoryView(GitRepo *repo, QWidget *parent)
    : QWidget(parent), m_repo(repo)
{
    // The page, top to bottom: the filter row, the sections in their
    // splitter, and the count row right under them; the gap under the filter
    // row is that row's own margin, and the splitter's handles are the block
    // gaps between the sections (applyTheme() scales them all).
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // ---- Filter row: the field, All branches and Refresh.
    m_filterRow = new QHBoxLayout;
    m_filterRow->setSpacing(0);
    m_filter = new FilterField;
    m_filterRow->addWidget(m_filter, 1);
    m_allRefsGap = new QSpacerItem(0, 0, QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_filterRow->addItem(m_allRefsGap);
    m_allRefs = toolButton<KitButton>(QString());
    m_allRefs->setAccessibleName(tr("All branches"));
    m_allRefs->setCheckable(true);
    applyAllRefsForm();
    m_filterRow->addWidget(m_allRefs);
    m_refreshGap = new QSpacerItem(0, 0, QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_filterRow->addItem(m_refreshGap);
    auto *refreshButton = iconButton(kRefresh, tr("R"), tr("Re-read the repository (F5)"), IconButtonSize::Toolbar, false);
    connect(refreshButton, &QToolButton::clicked, this, &HistoryView::refreshRequested);
    m_filterRow->addWidget(refreshButton);
    layout->addLayout(m_filterRow);

    // ---- Commit list: the loaded commits, or the matches of the filter
    m_model = new HistoryModel(repo, this);
    connect(m_model, &HistoryModel::searchChanged, this, &HistoryView::onSearchChanged);
    // The table's selection goes with a reset without a word.
    connect(m_model, &QAbstractItemModel::modelReset, this, [this] { m_currentHash.clear(); });

    m_table = new QTableView;
    m_table->setObjectName(QStringLiteral("commitsTable"));
    m_table->setModel(m_model);
    m_commitDelegate = new CommitDelegate(m_model, m_table);
    m_table->setItemDelegate(m_commitDelegate);
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
    m_table->horizontalHeader()->setHighlightSections(false);
    // The SHA is the model's, for the tooltips; the design shows no column of it.
    m_table->setColumnHidden(HistoryModel::Hash, true);
    onHeaderResize(m_table, [this] { fitColumns(); });
    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &HistoryView::onCommitChanged);
    connect(m_table, &QTableView::customContextMenuRequested, this, &HistoryView::showContextMenu);
    // The next 500 commits once the list is scrolled to its end, or the next
    // 500 of a filter's matches where its last page stopped full. A refresh
    // putting the list back where it was is not the user scrolling.
    connect(m_table->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        if (m_puttingBack || value < m_table->verticalScrollBar()->maximum() - 1)
            return;
        if (m_model->filtering() ? m_model->moreMatches() && !m_model->searching() : !m_model->exhausted())
            QTimer::singleShot(0, this, &HistoryView::loadMore);
    });
    // A table the user scrolls while a refresh brings the list back stays
    // where they leave it: its offset is not put back. Only the user
    // triggers an action on a scroll bar (the wheel, a drag, a click on the
    // bar, its keys) or presses its slider; the layout, scrollTo() and our
    // own setValue() only move the value.
    const auto userScrolls = [this](QScrollBar *bar, int *offset) {
        connect(bar, &QAbstractSlider::actionTriggered, this, [offset] { *offset = -1; });
        connect(bar, &QAbstractSlider::sliderPressed, this, [offset] { *offset = -1; });
    };
    userScrolls(m_table->verticalScrollBar(), &m_commitsOffset);

    // ---- Details of the selected commit
    m_details = new CommitDetails;
    connect(m_details, &CommitDetails::filesRequested, this, &HistoryView::filesRequested);

    // ---- Files of the selected commit
    m_files = new ChangesModel(this);
    m_files->setCheckable(false);
    auto *filesProxy = new QSortFilterProxyModel(this);
    filesProxy->setSourceModel(m_files);
    filesProxy->setSortRole(ChangesModel::SortRole);
    m_filesTable = new QTableView;
    m_filesTable->setObjectName(QStringLiteral("filesTable"));
    m_filesTable->setModel(filesProxy);
    // After the model, which says these files have no checkboxes: the setup
    // sizes the first column after that.
    // The design's file list, the commit page's: its columns by the width
    // class, every cell painted on the grid.
    m_filesSetup = new ChangesTableSetup(m_filesTable);
    // A file the user picks while a refresh brings the list back is theirs:
    // the one the commit comes back with (still showing, its diff shows at
    // once, currentFile()), and the files table stays where they have it.
    connect(m_filesTable->selectionModel(), &QItemSelectionModel::currentRowChanged, this, [this] {
        if (m_restoring && !m_puttingBack) {
            Commit c;
            FileChange f;
            if (m_keepShown && currentFile(&c, &f))
                m_pendingFile = f.path;
            m_filesOffset = -1;
        }
        emit currentFileChanged();
    });
    userScrolls(m_filesTable->verticalScrollBar(), &m_filesOffset);

    // The three sections share the height: the list takes whatever the card
    // and the files table leave, and the handles between them are the block
    // gaps.
    m_splitter = new QSplitter(Qt::Vertical);
    m_splitter->setObjectName(QStringLiteral("historySplitter"));
    m_splitter->setChildrenCollapsible(false);
    m_splitter->addWidget(m_table);
    m_splitter->addWidget(m_details);
    m_splitter->addWidget(m_filesTable);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 0);
    m_splitter->setStretchFactor(2, 0);
    // A handle the user drags keeps its place until the next session; the
    // design's heights come back only then.
    connect(m_splitter, &QSplitter::splitterMoved, this, [this] { m_sizedByHand = true; });
    layout->addWidget(m_splitter, 1);

    // ---- The count of commits, or of the filter's matches
    m_countLabel = new QLabel;
    m_countLabel->setObjectName(QStringLiteral("historyCount"));
    layout->addWidget(m_countLabel); // its height is the row's (alignCountRow())

    auto *filterDebounce = new QTimer(this);
    filterDebounce->setSingleShot(true);
    filterDebounce->setInterval(200);
    connect(filterDebounce, &QTimer::timeout, this, &HistoryView::onFilterChanged);
    connect(m_filter, &QLineEdit::textChanged, filterDebounce, qOverload<>(&QTimer::start));
    connect(m_allRefs, &QToolButton::toggled, this, [this](bool on) {
        // Filtering, the search starts over on the new scope, a new search:
        // nothing of a refresh's list is brought back.
        if (m_model->filtering()) {
            dropRestoration();
            rememberSearchCommit();
        }
        m_model->setAllRefs(on);
        m_table->setColumnWidth(HistoryModel::Graph, 0);
        fitColumns();
        if (!m_model->filtering())
            selectFirstCommit();
        updateFooter();
    });

    m_emptyMessage = tr("No commit selected.");
    m_details->clear(m_emptyMessage);
    applyTheme();
}

// Graph column sized to the lanes in use, Message takes the rest.
// The design measures the first and the last column from the table's outer
// edge, whose frame is their first (last) pixel: the cells, inside the frame,
// are that much narrower, so the columns between them line up with the
// design's (as the file lists', ChangesTableSetup).
void HistoryView::fitColumns()
{
    const bool filtering = m_model->filtering();
    const GraphGeometry geometry = graphGeometry(m_widthClass, m_model->laneCount());
    m_table->ensurePolished(); // the stylesheet's frame
    const int frame = m_table->frameWidth();
    static_cast<CommitDelegate *>(m_commitDelegate)->setGraph(geometry, frame);
    const int graph = filtering ? 0 : geometry.width - frame;
    m_table->setColumnWidth(HistoryModel::Graph, graph);
    m_table->setColumnHidden(HistoryModel::Graph, filtering || m_model->laneCount() == 0);
    // `graph` counts even while the column is hidden: with no lanes loaded yet
    // its room stays reserved, so the message column does not jump once it is.
    int others = graph;
    for (int c = HistoryModel::Author; c < HistoryModel::ColumnCount; ++c)
        if (!m_table->isColumnHidden(c))
            others += m_table->columnWidth(c);
    fitStretchColumn(m_table, HistoryModel::Message, others);
}

void HistoryView::applyTheme()
{
    // The files table is never shorter than its header and one row: two 24 px
    // rows, the tables' 1 px frame inside them.
    m_filesTable->setMinimumHeight(2 * rowHeight());
    m_table->verticalHeader()->setDefaultSectionSize(rowHeight());
    m_table->horizontalHeader()->setFixedHeight(tableHeaderHeight());
    static_cast<FilterField *>(m_filter)->applyTheme();
    m_filterRow->setContentsMargins(0, 0, 0, space(gap::controlRow));
    m_allRefsGap->changeSize(space(gap::item), 0, QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_refreshGap->changeSize(space(gap::item), 0, QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_filterRow->invalidate();
    alignCountRow();
    applyAllRefsForm(); // the glyph, and the square on the new text size
    m_details->applyTheme();
    m_filesSetup->applyTheme();
    applyCommitColumns();
    applySections();
    fitColumns();
    m_table->viewport()->update();
}

void HistoryView::setStacked(bool on)
{
    if (m_stacked == on)
        return;
    m_stacked = on;
    applyAllRefsForm();
    applySections();
}

void HistoryView::setWindowClass(WidthClass width, HeightClass height, bool extraSmall)
{
    const bool shallow = height == HeightClass::Shallow;
    const int block = densityFor(width, height).block;
    if (m_widthClass == width && m_shallow == shallow && m_block == block && m_extraSmall == extraSmall)
        return;
    const bool classChanged = m_widthClass != width || m_shallow != shallow || m_block != block;
    m_widthClass = width;
    m_shallow = shallow;
    m_block = block;
    m_extraSmall = extraSmall;
    static_cast<CommitDelegate *>(m_commitDelegate)->setRemoteChips(!extraSmall);
    if (classChanged) {
        applyCommitColumns();
        m_filesSetup->setWidthClass(width);
        applySections();
    }
    fitColumns();
    m_table->viewport()->update();
}

// Author and Date are the user's to drag, but every class change and every
// text size gives them the class's widths again; the SHA never shows.
void HistoryView::applyCommitColumns()
{
    const CommitColumns columns = commitColumns(m_widthClass);
    // Before the widths: a floor above a section's width would widen it.
    m_table->horizontalHeader()->setMinimumSectionSize(qMin(kMinDragColumn, space(kMinColumn)) - 1); // the frame-less first column
    const QList<QPair<int, int>> sized{{HistoryModel::Author, columns.author}, {HistoryModel::Date, columns.date}};
    // The last column shown, whose cells are inside the stylesheet's frame:
    // Date, or Author where the class has no Date.
    const int last = columns.date > 0 ? HistoryModel::Date : HistoryModel::Author;
    m_table->ensurePolished(); // the frame
    for (const auto &[column, px] : sized) {
        if (px > 0)
            m_table->setColumnWidth(column, space(px) - (column == last ? m_table->frameWidth() : 0));
        m_table->setColumnHidden(column, px == 0);
    }
    m_table->setColumnHidden(HistoryModel::Hash, true);
}

// Shallow, neither the card nor the files table; stacked, the card without
// the files table. The files table only ever hides: it is the Mini rail's
// model and selection, and the diff follows its current row. The card and
// the files table get the design's heights, the list the rest — unless the
// user has dragged a handle this session. The handles are the block gap.
void HistoryView::applySections()
{
    m_details->setStacked(m_stacked);
    m_details->setVisible(!m_shallow);
    m_filesTable->setVisible(!m_shallow && !m_stacked);
    m_splitter->setHandleWidth(space(m_block));
    // The stylesheet lights a 4 px strip in the handle's middle by this.
    if (m_splitter->property("blockGap").toInt() != m_block) {
        m_splitter->setProperty("blockGap", m_block);
        m_splitter->style()->unpolish(m_splitter);
        m_splitter->style()->polish(m_splitter);
    }
    if (m_sizedByHand)
        return;
    const int details = space(m_stacked ? kStackedDetailsHeight : kDetailsHeight);
    const int files = space(box::row) * (1 + kFilesRows);
    int total = 0;
    for (const int size : m_splitter->sizes())
        total += size;
    // The list is the one section that stretches, so it takes whatever the
    // other two leave however much that is.
    m_splitter->setSizes({qMax(1, total - details - files), details, files});
}

void HistoryView::applyAllRefsForm()
{
    const QString name = tr("All branches");
    if (m_stacked) {
        m_allRefs->setText(icon(kBranch, tr("B")).trimmed());
        m_allRefs->setToolTip(name);
    } else {
        m_allRefs->setText(icon(kBranch) + name);
        m_allRefs->setToolTip(tr("Show the commits of every branch and tag, not just the current branch"));
    }
    setIconForm(m_allRefs, m_stacked);
}

QAbstractItemView *HistoryView::activeListView() const
{
    return m_filesTable->isHidden() ? m_table : m_filesTable;
}

void HistoryView::focusFilter()
{
    m_filter->setFocus();
    m_filter->selectAll();
}

void HistoryView::reload()
{
    if (m_model->filtering()) {
        // The matches stay as they are unless a ref moved; then the search
        // starts over and brings the list back as it was: as many matches,
        // the commit that was current with its file (onSearchChanged()), and
        // the two lists' offsets. Until the commit is back the card, the
        // files and the diff keep showing it, so a commit that comes back
        // leaves them exactly as they were. A refresh while an earlier one's
        // list is still coming back keeps what that one put aside: the list
        // of the moment is only half of it. A table the user has scrolled
        // meanwhile is the exception: its reset would lose where they have it,
        // so that is the offset to put back now.
        const bool underWay = m_restoring;
        const bool keeping = m_keepShown;
        const bool commitsScrolled = underWay && m_commitsOffset < 0;
        const bool filesScrolled = underWay && m_filesOffset < 0;
        if (commitsScrolled)
            m_commitsOffset = m_table->verticalScrollBar()->value();
        if (filesScrolled)
            m_filesOffset = m_filesTable->verticalScrollBar()->value();
        bool ok = false;
        const Commit current = currentCommit(&ok);
        if (ok) {
            Commit c;
            FileChange f;
            const bool hasFile = currentFile(&c, &f);
            if (!underWay) {
                m_searchHash = current.hash;
                m_restoreRows = m_model->rowCount();
                m_commitsOffset = m_table->verticalScrollBar()->value();
                m_filesOffset = m_filesTable->verticalScrollBar()->value();
                m_restoring = true;
            }
            // Under way, a current commit is the one brought back already,
            // and its file the one it was given back.
            m_pendingFile = hasFile ? f.path : QString();
            m_keptCommit = current;
            m_keepShown = true; // before the reload, whose restart says so at once
        }
        m_reloading = true;
        const bool restarted = m_model->reload(false, m_restoring ? m_searchHash : QString(),
                                               m_restoring ? m_restoreRows : 0);
        m_reloading = false;
        if (!restarted) {
            // Nothing moved: the list, the selection and the offsets are as
            // they were, and an earlier refresh's list goes on coming back.
            if (!underWay) {
                dropRestoration();
            } else {
                m_keepShown = keeping;
                if (!keeping)
                    m_pendingFile.clear();
                // Nothing was reset, so nothing is to be put back: a table
                // the user scrolled stays theirs.
                if (commitsScrolled)
                    m_commitsOffset = -1;
                if (filesScrolled)
                    m_filesOffset = -1;
            }
        }
        fitColumns();
        updateFooter();
        return;
    }
    bool ok = false;
    const Commit current = currentCommit(&ok);
    if (ok)
        m_pendingHash = current.hash;
    {
        Commit c;
        FileChange f;
        if (currentFile(&c, &f))
            m_pendingFile = f.path;
    }
    // Re-selecting the commit and the file scrolls both tables to them; the
    // user may have scrolled away on purpose, so put the offsets back after.
    QScrollBar *const commitsBar = m_table->verticalScrollBar();
    QScrollBar *const filesBar = m_filesTable->verticalScrollBar();
    const int commitsScroll = commitsBar->value();
    const int filesScroll = filesBar->value();
    m_reloading = true;
    m_model->reload();
    m_reloading = false;
    fitColumns();
    updateFooter();

    if (!m_pendingHash.isEmpty()) {
        const Commit still = currentCommit(&ok);
        if (ok && still.hash == m_pendingHash) {
            // No ref moved, so the model kept its rows: everything is as it was.
            m_pendingHash.clear();
            m_pendingFile.clear();
            return;
        }
        const int row = m_model->rowOf(m_pendingHash);
        if (row >= 0) {
            m_table->selectRow(row);
            m_pendingHash.clear();
            onCommitChanged();
            m_pendingFile.clear();
            commitsBar->setValue(commitsScroll);
            filesBar->setValue(filesScroll);
            return;
        }
    }
    m_pendingHash.clear();
    m_pendingFile.clear();
    selectFirstCommit();
}

// The loaded commits' first, where there is one.
void HistoryView::selectFirstCommit()
{
    if (m_model->rowCount() > 0)
        m_table->selectRow(0);
    else
        showNoCommit(tr("No commits yet."));
}

void HistoryView::showNoCommit(const QString &message)
{
    m_currentHash.clear();
    m_files->setChanges({});
    m_emptyMessage = message;
    m_details->clear(m_emptyMessage);
    emit currentFileChanged();
}

void HistoryView::updateFooter()
{
    const int n = m_model->rowCount();
    const QString number = QLocale().toString(n); // 10,000 where the locale groups digits so
    if (m_model->filtering()) {
        const QString matches = n == 1 ? tr("1 match") : tr("%1 matches").arg(number);
        if (m_model->searching())
            m_countLabel->setText(tr("Searching… %1").arg(matches));
        else if (m_model->searchFailed()) // what was found stays; F5 tries again
            m_countLabel->setText(n == 0 ? tr("Search failed") : tr("%1 · search failed").arg(matches));
        else if (m_model->moreMatches())
            m_countLabel->setText(tr("%1 loaded").arg(matches)); // the pages so far, the next one a scroll away
        else
            m_countLabel->setText(n == 0 ? tr("No matches") : matches);
    } else if (m_model->failed()) {
        m_countLabel->setText(tr("No commits yet"));
    } else if (m_model->exhausted()) {
        m_countLabel->setText(n == 1 ? tr("1 commit") : tr("%1 commits").arg(number));
    } else {
        m_countLabel->setText(tr("%1 commits loaded").arg(number));
    }
}

// The count on the design's line: the label is the 24 px row, its text's
// baseline where kit.js text() puts it for the row's middle (the middle plus
// 0.36 of the size).
void HistoryView::alignCountRow()
{
    m_countLabel->setAlignment(Qt::AlignLeft);
    placeOnLine(m_countLabel, smallFont(), box::row); // the stylesheet's size for the label
}

void HistoryView::loadMore()
{
    if (!m_model->loadMore() || m_model->filtering())
        return; // a page of matches comes with searchChanged()
    fitColumns();
    updateFooter();
    if (!m_table->currentIndex().isValid())
        selectFirstCommit();
}

void HistoryView::onFilterChanged()
{
    const QString text = m_filter->text().trimmed();
    if (text == m_model->filter())
        return;
    rememberSearchCommit();
    dropRestoration(); // a refresh's list is not the new filter's business
    m_reloading = true; // the commit to select comes right after the reset, or with the matches
    m_model->setFilter(text);
    m_reloading = false;
    fitColumns();
    updateFooter();
    if (m_model->filtering())
        return; // onSearchChanged() selects among the matches
    // Back to the loaded commits: the one that was current (or looked for)
    // where they have it, else the first.
    const int row = m_searchHash.isEmpty() ? -1 : m_model->rowOf(m_searchHash);
    m_searchHash.clear();
    if (row < 0) {
        selectFirstCommit();
        return;
    }
    m_table->selectRow(row);
    m_table->scrollTo(m_model->index(row, HistoryModel::Message));
}

// Whatever is current is what a search that starts now looks for. Where
// nothing is, a search that has not found its commit yet hands it on: typing
// on while git is still walking does not lose it.
void HistoryView::rememberSearchCommit()
{
    bool ok = false;
    const Commit current = currentCommit(&ok);
    if (ok)
        m_searchHash = current.hash;
}

// A search started, took in a batch or ended. A commit is only ever
// selected where none is current, so one the user picked meanwhile stays:
// the commit that was current when the search started as soon as it turns
// up — the first match where there was none — and the first match anyway
// once git is done. A refresh's search still looking for its commit leaves
// the card, the files and the diff as they are; one that ends without it
// (or fails) gives them up.
void HistoryView::onSearchChanged()
{
    updateFooter();
    if (!m_model->filtering())
        return;
    if (!m_table->currentIndex().isValid()) {
        const int found = m_searchHash.isEmpty() ? -1 : m_model->rowOf(m_searchHash);
        int row = m_searchHash.isEmpty() ? 0 : found;
        if (row < 0 && !m_model->searching())
            row = 0;
        if (row >= 0 && row < m_model->rowCount()) {
            // The refresh's commit back, with its file; or not coming back,
            // and the list starts at the top. A list the user has scrolled
            // since the refresh stays where they have it, wherever the commit
            // is: selecting it scrolls to it, so the value goes back after.
            m_keepShown = false;
            m_restoring = m_restoring && row == found;
            m_puttingBack = m_restoring;
            QScrollBar *const bar = m_table->verticalScrollBar();
            const bool scrolled = m_restoring && m_commitsOffset < 0;
            const int value = bar->value();
            m_table->selectRow(row);
            if (scrolled)
                bar->setValue(value);
            else
                m_table->scrollTo(m_model->index(row, HistoryModel::Message));
            m_puttingBack = false;
            m_pendingFile.clear();
        } else if (m_keepShown && m_model->searching()) {
            return;
        } else {
            dropRestoration();
            showNoMatch();
            return;
        }
    }
    restoreOffsets();
}

void HistoryView::dropRestoration()
{
    m_keepShown = false;
    m_keptCommit = Commit();
    m_restoring = false;
    m_pendingFile.clear();
}

// The rows come in batches whose layout the table puts off to the event
// loop: laid out now, the scroll bars have the range the offsets need. A
// table the user scrolled meanwhile has no offset to put back.
void HistoryView::restoreOffsets()
{
    if (!m_restoring || m_keepShown || !m_table->currentIndex().isValid())
        return;
    if (m_model->rowCount() < m_restoreRows && m_model->searching())
        return;
    m_restoring = false;
    m_puttingBack = true;
    m_table->doItemsLayout();
    if (m_commitsOffset >= 0)
        m_table->verticalScrollBar()->setValue(m_commitsOffset);
    if (m_filesOffset >= 0)
        m_filesTable->verticalScrollBar()->setValue(m_filesOffset);
    m_puttingBack = false;
}

void HistoryView::showNoMatch()
{
    showNoCommit(m_model->searching()       ? tr("Searching…")
                 : m_model->searchFailed() ? tr("The search failed.")
                                           : tr("No commits match the filter."));
}

Commit HistoryView::currentCommit(bool *ok) const
{
    const QModelIndex idx = m_table->currentIndex();
    if (!idx.isValid()) {
        *ok = false;
        return Commit();
    }
    *ok = true;
    return m_model->commit(idx.row());
}

bool HistoryView::currentFile(Commit *commit, FileChange *change) const
{
    bool ok = false;
    Commit c = currentCommit(&ok);
    // A refresh's search still looking for the commit: the card and the
    // files show it yet, and a file picked among them is its file.
    if (!ok && m_keepShown) {
        c = m_keptCommit;
        ok = true;
    }
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
        // reload() selects a commit again right after the reset; a refresh's
        // search brings its commit back.
        if (m_reloading || m_keepShown)
            return;
        if (m_model->filtering())
            showNoMatch();
        else
            showNoCommit(tr("No commit selected."));
        return;
    }
    // A commit the user picks while a refresh's list is coming back is theirs.
    if (m_restoring && c.hash != m_searchHash)
        dropRestoration();

    // Another commit, or the same one again after a reset left none current
    // (a refresh's search that brought it back): the diff hears of it
    // whether or not the files' selection has anything to say, and an
    // identical diff stays where it was (MainWindow::presentDiff()).
    const bool another = c.hash != m_currentHash;
    m_currentHash = c.hash;
    m_files->setChanges(m_repo->commitChanges(c));
    m_details->setCommit(c, m_model->labels(m_table->currentIndex().row()), m_files->count());
    auto *filesProxy = static_cast<QSortFilterProxyModel *>(m_filesTable->model());
    if (filesProxy->rowCount() > 0) {
        int row = 0; // after a reload, the file that was selected before
        for (int r = 0; !m_pendingFile.isEmpty() && r < filesProxy->rowCount(); ++r) {
            if (m_files->change(filesProxy->mapToSource(filesProxy->index(r, 0)).row()).path == m_pendingFile) {
                row = r;
                break;
            }
        }
        if (m_filesTable->currentIndex().row() != row)
            m_filesTable->selectRow(row); // emits currentFileChanged via the selection model
        else if (another)
            emit currentFileChanged(); // a list equal to the last one keeps its row, and says nothing
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
    std::unique_ptr<QMenu> menu(commitMenu(c));
    menu->exec(m_table->viewport()->mapToGlobal(pos));
}

QMenu *HistoryView::commitMenu(const Commit &c)
{
    auto *menu = new QMenu(this);
    // Every row wears its glyph, as the More and Options menus' rows do.
    menu->addAction(icon(kContentCopy) + tr("Copy SHA"), this, [c] { QApplication::clipboard()->setText(c.hash); });
    menu->addAction(icon(kContentCopy) + tr("Copy short SHA"), this, [c] { QApplication::clipboard()->setText(c.shortHash); });
    menu->addAction(icon(kContentCopy) + tr("Copy message"), this, [c] {
        QApplication::clipboard()->setText(c.body.isEmpty() ? c.subject : c.subject + QStringLiteral("\n\n") + c.body);
    });
    menu->addSeparator();
    // The window's Ctrl+N does the same for the selected commit; the text
    // after the tab only names the keys.
    QAction *branch = menu->addAction(icon(kBranchPlus) + tr("New branch from here…") + QStringLiteral("\tCtrl+N"), this,
                                      [this, hash = c.hash] { emit newBranchRequested(hash); });
    branch->setToolTip(tr("Start a new branch at %1").arg(c.shortHash));
    return menu;
}
