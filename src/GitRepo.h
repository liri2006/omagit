#pragma once

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

class QProcess;

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
    qint64 size = -1;  // bytes of the file on the new side; -1 when it has none (deleted) or is unknown

    bool operator==(const FileChange &o) const
    {
        return path == o.path && oldPath == o.oldPath && index == o.index && worktree == o.worktree && kind == o.kind
            && linesAdded == o.linesAdded && linesRemoved == o.linesRemoved && binary == o.binary && size == o.size;
    }
    bool operator!=(const FileChange &o) const { return !(*this == o); }
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

    bool operator==(const RefLabel &o) const { return type == o.type && name == o.name && head == o.head; }
    bool operator!=(const RefLabel &o) const { return !(*this == o); }
};

// Where the current branch stands relative to its upstream.
struct UpstreamState {
    QString branch;        // empty when HEAD is detached or unborn
    bool detached = false;
    QString upstream;      // e.g. "origin/main"; empty when none is configured
    QString remote;        // the upstream's remote, else the remote a push would publish to
    bool upstreamGone = false; // configured, but the remote branch no longer exists
    int ahead = -1;        // commits to push, -1 when unknown
    int behind = -1;       // commits to pull, -1 when unknown
    QStringList remotes;

    bool hasUpstream() const { return !upstream.isEmpty() && !upstreamGone; }
};

// The branches of the repository, for the branch dropdown.
struct BranchList {
    QString current;   // the checked-out branch; empty when HEAD is detached or unborn
    QStringList local; // "main", "feature/x", sorted
    QStringList remote; // "origin/main", sorted; a remote's HEAD pointer is left out
};

class GitRepo : public QObject
{
    Q_OBJECT
public:
    explicit GitRepo(const QString &root, QObject *parent = nullptr);

    // Resolve the top-level directory of the repository containing `path`.
    static QString findRoot(const QString &path, QString *error = nullptr);

    QString root() const { return m_root; }
    // Points the object at another repository (the top-level directory of
    // one); the amend mode is dropped. Emits rootChanged().
    void setRoot(const QString &root);
    QString branch() const;
    bool hasHead() const;
    QString gitDir() const; // absolute path of the .git directory (also for worktrees)

    // --- Remotes ------------------------------------------------------------

    QStringList remotes() const;
    UpstreamState upstreamState() const;

    // --- Branches -----------------------------------------------------------

    BranchList branches() const;
    // Checks out `name`: a local branch, or a remote-tracking one
    // ("origin/x"), which switches to the local branch of the same name if
    // there is one and otherwise creates it tracking the remote branch.
    // Local changes are carried over; git refuses if they would be lost.
    bool checkout(const QString &name, QString *error) const;

    // Runs git without blocking; `done(exitCode, stdout, stderr)` is called from
    // the event loop when it finishes (exitCode -1: crashed, killed, or not
    // started), and not at all once `context` is gone. Credential prompts are
    // disabled, so a missing login fails instead of hanging. The returned
    // process is owned by this object.
    using Callback = std::function<void(int exitCode, const QByteArray &out, const QByteArray &err)>;
    QProcess *runAsync(const QStringList &args, QObject *context, Callback done, int timeoutMs = 120000,
                       const QStringList &env = QStringList());

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

    // The changes to `changes` as a compact unified diff for a reader that
    // cannot look at the repository (a coding agent writing the commit
    // message): a --stat summary of every file, then the patch with the
    // usual three lines of context, cut off at `maxBytes` with a note.
    // Untracked files show up as added.
    QString patch(const QList<FileChange> &changes, int maxBytes = 80000) const;

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

signals:
    void rootChanged(const QString &root);

private:
    static void traceCommand(const QStringList &args);
    QString emptyTree() const;
    QStringList stageablePaths(const QStringList &paths, const QStringList &env) const;
    void applyNumstat(const QByteArray &numstat, QList<FileChange> &changes) const;
    void applyTreeSizes(const QString &commit, QList<FileChange> &changes) const;

    QString m_root;
    bool m_amend = false;
    mutable QString m_emptyTree;
};
