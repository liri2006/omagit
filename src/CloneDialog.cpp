#include "CloneDialog.h"
#include "AskPass.h"
#include "GitRepo.h"
#include "LoginDialog.h"
#include "OmarchyTheme.h"
#include "ProcessUtil.h"
#include "UiHelpers.h"

#include <QButtonGroup>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QSignalBlocker>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <memory>

QString CloneDialog::defaultFolder(const QString &repositoryRoot)
{
    return repositoryRoot.isEmpty() ? QDir::currentPath() : QFileInfo(repositoryRoot).absolutePath();
}

QString CloneDialog::repositoryName(const QString &input)
{
    const QString value = input.trimmed();
    if (value.contains(QRegularExpression(QStringLiteral("[\\s\\x00-\\x1f\\x7f]"))))
        return {};
    QString path;
    if (value.contains(QStringLiteral("://"))) {
        const QUrl url(value, QUrl::StrictMode);
        if (!url.isValid() || url.host().isEmpty() || !url.password().isEmpty()
            || url.hasQuery() || url.hasFragment()
            || (url.scheme() != QLatin1String("https") && url.scheme() != QLatin1String("ssh")))
            return {};
        path = url.path();
    } else {
        // scp-style SSH, including SSH host aliases and bracketed IPv6 hosts.
        const auto match = QRegularExpression(QStringLiteral("^(?:[^/@:]+@)?(?:\\[[^\\]]+\\]|[^/:]+):(.+)$")).match(value);
        if (!match.hasMatch() || value.startsWith(QLatin1Char('-')))
            return {};
        path = match.captured(1);
    }
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    QString name = path.section(QLatin1Char('/'), -1);
    if (name.endsWith(QLatin1String(".git")))
        name.chop(4);
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String("..")
        || name.contains(QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f/:\\\\]"))))
        return {};
    return name;
}

CloneDialog::CloneDialog(const QString &folder, QWidget *parent, bool allowOpen)
    : QDialog(parent), m_askPass(new AskPass(this)), m_timeout(new QTimer(this))
{
    setWindowTitle(tr("Clone repository"));
    setObjectName(QStringLiteral("cloneDialog"));
    setWindowModality(Qt::WindowModal);
    resize(600, 390);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 22, 24, 22);
    layout->setSpacing(12);
    m_heading = new QLabel(tr("Clone repository"));
    layout->addWidget(m_heading);
    layout->addWidget(ui::dimLabel(tr("Download a repository and open it in Omagit.")));

    auto *tabs = new QHBoxLayout;
    tabs->setSpacing(0);
    auto *group = new QButtonGroup(this);
    m_urlTab = new QPushButton(tr("URL"));
    m_githubTab = new QPushButton(tr("GitHub"));
    m_urlTab->setObjectName(QStringLiteral("cloneUrlTab"));
    m_githubTab->setObjectName(QStringLiteral("cloneGitHubTab"));
    for (QPushButton *button : {m_urlTab, m_githubTab}) {
        button->setCheckable(true);
        group->addButton(button);
        tabs->addWidget(button);
    }
    m_urlTab->setChecked(true);
    layout->addLayout(tabs);
    connect(m_urlTab, &QPushButton::clicked, this, [this] { switchSource(0); });
    connect(m_githubTab, &QPushButton::clicked, this, [this] { switchSource(1); });

    m_sources = new QStackedWidget;
    auto *urlPage = new QWidget;
    auto *urlLayout = new QVBoxLayout(urlPage);
    urlLayout->setContentsMargins(0, 0, 0, 0);
    auto *urlCaption = ui::sectionLabel(tr("Repository URL"));
    urlLayout->addWidget(urlCaption);
    m_url = new QLineEdit;
    m_url->setObjectName(QStringLiteral("cloneUrl"));
    m_url->setPlaceholderText(QStringLiteral("https://github.com/owner/repo.git"));
    m_url->setAccessibleName(tr("Repository URL"));
    urlCaption->setBuddy(m_url);
    urlLayout->addWidget(m_url);
    urlLayout->addWidget(ui::dimLabel(tr("HTTPS or SSH · git@github.com:owner/repo.git")));
    urlLayout->addStretch();
    m_sources->addWidget(urlPage);

    auto *githubPage = new QWidget;
    auto *githubLayout = new QVBoxLayout(githubPage);
    githubLayout->setContentsMargins(0, 0, 0, 0);
    auto *accountRow = new QHBoxLayout;
    m_account = ui::dimLabel();
    m_account->setTextFormat(Qt::PlainText);
    m_account->setWordWrap(true);
    m_refresh = new QPushButton(tr("Refresh"));
    m_refresh->setObjectName(QStringLiteral("cloneRefresh"));
    accountRow->addWidget(m_account, 1);
    accountRow->addWidget(m_refresh);
    githubLayout->addLayout(accountRow);
    m_login = new QPushButton(tr("Sign in to GitHub"));
    m_login->setObjectName(QStringLiteral("cloneLogin"));
    githubLayout->addWidget(m_login, 0, Qt::AlignLeft);
    m_login->hide();
    m_search = new QLineEdit;
    m_search->setObjectName(QStringLiteral("cloneSearch"));
    m_search->setPlaceholderText(tr("Filter repositories…"));
    m_search->setAccessibleName(tr("Filter repositories"));
    m_search->installEventFilter(this);
    githubLayout->addWidget(m_search);
    m_repositories = new QListWidget;
    m_repositories->setObjectName(QStringLiteral("cloneRepositories"));
    m_repositories->setAccessibleName(tr("GitHub repositories"));
    m_repositories->setMinimumHeight(130);
    githubLayout->addWidget(m_repositories, 1);
    m_sources->addWidget(githubPage);
    m_sources->setFixedHeight(120);
    layout->addWidget(m_sources, 1);

    layout->addWidget(ui::hairline());
    auto *folderCaption = ui::sectionLabel(tr("Destination folder"));
    layout->addWidget(folderCaption);
    auto *folderRow = new QHBoxLayout;
    m_folder = new QLineEdit(folder);
    m_folder->setObjectName(QStringLiteral("cloneFolder"));
    m_folder->setAccessibleName(tr("Destination folder"));
    folderCaption->setBuddy(m_folder);
    m_browse = new QPushButton(tr("Browse…"));
    folderRow->addWidget(m_folder, 1);
    folderRow->addWidget(m_browse);
    layout->addLayout(folderRow);
    m_destinationRow = new QWidget;
    auto *destinationRow = new QHBoxLayout(m_destinationRow);
    destinationRow->setContentsMargins(0, 0, 0, 0);
    destinationRow->setSpacing(2);
    m_destinationPrefix = ui::dimLabel();
    m_destinationPrefix->setObjectName(QStringLiteral("cloneDestinationPrefix"));
    m_destinationPrefix->setTextFormat(Qt::PlainText);
    m_destinationPrefix->setWordWrap(false);
    m_name = new QLineEdit;
    m_name->setObjectName(QStringLiteral("cloneName"));
    m_name->setAccessibleName(tr("Repository folder name"));
    m_name->setToolTip(tr("Edit the name of the folder created for this repository"));
    m_name->setMinimumWidth(120);
    m_destinationPrefix->setBuddy(m_name);
    destinationRow->addWidget(m_destinationPrefix);
    destinationRow->addWidget(m_name, 1);
    layout->addWidget(m_destinationRow);
    m_destination = ui::dimLabel();
    m_destination->setObjectName(QStringLiteral("cloneDestination"));
    m_destination->setTextFormat(Qt::PlainText);
    m_destination->setWordWrap(true);
    layout->addWidget(m_destination);
    m_progress = new QProgressBar;
    m_progress->setRange(0, 0);
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(3);
    m_progress->hide();
    layout->addWidget(m_progress);
    m_status = ui::dimLabel();
    m_status->setObjectName(QStringLiteral("cloneStatus"));
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    layout->addWidget(m_status);
    auto *buttons = new QHBoxLayout;
    m_open = new QPushButton(tr("Open existing…"));
    m_open->setVisible(allowOpen);
    buttons->addWidget(m_open);
    buttons->addStretch();
    m_cancel = new QPushButton(tr("Cancel"));
    m_clone = new QPushButton(tr("Clone && open"));
    m_clone->setObjectName(QStringLiteral("cloneAccept"));
    buttons->addWidget(m_cancel);
    buttons->addWidget(m_clone);
    layout->addLayout(buttons);
    for (auto *button : findChildren<QPushButton *>()) {
        button->setAutoDefault(false);
        button->setCursor(Qt::PointingHandCursor);
    }
    m_clone->setDefault(true);
    connect(m_cancel, &QPushButton::clicked, this, &CloneDialog::reject);
    connect(m_clone, &QPushButton::clicked, this, &CloneDialog::clone);
    connect(m_open, &QPushButton::clicked, this, &CloneDialog::openExisting);
    connect(m_refresh, &QPushButton::clicked, this, &CloneDialog::loadGitHub);
    connect(m_login, &QPushButton::clicked, this, &CloneDialog::signIn);
    connect(m_url, &QLineEdit::textChanged, this, &CloneDialog::updateDestination);
    connect(m_folder, &QLineEdit::textChanged, this, &CloneDialog::updateDestination);
    connect(m_name, &QLineEdit::textChanged, this, &CloneDialog::updateDestination);
    connect(m_search, &QLineEdit::textChanged, this, &CloneDialog::filterRepositories);
    connect(m_repositories, &QListWidget::currentRowChanged, this, &CloneDialog::updateDestination);
    connect(m_browse, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Destination folder"), folderPath());
        if (!dir.isEmpty())
            m_folder->setText(dir);
    });
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, [this] {
        m_timedOut = true;
        if (m_process)
            m_process->kill();
    });
    connect(m_askPass, &AskPass::requestReceived, this, [this](const AskPassRequest &request) {
        m_timeout->stop();
        auto *dialog = new LoginDialog(request, nullptr, this);
        connect(dialog, &QDialog::accepted, m_askPass, [this, dialog, request] {
            if (request.kind == AskPassRequest::Username || request.kind == AskPassRequest::Password)
                m_askPass->answerLogin(request.id, dialog->username(), dialog->password());
            else
                m_askPass->answerSecret(request.id, dialog->password());
        });
        connect(dialog, &QDialog::rejected, m_askPass, [this, request] { m_askPass->cancel(request.id); });
        connect(m_askPass, &AskPass::requestDropped, dialog, [this, dialog, request](int id) {
            if (id == request.id) {
                disconnect(dialog, nullptr, m_askPass, nullptr);
                dialog->close();
            }
        });
        dialog->show();
    });
    connect(m_askPass, &AskPass::answered, this, [this] { m_timeout->start(); });
    connect(OmarchyTheme::instance(), &OmarchyTheme::changed, this, &CloneDialog::applyTheme);
    applyTheme();
    updateDestination();
    m_url->setFocus();
}

CloneDialog::~CloneDialog() { stopProcess(); }

void CloneDialog::applyTheme()
{
    const auto *theme = OmarchyTheme::instance();
    m_heading->setFont(theme->titleFont());
    for (auto *edit : {m_url, m_folder, m_search}) {
        edit->setFont(theme->uiFont());
        edit->setMinimumHeight(QFontMetrics(theme->titleFont()).height() + 18);
    }
    m_name->setFont(theme->captionFont());
    for (auto *label : {m_account, m_destination, m_destinationPrefix, m_status})
        label->setFont(theme->captionFont());
    m_repositories->setFont(theme->uiFont());
}

QString CloneDialog::folderPath() const
{
    QString path = m_folder->text().trimmed();
    if (path == QLatin1String("~"))
        path = QDir::homePath();
    else if (path.startsWith(QLatin1String("~/")))
        path = QDir::home().filePath(path.mid(2));
    return path.isEmpty() ? QString() : QDir(path).absolutePath();
}

QString CloneDialog::selectedUrl() const
{
    if (m_sources->currentIndex() == 0)
        return m_url->text().trimmed();
    const auto *item = m_repositories->currentItem();
    return item && !item->isHidden() ? item->data(Qt::UserRole).toString() : QString();
}

void CloneDialog::updateDestination()
{
    const QString suggestedName = repositoryName(selectedUrl());
    if (suggestedName != m_suggestedName) {
        // Follow the URL/selection until the user chooses their own folder name.
        if (m_name->text() == m_suggestedName) {
            const QSignalBlocker blocker(m_name);
            m_name->setText(suggestedName);
        }
        m_suggestedName = suggestedName;
    }
    const QString name = m_name->text();
    const QString folder = folderPath();
    m_destinationRow->setVisible(!suggestedName.isEmpty() && !folder.isEmpty());
    QString prefix = ui::tildePath(folder);
    if (!prefix.endsWith(QLatin1Char('/')))
        prefix += QLatin1Char('/');
    const QString preview = tr("Creates %1").arg(prefix);
    m_destinationPrefix->setText(QFontMetrics(m_destinationPrefix->font()).elidedText(preview, Qt::ElideMiddle, 350));
    m_destinationPrefix->setToolTip(preview);
    QString message;
    bool valid = false;
    if (suggestedName.isEmpty())
        message = m_sources->currentIndex() == 0 ? tr("Enter an HTTPS or SSH repository URL.") : tr("Choose a repository to clone.");
    else if (name.isEmpty() || name != name.trimmed() || name == QLatin1String(".") || name == QLatin1String("..")
             || name.contains(QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f/\\\\]"))))
        message = tr("Enter a folder name without slashes or leading or trailing spaces.");
    else if (folder.isEmpty() || !QFileInfo(folder).isDir())
        message = tr("Choose an existing destination folder.");
    else {
        const QString path = QDir(folder).filePath(name);
        const QFileInfo info(path);
        if (info.exists() || info.isSymLink())
            message = tr("Already exists: %1. Change the name or destination folder.").arg(ui::tildePath(path));
        else {
            valid = QFileInfo(folder).isWritable();
            message = valid ? QString() : tr("This folder is not writable.");
        }
    }
    m_destination->setText(message);
    m_destination->setVisible(!message.isEmpty());
    m_clone->setEnabled(valid && !m_process && !m_cloning && !m_loading);
}

void CloneDialog::setBusy(bool busy)
{
    m_progress->setVisible(busy);
    for (auto *widget : QList<QWidget *>{m_sources, m_folder, m_name, m_browse, m_open})
        widget->setEnabled(!busy);
    m_urlTab->setEnabled(!m_cloning);
    m_githubTab->setEnabled(!m_cloning);
    m_cancel->setText(busy ? tr("Stop") : tr("Cancel"));
    updateDestination();
}

void CloneDialog::stopProcess()
{
    m_timeout->stop();
    if (m_process) {
        abandonProcess(m_process, this);
        m_process->deleteLater();
        m_process = nullptr;
    }
    m_askPass->endOperation();
}

void CloneDialog::reject()
{
    if (m_process) {
        stopProcess();
        if (m_loading)
            m_repositories->clear();
        m_cloning = m_loading = false;
        setBusy(false);
        m_status->setText(m_cloneTarget.isEmpty() ? tr("Stopped.")
            : tr("Clone stopped. Any partial download remains at %1.").arg(ui::tildePath(m_cloneTarget)));
        return;
    }
    QDialog::reject();
}

void CloneDialog::run(const QString &program, const QStringList &args, Completion done, bool live)
{
    auto *process = new QProcess(this);
    m_process = process;
    m_timedOut = false;
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("GH_PROMPT_DISABLED"), QStringLiteral("1"));
    env.insert(QStringLiteral("GH_PAGER"), QStringLiteral("cat"));
    env.insert(QStringLiteral("NO_COLOR"), QStringLiteral("1"));
    env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    if (m_cloning && m_askPass->listen()) {
        for (const QString &entry : m_askPass->env()) {
            const int eq = entry.indexOf(QLatin1Char('='));
            env.insert(entry.left(eq), entry.mid(eq + 1));
        }
    }
    process->setProcessEnvironment(env);
    process->setWorkingDirectory(m_cloning ? folderPath() : QDir::currentPath());
    auto output = std::make_shared<QByteArray>();
    auto error = std::make_shared<QByteArray>();
    connect(process, &QProcess::readyReadStandardOutput, this, [process, output] { output->append(process->readAllStandardOutput()); });
    connect(process, &QProcess::readyReadStandardError, this, [this, process, error, live] {
        error->append(process->readAllStandardError());
        if (error->size() > 16384)
            *error = error->right(16384);
        if (live) {
            const QString text = QString::fromUtf8(*error).trimmed();
            const QStringList lines = text.split(QRegularExpression(QStringLiteral("[\\r\\n]")), Qt::SkipEmptyParts);
            m_status->setText(m_cloning ? lines.value(lines.size() - 1) : text);
        }
    });
    auto finish = [this, process, output, error, done](bool ok) {
        m_timeout->stop();
        output->append(process->readAllStandardOutput());
        error->append(process->readAllStandardError());
        m_process = nullptr;
        process->deleteLater();
        QString detail = m_timedOut ? tr("The request timed out. Try again.") : QString::fromUtf8(*error).trimmed();
        if (!ok && detail.isEmpty())
            detail = process->errorString();
        done(ok, *output, detail);
    };
    connect(process, &QProcess::finished, this, [finish](int code, QProcess::ExitStatus status) { finish(code == 0 && status == QProcess::NormalExit); });
    connect(process, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError error) { if (error == QProcess::FailedToStart) finish(false); });
    setBusy(true);
    process->start(program, args);
    m_timeout->start(live ? 15 * 60 * 1000 : 60000);
}

void CloneDialog::switchSource(int index)
{
    if (m_cloning)
        return;
    stopProcess();
    m_loading = false;
    m_sources->setCurrentIndex(index);
    m_sources->setFixedHeight(index == 0 ? 120 : 250);
    resize(width(), index == 0 ? 390 : 540);
    m_status->clear();
    setBusy(false);
    if (index == 1)
        loadGitHub();
    else
        m_url->setFocus();
}

void CloneDialog::loadGitHub()
{
    if (m_process)
        return;
    m_cloneTarget.clear();
    m_repositories->clear();
    m_login->hide();
    m_search->hide();
    m_repositories->hide();
    m_status->clear();
    if (QStandardPaths::findExecutable(QStringLiteral("gh")).isEmpty()) {
        m_account->setText(tr("Install GitHub CLI (gh) to browse your repositories."));
        updateDestination();
        return;
    }
    m_loading = true;
    m_account->setText(tr("Checking GitHub account…"));
    run(QStringLiteral("gh"), {QStringLiteral("api"), QStringLiteral("--hostname"), QStringLiteral("github.com"), QStringLiteral("user")},
        [this](bool ok, const QByteArray &out, const QString &error) {
        m_loading = false;
        const QString login = QJsonDocument::fromJson(out).object().value(QStringLiteral("login")).toString();
        if (!ok || login.isEmpty()) {
            m_account->setText(tr("Sign in to see your personal, organization and shared repositories."));
            m_login->show();
            m_status->setText(error.left(1500));
            setBusy(false);
            return;
        }
        m_account->setText(tr("Signed in as %1").arg(login));
        m_search->show();
        m_repositories->show();
        m_loading = true;
        loadPage(1);
    });
}

void CloneDialog::loadPage(int page)
{
    m_status->setText(tr("Loading repositories…"));
    run(QStringLiteral("gh"), {QStringLiteral("api"), QStringLiteral("--hostname"), QStringLiteral("github.com"),
        QStringLiteral("user/repos?per_page=100&sort=updated&affiliation=owner,collaborator,organization_member&page=%1").arg(page)},
        [this, page](bool ok, const QByteArray &out, const QString &error) {
        const auto doc = QJsonDocument::fromJson(out);
        if (!ok || !doc.isArray()) {
            m_loading = false;
            // Do not present an incomplete list as all available repositories.
            m_repositories->clear();
            setBusy(false);
            m_status->setText(tr("Could not load repositories. Refresh to retry.\n%1").arg(error.left(1500)));
            return;
        }
        const auto repos = doc.array();
        for (const auto &value : repos) {
            const auto repo = value.toObject();
            const QString url = repo.value(QStringLiteral("clone_url")).toString();
            const QString name = repo.value(QStringLiteral("full_name")).toString();
            if (name.isEmpty() || repositoryName(url).isEmpty() || QUrl(url).host() != QLatin1String("github.com"))
                continue;
            auto *item = new QListWidgetItem(name + (repo.value(QStringLiteral("private")).toBool() ? tr("   ·   private") : QString()), m_repositories);
            item->setData(Qt::UserRole, url);
            item->setToolTip(repo.value(QStringLiteral("description")).toString());
        }
        if (repos.size() == 100) {
            loadPage(page + 1);
            return;
        }
        m_loading = false;
        setBusy(false);
        filterRepositories();
    });
}

bool CloneDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_search && event->type() == QEvent::KeyPress && m_search->isEnabled()) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->modifiers() == Qt::NoModifier && (key->key() == Qt::Key_Down || key->key() == Qt::Key_Up)) {
            QList<QListWidgetItem *> visible;
            for (int i = 0; i < m_repositories->count(); ++i) {
                auto *item = m_repositories->item(i);
                if (!item->isHidden())
                    visible.append(item);
            }
            if (!visible.isEmpty()) {
                // Match the branch picker's wrapping navigation, keeping
                // focus in the search field so typing can refine the filter.
                const int at = visible.indexOf(m_repositories->currentItem());
                const int step = key->key() == Qt::Key_Down ? 1 : -1;
                const int next = at < 0 ? (step > 0 ? 0 : visible.size() - 1)
                                       : (at + step + visible.size()) % visible.size();
                m_repositories->setCurrentItem(visible.at(next));
                m_repositories->scrollToItem(visible.at(next));
            }
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void CloneDialog::filterRepositories()
{
    int count = 0;
    for (int i = 0; i < m_repositories->count(); ++i) {
        auto *item = m_repositories->item(i);
        item->setHidden(!item->text().contains(m_search->text().trimmed(), Qt::CaseInsensitive));
        if (!item->isHidden())
            ++count;
    }
    if (!m_loading && !m_process)
        m_status->setText(m_repositories->count() == 0 ? tr("No repositories available for this account.")
            : count == 0 ? tr("No repositories match your filter.") : count == 1 ? tr("1 repository") : tr("%n repositories", nullptr, count));
    updateDestination();
}

void CloneDialog::signIn()
{
    m_loading = true;
    m_status->setText(tr("Complete sign-in in your browser. A one-time code will appear here."));
    run(QStringLiteral("gh"), {QStringLiteral("auth"), QStringLiteral("login"), QStringLiteral("--hostname"),
        QStringLiteral("github.com"), QStringLiteral("--git-protocol"), QStringLiteral("https"), QStringLiteral("--web")},
        [this](bool ok, const QByteArray &, const QString &error) {
        m_loading = false;
        setBusy(false);
        if (ok)
            loadGitHub();
        else
            m_status->setText(tr("Sign-in did not finish. Try again.\n%1").arg(error.left(1500)));
    }, true);
    // gh may ask for Enter before opening the browser, even with --web.
    m_process->write("\n");
    m_process->closeWriteChannel();
}

void CloneDialog::clone()
{
    updateDestination();
    if (!m_clone->isEnabled())
        return;
    const QString url = selectedUrl();
    m_cloneTarget = QDir(folderPath()).filePath(m_name->text());
    QStringList args{QStringLiteral("clone")};
    if (m_sources->currentIndex() == 1) {
        // clone -c saves the helper in the new repository before fetching.
        // Later fetch/pull/push operations reuse gh without storing a token
        // here or changing the user's global Git configuration.
        args << QStringLiteral("-c") << QStringLiteral("credential.https://github.com.helper=")
             << QStringLiteral("-c") << QStringLiteral("credential.https://github.com.helper=!gh auth git-credential");
    }
    args << QStringLiteral("--progress") << QStringLiteral("--") << url << m_cloneTarget;
    m_cloning = true;
    m_status->setText(tr("Cloning %1…").arg(repositoryName(url)));
    run(QStringLiteral("git"), args, [this](bool ok, const QByteArray &, const QString &error) {
        const bool cancelled = m_askPass->cancelled();
        m_askPass->endOperation();
        m_cloning = false;
        setBusy(false);
        if (!ok) {
            m_status->setText(cancelled ? tr("Clone cancelled — not signed in.") : tr("Clone failed.\n%1").arg(error.right(1500)));
            return;
        }
        m_repositoryPath = m_cloneTarget;
        accept();
    }, true);
}

void CloneDialog::openExisting()
{
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Open repository"), folderPath());
    if (folder.isEmpty())
        return;
    m_repositoryPath = GitRepo::findRoot(folder);
    if (m_repositoryPath.isEmpty())
        m_status->setText(tr("That folder is not inside a Git repository."));
    else
        accept();
}
