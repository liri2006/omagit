#pragma once

#include "GitRepo.h"
#include "PaneLayout.h"

#include <QAbstractTableModel>
#include <QHash>
#include <QColor>
#include <QHeaderView>
#include <QList>
#include <QObject>
#include <QSet>

#include <functional>

class QAbstractItemDelegate;
class QAbstractItemView;
class QPainter;
class QStyleOptionViewItem;
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

    // Whether a path list names the files it means outright, or also matches
    // the source of a rename. Ticking the files of the commit being amended
    // wants the latter (HEAD names the old path of a file renamed since);
    // anything that acts on the rows a view is showing — a tree directory,
    // the check-all box over the filtered list — wants the former, or a
    // rename source that happens to spell another shown file's path would
    // drag that file's row in with it.
    enum PathMatch { MatchRenameSources, CurrentPathsOnly };

    // Position of a kind in the Status sort: modified first, untracked last.
    static int statusRank(FileChange::Kind kind);
    // The single letter a status is reduced to where there is no room for its
    // name: the Mini rail's badge and the status pill of the tree and compact
    // presentations. Untracked is "U", a status git did not name "?".
    static QChar statusLetter(FileChange::Kind kind);
    // The colour a status is written in, in every list and in the diff
    // pane's summary (modified blue, added purple, ...).
    static QColor statusColor(FileChange::Kind kind);
    // The status spelled out in its colour, the first line of a file's tip.
    static QString statusHtml(const FileChange &change);

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
    void setPathsChecked(const QStringList &paths, bool checked, PathMatch match = MatchRenameSources);

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

// The status colour of a row of a list over a ChangesModel, as the model
// gives it (the plain foreground where it gives none).
QColor statusColour(const QModelIndex &index);

// The 16 px box of a checkbox column (screens.js changesTable(): a 32 px
// column, the box centred), where the delegates and the header paint it and
// what a click toggles. `leading` is the pixels of the view's frame the
// column's design box starts with and its cell does not (the first column's).
QRect checkBoxRect(const QRect &cell, int leading = 0);
// The row's box at checkBoxRect(), in the state the model gives it, drawn by
// the view's style; and QStyledItemDelegate::editorEvent() for that box.
void paintCheckBox(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index);
bool checkBoxEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option,
                   const QModelIndex &index);

// The kit's status pill (kit.js statusPill()): a 16 px square with square
// corners, the row's status colour at 18 % for the fill and the status letter
// in it, centred in `cell`. A directory row of the tree has no status of its
// own and gets none. The narrow status column of the compact table and the
// tree, and of a commit's files in the history, is this.
void paintStatusPill(QPainter *painter, const QRect &cell, const QModelIndex &index);

// The header of a changes list, table or tree. Its first section holds the
// label-less check-all box of a checkable list — the model keeps its state —
// and answers a click with it instead of sorting by a column of checkboxes.
class ChangesHeader : public QHeaderView
{
    Q_OBJECT
public:
    explicit ChangesHeader(QAbstractItemView *view);

    // A label this presentation alone paints over the model's own, for the
    // tables that spell Status "St" in a 32 px column. The model
    // and the proxy every other view shares are left untouched; an empty
    // text hands the section back to them.
    void setSectionText(int section, const QString &text);
    // What the section says on screen: this presentation's own text where it
    // has one, the model's otherwise.
    QString sectionText(int section) const;

protected:
    void paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const override;
    void initStyleOptionForIndex(QStyleOptionHeader *option, int logicalIndex) const override;
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

    QAbstractItemView *m_view;
    QHash<int, QString> m_sectionText; // what this presentation calls a section
    bool m_checkHovered = false; // the pointer is over the box, not just the section
};

// Applies the shared look of a file list (screens.js changesTable()) — the
// commit page's changes and a commit's files in the history — and keeps its
// columns on the design's: a 32 px first column (the checkboxes, or the row
// numbers where the model has none), Path, Status (the "St" pill where it is
// 32 px), "+ −" and Size as the window's width class has them, and Name
// taking the rest, never narrower than ui::kMinStretchColumn (then the view
// scrolls sideways). Every row and every cell is painted on the design's
// grid. The table's model — and whether that model is checkable — has to be
// set before this is built.
class ChangesTableSetup : public QObject
{
    Q_OBJECT
public:
    explicit ChangesTableSetup(QTableView *table);
    void applyTheme();

    // The window's width class: the columns and their widths. The widths are
    // the user's to drag until the class or the text size changes.
    void setWidthClass(WidthClass widthClass);
    WidthClass widthClass() const { return m_widthClass; }
    // Gives Name whatever the other columns leave; the table's own resizes
    // call it, and a caller that has moved the other columns calls it again.
    void fitStretchColumn();

    // The compact presentation: the same table, its columns down to the
    // checkbox, the file name and a narrow status pill headed "St", with Name
    // taking whatever is left. The painting is the caller's — the commit page
    // owns both delegates — and so is the decision to switch; the columns
    // come back as they were when it is switched off.
    void setCompactDelegates(QAbstractItemDelegate *name, QAbstractItemDelegate *status);
    void setCompact(bool on);
    bool compact() const { return m_compact; }

private:
    bool hasChecks() const;
    void applyColumns();
    void enterCompact();
    void leaveCompact();

    QTableView *m_table;
    QAbstractItemDelegate *m_cells;                   // the design's cells, the table's own
    QAbstractItemDelegate *m_compactName = nullptr;   // the caller's, not owned
    QAbstractItemDelegate *m_compactStatus = nullptr; // likewise
    QList<int> m_savedWidths;                         // the widths the user left the table at before compact
    bool m_compact = false;
    WidthClass m_widthClass = WidthClass::Wide;
};
