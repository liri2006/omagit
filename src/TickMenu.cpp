#include "TickMenu.h"

#include "OmarchyTheme.h"
#include "UiHelpers.h"

#include <QAction>
#include <QFontMetrics>
#include <QPaintEvent>
#include <QPainter>
#include <QWidgetAction>

namespace {
constexpr int kEdge = 14; // matches the item's left padding
} // namespace

TickMenu::TickMenu(QWidget *parent)
    : QMenu(parent)
{
}

int TickMenu::tickReserve()
{
    const OmarchyTheme *theme = OmarchyTheme::instance();
    const QString g = theme->glyph(ui::kCheck);
    if (g.isEmpty())
        return 0;
    return QFontMetrics(theme->uiFont()).horizontalAdvance(g) + 10;
}

void TickMenu::paintEvent(QPaintEvent *event)
{
    QMenu::paintEvent(event);
    const OmarchyTheme *theme = OmarchyTheme::instance();
    const QString tick = theme->glyph(ui::kCheck);
    if (tick.isEmpty())
        return;
    QPainter p(this);
    p.setFont(theme->uiFont());
    p.setPen(theme->accent());
    const QFontMetrics fm(p.font());
    const int w = fm.horizontalAdvance(tick);
    const QList<QAction *> all = actions();
    for (QAction *a : all) {
        if (!a->isCheckable() || !a->isChecked() || !a->isVisible() || qobject_cast<QWidgetAction *>(a))
            continue;
        const QRect r = actionGeometry(a);
        if (r.isNull() || !event->rect().intersects(r))
            continue;
        p.drawText(QRect(r.right() - kEdge - w + 1, r.top(), w, r.height()), Qt::AlignVCenter | Qt::AlignLeft, tick);
    }
}
