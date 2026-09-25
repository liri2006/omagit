#include "ChangesModel.h"
#include "OmarchyTheme.h"
#include "UiHelpers.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QColor>
#include <QLocale>
#include <QFont>
#include <QFrame>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QStyleOptionHeader>
#include <QStyledItemDelegate>
#include <QTableView>

namespace {
// The design's first column (screens.js changesTable(): 32, a 16 px box with
// 8 either side) — the checkboxes, or the row numbers of the history's
// files — and the least every other column may be dragged to.
constexpr int kFirstColumn = 32, kMinDragColumn = 40;
// The status pill's letter, the bold 10 px caption, and the small 11 px text
// of the status, the line counts and the size: text sizes.
constexpr int kPillText = 10, kSmallText = 11;
// "+4" ends this far left of its column's middle and "−2" starts as far right
// of it (changesTable(): an optical nudge, off the grid).
constexpr int kLinesNudge = 2;

// The narrow first column. Where the list has checkboxes, the base class
// paints the model's check state and nothing else; where it has not, this
// paints the row's 1-based position in the view (so it follows sorting and
// filtering) in the muted colour, with a tighter left padding than the
// stylesheet gives ordinary cells so the column can stay narrow.
class FirstColumnDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::paint(painter, option, index); // selection / hover background only
        if (index.data(Qt::CheckStateRole).isValid()) {
            paintCheckBox(painter, option, index);
            return;
        }
        const OmarchyTheme *t = OmarchyTheme::instance();
        painter->save();
        painter->setFont(option.font);
        painter->setPen(option.state & QStyle::State_Selected ? t->accent() : t->mutedText());
        painter->drawText(option.rect.adjusted(ui::space(4), 0, -ui::space(ui::pad::control), 0),
                          Qt::AlignRight | Qt::AlignVCenter, QString::number(index.row() + 1));
        painter->restore();
    }

    bool editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option,
                     const QModelIndex &index) override
    {
        return checkBoxEvent(event, model, option, index);
    }

protected:
    // The text and the box are painted above, not by the style.
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        option->text.clear();
        option->features &= ~QStyleOptionViewItem::HasCheckIndicator;
    }
};

QFont smallFont()
{
    QFont font = OmarchyTheme::instance()->uiFont();
    font.setPixelSize(ui::fontPx(kSmallText));
    return font;
}

// The cell's own background — hover, selection — as the style paints it for
// the row, without the text: everything on it the delegates paint themselves.
void paintCellBackground(QPainter *p, QStyleOptionViewItem opt)
{
    opt.text.clear();
    const QWidget *w = opt.widget;
    QStyle *style = w ? w->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, p, w);
}

// The cells of a file list (screens.js changesTable()), text 8 in: the name
// in its status colour (the accent on the selected row, bold where the model
// says so), the dim folder, the status spelled out small in its colour — or,
// in a 32 px column, the kit's pill — the added and removed lines either side
// of the column's middle, and the dim size at the right. The first column is
// FirstColumnDelegate's.
class FileCellDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void setStatusPill(bool on) { m_statusPill = on; }

    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const int col = index.column();
        if (col != ChangesModel::Name && col != ChangesModel::Path && col != ChangesModel::Status
            && col != ChangesModel::LinesAdded && col != ChangesModel::Size) {
            QStyledItemDelegate::paint(p, option, index); // never shown
            return;
        }
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index); // the row's own font (Qt::FontRole)
        paintCellBackground(p, opt);

        const OmarchyTheme *t = OmarchyTheme::instance();
        const bool selected = opt.state & QStyle::State_Selected;
        const int inset = ui::space(ui::pad::control);
        const QRect r = opt.rect.adjusted(inset, 0, -inset, 0);
        p->save();
        p->setClipRect(opt.rect);
        switch (col) {
        case ChangesModel::Name:
        case ChangesModel::Path: {
            const QColor colour = col == ChangesModel::Path ? t->mutedText()
                : selected                                  ? t->accent()
                                                            : statusColour(index);
            drawText(p, r, opt.font, colour, index.data(Qt::DisplayRole).toString(), Qt::AlignLeft, opt.textElideMode);
            break;
        }
        case ChangesModel::Status:
            if (m_statusPill)
                paintStatusPill(p, opt.rect, index);
            else
                drawText(p, opt.rect, smallFont(), statusColour(index), index.data(Qt::DisplayRole).toString(),
                         Qt::AlignHCenter, Qt::ElideRight);
            break;
        case ChangesModel::LinesAdded: drawLines(p, opt.rect, index); break;
        case ChangesModel::Size:
            drawText(p, r, smallFont(), t->mutedText(), index.data(Qt::DisplayRole).toString(), Qt::AlignRight,
                     Qt::ElideLeft);
            break;
        }
        p->restore();
    }

private:
    static void drawText(QPainter *p, const QRect &r, const QFont &font, const QColor &colour, const QString &text,
                         Qt::Alignment align, Qt::TextElideMode elide)
    {
        p->setFont(font);
        p->setPen(colour);
        p->drawText(r, align | Qt::AlignVCenter, QFontMetrics(font).elidedText(text, elide, r.width()));
    }

    // "+4" in green ending 2 px left of the column's middle, "−2" in red
    // from 2 px right of it; nothing where neither side has a line.
    static void drawLines(QPainter *p, const QRect &cell, const QModelIndex &index)
    {
        const int added = index.siblingAtColumn(ChangesModel::LinesAdded).data(Qt::DisplayRole).toInt();
        const int removed = index.siblingAtColumn(ChangesModel::LinesRemoved).data(Qt::DisplayRole).toInt();
        if (added <= 0 && removed <= 0)
            return;
        const OmarchyTheme *t = OmarchyTheme::instance();
        const int middle = cell.left() + cell.width() / 2;
        const int nudge = ui::space(kLinesNudge);
        const QFont font = smallFont();
        drawText(p, QRect(cell.left(), cell.top(), middle - nudge - cell.left(), cell.height()), font,
                 t->color(QStringLiteral("green")), QStringLiteral("+%1").arg(qMax(0, added)), Qt::AlignRight,
                 Qt::ElideLeft);
        drawText(p, QRect(middle + nudge, cell.top(), cell.right() + 1 - middle - nudge, cell.height()), font,
                 t->color(QStringLiteral("red")), QStringLiteral("−%1").arg(qMax(0, removed)), Qt::AlignLeft,
                 Qt::ElideRight);
    }

    bool m_statusPill = false;
};

// The columns a file list shows by the window's width class (screens.js
// changesTable()), in design px after the 32 px first column; 0 is a column
// the class does not show, and Name takes the rest. Where the status column
// is 32 px it is the kit's status pill headed "St" (`pill`).
struct FileColumns {
    int path, status, lines, size;
    bool pill;
};

FileColumns fileColumns(WidthClass widthClass)
{
    switch (widthClass) {
    case WidthClass::Wide: return {128, 72, 72, 72, false};
    case WidthClass::Large: return {120, 72, 60, 0, false};
    case WidthClass::Medium: return {112, kFirstColumn, 0, 0, true};
    case WidthClass::Stacked: break;
    }
    return {0, kFirstColumn, 0, 0, true};
}

// The pixels of a view's frame, which the design's box starts with and the
// view's cells do not.
int frameOf(const QWidget *widget)
{
    const auto *frame = qobject_cast<const QFrame *>(widget);
    return frame ? frame->frameWidth() : 0;
}

// "812 B", "1.2 KiB", "34 MiB": short enough for a narrow column.
QString compactSize(qint64 bytes)
{
    static const char *const units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = bytes;
    int unit = 0;
    while (value >= 1024 && unit < 4) {
        value /= 1024;
        ++unit;
    }
    const QString number = unit == 0 ? QString::number(bytes)
                                     : QLocale().toString(value, 'f', value < 10 ? 1 : 0);
    return number + QLatin1Char(' ') + QLatin1String(units[unit]);
}
} // namespace

ChangesModel::ChangesModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void ChangesModel::setChanges(const QList<FileChange> &changes)
{
    if (changes == m_changes)
        return; // a reset would drop the selection and the scroll position for nothing
    beginResetModel();
    const bool firstLoad = m_changes.isEmpty() && m_checked.isEmpty();
    QSet<QString> previous = m_checked;
    m_changes = changes;
    m_checked.clear();
    for (const FileChange &c : m_changes) {
        // Versioned changes start checked and
        // unversioned files unchecked. Preserve the user's choices on refresh.
        const bool keep = firstLoad ? !c.isUntracked() : previous.contains(c.path);
        if (keep)
            m_checked.insert(c.path);
    }
    endResetModel();
    checkedHasChanged();
}

void ChangesModel::setCheckable(bool on)
{
    if (m_checkable == on)
        return;
    beginResetModel();
    m_checkable = on;
    endResetModel();
}

QStringList ChangesModel::checkedPaths() const
{
    QStringList out;
    for (const FileChange &c : m_changes)
        if (m_checked.contains(c.path)) {
            out << c.path;
            if (!c.oldPath.isEmpty())
                out << c.oldPath;
        }
    return out;
}

QList<FileChange> ChangesModel::checkedChanges() const
{
    QList<FileChange> out;
    for (const FileChange &c : m_changes)
        if (m_checked.contains(c.path))
            out << c;
    return out;
}

int ChangesModel::checkedCount() const
{
    int n = 0;
    for (const FileChange &c : m_changes)
        if (m_checked.contains(c.path))
            ++n;
    return n;
}

// An empty list has no row to name in dataChanged; checkedChanged() still
// goes out so the tristate box settles.
void ChangesModel::setChecked(const std::function<bool(const FileChange &)> &pick, bool checked)
{
    for (const FileChange &c : std::as_const(m_changes)) {
        if (!pick(c))
            continue;
        if (checked)
            m_checked.insert(c.path);
        else
            m_checked.remove(c.path);
    }
    if (!m_changes.isEmpty())
        emit dataChanged(index(0, Check), index(rowCount() - 1, Check), {Qt::CheckStateRole});
    checkedHasChanged();
}

void ChangesModel::checkedHasChanged()
{
    emit headerDataChanged(Qt::Horizontal, Check, Check); // the check-all box
    emit checkedChanged();
}

void ChangesModel::setAllChecked(bool checked)
{
    setChecked([](const FileChange &) { return true; }, checked);
}

void ChangesModel::setUnversionedChecked(bool checked)
{
    setChecked([](const FileChange &c) { return c.isUntracked(); }, checked);
}

void ChangesModel::setPathsChecked(const QStringList &paths, bool checked, PathMatch match)
{
    const QSet<QString> set(paths.begin(), paths.end());
    setChecked(
        [&set, match](const FileChange &c) {
            if (set.contains(c.path))
                return true;
            return match == MatchRenameSources && !c.oldPath.isEmpty() && set.contains(c.oldPath);
        },
        checked);
}

int ChangesModel::statusRank(FileChange::Kind kind)
{
    switch (kind) {
    case FileChange::Modified: return 0;
    case FileChange::Unmerged: return 1;
    case FileChange::Added: return 2;
    case FileChange::Deleted: return 3;
    case FileChange::Renamed: return 4;
    case FileChange::Copied: return 5;
    case FileChange::TypeChanged: return 6;
    case FileChange::Unknown: return 7;
    case FileChange::Untracked: return 8;
    }
    return 7;
}

QChar ChangesModel::statusLetter(FileChange::Kind kind)
{
    switch (kind) {
    case FileChange::Modified: return QLatin1Char('M');
    case FileChange::Added: return QLatin1Char('A');
    case FileChange::Deleted: return QLatin1Char('D');
    case FileChange::Renamed: return QLatin1Char('R');
    case FileChange::Copied: return QLatin1Char('C');
    case FileChange::TypeChanged: return QLatin1Char('T');
    case FileChange::Unmerged: return QLatin1Char('!');
    case FileChange::Untracked: return QLatin1Char('U');
    default: return QLatin1Char('?');
    }
}

// Status colours: modified blue, added purple, deleted red/brown,
// renamed cyan-ish, conflicted red, unversioned plain.
QColor ChangesModel::statusColor(FileChange::Kind kind)
{
    const OmarchyTheme *t = OmarchyTheme::instance();
    switch (kind) {
    case FileChange::Modified: return t->color(QStringLiteral("blue"));
    case FileChange::Added: return t->color(QStringLiteral("magenta"));
    case FileChange::Deleted: return t->color(QStringLiteral("red"));
    case FileChange::Renamed:
    case FileChange::Copied: return t->color(QStringLiteral("cyan"));
    case FileChange::TypeChanged: return t->color(QStringLiteral("yellow"));
    case FileChange::Unmerged: return t->color(QStringLiteral("bright_red"));
    case FileChange::Untracked: return t->mutedText();
    default: return t->text();
    }
}

QString ChangesModel::statusHtml(const FileChange &change)
{
    return QStringLiteral("<span style=\"color:%1\">%2</span>")
        .arg(statusColor(change.kind).name(), change.statusText().toHtmlEscaped());
}

QColor statusColour(const QModelIndex &index)
{
    const QColor colour = index.data(Qt::ForegroundRole).value<QColor>();
    return colour.isValid() ? colour : OmarchyTheme::instance()->text();
}

QRect checkBoxRect(const QRect &cell, int leading)
{
    const int side = ui::space(ui::box::check);
    const QRect design = cell.adjusted(-leading, 0, 0, 0);
    return QRect(design.left() + (design.width() - side) / 2, cell.top() + (cell.height() - side) / 2, side, side);
}

void paintCheckBox(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index)
{
    const QVariant state = index.data(Qt::CheckStateRole);
    if (!state.isValid())
        return;
    QStyleOptionViewItem box(option);
    box.rect = checkBoxRect(option.rect, frameOf(option.widget));
    box.features |= QStyleOptionViewItem::HasCheckIndicator;
    box.state &= ~(QStyle::State_On | QStyle::State_Off | QStyle::State_NoChange | QStyle::State_HasFocus);
    box.state |= state.toInt() == Qt::Checked            ? QStyle::State_On
                 : state.toInt() == Qt::PartiallyChecked ? QStyle::State_NoChange
                                                         : QStyle::State_Off;
    const QWidget *widget = option.widget;
    QStyle *style = widget ? widget->style() : QApplication::style();
    style->drawPrimitive(QStyle::PE_IndicatorItemViewItemCheck, &box, painter, widget);
}

// QStyledItemDelegate::editorEvent() for the box checkBoxRect() puts, rather
// than where the style would have put one: a left click on it (released
// there, or a double click) toggles it, a press on it is taken; Space and
// Select toggle it too.
bool checkBoxEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option,
                   const QModelIndex &index)
{
    const Qt::ItemFlags flags = model->flags(index);
    const QVariant value = index.data(Qt::CheckStateRole);
    if (!(flags & Qt::ItemIsUserCheckable) || !(flags & Qt::ItemIsEnabled) || !(option.state & QStyle::State_Enabled)
        || !value.isValid())
        return false;
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick: {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() != Qt::LeftButton
            || !checkBoxRect(option.rect, frameOf(option.widget)).contains(mouse->position().toPoint()))
            return false;
        if (event->type() != QEvent::MouseButtonRelease)
            return true;
        break;
    }
    case QEvent::KeyPress: {
        const int key = static_cast<QKeyEvent *>(event)->key();
        if (key != Qt::Key_Space && key != Qt::Key_Select)
            return false;
        break;
    }
    default:
        return false;
    }
    const Qt::CheckState next = value.toInt() == Qt::Checked ? Qt::Unchecked : Qt::Checked;
    return model->setData(index, next, Qt::CheckStateRole);
}

void paintStatusPill(QPainter *painter, const QRect &cell, const QModelIndex &index)
{
    const QVariant kind = index.data(ChangesModel::KindRole);
    if (!kind.isValid())
        return; // a directory row has no status of its own
    const QColor colour = statusColour(index);
    const int side = ui::space(ui::box::pill);
    QRect pill(0, 0, side, side);
    pill.moveCenter(cell.center());
    QColor fill = colour;
    fill.setAlphaF(0.18);
    painter->fillRect(pill, fill);
    QFont font = OmarchyTheme::instance()->uiFont();
    font.setPixelSize(ui::fontPx(kPillText));
    font.setBold(true);
    painter->setFont(font);
    painter->setPen(colour);
    painter->drawText(pill, Qt::AlignCenter,
                      QString(ChangesModel::statusLetter(FileChange::Kind(kind.toInt()))));
}

int ChangesModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_changes.size();
}

int ChangesModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant ChangesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_changes.size())
        return {};
    const FileChange &c = m_changes[index.row()];
    const OmarchyTheme *t = OmarchyTheme::instance();

    switch (role) {
    case PathRole: return c.path;
    case KindRole: return int(c.kind);
    case SortRole:
        if (index.column() == Size)
            return c.size;
        if (index.column() == Status)
            return statusRank(c.kind);
        return data(index, Qt::DisplayRole);
    case Qt::DisplayRole:
        switch (index.column()) {
        case Name: return c.path.section(QLatin1Char('/'), -1);
        case Path: {
            const int separator = c.path.lastIndexOf(QLatin1Char('/'));
            return separator < 0 ? QString() : c.path.left(separator);
        }
        case Extension: return c.extension();
        case Size: return c.size >= 0 ? QVariant(compactSize(c.size)) : QVariant();
        case Status: return c.statusText();
        case LinesAdded: return c.linesAdded >= 0 ? QVariant(c.linesAdded) : QVariant();
        case LinesRemoved: return c.linesRemoved >= 0 ? QVariant(c.linesRemoved) : QVariant();
        }
        break;
    case Qt::CheckStateRole:
        if (m_checkable && index.column() == Check)
            return m_checked.contains(c.path) ? Qt::Checked : Qt::Unchecked;
        break;
    case Qt::TextAlignmentRole:
        // The row numbers of a list without checkboxes are the delegate's,
        // which aligns them itself.
        if (index.column() == Size || index.column() == LinesAdded || index.column() == LinesRemoved)
            return int(Qt::AlignRight | Qt::AlignVCenter);
        break;
    case Qt::ForegroundRole: return statusColor(c.kind);
    case Qt::FontRole:
        if (c.kind == FileChange::Unmerged) {
            QFont f = t->uiFont();
            f.setBold(true);
            return f;
        }
        break;
    case Qt::ToolTipRole: {
        // Rich text for the coloured status; `pre` keeps Qt from wrapping a
        // rich-text tip at its fixed width, which would break a long path.
        QStringList lines{statusHtml(c), c.path.toHtmlEscaped()};
        if (!c.oldPath.isEmpty())
            lines << QStringLiteral("renamed from ") + c.oldPath.toHtmlEscaped();
        if (m_checkable)
            lines << QStringLiteral("index: %1  worktree: %2").arg(QChar(c.index), QChar(c.worktree)).toHtmlEscaped();
        if (c.binary)
            lines << QStringLiteral("binary");
        if (c.size >= 0)
            lines << QStringLiteral("%1 bytes").arg(QLocale().toString(c.size));
        return QStringLiteral("<p style=\"white-space:pre\">%1</p>").arg(lines.join(QStringLiteral("<br>")));
    }
    }
    return {};
}

bool ChangesModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!m_checkable || !index.isValid() || role != Qt::CheckStateRole || index.column() != Check)
        return false;
    const QString &path = m_changes[index.row()].path;
    if (value.toInt() == Qt::Checked)
        m_checked.insert(path);
    else
        m_checked.remove(path);
    emit dataChanged(index, index, {Qt::CheckStateRole});
    checkedHasChanged();
    return true;
}

QVariant ChangesModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal)
        return {};
    if (role == Qt::CheckStateRole) {
        if (!m_checkable || section != Check)
            return {};
        const int checked = checkedCount();
        return int(checked == 0                 ? Qt::Unchecked
                   : checked == m_changes.size() ? Qt::Checked
                                                 : Qt::PartiallyChecked);
    }
    // Name and Path read from the left, 8 px in (the section's padding), like
    // the text under them; the short columns keep their titles centred.
    if (role == Qt::TextAlignmentRole)
        return int((section == Name || section == Path ? Qt::AlignLeft : Qt::AlignHCenter) | Qt::AlignVCenter);
    if (role != Qt::DisplayRole)
        return {};
    switch (section) {
    // A column of checkboxes needs no title; the row numbers keep theirs.
    case Check: return m_checkable ? QString() : tr("#");
    case Name: return tr("Name");
    case Path: return tr("Path");
    case Extension: return tr("Ext");
    case Size: return tr("Size");
    case Status: return tr("Status");
    case LinesAdded: return tr("Added");
    case LinesRemoved: return tr("Removed");
    }
    return {};
}

bool ChangesModel::setHeaderData(int section, Qt::Orientation orientation, const QVariant &value, int role)
{
    if (!m_checkable || orientation != Qt::Horizontal || section != Check || role != Qt::CheckStateRole)
        return false;
    setAllChecked(value.toInt() == Qt::Checked); // headerDataChanged follows from it
    return true;
}

Qt::ItemFlags ChangesModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (m_checkable && index.column() == Check)
        f |= Qt::ItemIsUserCheckable;
    return f;
}

// ---------------------------------------------------------------------------

ChangesHeader::ChangesHeader(QAbstractItemView *view)
    : QHeaderView(Qt::Horizontal, view), m_view(view)
{
    // A view only makes the header it builds itself clickable, and a column
    // is sorted by clicking its section.
    setSectionsClickable(true);
}

// Qt fills the style option of a section before it paints it, labels and all:
// substituting the text there leaves the borders, the fonts and the sort
// handling exactly as they were, and costs the shared model nothing.
void ChangesHeader::setSectionText(int section, const QString &text)
{
    if (text.isEmpty() ? !m_sectionText.contains(section) : m_sectionText.value(section) == text)
        return;
    if (text.isEmpty())
        m_sectionText.remove(section);
    else
        m_sectionText.insert(section, text);
    updateSection(section);
}

QString ChangesHeader::sectionText(int section) const
{
    const auto it = m_sectionText.constFind(section);
    if (it != m_sectionText.constEnd())
        return *it;
    return model() ? model()->headerData(section, orientation(), Qt::DisplayRole).toString() : QString();
}

void ChangesHeader::initStyleOptionForIndex(QStyleOptionHeader *option, int logicalIndex) const
{
    QHeaderView::initStyleOptionForIndex(option, logicalIndex);
    const auto it = m_sectionText.constFind(logicalIndex);
    if (it != m_sectionText.constEnd())
        option->text = *it;
}

QVariant ChangesHeader::checkState() const
{
    return model() ? model()->headerData(ChangesModel::Check, Qt::Horizontal, Qt::CheckStateRole) : QVariant();
}

bool ChangesHeader::isCheckSection(const QPoint &pos) const
{
    return logicalIndexAt(pos) == ChangesModel::Check && checkState().isValid();
}

// Exactly where the rows put their own box, so the column reads as one line
// of checkboxes.
QRect ChangesHeader::checkRect(const QRect &section) const
{
    return checkBoxRect(section, frameOf(m_view));
}

QRect ChangesHeader::checkSectionRect() const
{
    const int section = ChangesModel::Check;
    return QRect(sectionViewportPosition(section), 0, sectionSize(section), viewport()->height());
}

void ChangesHeader::paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const
{
    QHeaderView::paintSection(painter, rect, logicalIndex);
    const QVariant state = logicalIndex == ChangesModel::Check ? checkState() : QVariant();
    if (!state.isValid())
        return;
    QStyleOptionViewItem box;
    box.initFrom(m_view);
    box.features |= QStyleOptionViewItem::HasCheckIndicator;
    box.rect = checkRect(rect);
    box.state = QStyle::State_Enabled
        | (state.toInt() == Qt::Checked            ? QStyle::State_On
           : state.toInt() == Qt::PartiallyChecked ? QStyle::State_NoChange
                                                   : QStyle::State_Off);
    if (m_checkHovered)
        box.state |= QStyle::State_MouseOver; // the rows' boxes light up under the pointer too
    m_view->style()->drawPrimitive(QStyle::PE_IndicatorItemViewItemCheck, &box, painter, m_view);
}

void ChangesHeader::toggleAll()
{
    const bool all = checkState().toInt() == Qt::Checked;
    // Through the model the table has, which may be a proxy that shows only
    // some of the files: the box is over the rows on screen.
    model()->setHeaderData(ChangesModel::Check, Qt::Horizontal, int(all ? Qt::Unchecked : Qt::Checked),
                           Qt::CheckStateRole);
}

// A column of checkboxes is nothing to sort by, so the press never reaches
// the header's own sorting: it ticks every file, or unticks them all once
// they are.
void ChangesHeader::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && isCheckSection(event->position().toPoint())) {
        toggleAll();
        event->accept();
        return;
    }
    QHeaderView::mousePressEvent(event);
}

void ChangesHeader::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && isCheckSection(event->position().toPoint())) {
        event->accept();
        return;
    }
    QHeaderView::mouseReleaseEvent(event);
}

// Qt answers the second of two fast clicks with a double click, which the
// header would take for a sort: toggle again instead, so clicking on and on
// ticks and unticks the way a checkbox does.
void ChangesHeader::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && isCheckSection(event->position().toPoint())) {
        toggleAll();
        event->accept();
        return;
    }
    QHeaderView::mouseDoubleClickEvent(event);
}

void ChangesHeader::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint pos = event->position().toPoint();
    setCheckHovered(isCheckSection(pos) && checkRect(checkSectionRect()).contains(pos));
    QHeaderView::mouseMoveEvent(event);
}

void ChangesHeader::leaveEvent(QEvent *event)
{
    setCheckHovered(false);
    QHeaderView::leaveEvent(event);
}

// Only the box lights up, not the whole section, so a move that stays inside
// it (or outside it) repaints nothing.
void ChangesHeader::setCheckHovered(bool on)
{
    if (m_checkHovered == on)
        return;
    m_checkHovered = on;
    updateSection(ChangesModel::Check);
}

// ---------------------------------------------------------------------------

ChangesTableSetup::ChangesTableSetup(QTableView *table)
    : QObject(table), m_table(table)
{
    // Before anything else on the header: a table hands its sorting and its
    // section settings to the header it has at the time.
    auto *header = new ChangesHeader(table);
    table->setHorizontalHeader(header);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(false);
    table->setSortingEnabled(true);
    table->setShowGrid(false);
    table->setFrameShape(QFrame::NoFrame);
    table->verticalHeader()->setVisible(false);
    header->setStretchLastSection(false);
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->setMinimumSectionSize(kMinDragColumn);
    header->setHighlightSections(false);
    // The checkbox column is the design's, not the user's, to size; applyTheme
    // gives it its width. The row numbers are the user's to widen until the
    // next text size.
    if (hasChecks())
        header->setSectionResizeMode(ChangesModel::Check, QHeaderView::Fixed);
    table->setWordWrap(false);
    m_cells = new FileCellDelegate(table);
    table->setItemDelegate(m_cells);
    table->setItemDelegateForColumn(ChangesModel::Check, new FirstColumnDelegate(table));
    // The design's columns (screens.js changesTable()): the extension is in
    // the name, the removed lines share the added lines' column ("+ −"), and
    // the size reads last, after the line counts.
    table->setColumnHidden(ChangesModel::Extension, true);
    table->setColumnHidden(ChangesModel::LinesRemoved, true);
    header->moveSection(header->visualIndex(ChangesModel::Size), ChangesModel::ColumnCount - 1);
    header->setSectionText(ChangesModel::LinesAdded, tr("+ −"));
    table->setTextElideMode(Qt::ElideMiddle);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->sortByColumn(ChangesModel::Status, Qt::AscendingOrder); // modified first, untracked last
    ui::onHeaderResize(table, [this] { fitStretchColumn(); });
    applyTheme();
}

// Whether the first column carries the checkboxes of a commit list or the
// row numbers of the history's files.
bool ChangesTableSetup::hasChecks() const
{
    return m_table->model()
        && m_table->model()->headerData(ChangesModel::Check, Qt::Horizontal, Qt::CheckStateRole).isValid();
}

void ChangesTableSetup::applyTheme()
{
    m_table->verticalHeader()->setDefaultSectionSize(ui::rowHeight());
    m_table->horizontalHeader()->setFixedHeight(ui::tableHeaderHeight());
    // The first column is the design's 32 px, which at the smaller text sizes
    // is under the floor the other columns keep, so the floor follows it down.
    m_table->horizontalHeader()->setMinimumSectionSize(qMin(kMinDragColumn, ui::space(kFirstColumn)) - 1);
    // Every text size gives the columns the class's widths again, the ones
    // put away for the return from the compact presentation too.
    m_savedWidths.clear();
    applyColumns();
    m_table->viewport()->update();
}

void ChangesTableSetup::setWidthClass(WidthClass widthClass)
{
    if (m_widthClass == widthClass)
        return;
    m_widthClass = widthClass;
    m_savedWidths.clear(); // widths of another class
    applyColumns();
    m_table->viewport()->update();
}

// The class's columns: shown at the design's widths or hidden, the status
// spelled out or the "St" pill — or, compact, the checkbox, Name and the pill
// alone, whatever the class. Extension and LinesRemoved never show. The
// design measures the first and the last column from the table's outer edge,
// whose frame is their first (last) pixel: the cells, inside the frame, are
// that much narrower, so the columns between them line up with the design's.
void ChangesTableSetup::applyColumns()
{
    const FileColumns columns = fileColumns(m_widthClass);
    const bool pill = m_compact || columns.pill;
    static_cast<FileCellDelegate *>(m_cells)->setStatusPill(pill);
    if (auto *header = qobject_cast<ChangesHeader *>(m_table->horizontalHeader()))
        header->setSectionText(ChangesModel::Status, pill ? tr("St") : QString());
    // In the order they read in; Name, which stretches, is not among them.
    const QList<QPair<int, int>> sized{{ChangesModel::Path, m_compact ? 0 : columns.path},
                                       {ChangesModel::Status, m_compact ? kFirstColumn : columns.status},
                                       {ChangesModel::LinesAdded, m_compact ? 0 : columns.lines},
                                       {ChangesModel::Size, m_compact ? 0 : columns.size}};
    int last = -1;
    for (const auto &[column, px] : sized) {
        m_table->setColumnHidden(column, px == 0);
        if (px > 0)
            last = column;
    }
    m_table->ensurePolished(); // the stylesheet's frame
    const int frame = frameOf(m_table);
    m_table->setColumnWidth(ChangesModel::Check, ui::space(kFirstColumn) - frame);
    for (const auto &[column, px] : sized)
        if (px > 0)
            m_table->setColumnWidth(column, ui::space(px) - (column == last ? frame : 0));
    fitStretchColumn();
}

void ChangesTableSetup::fitStretchColumn()
{
    int others = 0;
    for (int c = 0; c < ChangesModel::ColumnCount; ++c)
        if (c != ChangesModel::Name && !m_table->isColumnHidden(c))
            others += m_table->columnWidth(c);
    ui::fitStretchColumn(m_table, ChangesModel::Name, others);
}

void ChangesTableSetup::setCompactDelegates(QAbstractItemDelegate *name, QAbstractItemDelegate *status)
{
    m_compactName = name;
    m_compactStatus = status;
}

void ChangesTableSetup::setCompact(bool on)
{
    if (m_compact == on)
        return;
    m_compact = on;
    if (on)
        enterCompact();
    else
        leaveCompact();
    m_table->viewport()->update();
}

// The widths the user dragged are put away before anything is hidden, so the
// table comes back as it was left — unless the class or the text size moved
// on in between, which brings the class's widths instead.
void ChangesTableSetup::enterCompact()
{
    QHeaderView *header = m_table->horizontalHeader();
    m_savedWidths.clear();
    for (int c = 0; c < ChangesModel::ColumnCount; ++c)
        m_savedWidths << m_table->columnWidth(c);
    header->setSectionResizeMode(ChangesModel::Status, QHeaderView::Fixed);
    if (m_compactName)
        m_table->setItemDelegateForColumn(ChangesModel::Name, m_compactName);
    if (m_compactStatus)
        m_table->setItemDelegateForColumn(ChangesModel::Status, m_compactStatus);
    applyColumns();
}

void ChangesTableSetup::leaveCompact()
{
    m_table->setItemDelegateForColumn(ChangesModel::Name, nullptr);
    m_table->setItemDelegateForColumn(ChangesModel::Status, nullptr);
    m_table->horizontalHeader()->setSectionResizeMode(ChangesModel::Status, QHeaderView::Interactive);
    applyColumns();
    if (m_savedWidths.size() == ChangesModel::ColumnCount) {
        for (const int c : {int(ChangesModel::Path), int(ChangesModel::Status), int(ChangesModel::LinesAdded),
                            int(ChangesModel::Size)})
            if (!m_table->isColumnHidden(c))
                m_table->setColumnWidth(c, m_savedWidths.at(c));
        fitStretchColumn();
    }
}
