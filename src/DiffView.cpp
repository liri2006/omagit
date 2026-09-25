#include "DiffView.h"
#include "OmarchyTheme.h"
#include "SyntaxHighlighter.h"
#include "TickMenu.h"
#include "UiHelpers.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QtMath>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStyleOptionSlider>
#include <QWheelEvent>

// The design's diff (screens.js diffPane()), on the grid of Grid.h: each
// side's file header a 24 px row, its hairline its last row and its text 8
// in; the first code line 4 under it, the lines 16 at a 12 px base; a 20 px
// mark column (± in a 16 px box, centred) and the line numbers right-aligned
// 8 short of 52, the code 8 after that, at 60. The design measures from the
// box's edge, which the view's 1 px frame is the first pixel of.
static constexpr int kMarkColumn = 20;
static constexpr int kMargin = 52;         // the mark column and the numbers
static constexpr int kMarkGlyph = 14;      // the mark's glyph, in 12 px-base pixels of the diff font
static constexpr int kPaneGap = 4; // separator between the two panes
static constexpr int kMinPaneText = 40;    // text a pane keeps beside its margin
static constexpr int kColumnStep = 4;      // columns per arrow key / scroll step
static constexpr int kScrollBarWidth = 16;
static constexpr int kScrollBarHandleInset = 5; // leaves room for the ribbons
static constexpr int kRibbonWidth = 3;     // change marks beside the scroll thumb
static constexpr qreal kSelectionAlpha = 0.35;
static constexpr int kMinFontPx = 7;  // Ctrl+wheel zoom limits for the diff text
static constexpr int kMaxFontPx = 40;

// Keep the native thumb and interactions, with change ranges beside the thumb.
class DiffScrollBar : public QScrollBar
{
public:
    explicit DiffScrollBar(QWidget *parent) : QScrollBar(Qt::Vertical, parent)
    {
        setObjectName(QStringLiteral("diffScrollBar"));
        setStyleSheet(QStringLiteral(
            "QScrollBar#diffScrollBar:vertical { width: %1px; }"
            "QScrollBar#diffScrollBar::handle:vertical { margin: 0 %2px; }")
            .arg(kScrollBarWidth).arg(kScrollBarHandleInset));
    }

    void setChanges(const DiffDocument &doc, const QVector<QVector<int>> &panes)
    {
        m_ranges.clear();
        m_rows = panes.isEmpty() ? 0 : panes[0].size();
        for (const auto state : {DiffLine::Removed, DiffLine::Added}) {
            int start = -1;
            for (int row = 0; row <= m_rows; ++row) {
                bool changed = false;
                if (row < m_rows) {
                    for (const auto &pane : panes) {
                        const int line = pane[row];
                        if (line >= 0 && doc.lines[line].state == state)
                            changed = true;
                    }
                }
                if (changed && start < 0)
                    start = row;
                if (!changed && start >= 0) {
                    m_ranges.append({start, row, state});
                    start = -1;
                }
            }
        }
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QScrollBar::paintEvent(event);
        if (m_rows == 0)
            return;
        QStyleOptionSlider option;
        initStyleOption(&option);
        const QRect groove = style()->subControlRect(QStyle::CC_ScrollBar, &option,
                                                     QStyle::SC_ScrollBarGroove, this);
        QPainter painter(this);
        painter.setClipRect(groove);
        const auto *theme = OmarchyTheme::instance();
        for (const auto &range : m_ranges) {
            const int top = groove.top() + qFloor(qreal(range.start) * groove.height() / m_rows);
            const int bottom = groove.top() + qCeil(qreal(range.end) * groove.height() / m_rows);
            const bool added = range.state == DiffLine::Added;
            painter.fillRect(QRect(added ? groove.right() - kRibbonWidth : groove.left() + 1,
                                   top, kRibbonWidth, qMax(1, bottom - top)),
                             added ? theme->diffAddedIcon() : theme->diffRemovedIcon());
        }
    }

private:
    struct Range { int start; int end; DiffLine::State state; };
    QVector<Range> m_ranges;
    int m_rows = 0;
};

DiffView::DiffView(QWidget *parent)
    : QAbstractScrollArea(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    // The hidden native bar owns the shared column offset, including keyboard
    // navigation and saved view state. Each pane gets its own visible control.
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_changeBar = new DiffScrollBar(this);
    setVerticalScrollBar(m_changeBar);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    viewport()->setCursor(Qt::IBeamCursor);
    viewport()->setMouseTracking(true);
    for (int pane = 0; pane < 2; ++pane) {
        auto *bar = new QScrollBar(Qt::Horizontal, this);
        m_paneScrollBars[pane] = bar;
        bar->setObjectName(QStringLiteral("diffHorizontalScrollBar%1").arg(pane));
        connect(bar, &QScrollBar::valueChanged, horizontalScrollBar(), &QScrollBar::setValue);
        connect(horizontalScrollBar(), &QScrollBar::valueChanged, bar, &QScrollBar::setValue);
    }
    refreshTheme();
    connect(verticalScrollBar(), &QScrollBar::valueChanged, viewport(), qOverload<>(&QWidget::update));
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, viewport(), qOverload<>(&QWidget::update));
}

// Re-reads the theme font; the user's zoom rides on top of whatever base
// size the desktop is set to, so a text-size change re-flows the diff too.
void DiffView::refreshTheme()
{
    const OmarchyTheme *theme = OmarchyTheme::instance();
    // The token colours are the same for every line: look them up once here
    // rather than per span while painting.
    static_assert(TokenKind(kTokenKindCount - 1) == TokenKind::Function,
                  "a new TokenKind needs a colour from OmarchyTheme::syntaxColor()");
    for (int kind = 0; kind < kTokenKindCount; ++kind)
        m_syntaxPens[size_t(kind)] = theme->syntaxColor(TokenKind(kind));
    m_font = OmarchyTheme::instance()->monoFont();
    if (m_zoom != 0)
        m_font.setPixelSize(qBound(kMinFontPx, m_font.pixelSize() + m_zoom, kMaxFontPx));
    viewport()->setFont(m_font);
    updateMetrics();
    updateScrollBars();
    verticalScrollBar()->update();
    viewport()->update();
}

// The design's 16 px line at the theme's own size, and in proportion to the
// font under the user's zoom (never shorter than the font's own height).
void DiffView::updateMetrics()
{
    const QFontMetricsF fm(m_font);
    // Text advances are fractional; rounding each column accumulates drift.
    m_charWidth = qMax(qreal(1), fm.horizontalAdvance(QLatin1Char('M')));
    const int base = qMax(1, OmarchyTheme::instance()->monoFont().pixelSize());
    m_lineHeight = qMax(qCeil(fm.height()), qRound(ui::space(ui::box::line) * m_font.pixelSize() / double(base)));
}

// The header's 24, less the frame's top row it starts on.
int DiffView::headerHeight() const
{
    return qMax(1, ui::space(ui::box::row) - frameWidth());
}

// Where the first code line starts: 4 under the header.
int DiffView::linesTop() const
{
    return headerHeight() + ui::space(ui::gap::cluster);
}

// The margin beside the code: the mark column and the numbers, 52 or as wide
// as the numbers need, 8 short of its edge.
int DiffView::marginWidth() const
{
    return qMax(ui::space(kMargin),
                ui::space(kMarkColumn) + qCeil(m_digits * m_charWidth) + ui::space(ui::pad::control));
}

// The design's distances inside a pane are measured from the box's edge; the
// first pane's rect starts inside the view's frame, one pixel in from it.
int DiffView::edgeOffset(int pane) const
{
    return pane == 0 ? frameWidth() : 0;
}

// The margin's width inside the pane's rect: the design's from the box's edge.
int DiffView::paneMargin(int pane) const
{
    return marginWidth() - edgeOffset(pane);
}

DiffView::PaneSplit DiffView::paneSplitMetrics() const
{
    const int available = qMax(1, viewport()->width() - kPaneGap);
    return {available, qMin(available / 2, marginWidth() + kMinPaneText)};
}

QRect DiffView::paneRect(int pane) const
{
    const int top = linesTop();
    const int h = viewport()->height() - top;
    if (paneCount() <= 1)
        return QRect(0, top, viewport()->width(), h);
    const PaneSplit split = paneSplitMetrics();
    const int left = split.clamp(int(split.available * m_paneSplit));
    if (pane == 0)
        return QRect(0, top, left, h);
    return QRect(left + kPaneGap, top, split.available - left, h);
}

bool DiffView::onDivider(const QPoint &point) const
{
    return paneCount() == 2 && viewport()->rect().contains(point)
        && point.x() >= paneRect(0).width() - 2
        && point.x() < paneRect(1).left() + 2;
}

// The divider is the only thing in the viewport that is not text.
void DiffView::updateCursor(const QPoint &pos)
{
    viewport()->setCursor(onDivider(pos) && !m_dragging ? Qt::SplitHCursor : Qt::IBeamCursor);
}

void DiffView::setPaneSplit(qreal split)
{
    m_paneSplit = qBound(qreal(0), split, qreal(1));
    updateScrollBars();
    viewport()->update();
}

int DiffView::lineAt(int pane, int row) const
{
    if (pane < 0 || pane >= m_panes.size() || row < 0 || row >= m_panes[pane].size())
        return -1;
    return m_panes[pane][row];
}

// Builds the row layout for the current mode from the parsed diff.
void DiffView::rebuildLayout()
{
    m_panes.clear();
    m_blockStarts.clear();
    const int n = m_doc.lines.size();

    if (m_mode == OnePane) {
        QVector<int> rows(n);
        for (int i = 0; i < n; ++i)
            rows[i] = i;
        m_panes.append(rows);
        m_blockStarts = m_doc.blockStarts;
    } else {
        QVector<int> left, right;
        left.reserve(n);
        right.reserve(n);
        // A change block is removed lines then added lines. Pair them row by
        // row; whatever is left over gets a filler on the other side, exactly
        // like a classic two-pane diff view. Context lines sit on both.
        int context = 0;
        DiffModel::forEachChangeBlock(m_doc.lines, [&](int r0, int r1, int a0, int a1) {
            for (int i = context; i < r0; ++i) {
                left.append(i);
                right.append(i);
            }
            const int rows = qMax(r1 - r0, a1 - a0);
            m_blockStarts.append(left.size());
            for (int k = 0; k < rows; ++k) {
                left.append(k < r1 - r0 ? r0 + k : -1);
                right.append(k < a1 - a0 ? a0 + k : -1);
            }
            context = a1;
        });
        for (int i = context; i < n; ++i) {
            left.append(i);
            right.append(i);
        }
        m_panes.append(left);
        m_panes.append(right);
    }
    m_changeBar->setChanges(m_doc, m_panes);
}

// Expands the tabs of every line once, so the paint and hit-test paths can
// work off the expanded text and its column map without allocating.
void DiffView::rebuildLineLayouts()
{
    m_lineLayouts.clear();
    m_lineLayouts.reserve(m_doc.lines.size());
    m_maxCols = 0;
    for (const DiffLine &l : m_doc.lines) {
        LineLayout layout;
        if (!l.text.contains(QLatin1Char('\t'))) {
            layout.text = l.text; // implicitly shared, so this costs nothing
        } else {
            const int raw = l.text.size();
            layout.columns.resize(raw + 1);
            layout.text.reserve(raw + 16);
            for (int i = 0; i < raw; ++i) {
                layout.columns[i] = layout.text.size();
                const QChar c = l.text.at(i);
                if (c == QLatin1Char('\t'))
                    layout.text += QString(m_tabWidth - (layout.text.size() % m_tabWidth), QLatin1Char(' '));
                else
                    layout.text += c;
            }
            layout.columns[raw] = layout.text.size();
        }
        m_maxCols = qMax(m_maxCols, int(layout.text.size()));
        m_lineLayouts.append(layout);
    }
}

void DiffView::setSubtitleShown(bool shown)
{
    if (m_subtitleShown == shown)
        return;
    m_subtitleShown = shown;
    viewport()->update();
}

void DiffView::setDocument(const DiffDocument &doc, const QString &title, const QString &subtitle,
                           const QString &leftLabel, const QString &rightLabel)
{
    m_doc = doc;
    m_title = title;
    m_subtitle = subtitle;
    m_leftLabel = leftLabel;
    m_rightLabel = rightLabel;
    m_emptyMessage.clear();
    m_currentBlock = -1;
    m_selAnchor = m_selCursor = Pos();

    // One tokeniser pass per document, never in the paint path.
    m_language = SyntaxHighlighter::languageFor(m_title, &m_doc);
    applySyntax();

    rebuildLineLayouts();
    int maxNumber = 1;
    for (const DiffLine &l : m_doc.lines)
        maxNumber = qMax(maxNumber, qMax(l.oldNumber, l.newNumber));
    m_digits = QString::number(maxNumber).size();

    rebuildLayout();
    updateScrollBars();
    verticalScrollBar()->setValue(0);
    horizontalScrollBar()->setValue(0);
    viewport()->update();
    emit changeIndexChanged(m_currentBlock, m_blockStarts.size());
}

DiffView::ViewState DiffView::viewState() const
{
    return {verticalScrollBar()->value(), horizontalScrollBar()->value(), m_currentBlock};
}

void DiffView::restoreViewState(const ViewState &state)
{
    if (m_doc.lines.isEmpty())
        return;
    m_currentBlock = m_blockStarts.isEmpty() ? -1 : qBound(-1, state.block, int(m_blockStarts.size()) - 1);
    verticalScrollBar()->setValue(state.row); // the bars clamp to their ranges
    horizontalScrollBar()->setValue(state.column);
    viewport()->update();
    emit changeIndexChanged(m_currentBlock, m_blockStarts.size());
}

void DiffView::clear(const QString &message)
{
    m_doc = DiffDocument();
    rebuildLineLayouts();
    m_title.clear();
    m_subtitle.clear();
    m_emptyMessage = message;
    m_currentBlock = -1;
    m_selAnchor = m_selCursor = Pos();
    rebuildLayout();
    updateScrollBars();
    viewport()->update();
    emit changeIndexChanged(-1, 0);
}

void DiffView::setMode(Mode mode)
{
    if (mode == m_mode)
        return;
    m_mode = mode;
    m_resizingPanes = m_dragging = false;
    updateCursor(QPoint(-1, -1)); // no pointer to speak of: back to text
    // Keep the current change in view across the switch.
    const int block = m_currentBlock;
    rebuildLayout();
    m_selAnchor = m_selCursor = Pos();
    updateScrollBars();
    if (block >= 0 && block < m_blockStarts.size()) {
        m_currentBlock = block;
        scrollToRow(m_blockStarts[block]);
    }
    viewport()->update();
    emit modeChanged(m_mode);
    emit changeIndexChanged(m_currentBlock, m_blockStarts.size());
}

void DiffView::setShowWhitespace(bool on)
{
    m_showWhitespace = on;
    viewport()->update();
}

void DiffView::applySyntax()
{
    if (m_syntax)
        SyntaxHighlighter::highlight(m_doc, m_language);
    else
        SyntaxHighlighter::clear(m_doc);
}

void DiffView::setSyntaxHighlighting(bool on)
{
    if (m_syntax == on)
        return;
    m_syntax = on;
    applySyntax();
    viewport()->update();
    emit syntaxHighlightingChanged(on);
}

void DiffView::updateScrollBars()
{
    const int paneWidth = paneCount() == 2
        ? qMin(paneRect(0).width(), paneRect(1).width()) : paneRect(0).width();
    const int textWidth = paneWidth - marginWidth() - ui::space(ui::gap::item);
    const int visibleCols = qMax(1, qFloor(textWidth / m_charWidth));
    const int maxColumn = m_doc.lines.isEmpty() ? 0 : qMax(0, m_maxCols + 2 - visibleCols);
    const int barHeight = maxColumn > 0 ? m_paneScrollBars[0]->sizeHint().height() : 0;
    if (viewportMargins().bottom() != barHeight)
        setViewportMargins(0, 0, 0, barHeight);

    horizontalScrollBar()->setRange(0, maxColumn);
    horizontalScrollBar()->setPageStep(visibleCols);
    horizontalScrollBar()->setSingleStep(kColumnStep);
    for (int pane = 0; pane < 2; ++pane) {
        auto *bar = m_paneScrollBars[pane];
        const QSignalBlocker blocker(bar);
        bar->setRange(0, maxColumn);
        bar->setPageStep(visibleCols);
        bar->setSingleStep(kColumnStep);
        bar->setValue(horizontalScrollBar()->value());
        const QRect pr = paneRect(pane);
        bar->setGeometry(viewport()->x() + pr.x(), viewport()->geometry().bottom() + 1,
                         pr.width(), barHeight);
        bar->setVisible(maxColumn > 0 && pane < paneCount());
    }

    const int rows = m_panes.isEmpty() ? 0 : m_panes[0].size();
    const int visibleLines = qMax(1, (viewport()->height() - linesTop()) / m_lineHeight);
    verticalScrollBar()->setRange(0, qMax(0, rows - visibleLines));
    verticalScrollBar()->setPageStep(visibleLines);
    verticalScrollBar()->setSingleStep(1);

}

void DiffView::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBars();
}

const DiffView::LineLayout &DiffView::layoutAt(int pane, int row) const
{
    static const LineLayout filler;
    const int li = lineAt(pane, row);
    return li < 0 ? filler : m_lineLayouts.at(li);
}

QString DiffView::cellText(int pane, int row) const
{
    return layoutAt(pane, row).text;
}

// The design's change mark (screens.js diffPane(): icon minus / plus): the
// plain md-minus or md-plus glyph in the removed or added colour, 14 px on a
// 12 px line and zoomed with the text, centred by its ink in the icon cell.
void DiffView::drawMarginIcon(QPainter &p, const QRect &r, DiffLine::State state) const
{
    const OmarchyTheme *t = OmarchyTheme::instance();
    QColor c;
    uint code = 0;
    QString fallback;
    if (state == DiffLine::Added) {
        c = t->diffAddedIcon();
        code = ui::kPlus;
        fallback = QStringLiteral("+");
    } else if (state == DiffLine::Removed) {
        c = t->diffRemovedIcon();
        code = ui::kMinus;
        fallback = QStringLiteral("−");
    } else {
        return;
    }

    const QString glyph = t->glyph(code);
    QFont font = m_font;
    font.setPixelSize(qMax(1, qRound(m_font.pixelSize() * kMarkGlyph / 12.0)));
    const QString text = glyph.isEmpty() ? fallback : glyph;
    const QRectF ink = ui::inkRect(font, text);
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setFont(font);
    p.setPen(c);
    p.drawText(QRectF(r).center() - ink.center(), text);
    p.restore();
}

void DiffView::drawMargin(QPainter &p, int pane, int row, int y, const QRect &pr)
{
    const OmarchyTheme *t = OmarchyTheme::instance();
    const int li = lineAt(pane, row);
    if (li < 0)
        return;
    const DiffLine &l = m_doc.lines[li];

    const int markBox = ui::space(ui::box::icon);
    drawMarginIcon(p,
                   QRect(pr.left() - edgeOffset(pane) + (ui::space(kMarkColumn) - markBox) / 2,
                         y + (m_lineHeight - markBox) / 2, markBox, markBox),
                   l.state);

    QString number;
    p.setFont(m_font);
    if (l.state == DiffLine::Header) {
        number = QStringLiteral("…");
        p.setPen(t->mutedText());
    } else if (m_mode == TwoPane) {
        // Left pane numbers the base file, right pane the working tree.
        const int n = pane == 0 ? l.oldNumber : l.newNumber;
        number = n > 0 ? QString::number(n) : QString();
        p.setPen(l.state == DiffLine::Normal ? t->mutedText() : t->text());
    } else if (l.state == DiffLine::Removed) {
        number = l.oldNumber > 0 ? QString::number(l.oldNumber) : QString();
        p.setPen(t->mutedText());
    } else {
        number = l.newNumber > 0 ? QString::number(l.newNumber) : QString();
        p.setPen(l.state == DiffLine::Added ? t->text() : t->mutedText());
    }
    const int numbersLeft = pr.left() - edgeOffset(pane) + ui::space(kMarkColumn);
    const QRectF numRect(numbersLeft, y, pr.left() + paneMargin(pane) - ui::space(ui::pad::control) - numbersLeft,
                         m_lineHeight);
    p.drawText(numRect, Qt::AlignVCenter | Qt::AlignRight, number);
}

// The row's own tint, plus the tint its inline highlight draws on top.
QColor DiffView::fillLineBackground(QPainter &p, const DiffLine &l, const QRect &box) const
{
    const OmarchyTheme *t = OmarchyTheme::instance();
    QColor bg = t->diffNormalBg();
    QColor inlineBg;
    switch (l.state) {
    case DiffLine::Added:
        bg = t->diffAddedBg();
        inlineBg = t->diffInlineAddedBg();
        break;
    case DiffLine::Removed:
        bg = t->diffRemovedBg();
        inlineBg = t->diffInlineRemovedBg();
        break;
    case DiffLine::Header:
        bg = t->diffEmptyBg();
        break;
    case DiffLine::Normal:
        break;
    }
    p.fillRect(box, bg);
    return inlineBg;
}

// The intra-line ranges the model paired up, tinted on top of the row.
void DiffView::drawInlineHighlight(QPainter &p, const DiffLine &l, const LineLayout &layout,
                                   const Cell &cell, const QColor &inlineBg) const
{
    const int raw = l.text.size();
    for (const DiffSpan &s : l.inline_) {
        const int c0 = layout.column(qBound(0, s.start, raw));
        const int c1 = layout.column(qBound(0, s.start + s.length, raw));
        if (c1 > c0)
            p.fillRect(QRectF(cell.x0 + c0 * m_charWidth, cell.y, (c1 - c0) * m_charWidth, m_lineHeight),
                       inlineBg);
    }
}

// The selection lives in one pane, and stops one column past the row's text.
void DiffView::drawSelection(QPainter &p, int pane, int row, const LineLayout &layout, const Cell &cell) const
{
    if (!hasSelection() || m_selAnchor.pane != pane)
        return;
    Pos a = m_selAnchor, b = m_selCursor;
    if (b < a)
        std::swap(a, b);
    if (row < a.row || row > b.row)
        return;
    const int limit = int(layout.text.size()) + 1;
    const int c0 = qMin(row == a.row ? a.col : 0, limit);
    const int c1 = qMin(row == b.row ? b.col : limit, limit);
    if (c1 <= c0)
        return;
    QColor selc = OmarchyTheme::instance()->accent();
    selc.setAlphaF(kSelectionAlpha);
    p.fillRect(QRectF(cell.x0 + c0 * m_charWidth, cell.y, (c1 - c0) * m_charWidth, m_lineHeight), selc);
}

// One run generator for both the plain and the coloured path, so a syntax
// colour never shifts a character off the monospace grid.
void DiffView::drawCellText(QPainter &p, const DiffLine &l, const LineLayout &layout, const Cell &cell) const
{
    const OmarchyTheme *t = OmarchyTheme::instance();
    const QColor plain = l.state == DiffLine::Header ? t->mutedText() : t->text();
    auto run = [&](int c0, int c1, const QColor &pen) {
        c0 = qMax(c0, cell.firstCol);
        c1 = qMin(c1, cell.lastCol);
        if (c1 <= c0)
            return;
        p.setPen(pen);
        p.drawText(QPointF(cell.x0 + c0 * m_charWidth, cell.baseline), layout.text.mid(c0, c1 - c0));
    };

    p.setPen(plain);
    if (!m_syntax || l.syntax.isEmpty() || l.state == DiffLine::Header) {
        run(cell.firstCol, cell.lastCol, plain);
        return;
    }
    // The coloured spans and the gaps between them, as separate runs.
    const int raw = l.text.size();
    int col = cell.firstCol;
    for (const SyntaxSpan &sp : l.syntax) {
        const int c0 = layout.column(qBound(0, sp.start, raw));
        const int c1 = layout.column(qBound(0, sp.start + sp.length, raw));
        if (c1 <= cell.firstCol)
            continue;
        if (c0 >= cell.lastCol)
            break;
        run(col, c0, plain);
        run(c0, c1, m_syntaxPens[size_t(sp.kind)]);
        col = qMax(col, c1);
    }
    run(col, cell.lastCol, plain);
    p.setPen(plain);
}

void DiffView::drawWhitespaceMarkers(QPainter &p, const DiffLine &l, const LineLayout &layout,
                                     const Cell &cell) const
{
    p.save();
    p.setPen(OmarchyTheme::instance()->mutedText());
    for (int c = cell.firstCol; c < cell.lastCol; ++c)
        if (layout.text.at(c) == QLatin1Char(' '))
            p.drawText(QPointF(cell.x0 + c * m_charWidth, cell.baseline), QStringLiteral("·"));
    if (l.state != DiffLine::Header)
        p.drawText(QPointF(cell.x0 + layout.text.size() * m_charWidth, cell.baseline),
                   l.noNewline ? QStringLiteral("⌀") : QStringLiteral("¶"));
    p.restore();
}

void DiffView::drawCell(QPainter &p, int pane, int row, int y, const QRect &pr)
{
    const int textX = pr.left() + paneMargin(pane);
    const QRect box(textX, y, pr.right() - textX + 1, m_lineHeight);
    const int li = lineAt(pane, row);

    if (li < 0) {
        // Filler line: the other side has content here, this side does not.
        p.fillRect(box, OmarchyTheme::instance()->diffEmptyBg());
        return;
    }
    const DiffLine &l = m_doc.lines.at(li);
    const LineLayout &layout = m_lineLayouts.at(li);
    const QColor inlineBg = fillLineBackground(p, l, box);

    p.setFont(m_font);
    const int visibleCols = (pr.right() - textX) / m_charWidth + 3;
    Cell cell;
    cell.y = y;
    cell.x0 = textX + ui::space(ui::gap::item) - horizontalScrollBar()->value() * m_charWidth;
    cell.firstCol = qMax(0, horizontalScrollBar()->value() - 1);
    cell.lastCol = qMin(int(layout.text.size()), cell.firstCol + visibleCols);
    cell.baseline = y + (m_lineHeight + p.fontMetrics().ascent() - p.fontMetrics().descent()) / 2;

    drawInlineHighlight(p, l, layout, cell, inlineBg);
    drawSelection(p, pane, row, layout, cell);
    drawCellText(p, l, layout, cell);
    if (m_showWhitespace)
        drawWhitespaceMarkers(p, l, layout, cell);
}

void DiffView::paintEvent(QPaintEvent *)
{
    QPainter p(viewport());
    const OmarchyTheme *t = OmarchyTheme::instance();
    const int w = viewport()->width();
    const int h = viewport()->height();
    const int hh = headerHeight();

    p.fillRect(viewport()->rect(), t->diffNormalBg());

    // Header bar(s): file name, and in two-pane mode which version each side shows.
    p.fillRect(QRect(0, 0, w, hh), t->diffHeaderBg());
    p.setPen(t->border());
    p.drawLine(0, hh - 1, w, hh - 1);
    QFont bold = font();
    bold.setBold(true);

    if (m_title.isEmpty()) {
        // nothing loaded
    } else if (m_mode == TwoPane) {
        for (int pane = 0; pane < 2; ++pane) {
            const QRect pr = paneRect(pane);
            p.save();
            p.setClipRect(QRect(pr.left(), 0, pr.width(), hh));
            const int pad = ui::space(ui::pad::control);
            const QRect tr(pr.left() - edgeOffset(pane) + pad, 0, pr.width() + edgeOffset(pane) - 2 * pad, hh);
            const QString label = pane == 0 ? m_leftLabel : m_rightLabel;
            p.setFont(font());
            p.setPen(t->mutedText());
            const int labelW = p.fontMetrics().horizontalAdvance(label) + ui::space(ui::gap::item);
            p.drawText(tr, Qt::AlignVCenter | Qt::AlignRight, label);
            p.setFont(bold);
            p.setPen(t->text());
            p.drawText(tr, Qt::AlignVCenter | Qt::AlignLeft,
                       p.fontMetrics().elidedText(m_title, Qt::ElideMiddle, tr.width() - labelW));
            p.restore();
        }
    } else {
        const int pad = ui::space(ui::pad::control);
        const QRect tr(pad - edgeOffset(0), 0, w + edgeOffset(0) - 2 * pad, hh);
        p.setFont(font());
        p.setPen(t->mutedText());
        const QString subtitle = m_subtitleShown ? m_subtitle : QString();
        const int subW = subtitle.isEmpty() ? 0 : p.fontMetrics().horizontalAdvance(subtitle) + ui::space(ui::gap::item);
        if (!subtitle.isEmpty())
            p.drawText(tr, Qt::AlignVCenter | Qt::AlignRight, subtitle);
        p.setFont(bold);
        p.setPen(t->text());
        p.drawText(tr, Qt::AlignVCenter | Qt::AlignLeft,
                   p.fontMetrics().elidedText(m_title, Qt::ElideMiddle, tr.width() - subW));
    }

    if (paneCount() == 2)
        p.fillRect(QRect(paneRect(0).width(), 0, kPaneGap, h), t->border());

    if (m_doc.lines.isEmpty()) {
        p.setFont(font());
        p.setPen(t->mutedText());
        const QString msg = !m_doc.message.isEmpty() ? m_doc.message
                            : !m_emptyMessage.isEmpty() ? m_emptyMessage
                            : m_title.isEmpty() ? tr("Select a file to view its changes.")
                                                : tr("No differences.");
        p.drawText(QRect(0, hh, w, h - hh), Qt::AlignCenter, msg);
        return;
    }

    const int first = verticalScrollBar()->value();
    const int top = linesTop();
    const int visible = (h - top) / m_lineHeight + 2;
    const int rows = rowCount();


    for (int pane = 0; pane < paneCount(); ++pane) {
        const QRect pr = paneRect(pane);
        const int mw = paneMargin(pane);

        // Margin background + separator
        p.setClipRect(pr);
        p.fillRect(QRect(pr.left(), pr.top(), mw, pr.height()), t->diffMarginBg());
        p.setPen(t->border());
        p.drawLine(pr.left() + mw - 1, pr.top(), pr.left() + mw - 1, pr.bottom());

        for (int k = 0; k < visible; ++k) {
            const int row = first + k;
            if (row >= rows)
                break;
            const int y = top + k * m_lineHeight;
            p.setClipRect(QRect(pr.left() + mw, pr.top(), qMax(0, pr.width() - mw), pr.height()));
            drawCell(p, pane, row, y, pr);
            p.setClipRect(QRect(pr.left(), pr.top(), qMin(mw, pr.width()), pr.height()));
            drawMargin(p, pane, row, y, pr);
        }
    }
}

DiffView::Pos DiffView::posAt(const QPoint &pt, int forcePane) const
{
    Pos pos;
    const int rows = rowCount();
    pos.pane = forcePane;
    if (pos.pane < 0) {
        pos.pane = 0;
        for (int pane = 0; pane < paneCount(); ++pane)
            if (pt.x() >= paneRect(pane).left())
                pos.pane = pane;
    }
    const QRect pr = paneRect(pos.pane);
    pos.row = qBound(0, verticalScrollBar()->value() + (pt.y() - pr.top()) / m_lineHeight, qMax(0, rows - 1));
    const qreal x0 = pr.left() + paneMargin(pos.pane) + ui::space(ui::gap::item)
        - horizontalScrollBar()->value() * m_charWidth;
    pos.col = qMax(0, qFloor((pt.x() - x0) / m_charWidth + 0.5));
    pos.col = qMin(pos.col, int(layoutAt(pos.pane, pos.row).text.size()));
    return pos;
}

void DiffView::mousePressEvent(QMouseEvent *e)
{
    setFocus();
    if (e->button() == Qt::LeftButton && onDivider(e->pos())) {
        m_resizingPanes = true;
        m_dragging = false;
        m_dividerDragOffset = e->pos().x() - paneRect(0).width();
        updateCursor(e->pos());
        e->accept();
        return;
    }
    if (e->button() == Qt::LeftButton && !m_doc.lines.isEmpty() && e->pos().y() >= headerHeight()) {
        m_selAnchor = m_selCursor = posAt(e->pos());
        m_dragging = true;
        viewport()->update();
    }
    QAbstractScrollArea::mousePressEvent(e);
}

void DiffView::mouseMoveEvent(QMouseEvent *e)
{
    if (m_resizingPanes && (e->buttons() & Qt::LeftButton)) {
        const PaneSplit split = paneSplitMetrics();
        setPaneSplit(qreal(split.clamp(e->pos().x() - m_dividerDragOffset)) / split.available);
        e->accept();
        return;
    }
    updateCursor(e->pos());
    if (m_dragging && (e->buttons() & Qt::LeftButton)) {
        m_selCursor = posAt(e->pos(), m_selAnchor.pane);
        if (e->pos().y() < headerHeight())
            verticalScrollBar()->setValue(verticalScrollBar()->value() - 1);
        else if (e->pos().y() > viewport()->height())
            verticalScrollBar()->setValue(verticalScrollBar()->value() + 1);
        viewport()->update();
    }
    QAbstractScrollArea::mouseMoveEvent(e);
}

void DiffView::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        const bool resizing = m_resizingPanes;
        m_resizingPanes = m_dragging = false;
        updateCursor(e->pos());
        if (resizing) {
            e->accept();
            return;
        }
    }
    QAbstractScrollArea::mouseReleaseEvent(e);
}

void DiffView::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && onDivider(e->pos())) {
        m_resizingPanes = m_dragging = false;
        setPaneSplit(0.5);
        updateCursor(e->pos());
        e->accept();
        return;
    }
    if (m_doc.lines.isEmpty() || e->pos().y() < headerHeight())
        return;
    const Pos pos = posAt(e->pos());
    const QString text = cellText(pos.pane, pos.row);
    int a = qMin(pos.col, int(text.size())), b = a;
    auto isWord = [](QChar c) { return c.isLetterOrNumber() || c == QLatin1Char('_'); };
    while (a > 0 && isWord(text.at(a - 1)))
        --a;
    while (b < text.size() && isWord(text.at(b)))
        ++b;
    m_selAnchor = {pos.pane, pos.row, a};
    m_selCursor = {pos.pane, pos.row, b};
    m_dragging = false;
    viewport()->update();
}

void DiffView::zoomBy(int step)
{
    const int base = OmarchyTheme::instance()->monoFont().pixelSize();
    const int zoom = qBound(kMinFontPx, base + m_zoom + step, kMaxFontPx) - base;
    if (zoom == m_zoom)
        return;
    m_zoom = zoom;
    refreshTheme();
}

void DiffView::wheelEvent(QWheelEvent *e)
{
    if (e->modifiers() & Qt::ControlModifier) {
        const int delta = e->angleDelta().y();
        if (delta != 0)
            zoomBy(delta > 0 ? 1 : -1);
        e->accept();
        return;
    }
    QAbstractScrollArea::wheelEvent(e);
}

void DiffView::keyPressEvent(QKeyEvent *e)
{
    QScrollBar *vs = verticalScrollBar();
    const int page = vs->pageStep();
    if (e->matches(QKeySequence::Copy)) {
        copySelection();
    } else if (e->matches(QKeySequence::SelectAll)) {
        selectAll();
    } else if (e->key() == Qt::Key_Down) {
        vs->setValue(vs->value() + 1);
    } else if (e->key() == Qt::Key_Up) {
        vs->setValue(vs->value() - 1);
    } else if (e->key() == Qt::Key_PageDown || e->key() == Qt::Key_Space) {
        vs->setValue(vs->value() + page);
    } else if (e->key() == Qt::Key_PageUp) {
        vs->setValue(vs->value() - page);
    } else if (e->key() == Qt::Key_Home && (e->modifiers() & Qt::ControlModifier)) {
        vs->setValue(0);
    } else if (e->key() == Qt::Key_End && (e->modifiers() & Qt::ControlModifier)) {
        vs->setValue(vs->maximum());
    } else if (e->key() == Qt::Key_Left) {
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - kColumnStep);
    } else if (e->key() == Qt::Key_Right) {
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() + kColumnStep);
    } else if (e->key() == Qt::Key_Escape) {
        m_selAnchor = m_selCursor = Pos();
        viewport()->update();
    } else {
        QAbstractScrollArea::keyPressEvent(e);
        return;
    }
    e->accept();
}

void DiffView::contextMenuEvent(QContextMenuEvent *e)
{
    TickMenu menu(this);
    QAction *copy = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-copy")), tr("Copy"), this, &DiffView::copySelection);
    copy->setEnabled(hasSelection());
    menu.addAction(QIcon::fromTheme(QStringLiteral("edit-select-all")), tr("Select all"), this, &DiffView::selectAll);
    menu.addSeparator();
    menu.addAction(tr("Next change"), this, &DiffView::nextChange);
    menu.addAction(tr("Previous change"), this, &DiffView::previousChange);
    menu.addSeparator();
    QAction *two = menu.addAction(tr("Two-pane view"));
    two->setCheckable(true);
    two->setChecked(m_mode == TwoPane);
    connect(two, &QAction::toggled, this, &DiffView::setTwoPane);
    QAction *ws = menu.addAction(tr("Show whitespace"));
    ws->setCheckable(true);
    ws->setChecked(m_showWhitespace);
    connect(ws, &QAction::toggled, this, &DiffView::setShowWhitespace);
    QAction *syntax = menu.addAction(tr("Syntax highlighting"));
    syntax->setCheckable(true);
    syntax->setChecked(m_syntax);
    connect(syntax, &QAction::toggled, this, &DiffView::setSyntaxHighlighting);
    menu.exec(e->globalPos());
}

QString DiffView::selectedText() const
{
    if (!hasSelection())
        return QString();
    Pos a = m_selAnchor, b = m_selCursor;
    if (b < a)
        std::swap(a, b);
    QStringList out;
    for (int row = a.row; row <= b.row; ++row) {
        if (lineAt(a.pane, row) < 0)
            continue; // filler rows have no text
        const QString text = cellText(a.pane, row);
        const int c0 = row == a.row ? qMin(a.col, int(text.size())) : 0;
        const int c1 = row == b.row ? qMin(b.col, int(text.size())) : int(text.size());
        out << text.mid(c0, c1 - c0);
    }
    return out.join(QLatin1Char('\n'));
}

void DiffView::copySelection()
{
    const QString text = selectedText();
    if (!text.isEmpty())
        QApplication::clipboard()->setText(text);
}

void DiffView::selectAll()
{
    const int last = rowCount() - 1;
    if (last < 0)
        return;
    const int pane = qBound(0, m_selAnchor.pane, paneCount() - 1);
    m_selAnchor = {pane, 0, 0};
    m_selCursor = {pane, last, int(layoutAt(pane, last).text.size())};
    viewport()->update();
}

void DiffView::scrollToRow(int row)
{
    const int visible = qMax(1, (viewport()->height() - linesTop()) / m_lineHeight);
    verticalScrollBar()->setValue(qMax(0, row - visible / 3));
}

void DiffView::goToBlock(int index)
{
    if (m_blockStarts.isEmpty())
        return;
    m_currentBlock = qBound(0, index, int(m_blockStarts.size()) - 1);
    scrollToRow(m_blockStarts[m_currentBlock]);
    emit changeIndexChanged(m_currentBlock, m_blockStarts.size());
    viewport()->update();
}

void DiffView::nextChange()
{
    goToBlock(m_currentBlock + 1);
}

void DiffView::previousChange()
{
    goToBlock(m_currentBlock <= 0 ? 0 : m_currentBlock - 1);
}

void DiffView::firstChange()
{
    goToBlock(0);
}
