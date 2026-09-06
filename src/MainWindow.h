#pragma once

#include "ChangesModel.h"
#include "GitRepo.h"

#include <QMainWindow>

class DiffView;
class HistoryView;
class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSortFilterProxyModel;
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
    // Full: the left section takes the whole window; otherwise it is a sidebar
    // next to the diff pane.
    void setLeftFull(bool full, bool persist = true);
    // Ticks the "Amend last commit" box (like `git commit --amend`).
    void setAmend(bool on);

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

private:
    void buildUi();
    QWidget *buildCommitPage();
    void showDiffFor(const FileChange &change);
    void presentDiff(const QString &unified, const FileChange &change, bool binary, const QString &leftLabel,
                     const QString &rightLabel, const QString &emptyMessage);
    FileChange currentChange(bool *ok) const;

    GitRepo *m_repo;
    Mode m_mode = CommitMode;
    ChangesModel *m_model;
    QSortFilterProxyModel *m_proxy;
    QTableView *m_table;
    ChangesTableSetup *m_tableSetup;
    DiffView *m_diff;
    HistoryView *m_history;
    QStackedWidget *m_stack;
    QWidget *m_rightPane;
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
    QToolButton *m_layoutButton;
    QString m_initialSelection;
    QString m_diffSummary;
    QString m_headMessage;
    bool m_historyDirty = true;
};
