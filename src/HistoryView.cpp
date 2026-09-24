#include "HistoryView.h"
#include "ChangesModel.h"
#include "CommitDetails.h"
#include "HistoryModel.h"
#include "OmarchyTheme.h"
#include "RefChip.h"
#include "UiHelpers.h"

#include <QApplication>
#include <QClipboard>
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

using namespace ui;

namespace {

// screens.js historyPage(), in the pixels of a 12 px base font: the filter
// row, then the commit list as far below it as the diff sits below the diff
// pane's buttons (barGap(), not the design's 8 px); 10 px between the list,
// the details card and the files table; and the row with the count of
// commits, right under the last of them.
constexpr int kAllRefsGap = 8, kRefreshGap = 6;
constexpr int kSectionGap = 10;
// The card is 150 tall (132 stacked), the files table 110.
constexpr int kDetailsHeight = 150, kStackedDetailsHeight = 132, kFilesHeight = 110;
// The count row: the last section ends 22 px above the page's bottom edge,
// and the count's text is centred 6 px above that edge. While Load more
// stands in the row, the row is the button's 28 px.
constexpr int kCountRowHeight = 22, kCountTextAbove = 6;
// The field: its magnifier 8 px in, 14 px square; the text 28 px in. Under
// 200 px the placeholder is one word, under 300 two.
constexpr int kMagnifierX = 8, kMagnifierSize = 14, kFilterTextX = 28;
constexpr int kShortFilterWidth = 200, kMediumFilterWidth = 300;
// What QLineEdit keeps between its contents rectangle and the text on its
// own (QLineEditPrivate::horizontalMargin), under any style.
constexpr int kLineEditMargin = 2;

// The cells: text 10 px in from either side, the small 11 px text of the
// author, the date, the status and the counts; the ref chips 6 px apart, and
// never squeezing the subject under 60 px.
constexpr int kCellInset = 10, kSmallText = 11, kChipGap = 6, kMinSubject = 60;
// The narrowest the stretching column of either table gets before the table
// scrolls sideways: the design leaves both 100 px and more in the narrowest
// section they live in (the Medium class's 340 px), where the commit page's
// 240 would not fit at all.
constexpr int kMinStretch = 100;
// The least a column of the commit list may be dragged to: the 40 px the
// tables have always kept, or less where the design's narrowest column (the
// stacked graph, 30 px) is narrower than that — a floor above a column's
// width would widen it.
constexpr int kMinColumn = 30, kMinDragColumn = 40;

// The graph (screens.js commitsTable()): lanes 12 px apart, lines 2 px wide
// at 90 %, and every node the same 4 px disc with a 1.5 px ring of the
// window's background round it. The column holds up to twelve lanes.
constexpr int kLanePitch = 12, kMaxGraphLanes = 12;
constexpr qreal kLineWidth = 2, kLineAlpha = 0.9, kNodeRadius = 4, kNodeRing = 1.5;

// The commit list's columns by the window's width class, in design pixels;
// 0 is a column the class does not show. Message takes the rest.
struct CommitColumns {
    int graph, author, date;
};

CommitColumns commitColumns(WidthClass widthClass)
{
    switch (widthClass) {
    case WidthClass::Wide: return {40, 90, 130};
    case WidthClass::Large: return {40, 90, 100};
    case WidthClass::Medium: return {36, 0, 90};
    case WidthClass::Stacked: break;
    }
    return {30, 0, 0};
}

// The files table's columns by the width class (screens.js changesTable()),
// after the 30 px row numbers; Name takes the rest. Where the status column
// is 30 px it is the kit's status pill headed "St" (`pill`).
struct FileColumns {
    int path, status, lines, size;
    bool pill;
};

FileColumns fileColumns(WidthClass widthClass)
{
    switch (widthClass) {
    case WidthClass::Wide: return {130, 70, 70, 70, false};
    case WidthClass::Large: return {120, 60, 60, 0, false};
    case WidthClass::Medium:
    case WidthClass::Stacked: break;
    }
    return {110, 30, 0, 0, true};
}

QFont smallFont()
{
    QFont font = OmarchyTheme::instance()->uiFont();
    font.setPixelSize(space(kSmallText));
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

// Paints every column of the commit list but the SHA, which it never shows:
// the graph, the message (ref chips + subject), and the author and the date
// in the small dim text — dim on the selected row too, where only the
// subject turns to the accent.
class CommitDelegate : public QStyledItemDelegate
{
public:
    CommitDelegate(HistoryModel *model, QSortFilterProxyModel *proxy, QObject *parent)
        : QStyledItemDelegate(parent), m_model(model), m_proxy(proxy)
    {
    }

    void setGraph(const HistoryView::GraphGeometry &graph) { m_graph = graph; }
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

        const int row = m_proxy->mapToSource(index).row();
        p->save();
        p->setClipRect(opt.rect);
        switch (col) {
        case HistoryModel::Graph: drawGraph(p, opt.rect, row); break;
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
        auto laneX = [&](int lane) { return qreal(r.left() + m_graph.laneCentre(lane)); };
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
        const QRect r = opt.rect.adjusted(space(kCellInset), 0, -space(kCellInset), 0);
        int x = r.left();
        const int chipTop = r.top() + (r.height() - refChipHeight()) / 2;
        for (const RefLabel &label : m_model->labels(row)) {
            if (!m_remoteChips && label.type == RefLabel::Remote)
                continue;
            const int w = refChipWidth(label);
            if (x + w > r.right() + 1 - space(kMinSubject))
                break;
            paintRefChip(p, QPoint(x, chipTop), label);
            x += w + space(kChipGap);
        }
        p->setFont(opt.font);
        p->setPen((opt.state & QStyle::State_Selected) ? t->accent() : t->text());
        const QRect textRect(x, r.top(), r.right() + 1 - x, r.height());
        const QString subject = opt.fontMetrics.elidedText(m_model->commit(row).subject, Qt::ElideRight, textRect.width());
        p->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, subject);
    }

    // The date and the time where the column holds them, the date alone
    // where it does not (the design's 100 px and 90 px columns).
    static QString dateText(const QRect &cell, const Commit &commit)
    {
        const QDateTime when = commit.date.toLocalTime();
        const QString full = when.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
        const int room = cell.width() - 2 * space(kCellInset);
        return QFontMetrics(smallFont()).horizontalAdvance(full) <= room
            ? full
            : when.toString(QStringLiteral("yyyy-MM-dd"));
    }

    static void drawSmall(QPainter *p, const QRect &cell, const QString &text)
    {
        const QFont font = smallFont();
        const QRect r = cell.adjusted(space(kCellInset), 0, -space(kCellInset), 0);
        p->setFont(font);
        p->setPen(OmarchyTheme::instance()->mutedText());
        p->drawText(r, Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(font).elidedText(text, Qt::ElideRight, r.width()));
    }

    HistoryModel *m_model;
    QSortFilterProxyModel *m_proxy;
    HistoryView::GraphGeometry m_graph;
    bool m_remoteChips = true;
};

// The files of a commit (screens.js changesTable() over the commit's rows):
// the name in its status colour (the accent on the selected row), the dim
// folder, the status spelled out in its colour — or, in a 30 px column, the
// kit's pill — the added and removed lines either side of the column's
// middle, and the dim size at the right. The row numbers are the setup's.
class CommitFilesDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void setStatusPill(bool on) { m_statusPill = on; }

    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const int col = index.column();
        if (col != ChangesModel::Name && col != ChangesModel::Path && col != ChangesModel::Status
            && col != ChangesModel::LinesAdded && col != ChangesModel::Size) {
            QStyledItemDelegate::paint(p, option, index); // never shown here
            return;
        }
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index); // the row's own font (Qt::FontRole)
        paintCellBackground(p, opt);

        const OmarchyTheme *t = OmarchyTheme::instance();
        const bool selected = opt.state & QStyle::State_Selected;
        const QRect r = opt.rect.adjusted(space(kCellInset), 0, -space(kCellInset), 0);
        p->save();
        p->setClipRect(opt.rect);
        switch (col) {
        case ChangesModel::Name:
        case ChangesModel::Path: {
            const QColor colour = col == ChangesModel::Path ? t->mutedText()
                : selected                                  ? t->accent()
                                                            : statusColour(index);
            drawText(p, r, opt.font, colour, index.data(Qt::DisplayRole).toString(), Qt::AlignLeft, opt.textElideMode);
            break;
        }
        case ChangesModel::Status:
            if (m_statusPill)
                paintStatusPill(p, opt.rect, index);
            else
                drawText(p, opt.rect, smallFont(), statusColour(index), index.data(Qt::DisplayRole).toString(),
                         Qt::AlignHCenter, Qt::ElideRight);
            break;
        case ChangesModel::LinesAdded: drawLines(p, opt.rect, index); break;
        case ChangesModel::Size:
            drawText(p, r, smallFont(), t->mutedText(), index.data(Qt::DisplayRole).toString(), Qt::AlignRight,
                     Qt::ElideLeft);
            break;
        }
        p->restore();
    }

private:
    static void drawText(QPainter *p, const QRect &r, const QFont &font, const QColor &colour, const QString &text,
                         Qt::Alignment align, Qt::TextElideMode elide)
    {
        p->setFont(font);
        p->setPen(colour);
        p->drawText(r, align | Qt::AlignVCenter, QFontMetrics(font).elidedText(text, elide, r.width()));
    }

    // "+4" in green ending 2 px left of the column's middle, "−2" in red
    // from 2 px right of it; nothing where neither side has a line.
    static void drawLines(QPainter *p, const QRect &cell, const QModelIndex &index)
    {
        const int added = index.siblingAtColumn(ChangesModel::LinesAdded).data(Qt::DisplayRole).toInt();
        const int removed = index.siblingAtColumn(ChangesModel::LinesRemoved).data(Qt::DisplayRole).toInt();
        if (added <= 0 && removed <= 0)
            return;
        const OmarchyTheme *t = OmarchyTheme::instance();
        const int middle = cell.left() + cell.width() / 2;
        const int gap = space(2);
        const QFont font = smallFont();
        drawText(p, QRect(cell.left(), cell.top(), middle - gap - cell.left(), cell.height()), font,
                 t->color(QStringLiteral("green")), QStringLiteral("+%1").arg(qMax(0, added)), Qt::AlignRight,
                 Qt::ElideLeft);
        drawText(p, QRect(middle + gap, cell.top(), cell.right() + 1 - middle - gap, cell.height()), font,
                 t->color(QStringLiteral("red")), QStringLiteral("−%1").arg(qMax(0, removed)), Qt::AlignLeft,
                 Qt::ElideRight);
    }

    bool m_statusPill = false;
};

// The filter's magnifier: the dim glyph, centred by its ink in the kit's
// 14 px icon box. The field's child, so it stays whatever is typed.
class FilterMagnifier : public QWidget
{
public:
    explicit FilterMagnifier(QWidget *parent)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("filterIcon"));
        setAttribute(Qt::WA_TransparentForMouseEvents); // a click on it is the field's
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const QString glyph = icon(kMagnify).trimmed();
        if (glyph.isEmpty())
            return;
        QPainter p(this);
        QFont font = OmarchyTheme::instance()->uiFont();
        font.setPixelSize(space(kMagnifierSize));
        p.setFont(font);
        p.setPen(OmarchyTheme::instance()->mutedText());
        p.drawText(QRectF(rect()).center() - inkRect(font, glyph).center(), glyph);
    }
};

// The kit's field (kit.js field()) as the filter: the magnifier 8 px in, the
// text 28 px in, and a placeholder that says as much as the field's width
// holds.
class FilterField : public QLineEdit
{
public:
    FilterField()
    {
        setObjectName(QStringLiteral("historyFilter"));
        setAccessibleName(tr("Filter commits"));
        setClearButtonEnabled(true);
        m_magnifier = new FilterMagnifier(this);
    }

    void applyTheme()
    {
        setFixedHeight(buttonHeight());
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
    // The magnifier's box, the text's start and the placeholder, for the
    // width and the text size of the moment. The text starts after the
    // style's border and padding and QLineEdit's own margin, which the text
    // margin makes up to the design's 28 px.
    void fit()
    {
        const int side = space(kMagnifierSize);
        m_magnifier->setGeometry(space(kMagnifierX), (height() - side) / 2, side, side);
        QStyleOptionFrame option;
        initStyleOption(&option);
        const QRect contents = style()->subElementRect(QStyle::SE_LineEditContents, &option, this);
        const int margin = qMax(0, space(kFilterTextX) - contents.left() - kLineEditMargin);
        if (textMargins().left() != margin)
            setTextMargins(margin, 0, 0, 0);
        const QString placeholder = width() < space(kShortFilterWidth) ? tr("Filter")
            : width() < space(kMediumFilterWidth)                    ? tr("Filter commits")
                                                                      : tr("Filter by message, author or SHA");
        if (placeholderText() != placeholder)
            setPlaceholderText(placeholder);
    }

    FilterMagnifier *m_magnifier;
};

} // namespace

HistoryView::GraphGeometry HistoryView::graphGeometry(WidthClass widthClass, int lanes)
{
    const int design = commitColumns(widthClass).graph;
    GraphGeometry g;
    g.pitch = space(kLanePitch);
    // Half a pitch left of the column's middle: two lanes sit symmetric in
    // the design's width (14 and 26 px of 40).
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
    // row is that row's own margin, and the splitter's handles are the gaps
    // between the sections (applyTheme() scales them all).
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

    // ---- Commit list
    m_model = new HistoryModel(repo, this);
    auto *proxy = new CommitFilter(this);
    proxy->setSourceModel(m_model);
    m_proxy = proxy;

    m_table = new QTableView;
    m_table->setObjectName(QStringLiteral("commitsTable"));
    m_table->setModel(m_proxy);
    m_commitDelegate = new CommitDelegate(m_model, m_proxy, m_table);
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
    connect(m_table->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        if (value >= m_table->verticalScrollBar()->maximum() - 1 && !m_model->exhausted())
            QTimer::singleShot(0, this, &HistoryView::loadMore);
    });

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
    m_filesSetup = new ChangesTableSetup(m_filesTable);
    // The setup's delegate only turns the selected row's text to the accent;
    // this one paints the whole row the design's way.
    QAbstractItemDelegate *const setupDelegate = m_filesTable->itemDelegate();
    m_filesDelegate = new CommitFilesDelegate(m_filesTable);
    m_filesTable->setItemDelegate(m_filesDelegate);
    delete setupDelegate;
    // The extension is in the name, and the removed lines share the added
    // lines' column ("+ −").
    m_filesTable->setColumnHidden(ChangesModel::Extension, true);
    m_filesTable->setColumnHidden(ChangesModel::LinesRemoved, true);
    // The design reads Name, Path, Status, "+ −", Size: the size moves to the
    // end, where the model has the line counts.
    QHeaderView *const filesHeader = m_filesTable->horizontalHeader();
    filesHeader->moveSection(filesHeader->visualIndex(ChangesModel::Size), ChangesModel::ColumnCount - 1);
    connect(m_filesTable->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this] { emit currentFileChanged(); });

    // The three sections share the height: the list takes whatever the card
    // and the files table leave, and the handles between them are the gaps.
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

    // ---- The count of commits, and Load more while there is more to load
    auto *countRow = new QHBoxLayout;
    countRow->setContentsMargins(0, 0, 0, 0);
    countRow->setSpacing(0);
    m_countLabel = new QLabel;
    m_countLabel->setObjectName(QStringLiteral("historyCount"));
    countRow->addWidget(m_countLabel); // its height is the row's (alignCountRow())
    countRow->addStretch();
    m_moreButton = toolButton<KitButton>(icon(kChevron) + tr("Load more"), tr("Load the next 500 commits"));
    connect(m_moreButton, &QToolButton::clicked, this, &HistoryView::loadMore);
    countRow->addWidget(m_moreButton, 0, Qt::AlignVCenter);
    layout->addLayout(countRow);

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
    m_details->clear(m_emptyMessage);
    applyTheme();
}

// Graph column sized to the lanes in use, Message takes the rest.
void HistoryView::fitColumns()
{
    const bool filtering = !static_cast<CommitFilter *>(m_proxy)->text.isEmpty();
    const GraphGeometry geometry = graphGeometry(m_widthClass, m_model->laneCount());
    static_cast<CommitDelegate *>(m_commitDelegate)->setGraph(geometry);
    const int graph = filtering ? 0 : geometry.width;
    m_table->setColumnWidth(HistoryModel::Graph, graph);
    m_table->setColumnHidden(HistoryModel::Graph, filtering || m_model->laneCount() == 0);
    // `graph` counts even while the column is hidden: with no lanes loaded yet
    // its room stays reserved, so the message column does not jump once it is.
    int others = graph;
    for (int c = HistoryModel::Author; c < HistoryModel::ColumnCount; ++c)
        if (!m_table->isColumnHidden(c))
            others += m_table->columnWidth(c);
    fitStretchColumn(m_table, HistoryModel::Message, others, space(kMinStretch));
}

void HistoryView::applyTheme()
{
    // The files table is never shorter than its header and one row, inside
    // the tables' 1 px frame.
    m_filesTable->setMinimumHeight(tableHeaderHeight() + fileRowHeight() + 2);
    m_table->verticalHeader()->setDefaultSectionSize(tableRowHeight());
    m_table->horizontalHeader()->setFixedHeight(tableHeaderHeight());
    static_cast<FilterField *>(m_filter)->applyTheme();
    m_filterRow->setContentsMargins(0, 0, 0, barGap());
    m_allRefsGap->changeSize(space(kAllRefsGap), 0, QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_refreshGap->changeSize(space(kRefreshGap), 0, QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_filterRow->invalidate();
    m_moreButton->setText(icon(kChevron) + tr("Load more"));
    alignCountRow();
    applyAllRefsForm(); // the glyph, and the square on the new text size
    m_details->applyTheme();
    m_filesSetup->applyTheme();
    applyCommitColumns();
    applyFilesColumns();
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

void HistoryView::setWindowClass(WidthClass width, bool shallow, bool extraSmall)
{
    if (m_widthClass == width && m_shallow == shallow && m_extraSmall == extraSmall)
        return;
    const bool classChanged = m_widthClass != width || m_shallow != shallow;
    m_widthClass = width;
    m_shallow = shallow;
    m_extraSmall = extraSmall;
    static_cast<CommitDelegate *>(m_commitDelegate)->setRemoteChips(!extraSmall);
    if (classChanged) {
        applyCommitColumns();
        applyFilesColumns();
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
    m_table->horizontalHeader()->setMinimumSectionSize(qMin(kMinDragColumn, space(kMinColumn)));
    const QList<QPair<int, int>> sized{{HistoryModel::Author, columns.author}, {HistoryModel::Date, columns.date}};
    for (const auto &[column, px] : sized) {
        if (px > 0)
            m_table->setColumnWidth(column, space(px));
        m_table->setColumnHidden(column, px == 0);
    }
    m_table->setColumnHidden(HistoryModel::Hash, true);
}

// The files table's columns for the class, after the setup's 30 px row
// numbers: Path, Status (or the St pill), "+ −" and Size where the class has
// them, and Name taking the rest.
void HistoryView::applyFilesColumns()
{
    const FileColumns columns = fileColumns(m_widthClass);
    const QList<QPair<int, int>> sized{{ChangesModel::Path, columns.path},
                                       {ChangesModel::Status, columns.status},
                                       {ChangesModel::LinesAdded, columns.lines},
                                       {ChangesModel::Size, columns.size}};
    for (const auto &[column, px] : sized) {
        if (px > 0)
            m_filesTable->setColumnWidth(column, space(px));
        m_filesTable->setColumnHidden(column, px == 0);
    }
    static_cast<CommitFilesDelegate *>(m_filesDelegate)->setStatusPill(columns.pill);
    if (auto *header = qobject_cast<ChangesHeader *>(m_filesTable->horizontalHeader())) {
        header->setSectionText(ChangesModel::LinesAdded, tr("+ −"));
        header->setSectionText(ChangesModel::Status, columns.pill ? tr("St") : QString());
    }
    m_filesSetup->setStretchColumn(ChangesModel::Name, space(kMinStretch));
    m_filesTable->viewport()->update();
}

// Shallow, neither the card nor the files table; stacked, the card without
// the files table. The files table only ever hides: it is the Mini rail's
// model and selection, and the diff follows its current row. The card and
// the files table get the design's heights, the list the rest — unless the
// user has dragged a handle this session.
void HistoryView::applySections()
{
    m_details->setStacked(m_stacked);
    m_details->setVisible(!m_shallow);
    m_filesTable->setVisible(!m_shallow && !m_stacked);
    m_splitter->setHandleWidth(space(kSectionGap));
    if (m_sizedByHand)
        return;
    const int details = space(m_stacked ? kStackedDetailsHeight : kDetailsHeight);
    const int files = space(kFilesHeight);
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
        for (int r = 0; r < m_proxy->rowCount(); ++r) {
            const int src = m_proxy->mapToSource(m_proxy->index(r, 0)).row();
            if (m_model->commit(src).hash == m_pendingHash) {
                m_table->selectRow(r);
                m_pendingHash.clear();
                onCommitChanged();
                m_pendingFile.clear();
                commitsBar->setValue(commitsScroll);
                filesBar->setValue(filesScroll);
                return;
            }
        }
    }
    m_pendingHash.clear();
    m_pendingFile.clear();
    selectFirstCommit();
}

void HistoryView::selectFirstCommit()
{
    if (m_proxy->rowCount() > 0) {
        m_table->selectRow(0);
    } else {
        m_files->setChanges({});
        m_emptyMessage = m_model->failed() ? tr("No commits yet.") : tr("No commits match the filter.");
        m_details->clear(m_emptyMessage);
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
    alignCountRow();
}

// The count on the design's line: the label is the 22 px row, its text's
// baseline where kit.js text() puts it for a centre 6 px above the page's
// bottom edge (the centre plus 0.36 of the size). While Load more stands
// beside it, the row is the button's height and the two are centred on each
// other.
void HistoryView::alignCountRow()
{
    if (!m_moreButton->isHidden()) {
        m_countLabel->setContentsMargins(0, 0, 0, 0);
        m_countLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_countLabel->setFixedHeight(buttonHeight());
        return;
    }
    const QFont font = smallFont(); // the stylesheet's size for the label
    const int baseline = qRound(space(kCountRowHeight) - space(kCountTextAbove) + 0.36 * font.pixelSize());
    m_countLabel->setContentsMargins(0, qMax(0, baseline - QFontMetrics(font).ascent()), 0, 0);
    m_countLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_countLabel->setFixedHeight(space(kCountRowHeight));
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
        if (m_reloading)
            return; // reload() selects a commit again right after the reset
        m_files->setChanges({});
        m_emptyMessage = tr("No commit selected.");
        m_details->clear(m_emptyMessage);
        emit currentFileChanged();
        return;
    }

    m_files->setChanges(m_repo->commitChanges(c));
    const int sourceRow = m_proxy->mapToSource(m_table->currentIndex()).row();
    m_details->setCommit(c, m_model->labels(sourceRow), m_files->count());
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
