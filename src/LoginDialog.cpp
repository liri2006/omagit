#include "LoginDialog.h"
#include "CredentialKeeper.h"
#include "GitRepo.h"
#include "OmarchyTheme.h"
#include "UiHelpers.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QShowEvent>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <utility>

namespace {
// md-eye, md-eye_off: the show/hide toggle of the password field.
constexpr uint kEye = 0xF0208, kEyeOff = 0xF0209;
// As wide as the two fields want to be — narrower than the merge view, which
// carries two branch pickers side by side. Everything else is the grid's
// (Grid.h), as the merge view has it: a dialog's 16 of padding, its groups a
// group gap apart, captions 4 over their 28 px fields, the eye a 24 px ghost
// square 4 in from the field's right edge. A narrower window gets a narrower
// dialog (ui::fitDialogWidth()).
constexpr int kDialogWidth = 480;
// How long git may take to say which credential helper is configured. It is
// a config read, so this is only there to keep a wedged git off the screen.
constexpr int kConfigTimeoutMs = 3000;
// Every `credential.helper` and `credential.<url>.helper` there is, in the
// order git reads them. --get-regexp is what preserves that order, and -z
// writes each as key, newline, value, which a value with a space or a newline
// in it cannot be mistaken for two of.
const QLatin1String kHelperKeyPattern("^credential\\.(.+\\.)?helper$");
const QLatin1String kHelperKey("credential.helper");
const QLatin1String kKeyPrefix("credential."), kKeySuffix(".helper");
// The URL of every remote, read the same way: `git remote -v` would answer
// with names, URLs and (fetch)/(push) to take apart, where this is the shape
// the helper entries already arrive in.
const QLatin1String kRemoteUrlKeyPattern("^remote\\..*\\.url$");
} // namespace

// --- Which helper keeps this sign-in ---------------------------------------

namespace {

// The port a URL addresses, with the scheme's own port filled in where the URL
// names none: git normalizes the default port away before it compares two
// URLs, so https://example.com:443 and https://example.com are one place.
int effectivePort(const QUrl &url)
{
    if (url.port() != -1)
        return url.port();
    const QString scheme = url.scheme();
    if (scheme == QLatin1String("https"))
        return 443;
    if (scheme == QLatin1String("http"))
        return 80;
    if (scheme == QLatin1String("ssh"))
        return 22;
    if (scheme == QLatin1String("ftp"))
        return 21;
    if (scheme == QLatin1String("ftps"))
        return 990;
    if (scheme == QLatin1String("git"))
        return 9418;
    return -1;
}

// The host of a config URL, as it stands: QUrl will not hold a `*`, so the
// wildcard is taken off before the URL is parsed and handed back separately.
// It sits at the start of the host, which is what follows "://" and, when the
// URL names a user, the "@" after that.
QString takeWildcard(QString *spec)
{
    const int schemeEnd = spec->indexOf(QLatin1String("://"));
    if (schemeEnd < 0)
        return QString();
    int host = schemeEnd + 3;
    const int pathStart = spec->indexOf(QLatin1Char('/'), host);
    const QStringView authority = QStringView(*spec).mid(host, pathStart < 0 ? -1 : pathStart - host);
    const int at = authority.lastIndexOf(QLatin1Char('@'));
    if (at >= 0)
        host += at + 1;
    if (!QStringView(*spec).mid(host).startsWith(QLatin1String("*.")))
        return QString();
    spec->remove(host, 2);
    return QStringLiteral("*.");
}

// git's host pattern: the same host, or `*.example.com` for one or more whole
// components in front of example.com — never example.com itself.
bool hostMatches(const QString &wildcard, const QString &pattern, const QString &host)
{
    if (wildcard.isEmpty())
        return pattern.compare(host, Qt::CaseInsensitive) == 0;
    if (pattern.isEmpty())
        return false;
    // ".example.com" has to be the tail of the host with something before it.
    const QString tail = QLatin1Char('.') + pattern;
    return host.size() > tail.size()
        && QStringView(host).mid(host.size() - tail.size()).compare(tail, Qt::CaseInsensitive) == 0;
}

// The path of a config URL narrows the match to what lies under it, by whole
// components: /team covers /team and /team/repo, and never /teamwork. No path
// at all (or a bare "/") covers the host entire.
bool pathMatches(const QString &configPath, const QString &targetPath)
{
    QString pattern = configPath;
    while (pattern.endsWith(QLatin1Char('/')))
        pattern.chop(1);
    if (pattern.isEmpty())
        return true;
    QString path = targetPath;
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    return path == pattern || path.startsWith(pattern + QLatin1Char('/'));
}

// Whether the URL a `credential.<url>.helper` key names covers `target`, by
// git's urlmatch rules: the same scheme, the same host (the config side may
// lead with `*.`), the same port once the scheme's default is filled in on
// both sides, the same user where the config URL names one, and a path the
// target's lies under.
bool urlCovers(const QString &configUrl, const QUrl &target)
{
    QString spec = configUrl;
    const QString wildcard = takeWildcard(&spec);
    const QUrl pattern(spec);
    if (!pattern.isValid() || pattern.scheme().isEmpty())
        return false;
    return pattern.scheme() == target.scheme()
        && hostMatches(wildcard, pattern.host(), target.host())
        && effectivePort(pattern) == effectivePort(target)
        && (pattern.userName().isEmpty() || pattern.userName() == target.userName())
        && pathMatches(pattern.path(), target.path());
}

} // namespace

bool remoteUrlMatchesTarget(const QString &remoteUrl, const QUrl &target)
{
    if (target.scheme().isEmpty() || target.host().isEmpty())
        return false;
    const QUrl url(remoteUrl);
    // An ssh remote written the scp way — git@example.com:team/repo.git — is
    // no URL and carries no scheme; nothing asks an askpass helper for a
    // password on its behalf either, so it is simply not one of these.
    if (!url.isValid() || url.scheme().isEmpty() || url.host().isEmpty())
        return false;
    return url.scheme() == target.scheme()
        && url.host().compare(target.host(), Qt::CaseInsensitive) == 0
        && effectivePort(url) == effectivePort(target)
        && (url.userName().isEmpty() || target.userName().isEmpty()
            || url.userName() == target.userName());
}

QStringList credentialHelpersFor(const QStringList &configEntries, const QUrl &target)
{
    QStringList helpers;
    for (const QString &entry : configEntries) {
        // key, newline, value — and no newline at all for a key git was given
        // without one, which is as empty a value as an empty string.
        const int newline = entry.indexOf(QLatin1Char('\n'));
        const QString key = newline < 0 ? entry : entry.left(newline);
        const QString value = newline < 0 ? QString() : entry.mid(newline + 1);

        bool applies = key.compare(kHelperKey, Qt::CaseInsensitive) == 0;
        if (!applies) {
            // The URL between the two halves of `credential.<url>.helper`. Git
            // leaves the case of a subsection alone, so it is taken as written.
            const int inner = kKeyPrefix.size() + kKeySuffix.size();
            if (key.size() <= inner || !key.startsWith(kKeyPrefix, Qt::CaseInsensitive)
                || !key.endsWith(kKeySuffix, Qt::CaseInsensitive))
                continue;
            applies = urlCovers(key.mid(kKeyPrefix.size(), key.size() - inner), target);
        }
        if (!applies)
            continue;
        // Git appends a helper and empties the list on an empty value, in the
        // order the configuration is read — no entry outranks another.
        const QString helper = value.trimmed();
        if (helper.isEmpty())
            helpers.clear();
        else
            helpers.append(helper);
    }
    return helpers;
}

QString credentialHelperName(const QString &helper)
{
    QString command = helper.trimmed();
    if (command.startsWith(QLatin1Char('!')))
        command = command.mid(1).trimmed(); // a shell command of its own
    static const QRegularExpression space(QStringLiteral("\\s"));
    QString name = command.section(space, 0, 0).section(QLatin1Char('/'), -1);
    const QLatin1String prefix("git-credential-");
    if (name.startsWith(prefix) && name.size() > prefix.size())
        name = name.mid(prefix.size());
    return name.isEmpty() ? helper.trimmed() : name;
}

// --- The password field ----------------------------------------------------

// A password field with the eye at its right edge: it flips the echo mode and
// its own glyph, and the text keeps clear of it through the field's margins.
class SecretEdit : public QLineEdit
{
public:
    explicit SecretEdit(QWidget *parent = nullptr)
        : QLineEdit(parent)
    {
        setEchoMode(QLineEdit::Password);
        m_reveal = new QToolButton(this);
        m_reveal->setObjectName(QStringLiteral("revealButton"));
        m_reveal->setToolButtonStyle(Qt::ToolButtonTextOnly);
        m_reveal->setCursor(Qt::PointingHandCursor);
        m_reveal->setFocusPolicy(Qt::NoFocus);
        m_reveal->setCheckable(true);
        connect(m_reveal, &QToolButton::toggled, this, [this](bool on) {
            setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
            applyTheme();
        });
    }

    QToolButton *revealButton() const { return m_reveal; }

    void applyTheme()
    {
        const bool shown = echoMode() == QLineEdit::Normal;
        m_reveal->setFont(font());
        m_reveal->setText(ui::icon(shown ? kEyeOff : kEye, shown ? QStringLiteral("•") : QStringLiteral("◦")).trimmed());
        m_reveal->setToolTip(shown ? tr("Hide the password") : tr("Show the password"));
        m_reveal->setAccessibleName(shown ? tr("Hide") : tr("Show"));
        // The design's square, so the glyph swap does not move the field's
        // text about; the text keeps clear of it and of its inset.
        m_side = ui::space(ui::box::row);
        m_reveal->setFixedSize(m_side, m_side);
        setTextMargins(0, 0, m_side + ui::space(ui::gap::icon), 0);
        place();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLineEdit::resizeEvent(event);
        place();
    }

private:
    void place()
    {
        m_reveal->move(width() - ui::space(ui::gap::icon) - m_side, (height() - m_side) / 2);
    }

    QToolButton *m_reveal;
    int m_side = 0;
};

// --- The dialog ------------------------------------------------------------

LoginDialog::LoginDialog(const AskPassRequest &request, GitRepo *repo, QWidget *parent)
    : QDialog(parent), m_request(request)
{
    setWindowTitle(tr("Sign in"));
    setObjectName(QStringLiteral("loginDialog"));
    setWindowModality(Qt::WindowModal);
    setAttribute(Qt::WA_DeleteOnClose);
    setSizeGripEnabled(false);

    // In this order: which helper it is decides whether the flag is set, and
    // the two as arguments of one call would be read in whichever order.
    bool forEveryRemote = true;
    const QString helper = credentialHelperFor(repo, request, &forEveryRemote);
    m_note = noteText(helper, forEveryRemote);
    if (request.kind != AskPassRequest::Passphrase && credentialHelperName(helper) != helper)
        m_noteTip = helper;
    // A clone has no repository configuration to inspect yet.
    if (!repo && (request.kind == AskPassRequest::Username || request.kind == AskPassRequest::Password))
        m_note = tr("Git's configured credential helpers manage saved credentials.");
    // Nothing would keep this login: git is offered the helper to keep it
    // with, ticked, where git has that helper (CredentialKeeper).
    m_offerRemember = repo && helper.isEmpty()
        && (request.kind == AskPassRequest::Username || request.kind == AskPassRequest::Password)
        && CredentialKeeper::helperAvailable();

    buildUi();
    applyTheme();
    connect(OmarchyTheme::instance(), &OmarchyTheme::changed, this, &LoginDialog::applyTheme);
    updateAcceptable();
    // On the first thing still to fill in: the username of a fresh sign-in,
    // the password when the username is already known.
    const bool askUser = m_userEdit->isVisibleTo(this) && !m_userEdit->isReadOnly() && m_userEdit->text().isEmpty();
    (askUser ? static_cast<QWidget *>(m_userEdit) : m_secretEdit)->setFocus();
}

// git names the user in the URL of a password prompt; a username prompt is
// git asking for one it does not have yet.
bool LoginDialog::wantsUsername() const
{
    return m_request.kind == AskPassRequest::Username
        || (m_request.kind == AskPassRequest::Password && !m_request.user.isEmpty());
}

QString LoginDialog::headingText() const
{
    switch (m_request.kind) {
    case AskPassRequest::Username:
    case AskPassRequest::Password:
        return tr("Sign in to %1").arg(m_request.host.isEmpty() ? m_request.target : m_request.host);
    case AskPassRequest::Passphrase:
        return tr("Unlock %1").arg(ui::tildePath(m_request.keyPath));
    case AskPassRequest::Other:
        break;
    }
    // Git's own question, without the colon it ends its prompts with.
    QString text = m_request.prompt.trimmed();
    while (text.endsWith(QLatin1Char(':')))
        text.chop(1);
    return text.isEmpty() ? tr("Sign in") : text;
}

QString LoginDialog::hintText() const
{
    // What is typed for a plain-http remote crosses the network as it is;
    // only this machine's own loopback is spared the warning.
    if (m_request.kind == AskPassRequest::Username || m_request.kind == AskPassRequest::Password) {
        const QUrl url(m_request.target);
        const QString host = url.host();
        if (url.scheme() == QLatin1String("http") && host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) != 0
            && !QHostAddress(host).isLoopback())
            return tr("Plain HTTP — the password is sent unencrypted.");
    }
    if (!m_request.retry)
        return QString();
    return m_request.kind == AskPassRequest::Passphrase
        ? tr("That passphrase did not unlock the key — try again.")
        : tr("Asked again — the last answer was not accepted.");
}

// Which credential helper would keep this sign-in, resolved the way git
// resolves it. A helper is often configured for one remote — `[credential
// "https://github.com"] helper = …`, or credential.<url>.helper — and a plain
// `--get credential.helper` does not see those, so the note would promise the
// password is forgotten while git goes on storing it. --get-urlmatch does see
// them, but it answers with the one entry that matches best, and git picks no
// best: it walks every entry in configuration order, appending the helpers
// that apply and emptying the list on an empty value, so an unqualified
// `helper = store` written after `[credential "https://example.com"] helper =`
// stores the password all the same, and the other way round it does not. That
// order is what is read here and what credentialHelpersFor() replays; the last
// helper left standing is the one that answers, and no helper at all means
// nothing is remembered. There is nothing to match for a passphrase (the
// target is a key file) or for a question of git's own: the target is empty,
// no `credential.<url>.helper` covers it, and the unqualified entries are all
// such a request is told about.
//
// What the entries are matched against is not the prompt's own URL but the
// repository's remotes. Git walks the configuration with the URL as the remote
// carries it, path and all, and only afterwards drops the path — which is why
// the prompt has none. So `credential.https://example.com/team.helper = store`
// does keep the password of the remote https://example.com/team/repo.git,
// while matching it against the pathless prompt would find nothing at all. The
// remotes whose scheme, host, port and user are the prompt's are asked
// instead, with the user the prompt names filled in where the URL carries none
// — that being the credential git is holding by now. A prompt no remote
// answers for (a submodule's, a URL git rewrote through url.<base>.insteadOf,
// or one `git credential fill` put on its own) falls back to the prompt's own
// URL, which is the best that can be said about it.
QString LoginDialog::credentialHelperFor(GitRepo *repo, const AskPassRequest &request,
                                         bool *forEveryRemote)
{
    if (forEveryRemote)
        *forEveryRemote = true;
    if (!repo)
        return QString();
    // Both reads are configuration and answer in milliseconds; the timeout is
    // only there to keep a wedged git off the screen.
    const auto configEntries = [repo](QLatin1String pattern) {
        const QStringList args{QStringLiteral("config"), QStringLiteral("-z"),
                               QStringLiteral("--get-regexp"), pattern};
        return QString::fromUtf8(repo->run(args, nullptr, nullptr, kConfigTimeoutMs))
            .split(QLatin1Char('\0'), Qt::SkipEmptyParts);
    };
    // The value half of an entry: the key, a newline, then the value.
    const auto value = [](const QString &entry) {
        const int newline = entry.indexOf(QLatin1Char('\n'));
        return newline < 0 ? QString() : entry.mid(newline + 1).trimmed();
    };

    const QStringList entries = configEntries(kHelperKeyPattern);
    const QUrl target = request.host.isEmpty() ? QUrl() : QUrl(request.target);

    QList<QUrl> candidates;
    if (!target.host().isEmpty()) {
        for (const QString &entry : configEntries(kRemoteUrlKeyPattern)) {
            const QString url = value(entry);
            if (!remoteUrlMatchesTarget(url, target))
                continue;
            QUrl candidate(url);
            if (candidate.userName().isEmpty())
                candidate.setUserName(target.userName());
            candidates.append(candidate);
        }
    }
    if (candidates.isEmpty())
        candidates.append(target);

    // Every remote the prompt could be about is asked, and they mostly answer
    // the same thing: one remote, or several lying under the same configured
    // path. Where they do not (one path has a helper, another has none), the
    // prompt alone does not say which of them git is signing in to, so the
    // helper that was found is the one named and the note hedges about it.
    QStringList answers; // one per candidate, empty where nothing keeps it
    for (const QUrl &candidate : std::as_const(candidates)) {
        const QStringList helpers = credentialHelpersFor(entries, candidate);
        answers.append(helpers.isEmpty() ? QString() : helpers.constLast());
    }
    QString answer;
    bool agree = true;
    for (const QString &helper : std::as_const(answers)) {
        agree = agree && helper == answers.constFirst();
        if (answer.isEmpty())
            answer = helper;
    }
    if (forEveryRemote)
        *forEveryRemote = agree;
    return answer;
}

QString LoginDialog::noteText(const QString &credentialHelper, bool forEveryRemote) const
{
    if (m_request.kind == AskPassRequest::Passphrase)
        return tr("Not remembered — ssh-agent keeps the key unlocked once you add it");
    if (credentialHelper.isEmpty())
        return tr("Not remembered — no credential helper is configured");
    // Two remotes of this host, kept by different helpers: naming one of them
    // outright would promise for a sign-in that may well be the other's.
    if (!forEveryRemote)
        return tr("Remembered if git's credential helper covers this remote (%1)").arg(credentialHelperName(credentialHelper));
    return tr("Remembered by git's credential helper (%1)").arg(credentialHelperName(credentialHelper));
}

void LoginDialog::buildUi()
{
    // The heading and its hint, the fields, the note and the buttons, a group
    // gap apart (applyTheme() scales every gap).
    auto *layout = new QVBoxLayout(this);

    auto *head = m_headLayout = new QVBoxLayout;
    head->setContentsMargins(0, 0, 0, 0);
    m_heading = new QLabel(headingText());
    m_heading->setWordWrap(true);
    m_heading->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_hint = ui::dimLabel(hintText());
    m_hint->setWordWrap(true);
    m_hint->setVisible(!m_hint->text().isEmpty());
    head->addWidget(m_heading);
    head->addWidget(m_hint);
    layout->addLayout(head);

    // Caption above field, twice, the way the merge view captions its pickers.
    auto *fields = m_fieldsLayout = new QVBoxLayout;
    m_userCaption = ui::sectionLabel(tr("Username"));
    m_userEdit = new QLineEdit;
    m_userEdit->setObjectName(QStringLiteral("usernameEdit"));
    m_userEdit->setText(m_request.user);
    m_secretCaption = ui::sectionLabel(m_request.kind == AskPassRequest::Passphrase ? tr("Passphrase")
                                       : m_request.kind == AskPassRequest::Other    ? tr("Answer")
                                                                                    : tr("Password"));
    m_secretEdit = new SecretEdit;
    m_secretEdit->setObjectName(QStringLiteral("secretEdit"));
    if (m_request.kind == AskPassRequest::Username || m_request.kind == AskPassRequest::Password)
        m_secretEdit->setPlaceholderText(tr("Password or token"));
    fields->addWidget(m_userCaption);
    fields->addWidget(m_userEdit);
    // With the layout's caption gap before it, the gap between the groups.
    m_fieldsGap = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Fixed);
    fields->addItem(m_fieldsGap);
    fields->addWidget(m_secretCaption);
    fields->addWidget(m_secretEdit);
    layout->addLayout(fields);

    const bool showUser = wantsUsername();
    m_userCaption->setVisible(showUser);
    m_userEdit->setVisible(showUser);
    // The user git already knows about is shown, not asked for.
    if (showUser && m_request.kind == AskPassRequest::Password) {
        m_userEdit->setReadOnly(true);
        m_userEdit->setFocusPolicy(Qt::ClickFocus);
        m_userEdit->setToolTip(tr("The user the remote's URL names"));
    }

    // The offer to remember the login, with the note under it saying what
    // that means — or, with nothing to offer, the note alone.
    auto *foot = m_footLayout = new QVBoxLayout;
    foot->setContentsMargins(0, 0, 0, 0);
    if (m_offerRemember) {
        m_remember = new QCheckBox(tr("Remember this sign-in"));
        m_remember->setObjectName(QStringLiteral("rememberSignIn"));
        m_remember->setChecked(true);
        m_remember->setCursor(Qt::PointingHandCursor);
        m_remember->setToolTip(tr("Adds %1 = %2 to your git configuration; git's %2 helper keeps the login")
                                   .arg(CredentialKeeper::configKey(m_request.context), CredentialKeeper::helper()));
        foot->addWidget(m_remember);
    }
    m_noteLabel = ui::dimLabel(shownNote());
    m_noteLabel->setWordWrap(true);
    m_noteLabel->setToolTip(m_noteTip);
    foot->addWidget(m_noteLabel);
    layout->addLayout(foot);
    if (m_remember) {
        connect(m_remember, &QCheckBox::toggled, this, [this] {
            m_noteLabel->setText(shownNote());
            fitToContent();
        });
    }
    layout->addStretch(1);

    auto *buttons = m_buttonRow = new QHBoxLayout;
    m_cancelButton = new QPushButton(tr("Cancel"));
    m_cancelButton->setCursor(Qt::PointingHandCursor);
    m_cancelButton->setAutoDefault(false);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    m_signInButton = new QPushButton(tr("Sign in"));
    m_signInButton->setCursor(Qt::PointingHandCursor);
    m_signInButton->setDefault(true);
    ui::setPrimary(m_signInButton); // 16 in, the dialog's primary action
    connect(m_signInButton, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addStretch();
    buttons->addWidget(m_cancelButton);
    buttons->addWidget(m_signInButton);
    layout->addLayout(buttons);

    for (QLineEdit *edit : {m_userEdit, static_cast<QLineEdit *>(m_secretEdit)}) {
        connect(edit, &QLineEdit::textChanged, this, &LoginDialog::updateAcceptable);
        // Return from either field signs in, as long as there is something
        // to sign in with; the default button would otherwise do it anyway.
        connect(edit, &QLineEdit::returnPressed, this, [this] {
            if (m_signInButton->isEnabled())
                accept();
        });
    }

    setFixedWidth(ui::space(kDialogWidth));
    setTabOrder(m_userEdit, m_secretEdit);
    if (m_remember) {
        setTabOrder(m_secretEdit, m_remember);
        setTabOrder(m_remember, m_signInButton);
    } else {
        setTabOrder(m_secretEdit, m_signInButton);
    }
    setTabOrder(m_signInButton, m_cancelButton);
}

void LoginDialog::applyTheme()
{
    const OmarchyTheme *t = OmarchyTheme::instance();
    const int pad = ui::space(ui::pad::dialog);
    layout()->setContentsMargins(pad, pad, pad, pad);
    layout()->setSpacing(ui::space(ui::gap::group));
    m_headLayout->setSpacing(ui::space(ui::gap::caption));
    m_fieldsLayout->setSpacing(ui::space(ui::gap::caption));
    m_footLayout->setSpacing(ui::space(ui::gap::caption));
    if (m_remember)
        m_remember->setFont(t->uiFont());
    m_fieldsGap->changeSize(0, ui::space(ui::gap::group) - ui::space(ui::gap::caption), QSizePolicy::Minimum,
                            QSizePolicy::Fixed);
    m_buttonRow->setSpacing(ui::space(ui::gap::item));
    // Bold at the base size, like the merge view's verdict headline: the
    // stylesheet decides the size, the font the weight.
    m_heading->setFont(t->titleFont());
    for (QLabel *l : {m_hint, m_noteLabel})
        l->setFont(t->captionFont());
    for (QLabel *l : {m_userCaption, m_secretCaption}) {
        l->setFont(t->captionFont());
        ui::placeOnLine(l, t->captionFont(), ui::box::line);
    }
    for (QLineEdit *edit : {m_userEdit, static_cast<QLineEdit *>(m_secretEdit)}) {
        edit->setFont(t->uiFont());
        edit->setFixedHeight(ui::space(ui::box::control));
    }
    m_secretEdit->applyTheme();
    fitToContent();
}

void LoginDialog::updateAcceptable()
{
    const bool haveUser = !wantsUsername() || !m_userEdit->text().trimmed().isEmpty();
    m_signInButton->setEnabled(haveUser && !m_secretEdit->text().isEmpty());
}

bool LoginDialog::remember() const
{
    return m_remember && m_remember->isChecked();
}

// What the foot of the dialog says: where a remembered login goes and what is
// best kept there, or else whether anything keeps it at all.
QString LoginDialog::shownNote() const
{
    if (!remember())
        return m_note;
    return tr("Kept in your keyring by git's %1 helper. A personal access token is safer to keep "
              "there than your account password.")
        .arg(CredentialKeeper::helper());
}

QString LoginDialog::username() const
{
    return m_userEdit->text().trimmed();
}

QString LoginDialog::password() const
{
    return m_secretEdit->text();
}

// As tall as its content, like the merge view: the fields do not move when
// the retry hint or a longer note takes a second line.
void LoginDialog::fitToContent()
{
    QLayout *l = layout();
    l->invalidate();
    l->activate();
    const int content = l->hasHeightForWidth() ? l->totalHeightForWidth(width()) : l->totalSizeHint().height();
    const int target = qMax(content, l->totalMinimumSize().height());
    if (target != height() || maximumHeight() != target)
        setFixedHeight(target);
}

// Fonts and frame widths from the stylesheet are only final once the widgets
// are polished, which is later than the constructor.
void LoginDialog::showEvent(QShowEvent *event)
{
    ensurePolished();
    ui::fitDialogWidth(this, kDialogWidth);
    fitToContent();
    QDialog::showEvent(event);
}
