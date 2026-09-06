#pragma once

#include "ChangesModel.h"
#include "CommitMessageAgent.h"
#include "GitRepo.h"
#include "PaneLayout.h"
#include "RemoteSync.h"

#include <QMainWindow>

class BadgeButton;
class DiffView;
class Toolbar;
class HistoryView;
class MessageEdit;
class MiniRail;
class QCheckBox;
class QFileSystemWatcher;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QSortFilterProxyModel;
class QSplitter;
class QStackedWidget;
class QTableView;
class QTimer;
class QToolButton;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    enum Mode { CommitMode, HistoryMode };

    explicit MainWindow(GitRepo *repo, QWidget *parent = nullptr);

    void setMode(Mode mode);
    Mode mode() const { return m_mode; }
    // Docked: the left section next to the diff pane. Mini: a file rail instead
    // (which also shows the diff pane, a rail on its own being of no use).
    void setPaneLayout(PaneLayout layout, bool persist = true);
    PaneLayout paneLayout() const { return m_layout; }
    // Hides the diff pane so the left section fills the window (hiding it in
    // the Mini layout switches to Docked).
    void setDiffPaneVisible(bool on, bool persist = true);
    bool diffPaneVisible() const { return m_diffVisible; }
    // Ticks the "Amend last commit" box (like `git commit --amend`).
    void setAmend(bool on);
    // Automatic fetching keeps the Pull count current; off leaves the network alone.
    void setAutoFetchEnabled(bool on);
    // Shows the repository containing `path` instead of the current one
    // (false, after a message, if there is none).
    bool openRepository(const QString &path);
    // The repositories opened lately, latest first, those that still exist.
    static QStringList recentRepositories();

public slots:
    void refresh();
    void setInitialSelection(const QString &path) { m_initialSelection = path; }

private slots:
    void onCurrentRowChanged(const QModelIndex &current);
    void onCheckedChanged();
    void onAmendToggled(bool on);
    void commit();
    void openInEditor();
    void applyTheme();
    void showHistoryDiff();
    void updateSyncButtons();
    void onSyncFinished(RemoteSync::Op op, bool ok, bool automatic, const QString &message);
    void showBranchMenu();
    void showRepoMenu();
    void openRepositoryDialog();
    // Asks the chosen coding agent for a message describing the checked
    // changes (Ctrl+G); clicking again while it runs stops it.
    void generateMessage();
    void showAgentMenu();

protected:
    void changeEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    struct SyncButtons {
        BadgeButton *fetch, *pull, *push;
    };
    void buildUi();
    QWidget *buildCommitPage();
    void applyPanes();
    void showDiffFor(const FileChange &change);
    void presentDiff(const QString &unified, const FileChange &change, bool binary, const QString &leftLabel,
                     const QString &rightLabel, const QString &emptyMessage);
    FileChange currentChange(bool *ok) const;
    void checkoutBranch(const QString &name);
    void watchWorkingTree();
    void watchChangedFiles(); // the files in the changes list, for edits made in place
    void updateRepoLabels();
    void setGenerating(bool on);
    void onMessageGenerated(bool ok, const QString &text);
    static void rememberRepository(const QString &root);
    // The footer's message; `ms` > 0 brings the repository path back after that long.
    void showStatus(const QString &text, int ms = 0);

    GitRepo *m_repo;
    RemoteSync *m_sync;
    Mode m_mode = CommitMode;
    PaneLayout m_layout = PaneLayout::Docked;
    ChangesModel *m_model;
    QSortFilterProxyModel *m_proxy;
    QTableView *m_table;
    ChangesTableSetup *m_tableSetup;
    DiffView *m_diff;
    HistoryView *m_history;
    QStackedWidget *m_stack;
    Toolbar *m_toolbar;
    QSplitter *m_splitter;
    QWidget *m_left;
    QWidget *m_rightPane;
    MiniRail *m_rail;
    MessageEdit *m_message;      // the commit message, with the generate button in its corner
    QToolButton *m_agentButton;  // the cog at the right of the MESSAGE label: agent, model, reasoning
    CommitMessageAgent *m_agent;
    QTimer *m_spinner;           // animates the generate button while the agent runs
    int m_spinnerFrame = 0;
    bool m_streaming = false;    // a partial answer already replaced the message text
    QString m_messageBefore;     // the text the user had before the agent started, for a failed run
    QToolButton *m_branchButton; // the branch name; clicking it lists the branches
    QToolButton *m_repoButton;   // the repository name in the footer; clicking it lists recent ones
    QLabel *m_statusLabel;       // the footer's message, the repository path when there is none
    QTimer *m_statusTimer;
    QWidget *m_footerLine;
    QFileSystemWatcher *m_watcher; // the working tree root, the index and the changed files
    QString m_indexFile;
    QLabel *m_summaryLabel;
    QLabel *m_changeLabel;
    QPushButton *m_commitButton;
    QCheckBox *m_selectAll;
    QCheckBox *m_showUnversioned;
    QCheckBox *m_amend;
    QToolButton *m_prevButton;
    QToolButton *m_nextButton;
    QToolButton *m_commitModeButton;
    QToolButton *m_historyModeButton;
    QToolButton *m_layoutButton; // Docked/Mini toggle in the toolbar, checked in Mini
    QToolButton *m_diffToggle;   // top right corner, checked while the diff pane shows
    QHBoxLayout *m_toolbarRow;   // the toolbar (+ the diff toggle while the diff pane is hidden)
    QHBoxLayout *m_navRow;       // the diff pane's Prev/Next row (+ the diff toggle while it shows)
    bool m_diffVisible = true;
    QList<SyncButtons> m_syncButtons; // the toolbar's and the Mini rail's
    QString m_initialSelection;
    QString m_diffSummary;
    QString m_shownDiffKey;    // what the diff pane shows, to skip re-setting an identical document
    bool m_refreshing = false; // the model reset momentarily leaves no row current
    QString m_headMessage;
    bool m_shown = false;
    bool m_historyDirty = true;
};
