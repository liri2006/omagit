#pragma once

#include "ChangesModel.h"
#include "CommitMessageAgent.h"
#include "GitRepo.h"

#include <QWidget>

class MessageEdit;
class QCheckBox;
class QLabel;
class QPushButton;
class QSortFilterProxyModel;
class QTableView;
class QTimer;
class QToolButton;

// The commit dialog: message, changes list, options, buttons.
// It owns the coding-agent flow that writes the message; everything that
// needs the repository as a whole (committing, refreshing, the diff pane)
// is left to the window through the signals below.
class CommitPage : public QWidget
{
    Q_OBJECT
public:
    explicit CommitPage(GitRepo *repo, QWidget *parent = nullptr);

    ChangesModel *model() const { return m_model; }
    QSortFilterProxyModel *proxy() const { return m_proxy; }
    QTableView *table() const { return m_table; }
    MessageEdit *message() const { return m_message; }
    QCheckBox *amendBox() const { return m_amend; }
    QCheckBox *selectAllBox() const { return m_selectAll; }
    QCheckBox *showUnversionedBox() const { return m_showUnversioned; }
    QPushButton *commitButton() const { return m_commitButton; }

    // The row the file actions apply to, independent of the checked files.
    FileChange currentChange(bool *ok) const;
    // Checks every file for the commit, or none when all are checked (lazygit's "a").
    void toggleAllChecked();
    void toggleAmend();
    // Ticks the files of the commit being amended (after the refresh that follows).
    void checkHeadPaths();
    // Whether the diff pane shows, for what a double-click on a file does.
    void setDiffPaneVisible(bool on) { m_diffPaneVisible = on; }

    void applyTheme();

public slots:
    void onCheckedChanged();
    void onAmendToggled(bool on);
    // Asks the chosen coding agent for a message describing the checked
    // changes (Ctrl+G); clicking again while it runs stops it.
    void generateMessage();
    void showAgentMenu();

signals:
    void commitRequested();
    void refreshRequested();
    void openRequested();          // the file in its own program
    void showDiffPaneRequested();
    void discardRequested(const FileChange &change);
    void currentRowChanged(const QModelIndex &current);
    void amendToggled(bool on);
    void modeRequested();          // bring the commit view forward
    void statusMessage(const QString &text, int ms);

private:
    void setGenerating(bool on);
    void onMessageGenerated(bool ok, const QString &text);

    GitRepo *m_repo;
    ChangesModel *m_model;
    QSortFilterProxyModel *m_proxy;
    QTableView *m_table;
    ChangesTableSetup *m_tableSetup;
    MessageEdit *m_message;      // the commit message, with the generate button in its corner
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
    bool m_diffPaneVisible = true;
};
