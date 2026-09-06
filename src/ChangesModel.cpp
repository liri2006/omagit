#include "ChangesModel.h"
#include "OmarchyTheme.h"

#include <QColor>
#include <QLocale>
#include <QEvent>
#include <QFont>
#include <QFrame>
#include <QHeaderView>
#include <QPainter>
#include <QPalette>
#include <QStyledItemDelegate>
#include <QTableView>

namespace {
// Paints the row's 1-based position in the view (so it follows sorting and
// filtering) in the muted colour, with a tighter left padding than the
// stylesheet gives ordinary cells so the column can stay narrow.
class RowNumberDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
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
    emit checkedChanged();
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

void ChangesModel::setAllChecked(bool checked)
{
    m_checked.clear();
    if (checked)
        for (const FileChange &c : m_changes)
            m_checked.insert(c.path);
    emit dataChanged(index(0, Path), index(rowCount() - 1, Path), {Qt::CheckStateRole});
    emit checkedChanged();
}

void ChangesModel::setUnversionedChecked(bool checked)
{
    for (const FileChange &c : m_changes)
        if (c.isUntracked()) {
            if (checked)
                m_checked.insert(c.path);
            else
                m_checked.remove(c.path);
        }
    emit dataChanged(index(0, Path), index(rowCount() - 1, Path), {Qt::CheckStateRole});
    emit checkedChanged();
}

void ChangesModel::setPathsChecked(const QStringList &paths, bool checked)
{
    const QSet<QString> set(paths.begin(), paths.end());
    for (const FileChange &c : m_changes)
        if (set.contains(c.path) || (!c.oldPath.isEmpty() && set.contains(c.oldPath))) {
            if (checked)
                m_checked.insert(c.path);
            else
                m_checked.remove(c.path);
        }
    emit dataChanged(index(0, Path), index(rowCount() - 1, Path), {Qt::CheckStateRole});
    emit checkedChanged();
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
        return index.column() == Size ? QVariant(c.size) : data(index, Qt::DisplayRole);
    case Qt::DisplayRole:
        switch (index.column()) {
        case Path:
            return c.oldPath.isEmpty() ? c.path : QStringLiteral("%1 (from %2)").arg(c.path, c.oldPath);
        case Extension: return c.extension();
        case Size: return c.size >= 0 ? QVariant(compactSize(c.size)) : QVariant();
        case Status: return c.statusText();
        case LinesAdded: return c.linesAdded >= 0 ? QVariant(c.linesAdded) : QVariant();
        case LinesRemoved: return c.linesRemoved >= 0 ? QVariant(c.linesRemoved) : QVariant();
        }
        break;
    case Qt::CheckStateRole:
        if (m_checkable && index.column() == Path)
            return m_checked.contains(c.path) ? Qt::Checked : Qt::Unchecked;
        break;
    case Qt::TextAlignmentRole:
        if (index.column() == Number || index.column() == Size || index.column() == LinesAdded
            || index.column() == LinesRemoved)
            return int(Qt::AlignRight | Qt::AlignVCenter);
        break;
    case Qt::ForegroundRole: {
        // Status colours: modified blue, added purple, deleted red/brown,
        // renamed cyan-ish, conflicted red, unversioned plain.
        switch (c.kind) {
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
    case Qt::FontRole:
        if (c.kind == FileChange::Unmerged) {
            QFont f;
            f.setBold(true);
            return f;
        }
        break;
    case Qt::ToolTipRole: {
        QString tip = c.path;
        if (!c.oldPath.isEmpty())
            tip += QStringLiteral("\nrenamed from ") + c.oldPath;
        if (m_checkable)
            tip += QStringLiteral("\nindex: %1  worktree: %2").arg(QChar(c.index), QChar(c.worktree));
        if (c.binary)
            tip += QStringLiteral("\nbinary");
        if (c.size >= 0)
            tip += QStringLiteral("\n%1 bytes").arg(QLocale().toString(c.size));
        return tip;
    }
    }
    return {};
}

bool ChangesModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!m_checkable || !index.isValid() || role != Qt::CheckStateRole || index.column() != Path)
        return false;
    const QString &path = m_changes[index.row()].path;
    if (value.toInt() == Qt::Checked)
        m_checked.insert(path);
    else
        m_checked.remove(path);
    emit dataChanged(index, index, {Qt::CheckStateRole});
    emit checkedChanged();
    return true;
}

QVariant ChangesModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case Number: return tr("#");
    case Path: return tr("Path");
    case Extension: return tr("Ext");
    case Size: return tr("Size");
    case Status: return tr("Status");
    case LinesAdded: return tr("Added");
    case LinesRemoved: return tr("Removed");
    }
    return {};
}

Qt::ItemFlags ChangesModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (m_checkable && index.column() == Path)
        f |= Qt::ItemIsUserCheckable;
    return f;
}

// ---------------------------------------------------------------------------

ChangesTableSetup::ChangesTableSetup(QTableView *table)
    : QObject(table), m_table(table)
{
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(false);
    table->setSortingEnabled(true);
    table->setShowGrid(false);
    table->setFrameShape(QFrame::NoFrame);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(false);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table->horizontalHeader()->setMinimumSectionSize(40);
    table->horizontalHeader()->setHighlightSections(false);
    table->setWordWrap(false);
    table->setItemDelegate(new AccentSelectionDelegate(table));
    table->setItemDelegateForColumn(ChangesModel::Number, new RowNumberDelegate(table));
    table->setColumnWidth(ChangesModel::Number, 40);
    table->setColumnWidth(ChangesModel::Extension, 64);
    table->setColumnWidth(ChangesModel::Size, 100);
    table->setColumnWidth(ChangesModel::Status, 104);
    table->setColumnWidth(ChangesModel::LinesAdded, 76);
    table->setColumnWidth(ChangesModel::LinesRemoved, 92);
    table->setTextElideMode(Qt::ElideMiddle);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->sortByColumn(ChangesModel::Path, Qt::AscendingOrder);
    table->horizontalHeader()->installEventFilter(this);
    applyTheme();
}

void ChangesTableSetup::applyTheme()
{
    m_table->verticalHeader()->setDefaultSectionSize(qRound(OmarchyTheme::instance()->fontBase() * 2.33));
    m_table->viewport()->update();
}

bool ChangesTableSetup::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_table->horizontalHeader() && event->type() == QEvent::Resize)
        fitPathColumn();
    return QObject::eventFilter(watched, event);
}

void ChangesTableSetup::fitPathColumn()
{
    int others = 0;
    for (int c = 0; c < ChangesModel::ColumnCount; ++c)
        if (c != ChangesModel::Path)
            others += m_table->columnWidth(c);
    m_table->setColumnWidth(ChangesModel::Path, qMax(240, m_table->viewport()->width() - others));
}
