#include "ChangesTreeModel.h"
#include "ChangesModel.h"

#include <QAbstractProxyModel>

#include <algorithm>
#include <functional>

ChangesTreeModel::ChangesTreeModel(QAbstractItemModel *source, QObject *parent)
    : QAbstractItemModel(parent), m_source(source), m_changes(nullptr)
{
    // The batch writes of a directory go to the model under the proxy: it is
    // the one that knows every file, shown or filtered out, by path.
    for (QAbstractItemModel *m = source; m && !m_changes;) {
        m_changes = qobject_cast<ChangesModel *>(m);
        auto *proxy = qobject_cast<QAbstractProxyModel *>(m);
        m = proxy ? proxy->sourceModel() : nullptr;
    }
    // Rows coming, going or being sorted change the shape of the tree, so
    // they are the only thing that rebuilds it. Everything else — check
    // marks, colours, a theme change — is a repaint of the rows that exist.
    connect(source, &QAbstractItemModel::modelReset, this, &ChangesTreeModel::reload);
    connect(source, &QAbstractItemModel::rowsInserted, this, &ChangesTreeModel::reload);
    connect(source, &QAbstractItemModel::rowsRemoved, this, &ChangesTreeModel::reload);
    connect(source, &QAbstractItemModel::layoutChanged, this, &ChangesTreeModel::reload);
    connect(source, &QAbstractItemModel::dataChanged, this, &ChangesTreeModel::onSourceDataChanged);
    // The check-all box of the header is the proxy's, whichever view paints it.
    connect(source, &QAbstractItemModel::headerDataChanged, this,
            [this](Qt::Orientation orientation, int first, int last) {
                if (orientation == Qt::Horizontal && first <= ChangesModel::Check && last >= ChangesModel::Check)
                    emit headerDataChanged(Qt::Horizontal, Check, Check);
            });
    reload();
}

ChangesTreeModel::~ChangesTreeModel() = default;

// ---- Building --------------------------------------------------------------

namespace {
// Directories first and alphabetically, then the files in the order the flat
// proxy has them, which is whatever the header was last told to sort by. Two
// directory names that differ only in case are put in a fixed order of their
// own, so the tree never depends on which of them the proxy listed first.
bool beforeAsDirectory(const QString &a, const QString &b)
{
    const int cased = QString::compare(a, b, Qt::CaseInsensitive);
    return cased != 0 ? cased < 0 : a < b;
}
} // namespace

void ChangesTreeModel::reload()
{
    beginResetModel();
    m_rebuilding = true;
    m_fileByRow.clear();
    m_fileByPath.clear();
    m_dirByPath.clear();
    m_root.children.clear();
    m_root.files = 0;
    m_root.path.clear();

    const int rows = m_source ? m_source->rowCount() : 0;
    for (int row = 0; row < rows; ++row) {
        const QString path = m_source->index(row, ChangesModel::Check).data(ChangesModel::PathRole).toString();
        if (path.isEmpty())
            continue;
        // Split on '/' with the spelling git reports, case and all: the tree
        // is an index of the list, never a look at the filesystem, so a
        // deleted file is as much a leaf as any other.
        const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        Node *node = &m_root;
        for (int i = 0; i + 1 < parts.size(); ++i) {
            const QString &name = parts.at(i);
            Node *child = nullptr;
            for (const std::unique_ptr<Node> &candidate : node->children)
                if (candidate->directory && candidate->name == name) {
                    child = candidate.get();
                    break;
                }
            if (!child) {
                auto fresh = std::make_unique<Node>();
                fresh->name = name;
                fresh->path = node->path.isEmpty() ? name : node->path + QLatin1Char('/') + name;
                fresh->parent = node;
                fresh->directory = true;
                child = fresh.get();
                node->children.push_back(std::move(fresh));
                m_dirByPath.insert(child->path, child);
            }
            node = child;
        }
        auto file = std::make_unique<Node>();
        file->name = parts.isEmpty() ? path : parts.last();
        file->path = path;
        file->parent = node;
        file->sourceRow = row;
        m_fileByRow.insert(row, file.get());
        m_fileByPath.insert(path, file.get());
        node->children.push_back(std::move(file));
    }

    // One pass over the finished tree: the order of every level, the row
    // numbers the indexes are made of, and the file counts a collapsed
    // directory shows.
    const std::function<int(Node *)> arrange = [&arrange](Node *node) {
        auto &kids = node->children;
        const auto firstFile = std::stable_partition(kids.begin(), kids.end(),
                                                     [](const std::unique_ptr<Node> &n) { return n->directory; });
        std::sort(kids.begin(), firstFile, [](const std::unique_ptr<Node> &a, const std::unique_ptr<Node> &b) {
            return beforeAsDirectory(a->name, b->name);
        });
        int files = 0;
        for (int row = 0; row < int(kids.size()); ++row) {
            Node *child = kids[row].get();
            child->row = row;
            files += child->directory ? arrange(child) : 1;
        }
        node->files = files;
        return files;
    };
    arrange(&m_root);

    m_rebuilding = false;
    endResetModel();
    emit reloaded(); // the collapsed set and the current row go back on, here and now
}

// ---- Nodes and mapping -----------------------------------------------------

ChangesTreeModel::Node *ChangesTreeModel::nodeOf(const QModelIndex &index) const
{
    if (!index.isValid())
        return const_cast<Node *>(&m_root);
    if (index.model() != this)
        return nullptr;
    return static_cast<Node *>(index.internalPointer());
}

int ChangesTreeModel::sourceColumn(int column)
{
    switch (column) {
    case Check: return ChangesModel::Check;
    case Name: return ChangesModel::Name;
    case Status: return ChangesModel::Status;
    }
    return ChangesModel::Check;
}

QModelIndex ChangesTreeModel::sourceIndex(const Node *file, int column) const
{
    if (!m_source || !file || file->directory || file->sourceRow < 0 || file->sourceRow >= m_source->rowCount())
        return {};
    return m_source->index(file->sourceRow, sourceColumn(column));
}

QModelIndex ChangesTreeModel::indexOf(const Node *node, int column) const
{
    if (!node || node == &m_root)
        return {};
    return createIndex(node->row, column, const_cast<Node *>(node));
}

QModelIndex ChangesTreeModel::mapToSource(const QModelIndex &index) const
{
    const Node *node = nodeOf(index);
    if (!node || node == &m_root || node->directory)
        return {};
    return sourceIndex(node, index.column() < 0 ? int(Check) : index.column());
}

QModelIndex ChangesTreeModel::mapFromSource(const QModelIndex &sourceIndex) const
{
    if (!sourceIndex.isValid() || sourceIndex.model() != m_source)
        return {};
    Node *node = m_fileByRow.value(sourceIndex.row());
    return node ? indexOf(node, Check) : QModelIndex();
}

QModelIndex ChangesTreeModel::indexForPath(const QString &path) const
{
    Node *node = m_fileByPath.value(path);
    return node ? indexOf(node, Check) : QModelIndex();
}

QModelIndex ChangesTreeModel::indexForDirectory(const QString &path) const
{
    Node *node = m_dirByPath.value(path);
    return node ? indexOf(node, Check) : QModelIndex();
}

bool ChangesTreeModel::isDirectory(const QModelIndex &index) const
{
    const Node *node = nodeOf(index);
    return node && node != &m_root && node->directory;
}

QString ChangesTreeModel::path(const QModelIndex &index) const
{
    const Node *node = nodeOf(index);
    return node && node != &m_root ? node->path : QString();
}

int ChangesTreeModel::depth(const QModelIndex &index) const
{
    const Node *node = nodeOf(index);
    int level = -1;
    for (; node && node != &m_root; node = node->parent)
        ++level;
    return qMax(0, level);
}

int ChangesTreeModel::fileCount(const QModelIndex &index) const
{
    const Node *node = nodeOf(index);
    return node && node != &m_root && node->directory ? node->files : 0;
}

QStringList ChangesTreeModel::filePaths(const QModelIndex &index) const
{
    QStringList paths;
    const Node *node = nodeOf(index);
    if (node && node != &m_root)
        collectFiles(node, &paths);
    return paths;
}

void ChangesTreeModel::collectFiles(const Node *node, QStringList *paths) const
{
    if (!node->directory) {
        *paths << node->path;
        return;
    }
    for (const std::unique_ptr<Node> &child : node->children)
        collectFiles(child.get(), paths);
}

// None of them ticked, all of them, or something in between — over every file
// under the directory, whether the branch showing it is open or folded away.
Qt::CheckState ChangesTreeModel::directoryState(const Node *dir) const
{
    int checked = 0, total = 0;
    const std::function<void(const Node *)> walk = [&](const Node *node) {
        if (!node->directory) {
            ++total;
            if (sourceIndex(node, Check).data(Qt::CheckStateRole).toInt() == Qt::Checked)
                ++checked;
            return;
        }
        for (const std::unique_ptr<Node> &child : node->children)
            walk(child.get());
    };
    walk(dir);
    if (total == 0 || checked == 0)
        return Qt::Unchecked;
    return checked == total ? Qt::Checked : Qt::PartiallyChecked;
}

// ---- The model itself ------------------------------------------------------

QModelIndex ChangesTreeModel::index(int row, int column, const QModelIndex &parent) const
{
    if (row < 0 || column < 0 || column >= ColumnCount)
        return {};
    const Node *node = nodeOf(parent);
    if (!node || row >= int(node->children.size()))
        return {};
    return createIndex(row, column, node->children[row].get());
}

QModelIndex ChangesTreeModel::parent(const QModelIndex &index) const
{
    const Node *node = nodeOf(index);
    return node && node != &m_root ? indexOf(node->parent, 0) : QModelIndex();
}

int ChangesTreeModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() && parent.column() != 0)
        return 0; // children hang off column zero, as QTreeView expects
    const Node *node = nodeOf(parent);
    return node ? int(node->children.size()) : 0;
}

int ChangesTreeModel::columnCount(const QModelIndex &) const
{
    return ColumnCount;
}

QVariant ChangesTreeModel::data(const QModelIndex &index, int role) const
{
    const Node *node = nodeOf(index);
    if (!node || node == &m_root)
        return {};
    if (!node->directory) {
        // A file row is the proxy's row: every role it answers — the path,
        // the kind, the status colour, the conflict font, the tooltip — comes
        // straight from it, through the flat column this one stands for.
        const QModelIndex source = sourceIndex(node, index.column());
        return source.isValid() ? source.data(role) : QVariant();
    }
    switch (role) {
    case Qt::CheckStateRole:
        if (index.column() == Check)
            return int(directoryState(node));
        break;
    case Qt::DisplayRole:
        if (index.column() == Name)
            return node->name;
        break;
    case Qt::ToolTipRole:
        if (index.column() == Name)
            return node->path;
        break;
    }
    return {};
}

bool ChangesTreeModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role != Qt::CheckStateRole || index.column() != Check)
        return false;
    Node *node = nodeOf(index);
    if (!node || node == &m_root)
        return false;
    if (!node->directory)
        return m_source->setData(sourceIndex(node, Check), value, role);
    if (!m_changes)
        return false;
    QStringList paths;
    collectFiles(node, &paths);
    if (paths.isEmpty())
        return false;
    // Exactly these files and no others: a rename whose source happens to
    // spell another shown file's path must not be dragged in with them. The
    // notifications the model sends back are what repaints the rows.
    m_changes->setPathsChecked(paths, value.toInt() == Qt::Checked, ChangesModel::CurrentPathsOnly);
    return true;
}

QVariant ChangesTreeModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal)
        return {};
    // Check-all is the proxy's, over the files it is showing: the tree only
    // puts the box in a section of its own.
    if (role == Qt::CheckStateRole)
        return section == Check && m_source ? m_source->headerData(ChangesModel::Check, orientation, role) : QVariant();
    if (role != Qt::DisplayRole)
        return {};
    switch (section) {
    case Check: return QString(); // a column of checkboxes needs no title
    case Name: return tr("Name");
    case Status: return tr("St");
    }
    return {};
}

bool ChangesTreeModel::setHeaderData(int section, Qt::Orientation orientation, const QVariant &value, int role)
{
    if (section != Check || orientation != Qt::Horizontal || role != Qt::CheckStateRole || !m_source)
        return false;
    return m_source->setHeaderData(ChangesModel::Check, orientation, value, role);
}

Qt::ItemFlags ChangesTreeModel::flags(const QModelIndex &index) const
{
    const Node *node = nodeOf(index);
    if (!index.isValid() || !node || node == &m_root)
        return Qt::NoItemFlags;
    // Directories are selectable so the keyboard can walk them, and checkable
    // so Space and a click act on everything under them.
    Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() == Check && (node->directory || sourceIndex(node, Check).data(Qt::CheckStateRole).isValid()))
        flags |= Qt::ItemIsUserCheckable;
    return flags;
}

// ---- Notifications ---------------------------------------------------------

void ChangesTreeModel::onSourceDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight,
                                           const QList<int> &roles)
{
    if (m_rebuilding || m_root.children.empty())
        return;
    // The bulk check setters announce the whole Check column at once, and a
    // single one of them may be the end of several writes: rather than work
    // out which files those were, tell every parent its rows may have moved.
    // Nothing of the structure changed, so the view keeps its expansion, its
    // selection and its scroll position.
    if (bottomRight.row() > topLeft.row()) {
        announce(&m_root, roles);
        return;
    }
    Node *file = m_fileByRow.value(topLeft.row());
    if (!file)
        return;
    emit dataChanged(indexOf(file, 0), indexOf(file, ColumnCount - 1), roles);
    // A file's check mark is part of the state of every directory above it.
    for (const Node *dir = file->parent; dir && dir != &m_root; dir = dir->parent)
        emit dataChanged(indexOf(dir, Check), indexOf(dir, Check), roles);
}

void ChangesTreeModel::announce(const Node *node, const QList<int> &roles)
{
    if (node->children.empty())
        return;
    // One range per parent, as dataChanged is defined: the rows of one level.
    emit dataChanged(indexOf(node->children.front().get(), 0),
                     indexOf(node->children.back().get(), ColumnCount - 1), roles);
    for (const std::unique_ptr<Node> &child : node->children)
        if (child->directory)
            announce(child.get(), roles);
}
