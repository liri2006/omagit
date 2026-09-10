#pragma once

#include "GitRepo.h"
#include "PaneLayout.h"
#include "RemoteSync.h"

#include <QMainWindow>

class BadgeButton;
class CommitPage;
class DiffPane;
class Footer;
class Toolbar;
class HistoryView;
class MiniRail;
class QFileSystemWatcher;
class QHBoxLayout;
class QSplitter;
class QStackedWidget;
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
    void showKeybindings();
    void onCurrentRowChanged(const QModelIndex &current);
    void onAmendToggled(bool on);
    void commit();
    void openInEditor();
    void applyTheme();
    void showHistoryDiff();
    void updateSyncButtons();
    void onSyncFinished(RemoteSync::Op op, bool ok, bool automatic, const QString &message);
    void showBranchMenu();
    // The merge view (Ctrl+Shift+M): pick the two branches, see what the
    // merge would do, then do it — or abort the one in progress.
    void showMergeDialog();
    void showRepoMenu();
    void openRepositoryDialog();
    // Ctrl+S: the history view with its filter focused (lazygit's filter key).
    void focusHistoryFilter();
    // Ctrl+A in the changes list / Mini rail, Ctrl+Shift+A, Ctrl+D.
    void toggleAllChecked();
    void toggleAmend();
    void discardCurrent();
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
    void applyPanes();
    void showDiffFor(const FileChange &change);
    void presentDiff(const QString &unified, const FileChange &change, bool binary, const QString &leftLabel,
                     const QString &rightLabel, const QString &emptyMessage);
    void discardChange(const FileChange &change); // asks first
    void checkoutBranch(const QString &name);
    void watchWorkingTree();
    void watchChangedFiles(); // the files in the changes list, for edits made in place
    void updateRepoLabels();
    void updateCommitButton();
    void updateMergeButtons(const MergeState &merge);
    static void rememberRepository(const QString &root);
    // The footer's message; `ms` > 0 brings the repository path back after that long.
    void showStatus(const QString &text, int ms = 0);

    GitRepo *m_repo;
    RemoteSync *m_sync;
    Mode m_mode = CommitMode;
    PaneLayout m_layout = PaneLayout::Docked;
    CommitPage *m_commitPage;
    DiffPane *m_diffPane;
    Footer *m_footer;
    HistoryView *m_history;
    QStackedWidget *m_stack;
    Toolbar *m_toolbar;
    QSplitter *m_splitter;
    QWidget *m_left;
    MiniRail *m_rail;
    QFileSystemWatcher *m_watcher; // the working tree root, the index and the changed files
    QString m_indexFile;
    QToolButton *m_commitModeButton;
    QToolButton *m_historyModeButton;
    QHBoxLayout *m_toolbarRow;   // the toolbar (+ the diff toggle while the diff pane is hidden)
    bool m_diffVisible = true;
    QList<SyncButtons> m_syncButtons; // the toolbar's and the Mini rail's
    QList<BadgeButton *> m_mergeButtons; // the toolbar's and the Mini rail's, marked while a merge waits
    bool m_merging = false;   // a merge is in progress (MERGE_HEAD exists)
    QString m_mergeMessage;   // git's proposed message, put in the box while it is empty
    QString m_initialSelection;
    QString m_shownDiffKey;    // what the diff pane shows, to skip re-setting an identical document
    bool m_refreshing = false; // the model reset momentarily leaves no row current
    bool m_shown = false;
    bool m_historyDirty = true;
};
