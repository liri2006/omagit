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
// checked row's hint ends 4 short of the box).
int TickMenu::tickReserve()
{
    const OmarchyTheme *theme = OmarchyTheme::instance();
    if (theme->glyph(ui::kCheck).isEmpty())
        return 0;
    return ui::space(ui::box::icon) + ui::space(ui::gap::icon);
}

void TickMenu::paintEvent(QPaintEvent *event)
{
    QMenu::paintEvent(event);
    const OmarchyTheme *theme = OmarchyTheme::instance();
    const QString tick = theme->glyph(ui::kCheck);
    if (tick.isEmpty())
        return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setFont(theme->uiFont());
    p.setPen(theme->accent());
    const QRectF ink = ui::inkRect(p.font(), tick);
    const int side = ui::space(ui::box::icon);
    const QList<QAction *> all = actions();
    for (QAction *a : all) {
        if (!a->isCheckable() || !a->isChecked() || !a->isVisible() || qobject_cast<QWidgetAction *>(a))
            continue;
        const QRect r = actionGeometry(a);
        if (r.isNull() || !event->rect().intersects(r))
            continue;
        // In its 16 px box 8 from the row's right edge, centred by its ink.
        const QRectF box(r.right() + 1 - ui::space(ui::pad::control) - side, r.top(), side, r.height());
        p.drawText(box.center() - ink.center(), tick);
    }
}
