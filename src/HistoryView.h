#pragma once

#include "GitRepo.h"

#include <QWidget>

class ChangesModel;
class ChangesTableSetup;
class HistoryModel;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSortFilterProxyModel;
class QTableView;
class QToolButton;

// The "History" side of the window: a filterable commit list with a lane graph
// and ref labels, the selected commit's details, and the files it touched.
class HistoryView : public QWidget
{
    Q_OBJECT
public:
    explicit HistoryView(GitRepo *repo, QWidget *parent = nullptr);

    void reload();
    void applyTheme();
    void focusFilter();

    Commit currentCommit(bool *ok) const;
    // The file selected in the files list of the current commit.
    bool currentFile(Commit *commit, FileChange *change) const;
    // What the diff pane should say when currentFile() is false.
    QString emptyMessage() const;
    // The files list of the current commit (model + selection shared with the Mini rail).
    QTableView *filesTable() const { return m_filesTable; }

signals:
    void currentFileChanged();
    void refreshRequested();

private slots:
    void onCommitChanged();
    void onFilterChanged();
    void loadMore();
    void showContextMenu(const QPoint &pos);

private:
    void updateFooter();
    void selectFirstCommit();
    void fitColumns();

    GitRepo *m_repo;
    HistoryModel *m_model;
    QSortFilterProxyModel *m_proxy;
    QTableView *m_table;
    QLineEdit *m_filter;
    QToolButton *m_allRefs;
    QLabel *m_countLabel;
    QToolButton *m_moreButton;
    QPlainTextEdit *m_details;
    ChangesModel *m_files;
    QTableView *m_filesTable;
    ChangesTableSetup *m_filesSetup;
    QString m_emptyMessage;
    QString m_pendingHash; // commit to select after a reload
    QString m_pendingFile; // ... and the file to select in it
    bool m_reloading = false; // the model reset momentarily leaves no commit current
};
