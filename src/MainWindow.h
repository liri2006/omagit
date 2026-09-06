#pragma once

#include "ChangesModel.h"
#include "GitRepo.h"
#include "PaneLayout.h"
#include "RemoteSync.h"

#include <QMainWindow>

class BadgeButton;
class DiffView;
class Toolbar;
class HistoryView;
class MiniRail;
class QCheckBox;
class QHBoxLayout;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSortFilterProxyModel;
class QSplitter;
class QStackedWidget;
class QTableView;
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
    QPlainTextEdit *m_message;
    QLabel *m_branchLabel;
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
    QString m_headMessage;
    bool m_shown = false;
    bool m_historyDirty = true;
};
