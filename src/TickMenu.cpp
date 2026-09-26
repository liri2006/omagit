#include "TickMenu.h"

#include "OmarchyTheme.h"
#include "UiHelpers.h"

#include <QAction>
#include <QFontMetrics>
#include <QPaintEvent>
#include <QPainter>
#include <QWidgetAction>

TickMenu::TickMenu(QWidget *parent)
    : QMenu(parent)
{
}

// The tick's 16 px box and the 4 px before it (screens.js menuCard(): a
// checked row's hint ends 4 short of the box), kept whether or not the font
// has the tick: a submenu's chevron is there either way.
int TickMenu::tickReserve()
{
    return ui::space(ui::box::icon) + ui::space(ui::gap::icon);
}

// A submenu's entry carries a chevron in that same box, dim, or the accent
// while its row is the current one. Without the tick in the font the
// accent text alone marks a checked entry; the chevron has a fallback.
void TickMenu::paintEvent(QPaintEvent *event)
{
    QMenu::paintEvent(event);
    const OmarchyTheme *theme = OmarchyTheme::instance();
    const QString tick = theme->glyph(ui::kCheck);
    QString chevron = theme->glyph(ui::kChevronRight);
    if (chevron.isEmpty())
        chevron = QStringLiteral("›");
    // The chevron as every other one is drawn: at 12/16 of the glyphs' size.
    const QFont font = theme->uiFont();
    QFont small = font;
    small.setPixelSize(qMax(1, qRound(small.pixelSize() * ui::box::chevron / double(ui::box::icon))));
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF tickInk = tick.isEmpty() ? QRectF() : ui::inkRect(font, tick);
    const QRectF chevronInk = ui::inkRect(small, chevron);
    const int side = ui::space(ui::box::icon);
    const QList<QAction *> all = actions();
    for (QAction *a : all) {
        if (!a->isVisible() || qobject_cast<QWidgetAction *>(a))
            continue;
        const bool ticked = a->isCheckable() && a->isChecked() && !tick.isEmpty();
        if (!ticked && !a->menu())
            continue;
        const QRect r = actionGeometry(a);
        if (r.isNull() || !event->rect().intersects(r))
            continue;
        // In its 16 px box 8 from the row's right edge, centred by its ink.
        const QRectF box(r.right() + 1 - ui::space(ui::pad::control) - side, r.top(), side, r.height());
        if (ticked) {
            p.setFont(font);
            p.setPen(theme->accent());
            p.drawText(box.center() - tickInk.center(), tick);
        } else {
            p.setFont(small);
            p.setPen(a == activeAction() ? theme->accent() : theme->mutedText());
            p.drawText(box.center() - chevronInk.center(), chevron);
        }
    }
}
