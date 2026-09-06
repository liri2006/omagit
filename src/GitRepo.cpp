#include "GitRepo.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

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

QByteArray GitRepo::run(const QStringList &args, int *exitCode, QByteArray *err, int timeoutMs) const
{
    QProcess p;
    p.setWorkingDirectory(m_root);
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

static FileChange::Kind kindFor(char x, char y)
{
    if (x == '?' && y == '?')
        return FileChange::Untracked;
    if (x == 'U' || y == 'U' || (x == 'A' && y == 'A') || (x == 'D' && y == 'D'))
        return FileChange::Unmerged;
    // Worktree state wins when it says something; otherwise index state.
    auto fromChar = [](char c) {
        switch (c) {
        case 'M': return FileChange::Modified;
        case 'A': return FileChange::Added;
        case 'D': return FileChange::Deleted;
        case 'R': return FileChange::Renamed;
        case 'C': return FileChange::Copied;
        case 'T': return FileChange::TypeChanged;
        default: return FileChange::Unknown;
        }
    };
    if (x == 'A' && y == 'M')
        return FileChange::Added;
    if (x == 'R' && y == 'M')
        return FileChange::Renamed;
    if (y != ' ' && y != '?')
        return fromChar(y);
    return fromChar(x);
}

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

    const QList<QByteArray> parts = out.split('\0');
    for (int i = 0; i < parts.size(); ++i) {
        const QByteArray &entry = parts[i];
        if (entry.size() < 4)
            continue;
        FileChange c;
        c.index = entry[0];
        c.worktree = entry[1];
        c.path = QString::fromUtf8(entry.mid(3));
        c.kind = kindFor(c.index, c.worktree);
        result.append(c);
    }

    // Rename detection for the combined (HEAD vs worktree) view.
    const QByteArray renames = run({QStringLiteral("diff"), QStringLiteral("HEAD"), QStringLiteral("-M"),
                                    QStringLiteral("--name-status"), QStringLiteral("-z")},
                                   &code);
    if (code == 0) {
        // Entries are "<status>\0<path>\0", renames/copies "<R|C>nnn\0<old>\0<new>\0".
        const QList<QByteArray> rp = renames.split('\0');
        for (int i = 0; i + 1 < rp.size();) {
            const QByteArray &st = rp[i];
            if (st.isEmpty() || (st[0] != 'R' && st[0] != 'C')) {
                i += 2;
                continue;
            }
            if (i + 2 >= rp.size())
                break;
            const QString oldPath = QString::fromUtf8(rp[i + 1]);
            const QString newPath = QString::fromUtf8(rp[i + 2]);
            i += 3;
            int newIdx = -1, oldIdx = -1;
            for (int k = 0; k < result.size(); ++k) {
                if (result[k].path == newPath)
                    newIdx = k;
                else if (result[k].path == oldPath && result[k].kind == FileChange::Deleted)
                    oldIdx = k;
            }
            if (newIdx >= 0) {
                result[newIdx].kind = st[0] == 'R' ? FileChange::Renamed : FileChange::Copied;
                result[newIdx].oldPath = oldPath;
                if (oldIdx >= 0 && st[0] == 'R')
                    result.removeAt(oldIdx);
            }
        }
    }

    // Line statistics (HEAD vs worktree). Untracked files count all lines as added.
    const QByteArray numstat = run({QStringLiteral("diff"), QStringLiteral("HEAD"), QStringLiteral("-M"),
                                    QStringLiteral("--numstat"), QStringLiteral("-z")},
                                   &code);
    if (code == 0) {
        const QList<QByteArray> np = numstat.split('\0');
        for (int i = 0; i < np.size(); ++i) {
            const QByteArray &line = np[i];
            const QList<QByteArray> cols = line.split('\t');
            if (cols.size() < 3)
                continue;
            QString path = QString::fromUtf8(cols[2]);
            if (path.isEmpty() && i + 2 < np.size()) { // rename: "add\tdel\t\0old\0new\0"
                path = QString::fromUtf8(np[i + 2]);
                i += 2;
            }
            for (FileChange &c : result) {
                if (c.path == path) {
                    c.binary = cols[0] == "-";
                    c.linesAdded = c.binary ? 0 : cols[0].toInt();
                    c.linesRemoved = c.binary ? 0 : cols[1].toInt();
                }
            }
        }
    }
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

    std::sort(result.begin(), result.end(), [](const FileChange &a, const FileChange &b) {
        return a.path.localeAwareCompare(b.path) < 0;
    });
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
        QStringList args{QStringLiteral("diff"), QStringLiteral("HEAD"), QStringLiteral("-M"), ctx, QStringLiteral("--")};
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

bool GitRepo::commit(const QString &message, const QStringList &paths, QString *error) const
{
    if (paths.isEmpty()) {
        if (error)
            *error = QStringLiteral("No files selected.");
        return false;
    }
    int code = 0;
    QByteArray err;
    // Stage the working-tree state of each path. A path that exists neither in
    // the worktree nor in the index (old side of a staged rename, `git rm`ed
    // file) is already staged and would make `git add` fail, so skip it.
    QStringList toAdd;
    for (const QString &p : paths) {
        if (QFile::exists(QDir(m_root).filePath(p))) {
            toAdd << p;
            continue;
        }
        const QByteArray tracked = run({QStringLiteral("ls-files"), QStringLiteral("--"), p}, &code);
        if (code == 0 && !tracked.trimmed().isEmpty())
            toAdd << p;
    }
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
