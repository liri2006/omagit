#pragma once

#include <QCoreApplication>
#include <QString>

// What stands next to the diff pane: the left section (commit dialog or
// history) or a narrow rail of file miniatures. Whether the diff pane shows
// at all is a separate toggle (MainWindow::setDiffPaneVisible).
enum class PaneLayout { Docked, Mini };

constexpr int kPaneLayoutCount = 2;

// Nerd Font (Material Design) glyphs: md-dock_left, md-view_sequential.
inline uint paneLayoutGlyph(PaneLayout l)
{
    switch (l) {
    case PaneLayout::Docked: return 0xF10AA;
    case PaneLayout::Mini: return 0xF0729;
    }
    return 0;
}

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
