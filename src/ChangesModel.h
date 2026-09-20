#pragma once

#include "GitRepo.h"

#include <QAbstractTableModel>
#include <QHeaderView>
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
    // Check is the narrow first column: the row's checkbox where the list has
    // them, and otherwise (the history's files) the row's 1-based position in
    // the visible, sorted list, which the view paints.
    enum Column { Check, Name, Path, Extension, Size, Status, LinesAdded, LinesRemoved, ColumnCount };
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
    // The check-all box of the header lives on the Check section as a
    // CheckStateRole: checked / partial / unchecked over the whole list when
    // the list has checkboxes, nothing at all when it has not. Writing it
    // back is setAllChecked(), so a view can drive it through the proxy.
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    bool setHeaderData(int section, Qt::Orientation orientation, const QVariant &value, int role) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

signals:
    void checkedChanged();

private:
    // What the three setters above share: pick the rows, then tell the views
    // that the whole column of check marks may have moved.
    void setChecked(const std::function<bool(const FileChange &)> &pick, bool checked);
    // The marks moved: the header's check-all box follows them.
    void checkedHasChanged();

    QList<FileChange> m_changes;
    QSet<QString> m_checked;
    bool m_checkable = true;
};

// The header of a changes table. Its first section holds the label-less
// check-all box of a checkable list — the model keeps its state — and answers
// a click with it instead of sorting by a column of checkboxes.
class ChangesHeader : public QHeaderView
{
    Q_OBJECT
public:
    explicit ChangesHeader(QTableView *table);

protected:
    void paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    // The check-all state of the model, invalid where the list has no
    // checkboxes at all (the history's files).
    QVariant checkState() const;
    bool isCheckSection(const QPoint &pos) const;
    // Ticks every file, or unticks them all once they are.
    void toggleAll();
    // Where the box is painted inside `section` (the first section's rect).
    QRect checkRect(const QRect &section) const;
    QRect checkSectionRect() const;
    void setCheckHovered(bool on);

    QTableView *m_table;
    bool m_checkHovered = false; // the pointer is over the box, not just the section
};

// Applies the shared look of a changes table (column widths, row height, no
// grid) and keeps the Path column filling the leftover width, never narrower
// than ui::kMinStretchColumn (then the view scrolls horizontally).
// The table's model — and whether that model is checkable — has to be set
// before this is built: the first column is the design's to size where the
// list has checkboxes and the user's where it has row numbers, and the
// constructor reads which it is from the model that is on the table then.
class ChangesTableSetup : public QObject
{
    Q_OBJECT
public:
    explicit ChangesTableSetup(QTableView *table);
    void applyTheme();

private:
    bool hasChecks() const;
    void fitPathColumn();
    QTableView *m_table;
};
