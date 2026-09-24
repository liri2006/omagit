#include "ChangesModel.h"
#include "OmarchyTheme.h"
#include "UiHelpers.h"

#include <QAbstractItemView>
#include <QColor>
#include <QLocale>
#include <QFont>
#include <QFrame>
#include <QHeaderView>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QStyleOptionHeader>
#include <QStyledItemDelegate>
#include <QTableView>

namespace {
// The design's first column (the checkboxes, or the row numbers of the
// history's files), and the least every other column may be dragged to.
constexpr int kCheckColumn = 30, kNumberColumn = 40;
// The status pill: a 16 px square, its letter in the bold 10 px caption.
constexpr int kPillSize = 16, kPillText = 10;

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
        if (index.data(Qt::CheckStateRole).isValid()) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        QStyleOptionViewItem bg = option;
        bg.text.clear();
        QStyledItemDelegate::paint(painter, bg, index); // selection / hover background only
        const OmarchyTheme *t = OmarchyTheme::instance();
        painter->save();
        painter->setFont(option.font);
        painter->setPen(option.state & QStyle::State_Selected ? t->accent() : t->mutedText());
        painter->drawText(option.rect.adjusted(4, 0, -8, 0), Qt::AlignRight | Qt::AlignVCenter,
                          QString::number(index.row() + 1));
        painter->restore();
    }

protected:
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        option->text.clear();
    }
};

// The model colours each row after its status (ForegroundRole), which would
// otherwise win over the stylesheet's selected-item colour: paint the selected
// row's text in the accent like every other selected item in the app.
class AccentSelectionDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

protected:
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        if (option->state & QStyle::State_Selected) {
            const QColor acc = OmarchyTheme::instance()->accent();
            option->palette.setColor(QPalette::Text, acc);
            option->palette.setColor(QPalette::HighlightedText, acc);
        }
    }
};

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

void paintStatusPill(QPainter *painter, const QRect &cell, const QModelIndex &index)
{
    const QVariant kind = index.data(ChangesModel::KindRole);
    if (!kind.isValid())
        return; // a directory row has no status of its own
    const QColor colour = statusColour(index);
    const int side = ui::space(kPillSize);
    QRect pill(0, 0, side, side);
    pill.moveCenter(cell.center());
    QColor fill = colour;
    fill.setAlphaF(0.18);
    painter->fillRect(pill, fill);
    QFont font = OmarchyTheme::instance()->uiFont();
    font.setPixelSize(ui::space(kPillText));
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
    // Name and Path read from the left, 10 px in (the section's padding), like
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
// of checkboxes: the table's style measures both from the item rule.
QRect ChangesHeader::checkRect(const QRect &section) const
{
    QStyleOptionViewItem item;
    item.initFrom(m_view);
    item.rect = section;
    item.features |= QStyleOptionViewItem::HasCheckIndicator;
    return m_view->style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator, &item, m_view);
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
    : QObject(table), m_table(table), m_stretchColumn(ChangesModel::Path), m_stretchFloor(ui::kMinStretchColumn)
{
    // Before anything else on the header: a table hands its sorting and its
    // section settings to the header it has at the time.
    table->setHorizontalHeader(new ChangesHeader(table));
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(false);
    table->setSortingEnabled(true);
    table->setShowGrid(false);
    table->setFrameShape(QFrame::NoFrame);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(false);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table->horizontalHeader()->setMinimumSectionSize(kNumberColumn);
    table->horizontalHeader()->setHighlightSections(false);
    // The checkbox column is the design's, not the user's, to size; applyTheme
    // gives it its width.
    if (hasChecks())
        table->horizontalHeader()->setSectionResizeMode(ChangesModel::Check, QHeaderView::Fixed);
    table->setWordWrap(false);
    table->setItemDelegate(new AccentSelectionDelegate(table));
    table->setItemDelegateForColumn(ChangesModel::Check, new FirstColumnDelegate(table));
    table->setColumnWidth(ChangesModel::Check, kNumberColumn);
    table->setColumnWidth(ChangesModel::Name, 240);
    table->setColumnWidth(ChangesModel::Extension, 64);
    table->setColumnWidth(ChangesModel::Size, 100);
    table->setColumnWidth(ChangesModel::Status, 104);
    table->setColumnWidth(ChangesModel::LinesAdded, 76);
    table->setColumnWidth(ChangesModel::LinesRemoved, 92);
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
    m_table->verticalHeader()->setDefaultSectionSize(ui::fileRowHeight());
    m_table->horizontalHeader()->setFixedHeight(ui::tableHeaderHeight());
    // The first column is the design's 30 px, which at the smaller text sizes
    // is under the floor the other columns keep, so the floor follows it
    // down. The checkboxes keep to it for good; the row numbers of the
    // history's files are the user's to widen until the next text size.
    m_table->horizontalHeader()->setMinimumSectionSize(qMin(kNumberColumn, ui::space(kCheckColumn)));
    m_table->setColumnWidth(ChangesModel::Check, ui::space(kCheckColumn));
    // A new text size moves the compact widths too, and Name has to be fitted
    // again around them — without touching the table widths put away for the
    // return to the full presentation.
    if (m_compact) {
        applyCompactWidths();
        fitStretchColumn();
    }
    m_table->viewport()->update();
}

void ChangesTableSetup::setStretchColumn(int column, int floor)
{
    m_stretchColumn = column;
    m_stretchFloor = floor;
    fitStretchColumn();
}

void ChangesTableSetup::fitStretchColumn()
{
    const int stretch = m_compact ? int(ChangesModel::Name) : m_stretchColumn;
    int others = 0;
    for (int c = 0; c < ChangesModel::ColumnCount; ++c)
        if (c != stretch && !m_table->isColumnHidden(c))
            others += m_table->columnWidth(c);
    ui::fitStretchColumn(m_table, stretch, others, m_stretchFloor);
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
    fitStretchColumn();
    m_table->viewport()->update();
}

// The columns the user sized are put away before anything is hidden, so the
// table comes back exactly as it was left — whatever happened in between,
// including a text-size change.
void ChangesTableSetup::enterCompact()
{
    QHeaderView *header = m_table->horizontalHeader();
    m_savedWidths.clear();
    m_savedModes.clear();
    for (int c = 0; c < ChangesModel::ColumnCount; ++c) {
        m_savedWidths << m_table->columnWidth(c);
        m_savedModes << header->sectionResizeMode(c);
    }
    for (const int c : {int(ChangesModel::Path), int(ChangesModel::Extension), int(ChangesModel::Size),
                        int(ChangesModel::LinesAdded), int(ChangesModel::LinesRemoved)})
        m_table->setColumnHidden(c, true);
    header->setSectionResizeMode(ChangesModel::Status, QHeaderView::Fixed);
    applyCompactWidths();
    if (m_compactName)
        m_table->setItemDelegateForColumn(ChangesModel::Name, m_compactName);
    if (m_compactStatus)
        m_table->setItemDelegateForColumn(ChangesModel::Status, m_compactStatus);
    if (auto *changes = qobject_cast<ChangesHeader *>(header))
        changes->setSectionText(ChangesModel::Status, tr("St"));
}

void ChangesTableSetup::leaveCompact()
{
    QHeaderView *header = m_table->horizontalHeader();
    if (auto *changes = qobject_cast<ChangesHeader *>(header))
        changes->setSectionText(ChangesModel::Status, QString());
    m_table->setItemDelegateForColumn(ChangesModel::Name, nullptr);
    m_table->setItemDelegateForColumn(ChangesModel::Status, nullptr);
    for (int c = 0; c < ChangesModel::ColumnCount; ++c) {
        m_table->setColumnHidden(c, false);
        if (c < m_savedModes.size())
            header->setSectionResizeMode(c, m_savedModes.at(c));
        if (c < m_savedWidths.size())
            m_table->setColumnWidth(c, m_savedWidths.at(c));
    }
    // The checkbox column is the design's, never the user's: it follows the
    // text size of the moment rather than the width it had before.
    if (hasChecks())
        m_table->setColumnWidth(ChangesModel::Check, ui::space(kCheckColumn));
}

void ChangesTableSetup::applyCompactWidths()
{
    m_table->setColumnWidth(ChangesModel::Status, ui::space(kCheckColumn));
}
