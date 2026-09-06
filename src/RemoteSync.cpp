#include "RemoteSync.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>

namespace {
constexpr int kMaxBackoff = 15 * 60; // seconds
constexpr int kNudgeAge = 45;        // seconds; a nudge fetches when the last fetch is older

QString firstLine(const QByteArray &text)
{
    const QString s = QString::fromUtf8(text).trimmed();
    // git prefixes its errors with "fatal: " / "error: "; the dialog says that already.
    QString line = s.section(QLatin1Char('\n'), 0, 0);
    for (const char *prefix : {"fatal: ", "error: "})
        if (line.startsWith(QLatin1String(prefix)))
            line = line.mid(int(strlen(prefix)));
    return line;
}
} // namespace

RemoteSync::RemoteSync(GitRepo *repo, QObject *parent)
    : QObject(parent), m_repo(repo)
{
    m_autoTimer.setSingleShot(true);
    connect(&m_autoTimer, &QTimer::timeout, this, &RemoteSync::autoFetch);
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(400);
    connect(&m_debounce, &QTimer::timeout, this, &RemoteSync::repositoryChanged);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        watchGitDir(); // new ref directories (first fetch of a remote) need a watch too
        m_debounce.start();
    });
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] {
        watchGitDir(); // files replaced by rename drop out of the watch
        m_debounce.start();
    });
    connect(m_repo, &GitRepo::rootChanged, this, &RemoteSync::reset);
    m_gitDir = m_repo->gitDir();
    watchGitDir();
    refreshState();
}

void RemoteSync::reset()
{
    if (m_process) {
        // The old repository's fetch is of no interest any more (and its
        // result would be read as the new one's).
        disconnect(m_process, nullptr, this, nullptr);
        m_process->kill();
        m_process->waitForFinished(2000);
        m_process->deleteLater();
        m_process = nullptr;
    }
    m_op = None;
    m_autoOp = false;
    m_lastFetch = QDateTime();
    m_lastFetchOk = true;
    m_lastFetchError.clear();
    m_failures = 0;
    if (!m_watcher.directories().isEmpty())
        m_watcher.removePaths(m_watcher.directories());
    if (!m_watcher.files().isEmpty())
        m_watcher.removePaths(m_watcher.files());
    m_gitDir = m_repo->gitDir();
    watchGitDir();
    refreshState();
    nudge();
}

// A running fetch is not worth waiting for. Waiting emits finished(), so the
// callback is detached first: the window owning this object is half gone.
RemoteSync::~RemoteSync()
{
    if (m_process) {
        disconnect(m_process, nullptr, this, nullptr);
        m_process->kill();
        m_process->waitForFinished(2000);
    }
}

// Loose refs change by rename inside refs/heads and refs/remotes/<remote>,
// packed refs and HEAD by rename inside the git directory itself, and
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
        m_autoTimer.start(1500); // let the window paint first
    else
        scheduleAutoFetch();
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
    m_autoTimer.stop();
    emit stateChanged();
    QStringList full = args;
    if (op == Fetch && m_autoOp)
        full << QStringLiteral("--quiet");
    m_process = m_repo->runAsync(full, this, [this, op](int code, const QByteArray &out, const QByteArray &err) {
        onFinished(op, code, out, err);
    }, op == Fetch ? 90000 : 300000);
}

void RemoteSync::onFinished(Op op, int code, const QByteArray &out, const QByteArray &err)
{
    Q_UNUSED(out)
    const bool ok = code == 0;
    const bool automatic = m_autoOp;
    m_op = None;
    m_autoOp = false;
    watchGitDir();
    refreshState(); // also emits stateChanged, which shows the new counts

    QString message;
    const QString upstream = m_state.upstream;
    switch (op) {
    case Fetch:
        m_lastFetch = QDateTime::currentDateTime();
        m_lastFetchOk = ok;
        m_lastFetchError = ok ? QString() : firstLine(err);
        m_failures = ok ? 0 : m_failures + 1;
        if (!ok)
            message = (automatic ? tr("Automatic fetch failed: %1") : tr("Fetch failed: %1")).arg(firstLine(err));
        else if (m_state.behind > 0)
            message = tr("Fetched — %n commit(s) to pull from %1", nullptr, m_state.behind).arg(upstream);
        else if (m_state.hasUpstream())
            message = tr("Fetched — up to date with %1").arg(upstream);
        else
            message = tr("Fetched");
        break;
    case Pull:
        if (!ok)
            message = firstLine(err);
        else if (m_behindBefore > 0)
            message = tr("Pulled %n commit(s) from %1", nullptr, m_behindBefore).arg(upstream);
        else
            message = tr("Already up to date with %1").arg(upstream);
        break;
    case Push:
        if (!ok)
            message = firstLine(err);
        else if (m_state.hasUpstream() && m_aheadBefore > 0)
            message = tr("Pushed %n commit(s) to %1", nullptr, m_aheadBefore).arg(upstream);
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
    if (!ok && !err.trimmed().isEmpty() && op != Fetch)
        message += QStringLiteral("\n\n") + QString::fromUtf8(err).trimmed();
    emit finished(op, ok, automatic, message);
    scheduleAutoFetch();
}
