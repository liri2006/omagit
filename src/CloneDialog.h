#pragma once

#include "AskPass.h"
#include "SshKeys.h"

#include <QDialog>
#include <QProcess>
#include <QString>
#include <functional>

class BranchPicker;
class QBoxLayout;
class QFrame;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QSpacerItem;
class QStackedWidget;
class QTimer;
class QToolButton;
class QVBoxLayout;

// The folder is a container: cloning creates <folder>/<editable repository name>.
// An existing destination is never reused or removed.
class CloneDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CloneDialog(const QString &folder, QWidget *parent = nullptr, bool allowOpen = false);
    ~CloneDialog() override;
    QString repositoryPath() const { return m_repositoryPath; }
    // Whether repositoryPath() is a clone made just now, and not an existing
    // repository the dialog was asked to open.
    bool cloned() const { return m_cloned; }
    // The ssh key the clone signs in with, saved as the new repository's
    // core.sshCommand (sshkeys); empty for ssh's own choice. The field for it
    // shows for an ssh URL when ~/.ssh holds a key ssh would not offer by
    // itself — more than one pair, or one under a name of its own — and from
    // the moment a clone failed on a key the server turned down.
    QString sshKey() const { return m_keyPath; }
    void setSshKey(const QString &path);
    // The sign-ins of that clone the user asked to have remembered, for
    // whoever opens it to hand to CredentialKeeper.
    QList<KeptLogin> loginsToKeep() const { return m_loginsToKeep; }
    // And the keys the user asked to keep unlocked (AgentKeeper).
    QList<AgentKey> keysToUnlock() const { return m_keysToUnlock; }
    static QString defaultFolder(const QString &repositoryRoot = QString());
    static QString repositoryName(const QString &url);

public slots:
    void reject() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    using Completion = std::function<void(bool, const QByteArray &, const QString &)>;
    // What the GitHub page can say about the account it is browsing; it
    // decides the account line and what stands in for the list.
    enum class GitHub { Checking, Loading, SignedOut, NoTool, Ready };

    void applyTheme();
    void updateDestination();
    void userEdited();
    void switchSource(int index);
    void updateSourcePolicies();
    void loadGitHub();
    void loadPage(int page);
    void filterRepositories();
    void updateListArea();
    void updateListHeight();
    void signIn();
    void clone();
    void openExisting();
    void stopProcess();
    void run(const QString &program, const QStringList &args, Completion done, bool live = false);
    QString selectedUrl() const;
    QString folderPath() const;
    void setBusy(bool busy);
    void setStatus(const QString &text, bool alert = false);
    void updateMessage();
    // The window keeps its width and takes the height of its content, so a
    // second line of message or a taller list moves nothing else about.
    void fitToContent();
    void refit();
    // Shows the ssh key field where it applies (sshKey()), and says what it holds.
    void updateKeyRow();
    void showKeyMenu();
    bool keyApplies() const;

    AskPass *m_askPass;
    QProcess *m_process = nullptr;
    QTimer *m_timeout;
    QStackedWidget *m_sources, *m_listArea;
    QFrame *m_placeholderPage;
    QPushButton *m_urlTab, *m_githubTab, *m_browse, *m_login, *m_clone, *m_cancel, *m_open;
    QToolButton *m_refresh;
    QLineEdit *m_url, *m_folder, *m_search, *m_name;
    QListWidget *m_repositories;
    QLabel *m_heading, *m_account, *m_count, *m_placeholder, *m_destinationPrefix, *m_message;
    QLabel *m_subtitle, *m_urlCaption, *m_folderCaption;
    // The layouts whose gaps applyTheme() puts on the grid.
    QVBoxLayout *m_urlLayout, *m_githubLayout, *m_placeholderLayout, *m_destinationLayout, *m_statusLayout;
    QHBoxLayout *m_accountRow, *m_folderRow, *m_destinationRow, *m_buttonRow;
    QSpacerItem *m_urlHintGap, *m_destinationGap; // an item gap under a captioned field
    QProgressBar *m_progress;
    // The ssh key field under the URL: its caption, the picker, and a note
    // when an exported GIT_SSH_COMMAND overrides it.
    QWidget *m_keyRow;
    QVBoxLayout *m_keyLayout;
    QLabel *m_keyCaption, *m_keyNote;
    BranchPicker *m_keyPicker;
    QString m_keyPath, m_keyNoteColor;
    QList<sshkeys::Key> m_keys;
    bool m_keyNeeded = false; // a clone failed on a refused key: the field stays on
    QString m_repositoryPath, m_cloneTarget, m_suggestedName;
    // The hint the destination fields ask for, the last word from a command,
    // and the colour the message label carries because of it.
    QString m_hint, m_status, m_messageColor, m_accountName;
    GitHub m_github = GitHub::Checking;
    int m_visible = 0; // repositories the filter leaves on the list
    bool m_statusIsAlert = false;
    bool m_cloned = false;
    QList<KeptLogin> m_loginsToKeep;
    QList<AgentKey> m_keysToUnlock;
    bool m_refitPending = false;
    bool m_cloning = false;
    bool m_loading = false;
    bool m_timedOut = false;
};
