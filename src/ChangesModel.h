#pragma once

#include "GitRepo.h"

#include <QAbstractTableModel>
#include <QList>
#include <QObject>
#include <QSet>

#include <functional>

class QTableView;

// The list of changes in the commit dialog. Also used, without
// checkboxes, for the files of a commit in the history view.
class ChangesModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    // Number is painted by the view (1-based position in the visible, sorted list).
    enum Column { Number, Name, Path, Extension, Size, Status, LinesAdded, LinesRemoved, ColumnCount };
    // Extra roles (any column): the repo-relative path and the FileChange::Kind.
    // SortRole is what a proxy should sort by: the display text, except that
    // Size sorts by the byte count rather than its "1.2 KiB" rendering and
    // Status sorts by group (modified first, untracked last). A stable sort
    // keeps the model's path order inside a group, so the table's default
    // Status sort lists each group alphabetically.
    enum Role { PathRole = Qt::UserRole + 1, KindRole, SortRole };

    // Position of a kind in the Status sort: modified first, untracked last.
    static int statusRank(FileChange::Kind kind);

    explicit ChangesModel(QObject *parent = nullptr);

    void setChanges(const QList<FileChange> &changes);
    const FileChange &change(int row) const { return m_changes.at(row); }
    int count() const { return m_changes.size(); }

    void setCheckable(bool on);
    bool checkable() const { return m_checkable; }

    QStringList checkedPaths() const;
    QList<FileChange> checkedChanges() const; // in list order
    int checkedCount() const;
    void setAllChecked(bool checked);
    void setUnversionedChecked(bool checked);
    void setPathsChecked(const QStringList &paths, bool checked);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

signals:
    void checkedChanged();

private:
    // What the three setters above share: pick the rows, then tell the views
    // that the whole column of check marks may have moved.
    void setChecked(const std::function<bool(const FileChange &)> &pick, bool checked);

    QList<FileChange> m_changes;
    QSet<QString> m_checked;
    bool m_checkable = true;
};

// Applies the shared look of a changes table (column widths, row height, no
// grid) and keeps the Path column filling the leftover width, never narrower
// than ui::kMinStretchColumn (then the view scrolls horizontally).
class ChangesTableSetup : public QObject
{
    Q_OBJECT
public:
    explicit ChangesTableSetup(QTableView *table);
    void applyTheme();

private:
    void fitPathColumn();
    QTableView *m_table;
};
