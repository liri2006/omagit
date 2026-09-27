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
#include <QFrame>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QShowEvent>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QSignalBlocker>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <memory>

namespace {
// As wide as the merge view: its list of repositories and the folder row with
// its Browse button both want the room. Everything else is the grid's
// (Grid.h), as the merge view and the sign-in dialog have it: a dialog's 16
// of padding, its groups a group gap apart, controls an item gap apart,
// captions 4 over their 28 px fields, the list's rows 24 with their text 8 in.
// A narrower window gets a narrower dialog (ui::fitDialogWidth()).
constexpr int kDialogWidth = 640;
// The list area is this many rows tall, whichever page it shows, and the list
// scrolls beyond them.
constexpr int kRepositoryRows = 7;
// The name field takes the row after the folder it goes into, and never less
// than this: the folder shortens before the name field does.
constexpr int kNameMinWidth = 220;
// The idle progress bar's line, always there so a command starting moves
// nothing.
constexpr int kProgressHeight = 4;
// How much of a command's output the message line repeats: the bytes first,
// then the lines that carry the reason.
constexpr int kMessageBytes = 1500, kMessageLines = 4;
// Wide and tall enough for any text a label of the dialog is measured with.
constexpr int kMeasureLimit = 10000;

// A colour set by hand on a label, remembered so that the same colour twice
// does not re-polish the widget. An invalid colour hands the label back to
// the stylesheet's own dim rule.
void setTextColor(QLabel *label, QString *applied, const QColor &color)
{
    const QString sheet = color.isValid() ? QStringLiteral("color: %1;").arg(color.name()) : QString();
    if (*applied == sheet)
        return;
    *applied = sheet;
    label->setStyleSheet(sheet);
}

// The few lines of a command's output worth repeating: gh explains itself in
// its first lines, `git clone` in its last — and while it runs, only the
// last one, where its progress is.
QString fewLines(const QString &output, bool fromEnd, int count = kMessageLines)
{
    const QStringList lines = output.left(kMessageBytes).trimmed()
        .split(QRegularExpression(QStringLiteral("[\\r\\n]")), Qt::SkipEmptyParts);
    const QStringList kept = fromEnd ? lines.mid(qMax(0, lines.size() - count)) : lines.mid(0, count);
    return kept.join(QLatin1Char('\n'));
}

// "Creates ~/Projects/" beside the name field: as wide as its text so the
// name field sits right after it, yet ready to give width back — it paints as
// much of the path as the row leaves, so a deep folder shortens instead of
// pushing the name field off the dialog.
class ElidedLabel : public QLabel
{
public:
    explicit ElidedLabel()
    {
        setObjectName(QStringLiteral("dimLabel"));
        setFont(OmarchyTheme::instance()->captionFont());
        setTextFormat(Qt::PlainText);
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    }

    // May shrink to nothing; as tall as one line.
    QSize minimumSizeHint() const override { return QSize(0, QLabel::sizeHint().height()); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        const QString shown = fontMetrics().elidedText(text(), Qt::ElideMiddle, contentsRect().width());
        style()->drawItemText(&painter, contentsRect(), alignment() | Qt::TextSingleLine, palette(),
                              isEnabled(), shown, foregroundRole());
    }
};
} // namespace

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
            || (url.scheme() != QLatin1String("https") && url.scheme() != QLatin1String("http")
                && url.scheme() != QLatin1String("ssh")))
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
    setSizeGripEnabled(false);
    // The groups, top to bottom, a group gap apart (applyTheme() scales every
    // gap): the heading and its line, the source tabs, the source, a rule,
    // the destination, the progress and the message, the buttons.
    auto *layout = new QVBoxLayout(this);
    auto *head = new QVBoxLayout;
    head->setContentsMargins(0, 0, 0, 0);
    head->setSpacing(0);
    m_heading = new QLabel(tr("Clone repository"));
    head->addWidget(m_heading);
    m_subtitle = ui::dimLabel(tr("Download a repository and open it in Omagit."));
    head->addWidget(m_subtitle);
    layout->addLayout(head);

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
    m_sources->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *urlPage = new QWidget;
    auto *urlLayout = m_urlLayout = new QVBoxLayout(urlPage);
    urlLayout->setContentsMargins(0, 0, 0, 0);
    auto *urlCaption = m_urlCaption = ui::sectionLabel(tr("Repository URL"));
    urlLayout->addWidget(urlCaption);
    m_url = new QLineEdit;
    m_url->setObjectName(QStringLiteral("cloneUrl"));
    m_url->setPlaceholderText(QStringLiteral("https://github.com/owner/repo.git"));
    m_url->setAccessibleName(tr("Repository URL"));
    urlCaption->setBuddy(m_url);
    urlLayout->addWidget(m_url);
    // An item gap under the field, with the layout's caption gap before it.
    m_urlHintGap = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Fixed);
    urlLayout->addItem(m_urlHintGap);
    urlLayout->addWidget(ui::dimLabel(tr("HTTPS, HTTP or SSH · git@github.com:owner/repo.git")));
    m_sources->addWidget(urlPage);

    // The GitHub page keeps its three rows whatever it has to say, so it
    // never re-shapes itself between one account and the next.
    auto *githubPage = new QWidget;
    auto *githubLayout = m_githubLayout = new QVBoxLayout(githubPage);
    githubLayout->setContentsMargins(0, 0, 0, 0);
    auto *accountRow = m_accountRow = new QHBoxLayout;
    m_account = ui::dimLabel();
    m_account->setTextFormat(Qt::PlainText);
    m_account->setAccessibleName(tr("GitHub account"));
    m_count = ui::dimLabel();
    m_count->setTextFormat(Qt::PlainText);
    m_count->setAccessibleName(tr("Repository count"));
    m_refresh = ui::iconButton(ui::kRefresh, QStringLiteral("↻"), tr("Refresh the list"),
                               ui::IconButtonSize::Toolbar, false);
    m_refresh->setAccessibleName(tr("Refresh"));
    accountRow->addWidget(m_account, 1);
    accountRow->addWidget(m_count);
    accountRow->addWidget(m_refresh);
    githubLayout->addLayout(accountRow);
    m_search = new QLineEdit;
    m_search->setObjectName(QStringLiteral("cloneSearch"));
    m_search->setPlaceholderText(tr("Filter repositories…"));
    m_search->setAccessibleName(tr("Filter repositories"));
    m_search->installEventFilter(this);
    githubLayout->addWidget(m_search);

    // One slot of fixed height: the list, or a sentence about why there is
    // none to show.
    m_listArea = new QStackedWidget;
    m_repositories = new QListWidget;
    m_repositories->setObjectName(QStringLiteral("cloneRepositories"));
    m_repositories->setAccessibleName(tr("GitHub repositories"));
    m_listArea->addWidget(m_repositories);
    m_placeholderPage = new QFrame;
    m_placeholderPage->setObjectName(QStringLiteral("clonePlaceholder"));
    auto *placeholderLayout = m_placeholderLayout = new QVBoxLayout(m_placeholderPage);
    placeholderLayout->addStretch();
    m_placeholder = ui::dimLabel();
    m_placeholder->setTextFormat(Qt::PlainText);
    m_placeholder->setAccessibleName(tr("Repository list state"));
    m_placeholder->setWordWrap(true);
    m_placeholder->setAlignment(Qt::AlignCenter);
    placeholderLayout->addWidget(m_placeholder);
    m_login = new QPushButton(tr("Sign in to GitHub"));
    m_login->setObjectName(QStringLiteral("cloneLogin"));
    placeholderLayout->addWidget(m_login, 0, Qt::AlignHCenter);
    placeholderLayout->addStretch();
    m_listArea->addWidget(m_placeholderPage);
    githubLayout->addWidget(m_listArea);
    m_sources->addWidget(githubPage);
    layout->addWidget(m_sources);

    layout->addWidget(ui::hairline());
    // Caption above field, the way the sign-in dialog captions its fields.
    auto *destination = m_destinationLayout = new QVBoxLayout;
    auto *folderCaption = m_folderCaption = ui::sectionLabel(tr("Destination folder"));
    destination->addWidget(folderCaption);
    auto *folderRow = m_folderRow = new QHBoxLayout;
    m_folder = new QLineEdit(ui::tildePath(folder));
    m_folder->setObjectName(QStringLiteral("cloneFolder"));
    m_folder->setAccessibleName(tr("Destination folder"));
    folderCaption->setBuddy(m_folder);
    m_browse = new QPushButton(tr("Browse…"));
    folderRow->addWidget(m_folder, 1);
    folderRow->addWidget(m_browse);
    destination->addLayout(folderRow);
    m_destinationGap = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Fixed);
    destination->addItem(m_destinationGap);
    // The folder and the name read as one path: a cluster apart.
    auto *destinationRow = m_destinationRow = new QHBoxLayout;
    m_destinationPrefix = new ElidedLabel;
    m_destinationPrefix->setAccessibleName(tr("Destination"));
    m_name = new QLineEdit;
    m_name->setObjectName(QStringLiteral("cloneName"));
    m_name->setAccessibleName(tr("Repository folder name"));
    m_name->setPlaceholderText(tr("repository"));
    m_name->setToolTip(tr("Edit the name of the folder created for this repository"));
    m_destinationPrefix->setBuddy(m_name);
    destinationRow->addWidget(m_destinationPrefix);
    destinationRow->addWidget(m_name, 1);
    destination->addLayout(destinationRow);
    layout->addLayout(destination);

    // Always on screen, so a command starting does not move the buttons: the
    // idle bar has an empty range and a transparent track, and shows nothing.
    auto *status = m_statusLayout = new QVBoxLayout;
    status->setContentsMargins(0, 0, 0, 0);
    m_progress = new QProgressBar;
    m_progress->setTextVisible(false);
    status->addWidget(m_progress);
    // One line for everything the dialog has to say: what a command is doing
    // or how it ended, and the destination hint when nothing is running.
    m_message = ui::dimLabel();
    m_message->setAccessibleName(tr("Status"));
    m_message->setTextFormat(Qt::PlainText);
    m_message->setWordWrap(true);
    m_message->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_message->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    status->addWidget(m_message);
    layout->addLayout(status);

    auto *buttons = m_buttonRow = new QHBoxLayout;
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
    ui::setPrimary(m_clone); // 16 in, the dialog's primary action
    connect(m_cancel, &QPushButton::clicked, this, &CloneDialog::reject);
    connect(m_clone, &QPushButton::clicked, this, &CloneDialog::clone);
    connect(m_open, &QPushButton::clicked, this, &CloneDialog::openExisting);
    connect(m_refresh, &QToolButton::clicked, this, &CloneDialog::loadGitHub);
    connect(m_login, &QPushButton::clicked, this, &CloneDialog::signIn);
    connect(m_url, &QLineEdit::textChanged, this, &CloneDialog::userEdited);
    connect(m_folder, &QLineEdit::textChanged, this, &CloneDialog::userEdited);
    connect(m_name, &QLineEdit::textChanged, this, &CloneDialog::userEdited);
    connect(m_search, &QLineEdit::textChanged, this, &CloneDialog::filterRepositories);
    // Picking a repository names the folder after it, even over a name typed
    // for the one picked before: the list is what the user is choosing from.
    connect(m_repositories, &QListWidget::currentRowChanged, this, [this] {
        const QSignalBlocker blocker(m_name);
        m_name->setText(repositoryName(selectedUrl()));
        m_name->setCursorPosition(0);
        userEdited();
    });
    connect(m_browse, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Destination folder"), folderPath());
        if (!dir.isEmpty())
            m_folder->setText(ui::tildePath(dir));
    });
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, [this] {
        m_timedOut = true;
        if (m_process)
            m_process->kill();
    });
    connect(m_askPass, &AskPass::requestReceived, this, [this](const AskPassRequest &request) {
        m_timeout->stop();
        // A clone reads git's system and global configuration and no
        // repository's, which is what git finds from the file system's root.
        GitRepo outside(QDir::rootPath());
        auto *dialog = new LoginDialog(request, &outside, this);
        connect(dialog, &QDialog::accepted, m_askPass, [this, dialog, request] {
            if (request.kind == AskPassRequest::Username || request.kind == AskPassRequest::Password)
                m_askPass->answerLogin(request.id, dialog->username(), dialog->password(), dialog->remember());
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

    // The width is the design's, or a narrower window's (showEvent()); the
    // height follows the content (fitToContent()).
    setFixedWidth(ui::space(kDialogWidth));
    setTabOrder(m_urlTab, m_githubTab);
    setTabOrder(m_githubTab, m_url);
    setTabOrder(m_url, m_search);
    setTabOrder(m_search, m_repositories);
    setTabOrder(m_repositories, m_folder);
    setTabOrder(m_folder, m_browse);
    setTabOrder(m_browse, m_name);
    setTabOrder(m_name, m_clone);
    setTabOrder(m_clone, m_cancel);
    setTabOrder(m_cancel, m_open);
    updateSourcePolicies();
    setBusy(false);
    updateListArea();
    applyTheme();
    updateDestination();
    m_url->setFocus();
}

CloneDialog::~CloneDialog() { stopProcess(); }

void CloneDialog::applyTheme()
{
    const auto *theme = OmarchyTheme::instance();
    using namespace ui;
    const int pad = space(pad::dialog), item = space(gap::item), caption = space(gap::caption);
    layout()->setContentsMargins(pad, pad, pad, pad);
    layout()->setSpacing(space(gap::group));
    for (QBoxLayout *captioned : {m_urlLayout, m_destinationLayout})
        captioned->setSpacing(caption);
    m_urlHintGap->changeSize(0, item - caption, QSizePolicy::Minimum, QSizePolicy::Fixed);
    m_destinationGap->changeSize(0, item - caption, QSizePolicy::Minimum, QSizePolicy::Fixed);
    for (QBoxLayout *row : {static_cast<QBoxLayout *>(m_githubLayout), static_cast<QBoxLayout *>(m_accountRow),
                            static_cast<QBoxLayout *>(m_folderRow), static_cast<QBoxLayout *>(m_buttonRow)})
        row->setSpacing(item);
    m_destinationRow->setSpacing(space(gap::cluster));
    m_placeholderLayout->setContentsMargins(pad, space(pad::popover), pad, space(pad::popover));
    m_placeholderLayout->setSpacing(item);
    m_statusLayout->setSpacing(caption);
    m_progress->setFixedHeight(space(kProgressHeight));
    m_heading->setFont(theme->titleFont());
    placeOnLine(m_heading, theme->titleFont(), box::row);
    for (QLabel *label : {m_urlCaption, m_folderCaption})
        placeOnLine(label, theme->captionFont(), box::line);
    for (auto *edit : {m_url, m_folder, m_search}) {
        edit->setFont(theme->uiFont());
        edit->setFixedHeight(space(box::control));
    }
    m_name->setFont(theme->captionFont());
    m_name->setFixedHeight(space(box::row));
    m_name->setMinimumWidth(space(kNameMinWidth));
    for (auto *label : {m_account, m_count, m_placeholder, m_destinationPrefix, m_message})
        label->setFont(theme->captionFont());
    m_repositories->setFont(theme->uiFont());
    m_refresh->setFont(theme->uiFont());
    m_refresh->setText(ui::icon(ui::kRefresh, QStringLiteral("↻")).trimmed());
    m_clone->setText(ui::icon(ui::kFetch) + tr("Clone && open"));
    // Two lines of caption, so a one-line hint and a two-line one leave the
    // buttons where they are. Measured the way QLabel lays out wrapped text
    // (a bounding rect, not lineSpacing(): the two differ by the font's
    // leading at some sizes).
    m_message->setMinimumHeight(QFontMetrics(theme->captionFont())
        .boundingRect(0, 0, kMeasureLimit, kMeasureLimit, Qt::TextWordWrap, QStringLiteral("x\nx")).height());
    updateListHeight();
    updateMessage();
    fitToContent();
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
            // A long name shows its start, not the end setText() scrolls to.
            m_name->setCursorPosition(0);
        }
        m_suggestedName = suggestedName;
    }
    const QString name = m_name->text();
    const QString folder = folderPath();
    QString prefix = ui::tildePath(folder);
    if (!prefix.isEmpty() && !prefix.endsWith(QLatin1Char('/')))
        prefix += QLatin1Char('/');
    const QString preview = prefix.isEmpty() ? tr("Creates") : tr("Creates %1").arg(prefix);
    m_destinationPrefix->setText(preview);
    m_destinationPrefix->setToolTip(preview);
    m_hint.clear();
    bool valid = false;
    if (suggestedName.isEmpty())
        m_hint = m_sources->currentIndex() == 0 ? tr("Enter an HTTPS, HTTP or SSH repository URL.") : tr("Choose a repository to clone.");
    else if (name.isEmpty() || name != name.trimmed() || name == QLatin1String(".") || name == QLatin1String("..")
             || name.contains(QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f/\\\\]"))))
        m_hint = tr("Enter a folder name without slashes or leading or trailing spaces.");
    else if (folder.isEmpty() || !QFileInfo(folder).isDir())
        m_hint = tr("Choose an existing destination folder.");
    else {
        const QString path = QDir(folder).filePath(name);
        const QFileInfo info(path);
        if (info.exists() || info.isSymLink())
            m_hint = tr("Already exists: %1. Change the name or destination folder.").arg(ui::tildePath(path));
        else {
            valid = QFileInfo(folder).isWritable();
            if (!valid)
                m_hint = tr("This folder is not writable.");
        }
    }
    updateMessage();
    m_clone->setEnabled(valid && !m_process && !m_cloning && !m_loading);
}

// An edit means the user has started over: whatever the last command ended
// with is no longer what the dialog is about.
void CloneDialog::userEdited()
{
    if (!m_process && !m_status.isEmpty())
        setStatus(QString());
    updateDestination();
}

void CloneDialog::setStatus(const QString &text, bool alert)
{
    m_status = text;
    m_statusIsAlert = alert && !text.isEmpty();
    updateMessage();
}

// A running (or failed) command outranks the destination hint: the hint is
// what is left to say once nothing else is.
void CloneDialog::updateMessage()
{
    m_message->setText(m_status.isEmpty() ? m_hint : m_status);
    setTextColor(m_message, &m_messageColor,
                 m_statusIsAlert ? OmarchyTheme::instance()->color(QStringLiteral("red")) : QColor());
    refit();
}

void CloneDialog::setBusy(bool busy)
{
    // An empty range paints no chunk at all, a null one slides it along.
    m_progress->setRange(0, busy ? 0 : 1);
    if (!busy)
        m_progress->setValue(0);
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
        if (m_loading) {
            m_repositories->clear();
            m_visible = 0;
            m_github = GitHub::Ready;
            updateListArea();
        }
        m_cloning = m_loading = false;
        setBusy(false);
        setStatus(m_cloneTarget.isEmpty() ? tr("Stopped.")
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
            // git says where the clone has got to on its last line; gh says
            // what it wants from the browser over several.
            const QString text = QString::fromUtf8(*error);
            setStatus(m_cloning ? fewLines(text.right(kMessageBytes), true, 1) : fewLines(text, false));
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

// The stack is as tall as the page on screen, not as the taller of the two:
// the page it does not show is told its size hint does not count.
void CloneDialog::updateSourcePolicies()
{
    for (int i = 0; i < m_sources->count(); ++i) {
        const bool current = i == m_sources->currentIndex();
        m_sources->widget(i)->setSizePolicy(current ? QSizePolicy::Preferred : QSizePolicy::Ignored,
                                            current ? QSizePolicy::Fixed : QSizePolicy::Ignored);
    }
}

void CloneDialog::switchSource(int index)
{
    if (m_cloning)
        return;
    stopProcess();
    m_loading = false;
    m_sources->setCurrentIndex(index);
    updateSourcePolicies();
    setStatus(QString());
    setBusy(false);
    if (index == 1)
        loadGitHub();
    else
        m_url->setFocus();
    fitToContent();
}

void CloneDialog::loadGitHub()
{
    if (m_process)
        return;
    m_cloneTarget.clear();
    m_repositories->clear();
    m_visible = 0;
    m_accountName.clear();
    setStatus(QString());
    if (QStandardPaths::findExecutable(QStringLiteral("gh")).isEmpty()) {
        m_github = GitHub::NoTool;
        updateListArea();
        updateDestination();
        return;
    }
    m_loading = true;
    m_github = GitHub::Checking;
    updateListArea();
    run(QStringLiteral("gh"), {QStringLiteral("api"), QStringLiteral("--hostname"), QStringLiteral("github.com"), QStringLiteral("user")},
        [this](bool ok, const QByteArray &out, const QString &error) {
        m_loading = false;
        const QString login = QJsonDocument::fromJson(out).object().value(QStringLiteral("login")).toString();
        if (!ok || login.isEmpty()) {
            m_github = GitHub::SignedOut;
            updateListArea();
            // Why gh said no, beside the button that signs in: context, not
            // a failure of anything the user did.
            setStatus(fewLines(error, false));
            setBusy(false);
            return;
        }
        m_accountName = login;
        m_loading = true;
        m_github = GitHub::Loading;
        updateListArea();
        loadPage(1);
    });
}

void CloneDialog::loadPage(int page)
{
    run(QStringLiteral("gh"), {QStringLiteral("api"), QStringLiteral("--hostname"), QStringLiteral("github.com"),
        QStringLiteral("user/repos?per_page=100&sort=updated&affiliation=owner,collaborator,organization_member&page=%1").arg(page)},
        [this, page](bool ok, const QByteArray &out, const QString &error) {
        const auto doc = QJsonDocument::fromJson(out);
        if (!ok || !doc.isArray()) {
            m_loading = false;
            // Do not present an incomplete list as all available repositories.
            m_repositories->clear();
            m_visible = 0;
            m_github = GitHub::Ready;
            updateListArea();
            setBusy(false);
            setStatus(tr("Could not load repositories. Refresh to retry.\n%1").arg(fewLines(error, false)), true);
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
        m_github = GitHub::Ready;
        updateListHeight();
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
    m_visible = 0;
    for (int i = 0; i < m_repositories->count(); ++i) {
        auto *item = m_repositories->item(i);
        item->setHidden(!item->text().contains(m_search->text().trimmed(), Qt::CaseInsensitive));
        if (!item->isHidden())
            ++m_visible;
    }
    updateListArea();
    updateDestination();
}

// What the account line says, what stands in for the list, and how many
// repositories are on it — the three follow from the same state.
void CloneDialog::updateListArea()
{
    QString account, placeholder;
    switch (m_github) {
    case GitHub::Checking:
        account = placeholder = tr("Checking GitHub account…");
        break;
    case GitHub::Loading:
        account = tr("Signed in as %1").arg(m_accountName);
        placeholder = tr("Loading repositories…");
        break;
    case GitHub::SignedOut:
        account = tr("Not signed in");
        placeholder = tr("Sign in to see your personal, organization and shared repositories.");
        break;
    case GitHub::NoTool:
        account = tr("GitHub CLI (gh) is not installed");
        placeholder = tr("Install GitHub CLI (gh) to browse your repositories.");
        break;
    case GitHub::Ready:
        account = tr("Signed in as %1").arg(m_accountName);
        if (m_repositories->count() == 0)
            placeholder = tr("No repositories available for this account.");
        else if (m_visible == 0)
            placeholder = tr("No repositories match your filter.");
        break;
    }
    m_account->setText(account);
    m_placeholder->setText(placeholder);
    m_login->setVisible(m_github == GitHub::SignedOut);
    m_listArea->setCurrentWidget(placeholder.isEmpty() ? static_cast<QWidget *>(m_repositories) : m_placeholderPage);
    m_search->setEnabled(m_repositories->count() > 0);
    const int count = m_repositories->count();
    m_count->setText(m_github != GitHub::Ready || count == 0 ? QString()
        : m_visible < count ? tr("%1 of %2 repositories").arg(m_visible).arg(count)
        : count == 1 ? tr("1 repository") : tr("%n repositories", nullptr, count));
    refit();
}

// Seven rows tall, whichever page the area shows.
void CloneDialog::updateListHeight()
{
    const int row = m_repositories->count() > 0 && m_repositories->sizeHintForRow(0) > 0
        ? m_repositories->sizeHintForRow(0)
        : ui::space(ui::box::row);
    m_listArea->setFixedHeight(row * kRepositoryRows + 2 * qMax(1, m_repositories->frameWidth()));
    refit();
}

void CloneDialog::signIn()
{
    m_loading = true;
    setStatus(tr("Complete sign-in in your browser. A one-time code will appear here."));
    run(QStringLiteral("gh"), {QStringLiteral("auth"), QStringLiteral("login"), QStringLiteral("--hostname"),
        QStringLiteral("github.com"), QStringLiteral("--git-protocol"), QStringLiteral("https"), QStringLiteral("--web")},
        [this](bool ok, const QByteArray &, const QString &error) {
        m_loading = false;
        setBusy(false);
        if (ok)
            loadGitHub();
        else
            setStatus(tr("Sign-in did not finish. Try again.\n%1").arg(fewLines(error, false)), true);
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
    setStatus(tr("Cloning %1…").arg(repositoryName(url)));
    run(QStringLiteral("git"), args, [this](bool ok, const QByteArray &, const QString &error) {
        const bool cancelled = m_askPass->cancelled();
        // Logins to remember are only worth remembering once the clone worked.
        m_loginsToKeep = ok ? m_askPass->loginsToKeep() : QList<KeptLogin>();
        m_askPass->endOperation();
        m_cloning = false;
        setBusy(false);
        if (!ok) {
            setStatus(cancelled ? tr("Clone cancelled — not signed in.")
                                : tr("Clone failed.\n%1").arg(fewLines(error.right(kMessageBytes), true)), true);
            return;
        }
        m_repositoryPath = m_cloneTarget;
        m_cloned = true;
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
        setStatus(tr("That folder is not inside a Git repository."), true);
    else
        accept();
}

// Every change of a row's visibility or of a wrapped line of text asks for
// this; one pass at the end of the event does for all of them.
void CloneDialog::refit()
{
    if (m_refitPending)
        return;
    m_refitPending = true;
    QTimer::singleShot(0, this, [this] {
        m_refitPending = false;
        fitToContent();
    });
}

void CloneDialog::fitToContent()
{
    QLayout *l = layout();
    l->invalidate();
    l->activate();
    const int content = l->hasHeightForWidth() ? l->totalHeightForWidth(width()) : l->totalSizeHint().height();
    const int target = qMax(content, l->totalMinimumSize().height());
    // A fixed height, not a resize(): the compositor honours the window's
    // constraints, whereas a plain resize may be answered with the old size.
    if (target != height() || maximumHeight() != target)
        setFixedHeight(target);
}

// Fonts and frame widths from the stylesheet are only final once the widgets
// are polished, which is later than the constructor.
void CloneDialog::showEvent(QShowEvent *event)
{
    ensurePolished();
    updateListHeight();
    ui::fitDialogWidth(this, kDialogWidth);
    fitToContent();
    QDialog::showEvent(event);
}
