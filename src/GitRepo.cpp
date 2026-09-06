#include "GitRepo.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

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

QByteArray GitRepo::run(const QStringList &args, int *exitCode, QByteArray *err, int timeoutMs,
                        const QStringList &env) const
{
    QProcess p;
    p.setWorkingDirectory(m_root);
    if (!env.isEmpty()) {
        QProcessEnvironment pe = QProcessEnvironment::systemEnvironment();
        for (const QString &kv : env) {
            const int eq = kv.indexOf(QLatin1Char('='));
            pe.insert(kv.left(eq), kv.mid(eq + 1));
        }
        p.setProcessEnvironment(pe);
    }
    QStringList full{QStringLiteral("-c"), QStringLiteral("core.quotepath=off"),
                     QStringLiteral("-c"), QStringLiteral("color.ui=never")};
    full += args;
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

static void sortByPath(QList<FileChange> &changes)
{
    std::sort(changes.begin(), changes.end(), [](const FileChange &a, const FileChange &b) {
        return a.path.localeAwareCompare(b.path) < 0;
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
