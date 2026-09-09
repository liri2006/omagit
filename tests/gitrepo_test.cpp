// Exercises GitRepo against throw-away repositories: status/diff bases, amend.
// Build: cd tests && qmake6 tests.pro && make && ./gitrepo_test
#include "../src/CommitMessageAgent.h"
#include "../src/GitRepo.h"
#include "../src/DesktopExec.h"
#include "../src/RemoteSync.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextStream>

#include <cstdio>
#include <cstring>
#include <cstdlib>

static int failures = 0;
#define CHECK(cond)                                                                                   \
    do {                                                                                              \
        if (!(cond)) {                                                                                \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                           \
            ++failures;                                                                               \
        }                                                                                             \
    } while (0)

static QByteArray git(const QString &dir, const QStringList &args)
{
    QProcess p;
    p.setWorkingDirectory(dir);
    p.start(QStringLiteral("git"), args);
    p.waitForFinished(15000);
    if (p.exitCode() != 0)
        fprintf(stderr, "git %s failed: %s\n", qPrintable(args.join(' ')), p.readAllStandardError().constData());
    return p.readAllStandardOutput().trimmed();
}

static void write(const QString &dir, const QString &name, const QString &content)
{
    QFile f(QDir(dir).filePath(name));
    QDir().mkpath(QFileInfo(f).absolutePath());
    f.open(QIODevice::WriteOnly | QIODevice::Truncate);
    f.write(content.toUtf8());
}

static const FileChange *find(const QList<FileChange> &list, const QString &path)
{
    for (const FileChange &c : list)
        if (c.path == path)
            return &c;
    return nullptr;
}

static QString initRepo(const QString &dir)
{
    QDir().mkpath(dir);
    git(dir, {"init", "-q", "-b", "main"});
    git(dir, {"config", "user.name", "Tester"});
    git(dir, {"config", "user.email", "tester@example.com"});
    git(dir, {"config", "commit.gpgsign", "false"});
    return dir;
}

static void testDiscard(const QString &base)
{
    const QString dir = initRepo(base + "/discard");
    const QStringList names{"edited.txt", "deleted.txt", "renamed.txt", "keep.txt", "literal[1].txt", "literal1.txt"};
    for (const QString &name : names)
        write(dir, name, name + " original\n");
    git(dir, {"add", "."});
    git(dir, {"commit", "-q", "-m", "initial"});
    write(dir, "edited.txt", "staged\n");
    write(dir, "keep.txt", "keep staged\n");
    git(dir, {"add", "."});
    write(dir, "edited.txt", "unstaged\n");
    git(dir, {"rm", "-q", "deleted.txt"});
    git(dir, {"mv", "renamed.txt", "new name.txt"});
    write(dir, "added.txt", "new staged\n");
    git(dir, {"add", "added.txt"});
    write(dir, "untracked.txt", "new unstaged\n");
    write(dir, "literal[1].txt", "changed\n");
    write(dir, "literal1.txt", "keep unstaged\n");

    GitRepo repo(dir);
    QString error;
    for (const QString &name : QStringList{"edited.txt", "deleted.txt", "new name.txt", "added.txt", "untracked.txt", "literal[1].txt"}) {
        const auto changes = repo.status();
        const FileChange *change = find(changes, name);
        CHECK(change);
        if (change)
            CHECK(repo.discardChanges(*change, &error));
    }
    for (const QString &name : QStringList{"edited.txt", "deleted.txt", "renamed.txt", "literal[1].txt"}) {
        QFile file(dir + "/" + name);
        CHECK(file.open(QIODevice::ReadOnly));
        CHECK(file.readAll() == (name + " original\n").toUtf8());
    }
    CHECK(!QFileInfo::exists(dir + "/new name.txt"));
    CHECK(!QFileInfo::exists(dir + "/added.txt"));
    CHECK(!QFileInfo::exists(dir + "/untracked.txt"));
    CHECK(repo.status().size() == 2);
    CHECK(git(dir, {"show", ":keep.txt"}) == "keep staged");
    CHECK(git(dir, {"diff", "--", "literal1.txt"}).contains("+keep unstaged"));

    // Amend mode still restores HEAD, not the parent shown in the diff.
    write(dir, "edited.txt", "latest commit\n");
    git(dir, {"add", "edited.txt"});
    git(dir, {"commit", "-q", "-m", "second"});
    write(dir, "edited.txt", "pending\n");
    repo.setAmend(true);
    const auto changes = repo.status();
    const FileChange *edited = find(changes, "edited.txt");
    CHECK(edited);
    if (edited)
        CHECK(repo.discardChanges(*edited, &error));
    QFile restored(dir + "/edited.txt");
    CHECK(restored.open(QIODevice::ReadOnly));
    CHECK(restored.readAll() == "latest commit\n");

    // New repositories and a file staged after the menu was opened.
    const QString unbornDir = initRepo(base + "/discard-unborn");
    GitRepo unborn(unbornDir);
    write(unbornDir, "first.txt", "first\n");
    const auto newChanges = unborn.status();
    CHECK(newChanges.size() == 1);
    git(unbornDir, {"add", "."});
    if (!newChanges.isEmpty())
        CHECK(unborn.discardChanges(newChanges.first(), &error));
    CHECK(unborn.status().isEmpty());
    CHECK(!QFileInfo::exists(unbornDir + "/first.txt"));

    FileChange invalid;
    invalid.path = "../outside.txt";
    invalid.kind = FileChange::Untracked;
    CHECK(!unborn.discardChanges(invalid, &error));
    CHECK(!error.isEmpty());
}

static void testAmend(const QString &base)
{
    const QString dir = initRepo(base + "/amend");
    write(dir, "a.txt", "a1\n");
    write(dir, "b.txt", "b1\n");
    git(dir, {"add", "."});
    git(dir, {"commit", "-q", "-m", "first"});
    const QString first = git(dir, {"rev-parse", "HEAD"});
    write(dir, "a.txt", "a2\n");
    write(dir, "c.txt", "c1\n");
    git(dir, {"rm", "-q", "b.txt"});
    git(dir, {"add", "."});
    git(dir, {"commit", "-q", "-m", "second", "--author=Someone Else <else@example.com>"});
    const QString second = git(dir, {"rev-parse", "HEAD"});
    // Pending work on top of the second commit.
    write(dir, "c.txt", "c2\n");
    write(dir, "d.txt", "d1\n");

    GitRepo repo(dir);
    QList<FileChange> st = repo.status();
    CHECK(st.size() == 2); // c.txt modified, d.txt untracked
    CHECK(find(st, "c.txt") && find(st, "c.txt")->kind == FileChange::Modified);
    CHECK(find(st, "d.txt") && find(st, "d.txt")->kind == FileChange::Untracked);

    repo.setAmend(true);
    CHECK(repo.baseRef() == first);
    st = repo.status();
    CHECK(st.size() == 4);
    CHECK(find(st, "a.txt") && find(st, "a.txt")->kind == FileChange::Modified);
    CHECK(find(st, "b.txt") && find(st, "b.txt")->kind == FileChange::Deleted);
    CHECK(find(st, "b.txt") && find(st, "b.txt")->size == -1);
    CHECK(find(st, "c.txt") && find(st, "c.txt")->kind == FileChange::Added);
    CHECK(find(st, "c.txt") && find(st, "c.txt")->linesAdded == 1);
    CHECK(find(st, "c.txt") && find(st, "c.txt")->size == QFileInfo(dir + "/c.txt").size());
    CHECK(find(st, "d.txt") && find(st, "d.txt")->kind == FileChange::Untracked);
    CHECK(repo.headMessage() == "second");
    CHECK(repo.headPaths().contains("a.txt") && repo.headPaths().contains("b.txt") && repo.headPaths().contains("c.txt"));
    // Diff of a file that only changed in the last commit is shown against the parent.
    CHECK(repo.diff(*find(st, "a.txt")).contains("-a1") && repo.diff(*find(st, "a.txt")).contains("+a2"));

    // Amend with a, c, d but leave the deletion of b out.
    QString error;
    const bool ok = repo.amendCommit("second, amended\n\nwith body", {"a.txt", "c.txt", "d.txt"}, &error);
    CHECK(ok);
    if (!ok)
        fprintf(stderr, "amend error: %s\n", qPrintable(error));
    const QString head = git(dir, {"rev-parse", "HEAD"});
    CHECK(head != second);
    CHECK(git(dir, {"rev-parse", "HEAD^"}) == first);
    CHECK(git(dir, {"log", "-1", "--format=%s"}) == "second, amended");
    CHECK(git(dir, {"log", "-1", "--format=%b"}) == "with body");
    CHECK(git(dir, {"log", "-1", "--format=%an <%ae>"}) == "Someone Else <else@example.com>");
    CHECK(git(dir, {"log", "-1", "--format=%cn"}) == "Tester");
    CHECK(git(dir, {"show", "HEAD:a.txt"}) == "a2");
    CHECK(git(dir, {"show", "HEAD:b.txt"}) == "b1"); // kept, deletion was not checked
    CHECK(git(dir, {"show", "HEAD:c.txt"}) == "c2");
    CHECK(git(dir, {"show", "HEAD:d.txt"}) == "d1");
    CHECK(git(dir, {"rev-list", "--count", "HEAD"}) == "2");
    CHECK(!QFile::exists(dir + "/.git/omagit-amend-index"));

    // The unchecked deletion is back in the working tree / index.
    repo.setAmend(false);
    st = repo.status();
    CHECK(st.size() == 1);
    CHECK(find(st, "b.txt") && find(st, "b.txt")->kind == FileChange::Deleted);
    CHECK(git(dir, {"status", "--porcelain"}) == "D  b.txt");

    // Amending again with no pending changes and the same files is a no-op on content.
    repo.setAmend(true);
    CHECK(repo.amendCommit("third message", {"a.txt", "b.txt", "c.txt", "d.txt"}, &error));
    CHECK(git(dir, {"log", "-1", "--format=%s"}) == "third message");
    CHECK(git(dir, {"ls-tree", "--name-only", "HEAD"}) == "a.txt\nc.txt\nd.txt");
    repo.setAmend(false);
    CHECK(git(dir, {"status", "--porcelain"}).isEmpty());
}

static void testAmendRoot(const QString &base)
{
    const QString dir = initRepo(base + "/root");
    write(dir, "x.txt", "x\n");
    write(dir, "y.txt", "y\n");
    git(dir, {"add", "."});
    git(dir, {"commit", "-q", "-m", "root"});
    GitRepo repo(dir);
    repo.setAmend(true);
    CHECK(repo.baseRef().size() == 40 || repo.baseRef().size() == 64); // empty tree
    QList<FileChange> st = repo.status();
    CHECK(st.size() == 2);
    CHECK(find(st, "x.txt") && find(st, "x.txt")->kind == FileChange::Added);
    QString error;
    CHECK(repo.amendCommit("root, only x", {"x.txt"}, &error));
    CHECK(git(dir, {"rev-list", "--count", "HEAD"}) == "1");
    CHECK(git(dir, {"ls-tree", "--name-only", "HEAD"}) == "x.txt");
    CHECK(git(dir, {"status", "--porcelain"}) == "A  y.txt");
    CHECK(repo.headCommit().parents.isEmpty());
}

static void testStatusAndHistory(const QString &base)
{
    const QString dir = initRepo(base + "/history");
    write(dir, "src/main.c", "#include <stdio.h>\n\nint main()\n{\n    puts(\"hi\");\n    return 0;\n}\n");
    write(dir, "README", "hello\n");
    git(dir, {"add", "."});
    git(dir, {"commit", "-q", "-m", "initial"});
    git(dir, {"tag", "v0.1"});
    git(dir, {"checkout", "-q", "-b", "feature"});
    write(dir, "src/feature.c", "void f() {}\n");
    git(dir, {"add", "."});
    git(dir, {"commit", "-q", "-m", "add feature"});
    git(dir, {"checkout", "-q", "main"});
    write(dir, "README", "hello world\n");
    git(dir, {"commit", "-q", "-am", "update readme"});
    git(dir, {"merge", "-q", "--no-ff", "-m", "merge feature", "feature"});
    git(dir, {"mv", "README", "README.md"});
    git(dir, {"commit", "-q", "-m", "rename readme"});

    GitRepo repo(dir);
    bool ok = false;
    const QList<Commit> log = repo.log(0, 100, false, &ok);
    CHECK(ok);
    CHECK(log.size() == 5);
    CHECK(log.first().subject == "rename readme");
    CHECK(log[1].parents.size() == 2);
    CHECK(log.last().parents.isEmpty());
    CHECK(repo.log(3, 100, false).size() == 2);
    const auto refs = repo.refs();
    CHECK(refs.value(log.first().hash).size() == 1 && refs.value(log.first().hash).first().name == "main");
    CHECK(refs.value(log.first().hash).first().head);
    CHECK(refs.value(log.last().hash).size() == 1 && refs.value(log.last().hash).first().type == RefLabel::Tag);

    const QList<FileChange> renamed = repo.commitChanges(log.first());
    CHECK(renamed.size() == 1 && renamed.first().kind == FileChange::Renamed && renamed.first().oldPath == "README");
    CHECK(renamed.first().size == qint64(strlen("hello world\n")));
    const QList<FileChange> initial = repo.commitChanges(log.last());
    CHECK(initial.size() == 2 && initial.first().kind == FileChange::Added && initial.first().linesAdded == 1);
    CHECK(repo.commitDiff(log.last(), initial.first()).contains("+hello"));
    const QList<FileChange> merge = repo.commitChanges(log[1]);
    CHECK(merge.size() == 1 && merge.first().path == "src/feature.c"); // vs first parent

    // Working tree: staged rename + unstaged edit + untracked, HEAD base.
    git(dir, {"mv", "src/main.c", "src/app.c"});
    write(dir, "src/app.c", "#include <stdio.h>\n\nint main()\n{\n    puts(\"hi\");\n    return 1;\n}\n");
    write(dir, "notes.txt", "n\n");
    const QList<FileChange> st = repo.status();
    CHECK(st.size() == 2);
    CHECK(find(st, "src/app.c") && find(st, "src/app.c")->kind == FileChange::Renamed);
    CHECK(find(st, "src/app.c") && find(st, "src/app.c")->oldPath == "src/main.c");
    CHECK(find(st, "src/app.c") && find(st, "src/app.c")->linesAdded == 1);
    CHECK(find(st, "notes.txt") && find(st, "notes.txt")->isUntracked());
}

// Runs one RemoteSync operation to completion and returns whether it succeeded.
static bool runOp(RemoteSync &sync, void (RemoteSync::*op)(), QString *message = nullptr)
{
    QEventLoop loop;
    bool ok = false;
    QObject::connect(&sync, &RemoteSync::finished, &loop,
                     [&](RemoteSync::Op, bool success, bool, const QString &msg) {
                         ok = success;
                         if (message)
                             *message = msg;
                         loop.quit();
                     });
    (sync.*op)();
    if (!sync.busy())
        return false; // refused (canPull/canPush false)
    loop.exec();
    return ok;
}

static void testRemote(const QString &base)
{
    // A bare "server", a clone that is ours, and a second clone that plays the colleague.
    const QString server = base + "/server.git";
    QDir().mkpath(server);
    git(server, {"init", "-q", "--bare", "-b", "main"});
    const QString seed = initRepo(base + "/seed");
    write(seed, "a.txt", "a\n");
    git(seed, {"add", "."});
    git(seed, {"commit", "-q", "-m", "one"});
    git(seed, {"push", "-q", server, "main"});

    const QString mine = base + "/mine", theirs = base + "/theirs";
    git(base, {"clone", "-q", server, mine});
    git(base, {"clone", "-q", server, theirs});
    for (const QString &d : {mine, theirs}) {
        git(d, {"config", "user.name", "Tester"});
        git(d, {"config", "user.email", "tester@example.com"});
        git(d, {"config", "commit.gpgsign", "false"});
        git(d, {"config", "pull.rebase", "false"});
    }

    GitRepo repo(mine);
    UpstreamState s = repo.upstreamState();
    CHECK(s.branch == "main");
    CHECK(s.upstream == "origin/main" && s.remote == "origin" && s.hasUpstream());
    CHECK(s.ahead == 0 && s.behind == 0);
    CHECK(s.remotes == QStringList{"origin"});

    // Two local commits: ahead 2.
    write(mine, "b.txt", "b\n");
    git(mine, {"add", "."});
    git(mine, {"commit", "-q", "-m", "two"});
    write(mine, "c.txt", "c\n");
    git(mine, {"add", "."});
    git(mine, {"commit", "-q", "-m", "three"});
    s = repo.upstreamState();
    CHECK(s.ahead == 2 && s.behind == 0);

    // The colleague pushes one: behind 1 after a fetch, not before.
    write(theirs, "d.txt", "d\n");
    git(theirs, {"add", "."});
    git(theirs, {"commit", "-q", "-m", "theirs"});
    git(theirs, {"push", "-q"});
    RemoteSync sync(&repo);
    CHECK(sync.state().behind == 0);
    CHECK(sync.canFetch() && sync.canPull() && sync.canPush() && !sync.pushPublishes());
    QString message;
    CHECK(runOp(sync, &RemoteSync::fetch, &message));
    CHECK(sync.state().behind == 1 && sync.state().ahead == 2);
    CHECK(message.contains("1 commit"));
    CHECK(sync.lastFetchOk() && sync.lastFetch().isValid());

    CHECK(runOp(sync, &RemoteSync::pull, &message));
    CHECK(sync.state().behind == 0 && sync.state().ahead == 3); // merge commit
    CHECK(QFile::exists(mine + "/d.txt"));
    CHECK(runOp(sync, &RemoteSync::push, &message));
    CHECK(sync.state().behind == 0 && sync.state().ahead == 0);
    CHECK(message.contains("3 commit"));
    CHECK(git(server, {"rev-parse", "main"}) == git(mine, {"rev-parse", "HEAD"}));

    // A new branch without upstream: push publishes it.
    git(mine, {"checkout", "-q", "-b", "topic"});
    s = repo.upstreamState();
    CHECK(s.branch == "topic" && s.upstream.isEmpty() && s.remote == "origin" && !s.hasUpstream());
    sync.refreshState();
    CHECK(!sync.canPull() && sync.canPush() && sync.pushPublishes());
    CHECK(sync.pushArgs() == QStringList({"push", "-u", "origin", "topic"}));
    CHECK(runOp(sync, &RemoteSync::push, &message));
    CHECK(sync.state().upstream == "origin/topic" && sync.state().ahead == 0);

    // Upstream deleted on the server: reported as gone.
    git(server, {"branch", "-D", "topic"});
    CHECK(runOp(sync, &RemoteSync::fetch, &message));
    git(mine, {"fetch", "-q", "--prune"});
    sync.refreshState();
    CHECK(sync.state().upstreamGone && !sync.canPull() && sync.pushPublishes());

    // Detached HEAD: neither pull nor push.
    git(mine, {"checkout", "-q", "--detach", "main"});
    sync.refreshState();
    CHECK(sync.state().detached && !sync.canPull() && !sync.canPush() && sync.canFetch());

    // A failing operation reports an error and leaves the state readable.
    git(mine, {"checkout", "-q", "main"});
    git(mine, {"remote", "set-url", "origin", base + "/does-not-exist.git"});
    sync.refreshState();
    CHECK(!runOp(sync, &RemoteSync::fetch, &message));
    CHECK(!sync.lastFetchOk() && !message.isEmpty());
    CHECK(!sync.busy());
}

static void testBranches(const QString &base)
{
    const QString server = base + "/branches-server.git";
    QDir().mkpath(server);
    git(server, {"init", "-q", "--bare", "-b", "main"});
    const QString seed = initRepo(base + "/branches-seed");
    write(seed, "a.txt", "a\n");
    git(seed, {"add", "."});
    git(seed, {"commit", "-q", "-m", "one"});
    git(seed, {"branch", "feature/x"});
    git(seed, {"push", "-q", server, "main", "feature/x"});
    const QString mine = base + "/branches-mine";
    git(base, {"clone", "-q", server, mine});
    git(mine, {"config", "user.name", "Tester"});
    git(mine, {"config", "user.email", "tester@example.com"});
    git(mine, {"branch", "local-only"});

    GitRepo repo(mine);
    BranchList b = repo.branches();
    CHECK(b.current == "main");
    CHECK(b.local == QStringList({"local-only", "main"}));
    CHECK(b.remote == QStringList({"origin/feature/x", "origin/main"})); // no origin/HEAD

    QString error;
    CHECK(repo.checkout("local-only", &error));
    CHECK(repo.branches().current == "local-only");
    // A remote branch without a local twin: created tracking it.
    CHECK(repo.checkout("origin/feature/x", &error));
    b = repo.branches();
    CHECK(b.current == "feature/x");
    CHECK(b.local.contains("feature/x"));
    CHECK(repo.upstreamState().upstream == "origin/feature/x");
    // A remote branch with a local twin: the twin is checked out.
    CHECK(repo.checkout("origin/main", &error));
    CHECK(repo.branches().current == "main");
    CHECK(repo.branch() == "main");
    // Local changes that would be lost make git refuse, with its message.
    write(mine, "a.txt", "changed on main\n");
    git(mine, {"checkout", "-q", "feature/x"});
    write(mine, "a.txt", "changed\n");
    git(mine, {"commit", "-q", "-am", "diverge"});
    git(mine, {"checkout", "-q", "main"});
    write(mine, "a.txt", "dirty\n");
    CHECK(!repo.checkout("feature/x", &error));
    CHECK(error.contains("a.txt"));
    CHECK(repo.branches().current == "main");
    CHECK(!repo.checkout("no-such-branch", &error));
    CHECK(!error.isEmpty());

    // Pointing the object at another repository.
    const QString other = initRepo(base + "/branches-other");
    write(other, "b.txt", "b\n");
    git(other, {"add", "."});
    git(other, {"commit", "-q", "-m", "other"});
    git(other, {"checkout", "-q", "-b", "dev"});
    int changes = 0;
    QObject::connect(&repo, &GitRepo::rootChanged, [&changes](const QString &) { ++changes; });
    repo.setAmend(true);
    repo.setRoot(other);
    CHECK(changes == 1 && repo.root() == other && !repo.amending());
    CHECK(repo.branch() == "dev");
    CHECK(repo.branches().remote.isEmpty());
    repo.setRoot(other);
    CHECK(changes == 1);
}

// The diff handed to a coding agent: stat first, tracked and untracked files, a cut-off.
static void testPatch(const QString &base)
{
    const QString dir = base + "/patch";
    initRepo(dir);
    write(dir, "a.txt", "one\ntwo\n");
    git(dir, {"add", "."});
    git(dir, {"commit", "-q", "-m", "initial"});
    write(dir, "a.txt", "one\n2\n");
    write(dir, "new.txt", "hello\n");
    GitRepo repo(dir);
    const QList<FileChange> changes = repo.status();
    CHECK(changes.size() == 2);
    const QString patch = repo.patch(changes);
    CHECK(patch.contains("a.txt | 2 +-"));
    CHECK(patch.contains("new.txt (new file)"));
    CHECK(patch.contains("-two\n+2\n"));
    CHECK(patch.contains("+hello\n"));
    CHECK(patch.indexOf("new.txt (new file)") < patch.indexOf("diff --git"));
    const QString cut = repo.patch(changes, 120);
    CHECK(cut.contains("diff truncated"));
    CHECK(cut.size() < patch.size());
    // Only what was asked for.
    const FileChange *a = find(changes, "a.txt");
    CHECK(a && !repo.patch({*a}).contains("hello"));
}

static void testAgentCommands()
{
    CHECK(CommitMessageAgent::agents().size() == 2);
    CHECK(CommitMessageAgent::spec("claude").binary == "claude");
    CHECK(!CommitMessageAgent::spec("gemini").isValid());

    const QString diff = "diff --git a/x b/x\n";
    auto cmd = CommitMessageAgent::command({"claude", "opus", "high"}, diff, "");
    CHECK(cmd.program == "claude");
    CHECK(cmd.args.contains("-p") && cmd.args.contains("--no-session-persistence") && !cmd.args.contains("--bare"));
    CHECK(cmd.args.indexOf("--model") >= 0 && cmd.args.at(cmd.args.indexOf("--model") + 1) == "opus");
    CHECK(cmd.args.at(cmd.args.indexOf("--effort") + 1) == "high");
    // "--tools" takes a list, so the prompt must come after "--".
    CHECK(cmd.args.indexOf("--tools") + 1 == cmd.args.indexOf("") && cmd.args.indexOf("") + 1 == cmd.args.indexOf("--"));
    CHECK(cmd.args.last() == CommitMessageAgent::instructions());
    CHECK(cmd.stdinText == diff);
    CHECK(cmd.outputFile.isEmpty());

    cmd = CommitMessageAgent::command({"claude", "", ""}, diff, "");
    CHECK(!cmd.args.contains("--model") && !cmd.args.contains("--effort"));

    cmd = CommitMessageAgent::command({"codex", "gpt-6-astra", "xhigh"}, diff, "/tmp/out.txt");
    CHECK(cmd.program == "codex" && cmd.args.first() == "exec");
    CHECK(cmd.args.contains("--ephemeral") && cmd.args.contains("read-only"));
    CHECK(cmd.args.at(cmd.args.indexOf("-m") + 1) == "gpt-6-astra");
    CHECK(cmd.args.at(cmd.args.indexOf("-c") + 1) == "model_reasoning_effort=\"xhigh\"");
    CHECK(cmd.args.at(cmd.args.indexOf("-o") + 1) == "/tmp/out.txt" && cmd.outputFile == "/tmp/out.txt");
    CHECK(cmd.stdinText == diff);

    CHECK(CommitMessageAgent::command({"", "", ""}, diff, "").program.isEmpty());

    // The answer, without what CLIs wrap around it.
    CHECK(CommitMessageAgent::cleanMessage("Add a thing\n") == "Add a thing");
    CHECK(CommitMessageAgent::cleanMessage("```\nAdd a thing\n\nBecause.\n```\n") == "Add a thing\n\nBecause.");
    CHECK(CommitMessageAgent::cleanMessage("\"Add a thing\"") == "Add a thing");
    CHECK(CommitMessageAgent::cleanMessage("Commit message: Add a thing") == "Add a thing");
    CHECK(CommitMessageAgent::cleanMessage("\x1b[32mAdd\x1b[0m a thing  \r\n") == "Add a thing");
    CHECK(CommitMessageAgent::cleanMessage("Add\n\n\n\nBody") == "Add\n\nBody");
    CHECK(CommitMessageAgent::cleanMessage("Say \"hi\" to \"them\"") == "Say \"hi\" to \"them\"");
    // A subject wrapped by the agent becomes one line; a list stays a list.
    CHECK(CommitMessageAgent::cleanMessage("Add a thing to\nthe box\n\nBecause.") == "Add a thing to the box\n\nBecause.");
    CHECK(CommitMessageAgent::cleanMessage("Add a thing\n- one\n- two") == "Add a thing\n- one\n- two");
    CHECK(CommitMessageAgent::cleanMessage("Add two things\n\n- one\n- two") == "Add two things\n\n- one\n- two");
}

// Models and levels as the CLIs print them.
static void testAgentCatalogs()
{
    const QString help =
        "Options:\n"
        "  --effort <level>                      Effort level for the current session\n"
        "                                        (low, medium, high, xhigh, max)\n"
        "  --environment <environment_id>        Create a new cloud session\n"
        "  --model <model>                       Model for the current session. Provide\n"
        "                                        an alias for the latest model (e.g.\n"
        "                                        'fable', 'opus', or 'sonnet') or a\n"
        "                                        model's full name (e.g.\n"
        "                                        'claude-fable-5').\n"
        "  --no-chrome                           Disable Claude in Chrome integration\n";
    AgentCatalog c = CommitMessageAgent::parseClaudeHelp(help);
    CHECK(c.efforts == QStringList({"low", "medium", "high", "xhigh", "max"}));
    CHECK(c.models.size() == 3);
    CHECK(c.models.size() == 3 && c.models.at(1).id == "opus" && c.models.at(1).name == "Opus");
    CHECK(c.effortsFor("sonnet") == c.efforts && c.effortsFor("") == c.efforts);
    CHECK(c.error.isEmpty());
    CHECK(CommitMessageAgent::parseClaudeHelp("nothing here").isEmpty());

    const QByteArray json =
        "{\"models\":[{\"slug\":\"gpt-5.5\",\"display_name\":\"GPT-5.5\",\"visibility\":\"list\",\"priority\":12,"
        "\"default_reasoning_level\":\"medium\",\"supported_reasoning_levels\":[{\"effort\":\"low\"},{\"effort\":\"high\"}]},"
        "{\"slug\":\"gpt-reserve\",\"display_name\":\"GPT-Reserve\",\"visibility\":\"hide\",\"priority\":3},"
        "{\"slug\":\"gpt-6-astra\",\"display_name\":\"GPT-6-Astra\",\"visibility\":\"list\",\"priority\":1,"
        "\"default_reasoning_level\":\"medium\",\"supported_reasoning_levels\":[{\"effort\":\"low\"},{\"effort\":\"medium\"},"
        "{\"effort\":\"high\"},{\"effort\":\"xhigh\"},{\"effort\":\"max\"},{\"effort\":\"ultra\"}]}]}";
    c = CommitMessageAgent::parseCodexModels(json);
    CHECK(c.models.size() == 2); // the hidden one is left out
    CHECK(c.models.size() == 2 && c.models.first().id == "gpt-6-astra" && c.models.last().id == "gpt-5.5"); // by priority
    CHECK(c.models.size() == 2 && c.models.first().name == "GPT-6-Astra" && c.models.first().defaultEffort == "medium");
    CHECK(c.efforts.size() == 6 && c.efforts.last() == "ultra"); // the first model's, for "Default"
    CHECK(c.effortsFor("gpt-5.5") == QStringList({"low", "high"}));
    CHECK(c.effortsFor("unknown-model") == c.efforts);
    CHECK(c.error.isEmpty());
    c = CommitMessageAgent::parseCodexModels("not json");
    CHECK(c.isEmpty() && !c.error.isEmpty());
    CHECK(!CommitMessageAgent::spec("").isValid() && CommitMessageAgent::catalog("nope").isEmpty());
}

// Merging: the preview on trees alone, then the real thing, conflicts and all.
static void testMerge(const QString &base)
{
    const QString dir = initRepo(base + "/merge");
    write(dir, "a.txt", "one\ntwo\nthree\n");
    write(dir, "b.txt", "b\n");
    git(dir, {"add", "."});
    git(dir, {"commit", "-q", "-m", "base"});
    // feature: edits a.txt (line two) and adds c.txt; main: edits b.txt.
    git(dir, {"checkout", "-q", "-b", "feature"});
    write(dir, "a.txt", "one\nTWO\nthree\n");
    write(dir, "c.txt", "c\n");
    git(dir, {"add", "."});
    git(dir, {"commit", "-q", "-m", "feature work"});
    git(dir, {"checkout", "-q", "main"});
    GitRepo repo(dir);

    // main has nothing of its own yet: feature fast-forwards.
    MergePreview p = repo.mergePreview("feature", "main");
    CHECK(p.outcome == MergePreview::FastForward);
    CHECK(p.commits == 1 && p.diverged == 0);
    CHECK(p.files == 2 && p.added == 2 && p.removed == 1);
    CHECK(p.conflicts.isEmpty() && p.blocked.isEmpty() && p.canMerge());
    // ... and the other way round there is nothing to merge.
    CHECK(repo.mergePreview("main", "feature").outcome == MergePreview::UpToDate);
    CHECK(repo.mergePreview("main", "main").outcome == MergePreview::Same);
    p = repo.mergePreview("no-such", "main");
    CHECK(p.outcome == MergePreview::Failed && p.error.contains("no-such"));
    CHECK(!repo.mergeInProgress());

    // Local changes to a file the merge writes stand in the way.
    write(dir, "a.txt", "dirty\n");
    p = repo.mergePreview("feature", "main");
    CHECK(p.outcome == MergePreview::FastForward);
    CHECK(p.blocked == QStringList({"a.txt"}) && !p.canMerge());
    git(dir, {"checkout", "-q", "--", "a.txt"});
    // Untracked files the merge would create as well.
    write(dir, "c.txt", "mine\n");
    CHECK(repo.mergePreview("feature", "main").blocked == QStringList({"c.txt"}));
    QFile::remove(QDir(dir).filePath("c.txt"));

    // main moves on in b.txt: a clean merge commit.
    write(dir, "b.txt", "B\n");
    git(dir, {"commit", "-q", "-am", "main work"});
    p = repo.mergePreview("feature", "main");
    CHECK(p.outcome == MergePreview::Clean);
    CHECK(p.commits == 1 && p.diverged == 1 && p.files == 2);
    // The working tree is untouched by the preview.
    CHECK(repo.status().isEmpty());
    // Switching to the destination first: dirty files that differ between
    // HEAD and the destination block that switch.
    git(dir, {"checkout", "-q", "feature"});
    write(dir, "b.txt", "dirty\n");
    CHECK(repo.mergePreview("feature", "main").blocked == QStringList({"b.txt"}));
    git(dir, {"checkout", "-q", "--", "b.txt"});
    git(dir, {"checkout", "-q", "main"});

    // main edits line two as well: a conflict in a.txt, c.txt still fine.
    write(dir, "a.txt", "one\n2\nthree\n");
    git(dir, {"commit", "-q", "-am", "main conflict"});
    p = repo.mergePreview("feature", "main");
    CHECK(p.outcome == MergePreview::Conflicts);
    CHECK(p.conflicts == QStringList({"a.txt"}));
    CHECK(p.canMerge()); // starting it is allowed; the conflicts are left to resolve

    // Defaults for the pickers.
    CHECK(repo.defaultBranch() == "main");
    const QStringList active = repo.branchesByActivity(); // same-second commits: order is git's call
    CHECK(active.size() == 2 && active.contains("main") && active.contains("feature"));

    // The real merge: conflicts leave the merge in progress ...
    QString error;
    CHECK(repo.merge("feature", false, &error) == GitRepo::MergeConflicts);
    CHECK(error.contains("a.txt"));
    MergeState state = repo.mergeState();
    CHECK(state.inProgress && state.source == "feature");
    CHECK(state.conflicts == QStringList({"a.txt"}));
    CHECK(state.message.startsWith("Merge branch 'feature'"));
    const FileChange *conflicted = find(repo.status(), "a.txt");
    CHECK(conflicted && conflicted->kind == FileChange::Unmerged);
    // ... which a commit of the resolved file finishes (no pathspec during a merge).
    write(dir, "a.txt", "one\nresolved\nthree\n");
    CHECK(repo.commit("Merge feature", {"a.txt"}, &error));
    CHECK(!repo.mergeInProgress());
    CHECK(repo.headCommit().parents.size() == 2);
    CHECK(git(dir, {"show", "HEAD:c.txt"}) == "c");
    CHECK(repo.mergePreview("feature", "main").outcome == MergePreview::UpToDate);

    // Abort puts everything back.
    git(dir, {"checkout", "-q", "-b", "again", "HEAD~1"});
    write(dir, "a.txt", "one\nagain\nthree\n");
    git(dir, {"commit", "-q", "-am", "again"});
    const QString before = git(dir, {"rev-parse", "HEAD"});
    CHECK(repo.merge("main", false, &error) == GitRepo::MergeConflicts);
    CHECK(repo.mergeInProgress());
    CHECK(repo.abortMerge(&error));
    CHECK(!repo.mergeInProgress() && git(dir, {"rev-parse", "HEAD"}) == before);
    CHECK(repo.status().isEmpty());

    // A fast-forward, and a forced merge commit.
    git(dir, {"checkout", "-q", "-b", "ff", "main~1"});
    CHECK(repo.mergePreview("main", "ff").outcome == MergePreview::FastForward);
    CHECK(repo.merge("main", false, &error) == GitRepo::Merged);
    CHECK(git(dir, {"rev-parse", "HEAD"}) == git(dir, {"rev-parse", "main"}));
    git(dir, {"checkout", "-q", "-b", "noff", "main~1"});
    CHECK(repo.merge("main", true, &error) == GitRepo::Merged);
    CHECK(repo.headCommit().parents.size() == 2);
    CHECK(GitRepo::mergeArgs("x", true) == QStringList({"merge", "--no-edit", "--no-ff", "x"}));
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;
    const QString openPath = QStringLiteral("/tmp/a b;$(touch nope)%f.txt");
    CHECK(desktopExecArguments("nvim %F", openPath, "Neovim", "nvim", "nvim.desktop")
          == QStringList({"nvim", openPath}));
    CHECK(desktopExecArguments("\"/opt/My Editor/bin/editor\" --title %c %i %k %f %% %d", openPath,
                               "My Editor", "editor", "/tmp/editor.desktop")
          == QStringList({"/opt/My Editor/bin/editor", "--title", "My Editor", "--icon", "editor",
                          "/tmp/editor.desktop", openPath, "%"}));
    CHECK(desktopExecArguments("editor %U", "/tmp/a b.txt", {}, {}, {})
          == QStringList({"editor", "file:///tmp/a%20b.txt"}));
    testDiscard(tmp.path());
    testAmend(tmp.path());
    testAmendRoot(tmp.path());
    testStatusAndHistory(tmp.path());
    testRemote(tmp.path());
    testBranches(tmp.path());
    testMerge(tmp.path());
    testPatch(tmp.path());
    testAgentCommands();
    testAgentCatalogs();
    if (failures == 0)
        printf("all checks passed\n");
    return failures == 0 ? 0 : 1;
}
