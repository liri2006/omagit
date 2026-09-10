#pragma once

#include "ChangesModel.h"
#include "CommitMessageAgent.h"
#include "GitRepo.h"
#include "MessageEdit.h"

#include <QPoint>
#include <QWidget>

#include <functional>

class QCheckBox;
class QLabel;
class QLayout;
class QMenu;
class QPushButton;
class QSortFilterProxyModel;
class QSplitter;
class QTableView;
class QTimer;
class QToolButton;

// The commit dialog: message, changes list, options, buttons.
// It owns the coding-agent flow that writes the message and the commit
// itself; everything that needs the repository as a whole (refreshing, the
// diff pane) is left to the window through the signals below.
class CommitPage : public QWidget
{
    Q_OBJECT
public:
    explicit CommitPage(GitRepo *repo, QWidget *parent = nullptr);

    // The Mini rail shows the same files, through the same selection.
    QSortFilterProxyModel *proxy() const { return m_proxy; }
    QTableView *table() const { return m_table; }

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
    // Presses Commit, as Ctrl+Enter on the button itself does.
    void clickCommit();
    // Whether the diff pane shows, for what a double-click on a file does.
    void setDiffPaneVisible(bool on) { m_diffPaneVisible = on; }

    void applyTheme();

public slots:
    void onCheckedChanged();
    void onAmendToggled(bool on);
    // Writes the checked files (or rewrites the last commit) with the message
    // in the box, asking first when that would rewrite published history.
    void commit();
    // Asks the chosen coding agent for a message describing the checked
    // changes (Ctrl+G); clicking again while it runs stops it.
    void generateMessage();
    void showAgentMenu();

signals:
    void refreshRequested();
    void openRequested();          // the file in its own program
    void showDiffPaneRequested();
    void discardRequested(const FileChange &change);
    void currentRowChanged(const QModelIndex &current);
    void amendToggled(bool on);
    void modeRequested();          // bring the commit view forward
    void statusMessage(const QString &text, int ms);

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
    QLayout *buildOptionsRow();
    QLayout *buildButtonRow();
    void showFileMenu(const QPoint &pos);
    // The cog menu's three sections.
    void addAgentSection(QMenu *menu, const AgentChoice &choice, const std::function<void(const AgentChoice &)> &save);
    void addModelSection(QMenu *menu, const AgentSpec &agent, const AgentCatalog &catalog, const AgentChoice &choice,
                         const std::function<void(const AgentChoice &)> &save);
    void addReasoningSection(QMenu *menu, const QStringList &efforts, const AgentChoice &choice,
                             const std::function<void(const AgentChoice &)> &save);
    void updateCommitButton();
    void setGenerating(bool on);
    void onMessageGenerated(bool ok, const QString &text);

    GitRepo *m_repo;
    ChangesModel *m_model;
    QSortFilterProxyModel *m_proxy;
    QTableView *m_table;
    ChangesTableSetup *m_tableSetup;
    MessageEdit *m_message;      // the commit message, with the generate button in its corner
    QSplitter *m_messageSplitter; // the message over the changes list, the heights it grows in
    bool m_messageSizedByHand = false; // the user dragged the handle this session
    int m_messageRestHeight = -1;      // the pane's height before any text grew it
    QToolButton *m_agentButton;  // the cog at the right of the MESSAGE label: agent, model, reasoning
    CommitMessageAgent *m_agent;
    QTimer *m_spinner;           // animates the generate button while the agent runs
    int m_spinnerFrame = 0;
    bool m_streaming = false;    // a partial answer already replaced the message text
    QString m_messageBefore;     // the text the user had before the agent started, for a failed run
    QLabel *m_summaryLabel;
    QPushButton *m_commitButton;
    QCheckBox *m_selectAll;
    QCheckBox *m_showUnversioned;
    QCheckBox *m_amend;
    QString m_headMessage;
    bool m_merging = false;      // a merge is in progress (MERGE_HEAD exists)
    QString m_mergeMessage;      // git's proposed message, put in the box while it is empty
    bool m_diffPaneVisible = true;
};
