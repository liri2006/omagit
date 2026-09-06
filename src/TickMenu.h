#pragma once

#include <QMenu>

// A QMenu whose checked entries carry an accent-colored tick at the right
// edge instead of a checkbox on the left, so the names stay flush with the
// unchecked ones, and the checked entry's text takes the same accent color
// (see TickMenu::item:checked in OmarchyTheme). Everything else is a plain QMenu.
class TickMenu : public QMenu
{
    Q_OBJECT
public:
    explicit TickMenu(QWidget *parent = nullptr);

    // The room an item keeps free on the right for the tick (menus without
    // any checkable entry keep it too, so the widths match).
    static int tickReserve();

protected:
    void paintEvent(QPaintEvent *event) override;
};
