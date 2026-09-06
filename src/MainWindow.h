#pragma once

#include "ChangesModel.h"
#include "GitRepo.h"

#include <QMainWindow>

class DiffView;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSortFilterProxyModel;
class QTableView;
class QCheckBox;
class QToolButton;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(GitRepo *repo, QWidget *parent = nullptr);

public slots:
    void refresh();
    void setInitialSelection(const QString &path) { m_initialSelection = path; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onCurrentRowChanged(const QModelIndex &current);
    void onCheckedChanged();
    void commit();
    void openInEditor();
    void applyTheme();

private:
    void buildUi();
    void showDiffFor(const FileChange &change);
    FileChange currentChange(bool *ok) const;

    GitRepo *m_repo;
    ChangesModel *m_model;
    QSortFilterProxyModel *m_proxy;
    QTableView *m_table;
    DiffView *m_diff;
    QPlainTextEdit *m_message;
    QLabel *m_branchLabel;
    QLabel *m_summaryLabel;
    QLabel *m_changeLabel;
    QPushButton *m_commitButton;
    QCheckBox *m_selectAll;
    QCheckBox *m_showUnversioned;
    QToolButton *m_prevButton;
    QToolButton *m_nextButton;
    QString m_initialSelection;
    QString m_diffSummary;
};
