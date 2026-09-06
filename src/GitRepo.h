#pragma once

#include <QDateTime>
#include <QHash>
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

struct Commit {
    QString hash;
    QString shortHash;
    QStringList parents;
    QString author;
    QString email;
    QDateTime date;
    QString subject;
    QString body;

    bool isValid() const { return !hash.isEmpty(); }
};

// A branch/tag/remote decoration attached to a commit in the history view.
struct RefLabel {
    enum Type { Branch, Remote, Tag, DetachedHead };
    Type type = Branch;
    QString name;
    bool head = false; // the ref HEAD points at
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

    // --- Working tree -------------------------------------------------------

    // Amend mode compares the working tree against the parent of HEAD, so the
    // files of the last commit show up in the changes list and can be
    // re-committed (or left out) together with the new changes.
    void setAmend(bool on) { m_amend = on; }
    bool amending() const { return m_amend; }
    // "HEAD" normally; the parent of HEAD (or the empty tree) when amending.
    QString baseRef() const;

    QList<FileChange> status() const;

    // Unified diff of the working tree against baseRef() (or an empty tree for
    // untracked files) with the whole file as context so the viewer can show
    // the complete file like a classic one-pane diff view.
    QString diff(const FileChange &change, bool *binary = nullptr) const;

    bool commit(const QString &message, const QStringList &paths, QString *error) const;

    // Replace HEAD by a commit whose tree is HEAD's parent tree plus the
    // working-tree state of `paths`. Keeps HEAD's parents and author; files of
    // the old commit that are not in `paths` go back to the working tree.
    bool amendCommit(const QString &message, const QStringList &paths, QString *error) const;

    Commit headCommit() const;
    QString headMessage() const;
    QStringList headPaths() const;        // files touched by HEAD
    QStringList remoteBranchesContainingHead() const;

    // --- History ------------------------------------------------------------

    // `count` commits starting `skip` commits after the tip, parents never before
    // their children (--date-order). ok is false if git failed (e.g. no commits).
    QList<Commit> log(int skip, int count, bool allRefs, bool *ok = nullptr) const;
    QHash<QString, QList<RefLabel>> refs() const;
    QList<FileChange> commitChanges(const Commit &commit) const;
    QString commitDiff(const Commit &commit, const FileChange &change, bool *binary = nullptr) const;
    QString parentOf(const Commit &commit) const; // first parent or the empty tree

    // Runs git and returns stdout. exitCode receives the exit status. `env` holds
    // extra "KEY=VALUE" entries for the child process.
    QByteArray run(const QStringList &args, int *exitCode = nullptr, QByteArray *err = nullptr,
                   int timeoutMs = 15000, const QStringList &env = QStringList()) const;

private:
    QString emptyTree() const;
    QString gitDir() const;
    QStringList stageablePaths(const QStringList &paths, const QStringList &env) const;
    void applyNumstat(const QByteArray &numstat, QList<FileChange> &changes) const;

    QString m_root;
    bool m_amend = false;
    mutable QString m_emptyTree;
};
