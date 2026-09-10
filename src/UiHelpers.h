#pragma once

#include <QString>
#include <QStringList>
#include <QToolButton>

#include <functional>

class QAction;
class QDateTime;
class QFont;
class QLabel;
class QMenu;
class QTableView;
class QWidget;

// The small widget helpers the window's sections share: the Nerd Font glyphs
// of the shell, the label factories and the borderless buttons of its chrome.
namespace ui {

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
// md-creation (the sparkle of "generate"), md-cog, md-robot
constexpr uint kSparkle = 0xF0674, kCog = 0xF0493, kRobot = 0xF06A9;
// md-information: the keybindings button in the footer.
constexpr uint kInfo = 0xF02FC;

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

// Icon-only button with less padding, for a row of labels.
QToolButton *smallButton(uint glyph, const QString &fallback, const QString &tip);

// A borderless button that reads like a label and drops a menu down on click.
QToolButton *dropdownButton(const QString &objectName);

// A 1 px separator line that follows the theme by itself, so the sections
// that hold one do not each have to re-colour it. A vertical one keeps its
// height free for whoever lays it out.
QWidget *hairline(Qt::Orientation orientation = Qt::Horizontal);

// A dim caption inside a menu, like the section labels of the dialog.
QAction *addMenuHeader(QMenu *menu, const QString &text);

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
