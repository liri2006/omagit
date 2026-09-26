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

// The records of `git log -z` in the format GitRepo::log() asks for, read as
// they come in: feed() takes whatever the pipe had and returns the commits it
// completes, keeping what follows the last NUL for the next call, so a chunk
// may end anywhere — inside a multi-byte character too, as a record is only
// decoded whole. finish() returns what is left, for a last record git did not
// end with a NUL.
class LogStreamParser
{
public:
    QList<Commit> feed(const QByteArray &chunk);
    QList<Commit> finish();

private:
    QByteArray m_tail;
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

    bool isRemote(const QString &name) const { return !local.contains(name) && remote.contains(name); }
};

// What `git merge <source>` would do to `destination`, worked out on the
// trees alone (git merge-tree), so the working tree is not touched.
struct MergePreview {
    enum Outcome {
        Same,        // the same branch on both sides
        UpToDate,    // destination already contains source
        FastForward, // destination is behind source and just moves up to it
        Clean,       // a merge commit, no conflicts
        Conflicts,   // a merge commit git cannot complete on its own
        Failed       // git could not tell (unknown ref, git older than 2.38, ...)
    };
    Outcome outcome = Failed;
    QString source, destination;
    int commits = 0;   // commits of source that destination lacks
    int diverged = 0;  // commits of destination that source lacks (0: fast-forward)
    int files = 0, added = 0, removed = 0; // what the merge brings in
    QStringList conflicts; // paths git would leave with conflict markers
    QStringList blocked;   // paths with local changes git would refuse to overwrite
    QString error;

    bool isValid() const { return outcome != Failed; }
    bool canMerge() const { return (outcome == FastForward || outcome == Clean || outcome == Conflicts) && blocked.isEmpty(); }
};

// A merge git could not finish on its own: MERGE_HEAD exists and the
// conflicted files wait in the working tree.
struct MergeState {
    bool inProgress = false;
    QString source;        // the branch being merged in (its short hash when no branch points at it)
    QStringList conflicts; // paths still unmerged
    QString message;       // the commit message git proposes (MERGE_MSG without the comments)
};

// Every query runs a git process in the repository at root(). The const
// methods keep no state of their own beyond the empty-tree cache, so one
// worker thread may call them on its own instance (or on a shared one) as long
// as nothing calls setRoot() meanwhile; runAsync() and the methods built on it
// need the thread's event loop.
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
    // The repository's main line: what origin/HEAD points at (as the local
    // branch when there is one), else main/master/trunk/develop if it exists.
    QString defaultBranch() const;
    // The local branches, the one committed to most recently first.
    QStringList branchesByActivity() const;
    // The tags, the one made most recently first.
    QStringList tags() const;
    // Whether git takes `name` for a new branch as it is: `git check-ref-format
    // --branch` accepts it and prints it back unchanged (it expands "@{-1}" to
    // the previous branch's name and accepts that too), and it does not start
    // with a dash. "a b" and "HEAD" are refused.
    bool isValidBranchName(const QString &name) const;
    // What a branch name field makes of the text typed into it: every space
    // becomes a dash, the rest stays as typed.
    static QString typedBranchName(const QString &typed);
    // The commit `rev` names: its short hash and its subject (false when git
    // cannot resolve it).
    bool describeCommit(const QString &rev, QString *shortSha, QString *subject) const;
    // The changed files (changedPaths(), untracked included) that differ
    // between HEAD and `target`, sorted: what keeps `git switch` from moving
    // to it with the changes in tow.
    QStringList pathsBlockingSwitch(const QString &target) const;
    // The same for the changed paths `dirty` the caller has read already, so
    // that what it says of them comes from one look at the working tree.
    QStringList pathsBlockingSwitch(const QString &target, const QStringList &dirty) const;
    // Makes the branch `name` at `start` (HEAD when empty): `git switch -c
    // name [start]` when `switchTo`, else `git branch name [start]`. A
    // remote-tracking start is tracked, as git does by default. In a
    // repository without commits only switching works (it renames the unborn
    // branch). `error` gets git's message.
    bool createBranch(const QString &name, const QString &start, bool switchTo, QString *error) const;

    // --- Merging ------------------------------------------------------------

    MergePreview mergePreview(const QString &source, const QString &destination) const;
    // The command a merge of `source` into the current branch runs
    // (`git merge --no-edit [--no-ff] source`), for runAsync and tooltips.
    static QStringList mergeArgs(const QString &source, bool noFastForward);
    enum MergeResult { Merged, MergeConflicts, MergeFailed };
    // Merges `source` into the current branch. MergeConflicts leaves the
    // merge in progress (see mergeState()); `error` gets git's message.
    MergeResult merge(const QString &source, bool noFastForward, QString *error) const;
    // merge() without blocking: `done` is called from the event loop, and not
    // at all once `context` is gone.
    void mergeAsync(const QString &source, bool noFastForward, QObject *context,
                    std::function<void(MergeResult result, const QString &error)> done);
    MergeState mergeState() const;
    bool mergeInProgress() const;
    // `git merge --abort`: the branch and the working tree go back to how they were.
    bool abortMerge(QString *error) const;

    // Runs git without blocking; `done(exitCode, stdout, stderr)` is called from
    // the event loop when it finishes (exitCode -1: crashed, killed, or not
    // started), and not at all once `context` is gone. Credential prompts are
    // disabled, so a missing login fails instead of hanging. The returned
    // process is owned by this object and deletes itself when git is done,
    // so a context that goes first leaves git running to completion.
    using Callback = std::function<void(int exitCode, const QByteArray &out, const QByteArray &err)>;
    QProcess *runAsync(const QStringList &args, QObject *context, Callback done, int timeoutMs = kHistoryTimeoutMs,
                       const QStringList &env = QStringList());

    // The timeout above kills a git run that takes too long — but a run
    // stopped at a sign-in dialog is waiting for a person, not hanging, and
    // must not be counted out. Whoever shows the dialog holds the timer while
    // it is up and starts it afresh (from zero: git begins again where it
    // left off) once the answer is in. Both do nothing for a process that is
    // not from runAsync(), or that has finished already.
    static void holdTimeout(QProcess *process);
    static void resumeTimeout(QProcess *process);

    // --- Working tree -------------------------------------------------------

    // Amend mode compares the working tree against the parent of HEAD, so the
    // files of the last commit show up in the changes list and can be
    // re-committed (or left out) together with the new changes.
    void setAmend(bool on) { m_amend = on; }
    bool amending() const { return m_amend; }
    // "HEAD" normally; the parent of HEAD (or the empty tree) when amending.
    QString baseRef() const;

    QList<FileChange> status() const;
    // Every path `git status` lists, untracked included.
    QStringList changedPaths() const;
    // How many files are changed, untracked ones included: one for every
    // entry `git status` has, a rename (or a copy) counting once where
    // changedPaths() lists both of its paths.
    int changedFileCount() const;

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

    // Discard index and working-tree changes relative to HEAD (also in amend
    // mode). New files are removed; a rename restores both paths.
    bool discardChanges(const FileChange &change, QString *error) const;

    // Commit HEAD's tree plus the working-tree state of `paths`, like
    // `git commit -- paths`; other staged changes stay staged. During a merge
    // the whole index is committed.
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
    // The commits log() walks from, as the refs stand now: HEAD's, and with
    // `allRefs` every ref's (tags peeled to their commits, refs to anything
    // else left out), in a stable order. Empty without commits; ok is false
    // where git failed.
    QStringList logStartPoints(bool allRefs, bool *ok = nullptr) const;
    // The history log() pages through, walked from `startPoints`
    // (logStartPoints()) and nothing else, without blocking, from `skip`
    // commits in on: however the refs move meanwhile, the same start points
    // are the same walk, so a later call skips exactly the commits an earlier
    // one printed. `batch` gets the commits as git prints them, in log()'s
    // order, and `done(ok)` follows once git has exited (ok false where git
    // failed or was killed after the timeout). No start point is an empty
    // history: `done(true)` from the event loop, and no process. Neither is
    // called once `context` is gone. The process is runAsync()'s kind; a
    // caller that loses interest disconnects from it and kills it
    // (abandonProcess()).
    QProcess *logStream(const QStringList &startPoints, int skip, QObject *context,
                        std::function<void(const QList<Commit> &)> batch, std::function<void(bool ok)> done);
    // Has git write its commit-graph file (`git commit-graph write
    // --reachable`, as `git gc` does) where the repository has none yet,
    // without blocking: a walk that skips commits (`--date-order` and
    // `--skip`) sorts the whole history before it prints anything unless it
    // can read the graph. Nothing waits for it, and a failure goes unsaid (a
    // read-only repository simply stays without one).
    void ensureCommitGraph();
    QHash<QString, QList<RefLabel>> refs() const;
    QList<FileChange> commitChanges(const Commit &commit) const;
    QString commitDiff(const Commit &commit, const FileChange &change, bool *binary = nullptr) const;
    QString parentOf(const Commit &commit) const; // first parent or the empty tree

    // Runs git and returns stdout. exitCode receives the exit status. `env` holds
    // extra "KEY=VALUE" entries for the child process.
    QByteArray run(const QStringList &args, int *exitCode = nullptr, QByteArray *err = nullptr,
                   int timeoutMs = kQueryTimeoutMs, const QStringList &env = QStringList()) const;

signals:
    void rootChanged(const QString &root);

private:
    // How long a git command may take before it is killed. Local queries answer
    // in milliseconds; the longer budgets are there for big repositories and
    // for commands that run hooks, not because the wait is expected.
    static constexpr int kProbeTimeoutMs = 5000;      // "is this a repository at all"
    static constexpr int kQueryTimeoutMs = 15000;     // plain reads of index and refs
    static constexpr int kWorkTimeoutMs = 60000;      // commands that touch every file
    static constexpr int kHistoryTimeoutMs = 120000;  // whole-history walks, merge-tree
    static constexpr int kMergeTimeoutMs = 300000;    // a merge, hooks included

    // What one git process left behind. `code` stays -1 when git never finished.
    struct GitResult {
        int code = -1;
        QByteArray out, err;

        bool ok() const { return code == 0; }
        QString stderrText() const; // trimmed
        QString message() const;    // stderrText(), falling back to trimmed stdout
    };

    static void traceCommand(const QStringList &args);
    // The two -c options every command carries, in front of `args`.
    static QStringList fullArgs(const QStringList &args);
    void prepare(QProcess &p, const QStringList &env, bool terminalPrompt) const;
    // Starts git the way runAsync() and logStream() do: this object's
    // process, killed after `timeoutMs` and deleting itself when done.
    // `listen` connects the caller's callbacks before git starts; `input`,
    // where there is any, is git's stdin, closed after it.
    QProcess *startAsync(const QStringList &args, int timeoutMs, const QStringList &env,
                         const std::function<void(QProcess *)> &listen, const QByteArray &input = QByteArray());
    GitResult exec(const QStringList &args, int timeoutMs = kQueryTimeoutMs,
                   const QStringList &env = QStringList()) const;
    // Hands git's own words to the caller: `error` gets stderr, or `fallback`
    // when git said nothing. Returns whether the command succeeded.
    static bool report(const GitResult &r, QString *error, const QString &fallback = QString());
    // Fails with our own wording, git's message (`detail`) on the next line.
    static bool fail(QString *error, const QString &message, const QByteArray &detail = QByteArray());

    QString emptyTree() const;
    QString symbolicHead(int *code = nullptr) const; // the branch HEAD points at
    QStringList stageablePaths(const QStringList &paths, const QStringList &env) const;
    GitResult stageAll(const QStringList &paths, const QStringList &env, int timeoutMs) const;
    // `git status` as it is parsed everywhere: index/worktree columns and path.
    QList<FileChange> porcelainStatus(bool *ok = nullptr) const;
    void applyNumstat(const QByteArray &numstat, QList<FileChange> &changes) const;
    void applyTreeSizes(const QString &commit, QList<FileChange> &changes) const;

    // The steps of mergePreview(), in the order it runs them.
    bool verifyCommits(const QStringList &refs, QString *error) const;
    bool countMergeCommits(MergePreview &p) const;
    QStringList collectMergeStats(MergePreview &p) const; // returns the paths the merge writes
    void findBlockedPaths(QStringList touched, MergePreview &p) const;
    void mergeTreeVerdict(MergePreview &p) const;
    MergeResult interpretMerge(int code, const QByteArray &out, const QByteArray &err, QString *error) const;

    QString m_root;
    bool m_amend = false;
    mutable QString m_emptyTree;
};
