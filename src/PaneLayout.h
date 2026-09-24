#pragma once

#include <QCoreApplication>
#include <QString>

// What stands next to the diff pane: the left section (commit dialog or
// history) or a narrow rail of file miniatures. Whether the diff pane shows
// at all is a separate toggle (MainWindow::setDiffPaneVisible).
enum class PaneLayout { Docked, Mini };

constexpr int kPaneLayoutCount = 2;

// The window's width class (screens.js levelFor(), in design pixels): from
// 1400 up, from 1000, from 700, and the stacked widths below that. The
// window decides it; the commit page and the history size their parts by it.
enum class WidthClass { Wide, Large, Medium, Stacked };

inline QString paneLayoutName(PaneLayout l)
{
    switch (l) {
    case PaneLayout::Docked: return QCoreApplication::translate("PaneLayout", "Docked");
    case PaneLayout::Mini: return QCoreApplication::translate("PaneLayout", "Mini");
    }
    return QString();
}

inline QString paneLayoutTip(PaneLayout l)
{
    switch (l) {
    case PaneLayout::Docked:
        return QCoreApplication::translate("PaneLayout", "Docked — the left section next to the diff pane; click for the Mini rail (Ctrl+B)");
    case PaneLayout::Mini:
        return QCoreApplication::translate("PaneLayout", "Mini — a narrow rail of file miniatures, the diff pane fills the window; click for the Docked section (Ctrl+B)");
    }
    return QString();
}

// Settings key <-> enum
inline QString paneLayoutKey(PaneLayout l)
{
    switch (l) {
    case PaneLayout::Docked: return QStringLiteral("docked");
    case PaneLayout::Mini: return QStringLiteral("mini");
    }
    return QString();
}

inline PaneLayout paneLayoutFromKey(const QString &key, PaneLayout fallback = PaneLayout::Docked)
{
    if (key == QLatin1String("mini"))
        return PaneLayout::Mini;
    if (key == QLatin1String("docked"))
        return PaneLayout::Docked;
    return fallback; // includes the pre-0.4 "full", which is now Docked with the diff pane hidden
}
