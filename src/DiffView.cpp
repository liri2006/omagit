#include "DiffView.h"
#include "OmarchyTheme.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>

static constexpr int kIconSize = 16;
static constexpr int kHeaderPad = 6;
static constexpr int kPaneGap = 4; // separator between the two panes

DiffView::DiffView(QWidget *parent)
    : QAbstractScrollArea(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    viewport()->setCursor(Qt::IBeamCursor);
    refreshTheme();
    connect(verticalScrollBar(), &QScrollBar::valueChanged, viewport(), qOverload<>(&QWidget::update));
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, viewport(), qOverload<>(&QWidget::update));
}

void DiffView::refreshTheme()
{
    m_font = OmarchyTheme::instance()->monoFont();
    viewport()->setFont(m_font);
    updateMetrics();
    updateScrollBars();
    viewport()->update();
}

void DiffView::updateMetrics()
{
    const QFontMetrics fm(m_font);
    m_charWidth = qMax(1, fm.horizontalAdvance(QLatin1Char('M')));
    m_lineHeight = fm.height() + 2;
}

int DiffView::headerHeight() const
{
    return m_lineHeight + kHeaderPad * 2;
}

int DiffView::marginWidth() const
{
    return kIconSize + 6 + m_digits * m_charWidth + 10;
}

QRect DiffView::paneRect(int pane) const
{
    const int hh = headerHeight();
    const int w = viewport()->width();
    const int h = viewport()->height() - hh;
    if (paneCount() <= 1)
        return QRect(0, hh, w, h);
    const int half = (w - kPaneGap) / 2;
    if (pane == 0)
        return QRect(0, hh, half, h);
    return QRect(half + kPaneGap, hh, w - half - kPaneGap, h);
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
        int i = 0;
        while (i < n) {
            const DiffLine::State st = m_doc.lines[i].state;
            if (st == DiffLine::Removed || st == DiffLine::Added) {
                // A change block: removed lines then added lines. Pair them
                // row by row; whatever is left over gets a filler on the
                // other side, exactly like a classic two-pane diff view.
                int r0 = i;
                while (i < n && m_doc.lines[i].state == DiffLine::Removed)
                    ++i;
                int r1 = i;
                int a0 = i;
                while (i < n && m_doc.lines[i].state == DiffLine::Added)
                    ++i;
                int a1 = i;
                const int rows = qMax(r1 - r0, a1 - a0);
                m_blockStarts.append(left.size());
                for (int k = 0; k < rows; ++k) {
                    left.append(k < r1 - r0 ? r0 + k : -1);
                    right.append(k < a1 - a0 ? a0 + k : -1);
                }
            } else {
                left.append(i);
                right.append(i);
                ++i;
            }
        }
        m_panes.append(left);
        m_panes.append(right);
    }
}

void DiffView::setDocument(const DiffDocument &doc, const QString &title, const QString &subtitle,
                           const QString &leftLabel, const QString &rightLabel)
{
    m_doc = doc;
    m_title = title;
    m_subtitle = subtitle;
    m_leftLabel = leftLabel.isEmpty() ? tr("HEAD") : leftLabel;
    m_rightLabel = rightLabel.isEmpty() ? tr("Working Tree") : rightLabel;
    m_emptyMessage.clear();
    m_currentBlock = -1;
    m_selAnchor = m_selCursor = Pos();

    int maxNumber = 1;
    m_maxCols = 0;
    for (const DiffLine &l : m_doc.lines) {
        maxNumber = qMax(maxNumber, qMax(l.oldNumber, l.newNumber));
        m_maxCols = qMax(m_maxCols, int(expanded(l).size()));
    }
    m_digits = QString::number(maxNumber).size();

    rebuildLayout();
    updateScrollBars();
    verticalScrollBar()->setValue(0);
    horizontalScrollBar()->setValue(0);
    viewport()->update();
    emit changeIndexChanged(m_currentBlock, m_blockStarts.size());
}

void DiffView::clear(const QString &message)
{
    m_doc = DiffDocument();
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

void DiffView::setTabWidth(int spaces)
{
    m_tabWidth = qBound(1, spaces, 16);
    setDocument(m_doc, m_title, m_subtitle, m_leftLabel, m_rightLabel);
}

void DiffView::setShowWhitespace(bool on)
{
    m_showWhitespace = on;
    viewport()->update();
}

void DiffView::updateScrollBars()
{
    const int rows = m_panes.isEmpty() ? 0 : m_panes[0].size();
    const int visibleLines = qMax(1, (viewport()->height() - headerHeight()) / m_lineHeight);
    verticalScrollBar()->setRange(0, qMax(0, rows - visibleLines));
    verticalScrollBar()->setPageStep(visibleLines);
    verticalScrollBar()->setSingleStep(1);

    const int textWidth = paneRect(0).width() - marginWidth() - 8;
    const int visibleCols = qMax(1, textWidth / m_charWidth);
    horizontalScrollBar()->setRange(0, qMax(0, m_maxCols + 2 - visibleCols));
    horizontalScrollBar()->setPageStep(visibleCols);
    horizontalScrollBar()->setSingleStep(4);
}

void DiffView::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBars();
}

QString DiffView::expanded(const DiffLine &l) const
{
    if (!l.text.contains(QLatin1Char('\t')))
        return l.text;
    QString out;
    out.reserve(l.text.size() + 16);
    for (const QChar c : l.text) {
        if (c == QLatin1Char('\t')) {
            const int pad = m_tabWidth - (out.size() % m_tabWidth);
            out += QString(pad, QLatin1Char(' '));
        } else {
            out += c;
        }
    }
    return out;
}

QString DiffView::cellText(int pane, int row) const
{
    const int li = lineAt(pane, row);
    return li < 0 ? QString() : expanded(m_doc.lines[li]);
}

QVector<int> DiffView::columnMap(const QString &raw) const
{
    QVector<int> map(raw.size() + 1);
    int col = 0;
    for (int i = 0; i < raw.size(); ++i) {
        map[i] = col;
        if (raw.at(i) == QLatin1Char('\t'))
            col += m_tabWidth - (col % m_tabWidth);
        else
            ++col;
    }
    map[raw.size()] = col;
    return map;
}

void DiffView::drawMarginIcon(QPainter &p, const QRect &r, DiffLine::State state) const
{
    const OmarchyTheme *t = OmarchyTheme::instance();
    QColor c;
    if (state == DiffLine::Added)
        c = t->diffAddedIcon();
    else if (state == DiffLine::Removed)
        c = t->diffRemovedIcon();
    else
        return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF box = QRectF(r).adjusted(2, 2, -2, -2);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawRoundedRect(box, 3, 3);
    p.setPen(QPen(t->base(), 2));
    const QPointF cen = box.center();
    p.drawLine(QPointF(cen.x() - 3.5, cen.y()), QPointF(cen.x() + 3.5, cen.y()));
    if (state == DiffLine::Added)
        p.drawLine(QPointF(cen.x(), cen.y() - 3.5), QPointF(cen.x(), cen.y() + 3.5));
    p.restore();
}

void DiffView::drawMargin(QPainter &p, int pane, int row, int y, const QRect &pr)
{
    const OmarchyTheme *t = OmarchyTheme::instance();
    const int li = lineAt(pane, row);
    if (li < 0)
        return;
    const DiffLine &l = m_doc.lines[li];

    drawMarginIcon(p, QRect(pr.left() + 2, y + (m_lineHeight - kIconSize) / 2, kIconSize, kIconSize), l.state);

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
    const QRect numRect(pr.left() + kIconSize + 6, y, m_digits * m_charWidth, m_lineHeight);
    p.drawText(numRect, Qt::AlignVCenter | Qt::AlignRight, number);
}

void DiffView::drawCell(QPainter &p, int pane, int row, int y, const QRect &pr)
{
    const OmarchyTheme *t = OmarchyTheme::instance();
    const int textX = pr.left() + marginWidth();
    const int li = lineAt(pane, row);

    if (li < 0) {
        // Filler line: the other side has content here, this side does not.
        p.fillRect(QRect(textX, y, pr.right() - textX + 1, m_lineHeight), t->diffEmptyBg());
        return;
    }
    const DiffLine &l = m_doc.lines[li];

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
    case DiffLine::Empty:
        bg = t->diffEmptyBg();
        break;
    default:
        break;
    }
    p.fillRect(QRect(textX, y, pr.right() - textX + 1, m_lineHeight), bg);

    const QString text = expanded(l);
    const int hOff = horizontalScrollBar()->value();
    const int x0 = textX + 4 - hOff * m_charWidth;

    // Inline highlight
    if (!l.inline_.isEmpty()) {
        const QVector<int> map = columnMap(l.text);
        for (const DiffSpan &s : l.inline_) {
            const int c0 = map[qBound(0, s.start, int(l.text.size()))];
            const int c1 = map[qBound(0, s.start + s.length, int(l.text.size()))];
            if (c1 > c0)
                p.fillRect(QRect(x0 + c0 * m_charWidth, y, (c1 - c0) * m_charWidth, m_lineHeight), inlineBg);
        }
    }

    // Selection (lives in one pane)
    if (hasSelection() && m_selAnchor.pane == pane) {
        Pos a = m_selAnchor, b = m_selCursor;
        if (b < a)
            std::swap(a, b);
        if (row >= a.row && row <= b.row) {
            int c0 = row == a.row ? a.col : 0;
            int c1 = row == b.row ? b.col : text.size() + 1;
            c0 = qMin(c0, int(text.size()) + 1);
            c1 = qMin(c1, int(text.size()) + 1);
            if (c1 > c0) {
                QColor selc = t->accent();
                selc.setAlphaF(0.35);
                p.fillRect(QRect(x0 + c0 * m_charWidth, y, (c1 - c0) * m_charWidth, m_lineHeight), selc);
            }
        }
    }

    // Text
    p.setFont(m_font);
    p.setPen(l.state == DiffLine::Header ? t->mutedText() : t->text());
    const int firstCol = qMax(0, hOff - 1);
    const int visibleCols = (pr.right() - textX) / m_charWidth + 3;
    const int baseline = y + (m_lineHeight + p.fontMetrics().ascent() - p.fontMetrics().descent()) / 2;
    if (firstCol < text.size())
        p.drawText(x0 + firstCol * m_charWidth, baseline, text.mid(firstCol, visibleCols));

    if (m_showWhitespace) {
        p.save();
        p.setPen(t->mutedText());
        const int end = qMin(int(text.size()), firstCol + visibleCols);
        for (int c = firstCol; c < end; ++c)
            if (text.at(c) == QLatin1Char(' '))
                p.drawText(x0 + c * m_charWidth, baseline, QStringLiteral("·"));
        if (l.state != DiffLine::Header)
            p.drawText(x0 + text.size() * m_charWidth, baseline,
                       l.noNewline ? QStringLiteral("⌀") : QStringLiteral("¶"));
        p.restore();
    }
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
            const QRect tr(pr.left() + 10, 0, pr.width() - 20, hh);
            const QString label = pane == 0 ? m_leftLabel : m_rightLabel;
            p.setFont(font());
            p.setPen(t->mutedText());
            const int labelW = p.fontMetrics().horizontalAdvance(label) + 12;
            p.drawText(tr, Qt::AlignVCenter | Qt::AlignRight, label);
            p.setFont(bold);
            p.setPen(t->text());
            p.drawText(tr, Qt::AlignVCenter | Qt::AlignLeft,
                       p.fontMetrics().elidedText(m_title, Qt::ElideMiddle, tr.width() - labelW));
        }
    } else {
        const QRect tr(10, 0, w - 20, hh);
        p.setFont(font());
        p.setPen(t->mutedText());
        const int subW = m_subtitle.isEmpty() ? 0 : p.fontMetrics().horizontalAdvance(m_subtitle) + 12;
        if (!m_subtitle.isEmpty())
            p.drawText(tr, Qt::AlignVCenter | Qt::AlignRight, m_subtitle);
        p.setFont(bold);
        p.setPen(t->text());
        p.drawText(tr, Qt::AlignVCenter | Qt::AlignLeft,
                   p.fontMetrics().elidedText(m_title, Qt::ElideMiddle, tr.width() - subW));
    }

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
    const int visible = (h - hh) / m_lineHeight + 2;
    const int rows = m_panes[0].size();
    const int mw = marginWidth();

    for (int pane = 0; pane < paneCount(); ++pane) {
        const QRect pr = paneRect(pane);

        // Margin background + separator
        p.setClipRect(pr);
        p.fillRect(QRect(pr.left(), pr.top(), mw, pr.height()), t->diffMarginBg());
        p.setPen(t->border());
        p.drawLine(pr.left() + mw - 1, pr.top(), pr.left() + mw - 1, pr.bottom());

        for (int k = 0; k < visible; ++k) {
            const int row = first + k;
            if (row >= rows)
                break;
            const int y = hh + k * m_lineHeight;
            p.setClipRect(QRect(pr.left() + mw, pr.top(), pr.width() - mw, pr.height()));
            drawCell(p, pane, row, y, pr);
            p.setClipRect(QRect(pr.left(), pr.top(), mw, pr.height()));
            drawMargin(p, pane, row, y, pr);
        }
    }

    if (paneCount() == 2) {
        p.setClipping(false);
        const QRect l = paneRect(0);
        p.fillRect(QRect(l.right() + 1, 0, kPaneGap, h), t->border());
    }
}

DiffView::Pos DiffView::posAt(const QPoint &pt, int forcePane) const
{
    Pos pos;
    const int rows = m_panes.isEmpty() ? 0 : m_panes[0].size();
    pos.pane = forcePane;
    if (pos.pane < 0) {
        pos.pane = 0;
        for (int pane = 0; pane < paneCount(); ++pane)
            if (pt.x() >= paneRect(pane).left())
                pos.pane = pane;
    }
    const QRect pr = paneRect(pos.pane);
    pos.row = qBound(0, verticalScrollBar()->value() + (pt.y() - pr.top()) / m_lineHeight, qMax(0, rows - 1));
    const int x0 = pr.left() + marginWidth() + 4 - horizontalScrollBar()->value() * m_charWidth;
    pos.col = qMax(0, (pt.x() - x0 + m_charWidth / 2) / m_charWidth);
    pos.col = qMin(pos.col, int(cellText(pos.pane, pos.row).size()));
    return pos;
}

void DiffView::mousePressEvent(QMouseEvent *e)
{
    setFocus();
    if (e->button() == Qt::LeftButton && !m_doc.lines.isEmpty() && e->pos().y() >= headerHeight()) {
        m_selAnchor = m_selCursor = posAt(e->pos());
        m_dragging = true;
        viewport()->update();
    }
    QAbstractScrollArea::mousePressEvent(e);
}

void DiffView::mouseMoveEvent(QMouseEvent *e)
{
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

void DiffView::mouseDoubleClickEvent(QMouseEvent *e)
{
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

void DiffView::wheelEvent(QWheelEvent *e)
{
    if (e->modifiers() & Qt::ControlModifier) {
        const int delta = e->angleDelta().y();
        if (delta != 0) {
            m_font.setPointSize(qBound(6, m_font.pointSize() + (delta > 0 ? 1 : -1), 32));
            viewport()->setFont(m_font);
            updateMetrics();
            updateScrollBars();
            viewport()->update();
        }
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
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - 4);
    } else if (e->key() == Qt::Key_Right) {
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() + 4);
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
    QMenu menu(this);
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
    if (m_doc.lines.isEmpty())
        return;
    const int pane = m_selAnchor.pane;
    const int last = m_panes[pane].size() - 1;
    m_selAnchor = {pane, 0, 0};
    m_selCursor = {pane, last, int(cellText(pane, last).size())};
    viewport()->update();
}

void DiffView::scrollToRow(int row)
{
    const int visible = qMax(1, (viewport()->height() - headerHeight()) / m_lineHeight);
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
