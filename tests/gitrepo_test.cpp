// Exercises GitRepo against throw-away repositories: status/diff bases, amend.
// Build: cd tests && qmake6 tests.pro && make && ./gitrepo_test
#include "../src/AskPass.h"
#include "../src/CommitMessageAgent.h"
#include "../src/GitRepo.h"
#include "../src/DesktopExec.h"
#include "../src/RemoteSync.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDataStream>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLocalServer>
#include <QLocalSocket>
#include <QThread>
#include <QPointer>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>

#include <cstdio>
#include <cstring>
#include <cstdlib>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

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
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        fprintf(stderr, "cannot write %s\n", qPrintable(f.fileName()));
        ++failures;
        return;
    }
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
    const QStringList head = repo.logStartPoints(false, &ok);
    CHECK(ok && head.size() == 1);
    const QList<Commit> log = repo.log(head, 0, 100, &ok);
    CHECK(ok);
    CHECK(log.size() == 5);
    CHECK(log.first().subject == "rename readme");
    CHECK(log[1].parents.size() == 2);
    CHECK(log.last().parents.isEmpty());
    CHECK(repo.log(head, 3, 100).size() == 2);
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

// The records of `git log -z` fed in pieces cut anywhere — inside a
// multi-byte character, right at a NUL, a byte at a time — come out as the
// whole stream does; a last record git did not end with a NUL waits for
// finish().
static void testLogStreamParser()
{
    const auto record = [](const QString &hash, const QString &parents, const QString &author, const QString &subject,
                           const QString &body) {
        return QStringList({hash, hash.left(7), parents, author, "a@example.com", "2024-01-01T01:00:00+00:00", subject, body})
            .join(QChar(0x1f))
            .toUtf8();
    };
    const QString one(40, '1'), two(40, '2'), three(40, '3');
    const QByteArray stream = record(one, QString(), "Ádám Őrs", "első", "line one\n\nline two\n") + '\0'
        + record(two, one, "Zoë", "second", QString()) + '\0' + record(three, one + ' ' + two, "Łukasz", "third", "last");

    LogStreamParser whole;
    QList<Commit> expected = whole.feed(stream);
    CHECK(expected.size() == 2); // the third is still open
    expected += whole.finish();
    CHECK(expected.size() == 3);
    CHECK(whole.finish().isEmpty());
    if (expected.size() != 3)
        return;
    CHECK(expected[0].hash == one && expected[0].shortHash == "1111111" && expected[0].parents.isEmpty());
    CHECK(expected[0].author == QString("Ádám Őrs") && expected[0].subject == QString("első"));
    CHECK(expected[0].body == "line one\n\nline two");
    CHECK(expected[0].date == QDateTime::fromString("2024-01-01T01:00:00+00:00", Qt::ISODate));
    CHECK(expected[1].author == QString("Zoë") && expected[1].parents == QStringList({one}) && expected[1].body.isEmpty());
    CHECK(expected[2].parents == QStringList({one, two}) && expected[2].body == "last");

    const auto same = [](const QList<Commit> &a, const QList<Commit> &b) {
        if (a.size() != b.size())
            return false;
        for (int i = 0; i < a.size(); ++i)
            if (a[i].hash != b[i].hash || a[i].shortHash != b[i].shortHash || a[i].parents != b[i].parents
                || a[i].author != b[i].author || a[i].email != b[i].email || a[i].date != b[i].date
                || a[i].subject != b[i].subject || a[i].body != b[i].body)
                return false;
        return true;
    };
    // Two pieces, cut at every byte.
    for (int cut = 0; cut <= stream.size(); ++cut) {
        LogStreamParser parser;
        QList<Commit> commits = parser.feed(stream.left(cut));
        commits += parser.feed(stream.mid(cut));
        commits += parser.finish();
        CHECK(same(commits, expected));
    }
    // A byte at a time.
    {
        LogStreamParser parser;
        QList<Commit> commits;
        for (char byte : stream)
            commits += parser.feed(QByteArray(1, byte));
        commits += parser.finish();
        CHECK(same(commits, expected));
    }
    // Inside the Á of the first author: nothing yet, then the name whole.
    {
        const int inside = stream.indexOf("\xc3\x81") + 1;
        LogStreamParser parser;
        CHECK(parser.feed(stream.left(inside)).isEmpty());
        const QList<Commit> rest = parser.feed(stream.mid(inside));
        CHECK(rest.size() == 2 && rest.first().author == QString("Ádám Őrs"));
    }
    // Right before and right after the first NUL.
    {
        const int nul = stream.indexOf('\0');
        LogStreamParser before;
        CHECK(before.feed(stream.left(nul)).isEmpty());
        CHECK(before.feed(stream.mid(nul, 1)).size() == 1);
        LogStreamParser after;
        CHECK(after.feed(stream.left(nul + 1)).size() == 1);
        CHECK(after.feed(stream.mid(nul + 1)).size() == 1);
        CHECK(after.finish().size() == 1);
    }
}

// Runs GitRepo::logStream() to its end from the scope's start points:
// whether git was ok, the commits of every batch in order, and how many
// batches there were.
static bool streamLog(GitRepo &repo, bool allRefs, QList<Commit> *commits, int *batches, int skip = 0)
{
    QEventLoop loop;
    QObject context;
    bool result = false, done = false;
    bool resolved = false;
    const QStringList startPoints = repo.logStartPoints(allRefs, &resolved);
    CHECK(resolved && !startPoints.isEmpty());
    QPointer<QProcess> process = repo.logStream(
        startPoints, skip, &context,
        [&](const QList<Commit> &batch) {
            CHECK(!done && !batch.isEmpty());
            *commits += batch;
            ++*batches;
        },
        [&](bool ok) {
            result = ok;
            done = true;
            loop.quit();
        });
    CHECK(process && process->parent() == &repo);
    QTimer::singleShot(15000, &loop, &QEventLoop::quit);
    if (!done)
        loop.exec();
    CHECK(done);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    CHECK(!process); // deleted itself once git was done
    return result;
}

// The streamed log is log()'s, whole and in its order, over the same scope,
// and from as far into it as it is asked to start. A repository without
// commits has no start point, and no start point is no history — not
// HEAD's, which is what git log would walk given none.
static void testLogStream(const QString &base)
{
    const QString empty = initRepo(base + "/log-stream-empty");
    {
        GitRepo repo(empty);
        for (const bool allRefs : {false, true}) {
            bool ok = false;
            CHECK(repo.logStartPoints(allRefs, &ok).isEmpty() && ok);
        }
        // git log of HEAD would fail here: no git ran.
        bool ok = false;
        CHECK(repo.log({}, 0, 100, &ok).isEmpty() && ok);
        QEventLoop loop;
        QObject context;
        int batches = 0;
        bool result = false, done = false;
        CHECK(!repo.logStream({}, 0, &context, [&](const QList<Commit> &) { ++batches; },
                              [&](bool ok) {
                                  result = ok;
                                  done = true;
                                  loop.quit();
                              }));
        CHECK(!done); // from the event loop
        QTimer::singleShot(15000, &loop, &QEventLoop::quit);
        if (!done)
            loop.exec();
        CHECK(done && result && batches == 0);
        CHECK(repo.findChildren<QProcess *>().isEmpty());
    }

    const QString dir = initRepo(base + "/log-stream");
    for (int i = 0; i < 40; ++i)
        git(dir, {"commit", "-q", "--allow-empty", "-m", QString("commit %1").arg(i), "-m", "Ünïcode body\n\nsecond paragraph"});
    git(dir, {"checkout", "-q", "-b", "side", "HEAD~5"});
    git(dir, {"commit", "-q", "--allow-empty", "-m", "on the side"});
    git(dir, {"checkout", "-q", "main"});

    GitRepo repo(dir);
    for (const bool allRefs : {false, true}) {
        QList<Commit> commits;
        int batches = 0;
        CHECK(streamLog(repo, allRefs, &commits, &batches));
        CHECK(batches >= 1);
        const QStringList startPoints = repo.logStartPoints(allRefs);
        const QList<Commit> paged = repo.log(startPoints, 0, 1000);
        CHECK(commits.size() == (allRefs ? 41 : 40) && commits.size() == paged.size());
        for (int i = 0; i < qMin(commits.size(), paged.size()); ++i)
            CHECK(commits[i].hash == paged[i].hash && commits[i].body == paged[i].body);
        CHECK(!commits.isEmpty() && commits.last().body == QString("Ünïcode body\n\nsecond paragraph"));

        QList<Commit> rest;
        CHECK(streamLog(repo, allRefs, &rest, &batches, 12));
        const QList<Commit> tail = repo.log(startPoints, 12, 1000);
        CHECK(rest.size() == commits.size() - 12 && rest.size() == tail.size());
        for (int i = 0; i < qMin(rest.size(), tail.size()) && 12 + i < commits.size(); ++i)
            CHECK(rest[i].hash == tail[i].hash && rest[i].hash == commits[12 + i].hash);
    }
}

// The start points of the history: HEAD's commit; with every ref, each
// branch's and tag's commit too — an annotated tag's, a tag of a tag's —
// once, in a stable order, and none for a tag of a tree. A walk from them is
// log()'s of the same scope.
static void testLogStartPoints(const QString &base)
{
    const QString dir = initRepo(base + "/log-start-points");
    for (int i = 0; i < 4; ++i)
        git(dir, {"commit", "-q", "--allow-empty", "-m", QString("commit %1").arg(i)});
    const QString head = QString::fromUtf8(git(dir, {"rev-parse", "HEAD"}));
    const QString second = QString::fromUtf8(git(dir, {"rev-parse", "HEAD~2"}));
    git(dir, {"checkout", "-q", "-b", "side", "HEAD~3"});
    git(dir, {"commit", "-q", "--allow-empty", "-m", "on the side"});
    const QString side = QString::fromUtf8(git(dir, {"rev-parse", "HEAD"}));
    git(dir, {"checkout", "-q", "main"});
    git(dir, {"tag", "-a", "-m", "annotated", "v1", second});
    git(dir, {"-c", "advice.nestedTag=false", "tag", "-a", "-m", "nested", "v1-again", "v1"});
    git(dir, {"tag", "tree", "HEAD^{tree}"});

    GitRepo repo(dir);
    bool ok = false;
    CHECK(repo.logStartPoints(false, &ok) == QStringList({head}) && ok);
    const QStringList all = repo.logStartPoints(true, &ok);
    CHECK(ok && all.size() == 3);
    CHECK(all.contains(head) && all.contains(side) && all.contains(second));
    CHECK(repo.logStartPoints(true) == all);

    QList<Commit> commits;
    int batches = 0;
    CHECK(streamLog(repo, true, &commits, &batches));
    // git's own walk of every ref, as `git log --all` prints it.
    const QStringList logged = QString::fromUtf8(git(dir, {"log", "--date-order", "--format=%H", "--all"}))
                                   .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    CHECK(commits.size() == 5 && commits.size() == logged.size());
    for (int i = 0; i < qMin(commits.size(), logged.size()); ++i)
        CHECK(commits[i].hash == logged[i]);
    const QList<Commit> paged = repo.log(all, 0, 1000);
    CHECK(paged.size() == 5);
    for (int i = 0; i < qMin(paged.size(), logged.size()); ++i)
        CHECK(paged[i].hash == logged[i]);
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

// git with the tagger's and committer's clock set, so tags made in one
// second still have an order.
static void gitAt(const QString &dir, const QStringList &args, const QString &date)
{
    QProcess p;
    p.setWorkingDirectory(dir);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("GIT_COMMITTER_DATE"), date);
    p.setProcessEnvironment(env);
    p.start(QStringLiteral("git"), args);
    p.waitForFinished(15000);
    if (p.exitCode() != 0)
        fprintf(stderr, "git %s failed: %s\n", qPrintable(args.join(' ')), p.readAllStandardError().constData());
}

// A new branch: switched to or only made, from HEAD, a commit or a remote
// branch (which it then tracks); the names git takes; the changes that keep
// a switch from happening; the tags a start can be; a repository without
// commits.
static void testCreateBranch(const QString &base)
{
    const QString server = base + "/create-server.git";
    QDir().mkpath(server);
    git(server, {"init", "-q", "--bare", "-b", "main"});
    const QString seed = initRepo(base + "/create-seed");
    write(seed, "a.txt", "a\n");
    git(seed, {"add", "."});
    git(seed, {"commit", "-q", "-m", "one"});
    git(seed, {"branch", "feature/remote"});
    git(seed, {"push", "-q", server, "main", "feature/remote"});
    const QString dir = base + "/create-mine";
    git(base, {"clone", "-q", server, dir});
    git(dir, {"config", "user.name", "Tester"});
    git(dir, {"config", "user.email", "tester@example.com"});
    git(dir, {"config", "commit.gpgsign", "false"});
    write(dir, "b.txt", "b\n");
    git(dir, {"add", "."});
    git(dir, {"commit", "-q", "-m", "two"});
    const QString first = QString::fromUtf8(git(dir, {"rev-parse", "HEAD~1"}));
    const QString second = QString::fromUtf8(git(dir, {"rev-parse", "HEAD"}));

    GitRepo repo(dir);
    QString sha, subject;
    CHECK(repo.describeCommit("main", &sha, &subject));
    CHECK(second.startsWith(sha) && sha.size() >= 7 && subject == "two");
    CHECK(repo.describeCommit(first, &sha, &subject) && subject == "one");
    CHECK(!repo.describeCommit("no-such-thing", &sha, &subject));

    // Switching: the new branch is checked out, at HEAD.
    QString error;
    CHECK(repo.createBranch("feature/on", QString(), true, &error));
    CHECK(repo.branch() == "feature/on");
    CHECK(QString::fromUtf8(git(dir, {"rev-parse", "feature/on"})) == second);
    // Not switching: made where it was asked for, the checkout stays.
    CHECK(repo.createBranch("from-commit", first, false, &error));
    CHECK(repo.branch() == "feature/on");
    CHECK(QString::fromUtf8(git(dir, {"rev-parse", "from-commit"})) == first);
    int code = 0;
    repo.run({"config", "--get", "branch.from-commit.merge"}, &code);
    CHECK(code != 0); // a local start is not tracked
    // From a remote branch: it tracks it, as git does by default.
    CHECK(repo.createBranch("tracking", "origin/feature/remote", true, &error));
    CHECK(repo.branch() == "tracking");
    CHECK(git(dir, {"config", "--get", "branch.tracking.merge"}) == "refs/heads/feature/remote");
    CHECK(repo.upstreamState().upstream == "origin/feature/remote");
    // A name that is taken fails with git's own words.
    error.clear();
    CHECK(!repo.createBranch("main", QString(), true, &error));
    CHECK(error.contains("already exists"));
    CHECK(repo.branch() == "tracking");
    git(dir, {"switch", "-q", "main"});

    // The names git takes.
    for (const QString &bad : {QString(), QStringLiteral("a b"), QStringLiteral("HEAD"), QStringLiteral("-x"),
                               QStringLiteral("@{-1}"), QStringLiteral("x..y"), QStringLiteral("x.lock")})
        CHECK(!repo.isValidBranchName(bad));
    for (const QString &good : {QStringLiteral("feature/ok"), QStringLiteral("x"), QStringLiteral("fix-1.2")})
        CHECK(repo.isValidBranchName(good));
    CHECK(GitRepo::typedBranchName("my new  branch") == "my-new--branch");
    CHECK(GitRepo::typedBranchName("feature/ok") == "feature/ok");

    // The changes in the way of a switch: a changed file that differs at the
    // start point, not one that is the same there; untracked ones too.
    CHECK(repo.pathsBlockingSwitch(first).isEmpty()); // a clean tree
    write(dir, "a.txt", "changed\n");
    write(dir, "b.txt", "changed too\n");
    CHECK(repo.pathsBlockingSwitch(first) == QStringList{"b.txt"});
    CHECK(repo.pathsBlockingSwitch("main").isEmpty());
    CHECK(repo.pathsBlockingSwitch(QString()).isEmpty());
    git(dir, {"checkout", "-q", "--", "a.txt", "b.txt"});
    git(dir, {"switch", "-q", "from-commit"});
    write(dir, "b.txt", "untracked here\n");
    write(dir, "c.txt", "untracked\n");
    CHECK(repo.pathsBlockingSwitch("main") == QStringList{"b.txt"});
    CHECK(repo.pathsBlockingSwitch("main", {"c.txt"}).isEmpty()); // the paths given, not read again
    CHECK(repo.pathsBlockingSwitch("main", {"b.txt", "c.txt"}) == QStringList{"b.txt"});
    QFile::remove(dir + "/b.txt");
    QFile::remove(dir + "/c.txt");
    git(dir, {"switch", "-q", "main"});

    // The changed files as the card counts them: a staged rename once, a file
    // staged and changed again once, the untracked ones each.
    CHECK(repo.changedFileCount() == 0);
    git(dir, {"mv", "a.txt", "moved.txt"});
    write(dir, "b.txt", "staged\n");
    git(dir, {"add", "b.txt"});
    write(dir, "b.txt", "staged, then changed\n");
    write(dir, "new/one.txt", "1\n");
    write(dir, "new/two.txt", "2\n");
    CHECK(repo.changedPaths().size() == 5); // both sides of the rename
    CHECK(repo.changedFileCount() == 4);
    git(dir, {"reset", "-q", "--hard"});
    QDir(dir + "/new").removeRecursively();
    CHECK(repo.changedFileCount() == 0);

    // A start that renames a changed file: its old name is in the way, which
    // a diff pairing the rename would only have called by the new one. A
    // start named like a file is a start, not the file.
    const QString renames = initRepo(base + "/create-renames");
    write(renames, "a.txt", "a file long enough to be seen as renamed\n");
    write(renames, "README.md", "readme\n");
    git(renames, {"add", "."});
    git(renames, {"commit", "-q", "-m", "one"});
    git(renames, {"switch", "-q", "-c", "moved"});
    git(renames, {"mv", "a.txt", "b.txt"});
    git(renames, {"commit", "-q", "-m", "moved"});
    git(renames, {"switch", "-q", "-c", "README.md", "main"});
    write(renames, "README.md", "readme on its branch\n");
    git(renames, {"commit", "-q", "-am", "readme"});
    git(renames, {"switch", "-q", "main"});
    GitRepo renaming(renames);
    write(renames, "a.txt", "changed\n");
    CHECK(renaming.pathsBlockingSwitch("moved") == QStringList{"a.txt"});
    CHECK(renaming.pathsBlockingSwitch("README.md").isEmpty());
    git(renames, {"checkout", "-q", "--", "a.txt"});
    write(renames, "README.md", "changed\n");
    CHECK(renaming.pathsBlockingSwitch("README.md") == QStringList{"README.md"});
    CHECK(renaming.pathsBlockingSwitch("moved").isEmpty());

    // Tags, the newest first.
    CHECK(repo.tags().isEmpty());
    gitAt(dir, {"tag", "-a", "-m", "old", "v1", first}, "2024-01-01T10:00:00+00:00");
    gitAt(dir, {"tag", "-a", "-m", "new", "v2", second}, "2024-02-01T10:00:00+00:00");
    gitAt(dir, {"tag", "-a", "-m", "middle", "v1.5", first}, "2024-01-15T10:00:00+00:00");
    CHECK(repo.tags() == QStringList({"v2", "v1.5", "v1"}));
    CHECK(repo.createBranch("from-tag", "v1", false, &error));
    CHECK(QString::fromUtf8(git(dir, {"rev-parse", "from-tag"})) == first);

    // No commits yet: switching renames the unborn branch; only switching works.
    const QString unbornDir = initRepo(base + "/create-unborn");
    GitRepo unborn(unbornDir);
    CHECK(!unborn.hasHead());
    CHECK(unborn.pathsBlockingSwitch(QString()).isEmpty());
    error.clear();
    CHECK(!unborn.createBranch("side", QString(), false, &error));
    CHECK(!error.isEmpty());
    CHECK(unborn.createBranch("first-branch", QString(), true, &error));
    CHECK(unborn.branch() == "first-branch");
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

// Runs one mergeAsync to completion.
static GitRepo::MergeResult runMerge(GitRepo &repo, const QString &source, bool noFF, QString *error)
{
    QEventLoop loop;
    GitRepo::MergeResult result = GitRepo::MergeFailed;
    repo.mergeAsync(source, noFF, &repo, [&](GitRepo::MergeResult r, const QString &message) {
        result = r;
        if (error)
            *error = message;
        loop.quit();
    });
    loop.exec();
    return result;
}

// The same merges as testMerge, driven through the event loop.
static void testMergeAsync(const QString &base)
{
    const QString dir = initRepo(base + "/merge-async");
    write(dir, "a.txt", "one\ntwo\nthree\n");
    write(dir, "b.txt", "b\n");
    git(dir, {"add", "."});
    git(dir, {"commit", "-q", "-m", "base"});
    const QString start = git(dir, {"rev-parse", "HEAD"});
    git(dir, {"checkout", "-q", "-b", "feature"});
    write(dir, "a.txt", "one\nTWO\nthree\n");
    git(dir, {"commit", "-q", "-am", "feature work"});
    git(dir, {"checkout", "-q", "main"});

    GitRepo repo(dir);
    QString error;
    // main has nothing of its own: a fast-forward.
    CHECK(runMerge(repo, "feature", false, &error) == GitRepo::Merged);
    CHECK(error.isEmpty());
    CHECK(git(dir, {"rev-parse", "HEAD"}) == git(dir, {"rev-parse", "feature"}));
    CHECK(repo.headCommit().parents.size() == 1);

    // Both sides moved, in different files: a clean merge commit.
    git(dir, {"checkout", "-q", "-b", "other", start});
    write(dir, "b.txt", "B\n");
    git(dir, {"commit", "-q", "-am", "other work"});
    CHECK(runMerge(repo, "feature", false, &error) == GitRepo::Merged);
    CHECK(repo.headCommit().parents.size() == 2);
    CHECK(!repo.mergeInProgress());

    // Both edited the same line: the merge is left to resolve.
    git(dir, {"checkout", "-q", "-b", "clash", start});
    write(dir, "a.txt", "one\n2\nthree\n");
    git(dir, {"commit", "-q", "-am", "clash work"});
    CHECK(runMerge(repo, "feature", false, &error) == GitRepo::MergeConflicts);
    CHECK(error.contains("a.txt"));
    CHECK(repo.mergeInProgress());
    CHECK(repo.mergeState().conflicts == QStringList({"a.txt"}));
    CHECK(repo.abortMerge(&error));
}

// A caller that goes away mid-run (the merge view closed with Escape) must
// not take git down with it: the command runs to its end, only the answer
// is dropped, and the process cleans up after itself.
static void testRunAsyncOutlivesContext(const QString &base)
{
    const QString dir = initRepo(base + "/async-context");
    write(dir, "a.txt", "one\n");
    git(dir, {"add", "."});
    git(dir, {"commit", "-q", "-m", "base"});
    const QString before = git(dir, {"rev-parse", "HEAD"});

    GitRepo repo(dir);
    bool answered = false;
    auto *context = new QObject;
    QProcess *const process = repo.runAsync({"commit", "-q", "--allow-empty", "-m", "orphaned"}, context,
                                            [&answered](int, const QByteArray &, const QByteArray &) { answered = true; });
    QPointer<QProcess> alive = process;
    CHECK(process->parent() == &repo);
    delete context; // before git is done
    QEventLoop loop;
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    QObject::connect(process, &QProcess::finished, &loop, &QEventLoop::quit);
    loop.exec();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    CHECK(!answered);
    CHECK(!alive); // deleted itself once git was done
    CHECK(git(dir, {"rev-parse", "HEAD"}) != before);
    CHECK(git(dir, {"log", "-1", "--format=%s"}) == "orphaned");
}

// A tracked file that became ignored and was `git rm --cached`: still on disk,
// so `git add` refuses it and `git commit -- paths` would quietly re-add it.
static void testCommitIgnoredDeletion(const QString &base)
{
    const QString dir = initRepo(base + "/ignored");
    write(dir, "keep.txt", "k1\n");
    write(dir, "Makefile.x", "generated\n");
    write(dir, "tracked.log", "log1\n");
    git(dir, {"add", "."});
    git(dir, {"commit", "-q", "-m", "initial"});
    write(dir, ".gitignore", "Makefile*\n*.log\n");
    git(dir, {"rm", "-q", "--cached", "Makefile.x"});
    write(dir, "forced.log", "forced\n");
    git(dir, {"add", "-f", "forced.log"});
    write(dir, "keep.txt", "k2\n");

    GitRepo repo(dir);
    QList<FileChange> st = repo.status();
    CHECK(find(st, "Makefile.x") && find(st, "Makefile.x")->kind == FileChange::Deleted);
    CHECK(find(st, "forced.log") && find(st, "forced.log")->kind == FileChange::Added);

    // keep.txt stays out of the commit.
    QString error;
    const bool ok = repo.commit("ignore generated files", {".gitignore", "Makefile.x", "forced.log"}, &error);
    CHECK(ok);
    if (!ok)
        fprintf(stderr, "commit error: %s\n", qPrintable(error));
    CHECK(git(dir, {"ls-tree", "--name-only", "HEAD"}) == ".gitignore\nforced.log\nkeep.txt\ntracked.log");
    CHECK(git(dir, {"show", "HEAD:keep.txt"}) == "k1");
    CHECK(QFile::exists(dir + "/Makefile.x"));
    CHECK(git(dir, {"status", "--porcelain"}) == "M keep.txt"); // trimmed: " M keep.txt"
    CHECK(!QFile::exists(dir + "/.git/omagit-commit-index"));

    // The same through amend, where the scratch index starts from the parent tree.
    git(dir, {"rm", "-q", "--cached", "tracked.log", "forced.log"});
    repo.setAmend(true);
    CHECK(repo.amendCommit("ignore generated files and logs", {".gitignore", "Makefile.x", "forced.log", "tracked.log"},
                           &error));
    CHECK(git(dir, {"ls-tree", "--name-only", "HEAD"}) == ".gitignore\nkeep.txt");
    CHECK(QFile::exists(dir + "/tracked.log") && QFile::exists(dir + "/forced.log"));
    repo.setAmend(false);
    CHECK(git(dir, {"status", "--porcelain"}) == "M keep.txt");
}

// --- Signing in ------------------------------------------------------------

static void testAskPassPrompts()
{
    AskPassRequest r = parseAskPassPrompt(QStringLiteral("Username for 'https://github.com': "));
    CHECK(r.kind == AskPassRequest::Username);
    CHECK(r.target == "https://github.com");
    CHECK(r.host == "github.com");
    CHECK(r.user.isEmpty());
    CHECK(r.context == "https://github.com");

    r = parseAskPassPrompt(QStringLiteral("Password for 'https://andras@github.com': "));
    CHECK(r.kind == AskPassRequest::Password);
    CHECK(r.host == "github.com");
    CHECK(r.user == "andras");
    CHECK(r.context == "https://github.com"); // the user is who, not where

    // The context is git's own: the scheme, the host, the port and the path
    // its prompt carries (credential.useHttpPath puts one there), without the
    // user. The host alone would lump an http remote in with an https one.
    r = parseAskPassPrompt(QStringLiteral("Password for 'https://alice@example.com:8443/team/repo': "));
    CHECK(r.host == "example.com");
    CHECK(r.user == "alice");
    CHECK(r.context == "https://example.com:8443/team/repo");
    CHECK(parseAskPassPrompt(QStringLiteral("Username for 'http://example.com': ")).context
          == "http://example.com");

    r = parseAskPassPrompt(QStringLiteral("Enter passphrase for key '/home/x/.ssh/id_ed25519': "));
    CHECK(r.kind == AskPassRequest::Passphrase);
    CHECK(r.keyPath == "/home/x/.ssh/id_ed25519");
    CHECK(r.host.isEmpty());
    CHECK(r.context == "/home/x/.ssh/id_ed25519");

    // ssh's second and third go at the same key, worded differently and
    // asking for the same thing.
    r = parseAskPassPrompt(QStringLiteral("Bad passphrase, try again for key '/x/id_ed25519': "));
    CHECK(r.kind == AskPassRequest::Passphrase);
    CHECK(r.keyPath == "/x/id_ed25519");
    CHECK(r.context == "/x/id_ed25519");

    // Anything else is shown as git or ssh worded it, quotes and all.
    const QString hostKey = QStringLiteral("The authenticity of host 'github.com (140.82.121.4)' can't be "
                                           "established.\nAre you sure you want to continue connecting? ");
    r = parseAskPassPrompt(hostKey);
    CHECK(r.kind == AskPassRequest::Other);
    CHECK(r.prompt == hostKey);
    CHECK(r.target.isEmpty() && r.host.isEmpty());
    CHECK(r.context == hostKey);
}

// What one helper run left behind: its exit status and what it printed.
struct AskPassRun {
    int code = -1;
    QByteArray out;
};

// Spins the event loop until `ready` holds, or the wait runs out; returns
// whether it held in the end.
template <typename Ready>
static bool spinUntil(Ready ready, int timeoutMs = 5000)
{
    QElapsedTimer clock;
    clock.start();
    while (!ready() && clock.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::WaitForMoreEvents, 50);
    return ready();
}

// The frame AskPass listens for: a big-endian length and the prompt in UTF-8.
// Written out here so a test can play the helper without askPassClient, which
// waits for its answer and would hold this thread while the server needs it.
static QByteArray promptFrame(const QString &prompt)
{
    const QByteArray payload = prompt.toUtf8();
    QByteArray out;
    QDataStream stream(&out, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);
    stream << quint32(payload.size());
    return out + payload;
}

// Runs the helper's side in a thread of its own — it blocks on the answer,
// while the server needs this thread's event loop to give one.
static AskPassRun runAskPassClient(const QString &socketPath, const QString &prompt)
{
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    AskPassRun run;
    QThread *thread = QThread::create([&] { run.code = askPassClient(socketPath, prompt, &buffer); });
    QEventLoop loop;
    QObject::connect(thread, &QThread::finished, &loop, &QEventLoop::quit);
    QTimer guard; // so a helper that is never answered ends the test instead of hanging it
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
    guard.start(20000);
    thread->start();
    loop.exec();
    thread->wait();
    delete thread;
    run.out = buffer.data();
    return run;
}

// A socket file nobody serves: bound, never listened on, closed — what a
// crashed run leaves behind.
static bool leaveStaleSocket(const QString &path)
{
    const QByteArray name = QFile::encodeName(path);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (name.size() >= int(sizeof(address.sun_path)))
        return false;
    memcpy(address.sun_path, name.constData(), size_t(name.size()));
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return false;
    const bool bound = ::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0;
    ::close(fd);
    return bound;
}

// Only leftovers are cleared away: a socket named after a pid this process
// cannot see is still somebody's while a server answers on it (an Omagit in
// another pid namespace — a sandbox, a test run under unshare), and a server
// whose own file was removed all the same listens again when asked to.
static void testAskPassKeepsLiveSockets()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    const QString live = dir + QStringLiteral("/omagit-askpass-999999999-live");
    const QString stale = dir + QStringLiteral("/omagit-askpass-999999998-stale");
    QLocalServer::removeServer(live);
    QLocalServer::removeServer(stale);
    QLocalServer elsewhere;
    CHECK(elsewhere.listen(live));
    CHECK(leaveStaleSocket(stale));

    AskPass askPass;
    CHECK(askPass.listen());
    CHECK(QFileInfo::exists(live));
    CHECK(!QFileInfo::exists(stale));
    elsewhere.close();

    QObject::connect(&askPass, &AskPass::requestReceived, &askPass, [&](const AskPassRequest &request) {
        askPass.answerSecret(request.id, "back");
    });
    CHECK(QFile::remove(askPass.socketPath()));
    CHECK(askPass.listen());
    CHECK(QFileInfo::exists(askPass.socketPath()));
    const auto result = runAskPassClient(askPass.socketPath(), "Token:");
    CHECK(result.code == 0);
    CHECK(result.out == "back\n");
}

static void testAskPassServer()
{
    AskPass askPass;
    CHECK(askPass.env().isEmpty()); // nothing to point git at before it listens
    CHECK(askPass.listen());
    CHECK(askPass.listen()); // idempotent
    const QString helper = QCoreApplication::applicationFilePath();
    CHECK(askPass.env() == QStringList({"GIT_ASKPASS=" + helper, "SSH_ASKPASS=" + helper,
                                        "SSH_ASKPASS_REQUIRE=force",
                                        "OMAGIT_ASKPASS_SOCKET=" + askPass.socketPath()}));
    CHECK(askPass.socketPath().contains(QString::number(QCoreApplication::applicationPid())));
    {
        AskPass clonePass;
        CHECK(clonePass.socketPath() != askPass.socketPath());
        CHECK(clonePass.listen());
        QObject::connect(&clonePass, &AskPass::requestReceived, &clonePass, [&](const AskPassRequest &request) {
            clonePass.answerSecret(request.id, "clone-only");
        });
        const auto result = runAskPassClient(clonePass.socketPath(), "Token:");
        CHECK(result.code == 0);
        CHECK(result.out == "clone-only\n");
    } // destroying a clone's helper must leave remote sync's socket reachable


    QList<AskPassRequest> seen;
    bool stalled = false; // leave the request standing, the way a dialog does
    int staleId = 0;      // a dropped request to try answering first
    QObject::connect(&askPass, &AskPass::requestReceived, &askPass, [&](const AskPassRequest &request) {
        seen << request;
        CHECK(request.id > 0);
        if (stalled)
            return;
        // What a dialog left over from a dropped request would say, said now
        // that somebody else is being asked: it is no answer to this one, and
        // no refusal of it either.
        if (staleId) {
            askPass.cancel(staleId);
            askPass.answerLogin(staleId, QStringLiteral("mallory"), QStringLiteral("not-hers"));
            CHECK(!askPass.cancelled());
            staleId = 0;
        }
        switch (request.kind) {
        case AskPassRequest::Username: askPass.answerLogin(request.id, "alice", "s3cret"); break;
        // A password prompt only gets this far when no kept login is that
        // user's, so it is a sign-in of its own — as the dialog does, the user
        // the URL named comes back with the password typed for it.
        case AskPassRequest::Password:
            askPass.answerLogin(request.id, request.user, request.user + "-pw");
            break;
        // The passphrase is given once and refused; the second try gives up.
        case AskPassRequest::Passphrase:
            request.retry ? askPass.cancel(request.id) : askPass.answerSecret(request.id, "unlock");
            break;
        default: askPass.cancel(request.id); break; // nothing else should reach a dialog
        }
    });

    const QString userPrompt = QStringLiteral("Username for 'https://example.com': ");
    const QString passPrompt = QStringLiteral("Password for 'https://alice@example.com': ");
    const QString keyPrompt = QStringLiteral("Enter passphrase for key '/tmp/key': ");
    const QString keyAgain = QStringLiteral("Bad passphrase, try again for key '/tmp/key': ");

    // The one sign-in of the operation, collected at the username prompt...
    AskPassRun run = runAskPassClient(askPass.socketPath(), userPrompt);
    CHECK(run.code == 0);
    CHECK(run.out == "alice\n");
    CHECK(seen.size() == 1 && !seen.at(0).retry);

    // ... answers git's next question without a second dialog...
    run = runAskPassClient(askPass.socketPath(), passPrompt);
    CHECK(run.code == 0);
    CHECK(run.out == "s3cret\n");
    CHECK(seen.size() == 1);

    // ... and goes on answering for as long as the operation lasts, however
    // often git asks: a `fetch --all` over two remotes on one host asks for
    // the username and the password once each per remote.
    run = runAskPassClient(askPass.socketPath(), userPrompt);
    CHECK(run.code == 0 && run.out == "alice\n");
    run = runAskPassClient(askPass.socketPath(), passPrompt);
    CHECK(run.code == 0 && run.out == "s3cret\n");
    CHECK(seen.size() == 1);

    // A remote of the same host that names somebody else is somebody else's
    // sign-in: it asks, and alice's password is not what bob's prompt gets.
    const QString bobPrompt = QStringLiteral("Password for 'https://bob@example.com': ");
    run = runAskPassClient(askPass.socketPath(), bobPrompt);
    CHECK(run.code == 0);
    CHECK(run.out == "bob-pw\n");
    CHECK(seen.size() == 2 && seen.at(1).kind == AskPassRequest::Password);
    CHECK(seen.at(1).user == "bob" && !seen.at(1).retry); // asked about, not asked again
    // Bob's own prompts are then answered from memory like anyone's.
    run = runAskPassClient(askPass.socketPath(), bobPrompt);
    CHECK(run.code == 0 && run.out == "bob-pw\n");
    CHECK(seen.size() == 2);

    // The same host over http is another credential context — handing it what
    // was typed for https would put it on the wire in the clear — so it asks.
    run = runAskPassClient(askPass.socketPath(), QStringLiteral("Username for 'http://example.com': "));
    CHECK(run.code == 0 && run.out == "alice\n");
    CHECK(seen.size() == 3 && seen.at(2).context == "http://example.com" && !seen.at(2).retry);

    // A passphrase is not kept: ssh asks up to three times for the same key,
    // and each try is a dialog of its own.
    run = runAskPassClient(askPass.socketPath(), keyPrompt);
    CHECK(run.code == 0);
    CHECK(run.out == "unlock\n");
    CHECK(seen.size() == 4 && seen.at(3).kind == AskPassRequest::Passphrase && !seen.at(3).retry);

    // Asked for it once more: the one given was refused, and the dialog says
    // so. Cancelled here — nothing on stdout, and ssh is told there is no
    // answer.
    run = runAskPassClient(askPass.socketPath(), keyAgain);
    CHECK(run.code == 1);
    CHECK(run.out.isEmpty());
    CHECK(askPass.cancelled());
    CHECK(seen.size() == 5 && seen.at(4).keyPath == "/tmp/key" && seen.at(4).retry);

    // And that is the end of the asking for this operation: git walks on to
    // the next remote of a `fetch --all` after a refusal and prompts there
    // too, and every one of those is turned down where it arrives — no second
    // dialog for a user who has just said no. Kept logins count for nothing
    // either: the operation is being given up on.
    for (const QString &prompt : {userPrompt, passPrompt,
                                  QStringLiteral("Username for 'https://elsewhere.example': "),
                                  QStringLiteral("Enter passphrase for key '/tmp/other': ")}) {
        run = runAskPassClient(askPass.socketPath(), prompt);
        CHECK(run.code == 1);
        CHECK(run.out.isEmpty());
    }
    CHECK(seen.size() == 5);

    // The operation is over: the logins, what was answered and the cancelled
    // flag go with it, so the next one asks afresh and nothing is a retry.
    askPass.endOperation();
    CHECK(!askPass.cancelled());
    run = runAskPassClient(askPass.socketPath(), userPrompt);
    CHECK(run.code == 0 && run.out == "alice\n");
    CHECK(seen.size() == 6 && !seen.at(5).retry);
    run = runAskPassClient(askPass.socketPath(), keyPrompt);
    CHECK(run.code == 0 && run.out == "unlock\n");
    CHECK(seen.size() == 7 && !seen.at(6).retry);

    // A helper that asks and then goes away without waiting: git was killed,
    // or the ten minutes it allows for an answer ran out. The request is
    // dropped by id — whoever has it on screen closes that dialog — and git is
    // running again as far as the timeout is concerned.
    int dropped = 0, answers = 0;
    QObject::connect(&askPass, &AskPass::requestDropped, &askPass, [&](int id) { dropped = id; });
    QObject::connect(&askPass, &AskPass::answered, &askPass, [&] { ++answers; });
    stalled = true;
    QLocalSocket abandoned;
    abandoned.connectToServer(askPass.socketPath());
    CHECK(abandoned.waitForConnected(5000));
    abandoned.write(promptFrame(QStringLiteral("Username for 'https://abandoned.example': ")));
    CHECK(abandoned.waitForBytesWritten(5000));
    CHECK(spinUntil([&] { return seen.size() == 8; }));
    CHECK(dropped == 0 && answers == 0); // still being asked
    const int abandonedId = seen.at(7).id;
    abandoned.disconnectFromServer();
    CHECK(spinUntil([&] { return dropped != 0; }));
    CHECK(dropped == abandonedId);
    CHECK(answers == 1);

    // The dialog of that request is still on screen until it hears so, and
    // whatever it says then names a request that is over: the next sign-in —
    // another host, another user — is neither answered with it nor turned
    // down by it. (The handler above tries both before answering properly.)
    stalled = false;
    staleId = abandonedId;
    run = runAskPassClient(askPass.socketPath(), QStringLiteral("Username for 'https://after.example': "));
    CHECK(run.code == 0 && run.out == "alice\n");
    CHECK(seen.size() == 9 && staleId == 0);
    CHECK(!askPass.cancelled());
    CHECK(dropped == abandonedId); // answering it was no reason to drop anything

    // The operation ending under a prompt drops that one as well: git is on
    // its way out, so the helper is told there is no answer and the dialog is
    // taken off the screen rather than left to sign in to nothing. The helpers
    // queued behind it end with it too — another remote of a parallel fetch, a
    // submodule's — whose turn would otherwise come next: a dialog for an
    // operation that is over, whose answer would be kept for the one after.
    // One that has not got its prompt out yet asked for this operation just
    // the same.
    stalled = true;
    QLocalSocket standing, queued, silent;
    for (QLocalSocket *socket : {&standing, &queued, &silent}) {
        socket->connectToServer(askPass.socketPath());
        CHECK(socket->waitForConnected(5000));
    }
    standing.write(promptFrame(QStringLiteral("Username for 'https://ending.example': ")));
    CHECK(standing.waitForBytesWritten(5000));
    CHECK(spinUntil([&] { return seen.size() == 10; }));
    queued.write(promptFrame(QStringLiteral("Username for 'https://queued.example': ")));
    CHECK(queued.waitForBytesWritten(5000));
    askPass.endOperation();
    CHECK(dropped == seen.at(9).id);
    // Turned down where they stand and let go, both of them, and neither is
    // asked about however long the event loop runs afterwards.
    const QByteArray cancelledFrame = promptFrame(QStringLiteral("C"));
    QByteArray answer;
    CHECK(spinUntil([&] {
        answer += queued.readAll();
        return answer.size() >= cancelledFrame.size();
    }));
    CHECK(answer == cancelledFrame);
    CHECK(spinUntil([&] {
        return queued.state() != QLocalSocket::ConnectedState
            && silent.state() != QLocalSocket::ConnectedState;
    }));
    CHECK(!spinUntil([&] { return seen.size() != 10; }, 200));
    standing.disconnectFromServer();
    stalled = false;

    // And nothing of that operation is left over for the next: its prompt is a
    // first asking, not a retry, and is answered by a dialog rather than out of
    // a cache that went with it.
    run = runAskPassClient(askPass.socketPath(), userPrompt);
    CHECK(run.code == 0 && run.out == "alice\n");
    CHECK(seen.size() == 11 && !seen.at(10).retry);
    CHECK(!askPass.cancelled());

    // No socket to talk to: the helper says so instead of waiting.
    QBuffer nowhere;
    nowhere.open(QIODevice::WriteOnly);
    CHECK(askPassClient(QString(), userPrompt, &nowhere) == 1);
    CHECK(nowhere.data().isEmpty());
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
    testCommitIgnoredDeletion(tmp.path());
    testStatusAndHistory(tmp.path());
    testLogStreamParser();
    testLogStream(tmp.path());
    testLogStartPoints(tmp.path());
    testRemote(tmp.path());
    testBranches(tmp.path());
    testCreateBranch(tmp.path());
    testMerge(tmp.path());
    testMergeAsync(tmp.path());
    testRunAsyncOutlivesContext(tmp.path());
    testPatch(tmp.path());
    testAgentCommands();
    testAgentCatalogs();
    testAskPassPrompts();
    testAskPassServer();
    testAskPassKeepsLiveSockets();
    if (failures == 0)
        printf("all checks passed\n");
    return failures == 0 ? 0 : 1;
}
