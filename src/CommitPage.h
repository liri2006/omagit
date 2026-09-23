#pragma once

#include "ChangesModel.h"
#include "CommitMessageAgent.h"
#include "GitRepo.h"
#include "MessageEdit.h"

#include <QModelIndex>
#include <QPoint>
#include <QSet>
#include <QWidget>

class ChangesTreeModel;
class QAbstractItemView;
class QButtonGroup;
class QCheckBox;
class QHBoxLayout;
class QLabel;
class QLayout;
class QPushButton;
class QSortFilterProxyModel;
class QSpacerItem;
class QSplitter;
class QStackedWidget;
class QTableView;
class QTextDocument;
class QTimer;
class QToolButton;
class QTreeView;

// The commit dialog: message, changes list, action bar.
// It owns the coding-agent flow that writes the message and the commit
// itself; everything that needs the repository as a whole (refreshing, the
// diff pane) is left to the window through the signals below.
class CommitPage : public QWidget
{
    Q_OBJECT
public:
    // How the pending files are listed. Table is the one that has always
    // been there, columns and all; Compact is that same table with only the
    // checkbox, the name and a status pill; Tree lists them under their
    // directories. Whichever is on, the flat list below is the same.
    enum class FilesView { Compact, Tree, Table };

    // What the commit controls say at this moment, for a second face of them
    // (the Mini layout's commit popover): read whole whenever
    // commitControlsChanged() says something in it may have moved.
    struct CommitControls {
        QString commitText, commitName, commitTip; // the Commit button's text, accessible name, tooltip
        bool commitEnabled = false;
        bool amendChecked = false, amendEnabled = false;
        QString amendTip;
        int checked = 0; // the files the commit would take
        int shown = 0;   // the rows the list shows
        QString generateText, generateTip; // the generate button in the message box's corner
        bool generating = false; // an agent is writing the message; the generate button stops it
    };

    explicit CommitPage(GitRepo *repo, QWidget *parent = nullptr);

    // The message being written, one document for every box that edits it:
    // whoever shows it elsewhere shares the text and its undo stack.
    QTextDocument *messageDocument() const;
    CommitControls commitControls() const;
    // The cog's tooltip, for a cog elsewhere that opens the same settings.
    static QString agentButtonTip();

    // The Mini rail shows the same files, through the same selection: the
    // proxy and the table's selection model are canonical in every view.
    QSortFilterProxyModel *proxy() const { return m_proxy; }
    QTableView *table() const { return m_table; }
    QTreeView *tree() const;
    // The list the user is looking at: the table (in either of its two
    // presentations) or the tree.
    QAbstractItemView *activeListView() const;

    FilesView filesView() const { return m_filesView; }
    // `persist` writes the choice to window/filesView, which is what a click
    // on one of the three buttons does; a run started with --files-view never
    // writes it, whatever is clicked afterwards.
    void setFilesView(FilesView view, bool persist = true);
    // The --files-view override: this run lists its files that way and leaves
    // the saved choice alone.
    void setFilesViewOverride(FilesView view);
    // The spelling of a view in the settings and on the command line; `ok`
    // comes back false for anything but the three lowercase names.
    static FilesView viewFromKey(const QString &key, bool *ok);
    static QString viewKey(FilesView view);

    // The row the file actions apply to, independent of the checked files.
    FileChange currentChange(bool *ok) const;
    // Re-reads the working tree into the changes list.
    void reload();
    // The repo-relative paths in the list, in model order (for the file watcher).
    QStringList paths() const;
    // Selects the row of `path`, leaving an already current row alone; false
    // when the list has no such file.
    bool selectPath(const QString &path);
    // Selects the first row; false when the list is empty.
    bool selectFirstRow();
    // Selects the first file a merge left conflicted, if there is one.
    void selectFirstConflict();
    // The scroll offsets of the changes list (x sideways, y down), which
    // selecting a row again would lose.
    QPoint scrollOffset() const;
    void setScrollOffset(const QPoint &offset);
    // Checks every file for the commit, or none when all are checked (lazygit's "a").
    void toggleAllChecked();
    void toggleAmend();
    // Ticks or unticks "Amend last commit" (nothing while there is none to amend).
    void setAmendChecked(bool on);
    // Drops the amend state of the repository just left behind (the message
    // goes with it, and the window refreshes for the new one).
    void resetAmend();
    // Ticks the files of the commit being amended (after the refresh that follows).
    void checkHeadPaths();
    // A merge in progress decides what the Commit button says, whether the
    // amend box may be ticked, and puts git's proposed message in the box.
    void setMergeState(const MergeState &merge, const Commit &head);
    // Presses Commit, the way the window's Ctrl+Enter does outside the Mini layout.
    void clickCommit();
    // Whether the diff pane shows, for what a double-click on a file does.
    void setDiffPaneVisible(bool on) { m_diffPaneVisible = on; }

    // The three files-view buttons, for the tests and for anyone who wants to
    // press one without going through the accessible names.
    QToolButton *compactButton() const { return m_compactButton; }
    QToolButton *treeButton() const { return m_treeButton; }
    QToolButton *tableButton() const { return m_tableButton; }
    QToolButton *unversionedButton() const { return m_unversioned; }
    // The cog at the right of the MESSAGE label, the agent popover's anchor.
    QToolButton *agentButton() const { return m_agentButton; }

    void applyTheme();

protected:
    void resizeEvent(QResizeEvent *event) override;

public slots:
    void onCheckedChanged();
    void onAmendToggled(bool on);
    // Writes the checked files (or rewrites the last commit) with the message
    // in the box, asking first when that would rewrite published history.
    // True once the commit (or the amend) is made; false when there was no
    // message, the user backed out of rewriting published history, or git
    // failed — each of which the page has already told the user about.
    bool commit();
    // Asks the chosen coding agent for a message describing the checked
    // changes (Ctrl+G); clicking again while it runs stops it.
    void generateMessage();
    // Saves the agent, model and reasoning level the agent popover picked, and
    // puts them on the generate button's tooltip.
    void applyAgentChoice(const AgentChoice &choice);
    // A cog was pressed, the page's own or one showing the page elsewhere:
    // says so through agentSettingsRequested(), hanging from `anchor`.
    void requestAgentSettings(QWidget *anchor);

signals:
    void refreshRequested();
    void openRequested();          // the file in its own program
    void showDiffPaneRequested();
    void discardRequested(const FileChange &change);
    void currentRowChanged(const QModelIndex &current);
    void amendToggled(bool on);
    void modeRequested();          // bring the commit view forward
    void statusMessage(const QString &text, int ms);
    // Something commitControls() reports may have changed.
    void commitControlsChanged();
    // A cog was pressed — the page's own or one showing the page elsewhere
    // (the Mini commit card's): the agent settings, hanging from `anchor`.
    void agentSettingsRequested(QWidget *anchor);

private:
    // The sections of the page, top to bottom, as the constructor builds them.
    void setupAgent();
    QLayout *buildMessageSection();
    // Gives the message pane the height its text needs: growing up to half
    // the splitter, shrinking down to its resting height once text is
    // deleted. Typing does not undo a size the user dragged; pasted or
    // deleted text does.
    void fitMessage(MessageEdit::Edit edit);
    QWidget *buildChangesSection();
    // The bottom row: Amend at the left, the Commit button at the right.
    QLayout *buildActionBar();
    // "Amend last commit" where the row has the width for it, "Amend" where
    // it has not.
    void updateAmendLabel();
    // The three ghost buttons at the right of the CHANGES row, in their own
    // layout so the section row's spacing is not added on top of the
    // design's 2 / 6 / 7 px gaps.
    QHBoxLayout *buildChangesTools();
    // The tree, its model and its delegates, built once beside the table.
    QWidget *buildChangesTree();
    // Its two narrow columns and its row height, in the scaled pixels of the
    // text size of the moment.
    void applyTreeMetrics();
    // Unticks the unversioned files while the eye hides them.
    void untickHidden();
    // The file menu of whichever list was right-clicked; `index` is that
    // view's own, and directories and empty space have no menu at all.
    void showFileMenu(QAbstractItemView *view, const QModelIndex &index, const QPoint &pos);
    // The tree's current row became `index`: a file makes the flat row
    // current, a directory leaves the canonical file where it is.
    void onTreeCurrentChanged(const QModelIndex &index);
    // The canonical current file changed: show it in the tree, ancestors
    // opened, without another current-row notification coming back.
    void onTableCurrentChanged(const QModelIndex &current);
    // Opens every directory above `path` and makes its row current.
    void revealInTree(const QString &path, bool makeCurrent);
    // Remembers which row the tree's keyboard is on — its path and whether it
    // is a file or a directory, since one name can be both.
    void rememberTreeCurrent(const QModelIndex &index);
    // Puts the collapsed set back on the freshly built nodes, then brings the
    // view's layout up to date, so a scroll offset restored right after this
    // is measured against the rows the user will see.
    void restoreTreeState();
    void onTreeExpanded(const QModelIndex &index, bool expanded);
    // The repo-relative path of the canonical current file, empty when none is.
    QString currentPath() const;
    void updateCommitButton();
    void setGenerating(bool on);
    // The one place the generate button's face changes, so the text and the
    // tooltip are both current whenever commitControlsChanged() goes out.
    void setGenerateFace(const QString &text, const QString &tip);
    void onMessageGenerated(bool ok, const QString &text);

    GitRepo *m_repo;
    ChangesModel *m_model;
    QSortFilterProxyModel *m_proxy;
    QTableView *m_table;
    ChangesTableSetup *m_tableSetup;
    QStackedWidget *m_listStack;   // the table and the tree in the list's slot
    QTreeView *m_tree;
    ChangesTreeModel *m_treeModel;
    FilesView m_filesView = FilesView::Table;
    bool m_filesViewLocked = false;  // --files-view: this run saves no choice
    QSet<QString> m_collapsed;       // directories folded away, by exact path
    bool m_restoringTree = false;    // a rebuild is putting that state back
    bool m_syncingCurrent = false;   // one list is following the other
    QToolButton *m_compactButton;
    QToolButton *m_treeButton;
    QToolButton *m_tableButton;
    QButtonGroup *m_viewButtons;
    QHBoxLayout *m_changesTools;        // the switcher, the eye and Refresh
    QList<QPair<QSpacerItem *, int>> m_toolSpacers; // their gaps, with the design px
    QWidget *m_toolsDivider;            // between the switcher and the eye
    QString m_treeCurrentPath;          // the tree's own row, kept over rebuilds
    bool m_treeCurrentIsDirectory = false; // ...and which of the two lookups finds it again
    // The canonical file the tree was last put on. It is the tree's memory of
    // the flat list, not of itself: the row the keyboard is on may be a
    // directory, and a reload empties the table's selection before the window
    // puts it back — neither is a new file to reveal.
    QString m_syncedCanonicalPath;
    MessageEdit *m_message;      // the commit message, with the generate button in its corner
    QSplitter *m_messageSplitter; // the message over the changes list, the heights it grows in
    QLayout *m_sectionsLayout;    // MESSAGE and CHANGES, over the action bar
    QLayout *m_changesLayout;     // CHANGES: its header row over the list, a scaled gap apart
    QHBoxLayout *m_actionBar;     // the bottom row, a scaled gap under the list
    bool m_messageSizedByHand = false; // the user dragged the handle this session
    int m_messageRestHeight = -1;      // the pane's height before any text grew it
    QToolButton *m_agentButton;  // the cog at the right of the MESSAGE label: agent, model, reasoning
    CommitMessageAgent *m_agent;
    QTimer *m_spinner;           // animates the generate button while the agent runs
    int m_spinnerFrame = 0;
    bool m_streaming = false;    // a partial answer already replaced the message text
    QString m_messageBefore;     // the text the user had before the agent started, for a failed run
    QLabel *m_changesLabel;      // CHANGES · checked / shown
    QWidget *m_changesDivider;   // between the eye and Refresh
    QPushButton *m_commitButton = nullptr; // built after the message section
    QToolButton *m_unversioned;  // the eye: checked = unversioned files shown
    QCheckBox *m_amend = nullptr;
    QString m_headMessage;
    bool m_merging = false;      // a merge is in progress (MERGE_HEAD exists)
    QString m_mergeMessage;      // git's proposed message, put in the box while it is empty
    bool m_diffPaneVisible = true;
};
