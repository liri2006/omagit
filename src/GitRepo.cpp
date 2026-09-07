#include "GitRepo.h"

#include <cstdio>

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>

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
    if (!p.waitForFinished(5000) || p.exitCode() != 0) {
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

QByteArray GitRepo::run(const QStringList &args, int *exitCode, QByteArray *err, int timeoutMs,
                        const QStringList &env) const
{
    QProcess p;
    p.setWorkingDirectory(m_root);
    QProcessEnvironment pe = QProcessEnvironment::systemEnvironment();
    // status and diff would otherwise take .git/index.lock to refresh the
    // index, and that alone wakes the .git watcher, which asks for another
    // status: an endless refresh loop.
    pe.insert(QStringLiteral("GIT_OPTIONAL_LOCKS"), QStringLiteral("0"));
    for (const QString &kv : env) {
        const int eq = kv.indexOf(QLatin1Char('='));
        pe.insert(kv.left(eq), kv.mid(eq + 1));
    }
    p.setProcessEnvironment(pe);
    QStringList full{QStringLiteral("-c"), QStringLiteral("core.quotepath=off"),
                     QStringLiteral("-c"), QStringLiteral("color.ui=never")};
    full += args;
    traceCommand(args);
    p.start(gitExecutable(), full);
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        if (exitCode)
            *exitCode = -1;
        if (err)
            *err = "git timed out";
        return QByteArray();
    }
    if (exitCode)
        *exitCode = p.exitCode();
    if (err)
        *err = p.readAllStandardError();
    return p.readAllStandardOutput();
}

QProcess *GitRepo::runAsync(const QStringList &args, QObject *context, Callback done, int timeoutMs,
                            const QStringList &env)
{
    auto *p = new QProcess(this);
    p->setWorkingDirectory(m_root);
    QProcessEnvironment pe = QProcessEnvironment::systemEnvironment();
    pe.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    pe.insert(QStringLiteral("GIT_OPTIONAL_LOCKS"), QStringLiteral("0"));
    for (const QString &kv : env) {
        const int eq = kv.indexOf(QLatin1Char('='));
        pe.insert(kv.left(eq), kv.mid(eq + 1));
    }
    p->setProcessEnvironment(pe);
    QStringList full{QStringLiteral("-c"), QStringLiteral("core.quotepath=off"),
                     QStringLiteral("-c"), QStringLiteral("color.ui=never")};
    full += args;
    traceCommand(args);

    auto *timeout = new QTimer(p);
    timeout->setSingleShot(true);
    timeout->setInterval(timeoutMs);
    connect(timeout, &QTimer::timeout, p, &QProcess::kill);
    connect(p, &QProcess::finished, context, [p, done](int code, QProcess::ExitStatus status) {
        const QByteArray out = p->readAllStandardOutput();
        QByteArray err = p->readAllStandardError();
        if (status != QProcess::NormalExit) {
            code = -1;
            if (err.trimmed().isEmpty())
                err = "git did not finish (killed after the timeout)";
        }
        p->deleteLater();
        done(code, out, err);
    });
    connect(p, &QProcess::errorOccurred, context, [p, done](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return; // every other error is followed by finished()
        p->deleteLater();
        done(-1, QByteArray(), "could not start git");
    });
    p->start(gitExecutable(), full);
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

QString GitRepo::branch() const
{
    int code = 0;
    QByteArray out = run({QStringLiteral("symbolic-ref"), QStringLiteral("--short"), QStringLiteral("HEAD")}, &code);
    if (code == 0)
        return QString::fromUtf8(out).trimmed();
    out = run({QStringLiteral("rev-parse"), QStringLiteral("--short"), QStringLiteral("HEAD")}, &code);
    if (code == 0)
        return QStringLiteral("detached at ") + QString::fromUtf8(out).trimmed();
    return QStringLiteral("(no commits yet)");
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
    int code = 0;
    const QByteArray out = run({QStringLiteral("remote")}, &code);
    QStringList list;
    if (code != 0)
        return list;
    for (const QByteArray &line : out.split('\n')) {
        const QString name = QString::fromUtf8(line).trimmed();
        if (!name.isEmpty())
            list << name;
    }
    return list;
}

UpstreamState GitRepo::upstreamState() const
{
    UpstreamState s;
    s.remotes = remotes();
    int code = 0;
    const QByteArray head = run({QStringLiteral("symbolic-ref"), QStringLiteral("--short"), QStringLiteral("HEAD")}, &code);
    if (code != 0) {
        s.detached = hasHead();
        return s;
    }
    s.branch = QString::fromUtf8(head).trimmed();
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
    int code = 0;
    QByteArray err;
    run(args, &code, &err, 60000);
    if (code != 0 && error) {
        *error = QString::fromUtf8(err).trimmed();
        if (error->isEmpty())
            *error = QStringLiteral("git %1 failed").arg(args.join(QLatin1Char(' ')));
    }
    return code == 0;
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

// Output of `git diff --numstat -z`: "add\tdel\tpath\0", or "add\tdel\t\0old\0new\0"
// for renames. Binary files report "-" for both counts.
void GitRepo::applyNumstat(const QByteArray &numstat, QList<FileChange> &changes) const
{
    const QList<QByteArray> np = numstat.split('\0');
    for (int i = 0; i < np.size(); ++i) {
        const QList<QByteArray> cols = np[i].split('\t');
        if (cols.size() < 3)
            continue;
        QString path = QString::fromUtf8(cols[2]);
        if (path.isEmpty() && i + 2 < np.size()) {
            path = QString::fromUtf8(np[i + 2]);
            i += 2;
        }
        for (FileChange &c : changes) {
            if (c.path == path) {
                c.binary = cols[0] == "-";
                c.linesAdded = c.binary ? 0 : cols[0].toInt();
                c.linesRemoved = c.binary ? 0 : cols[1].toInt();
            }
        }
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
        int code = 0;
        const QByteArray out = run(args, &code, nullptr, 60000);
        if (code != 0)
            continue;
        for (const QByteArray &entry : out.split('\0')) {
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

QList<FileChange> GitRepo::status() const
{
    QList<FileChange> result;
    int code = 0;
    const QByteArray out = run({QStringLiteral("status"), QStringLiteral("--porcelain=v1"), QStringLiteral("-z"),
                                QStringLiteral("--untracked-files=all"), QStringLiteral("--no-renames")},
                               &code);
    // Renames are detected separately below; --no-renames keeps the parse simple
    // and makes staged renames show up as delete+add unless git can pair them.
    if (code != 0)
        return result;

    QHash<QString, QPair<char, char>> stateOf; // path -> (index, worktree)
    const QList<QByteArray> parts = out.split('\0');
    for (const QByteArray &entry : parts) {
        if (entry.size() < 4)
            continue;
        FileChange c;
        c.index = entry[0];
        c.worktree = entry[1];
        c.path = QString::fromUtf8(entry.mid(3));
        c.kind = kindFor(c.index, c.worktree);
        stateOf.insert(c.path, {c.index, c.worktree});
        // When amending, tracked changes are taken from the diff against the
        // parent commit below so the last commit's files are included.
        if (m_amend && c.kind != FileChange::Untracked && c.kind != FileChange::Unmerged)
            continue;
        result.append(c);
    }

    const QString base = baseRef();
    const QByteArray nameStatus = run({QStringLiteral("diff"), base, QStringLiteral("-M"),
                                       QStringLiteral("--name-status"), QStringLiteral("-z")},
                                      &code);
    if (code == 0) {
        for (const NameStatus &e : parseNameStatus(nameStatus)) {
            int idx = -1;
            for (int k = 0; k < result.size() && idx < 0; ++k)
                if (result[k].path == e.path)
                    idx = k;
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
                result.append(c);
                idx = result.size() - 1;
            } else if (!rename || result[idx].kind == FileChange::Unmerged) {
                continue;
            }
            if (rename) {
                result[idx].kind = e.status == 'R' ? FileChange::Renamed : FileChange::Copied;
                result[idx].oldPath = e.oldPath;
                if (e.status == 'R') {
                    for (int k = 0; k < result.size(); ++k) {
                        if (result[k].path == e.oldPath && result[k].kind == FileChange::Deleted) {
                            result.removeAt(k);
                            break;
                        }
                    }
                }
            }
        }
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
                const QByteArray data = f.read(8 * 1024 * 1024);
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
    const QString ctx = QStringLiteral("-U1000000");
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
        int code = 0;
        const QByteArray tracked = run({QStringLiteral("ls-files"), QStringLiteral("--"), p}, &code, nullptr, 15000, env);
        if (code == 0 && !tracked.trimmed().isEmpty())
            out << p;
    }
    return out;
}

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
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    QStringList paths{change.path};
    if (change.kind == FileChange::Renamed && !change.oldPath.isEmpty())
        paths << change.oldPath;
    for (const QString &path : paths) {
        if (path.isEmpty() || QDir::isAbsolutePath(path) || QDir::cleanPath(path) != path
            || path == QStringLiteral("..") || path.startsWith(QStringLiteral("../")))
            return fail(QStringLiteral("Invalid repository file path."));
    }

    int code = 0;
    QByteArray err;
    const QStringList env{QStringLiteral("GIT_LITERAL_PATHSPECS=1")};
    if (change.isUntracked()) {
        // Recheck the index in case the file was staged while the menu was open.
        const QByteArray tracked = run({QStringLiteral("ls-files"), QStringLiteral("-z"),
                                       QStringLiteral("--"), change.path}, &code, &err, 15000, env);
        if (code != 0)
            return fail(QString::fromUtf8(err).trimmed());
        if (tracked.isEmpty()) {
            QFile file(QDir(m_root).filePath(change.path));
            if (!file.remove())
                return fail(file.errorString());
            return true;
        }
    }

    QStringList args{QStringLiteral("restore"), QStringLiteral("--source=")
                        + (hasHead() ? QStringLiteral("HEAD") : emptyTree()),
                     QStringLiteral("--staged"), QStringLiteral("--worktree"), QStringLiteral("--")};
    args += paths;
    run(args, &code, &err, 15000, env);
    return code == 0 || fail(QString::fromUtf8(err).trimmed());
}

bool GitRepo::commit(const QString &message, const QStringList &paths, QString *error) const
{
    if (paths.isEmpty()) {
        if (error)
            *error = QStringLiteral("No files selected.");
        return false;
    }
    int code = 0;
    QByteArray err;
    const QStringList toAdd = stageablePaths(paths, {});
    if (!toAdd.isEmpty()) {
        QStringList add{QStringLiteral("add"), QStringLiteral("-A"), QStringLiteral("--")};
        add += toAdd;
        run(add, &code, &err);
        if (code != 0) {
            if (error)
                *error = QString::fromUtf8(err).trimmed();
            return false;
        }
    }
    QStringList commit{QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-m"), message, QStringLiteral("--")};
    commit += paths;
    run(commit, &code, &err, 60000);
    if (code != 0) {
        if (error)
            *error = QString::fromUtf8(err).trimmed();
        return false;
    }
    return true;
}

bool GitRepo::amendCommit(const QString &message, const QStringList &paths, QString *error) const
{
    auto fail = [error](const QString &msg, const QByteArray &detail = QByteArray()) {
        if (error) {
            *error = msg;
            if (!detail.trimmed().isEmpty())
                *error += QStringLiteral("\n") + QString::fromUtf8(detail).trimmed();
        }
        return false;
    };

    const Commit head = headCommit();
    if (!head.isValid())
        return fail(QStringLiteral("There is no commit to amend."));
    const QString base = head.parents.isEmpty() ? emptyTree() : head.parents.first();

    // Build the new tree in a scratch index: parent tree + working-tree state
    // of the chosen paths. The real index is untouched until the commit exists.
    const QString tmpIndex = gitDir() + QStringLiteral("/omagit-amend-index");
    QFile::remove(tmpIndex);
    const QStringList env{QStringLiteral("GIT_INDEX_FILE=") + tmpIndex};
    struct Cleanup {
        QString path;
        ~Cleanup() { QFile::remove(path); }
    } cleanup{tmpIndex};

    int code = 0;
    QByteArray err;
    QStringList readTree{QStringLiteral("read-tree")};
    if (head.parents.isEmpty())
        readTree << QStringLiteral("--empty");
    else
        readTree << base;
    run(readTree, &code, &err, 60000, env);
    if (code != 0)
        return fail(QStringLiteral("Could not read the parent tree."), err);

    const QStringList toAdd = stageablePaths(paths, env);
    if (!toAdd.isEmpty()) {
        QStringList add{QStringLiteral("add"), QStringLiteral("-A"), QStringLiteral("--")};
        add += toAdd;
        run(add, &code, &err, 60000, env);
        if (code != 0)
            return fail(QStringLiteral("Could not stage the selected files."), err);
    }
    const QString tree = QString::fromUtf8(run({QStringLiteral("write-tree")}, &code, &err, 60000, env)).trimmed();
    if (code != 0 || tree.isEmpty())
        return fail(QStringLiteral("Could not write the tree."), err);

    // Same parents and author as the old commit; the committer is you, now.
    QStringList commitTree{QStringLiteral("commit-tree")};
    for (const QString &p : head.parents)
        commitTree << QStringLiteral("-p") << p;
    commitTree << QStringLiteral("-m") << message << tree;
    const QStringList authorEnv{QStringLiteral("GIT_AUTHOR_NAME=") + head.author,
                                QStringLiteral("GIT_AUTHOR_EMAIL=") + head.email,
                                QStringLiteral("GIT_AUTHOR_DATE=") + head.date.toString(Qt::ISODate)};
    const QString newHash = QString::fromUtf8(run(commitTree, &code, &err, 60000, authorEnv)).trimmed();
    if (code != 0 || newHash.isEmpty())
        return fail(QStringLiteral("Could not create the commit."), err);

    // Move HEAD only if it still is the commit we amended.
    run({QStringLiteral("update-ref"), QStringLiteral("-m"), QStringLiteral("commit (amend): ") + message.section(QLatin1Char('\n'), 0, 0),
         QStringLiteral("HEAD"), newHash, head.hash},
        &code, &err);
    if (code != 0)
        return fail(QStringLiteral("Could not update HEAD."), err);

    // Stage the committed state of the chosen paths in the real index so they
    // no longer show up as pending changes.
    const QStringList realAdd = stageablePaths(paths, {});
    if (!realAdd.isEmpty()) {
        QStringList add{QStringLiteral("add"), QStringLiteral("-A"), QStringLiteral("--")};
        add += realAdd;
        run(add, &code, &err, 60000);
    }
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
    int code = 0;
    const QByteArray out = run({QStringLiteral("branch"), QStringLiteral("-r"), QStringLiteral("--format=%(refname:short)"),
                                QStringLiteral("--contains"), QStringLiteral("HEAD")},
                               &code);
    if (code != 0)
        return {};
    QStringList list;
    for (const QByteArray &line : out.split('\n')) {
        const QString name = QString::fromUtf8(line).trimmed();
        if (!name.isEmpty() && !name.endsWith(QLatin1String("/HEAD")))
            list << name;
    }
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
    int code = 0;
    const QByteArray out = run(args, &code, nullptr, 120000);
    if (ok)
        *ok = code == 0;
    QList<Commit> commits;
    if (code != 0)
        return commits;
    for (const QByteArray &record : out.split('\0')) {
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
    const QString headBranch = QString::fromUtf8(
        run({QStringLiteral("symbolic-ref"), QStringLiteral("--short"), QStringLiteral("HEAD")}, &code)).trimmed();
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
                                      &code, nullptr, 60000);
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
                                   &code, nullptr, 60000);
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
    QStringList args{QStringLiteral("diff"), QStringLiteral("-M"), QStringLiteral("-U1000000"), parentOf(commit),
                     commit.hash, QStringLiteral("--")};
    if (!change.oldPath.isEmpty())
        args << change.oldPath;
    args << change.path;
    int code = 0;
    const QByteArray out = run(args, &code, nullptr, 60000);
    if (binary && out.contains("Binary files"))
        *binary = true;
    return QString::fromUtf8(out);
}
