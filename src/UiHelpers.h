#pragma once

#include <QLabel>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QToolButton>

#include <functional>

class QAction;
class QDateTime;
class QFont;
class QHBoxLayout;
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
// The room between a row of buttons and the line under it, and on from that
// line to the next row: the top bar's row to its hairline, the hairline to
// the body, the diff pane's toolbar to the diff. 5 px, one value, so the
// window's rows keep one rhythm.
int barGap();
// A text button's height, the kit's 28 px: the height of a row of them.
int buttonHeight();

// The window's margin (screens.js screen(): m = 12): the body, the top bar's
// row and the footer's keep it at either side, and the overlays stay inside
// it all round.
int windowMargin();

// Nerd Font (Material Design) glyphs used by the shell; empty if the font lacks them.
QString icon(uint cp, const QString &fallback = QString());
constexpr uint kRefresh = 0xF0450, kArrowUp = 0xF005D, kArrowDown = 0xF0045, kCommit = 0xF0718,
               kBranch = 0xF062C, kSplit = 0xF0BCC, kPilcrow = 0xF06D8, kHistory = 0xF02DA;
// md-code-tags: the syntax colouring toggle beside the whitespace one.
constexpr uint kCodeTags = 0xF0174;
// md-cloud_download_outline, md-download, md-upload, md-dock_right, md-source_merge
constexpr uint kFetch = 0xF0B7D, kPull = 0xF01DA, kPush = 0xF0552, kDockRight = 0xF10AB, kMerge = 0xF062D;
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
// md-information: the merge view's informational verdict.
constexpr uint kInfo = 0xF02FC;
// md-keyboard_outline: the keybindings, in the footer and in the menus.
constexpr uint kKeyboard = 0xF097B;
// md-view_compact_outline: the Mini layout toggle of the top bar.
constexpr uint kViewCompact = 0xF0E6C;
// md-view_sequential_outline: the diff's unified view, beside kSplit's split one.
constexpr uint kUnified = 0xF148F;
// md-minus and md-plus: the removed and added lines in the diff's gutter.
constexpr uint kMinus = 0xF0374, kPlus = 0xF0415;
// md-cloud_outline: a remote branch in the branch menu.
constexpr uint kCloudOutline = 0xF0163;
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

// The ink of `text` in `font`, from its baseline origin, to centre a glyph by:
// the outline's exact bounds. QFontMetricsF::tightBoundingRect() rounds out
// to whole pixels and misses badly on thin glyphs (the "…" dots came out
// 1.3 px high), and a Nerd Font glyph's ink overhangs its advance anyway.
QRectF inkRect(const QFont &font, const QString &text);

// "/home/me/Projects/x" → "~/Projects/x"
QString tildePath(const QString &path);

QString ago(const QDateTime &when);

QLabel *sectionLabel(const QString &text);

QLabel *dimLabel(const QString &text = QString());

// A one-line label that elides its text at the right to the width it is
// given, rather than cutting the last character it has room for in half; the
// full text is its tooltip while it is elided. It asks for the full text's
// width and gives up any of it, so a row can squeeze it to nothing.
class ElidedLabel : public QLabel
{
    Q_OBJECT
public:
    explicit ElidedLabel(QWidget *parent = nullptr);

    // What the label says when it has the room: setText() is for the
    // elided copy the label shows, and only the label calls it.
    void setFullText(const QString &text);
    QString fullText() const { return m_fullText; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void elide();
    QString m_fullText;
};

// A button whose face is one glyph, centred by its ink rather than by the
// advance the style centres text by (a Nerd Font glyph's ink overhangs it, so
// the style's centring shows as a glyph off to one side). A braille spinner
// frame is centred as the whole cell, so the turning dots do not jiggle. The
// chrome and the pen are the style's: the stylesheet's text colour, the
// accent when checked, the disabled pen when disabled.
class GlyphButton : public QToolButton
{
    Q_OBJECT
public:
    explicit GlyphButton(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
};

// A text button of the window's chrome laid out the way the design kit
// measures one (kit.js measureButton()): 10 px, a 14 px glyph, 6 px, the
// label, 6 px and a 12 px chevron when it drops a menu down, 10 px — and
// 28 px tall. Its text is written the usual way, icon(glyph) + label, with
// chevron() at the end for a dropdown; the button reads the three parts back
// out of it, so every caller keeps setting plain text. A glyph alone, or the
// icon form (setIconForm()), is the design's 28 px square with the glyph
// centred by its ink. The chrome is the style's.
class KitButton : public QToolButton
{
    Q_OBJECT
public:
    explicit KitButton(QWidget *parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
};

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

// A text button of a folding row wearing its glyph alone: the `iconForm`
// property makes a KitButton the design's 28 px square, the glyph centred by
// its ink, and the button takes that width; switched off, its width is free
// again. The caller swaps the text. Only a button that really changes form
// is repolished, so a row may apply its form on every resize.
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

// A borderless button that reads like a label and drops a menu down on click
// (a KitButton, so it measures like the design's chips).
QToolButton *dropdownButton(const QString &objectName);

// A 1 px separator line that follows the theme by itself, so the sections
// that hold one do not each have to re-colour it. A vertical one keeps its
// height free for whoever lays it out. The window's own two rules, under the
// top bar and over the footer, are the fainter chrome tone (foreground at
// 12 %); every other separator is the border tone (20 %).
enum class HairlineTone { Border, Chrome };
QWidget *hairline(Qt::Orientation orientation = Qt::Horizontal, HairlineTone tone = HairlineTone::Border);

// A dim caption inside a menu, like the section labels of the dialog.
QAction *addMenuHeader(QMenu *menu, const QString &text);

// A button's drop-down menu that stays inside the button's window: where it
// would run past the window's right edge it keeps the window's right margin
// instead (never further left than the window's own edge), and where it
// would run past the bottom it opens upwards — what Qt does at the edges of
// the screen, at the edges of the tile the user reads it against. With a
// `bar`, a menu opening downwards hangs from the bar rather than from the
// button: popupTop() under it, like the design's top-bar popups.
void keepMenuInWindow(QMenu *menu, QWidget *button, QWidget *bar = nullptr);

// Where a popup of the top bar starts, in global coordinates: 4 px under the
// bar's bottom edge (screens.js: menus at y = 40 + 4).
int popupTop(const QWidget *bar);

// A popup's width from the design (menuCard() w), `px` at base 12, never
// wider than the window less its margins.
int popupWidth(const QWidget *window, int px);

// The shared geometry of the window's tables: one row of the commit list, one
// row of a file list (the changes table and tree, a commit's files, a little
// tighter), the header every table has, and the narrowest the column taking
// up the leftover width may get — below that the table scrolls sideways
// instead of squeezing it further.
int tableRowHeight();
int fileRowHeight();
int tableHeaderHeight();
constexpr int kMinStretchColumn = 240;

// Gives `column` whatever the other columns, `others` px wide together, leave,
// never less than `floor`: a table with a narrower home than the commit
// page's (the history's, whose design leaves its stretch column 100 px and
// more) passes a floor of its own.
void fitStretchColumn(QTableView *table, int column, int others, int floor = kMinStretchColumn);

// Runs `fit` on every resize of the table's horizontal header.
void onHeaderResize(QTableView *table, std::function<void()> fit);

} // namespace ui
