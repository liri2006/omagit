#include "Segmented.h"
#include "OmarchyTheme.h"
#include "UiHelpers.h"

#include <QFontMetrics>
#include <QPainter>
#include <QResizeEvent>

using namespace ui;

namespace {
// Every segment's box starts with a line the strip draws, the frame's left
// edge or the divider before it (kit.js segmented(): the lines are drawn
// inside the boxes): the segment widget begins one pixel after it, so the
// design's distances from the box's edge are one less from the widget's.
constexpr int kLeadingLine = 1;
} // namespace

// ---------------------------------------------------------------- segment

SegmentButton::SegmentButton()
{
    setAttribute(Qt::WA_Hover);
}

void SegmentButton::setGlyph(uint code, const QString &fallback)
{
    m_glyph = code;
    m_fallback = fallback;
    refreshGlyph();
}

void SegmentButton::refreshGlyph()
{
    m_glyphText = ui::icon(m_glyph, m_fallback).trimmed(); // QAbstractButton has an icon() of its own
    updateGeometry();
    update();
}

void SegmentButton::setLabelled(bool on)
{
    if (m_labelled == on)
        return;
    m_labelled = on;
    updateGeometry();
    update();
}

void SegmentButton::setCount(int count)
{
    count = qMax(0, count);
    if (m_count == count)
        return;
    m_count = count;
    updateGeometry();
    update();
}

void SegmentButton::setCentred(bool on)
{
    if (m_centred == on)
        return;
    m_centred = on;
    update();
}

// The segment's box, the lines the strip draws included (kit.js segmented():
// [8][glyph 16][4][label][4][count pill][8], 28 high).
QSize SegmentButton::sizeHint() const
{
    return QSize(2 * space(pad::control) + contentWidth(), space(box::control));
}

void SegmentButton::paintEvent(QPaintEvent *)
{
    const OmarchyTheme *t = OmarchyTheme::instance();
    const bool selected = isChecked();
    QPainter p(this);
    // Selected stays selected under the pointer: the segment says where you are.
    p.fillRect(rect(), selected ? t->selectedFill() : (underMouse() ? t->hoverFill() : t->normalFill()));

    const QColor fg = selected ? t->accent() : t->text();
    const QFont plain = t->uiFont();
    QFont label = plain;
    label.setBold(selected);
    p.setPen(fg);

    // From the box's left edge, which is the line before the widget: 8 in,
    // or centred the way kit.js rounds it, Math.round((w - content) / 2), in
    // the slot the strip gave the segment (the widget and its lines).
    const int slot = width() + kLeadingLine + (m_lastInStrip ? 1 : 0);
    int x = (m_centred ? qRound((slot - contentWidth()) / 2.0) : space(pad::control)) - kLeadingLine;
    {
        p.setFont(plain);
        const int w = glyphBox();
        // Centred by its ink, as the kit's buttons do: the History clock's
        // ink sits right of its advance, so centring the advance showed it
        // 2.5 px off. Nothing clips it: the box is the room the glyph takes
        // in the row, not a crop of it.
        p.drawText(QRectF(x, 0, w, height()).center() - inkRect(plain, m_glyphText).center(), m_glyphText);
        x += w;
    }
    if (m_labelled && !text().isEmpty()) {
        p.setFont(label);
        x += space(gap::icon);
        const int w = QFontMetrics(label).horizontalAdvance(text());
        p.drawText(QRect(x, 0, w, height()), Qt::AlignVCenter | Qt::AlignLeft, text());
        x += w;
    }
    if (m_count > 0) {
        x += space(gap::icon);
        const int w = pillWidth(), h = space(box::pill);
        const QRect pill(x, (height() - h) / 2, w, h);
        p.setPen(Qt::NoPen);
        p.setBrush(selected ? t->accent() : t->fill(0.14));
        p.drawRect(pill);
        p.setFont(pillFont());
        p.setPen(selected ? t->window() : t->text());
        p.drawText(pill, Qt::AlignCenter, countText());
    }
}

QString SegmentButton::countText() const
{
    return m_count > 999 ? QStringLiteral("999+") : QString::number(m_count);
}

// The room the glyph gets. A Nerd Font glyph's ink hangs over the advance
// its font metrics report, so the box is the design's 16 px icon box at the
// least, and the paint is never clipped to it.
int SegmentButton::glyphBox() const
{
    return qMax(QFontMetrics(OmarchyTheme::instance()->uiFont()).horizontalAdvance(m_glyphText), space(box::icon));
}

// The bold 10 px caption, without the section captions' letter spacing
// (kit.js segmented(): the count's text).
QFont SegmentButton::pillFont() const
{
    QFont f = OmarchyTheme::instance()->captionFont();
    f.setBold(true);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 0);
    return f;
}

// kit.js pillWidth(): 16 high and at least as wide, 4 either side of the digits.
int SegmentButton::pillWidth() const
{
    return qMax(space(box::pill), QFontMetrics(pillFont()).horizontalAdvance(countText()) + 2 * space(pad::pill));
}

// The label is measured bold, the weight it wears while selected, so the
// strip does not change width when the selection moves between segments.
int SegmentButton::contentWidth() const
{
    const OmarchyTheme *t = OmarchyTheme::instance();
    QFont bold = t->uiFont();
    bold.setBold(true);
    int w = glyphBox();
    if (m_labelled && !text().isEmpty())
        w += space(gap::icon) + QFontMetrics(bold).horizontalAdvance(text());
    if (m_count > 0)
        w += space(gap::icon) + pillWidth();
    return w;
}

// ---------------------------------------------------------------- strip

SegmentStrip::SegmentStrip(const QList<SegmentButton *> &segments, QWidget *parent)
    : QWidget(parent), m_segments(segments)
{
    for (SegmentButton *s : std::as_const(m_segments))
        s->setParent(this);
}

void SegmentStrip::setStretch(bool on)
{
    m_stretch = on;
    for (SegmentButton *s : std::as_const(m_segments))
        s->setCentred(on);
    updateGeometry();
    if (isVisible())
        layoutSegments();
}

void SegmentStrip::setSegmentVisible(SegmentButton *segment, bool on)
{
    if (!m_segments.contains(segment))
        return;
    segment->setVisible(on);
    updateGeometry();
    layoutSegments();
    update();
}

QList<SegmentButton *> SegmentStrip::participating() const
{
    QList<SegmentButton *> out;
    for (SegmentButton *s : m_segments)
        if (!s->isHidden())
            out << s;
    return out;
}

// The segments' boxes side by side: the frame and the dividers are drawn
// inside them, never added to them.
QSize SegmentStrip::sizeHint() const
{
    const QList<SegmentButton *> shown = participating();
    int width = 0, height = 0;
    for (const SegmentButton *s : shown) {
        const QSize hint = s->sizeHint();
        width += hint.width();
        height = qMax(height, hint.height());
    }
    return QSize(width, height);
}

void SegmentStrip::resizeEvent(QResizeEvent *)
{
    layoutSegments();
}

void SegmentStrip::layoutSegments()
{
    const QList<SegmentButton *> shown = participating();
    const int n = int(shown.size());
    if (n == 0)
        return;
    // A box per segment, its leading line (the frame's, or the divider before
    // it) its first column and, for the last, the frame's right edge its
    // last; the widget is the box less those lines, and the frame's top and
    // bottom rows. Stretched, the boxes are floor(width / n) each and the
    // last takes the remainder (kit.js segmented({stretch: true})); otherwise
    // each is as wide as its hint and the last takes whatever is left.
    const int h = qMax(0, height() - 2);
    const int each = width() / n;
    int x = 0;
    for (int i = 0; i < n; ++i) {
        SegmentButton *segment = shown.at(i);
        const bool last = i == n - 1;
        const int box = last ? width() - x : m_stretch ? each : qBound(0, segment->sizeHint().width(), width() - x);
        segment->setLastInStrip(last);
        segment->setGeometry(x + 1, 1, qMax(0, box - 1 - (last ? 1 : 0)), h);
        x += box;
    }
}

void SegmentStrip::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setPen(QPen(OmarchyTheme::instance()->normalBorder(), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(rect().adjusted(0, 0, -1, -1));
    const QList<SegmentButton *> shown = participating();
    for (int i = 1; i < shown.size(); ++i) {
        const int x = shown.at(i)->x() - 1;
        p.drawLine(x, 1, x, height() - 2);
    }
}
