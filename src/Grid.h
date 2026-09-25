#pragma once

#include "PaneLayout.h"

// The design's grid (design/figma-gen/kit.js: BOX, PAD, GAP, DENSITY, BAR;
// design/README.md "The grid"), one to one, in design pixels at a 12 px base
// font: every one of them reaches the screen through ui::space(), whose unit
// is 4 of them scaled once, so sums of them stay on the grid at any text size.
// Hairlines and borders are 1 px and drawn inside the box they belong to,
// never added to it; text sizes are not grid values (ui::fontPx()).
namespace ui {

// Sizes.
namespace box {
constexpr int control = 28; // buttons, fields, segmented controls
constexpr int row = 24;     // every clickable row (files, commits, menus, models), table and section headers
constexpr int line = 16;    // a line of text: captions, code, message and body copy
constexpr int icon = 16;    // an icon's box; the glyph fills 7/8 of it
constexpr int chevron = 12;
constexpr int check = 16;   // a checkbox
constexpr int pill = 16;    // status pills, ref chips, counts inside a control
constexpr int badge = 12;   // counts hanging off a button's corner
constexpr int tile = 40;    // the Mini rail's tiles
constexpr int footer = 28;
constexpr int divider = 16; // a vertical divider between groups in a row
} // namespace box

// Padding inside an element: it follows the element's size and is the same in
// every window.
namespace pad {
constexpr int pill = 4;     // 16 px elements: pills, chips (2 in the 12 px badge)
constexpr int control = 8;  // 24–28 px elements: buttons, fields, rows, cells, menu items
constexpr int primary = 16; // the primary action of a surface (Commit, Merge, Generate now)
constexpr int big = 12;     // 36 px controls (the merge dialog's branch pickers)
constexpr int menu = 4;     // around a menu's rows, its frame included
constexpr int popover = 12; // popovers and cards
constexpr int dialog = 16;
} // namespace pad

// Gaps inside a group, the same in every window.
namespace gap {
constexpr int icon = 4;       // an icon box (or chevron) to its label
constexpr int cluster = 4;    // bordered controls acting as one: Prev|Next, toggles, repo·branch, tiles, chips
constexpr int check = 8;      // a checkbox to its label
constexpr int item = 8;       // separate controls in a row, a control to its text
constexpr int group = 16;     // groups in a row; a divider stands at the start of its right half (8 | 8)
constexpr int caption = 4;    // a caption line (popovers, dialogs) to its content
constexpr int header = 8;     // a 24 px section header row to its content
constexpr int controlRow = 4; // a 28 px control row (diff toolbar, history filter) to its content
} // namespace gap

// Chrome bars: 8 + 28 + 8, the top bar and the action row alike; the body
// stands 8 from the top bar's hairline and from the footer's.
constexpr int kBar = 8;

// The spacing between things, which steps with the window (kit.js DENSITY,
// screens.js density()): the side margins and the pane gaps by its width,
// the block gap between the parts stacked in a pane by its height.
struct Density {
    int margin; // design px
    int block;  // design px
    bool operator==(const Density &other) const { return margin == other.margin && block == other.block; }
    bool operator!=(const Density &other) const { return !(*this == other); }
};

// Wide 16, Large and Medium 12, Stacked 8; Shallow 4, Normal 8, Tall 12.
constexpr Density densityFor(WidthClass width, HeightClass height)
{
    const int margin = width == WidthClass::Wide ? 16 : width == WidthClass::Stacked ? 8 : 12;
    const int block = height == HeightClass::Tall ? 12 : height == HeightClass::Shallow ? 4 : 8;
    return {margin, block};
}

// A widget outside the window's classes (a dialog on its own, a test's
// widget) keeps the regular density.
constexpr Density kRegularDensity = densityFor(WidthClass::Large, HeightClass::Normal);

} // namespace ui
