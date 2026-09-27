#include "RemoteSync.h"
#include "ProcessUtil.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QUrl>

namespace {
constexpr int kMaxBackoff = 15 * 60; // seconds
constexpr int kNudgeAge = 45;        // seconds; a nudge fetches when the last fetch is older
// A ref file changes several times per command; the watch waits for quiet.
constexpr int kWatchDebounceMs = 400;
// Long enough for the window to have painted before a fetch competes for the
// disk, short enough that it still feels like part of opening.
constexpr int kFirstFetchDelayMs = 1500;
// A fetch hanging on the network is worth less than a responsive window; a
// pull or a push may legitimately move a lot of data.
constexpr int kFetchTimeoutMs = 90000;
constexpr int kTransferTimeoutMs = 300000;

// The line of git's output that says what went wrong: its first "fatal: " or
// "error: " line — a rejected push starts with "To <url>" and the rejection,
// and says "error: failed to push some refs" after them — else the first line.
QString firstLine(const QByteArray &text)
{
    const QStringList lines = QString::fromUtf8(text).trimmed().split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        // git prefixes its errors with "fatal: " / "error: "; the dialog says that already.
        for (const char *prefix : {"fatal: ", "error: "})
            if (line.startsWith(QLatin1String(prefix)))
                return line.mid(int(strlen(prefix))).trimmed();
    }
    return lines.constFirst().trimmed();
}
} // namespace

// git's prompt code, word for word and untranslated: "could not read Username
// for 'https://example.com': terminal prompts disabled" (or Password, with the
// user in the URL). The URL is the remote's without its path.
QString RemoteSync::failureText(const QByteArray &err)
{
    const QString output = QString::fromUtf8(err).trimmed();
    if (output.isEmpty())
        return QString();
    // All of it under the headline, where there is more to it than that line.
    const QString headline = firstLine(err);
    return output.contains(QLatin1Char('\n')) ? headline + QStringLiteral("\n\n") + output : headline;
}

bool RemoteSync::needsSignIn(const QByteArray &err, QString *where)
{
    const QString text = QString::fromUtf8(err);
    if (!text.contains(QLatin1String("terminal prompts disabled")))
        return false;
    if (where) {
        static const QRegularExpression prompt(QStringLiteral("could not read \\w+ for '([^']+)'"));
        const QUrl url(prompt.match(text).captured(1));
        *where = url.host();
        if (!where->isEmpty() && url.port() != -1)
            *where += QLatin1Char(':') + QString::number(url.port());
    }
    return true;
}

RemoteSync::RemoteSync(GitRepo *repo, QObject *parent)
    : QObject(parent), m_repo(repo), m_askPass(new AskPass(this))
{
    // A prompt is git waiting for a person, not git hanging: the kill timer
    // is held while the dialog is up and starts over once the answer is in.
    connect(m_askPass, &AskPass::requestReceived, this, [this] { GitRepo::holdTimeout(m_process); });
    connect(m_askPass, &AskPass::answered, this, [this] { GitRepo::resumeTimeout(m_process); });
    m_autoTimer.setSingleShot(true);
    connect(&m_autoTimer, &QTimer::timeout, this, &RemoteSync::autoFetch);
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(kWatchDebounceMs);
    connect(&m_debounce, &QTimer::timeout, this, &RemoteSync::repositoryChanged);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString &path) {
        watchGitDir(); // new ref directories (first fetch of a remote) need a watch too
        if (path == m_gitDir) {
            // Every status or diff (ours or an editor's) creates and removes
            // .git/index.lock, which counts as a change of the directory;
            // only react when something that matters has changed.
            const QString stamp = gitDirStamp();
            if (stamp == m_gitDirStamp)
                return;
            m_gitDirStamp = stamp;
        }
        m_debounce.start();
    });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] {
        watchGitDir(); // files replaced by rename drop out of the watch
        m_debounce.start();
    });
    connect(m_repo, &GitRepo::rootChanged, this, &RemoteSync::reset);
    m_gitDir = m_repo->gitDir();
    m_gitDirStamp = gitDirStamp();
    watchGitDir();
    refreshState();
}

void RemoteSync::reset()
{
    if (m_process) {
        // The old repository's fetch is of no interest any more (and its
        // result would be read as the new one's).
        abandonProcess(m_process, this);
        m_process->deleteLater();
        m_process = nullptr;
    }
    m_op = None;
    m_autoOp = false;
    m_askPass->endOperation(); // another repository, other credentials
    m_signInCancelled = false;
    m_lastFetch = QDateTime();
    m_lastFetchOk = true;
    m_lastFetchNeedsSignIn = false;
    m_lastFetchError.clear();
    m_failures = 0;
    if (!m_watcher.directories().isEmpty())
        m_watcher.removePaths(m_watcher.directories());
    if (!m_watcher.files().isEmpty())
        m_watcher.removePaths(m_watcher.files());
    m_gitDir = m_repo->gitDir();
    m_gitDirStamp = gitDirStamp();
    watchGitDir();
    refreshState();
    nudge();
}

// A running fetch is not worth waiting for: the window that owns this
// object is half gone.
RemoteSync::~RemoteSync()
{
    abandonProcess(m_process, this);
}

// The entries of .git that say where the refs stand (HEAD, FETCH_HEAD,
// packed-refs, ORIG_HEAD, the refs and logs directories...) with their sizes
// and times. Lock files come and go with every git command and the index is
// the working tree's business, so they are left out.
QString RemoteSync::gitDirStamp() const
{
    if (m_gitDir.isEmpty())
        return QString();
    QStringList parts;
    const QFileInfoList entries = QDir(m_gitDir).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name);
    for (const QFileInfo &fi : entries) {
        const QString name = fi.fileName();
        if (name.endsWith(QLatin1String(".lock")) || name == QLatin1String("index") || name == QLatin1String("objects"))
            continue;
        parts << name + QLatin1Char(':') + QString::number(fi.size()) + QLatin1Char(':')
                + QString::number(fi.lastModified().toMSecsSinceEpoch());
    }
    return parts.join(QLatin1Char('\n'));
}

// FETCH_HEAD is rewritten by every fetch (also one that brought nothing).
void RemoteSync::watchGitDir()
{
    if (m_gitDir.isEmpty())
        return;
    QStringList paths{m_gitDir, m_gitDir + QStringLiteral("/refs"), m_gitDir + QStringLiteral("/refs/heads"),
                      m_gitDir + QStringLiteral("/refs/remotes")};
    const QDir remotes(m_gitDir + QStringLiteral("/refs/remotes"));
    for (const QString &r : remotes.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        paths << remotes.filePath(r);
    paths << m_gitDir + QStringLiteral("/FETCH_HEAD") << m_gitDir + QStringLiteral("/packed-refs");
    const QStringList watched = m_watcher.directories() + m_watcher.files();
    for (const QString &p : std::as_const(paths)) {
        if (!watched.contains(p) && QFileInfo::exists(p))
            m_watcher.addPath(p);
    }
}

void RemoteSync::refreshState()
{
    m_state = m_repo->upstreamState();
    emit stateChanged();
}

void RemoteSync::setAutoFetchInterval(int seconds)
{
    m_interval = qMax(0, seconds);
    m_failures = 0;
    scheduleAutoFetch();
}

void RemoteSync::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    if (active)
        nudge();
    else
        m_autoTimer.stop();
}

void RemoteSync::nudge()
{
    if (m_interval <= 0 || !m_active)
        return;
    const bool stale = !m_lastFetch.isValid() || m_lastFetch.secsTo(QDateTime::currentDateTime()) >= kNudgeAge;
    if (stale && m_failures == 0)
        m_autoTimer.start(kFirstFetchDelayMs); // let the window paint first
    else
        scheduleAutoFetch();
}

void RemoteSync::markFetched()
{
    m_lastFetch = QDateTime::currentDateTime();
    m_lastFetchOk = true;
    m_lastFetchNeedsSignIn = false;
    m_lastFetchError.clear();
    m_failures = 0;
    scheduleAutoFetch();
    emit stateChanged();
}

void RemoteSync::scheduleAutoFetch()
{
    m_autoTimer.stop();
    if (m_interval <= 0 || !m_active)
        return;
    int seconds = m_interval;
    for (int i = 0; i < m_failures && seconds < kMaxBackoff; ++i)
        seconds *= 2;
    m_autoTimer.start(qMin(seconds, kMaxBackoff) * 1000);
}

void RemoteSync::autoFetch()
{
    if (busy() || !m_active) {
        scheduleAutoFetch();
        return;
    }
    if (m_state.remotes.isEmpty()) {
        refreshState(); // a remote may have been added meanwhile
        if (m_state.remotes.isEmpty()) {
            scheduleAutoFetch();
            return;
        }
    }
    m_autoOp = true;
    start(Fetch, fetchArgs());
}

QStringList RemoteSync::fetchArgs() const
{
    return {QStringLiteral("fetch"), QStringLiteral("--all")};
}

QStringList RemoteSync::pullArgs() const
{
    return {QStringLiteral("pull")};
}

QStringList RemoteSync::pushArgs() const
{
    if (pushPublishes())
        return {QStringLiteral("push"), QStringLiteral("-u"), m_state.remote, m_state.branch};
    return {QStringLiteral("push")};
}

void RemoteSync::fetch()
{
    if (!canFetch())
        return;
    m_autoOp = false;
    start(Fetch, fetchArgs());
}

void RemoteSync::pull()
{
    if (!canPull())
        return;
    m_autoOp = false;
    start(Pull, pullArgs());
}

void RemoteSync::push()
{
    if (!canPush())
        return;
    m_autoOp = false;
    start(Push, pushArgs());
}

void RemoteSync::start(Op op, const QStringList &args)
{
    m_op = op;
    m_behindBefore = qMax(0, m_state.behind);
    m_aheadBefore = qMax(0, m_state.ahead);
    m_signInCancelled = false;
    m_autoTimer.stop();
    emit stateChanged();
    QStringList full = args;
    if (op == Fetch && m_autoOp)
        full << QStringLiteral("--quiet");
    // Only what the user asked for may put a dialog on screen. An automatic
    // fetch keeps GIT_TERMINAL_PROMPT=0 and nothing else, so a remote it
    // cannot sign in to ends as lastFetchNeedsSignIn() and the backoff takes over.
    QStringList env;
    if (!m_autoOp && m_askPass->listen())
        env = m_askPass->env();
    m_process = m_repo->runAsync(full, this, [this, op](int code, const QByteArray &out, const QByteArray &err) {
        onFinished(op, code, out, err);
    }, op == Fetch ? kFetchTimeoutMs : kTransferTimeoutMs, env);
}

void RemoteSync::onFinished(Op op, int code, const QByteArray &out, const QByteArray &err)
{
    Q_UNUSED(out)
    const bool ok = code == 0;
    const bool automatic = m_autoOp;
    // The sign-in dialog was closed, so git had nothing to log in with and
    // gave up. Not a failure of the remote: it is reported quietly, without
    // a message box, and the credentials of this operation are dropped.
    const bool cancelled = !ok && m_askPass->cancelled();
    m_signInCancelled = cancelled;
    // An automatic fetch has no dialog to ask with, so a remote that wants a
    // password turns it down; the user's own fetch would have asked.
    QString signInHost;
    const bool signInNeeded = !ok && automatic && op == Fetch && needsSignIn(err, &signInHost);
    // Logins to remember are only worth remembering once they worked.
    const QList<KeptLogin> keep = ok ? m_askPass->loginsToKeep() : QList<KeptLogin>();
    m_askPass->endOperation();
    m_op = None;
    m_autoOp = false;
    watchGitDir();
    refreshState(); // also emits stateChanged, which shows the new counts

    QString message;
    const QString upstream = m_state.upstream;
    switch (op) {
    case Fetch:
        // A cancelled sign-in leaves the fetch history and the backoff alone:
        // nothing was tried, so the button carries no error mark either.
        if (!cancelled) {
            m_lastFetch = QDateTime::currentDateTime();
            m_lastFetchOk = ok;
            m_lastFetchNeedsSignIn = signInNeeded;
            m_lastFetchError = ok ? QString() : firstLine(err);
            m_failures = ok ? 0 : m_failures + 1;
        }
        if (cancelled)
            message = tr("Fetch cancelled — not signed in");
        else if (signInNeeded)
            message = signInHost.isEmpty() ? tr("Sign-in needed — Fetch (Ctrl+F) to sign in")
                                           : tr("Sign-in needed for %1 — Fetch (Ctrl+F) to sign in").arg(signInHost);
        else if (!ok)
            message = (automatic ? tr("Automatic fetch failed: %1") : tr("Fetch failed: %1")).arg(firstLine(err));
        else if (m_state.behind > 0)
            message = m_state.behind == 1 ? tr("Fetched — 1 commit to pull from %1").arg(upstream)
                                          : tr("Fetched — %1 commits to pull from %2").arg(m_state.behind).arg(upstream);
        else if (m_state.hasUpstream())
            message = tr("Fetched — up to date with %1").arg(upstream);
        else
            message = tr("Fetched");
        break;
    case Pull:
        if (cancelled)
            message = tr("Pull cancelled — not signed in");
        else if (!ok)
            message = failureText(err);
        else if (m_behindBefore > 0)
            message = m_behindBefore == 1 ? tr("Pulled 1 commit from %1").arg(upstream)
                                          : tr("Pulled %1 commits from %2").arg(m_behindBefore).arg(upstream);
        else
            message = tr("Already up to date with %1").arg(upstream);
        break;
    case Push:
        if (cancelled)
            message = tr("Push cancelled — not signed in");
        else if (!ok)
            message = failureText(err);
        else if (m_state.hasUpstream() && m_aheadBefore > 0)
            message = m_aheadBefore == 1 ? tr("Pushed 1 commit to %1").arg(upstream)
                                         : tr("Pushed %1 commits to %2").arg(m_aheadBefore).arg(upstream);
        else if (m_state.hasUpstream())
            message = tr("Everything up to date with %1").arg(upstream);
        else
            message = tr("Pushed");
        break;
    case None:
        break;
    }
    if (!ok && message.isEmpty())
        message = tr("git exited with status %1").arg(code);
    emit finished(op, ok, automatic, message);
    if (!keep.isEmpty())
        emit loginsToKeep(keep);
    scheduleAutoFetch();
}
