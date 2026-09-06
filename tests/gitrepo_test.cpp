// Exercises GitRepo against throw-away repositories: status/diff bases, amend.
// Build: cd tests && qmake6 tests.pro && make && ./gitrepo_test
#include "../src/GitRepo.h"
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

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;
    testAmend(tmp.path());
    testAmendRoot(tmp.path());
    testStatusAndHistory(tmp.path());
    testRemote(tmp.path());
    if (failures == 0)
        printf("all checks passed\n");
    return failures == 0 ? 0 : 1;
}
