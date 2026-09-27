#pragma once

#include "CommitPage.h"
#include "DiffView.h"
#include "GitRepo.h"
#include "Grid.h"
#include "PaneLayout.h"
#include "RemoteSync.h"
#include "TopBar.h"

#include <QKeySequence>
#include <QMainWindow>
#include <QPoint>

#include <functional>

class BadgeButton;
class AgentPopover;
class CommitPopover;
class DiffPane;
class Footer;
class HistoryView;
class MiniRail;
class NewBranchCard;
class QFileSystemWatcher;
class QHBoxLayout;
class QVBoxLayout;
class QSplitter;
class QStackedWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    enum Mode { CommitMode, HistoryMode };

    explicit MainWindow(GitRepo *repo, QWidget *parent = nullptr);
    ~MainWindow() override;

    // Stacks (or not) for the width the window is about to be shown at, so
    // that the minimum its layout sets on the way is the presentation's.
    void setVisible(bool visible) override;

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
    // Below the stacking width the body shows one thing at a time — the page
    // of the mode, or the Diff tab: the Mini rail beside the diff pane. It is
    // presentation only: the two preferences above stay what they were, and
    // come back as the window widens again.
    bool isStacked() const { return m_stacked; }
    // Shows or leaves the Diff tab; nothing at all outside the stacked width.
    void setDiffTab(bool on);
    bool diffTab() const { return m_diffTab; }
    // Ticks the "Amend last commit" box (like `git commit --amend`).
    void setAmend(bool on);
    // Automatic fetching keeps the Pull count current; off leaves the network alone.
    void setAutoFetchEnabled(bool on);
    // The --files-view override: the commit page lists its files as `key`
    // ("compact", "tree" or "table") spells it, for this run only.
    void setFilesView(const QString &key);
    // Shows the repository containing `path` instead of the current one
    // (false, after a message, if there is none).
    bool openRepository(const QString &path);
    // The repositories opened lately, latest first, those that still exist.
    static QStringList recentRepositories();

public slots:
    void refresh();
    void setInitialSelection(const QString &path) { m_initialSelection = path; }
    // The commit popover beside the Mini rail's commit tile (the tile, Ctrl+Enter,
    // --screenshot-menu commit). Only in the Mini layout's commit view; opening
    // it while it is open only puts the keyboard back in its message box.
    void showCommitPopover();
    // The New branch card under the branch chip (Ctrl+N, the branch menu's
    // last row, a commit's menu in the history, --screenshot-menu newbranch):
    // `name` in its field and `start` where the branch starts — a branch, a
    // tag, a commit, or, empty, the current branch.
    void showNewBranchCard(const QString &start = QString(), const QString &name = QString());

private slots:
    void showKeybindings();
    void showSettings();
    void onCurrentRowChanged(const QModelIndex &current);
    void onAmendToggled(bool on);
    void openInEditor();
    void applyTheme();
    void showHistoryDiff();
    void updateSyncButtons();
    void onSyncFinished(RemoteSync::Op op, bool ok, bool automatic, const QString &message);
    void showBranchMenu();
    // The merge view (Ctrl+Shift+M): pick the two branches, see what the
    // merge would do, then do it — or abort the one in progress.
    void showMergeDialog();
    // git (or ssh) is waiting for a login: show the dialog and hand what the
    // user types back to the helper process that asked.
    void onAskPassRequest(const AskPassRequest &request);
    // The same dialog with a made-up github.com request, so a screenshot of
    // it can be taken (--screenshot-menu login); the answers go nowhere.
    void showLoginDialog();
    void showRepoMenu();
    void openRepositoryDialog();
    void showCloneDialog();
    // Ctrl+S: the history view with its filter focused (lazygit's filter key).
    void focusHistoryFilter();
    // Ctrl+A in the changes list / Mini rail, Ctrl+Shift+A, Ctrl+D.
    void toggleAllChecked();
    void toggleAmend();
    void discardCurrent();
    // Asks the chosen coding agent for a message describing the checked
    // changes (Ctrl+G); clicking again while it runs stops it.
    void generateMessage();
    // The agent settings under the cog of the current layout
    // (--screenshot-menu agent); nothing in the history.
    void showAgentMenu();
    // The stacked sync dropdown's menu, the more menu and the stacked action
    // bar's options menu (--screenshot-menu sync|more|options); each only
    // where its button is on screen.
    void showSyncMenu();
    void showMoreMenu();
    void showOptionsMenu();
    // The diff pane's view options behind its "…" (--screenshot-menu diff):
    // only while the pane is narrow enough to wear that button.
    void showDiffOptionsMenu();

protected:
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    struct SyncButtons {
        BadgeButton *fetch, *pull, *push;
    };
    // One keybinding, declared once: the row the panel lists and, when `keys`
    // is not empty, the QShortcut(s) installShortcuts() makes for it.
    struct Binding {
        Binding(QList<QKeySequence> keys, QString display, QString action, QString context = {},
                std::function<void()> run = {}, QList<QWidget *> hosts = {}, QObject *receiver = nullptr,
                bool panelRuns = true)
            : keys(std::move(keys)), display(std::move(display)), action(std::move(action)),
              context(std::move(context)), run(std::move(run)), hosts(std::move(hosts)), receiver(receiver),
              panelRuns(panelRuns)
        {
        }

        QList<QKeySequence> keys;    // empty: a widget or a button already owns the keys
        QString display;             // the panel's spelling, when the keys do not give it
        QString action;
        QString context;             // where the keys work, when that is not everywhere
        std::function<void()> run;   // what the keys — and the panel row — do
        QList<QWidget *> hosts;      // widgets the shortcut belongs to; the window when empty
        QObject *receiver;           // context object of `run`; the window when null
        bool panelRuns;              // false: the panel lists the row but does not run it
        bool listed = true;          // false: the shortcut works but the panel leaves it out
    };
    // Ctrl+Enter: commits from the popover when it is open, opens it in the
    // Mini layout, presses the page's Commit button in the Docked one.
    void commitKeys();
    // Ctrl+N: the New branch card from the current branch, or from the
    // commit selected in the history; with the card open, its name field.
    void newBranchKeys();
    // Where the user was before a refresh: the scroll offsets of the two file
    // lists and the place in the diff.
    struct ViewState {
        QPoint changes;  // x horizontal, y vertical
        int rail = 0;
        DiffView::ViewState diff;
    };
    // The sync buttons' tooltips and the branch button's upstream line.
    struct SyncTips {
        QString fetch, pull, push, upstream;
    };
    // What the fetch tooltip knows about fetching.
    struct FetchHistory {
        QDateTime last;
        bool ok = true;
        QString error;
        int interval = 0; // seconds between automatic fetches, 0 when they are off
    };
    // The captions above the two sides of a diff and its "nothing to show" line.
    struct DiffLabels {
        QString left, right, empty;
    };

    void buildUi();
    QList<Binding> bindings();
    void installShortcuts();
    void applyPanes();
    // Classifies the window's width and, when that changed, stacks or
    // unstacks the presentation (see isStacked()).
    void updateStacking();
    // The left section's share of the splitter: remembered, or the design's
    // width for the window's width class.
    void applySplitterSizes();
    int defaultLeftWidth() const;
    // The density of the window's classes: the margins, the gaps and the
    // footer, or the room it leaves, and whoever lays out with them.
    void applyDensity();
    // Folds the commit page's header rows into More, or brings them back: on
    // two rows, and in a shallow window at any width.
    void applyHeaderRows();
    // Whether the Mini rail is on screen: the Mini layout, or the Diff tab.
    bool railShowing() const;
    // A tab of the top bar, or Ctrl+1 / Ctrl+2: Changes and History are the
    // two modes (never set again when already on), Diff the stacked tab.
    void showTab(TopBar::Tab tab);
    // The top bar's tabs say what the body shows.
    void syncTab();
    // "full" (pre-0.4) and window/leftFull (pre-0.3) become Docked + hidden diff.
    void migrateLayoutSettings();
    static int autoFetchSecondsSetting();
    // The steps of refresh(), in the order it runs them.
    void updateHeader();
    void reloadChanges();
    ViewState viewState() const;
    void restoreSelection(const QString &path, bool sameFile, const ViewState &state);
    void reloadHistory(const ViewState &state);
    void showCurrentDiff();
    void showDiffFor(const FileChange &change);
    static DiffLabels diffLabels(const FileChange &change, const QString &base, const QString &baseLabel,
                                 const QString &right, const QString &unchanged);
    static SyncTips syncTips(const UpstreamState &s, RemoteSync::Op op, const FetchHistory &fetches,
                             bool pushPublishes, const QStringList &pushArgs);
    void presentDiff(const QString &unified, const FileChange &change, bool binary, const QString &leftLabel,
                     const QString &rightLabel, const QString &emptyMessage);
    void discardChange(const FileChange &change); // asks first
    void checkoutBranch(const QString &name);
    void watchWorkingTree();
    void watchChangedFiles(); // the files in the changes list, for edits made in place
    void updateRepoLabels();
    // The count in the Changes tab: the rows the changes list shows, in both
    // modes. Reads the proxy and nothing else.
    void updateChangesCount();
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
    TopBar *m_topBar;
    QSplitter *m_splitter;
    QWidget *m_left;
    MiniRail *m_rail;
    CommitPopover *m_commitPopover; // the Mini layout's commit controls, an overlay of the central widget
    AgentPopover *m_agentPopover;   // the agent settings, an overlay hanging from a cog
    NewBranchCard *m_newBranchCard; // an overlay too, hanging under the branch chip
    QFileSystemWatcher *m_watcher; // the working tree root, the index and the changed files
    QString m_indexFile;
    bool m_diffVisible = true;
    bool m_stacked = false; // the window is narrower than the stacking width
    WidthClass m_widthClass = WidthClass::Wide;
    HeightClass m_heightClass = HeightClass::Normal;
    ui::Density m_density = ui::kRegularDensity; // of the two classes
    QVBoxLayout *m_rootLayout; // the top bar, the body and the footer
    QHBoxLayout *m_bodyLayout; // the rail and the splitter, inside the window's margins
    bool m_diffTab = false; // stacked: the Diff tab, rather than the mode's page
    SyncButtons m_syncButtons;     // the top bar's
    BadgeButton *m_mergeButton;    // the top bar's, marked while a merge waits
    QString m_initialSelection;
    QString m_shownDiffKey;    // what the diff pane shows, to skip re-setting an identical document
    bool m_refreshing = false; // the model reset momentarily leaves no row current
    bool m_shown = false;
    bool m_historyDirty = true;
};
