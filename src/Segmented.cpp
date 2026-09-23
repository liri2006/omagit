#include "Segmented.h"
#include "OmarchyTheme.h"
#include "UiHelpers.h"

#include <QFontMetrics>
#include <QPainter>
#include <QResizeEvent>

using namespace ui;

namespace {
// The design's distances (design/figma-gen/kit.js segmented()), in 12 px-base
// pixels: every one of them goes through space().
constexpr int kSegmentPad = 12;   // inside a segment, left and right
constexpr int kSegmentGap = 6;    // between its glyph, its label and its pill
constexpr int kSegmentGlyph = 14; // the least room a glyph gets
constexpr int kPillHeight = 14, kPillPad = 4;
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

QSize SegmentButton::sizeHint() const
{
    const QFontMetrics fm(OmarchyTheme::instance()->uiFont());
    // The height a text button of the row comes to: the stylesheet's 5 px
    // of padding and its 1 px border above and below the text. The bar
    // measures the buttons themselves and places the strip on their
    // height, so this is only what the segments ask for on their own.
    return QSize(2 * space(kSegmentPad) + contentWidth(), fm.height() + 2 * (5 + 1));
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

    // Centred the way kit.js rounds it: Math.round((w - content) / 2).
    int x = m_centred ? qRound((width() - contentWidth()) / 2.0) : space(kSegmentPad);
    {
        p.setFont(plain);
        const int w = glyphBox();
        // TextDontClip: the box is the room the glyph takes in the row, not
        // a crop of it.
        p.drawText(QRect(x, 0, w, height()), Qt::AlignCenter | Qt::TextDontClip, m_glyphText);
        x += w;
    }
    if (m_labelled && !text().isEmpty()) {
        p.setFont(label);
        x += space(kSegmentGap);
        const int w = QFontMetrics(label).horizontalAdvance(text());
        p.drawText(QRect(x, 0, w, height()), Qt::AlignVCenter | Qt::AlignLeft, text());
        x += w;
    }
    if (m_count > 0) {
        x += space(kSegmentGap);
        const int w = pillWidth(), h = space(kPillHeight);
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
// its font metrics report — the History clock is half a pixel column wider
// on either side — so the box is the design's icon width at the least, and
// the paint is never clipped to it.
int SegmentButton::glyphBox() const
{
    return qMax(QFontMetrics(OmarchyTheme::instance()->uiFont()).horizontalAdvance(m_glyphText), space(kSegmentGlyph));
}

QFont SegmentButton::pillFont() const
{
    QFont f = OmarchyTheme::instance()->captionFont();
    f.setBold(true);
    return f;
}

int SegmentButton::pillWidth() const
{
    return qMax(space(kPillHeight), QFontMetrics(pillFont()).horizontalAdvance(countText()) + 2 * space(kPillPad));
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
        w += space(kSegmentGap) + QFontMetrics(bold).horizontalAdvance(text());
    if (m_count > 0)
        w += space(kSegmentGap) + pillWidth();
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

QSize SegmentStrip::sizeHint() const
{
    const QList<SegmentButton *> shown = participating();
    int width = 0, height = 0;
    for (const SegmentButton *s : shown) {
        const QSize hint = s->sizeHint();
        width += hint.width();
        height = qMax(height, hint.height());
    }
    return QSize(width + int(shown.size()) + 1, height + 2);
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
    const int h = qMax(0, height() - 2);
    if (m_stretch) {
        // A slot of floor(width / n) per segment, its leading line included:
        // the design's segments share their lines with the frame.
        const int each = width() / n;
        for (int i = 0; i < n; ++i) {
            const int x = i * each + 1;
            const int w = i == n - 1 ? width() - 1 - x : each - 1;
            shown.at(i)->setGeometry(x, 1, qMax(0, w), h);
        }
        return;
    }
    int x = 1;
    for (int i = 0; i < n; ++i) {
        // Room left for this one and the lines after it.
        const int room = qMax(0, width() - x - (n - i));
        const int w = i == n - 1 ? room : qBound(0, shown.at(i)->sizeHint().width(), room);
        shown.at(i)->setGeometry(x, 1, w, h);
        x += w + 1;
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
