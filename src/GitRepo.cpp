#include "GitRepo.h"

#include <cstdio>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>

// The whole file as context, so the diff viewer can show it like
// a classic one-pane diff view.
static const QString kWholeFileContext = QStringLiteral("-U1000000");
// How much of an untracked file is read to count its lines; beyond that the
// count would cost more than it is worth.
static constexpr qint64 kUntrackedProbeBytes = 8 * 1024 * 1024;

QString FileChange::statusText() const
{
    switch (kind) {
    case Modified: return QStringLiteral("Modified");
    case Added: return QStringLiteral("Added");
    case Deleted: return QStringLiteral("Deleted");
    case Renamed: return QStringLiteral("Renamed");
    case Copied: return QStringLiteral("Copied");
    case TypeChanged: return QStringLiteral("Type changed");
    case Unmerged: return QStringLiteral("Conflicted");
    case Untracked: return QStringLiteral("Unknown");
    default: return QStringLiteral("Unknown");
    }
}

QString FileChange::extension() const
{
    const QString suffix = QFileInfo(path).suffix();
    return suffix.isEmpty() ? QString() : QStringLiteral(".") + suffix;
}

static QString gitExecutable()
{
    static const QString git = QStandardPaths::findExecutable(QStringLiteral("git"));
    return git.isEmpty() ? QStringLiteral("git") : git;
}

// `git diff --name-only -z a b`, both sides of a rename included.
static QStringList nulSeparated(const QByteArray &out)
{
    QStringList list;
    for (const QByteArray &entry : out.split('\0'))
        if (!entry.isEmpty())
            list << QString::fromUtf8(entry);
    return list;
}

// One name per line, as `git remote` and `for-each-ref` print them.
static QStringList trimmedLines(const QByteArray &out)
{
    QStringList list;
    for (const QByteArray &line : out.split('\n')) {
        const QString name = QString::fromUtf8(line).trimmed();
        if (!name.isEmpty())
            list << name;
    }
    return list;
}

namespace {
struct NumstatEntry {
    QString path;    // the new side
    QString oldPath; // renames only
    bool binary = false;
    int added = 0, removed = 0;
};
} // namespace

// Output of `git diff --numstat -z`: "add\tdel\tpath\0", or "add\tdel\t\0old\0new\0"
// for renames. Binary files report "-" for both counts.
static QList<NumstatEntry> parseNumstat(const QByteArray &numstat)
{
    QList<NumstatEntry> list;
    const QList<QByteArray> np = numstat.split('\0');
    for (int i = 0; i < np.size(); ++i) {
        const QList<QByteArray> cols = np[i].split('\t');
        if (cols.size() < 3)
            continue;
        NumstatEntry e;
        e.path = QString::fromUtf8(cols[2]);
        if (e.path.isEmpty() && i + 2 < np.size()) { // the rename form
            e.oldPath = QString::fromUtf8(np[i + 1]);
            e.path = QString::fromUtf8(np[i + 2]);
            i += 2;
        }
        e.binary = cols[0] == "-";
        if (!e.binary) {
            e.added = cols[0].toInt();
            e.removed = cols[1].toInt();
        }
        list.append(e);
    }
    return list;
}

GitRepo::GitRepo(const QString &root, QObject *parent)
    : QObject(parent), m_root(root)
{
}

QString GitRepo::findRoot(const QString &path, QString *error)
{
    QFileInfo info(path);
    const QString dir = info.isDir() ? info.absoluteFilePath() : info.absolutePath();

    QProcess p;
    p.setWorkingDirectory(dir);
    p.start(gitExecutable(), {QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel")});
    if (!p.waitForFinished(kProbeTimeoutMs) || p.exitCode() != 0) {
        if (error)
            *error = QString::fromUtf8(p.readAllStandardError()).trimmed();
        return QString();
    }
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

// OMAGIT_TRACE_GIT=1 in the environment prints every git command to stderr
// (to see, for instance, what keeps refreshing the window).
void GitRepo::traceCommand(const QStringList &args)
{
    static const bool trace = qEnvironmentVariableIsSet("OMAGIT_TRACE_GIT");
    if (!trace)
        return;
    // Straight to stderr: Qt's own logging may be routed to the journal.
    fprintf(stderr, "git %s\n", qPrintable(args.join(QLatin1Char(' '))));
    fflush(stderr);
}

QString GitRepo::GitResult::stderrText() const
{
    return QString::fromUtf8(err).trimmed();
}

QString GitRepo::GitResult::message() const
{
    // Some git commands (merge above all) explain a failure on stdout.
    const QString text = stderrText();
    return text.isEmpty() ? QString::fromUtf8(out).trimmed() : text;
}

bool GitRepo::report(const GitResult &r, QString *error, const QString &fallback)
{
    if (r.ok())
        return true;
    if (error) {
        *error = r.stderrText();
        if (error->isEmpty())
            *error = fallback;
    }
    return false;
}

bool GitRepo::fail(QString *error, const QString &message, const QByteArray &detail)
{
    if (error) {
        *error = message;
        if (!detail.trimmed().isEmpty())
            *error += QStringLiteral("\n") + QString::fromUtf8(detail).trimmed();
    }
    return false;
}

QStringList GitRepo::fullArgs(const QStringList &args)
{
    QStringList full{QStringLiteral("-c"), QStringLiteral("core.quotepath=off"),
                     QStringLiteral("-c"), QStringLiteral("color.ui=never")};
    full += args;
    return full;
}

void GitRepo::prepare(QProcess &p, const QStringList &env, bool terminalPrompt) const
{
    p.setWorkingDirectory(m_root);
    QProcessEnvironment pe = QProcessEnvironment::systemEnvironment();
    if (!terminalPrompt)
        pe.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    // status and diff would otherwise take .git/index.lock to refresh the
    // index, and that alone wakes the .git watcher, which asks for another
    // status: an endless refresh loop.
    pe.insert(QStringLiteral("GIT_OPTIONAL_LOCKS"), QStringLiteral("0"));
    for (const QString &kv : env) {
        const int eq = kv.indexOf(QLatin1Char('='));
        pe.insert(kv.left(eq), kv.mid(eq + 1));
    }
    p.setProcessEnvironment(pe);
}

GitRepo::GitResult GitRepo::exec(const QStringList &args, int timeoutMs, const QStringList &env) const
{
    GitResult r;
    QProcess p;
    prepare(p, env, true);
    traceCommand(args);
    p.start(gitExecutable(), fullArgs(args));
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        r.err = "git timed out";
        return r; // code stays -1
    }
    r.code = p.exitCode();
    r.err = p.readAllStandardError();
    r.out = p.readAllStandardOutput();
    return r;
}

QByteArray GitRepo::run(const QStringList &args, int *exitCode, QByteArray *err, int timeoutMs,
                        const QStringList &env) const
{
    const GitResult r = exec(args, timeoutMs, env);
    if (exitCode)
        *exitCode = r.code;
    if (err)
        *err = r.err;
    return r.out;
}

QProcess *GitRepo::runAsync(const QStringList &args, QObject *context, Callback done, int timeoutMs,
                            const QStringList &env)
{
    // Parented here, not to the context: a context that dies first (the merge
    // view closed with Escape) must not take a running git down with it. The
    // process cleans up after itself instead, whether or not anyone listens.
    auto *p = new QProcess(this);
    prepare(*p, env, false);
    traceCommand(args);

    auto *timeout = new QTimer(p);
    timeout->setSingleShot(true);
    timeout->setInterval(timeoutMs);
    connect(timeout, &QTimer::timeout, p, &QProcess::kill);
    connect(p, &QProcess::finished, p, &QObject::deleteLater);
    connect(p, &QProcess::errorOccurred, p, [p](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            p->deleteLater();
    });
    connect(p, &QProcess::finished, context, [p, done](int code, QProcess::ExitStatus status) {
        const QByteArray out = p->readAllStandardOutput();
        QByteArray err = p->readAllStandardError();
        if (status != QProcess::NormalExit) {
            code = -1;
            if (err.trimmed().isEmpty())
                err = "git did not finish (killed after the timeout)";
        }
        done(code, out, err);
    });
    connect(p, &QProcess::errorOccurred, context, [done](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return; // every other error is followed by finished()
        done(-1, QByteArray(), "could not start git");
    });
    p->start(gitExecutable(), fullArgs(args));
    timeout->start();
    return p;
}

void GitRepo::setRoot(const QString &root)
{
    if (root == m_root)
        return;
    m_root = root;
    m_amend = false;
    m_emptyTree.clear();
    emit rootChanged(root);
}

// The branch HEAD points at; empty (and `code` non-zero) when HEAD is detached
// or unborn.
QString GitRepo::symbolicHead(int *code) const
{
    const GitResult r = exec({QStringLiteral("symbolic-ref"), QStringLiteral("--short"), QStringLiteral("HEAD")});
    if (code)
        *code = r.code;
    return QString::fromUtf8(r.out).trimmed();
}

QString GitRepo::branch() const
{
    int code = 0;
    const QString head = symbolicHead(&code);
    if (code == 0)
        return head;
    const QByteArray out = run({QStringLiteral("rev-parse"), QStringLiteral("--short"), QStringLiteral("HEAD")}, &code);
    if (code == 0)
        return tr("detached at %1").arg(QString::fromUtf8(out).trimmed());
    return tr("(no commits yet)");
}

bool GitRepo::hasHead() const
{
    int code = 0;
    run({QStringLiteral("rev-parse"), QStringLiteral("--verify"), QStringLiteral("-q"), QStringLiteral("HEAD")}, &code);
    return code == 0;
}

QString GitRepo::emptyTree() const
{
    if (m_emptyTree.isEmpty()) {
        // Depends on the object format (SHA-1 vs SHA-256), so ask git.
        m_emptyTree = QString::fromUtf8(run({QStringLiteral("hash-object"), QStringLiteral("-t"),
                                             QStringLiteral("tree"), QStringLiteral("/dev/null")}))
                          .trimmed();
    }
    return m_emptyTree;
}

QString GitRepo::gitDir() const
{
    return QString::fromUtf8(run({QStringLiteral("rev-parse"), QStringLiteral("--absolute-git-dir")})).trimmed();
}

QStringList GitRepo::remotes() const
{
    const GitResult r = exec({QStringLiteral("remote")});
    return r.ok() ? trimmedLines(r.out) : QStringList();
}

UpstreamState GitRepo::upstreamState() const
{
    UpstreamState s;
    s.remotes = remotes();
    int code = 0;
    const QString head = symbolicHead(&code);
    if (code != 0) {
        s.detached = hasHead();
        return s;
    }
    s.branch = head;
    const QByteArray track = run({QStringLiteral("for-each-ref"),
                                  QStringLiteral("--format=%(upstream:short)%00%(upstream:remotename)"),
                                  QStringLiteral("refs/heads/") + s.branch},
                                 &code);
    const QList<QByteArray> parts = track.trimmed().split('\0');
    if (code == 0 && !parts.isEmpty()) {
        s.upstream = QString::fromUtf8(parts[0]);
        if (parts.size() > 1)
            s.remote = QString::fromUtf8(parts[1]);
    }
    if (s.remote.isEmpty()) {
        if (s.remotes.contains(QStringLiteral("origin")))
            s.remote = QStringLiteral("origin");
        else if (s.remotes.size() == 1)
            s.remote = s.remotes.first();
    }
    if (s.upstream.isEmpty())
        return s;
    const QByteArray counts = run({QStringLiteral("rev-list"), QStringLiteral("--left-right"), QStringLiteral("--count"),
                                   QStringLiteral("HEAD...") + s.upstream},
                                  &code);
    if (code != 0) {
        s.upstreamGone = true;
        return s;
    }
    const QList<QByteArray> cols = counts.trimmed().split('\t');
    if (cols.size() == 2) {
        s.ahead = cols[0].toInt();
        s.behind = cols[1].toInt();
    }
    return s;
}

BranchList GitRepo::branches() const
{
    BranchList b;
    int code = 0;
    const QByteArray out = run({QStringLiteral("for-each-ref"), QStringLiteral("--sort=refname"),
                                QStringLiteral("--format=%(refname)%00%(refname:short)%00%(HEAD)"),
                                QStringLiteral("refs/heads"), QStringLiteral("refs/remotes")},
                               &code);
    if (code != 0)
        return b;
    for (const QByteArray &line : out.split('\n')) {
        const QList<QByteArray> parts = line.split('\0');
        if (parts.size() < 3)
            continue;
        const QString ref = QString::fromUtf8(parts[0]);
        const QString name = QString::fromUtf8(parts[1]);
        if (ref.startsWith(QLatin1String("refs/heads/"))) {
            b.local << name;
            if (parts[2] == "*")
                b.current = name;
        } else if (ref.startsWith(QLatin1String("refs/remotes/")) && !ref.endsWith(QLatin1String("/HEAD"))) {
            b.remote << name;
        }
    }
    return b;
}

bool GitRepo::checkout(const QString &name, QString *error) const
{
    const BranchList b = branches();
    QStringList args{QStringLiteral("switch")};
    if (b.local.contains(name)) {
        args << name;
    } else {
        // "origin/feature": the local "feature" if there is one, else a new
        // one tracking the remote branch (the remote name may contain "/").
        QString local;
        for (const QString &remote : remotes()) {
            if (name.startsWith(remote + QLatin1Char('/'))) {
                local = name.mid(remote.size() + 1);
                break;
            }
        }
        if (!local.isEmpty() && b.local.contains(local))
            args << local;
        else
            args << QStringLiteral("--track") << name;
    }
    return report(exec(args, kWorkTimeoutMs), error, tr("git %1 failed").arg(args.join(QLatin1Char(' '))));
}

QString GitRepo::defaultBranch() const
{
    const BranchList b = branches();
    QStringList remotes = this->remotes();
    remotes.removeAll(QStringLiteral("origin"));
    remotes.prepend(QStringLiteral("origin"));
    for (const QString &remote : std::as_const(remotes)) {
        int code = 0;
        const QByteArray out = run({QStringLiteral("symbolic-ref"), QStringLiteral("-q"), QStringLiteral("--short"),
                                    QStringLiteral("refs/remotes/") + remote + QStringLiteral("/HEAD")},
                                   &code);
        if (code != 0)
            continue;
        const QString name = QString::fromUtf8(out).trimmed(); // "origin/main"
        const QString local = name.mid(remote.size() + 1);
        if (b.local.contains(local))
            return local;
        if (b.remote.contains(name))
            return name;
    }
    for (const char *candidate : {"main", "master", "trunk", "develop"}) {
        const QString name = QLatin1String(candidate);
        if (b.local.contains(name))
            return name;
    }
    return QString();
}

QStringList GitRepo::branchesByActivity() const
{
    const GitResult r = exec({QStringLiteral("for-each-ref"), QStringLiteral("--sort=-committerdate"),
                              QStringLiteral("--format=%(refname:short)"), QStringLiteral("refs/heads")});
    return r.ok() ? trimmedLines(r.out) : QStringList();
}

// ---------------------------------------------------------------------------
// Merging

QStringList GitRepo::changedPaths() const
{
    QStringList paths;
    for (const FileChange &c : porcelainStatus())
        paths << c.path;
    return paths;
}

// Every ref has to name a commit; the first one that does not gives the error.
bool GitRepo::verifyCommits(const QStringList &refs, QString *error) const
{
    for (const QString &ref : refs) {
        if (exec({QStringLiteral("rev-parse"), QStringLiteral("--verify"), QStringLiteral("-q"),
                  ref + QStringLiteral("^{commit}")})
                .ok())
            continue;
        if (error)
            *error = tr("%1 is not a branch or commit.").arg(ref);
        return false;
    }
    return true;
}

// How far the two sides have walked apart, in commits.
bool GitRepo::countMergeCommits(MergePreview &p) const
{
    const GitResult r = exec({QStringLiteral("rev-list"), QStringLiteral("--left-right"), QStringLiteral("--count"),
                              p.destination + QStringLiteral("...") + p.source},
                             kWorkTimeoutMs);
    if (!r.ok()) {
        p.error = r.stderrText();
        return false;
    }
    const QList<QByteArray> cols = r.out.trimmed().split('\t');
    if (cols.size() == 2) {
        p.diverged = cols[0].toInt();
        p.commits = cols[1].toInt();
    }
    return true;
}

// What the merge writes: the changes on source since the two parted. Fills in
// the file and line counts and returns the paths it touches, renames included.
QStringList GitRepo::collectMergeStats(MergePreview &p) const
{
    QStringList touched;
    const GitResult r = exec({QStringLiteral("diff"), QStringLiteral("--numstat"), QStringLiteral("-z"),
                              p.destination + QStringLiteral("...") + p.source},
                             kWorkTimeoutMs);
    if (!r.ok())
        return touched;
    for (const NumstatEntry &e : parseNumstat(r.out)) {
        if (!e.oldPath.isEmpty())
            touched << e.oldPath;
        touched << e.path;
        ++p.files;
        if (!e.binary) {
            p.added += e.added;
            p.removed += e.removed;
        }
    }
    return touched;
}

// git refuses to overwrite local changes: those in files the merge writes,
// and — when destination has to be checked out first — those in files that
// differ between HEAD and destination.
void GitRepo::findBlockedPaths(QStringList touched, MergePreview &p) const
{
    const QStringList dirty = changedPaths();
    if (dirty.isEmpty())
        return;
    if (branch() != p.destination) {
        const GitResult r = exec({QStringLiteral("diff"), QStringLiteral("--name-only"), QStringLiteral("-z"),
                                  QStringLiteral("HEAD"), p.destination},
                                 kWorkTimeoutMs);
        if (r.ok())
            touched += nulSeparated(r.out);
    }
    for (const QString &path : dirty)
        if (touched.contains(path) && !p.blocked.contains(path))
            p.blocked << path;
    p.blocked.sort();
}

// The verdict on the trees alone: "<tree>\0<conflicted path>\0...", exit 0
// clean, 1 conflicts, else an error.
void GitRepo::mergeTreeVerdict(MergePreview &p) const
{
    const GitResult r = exec({QStringLiteral("merge-tree"), QStringLiteral("--write-tree"), QStringLiteral("--name-only"),
                              QStringLiteral("--no-messages"), QStringLiteral("-z"), p.destination, p.source},
                             kHistoryTimeoutMs);
    if (r.code == 0) {
        p.outcome = MergePreview::Clean;
    } else if (r.code == 1) {
        p.outcome = MergePreview::Conflicts;
        p.conflicts = nulSeparated(r.out).mid(1);
        p.conflicts.sort();
    } else {
        p.error = r.stderrText();
        if (p.error.contains(QLatin1String("--write-tree")))
            p.error = tr("git 2.38 or newer is needed to check a merge ahead of time.");
    }
}

MergePreview GitRepo::mergePreview(const QString &source, const QString &destination) const
{
    MergePreview p;
    p.source = source;
    p.destination = destination;
    if (source.isEmpty() || destination.isEmpty()) {
        p.error = tr("Pick a branch on both sides.");
        return p;
    }
    if (source == destination) {
        p.outcome = MergePreview::Same;
        return p;
    }
    if (!verifyCommits({source, destination}, &p.error))
        return p;
    if (!countMergeCommits(p))
        return p;
    if (p.commits == 0) {
        p.outcome = MergePreview::UpToDate;
        return p;
    }
    findBlockedPaths(collectMergeStats(p), p);
    if (p.diverged == 0) {
        p.outcome = MergePreview::FastForward;
        return p;
    }
    mergeTreeVerdict(p);
    return p;
}

QStringList GitRepo::mergeArgs(const QString &source, bool noFastForward)
{
    return {QStringLiteral("merge"), QStringLiteral("--no-edit"),
            noFastForward ? QStringLiteral("--no-ff") : QStringLiteral("--ff"), source};
}

// What `git merge` made of it. A non-zero exit with MERGE_HEAD still around is
// a merge left to resolve, not a failure; git explains itself on stdout as
// often as on stderr.
GitRepo::MergeResult GitRepo::interpretMerge(int code, const QByteArray &out, const QByteArray &err,
                                             QString *error) const
{
    if (code == 0)
        return Merged;
    if (error)
        *error = GitResult{code, out, err}.message();
    return mergeInProgress() ? MergeConflicts : MergeFailed;
}

GitRepo::MergeResult GitRepo::merge(const QString &source, bool noFastForward, QString *error) const
{
    const GitResult r = exec(mergeArgs(source, noFastForward), kMergeTimeoutMs, {QStringLiteral("GIT_EDITOR=true")});
    return interpretMerge(r.code, r.out, r.err, error);
}

void GitRepo::mergeAsync(const QString &source, bool noFastForward, QObject *context,
                         std::function<void(MergeResult result, const QString &error)> done)
{
    runAsync(
        mergeArgs(source, noFastForward), context,
        [this, done](int code, const QByteArray &out, const QByteArray &err) {
            QString error;
            const MergeResult result = interpretMerge(code, out, err, &error);
            done(result, error);
        },
        kMergeTimeoutMs, {QStringLiteral("GIT_EDITOR=true")});
}

bool GitRepo::mergeInProgress() const
{
    return QFile::exists(gitDir() + QStringLiteral("/MERGE_HEAD"));
}

MergeState GitRepo::mergeState() const
{
    MergeState s;
    const QString dir = gitDir();
    if (!QFile::exists(dir + QStringLiteral("/MERGE_HEAD")))
        return s;
    s.inProgress = true;
    int code = 0;
    const QByteArray names = run({QStringLiteral("for-each-ref"), QStringLiteral("--points-at"), QStringLiteral("MERGE_HEAD"),
                                  QStringLiteral("--format=%(refname:short)"), QStringLiteral("refs/heads"),
                                  QStringLiteral("refs/remotes")},
                                 &code);
    if (code == 0)
        s.source = QString::fromUtf8(names.split('\n').value(0)).trimmed();
    if (s.source.isEmpty())
        s.source = QString::fromUtf8(run({QStringLiteral("rev-parse"), QStringLiteral("--short"), QStringLiteral("MERGE_HEAD")})).trimmed();
    const QByteArray unmerged = run({QStringLiteral("diff"), QStringLiteral("--name-only"), QStringLiteral("--diff-filter=U"),
                                     QStringLiteral("-z")},
                                    &code);
    if (code == 0)
        s.conflicts = nulSeparated(unmerged);
    QFile msg(dir + QStringLiteral("/MERGE_MSG"));
    if (msg.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QStringList lines;
        for (const QByteArray &line : msg.readAll().split('\n'))
            if (!line.startsWith('#'))
                lines << QString::fromUtf8(line);
        s.message = lines.join(QLatin1Char('\n')).trimmed();
    }
    return s;
}

bool GitRepo::abortMerge(QString *error) const
{
    return report(exec({QStringLiteral("merge"), QStringLiteral("--abort")}, kWorkTimeoutMs), error);
}

QString GitRepo::baseRef() const
{
    if (!m_amend)
        return QStringLiteral("HEAD");
    int code = 0;
    const QByteArray parent = run({QStringLiteral("rev-parse"), QStringLiteral("--verify"), QStringLiteral("-q"),
                                   QStringLiteral("HEAD^")},
                                  &code);
    return code == 0 ? QString::fromUtf8(parent).trimmed() : emptyTree();
}

// ---------------------------------------------------------------------------
// Parsing helpers

static FileChange::Kind kindFromLetter(char c)
{
    switch (c) {
    case 'M': return FileChange::Modified;
    case 'A': return FileChange::Added;
    case 'D': return FileChange::Deleted;
    case 'R': return FileChange::Renamed;
    case 'C': return FileChange::Copied;
    case 'T': return FileChange::TypeChanged;
    case 'U': return FileChange::Unmerged;
    default: return FileChange::Unknown;
    }
}

static FileChange::Kind kindFor(char x, char y)
{
    if (x == '?' && y == '?')
        return FileChange::Untracked;
    if (x == 'U' || y == 'U' || (x == 'A' && y == 'A') || (x == 'D' && y == 'D'))
        return FileChange::Unmerged;
    // Worktree state wins when it says something; otherwise index state.
    if (x == 'A' && y == 'M')
        return FileChange::Added;
    if (x == 'R' && y == 'M')
        return FileChange::Renamed;
    if (y != ' ' && y != '?')
        return kindFromLetter(y);
    return kindFromLetter(x);
}

namespace {
struct NameStatus {
    char status = ' ';
    QString oldPath; // renames / copies
    QString path;
};
} // namespace

// Output of `git diff --name-status -z`: "<status>\0<path>\0", or
// "<R|C>nnn\0<old>\0<new>\0" for renames and copies.
static QList<NameStatus> parseNameStatus(const QByteArray &out)
{
    QList<NameStatus> list;
    const QList<QByteArray> parts = out.split('\0');
    for (int i = 0; i + 1 < parts.size();) {
        const QByteArray &st = parts[i];
        if (st.isEmpty()) {
            ++i;
            continue;
        }
        NameStatus e;
        e.status = st[0];
        if (e.status == 'R' || e.status == 'C') {
            if (i + 2 >= parts.size())
                break;
            e.oldPath = QString::fromUtf8(parts[i + 1]);
            e.path = QString::fromUtf8(parts[i + 2]);
            i += 3;
        } else {
            e.path = QString::fromUtf8(parts[i + 1]);
            i += 2;
        }
        list.append(e);
    }
    return list;
}

// Line counts onto the matching changes; paths the list does not know are skipped.
void GitRepo::applyNumstat(const QByteArray &numstat, QList<FileChange> &changes) const
{
    QHash<QString, int> rowOf;
    for (int i = 0; i < changes.size(); ++i)
        rowOf.insert(changes[i].path, i);
    for (const NumstatEntry &e : parseNumstat(numstat)) {
        const auto it = rowOf.constFind(e.path);
        if (it == rowOf.constEnd())
            continue;
        FileChange &c = changes[it.value()];
        c.binary = e.binary;
        c.linesAdded = e.added;
        c.linesRemoved = e.removed;
    }
}

// Blob sizes from `git ls-tree -l -z <commit> -- <paths>`: "mode type sha size\tpath\0",
// with the size right-aligned. Paths missing from the commit (deleted) stay -1.
void GitRepo::applyTreeSizes(const QString &commit, QList<FileChange> &changes) const
{
    QHash<QString, int> rowOf;
    for (int i = 0; i < changes.size(); ++i)
        if (changes[i].kind != FileChange::Deleted)
            rowOf.insert(changes[i].path, i);
    const QStringList paths = rowOf.keys();
    constexpr int kChunk = 200; // keeps the command line short
    for (int start = 0; start < paths.size(); start += kChunk) {
        QStringList args{QStringLiteral("ls-tree"), QStringLiteral("-l"), QStringLiteral("-z"), commit, QStringLiteral("--")};
        args += paths.mid(start, kChunk);
        const GitResult r = exec(args, kWorkTimeoutMs);
        if (!r.ok())
            continue;
        for (const QByteArray &entry : r.out.split('\0')) {
            const int tab = entry.indexOf('\t');
            if (tab < 0)
                continue;
            const QList<QByteArray> meta = entry.left(tab).simplified().split(' ');
            if (meta.size() < 4 || meta[1] != "blob")
                continue;
            const auto it = rowOf.constFind(QString::fromUtf8(entry.mid(tab + 1)));
            if (it != rowOf.constEnd())
                changes[it.value()].size = meta[3].toLongLong();
        }
    }
}

// Case-insensitive compare with case as a tie-break so the order stays total.
static int foldedCompare(const QString &a, const QString &b)
{
    const int folded = QString::compare(a, b, Qt::CaseInsensitive);
    return folded != 0 ? folded : QString::compare(a, b);
}

// Directory first, then file name, so the files of a folder come before the
// files of its subfolders (a plain path compare would interleave them).
static void sortByPath(QList<FileChange> &changes)
{
    std::sort(changes.begin(), changes.end(), [](const FileChange &a, const FileChange &b) {
        const int as = a.path.lastIndexOf(QLatin1Char('/')), bs = b.path.lastIndexOf(QLatin1Char('/'));
        const int dir = foldedCompare(a.path.left(qMax(as, 0)), b.path.left(qMax(bs, 0)));
        if (dir != 0)
            return dir < 0;
        return foldedCompare(a.path.mid(as + 1), b.path.mid(bs + 1)) < 0;
    });
}

// ---------------------------------------------------------------------------
// Working tree

// Renames are detected by the callers' own diff; --no-renames keeps the parse
// simple and makes staged renames show up as delete+add unless git can pair them.
QList<FileChange> GitRepo::porcelainStatus(bool *ok) const
{
    const GitResult r = exec({QStringLiteral("status"), QStringLiteral("--porcelain=v1"), QStringLiteral("-z"),
                              QStringLiteral("--untracked-files=all"), QStringLiteral("--no-renames")});
    if (ok)
        *ok = r.ok();
    QList<FileChange> entries;
    if (!r.ok())
        return entries;
    for (const QByteArray &entry : r.out.split('\0')) {
        if (entry.size() < 4)
            continue;
        FileChange c;
        c.index = entry[0];
        c.worktree = entry[1];
        c.path = QString::fromUtf8(entry.mid(3));
        c.kind = kindFor(c.index, c.worktree);
        entries.append(c);
    }
    return entries;
}

QList<FileChange> GitRepo::status() const
{
    QList<FileChange> result;
    bool ok = false;
    const QList<FileChange> entries = porcelainStatus(&ok);
    if (!ok)
        return result;

    QHash<QString, QPair<char, char>> stateOf; // path -> (index, worktree)
    QHash<QString, int> rowOf;                 // path -> row in result
    for (const FileChange &c : entries) {
        stateOf.insert(c.path, {c.index, c.worktree});
        // When amending, tracked changes are taken from the diff against the
        // parent commit below so the last commit's files are included.
        if (m_amend && c.kind != FileChange::Untracked && c.kind != FileChange::Unmerged)
            continue;
        rowOf.insert(c.path, result.size());
        result.append(c);
    }

    const QString base = baseRef();
    int code = 0;
    const QByteArray nameStatus = run({QStringLiteral("diff"), base, QStringLiteral("-M"),
                                       QStringLiteral("--name-status"), QStringLiteral("-z")},
                                      &code);
    // The old sides of renames drop out of the list; taking them out right away
    // would shift every row after them, so they are collected and cut at the end.
    QSet<int> dropped;
    if (code == 0) {
        for (const NameStatus &e : parseNameStatus(nameStatus)) {
            int idx = rowOf.value(e.path, -1);
            const bool rename = e.status == 'R' || e.status == 'C';
            if (idx < 0) {
                if (!m_amend)
                    continue; // index/worktree changes are all in the porcelain list already
                FileChange c;
                c.path = e.path;
                c.kind = kindFromLetter(e.status);
                const auto it = stateOf.constFind(e.path);
                c.index = it != stateOf.constEnd() ? it->first : e.status;
                c.worktree = it != stateOf.constEnd() ? it->second : ' ';
                idx = result.size();
                rowOf.insert(c.path, idx);
                result.append(c);
            } else if (!rename || result[idx].kind == FileChange::Unmerged) {
                continue;
            }
            if (rename) {
                result[idx].kind = e.status == 'R' ? FileChange::Renamed : FileChange::Copied;
                result[idx].oldPath = e.oldPath;
                if (e.status == 'R') {
                    const auto it = rowOf.constFind(e.oldPath);
                    if (it != rowOf.constEnd() && result[it.value()].kind == FileChange::Deleted) {
                        dropped.insert(it.value());
                        rowOf.remove(e.oldPath);
                    }
                }
            }
        }
    }
    if (!dropped.isEmpty()) {
        QList<FileChange> kept;
        kept.reserve(result.size() - dropped.size());
        for (int i = 0; i < result.size(); ++i)
            if (!dropped.contains(i))
                kept.append(result[i]);
        result = kept;
    }

    const QByteArray numstat = run({QStringLiteral("diff"), base, QStringLiteral("-M"),
                                    QStringLiteral("--numstat"), QStringLiteral("-z")},
                                   &code);
    if (code == 0)
        applyNumstat(numstat, result);

    // Untracked files count all lines as added.
    for (FileChange &c : result) {
        if (c.kind == FileChange::Untracked) {
            QFile f(QDir(m_root).filePath(c.path));
            if (f.open(QIODevice::ReadOnly)) {
                const QByteArray data = f.read(kUntrackedProbeBytes);
                if (data.contains('\0')) {
                    c.binary = true;
                    c.linesAdded = c.linesRemoved = 0;
                } else {
                    c.linesAdded = data.count('\n') + ((!data.isEmpty() && !data.endsWith('\n')) ? 1 : 0);
                    c.linesRemoved = 0;
                }
            }
        }
    }

    // The size of the file as it is in the working tree; deleted files have none.
    const QDir root(m_root);
    for (FileChange &c : result) {
        if (c.kind == FileChange::Deleted)
            continue;
        const QFileInfo info(root.filePath(c.path));
        if (info.exists())
            c.size = info.size();
    }

    sortByPath(result);
    return result;
}

QString GitRepo::diff(const FileChange &change, bool *binary) const
{
    if (binary)
        *binary = change.binary;
    int code = 0;
    QByteArray out;
    const QString &ctx = kWholeFileContext;
    if (change.kind == FileChange::Untracked) {
        out = run({QStringLiteral("diff"), QStringLiteral("--no-index"), ctx, QStringLiteral("--"),
                   QStringLiteral("/dev/null"), change.path},
                  &code);
    } else if (hasHead()) {
        QStringList args{QStringLiteral("diff"), baseRef(), QStringLiteral("-M"), ctx, QStringLiteral("--")};
        if (!change.oldPath.isEmpty())
            args << change.oldPath;
        args << change.path;
        out = run(args, &code);
    } else {
        // No commits yet: staged content vs empty tree, plus unstaged changes.
        out = run({QStringLiteral("diff"), QStringLiteral("--cached"), ctx, QStringLiteral("--"), change.path}, &code);
        if (out.isEmpty())
            out = run({QStringLiteral("diff"), QStringLiteral("--no-index"), ctx, QStringLiteral("--"),
                       QStringLiteral("/dev/null"), change.path},
                      &code);
    }
    if (binary && out.contains("Binary files"))
        *binary = true;
    return QString::fromUtf8(out);
}

// Paths `git add -A` can take: present in the working tree, or known to the
// index (so a deletion can be staged). Anything else (e.g. the old side of an
// already staged rename) would make git add fail with "did not match any files".
QStringList GitRepo::stageablePaths(const QStringList &paths, const QStringList &env) const
{
    QStringList out;
    for (const QString &p : paths) {
        if (QFile::exists(QDir(m_root).filePath(p))) {
            out << p;
            continue;
        }
        const GitResult r = exec({QStringLiteral("ls-files"), QStringLiteral("--"), p}, kQueryTimeoutMs, env);
        if (r.ok() && !r.out.trimmed().isEmpty())
            out << p;
    }
    return out;
}

// Stages the working-tree state of `paths` (deletions included); paths git
// would refuse are left out, and staging nothing counts as success.
//
// A file git ignores and the real index does not track (`git rm --cached` after
// a new .gitignore rule) counts as absent even though it is still on disk: it
// is dropped from `env`'s index, where `git add` would refuse it. Everything
// else is added with -f, so a deliberately tracked ignored file still stages
// into a scratch index that does not know it yet.
GitRepo::GitResult GitRepo::stageAll(const QStringList &paths, const QStringList &env, int timeoutMs) const
{
    GitResult r;
    r.code = 0;
    const QStringList stageable = stageablePaths(paths, env);
    if (stageable.isEmpty())
        return r;

    // No GIT_INDEX_FILE here: "untracked" means untracked in the real index.
    r = exec(QStringList{QStringLiteral("ls-files"), QStringLiteral("-z"), QStringLiteral("--others"),
                         QStringLiteral("--ignored"), QStringLiteral("--exclude-standard"), QStringLiteral("--")}
                 + stageable,
             kQueryTimeoutMs, {QStringLiteral("GIT_LITERAL_PATHSPECS=1")});
    if (!r.ok())
        return r;
    const QStringList ignoredList = nulSeparated(r.out);
    const QSet<QString> ignored(ignoredList.cbegin(), ignoredList.cend());

    QStringList toRemove, toAdd;
    for (const QString &p : stageable)
        (ignored.contains(p) ? toRemove : toAdd) << p;
    if (!toRemove.isEmpty()) {
        r = exec(QStringList{QStringLiteral("update-index"), QStringLiteral("--force-remove"), QStringLiteral("--")}
                     + toRemove,
                 timeoutMs, env);
        if (!r.ok())
            return r;
    }
    if (!toAdd.isEmpty())
        r = exec(QStringList{QStringLiteral("add"), QStringLiteral("-A"), QStringLiteral("-f"), QStringLiteral("--")} + toAdd,
                 timeoutMs, env);
    return r;
}

namespace {
// A throw-away index file, gone again when this goes out of scope.
struct ScratchIndex {
    explicit ScratchIndex(const QString &path) : path(path) { QFile::remove(path); }
    ~ScratchIndex() { QFile::remove(path); }
    QStringList env() const { return {QStringLiteral("GIT_INDEX_FILE=") + path}; }
    QString path;
};
} // namespace

QString GitRepo::patch(const QList<FileChange> &changes, int maxBytes) const
{
    QStringList tracked, untracked;
    for (const FileChange &c : changes) {
        if (c.kind == FileChange::Untracked) {
            untracked << c.path;
            continue;
        }
        if (!c.oldPath.isEmpty())
            tracked << c.oldPath;
        tracked << c.path;
    }
    tracked.removeDuplicates();

    QByteArray stat, body;
    if (!tracked.isEmpty()) {
        if (hasHead()) {
            const QStringList common{QStringLiteral("diff"), baseRef(), QStringLiteral("-M"), QStringLiteral("--no-color")};
            stat = run(common + QStringList{QStringLiteral("--stat=100"), QStringLiteral("--")} + tracked);
            body = run(common + QStringList{QStringLiteral("--")} + tracked);
        } else {
            // No commits yet: what is staged against the empty tree.
            const QStringList common{QStringLiteral("diff"), QStringLiteral("--cached"), QStringLiteral("--no-color")};
            stat = run(common + QStringList{QStringLiteral("--stat=100"), QStringLiteral("--")} + tracked);
            body = run(common + QStringList{QStringLiteral("--")} + tracked);
        }
    }
    for (const QString &path : std::as_const(untracked)) {
        int code = 0;
        // Exit code 1 is "there are differences", the point of the exercise.
        body += run({QStringLiteral("diff"), QStringLiteral("--no-index"), QStringLiteral("--no-color"),
                     QStringLiteral("--"), QStringLiteral("/dev/null"), path},
                    &code);
        stat += QStringLiteral(" %1 (new file)\n").arg(path).toUtf8();
    }

    QString out;
    if (!stat.trimmed().isEmpty())
        out += QString::fromUtf8(stat).trimmed() + QStringLiteral("\n\n");
    QString text = QString::fromUtf8(body);
    if (text.size() > maxBytes) {
        int cut = text.lastIndexOf(QLatin1Char('\n'), maxBytes);
        if (cut < maxBytes / 2)
            cut = maxBytes;
        text = text.left(cut) + QStringLiteral("\n[... diff truncated; the file list above is complete ...]\n");
    }
    return out + text;
}

bool GitRepo::discardChanges(const FileChange &change, QString *error) const
{
    if (error)
        error->clear();
    QStringList paths{change.path};
    if (change.kind == FileChange::Renamed && !change.oldPath.isEmpty())
        paths << change.oldPath;
    for (const QString &path : paths) {
        if (path.isEmpty() || QDir::isAbsolutePath(path) || QDir::cleanPath(path) != path
            || path == QStringLiteral("..") || path.startsWith(QStringLiteral("../")))
            return fail(error, tr("Invalid repository file path."));
    }

    const QStringList env{QStringLiteral("GIT_LITERAL_PATHSPECS=1")};
    if (change.isUntracked()) {
        // Recheck the index in case the file was staged while the menu was open.
        const GitResult r = exec({QStringLiteral("ls-files"), QStringLiteral("-z"), QStringLiteral("--"), change.path},
                                 kQueryTimeoutMs, env);
        if (!r.ok())
            return fail(error, r.stderrText());
        if (r.out.isEmpty()) {
            QFile file(QDir(m_root).filePath(change.path));
            if (!file.remove())
                return fail(error, file.errorString());
            return true;
        }
    }

    QStringList args{QStringLiteral("restore"), QStringLiteral("--source=")
                        + (hasHead() ? QStringLiteral("HEAD") : emptyTree()),
                     QStringLiteral("--staged"), QStringLiteral("--worktree"), QStringLiteral("--")};
    args += paths;
    return report(exec(args, kQueryTimeoutMs, env), error);
}

bool GitRepo::commit(const QString &message, const QStringList &paths, QString *error) const
{
    if (paths.isEmpty()) {
        if (error)
            *error = tr("No files selected.");
        return false;
    }
    const QStringList commit{QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-m"), message};
    // A partial commit is impossible during a merge: what is staged — the
    // checked files just added plus git's own merge result — is committed.
    if (mergeInProgress()) {
        if (!report(stageAll(paths, {}, kQueryTimeoutMs), error))
            return false;
        return report(exec(commit, kWorkTimeoutMs), error);
    }

    // What `git commit -- paths` does, with the staging done by stageAll():
    // git's own version re-adds a file that is still on disk, so the removal
    // of a newly ignored file would silently drop out of the commit.
    ScratchIndex scratch(gitDir() + QStringLiteral("/omagit-commit-index"));
    const QStringList env = scratch.env();
    QStringList readTree{QStringLiteral("read-tree")};
    readTree << (hasHead() ? QStringLiteral("HEAD") : QStringLiteral("--empty"));
    GitResult r = exec(readTree, kWorkTimeoutMs, env);
    if (!r.ok())
        return fail(error, tr("Could not read the HEAD tree."), r.err);
    r = stageAll(paths, env, kWorkTimeoutMs);
    if (!r.ok())
        return fail(error, tr("Could not stage the selected files."), r.err);
    // Hooks run as usual, against the scratch index (as with `git commit -- paths`).
    if (!report(exec(commit, kWorkTimeoutMs, env), error))
        return false;

    // Stage the committed state in the real index so the paths no longer show
    // up as pending changes.
    stageAll(paths, {}, kWorkTimeoutMs);
    return true;
}

bool GitRepo::amendCommit(const QString &message, const QStringList &paths, QString *error) const
{
    const Commit head = headCommit();
    if (!head.isValid())
        return fail(error, tr("There is no commit to amend."));
    const QString base = head.parents.isEmpty() ? emptyTree() : head.parents.first();

    // Build the new tree in a scratch index: parent tree + working-tree state
    // of the chosen paths. The real index is untouched until the commit exists.
    ScratchIndex scratch(gitDir() + QStringLiteral("/omagit-amend-index"));
    const QStringList env = scratch.env();

    QStringList readTree{QStringLiteral("read-tree")};
    if (head.parents.isEmpty())
        readTree << QStringLiteral("--empty");
    else
        readTree << base;
    GitResult r = exec(readTree, kWorkTimeoutMs, env);
    if (!r.ok())
        return fail(error, tr("Could not read the parent tree."), r.err);

    r = stageAll(paths, env, kWorkTimeoutMs);
    if (!r.ok())
        return fail(error, tr("Could not stage the selected files."), r.err);

    r = exec({QStringLiteral("write-tree")}, kWorkTimeoutMs, env);
    const QString tree = QString::fromUtf8(r.out).trimmed();
    if (!r.ok() || tree.isEmpty())
        return fail(error, tr("Could not write the tree."), r.err);

    // Same parents and author as the old commit; the committer is you, now.
    QStringList commitTree{QStringLiteral("commit-tree")};
    for (const QString &p : head.parents)
        commitTree << QStringLiteral("-p") << p;
    commitTree << QStringLiteral("-m") << message << tree;
    const QStringList authorEnv{QStringLiteral("GIT_AUTHOR_NAME=") + head.author,
                                QStringLiteral("GIT_AUTHOR_EMAIL=") + head.email,
                                QStringLiteral("GIT_AUTHOR_DATE=") + head.date.toString(Qt::ISODate)};
    r = exec(commitTree, kWorkTimeoutMs, authorEnv);
    const QString newHash = QString::fromUtf8(r.out).trimmed();
    if (!r.ok() || newHash.isEmpty())
        return fail(error, tr("Could not create the commit."), r.err);

    // Move HEAD only if it still is the commit we amended.
    r = exec({QStringLiteral("update-ref"), QStringLiteral("-m"),
              QStringLiteral("commit (amend): ") + message.section(QLatin1Char('\n'), 0, 0),
              QStringLiteral("HEAD"), newHash, head.hash});
    if (!r.ok())
        return fail(error, tr("Could not update HEAD."), r.err);

    // Stage the committed state of the chosen paths in the real index so they
    // no longer show up as pending changes.
    stageAll(paths, {}, kWorkTimeoutMs);
    return true;
}

Commit GitRepo::headCommit() const
{
    bool ok = false;
    const QList<Commit> list = log(0, 1, false, &ok);
    return ok && !list.isEmpty() ? list.first() : Commit();
}

QString GitRepo::headMessage() const
{
    int code = 0;
    const QByteArray out = run({QStringLiteral("log"), QStringLiteral("-1"), QStringLiteral("--format=%B")}, &code);
    return code == 0 ? QString::fromUtf8(out).trimmed() : QString();
}

QStringList GitRepo::headPaths() const
{
    const Commit head = headCommit();
    if (!head.isValid())
        return {};
    QStringList paths;
    for (const FileChange &c : commitChanges(head)) {
        paths << c.path;
        if (!c.oldPath.isEmpty())
            paths << c.oldPath;
    }
    return paths;
}

QStringList GitRepo::remoteBranchesContainingHead() const
{
    const GitResult r = exec({QStringLiteral("branch"), QStringLiteral("-r"), QStringLiteral("--format=%(refname:short)"),
                              QStringLiteral("--contains"), QStringLiteral("HEAD")});
    if (!r.ok())
        return {};
    QStringList list = trimmedLines(r.out);
    list.removeIf([](const QString &name) { return name.endsWith(QLatin1String("/HEAD")); });
    return list;
}

// ---------------------------------------------------------------------------
// History

QList<Commit> GitRepo::log(int skip, int count, bool allRefs, bool *ok) const
{
    QStringList args{QStringLiteral("log"), QStringLiteral("-z"), QStringLiteral("--date-order"),
                     QStringLiteral("--format=%H%x1f%h%x1f%P%x1f%an%x1f%ae%x1f%aI%x1f%s%x1f%b"),
                     QStringLiteral("--max-count=%1").arg(count), QStringLiteral("--skip=%1").arg(skip)};
    if (allRefs)
        args << QStringLiteral("--all");
    const GitResult r = exec(args, kHistoryTimeoutMs);
    if (ok)
        *ok = r.ok();
    QList<Commit> commits;
    if (!r.ok())
        return commits;
    for (const QByteArray &record : r.out.split('\0')) {
        if (record.isEmpty())
            continue;
        const QList<QByteArray> f = record.split('\x1f');
        if (f.size() < 7)
            continue;
        Commit c;
        c.hash = QString::fromUtf8(f[0]);
        c.shortHash = QString::fromUtf8(f[1]);
        c.parents = QString::fromUtf8(f[2]).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        c.author = QString::fromUtf8(f[3]);
        c.email = QString::fromUtf8(f[4]);
        c.date = QDateTime::fromString(QString::fromUtf8(f[5]), Qt::ISODate);
        c.subject = QString::fromUtf8(f[6]);
        if (f.size() > 7)
            c.body = QString::fromUtf8(f[7]).trimmed();
        commits.append(c);
    }
    return commits;
}

QHash<QString, QList<RefLabel>> GitRepo::refs() const
{
    QHash<QString, QList<RefLabel>> map;
    int code = 0;
    const QByteArray out = run({QStringLiteral("for-each-ref"),
                                QStringLiteral("--format=%(objectname)%00%(refname)%00%(*objectname)")},
                               &code);
    if (code != 0)
        return map;
    const QString headBranch = symbolicHead(&code);
    const bool detached = code != 0;

    for (const QByteArray &line : out.split('\n')) {
        const QList<QByteArray> parts = line.split('\0');
        if (parts.size() < 2)
            continue;
        // Annotated tags: label the commit the tag points at, not the tag object.
        const QString hash = parts.size() > 2 && !parts[2].isEmpty() ? QString::fromUtf8(parts[2])
                                                                     : QString::fromUtf8(parts[0]);
        const QString ref = QString::fromUtf8(parts[1]);
        RefLabel label;
        if (ref.startsWith(QLatin1String("refs/heads/"))) {
            label.type = RefLabel::Branch;
            label.name = ref.mid(11);
            label.head = !detached && label.name == headBranch;
        } else if (ref.startsWith(QLatin1String("refs/remotes/"))) {
            label.type = RefLabel::Remote;
            label.name = ref.mid(13);
            if (label.name.endsWith(QLatin1String("/HEAD")))
                continue;
        } else if (ref.startsWith(QLatin1String("refs/tags/"))) {
            label.type = RefLabel::Tag;
            label.name = ref.mid(10);
        } else {
            continue; // stash, notes, ...
        }
        map[hash].append(label);
    }
    if (detached) {
        const QString head = QString::fromUtf8(run({QStringLiteral("rev-parse"), QStringLiteral("HEAD")}, &code)).trimmed();
        if (code == 0 && !head.isEmpty()) {
            RefLabel label;
            label.type = RefLabel::DetachedHead;
            label.name = QStringLiteral("HEAD");
            label.head = true;
            map[head].prepend(label);
        }
    }
    // HEAD's branch first, then branches, remotes, tags.
    for (auto it = map.begin(); it != map.end(); ++it) {
        std::stable_sort(it->begin(), it->end(), [](const RefLabel &a, const RefLabel &b) {
            if (a.head != b.head)
                return a.head;
            return a.type < b.type;
        });
    }
    return map;
}

QString GitRepo::parentOf(const Commit &commit) const
{
    return commit.parents.isEmpty() ? emptyTree() : commit.parents.first();
}

QList<FileChange> GitRepo::commitChanges(const Commit &commit) const
{
    QList<FileChange> result;
    if (!commit.isValid())
        return result;
    const QString base = parentOf(commit);
    int code = 0;
    const QByteArray nameStatus = run({QStringLiteral("diff"), QStringLiteral("-M"), QStringLiteral("--name-status"),
                                       QStringLiteral("-z"), base, commit.hash},
                                      &code, nullptr, kWorkTimeoutMs);
    if (code != 0)
        return result;
    for (const NameStatus &e : parseNameStatus(nameStatus)) {
        FileChange c;
        c.path = e.path;
        c.oldPath = e.oldPath;
        c.kind = kindFromLetter(e.status);
        c.index = e.status;
        result.append(c);
    }
    const QByteArray numstat = run({QStringLiteral("diff"), QStringLiteral("-M"), QStringLiteral("--numstat"),
                                    QStringLiteral("-z"), base, commit.hash},
                                   &code, nullptr, kWorkTimeoutMs);
    if (code == 0)
        applyNumstat(numstat, result);
    applyTreeSizes(commit.hash, result);
    sortByPath(result);
    return result;
}

QString GitRepo::commitDiff(const Commit &commit, const FileChange &change, bool *binary) const
{
    if (binary)
        *binary = change.binary;
    QStringList args{QStringLiteral("diff"), QStringLiteral("-M"), kWholeFileContext, parentOf(commit),
                     commit.hash, QStringLiteral("--")};
    if (!change.oldPath.isEmpty())
        args << change.oldPath;
    args << change.path;
    const QByteArray out = run(args, nullptr, nullptr, kWorkTimeoutMs);
    if (binary && out.contains("Binary files"))
        *binary = true;
    return QString::fromUtf8(out);
}
