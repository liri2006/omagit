#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

struct FileChange {
    enum Kind { Modified, Added, Deleted, Renamed, Copied, TypeChanged, Unmerged, Untracked, Unknown };

    QString path;      // repo-relative path (new path for renames)
    QString oldPath;   // for renames/copies
    char index = ' ';  // X column of porcelain status
    char worktree = ' '; // Y column
    Kind kind = Unknown;
    int linesAdded = -1;
    int linesRemoved = -1;
    bool binary = false;

    bool isUntracked() const { return kind == Untracked; }
    bool isStaged() const { return index != ' ' && index != '?' && index != '!'; }
    QString statusText() const;
    QString extension() const;
};

class GitRepo : public QObject
{
    Q_OBJECT
public:
    explicit GitRepo(const QString &root, QObject *parent = nullptr);

    // Resolve the top-level directory of the repository containing `path`.
    static QString findRoot(const QString &path, QString *error = nullptr);

    QString root() const { return m_root; }
    QString branch() const;
    bool hasHead() const;

    QList<FileChange> status() const;

    // Unified diff of the working tree against HEAD (or an empty tree for
    // untracked files) with the whole file as context so the viewer can show
    // the complete file like a classic one-pane diff view.
    QString diff(const FileChange &change, bool *binary = nullptr) const;

    bool commit(const QString &message, const QStringList &paths, QString *error) const;

    // Runs git and returns stdout. exitCode receives the exit status.
    QByteArray run(const QStringList &args, int *exitCode = nullptr, QByteArray *err = nullptr,
                   int timeoutMs = 15000) const;

private:
    QString m_root;
};
