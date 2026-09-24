#include "RefChip.h"
#include "OmarchyTheme.h"
#include "UiHelpers.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QtMath>

namespace {
// kit.js refChip(): the label's advance and 10 px, 16 px tall; the tinted
// kinds are their hue at 14 % with a border of it at 80 %.
constexpr int kChipHeight = 16, kChipPad = 10;
constexpr qreal kTintFill = 0.14, kTintBorder = 0.8;

// The kit's chip label: the caption's bold 10 px, without the letter spacing
// of the section captions it is otherwise the font of.
QFont chipFont()
{
    QFont font = OmarchyTheme::instance()->captionFont();
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0);
    return font;
}

// The hue of a kind: the accent for the local branches (and HEAD), magenta
// for the remote ones, yellow for the tags.
QColor chipHue(const RefLabel &label)
{
    const OmarchyTheme *theme = OmarchyTheme::instance();
    switch (label.type) {
    case RefLabel::Remote: return theme->color(QStringLiteral("magenta"));
    case RefLabel::Tag: return theme->color(QStringLiteral("yellow"));
    case RefLabel::Branch:
    case RefLabel::DetachedHead: break;
    }
    return theme->accent();
}
} // namespace

int refChipWidth(const RefLabel &label)
{
    // Rounded up, so the label is never elided by a fraction of a pixel.
    return qCeil(QFontMetricsF(chipFont()).horizontalAdvance(label.name)) + ui::space(kChipPad);
}

int refChipHeight()
{
    return ui::space(kChipHeight);
}

void paintRefChip(QPainter *painter, const QPoint &topLeft, const RefLabel &label)
{
    const OmarchyTheme *theme = OmarchyTheme::instance();
    const QRect chip(topLeft, QSize(refChipWidth(label), refChipHeight()));
    const QColor hue = chipHue(label);
    // The checked-out branch and a detached HEAD are the solid chip; every
    // other one is its hue over the window, the way the design lays the kit's
    // alpha over the background.
    const bool solid = label.head || label.type == RefLabel::DetachedHead;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, false);
    if (solid) {
        painter->fillRect(chip, hue);
    } else {
        // The kit strokes the border over the fill, so the border is its
        // alpha over the tinted fill rather than over the window.
        const QColor fill = OmarchyTheme::mix(theme->window(), hue, kTintFill);
        painter->fillRect(chip, fill);
        painter->setPen(OmarchyTheme::mix(fill, hue, kTintBorder));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(chip.adjusted(0, 0, -1, -1));
    }
    painter->setFont(chipFont());
    painter->setPen(solid ? theme->window() : hue);
    painter->drawText(chip, Qt::AlignCenter, label.name);
    painter->restore();
}
