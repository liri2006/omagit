#pragma once

#include "GitRepo.h"
#include "Grid.h"
#include "PaneLayout.h"

#include <QWidget>

class ChangesModel;
class ChangesTableSetup;
class CommitDetails;
class HistoryModel;
class QAbstractItemView;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QSpacerItem;
class QSplitter;
class QStyledItemDelegate;
class QTableView;
class QToolButton;

// The "History" side of the window (screens.js historyPage()): the filter
// row, a commit list with a lane graph and ref chips, the selected commit's
// details card and the files it touched, and the count of commits under them.
// The list, the card and the files share their height in a splitter.
class HistoryView : public QWidget
{
    Q_OBJECT
public:
    // Where the graph column puts its lanes (screens.js commitsTable()), in
    // the pixels of the text size of the moment: lanes `pitch` apart, the
    // first `firstLane` in from the column's left edge, so that two lanes sit
    // symmetric in the design's width and the first never moves as lanes
    // come and go. The column is the design's width, or wider where more
    // lanes are in use than it holds.
    struct GraphGeometry {
        int width = 0;
        int firstLane = 0;
        int pitch = 0;
        int laneCentre(int lane) const { return firstLane + lane * pitch; }
    };
    static GraphGeometry graphGeometry(WidthClass widthClass, int lanes);

    explicit HistoryView(GitRepo *repo, QWidget *parent = nullptr);

    void reload();
    void applyTheme();
    void focusFilter();
    // The window's narrow presentation: All branches wears its glyph alone,
    // the design's 28 px square, with its name in the tooltip; the details
    // card is shorter and leads to the commit's files through a button of
    // its own, the files table having no room.
    void setStacked(bool on);
    // The window's classes, which only ever size things: the columns of the
    // two tables by the width class; the block gap between the sections by
    // the height class and, shallow, no details card and no files table;
    // extra small, no remote chips in the commit list's rows. Nothing of it
    // is saved.
    void setWindowClass(WidthClass width, HeightClass height, bool extraSmall);

    Commit currentCommit(bool *ok) const;
    // The file selected in the files list of the current commit; while a
    // refresh's search brings that commit back (no row current yet), of the
    // commit the files it keeps showing belong to.
    bool currentFile(Commit *commit, FileChange *change) const;
    // What the diff pane should say when currentFile() is false.
    QString emptyMessage() const;
    // The files list of the current commit (model + selection shared with the
    // Mini rail). It stays alive where the page hides it.
    QTableView *filesTable() const { return m_filesTable; }
    QTableView *commitsTable() const { return m_table; }
    CommitDetails *details() const { return m_details; }
    QLineEdit *filterField() const { return m_filter; }
    // The list the keyboard belongs on: the commit's files where the page
    // shows them, the commit list where it does not (stacked, shallow).
    QAbstractItemView *activeListView() const;

signals:
    void currentFileChanged();
    void refreshRequested();
    // The details card's files button (stacked only): the commit's files,
    // which the window shows on the Diff tab.
    void filesRequested();

private slots:
    void onCommitChanged();
    void onFilterChanged();
    void onSearchChanged();
    void loadMore();
    void showContextMenu(const QPoint &pos);

private:
    void updateFooter();
    // The count row's height and the count's place in it.
    void alignCountRow();
    void selectFirstCommit();
    // The card and the files empty, `message` in their place and the diff
    // pane's; showNoMatch() says why while filtering.
    void showNoCommit(const QString &message);
    void showNoMatch();
    void rememberSearchCommit();
    // The end of what a refresh put aside to bring back (m_keepShown and
    // the rest): the commit is back, or will not be, or the user moved on.
    void dropRestoration();
    // The two lists' offsets back where the refresh found them, once the
    // commit is back and the list as long as it was (or the search is done).
    void restoreOffsets();
    // The graph column sized to the lanes in use, Message takes the rest.
    void fitColumns();
    // All branches' face for the presentation of the moment.
    void applyAllRefsForm();
    // The commit list's columns of the width class of the moment, in scaled
    // pixels (the files table's are its setup's).
    void applyCommitColumns();
    // Which of the card and the files table show, and their heights.
    void applySections();

    GitRepo *m_repo;
    HistoryModel *m_model;
    QTableView *m_table;
    QStyledItemDelegate *m_commitDelegate;
    QLineEdit *m_filter;
    QHBoxLayout *m_filterRow;
    QSpacerItem *m_allRefsGap;  // the field to All branches
    QSpacerItem *m_refreshGap;  // All branches to Refresh
    QToolButton *m_allRefs;
    QSplitter *m_splitter;      // the commit list, the card and the files table
    CommitDetails *m_details;
    ChangesModel *m_files;
    QTableView *m_filesTable;
    ChangesTableSetup *m_filesSetup;
    QLabel *m_countLabel;       // the count under the sections
    QString m_emptyMessage;
    QString m_pendingHash; // commit to select after a reload
    QString m_pendingFile; // ... and the file to select in it
    QString m_searchHash;  // the commit a filter's search selects once it turns up
    QString m_currentHash; // the list's current commit as onCommitChanged() last had it; none after a reset
    // A refresh that starts the search over brings the list back as it was:
    // the commit that was current (m_searchHash) with its file
    // (m_pendingFile), then the offsets of the commit list and the files
    // table once as many rows as there were are in again. What the user
    // does meanwhile is theirs: a file they pick is the one the commit comes
    // back with, and a table they scroll (or whose file they pick) keeps
    // the offset they leave it at — the commit coming back does not scroll
    // the list to it, and a second refresh that starts over puts it back
    // there.
    bool m_keepShown = false;     // the commit is not back yet: the card, the files and the diff stay as they are
    Commit m_keptCommit;          // ... that commit, the files' while no row is current (currentFile())
    bool m_restoring = false;     // a refresh's list is coming back: the offsets below are still to go back
    int m_restoreRows = 0;        // the rows the list had
    int m_commitsOffset = -1;     // -1: none to put back, the user's
    int m_filesOffset = -1;
    bool m_puttingBack = false;   // the list is put back (the commit, its file, the offsets): not the user
    bool m_reloading = false; // the model reset momentarily leaves no commit current
    bool m_stacked = false;
    WidthClass m_widthClass = WidthClass::Wide;
    bool m_shallow = false;
    int m_block = ui::kRegularDensity.block; // design px, by the window's height class
    bool m_extraSmall = false;
    bool m_sizedByHand = false; // the user dragged a handle of the splitter this session
};
