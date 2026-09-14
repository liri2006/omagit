#pragma once

#include <QDialog>
#include <QProcess>
#include <functional>

class AskPass;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTimer;

// The folder is a container: cloning creates <folder>/<editable repository name>.
// An existing destination is never reused or removed.
class CloneDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CloneDialog(const QString &folder, QWidget *parent = nullptr, bool allowOpen = false);
    ~CloneDialog() override;
    QString repositoryPath() const { return m_repositoryPath; }
    static QString defaultFolder(const QString &repositoryRoot = QString());
    static QString repositoryName(const QString &url);

public slots:
    void reject() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    using Completion = std::function<void(bool, const QByteArray &, const QString &)>;
    void applyTheme();
    void updateDestination();
    void switchSource(int index);
    void loadGitHub();
    void loadPage(int page);
    void filterRepositories();
    void signIn();
    void clone();
    void openExisting();
    void stopProcess();
    void run(const QString &program, const QStringList &args, Completion done, bool live = false);
    QString selectedUrl() const;
    QString folderPath() const;
    void setBusy(bool busy);

    AskPass *m_askPass;
    QProcess *m_process = nullptr;
    QTimer *m_timeout;
    QStackedWidget *m_sources;
    QPushButton *m_urlTab, *m_githubTab, *m_browse, *m_refresh, *m_login, *m_clone, *m_cancel, *m_open;
    QLineEdit *m_url, *m_folder, *m_search, *m_name;
    QWidget *m_destinationRow;
    QListWidget *m_repositories;
    QLabel *m_heading, *m_account, *m_destination, *m_destinationPrefix, *m_status;
    QProgressBar *m_progress;
    QString m_repositoryPath, m_cloneTarget, m_suggestedName;
    bool m_cloning = false;
    bool m_loading = false;
    bool m_timedOut = false;
};
