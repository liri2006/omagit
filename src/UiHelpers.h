#pragma once

#include <QString>
#include <QStringList>
#include <QToolButton>

#include <functional>

class QAction;
class QDateTime;
class QFont;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QMenu;
class QTableView;
class QWidget;

// The small widget helpers the window's sections share: the Nerd Font glyphs
// of the shell, the label factories and the borderless buttons of its chrome.
namespace ui {

// The shell's spacing scale: the design's pixel values are meant for a 12 px
// base font and grow with it (Style.space() in omarchy-shell).
int space(int px);

// The design's grid for a section: a 24 px header row, its content 6 px below
// it, and 16 px to the next section.
int headerRowHeight();
int headerGap();
int sectionGap();

// Nerd Font (Material Design) glyphs used by the shell; empty if the font lacks them.
QString icon(uint cp, const QString &fallback = QString());
constexpr uint kRefresh = 0xF0450, kArrowUp = 0xF005D, kArrowDown = 0xF0045, kCommit = 0xF0718,
               kBranch = 0xF062C, kSplit = 0xF0BCC, kPilcrow = 0xF06D8, kHistory = 0xF02DA;
// md-code-tags: the syntax colouring toggle beside the whitespace one.
constexpr uint kCodeTags = 0xF0174;
// md-cloud_download, md-tray_arrow_down, md-tray_arrow_up, md-dock_right, md-source_merge
constexpr uint kFetch = 0xF0162, kPull = 0xF0120, kPush = 0xF011D, kDockRight = 0xF10AB, kMerge = 0xF062D;
// md-chevron_down, md-folder, md-folder_open
constexpr uint kChevron = 0xF0140, kFolder = 0xF024B, kFolderOpen = 0xF0770, kMagnify = 0xF0349;
// md-chevron_right and md-folder_outline: the collapsed branch and the folder
// of a directory row in the tree presentation of the changes list.
constexpr uint kChevronRight = 0xF0142, kFolderOutline = 0xF0256;
// md-format_list_bulleted, md-file_tree, md-table: the three files-view
// buttons at the right of the CHANGES row.
constexpr uint kFormatListBulleted = 0xF0279, kFileTree = 0xF0645, kTable = 0xF04EB;
// md-creation (the sparkle of "generate"), md-cog, md-robot
constexpr uint kSparkle = 0xF0674, kCog = 0xF0493, kRobot = 0xF06A9;
// md-information: the keybindings button in the footer.
constexpr uint kInfo = 0xF02FC;
// md-eye: the unversioned files are shown (the button is on) or hidden.
constexpr uint kEye = 0xF0208;
// md-check: the tick of a chosen entry (TickMenu, the agent popover's model
// rows); md-content_copy: copies a command to the clipboard.
constexpr uint kCheck = 0xF012C, kContentCopy = 0xF018F;
// md-file_compare: the stacked layout's Diff tab; md-undo: amending, in the
// action bar's options menu; md-dots_horizontal: the more and options menus.
constexpr uint kDiff = 0xF08AA, kUndo = 0xF054C, kDotsHorizontal = 0xF01D8;

// The frames of the generate button while an agent thinks: a braille spinner
// when the font has one, a turning circle otherwise.
QStringList spinnerFrames(const QFont &font);

// The "this opens a list" mark at the end of a dropdown button's text.
QString chevron();

// "/home/me/Projects/x" → "~/Projects/x"
QString tildePath(const QString &path);

QString ago(const QDateTime &when);

QLabel *sectionLabel(const QString &text);

QLabel *dimLabel(const QString &text = QString());

template <typename Button = QToolButton>
Button *toolButton(const QString &text, const QString &tip = QString())
{
    auto *b = new Button;
    b->setText(text);
    b->setToolButtonStyle(Qt::ToolButtonTextOnly);
    b->setToolTip(tip);
    b->setCursor(Qt::PointingHandCursor);
    b->setFocusPolicy(Qt::NoFocus);
    return b;
}

// Icon-only buttons: Inline is a 24 px square for a section header row, Toolbar
// is 28 px wide and exactly as tall as a text button of the row it sits in.
enum class IconButtonSize { Inline = 24, Toolbar = 28 };

// A centred glyph in a square that follows the text size. Ghost ones carry no
// chrome until the pointer is on them; the others look like any other button.
QToolButton *iconButton(uint glyph, const QString &fallback, const QString &tip,
                        IconButtonSize size = IconButtonSize::Inline, bool ghost = true);

// A text button of a folding row wearing its glyph alone: the stylesheet
// drops its side padding by the `iconForm` property and the button takes the
// design's 28 px square sideways; switched off, its width is free again. The
// caller swaps the text. Only a button that really changes form is
// repolished, so a row may apply its form on every resize.
void setIconForm(QToolButton *button, bool on);

// The search prompt of a popup, in the Omarchy menu look: no box of its own —
// the popup's accent frame is the focus cue — with a magnifier that stays put
// while typing. promptBox() is the row it lives in, hairline included, ready
// for a QWidgetAction.
QLineEdit *promptField(const QString &placeholder);
QWidget *promptBox(QLineEdit *field);

// A section's header row: the label, and whatever the caller adds after a
// stretch, on one 24 px line.
QHBoxLayout *sectionHeaderRow(QLabel *label);

// A borderless button that reads like a label and drops a menu down on click.
QToolButton *dropdownButton(const QString &objectName);

// A 1 px separator line that follows the theme by itself, so the sections
// that hold one do not each have to re-colour it. A vertical one keeps its
// height free for whoever lays it out.
QWidget *hairline(Qt::Orientation orientation = Qt::Horizontal);

// A dim caption inside a menu, like the section labels of the dialog.
QAction *addMenuHeader(QMenu *menu, const QString &text);

// A button's drop-down menu that stays inside the button's window: where it
// would run past the window's right edge it hangs from the button's right
// edge instead, and where it would run past the bottom it opens upwards —
// what Qt does at the edges of the screen, at the edges of the tile the user
// reads it against.
void keepMenuInWindow(QMenu *menu, QWidget *button);

// The shared geometry of the window's tables: one row of a file or commit
// list, and the narrowest the column taking up the leftover width may get —
// below that the table scrolls sideways instead of squeezing it further.
int tableRowHeight();
constexpr int kMinStretchColumn = 240;

// Gives `column` whatever the other columns, `others` px wide together, leave.
void fitStretchColumn(QTableView *table, int column, int others);

// Runs `fit` on every resize of the table's horizontal header.
void onHeaderResize(QTableView *table, std::function<void()> fit);

} // namespace ui
