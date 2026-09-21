#pragma once

#include <QAbstractItemModel>
#include <QHash>
#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

class ChangesModel;

// The directory tree of the pending changes, built over the flat changes
// proxy the whole window shares: an adapter, not a model of its own. The
// files are still the proxy's rows — their check marks, colours, fonts and
// tooltips are read straight off it — and every write goes back through it,
// so the table, the Mini rail and the tree can never disagree about what is
// checked or what is current.
//
// Three columns: the design's 30 px checkbox, the name (which carries the
// whole depth geometry, painted by the view's delegate), and a 30 px status
// pill. Directories carry a derived check state and a name and nothing else.
class ChangesTreeModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    // Deliberately its own enum: only Check and Name line up with the flat
    // model's columns, Status is the flat model's sixth one.
    enum Column { Check = 0, Name = 1, Status = 2, ColumnCount = 3 };

    // `source` is the flat proxy (its source model has to be a ChangesModel:
    // directory check marks are written through it in one batch).
    explicit ChangesTreeModel(QAbstractItemModel *source, QObject *parent = nullptr);
    ~ChangesTreeModel() override;

    // Builds the tree again from the proxy's rows as they are now. The
    // structural signals of the proxy call this by themselves; it is public
    // for the tests and for a view that wants it done on the spot.
    void reload();

    bool isDirectory(const QModelIndex &index) const;
    // The repo-relative path of a file row or a directory row, empty for
    // anything else. Directories are their own kind of thing: a directory is
    // never mapped to a file, however its path is spelled.
    QString path(const QModelIndex &index) const;
    // 0 for the top-level rows, one more per level below them.
    int depth(const QModelIndex &index) const;
    // How many of the proxy's files sit under a directory, collapsed branches
    // and all; 0 for a file row.
    int fileCount(const QModelIndex &index) const;
    // The paths of those files, in tree order.
    QStringList filePaths(const QModelIndex &index) const;

    // File rows only; an invalid index for a directory or for nothing.
    QModelIndex mapToSource(const QModelIndex &index) const;
    QModelIndex mapFromSource(const QModelIndex &sourceIndex) const;
    // The file row of an exact repo-relative path. Files and directories are
    // looked up apart because one name can be both: a repository may list a
    // file `a` beside a file `a/b`, and revealing a file must never land on
    // the directory that happens to spell it.
    QModelIndex indexForPath(const QString &path) const;
    // The directory row of an exact repo-relative path.
    QModelIndex indexForDirectory(const QString &path) const;

    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &index) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    bool setHeaderData(int section, Qt::Orientation orientation, const QVariant &value, int role) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

signals:
    // The nodes have been built again and the view has been reset: whoever
    // keeps state by path (the collapsed directories, the current row) puts
    // it back here, while the call that caused the rebuild is still running.
    void reloaded();

private:
    // One row of the tree. Directories hold their children; a file holds the
    // proxy row it stands for, which is good until the next rebuild — every
    // structural change of the proxy is one.
    struct Node {
        QString name;
        QString path;                                   // repo-relative
        Node *parent = nullptr;
        bool directory = false;
        int row = 0;                                    // among its siblings
        int sourceRow = -1;                             // files only
        int files = 0;                                  // descendant files
        std::vector<std::unique_ptr<Node>> children;
    };

    Node *nodeOf(const QModelIndex &index) const;
    // The flat column a tree column reads: Status is the odd one out.
    static int sourceColumn(int column);
    QModelIndex sourceIndex(const Node *file, int column) const;
    // Unchecked / partial / checked over the files under a directory.
    Qt::CheckState directoryState(const Node *dir) const;
    void collectFiles(const Node *node, QStringList *paths) const;
    QModelIndex indexOf(const Node *node, int column) const;

    void onSourceDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QList<int> &roles);
    // Tells the view that every row under `node` may have moved, one parent
    // at a time, without pretending the structure changed.
    void announce(const Node *node, const QList<int> &roles);

    QAbstractItemModel *m_source;
    ChangesModel *m_changes; // the proxy's source model, for batch check writes
    Node m_root;
    QHash<int, Node *> m_fileByRow;      // proxy row → file node
    QHash<QString, Node *> m_fileByPath; // repo-relative path → file node
    QHash<QString, Node *> m_dirByPath;  // ...and the same path as a directory
    bool m_rebuilding = false;
};
