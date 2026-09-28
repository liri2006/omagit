// The History page: the commit graph's lanes and geometry, the whole-history
// filter that git's log streams in pages, the details card, and a refresh
// that keeps the search, the scroll, the current commit and its diff.
#include "UiTestCase.h"
#include "fixtures.h"

#include "../../src/CommitDetails.h"
#include "../../src/DiffModel.h"
#include "../../src/HistoryModel.h"
#include "../../src/HistoryView.h"
#include "../../src/UiHelpers.h"

#include <QAbstractItemModelTester>
#include <QApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTimeZone>

namespace {

struct ImportedCommit {
    QString message;
    QString author = QStringLiteral("Test");
    QString email = QStringLiteral("test@example.com");
    QStringList files = {}; // written by the commit, the message their content
};

// A new repository whose main branch is `commits`, the oldest first, every
// one an hour after the one before and empty but for its `files`; one `git
// fast-import` makes them all, where hundreds of `git commit`s would take
// seconds. Under a hundred objects, git leaves each loose.
bool importHistory(const QString &dir, const QList<ImportedCommit> &commits)
{
    if (!git(dir, {QStringLiteral("init"), QStringLiteral("-q"), QStringLiteral("-b"), QStringLiteral("main")}))
        return false;
    constexpr qint64 kNewYear2024 = 1704067200; // 2024-01-01 00:00 UTC, stamp()'s day
    QByteArray stream;
    for (int i = 0; i < commits.size(); ++i) {
        const ImportedCommit &c = commits.at(i);
        const QByteArray who =
            QStringLiteral("%1 <%2> %3 +0000").arg(c.author, c.email).arg(kNewYear2024 + 3600 * (i + 1)).toUtf8();
        const QByteArray message = c.message.toUtf8();
        stream += "commit refs/heads/main\nauthor " + who + "\ncommitter " + who + "\ndata "
            + QByteArray::number(message.size()) + '\n' + message + '\n';
        for (const QString &file : c.files)
            stream += "M 644 inline " + file.toUtf8() + "\ndata " + QByteArray::number(message.size() + 1) + '\n'
                + message + "\n\n";
    }
    QProcess p;
    p.setWorkingDirectory(dir);
    p.start(QStringLiteral("git"), {QStringLiteral("fast-import"), QStringLiteral("--quiet")});
    p.write(stream);
    p.closeWriteChannel();
    if (!p.waitForFinished(30000) || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        qWarning("git fast-import failed: %s", p.readAllStandardError().constData());
        return false;
    }
    return true;
}

} // namespace

class HistoryTest : public UiTestCase
{
    Q_OBJECT
private slots:
    void graphOfLinearHistory()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(git(dir.path(), {"init", "-q", "-b", "main"}));
        QVERIFY(commit(dir.path(), QStringLiteral("A"), 1));
        QVERIFY(commit(dir.path(), QStringLiteral("B"), 2));
        QVERIFY(commit(dir.path(), QStringLiteral("C"), 3));

        GitRepo repo(dir.path());
        HistoryModel model(&repo);
        model.reload();
        QCOMPARE(model.rowCount(), 3);
        QCOMPARE(model.laneCount(), 1);
        for (int row = 0; row < 3; ++row)
            QCOMPARE(model.graph(row).lane, 0);
        // The tip starts its line, the root ends it.
        QCOMPARE(model.graph(0).edges.size(), 1);
        QCOMPARE(model.graph(0).edges.first().from, -1);
        QCOMPARE(model.graph(0).edges.first().to, 0);
        QCOMPARE(model.graph(2).edges.size(), 1);
        QCOMPARE(model.graph(2).edges.first().from, 0);
        QCOMPARE(model.graph(2).edges.first().to, -1);
        QVERIFY(model.isHead(0));
        // No header text over the graph, as in the design; the column keeps
        // its name for the tooltip and for screen readers.
        QVERIFY(model.headerData(HistoryModel::Graph, Qt::Horizontal, Qt::DisplayRole).toString().isEmpty());
        QCOMPARE(model.headerData(HistoryModel::Graph, Qt::Horizontal, Qt::ToolTipRole).toString(), QStringLiteral("Graph"));
        QCOMPARE(model.headerData(HistoryModel::Graph, Qt::Horizontal, Qt::AccessibleTextRole).toString(),
                 QStringLiteral("Graph"));
        QCOMPARE(model.headerData(HistoryModel::Message, Qt::Horizontal, Qt::DisplayRole).toString(),
                 QStringLiteral("Message"));
    }

    // Two branches that are merged one after the other: the second one must
    // take the lane the first gave back instead of opening a third.
    void graphOfBranchesAndMerges()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path();
        QVERIFY(git(path, {"init", "-q", "-b", "main"}));
        QVERIFY(commit(path, QStringLiteral("A"), 1));
        QVERIFY(git(path, {"branch", "f1"}));
        QVERIFY(commit(path, QStringLiteral("B"), 2));
        QVERIFY(git(path, {"checkout", "-q", "f1"}));
        QVERIFY(commit(path, QStringLiteral("C"), 3));
        QVERIFY(git(path, {"checkout", "-q", "main"}));
        QVERIFY(git(path, {"merge", "--no-ff", "--no-edit", "-q", "-m", "M1", "f1"}, 4));
        QVERIFY(git(path, {"branch", "f2"}));
        QVERIFY(git(path, {"checkout", "-q", "f2"}));
        QVERIFY(commit(path, QStringLiteral("D"), 5));
        QVERIFY(git(path, {"checkout", "-q", "main"}));
        QVERIFY(git(path, {"merge", "--no-ff", "--no-edit", "-q", "-m", "M2", "f2"}, 6));

        GitRepo repo(dir.path());
        HistoryModel model(&repo);
        model.reload();
        QCOMPARE(model.rowCount(), 6);
        QStringList subjects;
        for (int row = 0; row < model.rowCount(); ++row)
            subjects << model.commit(row).subject;
        QCOMPARE(subjects, QStringList({"M2", "D", "M1", "C", "B", "A"}));
        // The second branch reuses lane 1, so two lanes hold the whole graph.
        QCOMPARE(model.laneCount(), 2);
        QList<int> lanes;
        for (int row = 0; row < model.rowCount(); ++row)
            lanes << model.graph(row).lane;
        QCOMPARE(lanes, QList<int>({0, 1, 0, 1, 0, 1}));
        // The merge branches out of its node into the second parent's lane.
        const GraphRow &merge = model.graph(0);
        QCOMPARE(merge.edges.size(), 2);
        QCOMPARE(merge.edges.last().from, -1);
        QCOMPARE(merge.edges.last().to, 1);
    }

    // The filter's rule: the subject, the body, the author's name or e-mail
    // contain the text, or the SHA starts with it — case aside, a non-ASCII
    // letter's too.
    void commitMatchesTheFiltersRule()
    {
        Commit c;
        c.hash = QStringLiteral("0123abcdef0123abcdef0123abcdef0123abcdef");
        c.shortHash = c.hash.left(7);
        c.subject = QStringLiteral("Fix the parser");
        c.body = QStringLiteral("A longer story\nover two lines, with Ünïcode.");
        c.author = QStringLiteral("Ádám Kovács");
        c.email = QStringLiteral("adam@example.org");
        QVERIFY(commitMatches(c, QStringLiteral("fix")));
        QVERIFY(commitMatches(c, QStringLiteral("THE PARSER")));
        QVERIFY(commitMatches(c, QStringLiteral("two lines")));
        QVERIFY(commitMatches(c, QStringLiteral("ünï")));
        QVERIFY(commitMatches(c, QStringLiteral("ádám")));
        QVERIFY(commitMatches(c, QStringLiteral("KOVÁCS")));
        QVERIFY(commitMatches(c, QStringLiteral("@EXAMPLE.org")));
        QVERIFY(commitMatches(c, QStringLiteral("0123ab")));
        QVERIFY(commitMatches(c, QStringLiteral("0123ABCDEF")));
        QVERIFY(!commitMatches(c, QStringLiteral("123abc"))); // in the SHA, but not its start
        QVERIFY(!commitMatches(c, QStringLiteral("merge")));
        QVERIFY(!commitMatches(c, QStringLiteral("Adam Kovacs"))); // accents count
    }

    // A filter matches a commit by its message or by its author, whichever:
    // one of each shows, and one known by its e-mail alone, and one by the
    // start of its SHA — what git log --grep with --author, which ANDs the
    // two, could not give.
    void theHistoryFilterMatchesMessageOrAuthor()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(importHistory(dir.path(), {{QStringLiteral("Fix the parser"), QStringLiteral("Alice"), QStringLiteral("alice@example.com")},
                                           {QStringLiteral("Update the docs"), QStringLiteral("Bob Fixer"), QStringLiteral("bob@example.com")},
                                           {QStringLiteral("Tidy up"), QStringLiteral("Carol"), QStringLiteral("carol@fix.example")},
                                           {QStringLiteral("Unrelated"), QStringLiteral("Dave"), QStringLiteral("dave@example.com")}}));
        GitRepo repo(dir.path());
        HistoryModel model(&repo);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.reload();
        QCOMPARE(model.rowCount(), 4);
        const auto subjects = [&model] {
            QStringList out;
            for (int row = 0; row < model.rowCount(); ++row)
                out << model.commit(row).subject;
            return out;
        };
        const QString dave = model.commit(0).hash;

        QSignalSpy changes(&model, &HistoryModel::searchChanged);
        model.setFilter(QStringLiteral("fix"));
        QVERIFY(model.filtering());
        QVERIFY(model.searching());
        QCOMPARE(model.rowCount(), 0);
        QTRY_VERIFY(!model.searching());
        QVERIFY(changes.count() >= 2); // the start, and the end at least
        // Newest first, as the list has them: the e-mail, the author, the message.
        QCOMPARE(subjects(), QStringList({"Tidy up", "Update the docs", "Fix the parser"}));
        // Out of sight, the loaded commits stay as they were.
        QVERIFY(model.exhausted());
        QCOMPARE(model.laneCount(), 1);

        model.setFilter(dave.left(8).toUpper());
        QTRY_VERIFY(!model.searching());
        QCOMPARE(subjects(), QStringList({"Unrelated"}));
        QCOMPARE(model.rowOf(dave), 0);
        QVERIFY(model.isHead(0));
        QCOMPARE(model.labels(0).size(), 1); // the chips of a match: main
        QCOMPARE(model.labels(0).first().name, QStringLiteral("main"));
        // Nothing more to load: the one page had room for every match.
        QVERIFY(!model.moreMatches());
        QVERIFY(!model.loadMore());

        // The same text again starts nothing; none brings the loaded commits back.
        changes.clear();
        model.setFilter(dave.left(8).toUpper());
        QCOMPARE(changes.count(), 0);
        model.setFilter(QString());
        QVERIFY(!model.filtering() && !model.searching());
        QCOMPARE(subjects(), QStringList({"Unrelated", "Tidy up", "Update the docs", "Fix the parser"}));
        QCOMPARE(model.graph(3).lane, 0);
    }

    // A search takes its matches a page at a time, a batch of them (500 in
    // the app, seven here), and lets git go; loadMore() takes the next page
    // from where the last one stopped, one page at a time, the rows and the
    // current one staying put, until the walk reaches the end. The pages
    // together are one walk's matches: the same commits in the same order,
    // none twice, none missing. Matches that fit in one page are the whole
    // history's, with nothing more to load; a new filter starts at page one.
    void theHistorySearchLoadsItsMatchesInPages()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QList<ImportedCommit> imported;
        for (int i = 1; i <= 400; ++i)
            imported.append({(i % 3 ? QStringLiteral("miss %1") : QStringLiteral("hit %1")).arg(i)});
        imported[0].message = QStringLiteral("rare, the oldest");
        imported[199].message = QStringLiteral("rare, in the middle");
        QVERIFY(importHistory(dir.path(), imported));
        GitRepo repo(dir.path());
        HistoryModel model(&repo);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QCOMPARE(model.batchSize(), 500);
        model.setBatchSize(7);
        QCOMPARE(model.batchSize(), 7);
        model.reload();
        QCOMPARE(model.rowCount(), 7); // the log's batch is the same number
        const auto subjects = [&model] {
            QStringList out;
            for (int row = 0; row < model.rowCount(); ++row)
                out << model.commit(row).subject;
            return out;
        };
        const auto hashes = [&model] {
            QStringList out;
            for (int row = 0; row < model.rowCount(); ++row)
                out << model.commit(row).hash;
            return out;
        };
        // The matches of one search without pages, in git's order.
        const auto unpaged = [&repo](const QString &text) {
            QStringList out;
            for (const Commit &c : repo.log(repo.logStartPoints(false), 0, 100000))
                if (commitMatches(c, text))
                    out << c.hash;
            return out;
        };
        const QStringList hits = unpaged(QStringLiteral("hit"));
        QCOMPARE(hits.size(), 133); // nineteen pages of seven
        const auto gitRunning = [&repo] { return !repo.findChildren<QProcess *>().isEmpty(); };

        // The first page: exactly the first seven, git gone, more to load.
        QSignalSpy changes(&model, &HistoryModel::searchChanged);
        model.setFilter(QStringLiteral("hit"));
        QVERIFY(model.searching() && !model.moreMatches());
        QVERIFY(!model.loadMore()); // a page at a time
        QTRY_VERIFY(!model.searching());
        QVERIFY(model.moreMatches());
        QCOMPARE(hashes(), hits.first(7));
        QTRY_VERIFY(!gitRunning());
        const int ended = changes.count();
        QTest::qWait(100);
        QCOMPARE(changes.count(), ended);
        QCOMPARE(model.rowCount(), 7);

        // Page after page: appended below the rows there, the current one
        // staying where it is, never more than seven at a time. The
        // twentieth page finds no match left and ends the walk.
        QItemSelectionModel selection(&model);
        selection.setCurrentIndex(model.index(3, HistoryModel::Message),
                                  QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        const QString selected = model.commit(3).hash;
        QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
        int pages = 1;
        while (model.moreMatches() && pages < 50) {
            const QStringList before = hashes();
            changes.clear();
            QVERIFY(model.loadMore());
            QCOMPARE(changes.count(), 1); // the page started
            QVERIFY(model.searching() && !model.moreMatches());
            QVERIFY(!model.loadMore());
            QTRY_VERIFY(!model.searching());
            QCOMPARE(hashes().first(before.size()), before);
            QVERIFY(model.rowCount() - before.size() <= 7);
            QCOMPARE(selection.currentIndex().row(), 3);
            ++pages;
        }
        QCOMPARE(pages, 20);
        QCOMPARE(hashes(), hits);
        QCOMPARE(model.commit(3).hash, selected);
        QCOMPARE(resets.count(), 0);
        QVERIFY(!model.moreMatches());
        QVERIFY(!model.loadMore());
        QTRY_VERIFY(!gitRunning());

        // Matches that fit in one page: git walks to the end, the oldest
        // commit and all, and there is nothing more to load.
        model.setFilter(QStringLiteral("rare"));
        QTRY_VERIFY(!model.searching());
        QVERIFY(!model.moreMatches());
        QCOMPARE(subjects(), QStringList({"rare, in the middle", "rare, the oldest"}));
        QCOMPARE(hashes(), unpaged(QStringLiteral("rare")));
        QVERIFY(!model.loadMore());

        // A new filter starts at the first page, and none brings the loaded
        // commits back, one batch of them.
        model.setFilter(QStringLiteral("hit"));
        QTRY_VERIFY(!model.searching());
        QVERIFY(model.moreMatches());
        QCOMPARE(hashes(), hits.first(7));
        model.setFilter(QString());
        QVERIFY(!model.filtering() && !model.moreMatches());
        QCOMPARE(model.rowCount(), 7);
        QTRY_VERIFY(!gitRunning());
    }

    // A page ends right after the match that fills it, though git printed
    // more commits in the same batch: the next page starts at the commit
    // after that match, so its first row is the match that came next —
    // neither lost with the rest of the batch nor the page's last one again.
    void aSearchPageEndsRightAfterItsLastMatch()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QStringList newestFirst = {"hit A", "miss 1", "hit B", "hit C", "hit D",
                                         "miss 2", "hit E", "miss 3", "hit F", "hit G"};
        QList<ImportedCommit> imported;
        for (auto it = newestFirst.crbegin(); it != newestFirst.crend(); ++it)
            imported.append({*it});
        QVERIFY(importHistory(dir.path(), imported));
        // Git flushes after every commit it prints into a pipe; told not
        // to, it hands over a history this small in one piece, one batch.
        const ScopedEnv oneBatch("GIT_FLUSH", "0");
        GitRepo repo(dir.path());
        HistoryModel model(&repo);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setBatchSize(3);
        model.reload();
        const auto subjects = [&model] {
            QStringList out;
            for (int row = 0; row < model.rowCount(); ++row)
                out << model.commit(row).subject;
            return out;
        };

        QSignalSpy inserts(&model, &QAbstractItemModel::rowsInserted);
        model.setFilter(QStringLiteral("hit"));
        QTRY_VERIFY(!model.searching());
        QVERIFY(model.moreMatches());
        QCOMPARE(subjects(), QStringList({"hit A", "hit B", "hit C"}));
        QCOMPARE(inserts.count(), 1); // the page's three out of the one batch
        QVERIFY(model.loadMore());
        QTRY_VERIFY(!model.searching());
        QVERIFY(model.moreMatches());
        QCOMPARE(subjects(), QStringList({"hit A", "hit B", "hit C", "hit D", "hit E", "hit F"}));
        QVERIFY(model.loadMore());
        QTRY_VERIFY(!model.searching());
        QVERIFY(!model.moreMatches());
        QCOMPARE(subjects(), QStringList({"hit A", "hit B", "hit C", "hit D", "hit E", "hit F", "hit G"}));
        QTRY_VERIFY(repo.findChildren<QProcess *>().isEmpty());
    }

    // A second page, of the search or of the log, has git write its
    // commit-graph where the repository has none, so that the pages after
    // it walk the commits they skip instead of sorting the whole history
    // first; a first page never does, a repository is asked once, and a
    // graph that is there already is left alone.
    void aSecondPageWritesTheCommitGraph()
    {
        const auto graphFile = [](const QString &dir) {
            for (const char *path : {"/.git/objects/info/commit-graph", "/.git/objects/info/commit-graphs/commit-graph-chain"})
                if (QFileInfo(dir + QLatin1String(path)).isFile())
                    return dir + QLatin1String(path);
            return QString();
        };
        QList<ImportedCommit> imported;
        for (int i = 1; i <= 30; ++i)
            imported.append({QStringLiteral("commit %1").arg(i)});
        QTemporaryDir searched, logged, graphed;
        QVERIFY(searched.isValid() && logged.isValid() && graphed.isValid());
        QVERIFY(importHistory(searched.path(), imported));
        QVERIFY(importHistory(logged.path(), imported));
        QVERIFY(importHistory(graphed.path(), imported));
        QVERIFY(graphFile(searched.path()).isEmpty());
        QVERIFY(graphFile(logged.path()).isEmpty());
        QVERIFY(git(graphed.path(), {"commit-graph", "write", "--reachable"}));
        const QString existing = graphFile(graphed.path());
        QVERIFY(!existing.isEmpty());
        {
            // A day long gone, so that a graph written again could not keep it.
            QFile file(existing);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QVERIFY(file.setFileTime(QDateTime(QDate(2024, 1, 1), QTime(0, 0), QTimeZone::UTC),
                                     QFileDevice::FileModificationTime));
        }
        const QDateTime written = QFileInfo(existing).lastModified();

        GitRepo repo(searched.path());
        const auto gitRunning = [&repo] { return !repo.findChildren<QProcess *>().isEmpty(); };
        HistoryModel model(&repo);
        model.setBatchSize(5);
        model.reload();
        QCOMPARE(model.rowCount(), 5);
        QVERIFY(!gitRunning());

        // The search: its first page leaves the repository as it is, its
        // second has the graph written while the page runs.
        model.setFilter(QStringLiteral("commit"));
        QTRY_VERIFY(!model.searching());
        QVERIFY(model.moreMatches());
        QTRY_VERIFY(!gitRunning());
        QVERIFY(graphFile(searched.path()).isEmpty());
        QVERIFY(model.loadMore());
        QTRY_VERIFY(!graphFile(searched.path()).isEmpty());
        QTRY_VERIFY(!model.searching());
        QCOMPARE(model.rowCount(), 10);
        QTRY_VERIFY(!gitRunning());

        // Another repository is asked afresh. The log: its first batch
        // leaves it as it is, its second has the graph written, and a
        // third asks no more.
        repo.setRoot(logged.path());
        model.setFilter(QString());
        model.reload();
        QCOMPARE(model.rowCount(), 5);
        QVERIFY(!gitRunning());
        QVERIFY(model.loadMore());
        QCOMPARE(model.rowCount(), 10);
        QTRY_VERIFY(!graphFile(logged.path()).isEmpty());
        QTRY_VERIFY(!gitRunning());
        QVERIFY(QFile::remove(graphFile(logged.path())));
        QVERIFY(model.loadMore());
        QCOMPARE(model.rowCount(), 15);
        QVERIFY(!gitRunning());
        QVERIFY(graphFile(logged.path()).isEmpty());

        // A graph there already: asked about, and left alone.
        repo.setRoot(graphed.path());
        model.reload();
        QCOMPARE(model.rowCount(), 15); // as many as were loaded before
        QVERIFY(!gitRunning());
        QVERIFY(model.loadMore());
        QVERIFY(gitRunning()); // the question
        QTRY_VERIFY(!gitRunning());
        QCOMPARE(graphFile(graphed.path()), existing);
        QCOMPARE(QFileInfo(existing).lastModified(), written);
    }

    // Every page of a search walks the history the search started from: a
    // commit on HEAD, or with All branches a new branch's, that comes between
    // two pages before any reload has seen it neither shows up in the later
    // pages nor shifts them. The pages together are the matches of the start
    // points the search began with, none twice, none missing. Without a
    // commit there is nowhere to start, and nothing matches — no HEAD walked
    // in its place.
    void everyPageOfASearchWalksTheSameHistory()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QList<ImportedCommit> imported;
        for (int i = 1; i <= 40; ++i)
            imported.append({(i % 2 ? QStringLiteral("hit %1") : QStringLiteral("miss %1")).arg(i)});
        QVERIFY(importHistory(dir.path(), imported));
        GitRepo repo(dir.path());
        HistoryModel model(&repo);
        QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setBatchSize(4);
        model.reload();
        const auto hashes = [&model] {
            QStringList out;
            for (int row = 0; row < model.rowCount(); ++row)
                out << model.commit(row).hash;
            return out;
        };
        const auto unpaged = [&repo](bool allRefs) {
            QStringList out;
            for (const Commit &c : repo.log(repo.logStartPoints(allRefs), 0, 100000))
                if (commitMatches(c, QStringLiteral("hit")))
                    out << c.hash;
            return out;
        };
        const auto readToTheEnd = [&model] {
            for (int pages = 0; model.moreMatches() && pages < 50; ++pages) {
                QVERIFY(model.loadMore());
                QTRY_VERIFY(!model.searching());
            }
            QVERIFY(!model.moreMatches() && !model.searchFailed());
        };

        // HEAD moves on by a match, the newest commit of all.
        const QStringList onHead = unpaged(false);
        QCOMPARE(onHead.size(), 20);
        model.setFilter(QStringLiteral("hit"));
        QTRY_VERIFY(!model.searching());
        QCOMPARE(hashes(), onHead.first(4));
        QVERIFY(commit(dir.path(), QStringLiteral("hit on top"), 0));
        readToTheEnd();
        QCOMPARE(hashes(), onHead);

        // All branches: a new branch, its tip the newest commit of all.
        model.setFilter(QString());
        model.setAllRefs(true);
        const QStringList everywhere = unpaged(true);
        QCOMPARE(everywhere.size(), 21);
        model.setFilter(QStringLiteral("hit"));
        QTRY_VERIFY(!model.searching());
        QCOMPARE(hashes(), everywhere.first(4));
        QVERIFY(git(dir.path(), {"checkout", "-q", "-b", "side", "main~6"}));
        QVERIFY(commit(dir.path(), QStringLiteral("hit on the side"), 0));
        QVERIFY(git(dir.path(), {"checkout", "-q", "main"}));
        readToTheEnd();
        QCOMPARE(hashes(), everywhere);
        QTRY_VERIFY(repo.findChildren<QProcess *>().isEmpty());

        // No commit yet: no match, and nothing failed.
        QTemporaryDir empty;
        QVERIFY(empty.isValid());
        QVERIFY(git(empty.path(), {"init", "-q", "-b", "main"}));
        repo.setRoot(empty.path());
        model.reload();
        for (const bool all : {true, false}) {
            model.setAllRefs(all);
            QVERIFY(!model.searching());
            QVERIFY(!model.searchFailed() && !model.moreMatches());
            QCOMPARE(model.rowCount(), 0);
        }
        QVERIFY(repo.findChildren<QProcess *>().isEmpty());
    }

    // The loaded commits' batches, the same way: a commit on HEAD, HEAD
    // moved back, or with All branches a new branch, that comes between two
    // batches before any reload has seen it neither shows up in the later
    // batches nor shifts them — none twice, none missing. The next reload
    // reads the refs as they are. Without a commit, nothing is loaded and
    // nothing failed.
    void everyBatchOfTheLogWalksTheSameHistory()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QList<ImportedCommit> imported;
        for (int i = 1; i <= 12; ++i)
            imported.append({QStringLiteral("commit %1").arg(i)});
        QVERIFY(importHistory(dir.path(), imported));
        GitRepo repo(dir.path());
        const auto hashes = [](const HistoryModel &model) {
            QStringList out;
            for (int row = 0; row < model.rowCount(); ++row)
                out << model.commit(row).hash;
            return out;
        };
        const auto whole = [&repo](bool allRefs) {
            QStringList out;
            for (const Commit &c : repo.log(repo.logStartPoints(allRefs), 0, 100000))
                out << c.hash;
            return out;
        };
        const auto readToTheEnd = [](HistoryModel &model) {
            for (int batches = 0; !model.exhausted() && batches < 50; ++batches)
                model.loadMore();
            QVERIFY(model.exhausted() && !model.failed());
        };

        // HEAD moves on by a commit, the newest of all.
        {
            HistoryModel model(&repo);
            QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
            model.setBatchSize(4);
            model.reload();
            const QStringList onHead = whole(false);
            QCOMPARE(onHead.size(), 12);
            QCOMPARE(hashes(model), onHead.first(4));
            QVERIFY(commit(dir.path(), QStringLiteral("on top"), 0));
            readToTheEnd(model);
            QCOMPARE(hashes(model), onHead);
            QCOMPARE(model.laneCount(), 1);
            QVERIFY(model.reload());
            QCOMPARE(model.commit(0).subject, QStringLiteral("on top"));

            // HEAD back by two, with the oldest commit still to load.
            QCOMPARE(model.rowCount(), 12);
            QVERIFY(!model.exhausted());
            const QStringList before = whole(false);
            QCOMPARE(before.size(), 13);
            QVERIFY(git(dir.path(), {"reset", "-q", "--soft", "HEAD~2"}));
            readToTheEnd(model);
            QCOMPARE(hashes(model), before);
        }

        // All branches: a new branch off the middle of main.
        {
            HistoryModel model(&repo);
            QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
            model.setBatchSize(4);
            model.setAllRefs(true);
            const QStringList everywhere = whole(true);
            QCOMPARE(everywhere.size(), 11); // main, two back
            QCOMPARE(hashes(model), everywhere.first(4));
            QVERIFY(git(dir.path(), {"branch", "side", "main~6"}));
            QVERIFY(git(dir.path(), {"checkout", "-q", "side"}));
            QVERIFY(commit(dir.path(), QStringLiteral("on the side"), 0));
            QVERIFY(git(dir.path(), {"checkout", "-q", "main"}));
            readToTheEnd(model);
            QCOMPARE(hashes(model), everywhere);
            QVERIFY(model.reload());
            QCOMPARE(model.commit(0).subject, QStringLiteral("on the side"));
        }

        // No commit yet: nothing to load, and nothing failed.
        QTemporaryDir empty;
        QVERIFY(empty.isValid());
        QVERIFY(git(empty.path(), {"init", "-q", "-b", "main"}));
        repo.setRoot(empty.path());
        HistoryModel model(&repo);
        model.reload();
        for (const bool all : {false, true}) {
            model.setAllRefs(all);
            QCOMPARE(model.rowCount(), 0);
            QVERIFY(model.exhausted() && !model.failed());
            QVERIFY(!model.loadMore());
        }
    }

    // The graph column (screens.js commitsTable()): lanes 12 px apart, the
    // first half a pitch left of the middle of the class's design width, so
    // two lanes sit symmetric in it and the first never moves as lanes come
    // and go; a third lane widens the column past the design's width.
    void theHistoryGraphFollowsTheDesignsGeometry()
    {
        struct Case {
            WidthClass widthClass;
            int design, firstLane;
        };
        for (const Case c : {Case{WidthClass::Wide, 40, 14}, Case{WidthClass::Large, 40, 14},
                             Case{WidthClass::Medium, 36, 12}, Case{WidthClass::Stacked, 32, 10}}) {
            for (int lanes = 1; lanes <= 3; ++lanes) {
                const HistoryView::GraphGeometry g = HistoryView::graphGeometry(c.widthClass, lanes);
                const QByteArray where = QStringLiteral("%1 px, %2 lanes").arg(c.design).arg(lanes).toUtf8();
                QVERIFY2(g.laneCentre(0) == ui::space(c.firstLane), where.constData());
                QVERIFY2(g.laneCentre(1) == ui::space(c.firstLane) + ui::space(12), where.constData());
                // The design's width, or as wide as the lanes need with the
                // first lane's room on either side: at a 12 px base that is
                // 40, 40, 52 for the wide classes' one, two and three lanes.
                const int width = qMax(ui::space(c.design), 2 * ui::space(c.firstLane) + (lanes - 1) * ui::space(12));
                QVERIFY2(g.width == width, where.constData());
                if (lanes == 3)
                    QVERIFY2(g.width > ui::space(c.design), where.constData());
            }
            // Two lanes, symmetric in the design's width (to the rounding of
            // the text size of the moment).
            const HistoryView::GraphGeometry two = HistoryView::graphGeometry(c.widthClass, 2);
            QVERIFY(qAbs(two.laneCentre(0) - (two.width - two.laneCentre(1))) <= 1);
        }
        // Twelve lanes at the most.
        QCOMPARE(HistoryView::graphGeometry(WidthClass::Wide, 40).width,
                 HistoryView::graphGeometry(WidthClass::Wide, 12).width);
        QCOMPARE(HistoryView::graphGeometry(WidthClass::Wide, 12).width, 2 * ui::space(14) + 11 * ui::space(12));
    }

    // The details card (screens.js commitDetails()): the subject, the meta
    // line (no date stacked), the parents in words, every ref the commit
    // wears and the body alone; the copy button puts the full SHA on the
    // clipboard; stacked, the files button counts the commit's files, and a
    // commit without any has none.
    void theHistoryDetailsCardDescribesTheCommit()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path();
        const QDir root(path);
        QVERIFY(git(path, {"init", "-q", "-b", "main"}));
        QVERIFY(writeFixture(root.filePath(QStringLiteral("a.txt")), "a\n"));
        QVERIFY(git(path, {"add", "-A"}));
        QVERIFY(git(path, {"commit", "-q", "-m", "root"}, 1));
        QVERIFY(git(path, {"checkout", "-q", "-b", "feature"}));
        QVERIFY(writeFixture(root.filePath(QStringLiteral("b.txt")), "b\n"));
        QVERIFY(writeFixture(root.filePath(QStringLiteral("c.txt")), "c\n"));
        QVERIFY(git(path, {"add", "-A"}));
        QVERIFY(git(path, {"commit", "-q", "-m", "side", "-m", "The body, alone.\n\nIts second paragraph."}, 2));
        QVERIFY(git(path, {"checkout", "-q", "main"}));
        QVERIFY(commit(path, QStringLiteral("empty"), 3));
        QVERIFY(git(path, {"merge", "-q", "--no-ff", "feature", "-m", "merge"}, 4));
        QVERIFY(git(path, {"tag", "v1"}));
        QVERIFY(git(path, {"update-ref", "refs/remotes/origin/main", "HEAD"}));

        GitRepo repo(path);
        HistoryView history(&repo);
        history.resize(ui::space(560), ui::space(800));
        history.show();
        QVERIFY(QTest::qWaitForWindowExposed(&history));
        history.reload();
        settle();
        CommitDetails *card = history.details();
        QVERIFY(card->isVisible());
        QTableView *commits = history.commitsTable();
        const auto select = [&](const QString &subject) {
            for (int row = 0; row < commits->model()->rowCount(); ++row) {
                commits->selectRow(row);
                bool ok = false;
                const Commit c = history.currentCommit(&ok);
                if (ok && c.subject == subject)
                    return c;
            }
            return Commit();
        };
        const auto date = [](const Commit &c) { return c.date.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")); };
        const auto names = [card] {
            QStringList out;
            for (const RefLabel &label : card->refs())
                out << label.name;
            return out;
        };

        // The merge: two parents, and every ref, the remote one too.
        const Commit merge = select(QStringLiteral("merge"));
        QVERIFY(merge.isValid());
        QCOMPARE(card->title(), QStringLiteral("merge"));
        QCOMPARE(card->metaParts(), QStringList({merge.shortHash, QStringLiteral("Test <test@example.com>"), date(merge)}));
        QCOMPARE(card->parentsText(), QStringLiteral("Parents %1 %2")
                                          .arg(merge.parents.at(0).left(merge.shortHash.size()),
                                               merge.parents.at(1).left(merge.shortHash.size())));
        QCOMPARE(names(), QStringList({"main", "origin/main", "v1"}));
        QCOMPARE(card->body()->toPlainText(), QString()); // a one-line message has no body
        card->copyButton()->click();
        QCOMPARE(QApplication::clipboard()->text(), merge.hash);
        QCOMPARE(card->copyButton()->accessibleName(), QStringLiteral("Copy full SHA"));

        // One parent, and the body without the subject.
        const Commit side = select(QStringLiteral("side"));
        QVERIFY(side.isValid());
        QCOMPARE(card->parentsText(), QStringLiteral("Parent %1").arg(side.parents.first().left(side.shortHash.size())));
        QCOMPARE(names(), QStringList({"feature"}));
        QCOMPARE(card->body()->toPlainText(), QStringLiteral("The body, alone.\n\nIts second paragraph."));

        // None.
        QVERIFY(select(QStringLiteral("root")).isValid());
        QCOMPARE(card->parentsText(), QStringLiteral("Root commit"));
        QVERIFY(card->refs().isEmpty());

        // Only stacked: the files button, and no date on the meta line.
        QVERIFY(!card->filesButton()->isVisible());
        history.setStacked(true);
        settle();
        const Commit rootCommit = select(QStringLiteral("root"));
        QCOMPARE(card->metaParts(), QStringList({rootCommit.shortHash, QStringLiteral("Test <test@example.com>")}));
        QVERIFY(card->filesButton()->isVisible());
        QCOMPARE(card->filesButton()->text(), QStringLiteral("1 file ›"));
        // A 24 px ghost button, its text 8 in, 8 in from the card's corner
        // (its text on the card's 12 of padding).
        QCOMPARE(card->filesButton()->width(), card->filesButton()->fontMetrics().horizontalAdvance(QStringLiteral("1 file ›"))
                                                   + 2 * ui::space(ui::pad::control));
        QCOMPARE(card->filesButton()->height(), ui::space(ui::box::row));
        const QRect button = card->filesButton()->geometry();
        QCOMPARE(card->width() - (button.right() + 1), ui::space(8));
        QCOMPARE(card->height() - (button.bottom() + 1), ui::space(8));
        select(QStringLiteral("side"));
        QCOMPARE(card->filesButton()->text(), QStringLiteral("2 files ›"));
        QSignalSpy requests(&history, &HistoryView::filesRequested);
        QTest::mouseClick(card->filesButton(), Qt::LeftButton);
        QCOMPARE(requests.count(), 1);
        select(QStringLiteral("empty"));
        QVERIFY(!card->filesButton()->isVisible());
        history.setStacked(false);
        settle();
        QVERIFY(!card->filesButton()->isVisible());

        // No commit at all: the lines empty, the view's message in the body
        // once the search is done.
        history.filterField()->setText(QStringLiteral("no such commit"));
        QTRY_VERIFY(!commits->currentIndex().isValid());
        QVERIFY(card->title().isEmpty());
        QVERIFY(card->metaParts().isEmpty());
        QVERIFY(card->parentsText().isEmpty());
        QTRY_COMPARE(card->body()->placeholderText(), QStringLiteral("No commits match the filter."));
        QCOMPARE(card->body()->placeholderText(), history.emptyMessage());
    }

    // The filter says as much as its width holds, and its magnifier stays
    // where it is whatever is typed; the text starts after it.
    void theHistoryFilterFitsItsPlaceholder()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        f.window->setMode(MainWindow::HistoryMode);
        settle();
        auto *history = f.window->findChild<HistoryView *>();
        QLineEdit *field = history->filterField();
        QWidget *magnifier = field->findChild<QWidget *>(QStringLiteral("filterIcon"));
        QVERIFY(magnifier);
        QCOMPARE(field->height(), ui::space(28));
        // The view's width less the field's is what the buttons take.
        const int buttons = history->width() - field->width();
        const auto at = [&](int fieldWidth) {
            history->resize(buttons + fieldWidth, history->height());
            QCoreApplication::processEvents();
            return field->placeholderText();
        };
        QCOMPARE(at(ui::space(180)), QStringLiteral("Filter"));
        QCOMPARE(at(ui::space(250)), QStringLiteral("Filter commits"));
        QCOMPARE(at(ui::space(360)), QStringLiteral("Filter by message, author or SHA"));
        QCOMPARE(field->width(), ui::space(360));

        QVERIFY(magnifier->isVisible());
        const int box = ui::space(ui::box::icon);
        QCOMPARE(magnifier->geometry(), QRect(ui::space(ui::pad::control), (field->height() - box) / 2, box, box));
        // The clear glyph is there only while the field holds text, in the
        // same box at the other end.
        QWidget *clear = field->findChild<QWidget *>(QStringLiteral("filterClear"));
        QVERIFY(clear);
        QVERIFY(!clear->isVisible());
        field->setFocus();
        QTest::keyClicks(field, QStringLiteral("merge"));
        QCOMPARE(field->text(), QStringLiteral("merge"));
        QVERIFY(magnifier->isVisible());
        QVERIFY(clear->isVisible());
        QCOMPARE(clear->geometry(),
                 QRect(field->width() - ui::space(ui::pad::control) - box, (field->height() - box) / 2, box, box));
        // The text keeps clear of both glyphs' boxes.
        QVERIFY(field->textMargins().left() > 0);
        QVERIFY(field->textMargins().right() > 0);
        // A click on it empties the field and leaves the focus in it.
        QTest::mouseClick(clear, Qt::LeftButton);
        QVERIFY(field->text().isEmpty());
        QVERIFY(!clear->isVisible());
        QVERIFY(field->hasFocus());
    }

    // The filter searches the whole history, not the commits loaded so far:
    // of 600, 500 are loaded and the one match is the oldest. The count row
    // says how far the search is; the selection follows the commit that was
    // current where it turns up; a new filter drops the search before it;
    // clearing the filter brings the 500 back with their graph. There is no
    // Load more anywhere: scrolling loads.
    void theHistoryFilterSearchesTheWholeHistory()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QList<ImportedCommit> imported;
        for (int i = 1; i <= 600; ++i)
            imported.append({QStringLiteral("commit %1").arg(i)});
        imported.first().message = QStringLiteral("The needle, oldest of all");
        QVERIFY(importHistory(dir.path(), imported));

        GitRepo repo(dir.path());
        HistoryView history(&repo);
        history.resize(ui::space(945), ui::space(800));
        history.show();
        QVERIFY(QTest::qWaitForWindowExposed(&history));
        history.reload();
        settle();
        QTableView *table = history.commitsTable();
        auto *model = qobject_cast<HistoryModel *>(table->model());
        QVERIFY(model);
        auto *count = history.findChild<QLabel *>(QStringLiteral("historyCount"));
        QVERIFY(count);
        QCOMPARE(model->rowCount(), 500);
        QCOMPARE(count->text(), QStringLiteral("500 commits loaded"));
        QCOMPARE(count->height(), ui::space(ui::box::row));
        QVERIFY(!table->isColumnHidden(HistoryModel::Graph));
        for (QAbstractButton *button : history.findChildren<QAbstractButton *>())
            QVERIFY2(!button->text().contains(QStringLiteral("Load more")) && !button->toolTip().contains(QStringLiteral("500")),
                     qPrintable(button->text()));

        // Every text the count row wears while filtering is one of these.
        const QRegularExpression countText(
            QStringLiteral("^Searching… (1 match|\\d+ matches)$|^(1 match|\\d+ matches)( loaded)?$|^No matches$"));
        QStringList counts;
        connect(model, &HistoryModel::searchChanged, &history, [&] {
            if (model->filtering())
                counts << count->text();
        });
        const auto filter = [&](const QString &text) {
            history.filterField()->setText(text);
            // The debounce's slot, now, so what it shows before any match is in can be seen.
            QVERIFY(QMetaObject::invokeMethod(&history, "onFilterChanged"));
        };
        const auto current = [&history] {
            bool ok = false;
            const Commit c = history.currentCommit(&ok);
            return ok ? c.subject : QString();
        };
        const auto everyRowMatches = [model](const QString &text) {
            for (int row = 0; row < model->rowCount(); ++row)
                if (!commitMatches(model->commit(row), text))
                    return false;
            return true;
        };
        QCOMPARE(current(), QStringLiteral("commit 600"));

        // The one match, beyond the loaded commits; the commit that was
        // current is not among the matches, so the first one is selected.
        filter(QStringLiteral("NEEDLE"));
        QVERIFY(model->searching());
        QCOMPARE(count->text(), QStringLiteral("Searching… 0 matches"));
        QCOMPARE(history.emptyMessage(), QStringLiteral("Searching…"));
        QCOMPARE(history.details()->body()->placeholderText(), QStringLiteral("Searching…"));
        QVERIFY(table->isColumnHidden(HistoryModel::Graph));
        QTRY_VERIFY(!model->searching());
        QCOMPARE(model->rowCount(), 1);
        QCOMPARE(count->text(), QStringLiteral("1 match"));
        QCOMPARE(current(), QStringLiteral("The needle, oldest of all"));

        // None at all.
        filter(QStringLiteral("no such commit"));
        QTRY_VERIFY(!model->searching());
        QCOMPARE(model->rowCount(), 0);
        QCOMPARE(count->text(), QStringLiteral("No matches"));
        QCOMPARE(history.emptyMessage(), QStringLiteral("No commits match the filter."));
        QCOMPARE(history.details()->body()->placeholderText(), QStringLiteral("No commits match the filter."));

        // Cleared: the 500 loaded commits and their graph; the commit last
        // current is not among them, so the list starts at the top.
        filter(QString());
        QVERIFY(!model->filtering());
        QCOMPARE(model->rowCount(), 500);
        QVERIFY(!table->isColumnHidden(HistoryModel::Graph));
        QVERIFY(table->columnWidth(HistoryModel::Graph) > 0);
        QCOMPARE(count->text(), QStringLiteral("500 commits loaded"));
        QCOMPARE(current(), QStringLiteral("commit 600"));

        // A commit the matches have is selected again when it turns up, and
        // stays selected, in view, once the filter is cleared.
        table->selectRow(600 - 555);
        QCOMPARE(current(), QStringLiteral("commit 555"));
        filter(QStringLiteral("commit 5"));
        QTRY_VERIFY(!model->searching());
        QCOMPARE(model->rowCount(), 1 + 10 + 100); // 5, 50–59, 500–599
        QVERIFY(everyRowMatches(QStringLiteral("commit 5")));
        QCOMPARE(count->text(), QStringLiteral("111 matches"));
        QCOMPARE(current(), QStringLiteral("commit 555"));
        filter(QString());
        QCOMPARE(current(), QStringLiteral("commit 555"));
        QVERIFY(table->viewport()->rect().intersects(table->visualRect(table->currentIndex())));

        // A new filter while a search runs: only its own matches arrive,
        // nothing of the search it replaced — started a moment ago, or with
        // its first commits in already.
        filter(QStringLiteral("commit"));
        filter(QStringLiteral("needle"));
        QTRY_VERIFY(!model->searching());
        QTest::qWait(100);
        QCOMPARE(model->rowCount(), 1);
        QCOMPARE(model->commit(0).subject, QStringLiteral("The needle, oldest of all"));
        filter(QStringLiteral("commit"));
        QTRY_VERIFY(model->rowCount() > 0);
        filter(QStringLiteral("commit 60"));
        QTRY_VERIFY(!model->searching());
        QTest::qWait(100);
        QCOMPARE(model->rowCount(), 2); // 60 and 600
        QVERIFY(everyRowMatches(QStringLiteral("commit 60")));
        QCOMPARE(count->text(), QStringLiteral("2 matches"));

        QVERIFY(!counts.isEmpty());
        for (const QString &text : counts)
            QVERIFY2(countText.match(text).hasMatch(), qPrintable(text));
    }

    // A filter's matches come 500 at a time, like the commits: where a page
    // stopped full the count row says "N matches loaded", scrolling the list
    // to its end loads the next page ("Searching… N matches" while it runs)
    // below the rows there, the one the user picked staying current, and the
    // last page says "N matches"; the numbers grouped the locale's way. At
    // the end, scrolling loads nothing more.
    void scrollingLoadsTheNextPageOfMatches()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QList<ImportedCommit> imported;
        for (int i = 1; i <= 1005; ++i)
            imported.append({QStringLiteral("commit %1").arg(i)});
        QVERIFY(importHistory(dir.path(), imported));
        const QLocale locale;
        QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
        const auto restoreLocale = qScopeGuard([locale] { QLocale::setDefault(locale); });

        GitRepo repo(dir.path());
        HistoryView history(&repo);
        history.resize(ui::space(945), ui::space(800));
        history.show();
        QVERIFY(QTest::qWaitForWindowExposed(&history));
        history.reload();
        settle();
        QTableView *table = history.commitsTable();
        auto *model = qobject_cast<HistoryModel *>(table->model());
        QVERIFY(model);
        auto *count = history.findChild<QLabel *>(QStringLiteral("historyCount"));
        QVERIFY(count);
        QCOMPARE(count->text(), QStringLiteral("500 commits loaded"));
        QStringList counts;
        connect(model, &HistoryModel::searchChanged, &history, [&] { counts << count->text(); });
        const auto current = [&history] {
            bool ok = false;
            const Commit c = history.currentCommit(&ok);
            return ok ? c.subject : QString();
        };

        history.filterField()->setText(QStringLiteral("commit"));
        QVERIFY(QMetaObject::invokeMethod(&history, "onFilterChanged"));
        QTRY_VERIFY(!model->searching());
        QVERIFY(model->moreMatches());
        QCOMPARE(model->rowCount(), 500);
        QCOMPARE(count->text(), QStringLiteral("500 matches loaded"));
        QCOMPARE(current(), QStringLiteral("commit 1005"));
        table->selectRow(1005 - 990);
        QCOMPARE(current(), QStringLiteral("commit 990"));

        QScrollBar *bar = table->verticalScrollBar();
        const auto scrollToTheEnd = [&] {
            QVERIFY(bar->maximum() > 0);
            counts.clear();
            bar->setValue(bar->maximum());
        };
        scrollToTheEnd();
        QTRY_VERIFY(model->rowCount() > 500 && !model->searching());
        QVERIFY(counts.contains(QStringLiteral("Searching… 500 matches")));
        QCOMPARE(model->rowCount(), 1000);
        QCOMPARE(count->text(), QStringLiteral("1,000 matches loaded"));
        QCOMPARE(current(), QStringLiteral("commit 990"));

        scrollToTheEnd();
        QTRY_VERIFY(model->rowCount() > 1000 && !model->searching());
        QVERIFY(counts.contains(QStringLiteral("Searching… 1,000 matches")));
        QVERIFY(!model->moreMatches());
        QCOMPARE(model->rowCount(), 1005);
        QCOMPARE(count->text(), QStringLiteral("1,005 matches"));
        QCOMPARE(table->currentIndex().row(), 1005 - 990);
        for (int row = 0; row < model->rowCount(); ++row)
            QCOMPARE(model->commit(row).subject, QStringLiteral("commit %1").arg(1005 - row));

        scrollToTheEnd();
        QTest::qWait(100);
        QVERIFY(!model->searching());
        QCOMPARE(model->rowCount(), 1005);
        QCOMPARE(count->text(), QStringLiteral("1,005 matches"));
        QTRY_VERIFY(repo.findChildren<QProcess *>().isEmpty()); // the commit-graph the second page asked for
    }

    // A refresh whose search starts over (a ref moved) brings the list back
    // as it was: as many matches as the pages had, and past them for the
    // commit that was current where new matches pushed it down; that commit
    // current again with its file, and the offsets of the commit list and of
    // the files table put back. Until it is back, the card and the files go
    // on showing it. A second refresh before the list is back keeps what the
    // first one put aside; one where nothing moved leaves nothing behind to
    // be put back later.
    void aRefreshBringsTheSearchsListBack()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QList<ImportedCommit> imported;
        for (int i = 1; i <= 40; ++i)
            imported.append({QStringLiteral("hit %1").arg(i)});
        for (int i = 1; i <= 30; ++i)
            imported[29].files << QStringLiteral("file%1.txt").arg(i, 2, 10, QLatin1Char('0'));
        QVERIFY(importHistory(dir.path(), imported));

        GitRepo repo(dir.path());
        HistoryView history(&repo);
        history.resize(ui::space(945), ui::space(560));
        history.show();
        QVERIFY(QTest::qWaitForWindowExposed(&history));
        QTableView *table = history.commitsTable();
        QTableView *files = history.filesTable();
        auto *model = qobject_cast<HistoryModel *>(table->model());
        QVERIFY(model);
        model->setBatchSize(5);
        history.reload();
        settle();
        history.filterField()->setText(QStringLiteral("hit"));
        QVERIFY(QMetaObject::invokeMethod(&history, "onFilterChanged"));
        QTRY_VERIFY(!model->searching());
        for (int page = 2; page <= 3; ++page) {
            QVERIFY(model->loadMore());
            QTRY_VERIFY(!model->searching());
        }
        QCOMPARE(model->rowCount(), 15);
        const auto current = [&history] {
            bool ok = false;
            const Commit c = history.currentCommit(&ok);
            return ok ? c.subject : QString();
        };
        const auto currentFile = [&history] {
            Commit c;
            FileChange f;
            return history.currentFile(&c, &f) ? f.path : QString();
        };

        // "hit 30" on the third page, its twentieth file, both lists
        // scrolled off the rows they show.
        table->selectRow(10);
        QCOMPARE(current(), QStringLiteral("hit 30"));
        QCOMPARE(files->model()->rowCount(), 30);
        files->selectRow(19);
        QCOMPARE(currentFile(), QStringLiteral("file20.txt"));
        QScrollBar *commitsBar = table->verticalScrollBar();
        QScrollBar *filesBar = files->verticalScrollBar();
        QVERIFY(commitsBar->maximum() >= 4 && filesBar->maximum() >= 7);
        commitsBar->setValue(4);
        filesBar->setValue(7);
        settle();
        QCOMPARE(commitsBar->value(), 4);
        QCOMPARE(filesBar->value(), 7);

        // A ref moves: the search starts over, and while it looks for the
        // commit the card and the files stay. A second ref moves and a second
        // refresh comes before anything is back.
        QVERIFY(git(dir.path(), {"branch", "moved", "main~3"}));
        history.reload();
        QVERIFY(model->searching());
        QCOMPARE(model->rowCount(), 0);
        QCOMPARE(history.details()->title(), QStringLiteral("hit 30"));
        QCOMPARE(files->model()->rowCount(), 30);
        QCOMPARE(files->currentIndex().row(), 19);
        QVERIFY(git(dir.path(), {"tag", "again", "main~7"}));
        history.reload();
        QVERIFY(model->searching());
        QCOMPARE(history.details()->title(), QStringLiteral("hit 30"));
        QTRY_VERIFY(!model->searching());
        QCOMPARE(model->rowCount(), 15);
        QVERIFY(model->moreMatches());
        QCOMPARE(current(), QStringLiteral("hit 30"));
        QCOMPARE(currentFile(), QStringLiteral("file20.txt"));
        QCOMPARE(commitsBar->value(), 4);
        QCOMPARE(filesBar->value(), 7);

        // Six new matches on top push the commit past the fifteen: the page
        // goes on until it is in, and ends there.
        for (int i = 1; i <= 6; ++i)
            QVERIFY(commit(dir.path(), QStringLiteral("hit new %1").arg(i), 0));
        history.reload();
        QTRY_VERIFY(!model->searching());
        QCOMPARE(model->rowCount(), 6 + 11);
        QCOMPARE(model->commit(16).subject, QStringLiteral("hit 30"));
        QVERIFY(model->moreMatches());
        QCOMPARE(current(), QStringLiteral("hit 30"));
        QCOMPARE(currentFile(), QStringLiteral("file20.txt"));
        QCOMPARE(commitsBar->value(), 4);
        QCOMPARE(filesBar->value(), 7);

        // Nothing moved: nothing starts over, and nothing is put back when
        // the next page comes in.
        QSignalSpy resets(model, &QAbstractItemModel::modelReset);
        history.reload();
        QCOMPARE(resets.count(), 0);
        commitsBar->setValue(2);
        filesBar->setValue(3);
        QVERIFY(QMetaObject::invokeMethod(&history, "loadMore"));
        QTRY_VERIFY(!model->searching());
        QCOMPARE(model->rowCount(), 17 + 5);
        QCOMPARE(commitsBar->value(), 2);
        QCOMPARE(filesBar->value(), 3);
        QCOMPARE(current(), QStringLiteral("hit 30"));
        QCOMPARE(currentFile(), QStringLiteral("file20.txt"));
        QTRY_VERIFY(repo.findChildren<QProcess *>().isEmpty());
    }

    // A refresh's search goes on past the matches the list had for the
    // commit that was current a batch of matches at most: one that is gone
    // (amended away here) ends the page there, full with more to load, and
    // the first match is current, the card with it. The next page goes on
    // right after it: the pages are the history's matches, none twice, none
    // missing.
    void aRefreshGivesUpOnACommitThatIsGone()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // Every match 4 KB long: git's pipe holds 64 KB, so the refresh's
        // page comes in pieces, whose matches count together.
        const QString filler(4000, QLatin1Char('x'));
        QList<ImportedCommit> imported;
        for (int i = 1; i <= 60; ++i)
            imported.append({QStringLiteral("hit %1\n\n%2").arg(i).arg(filler)});
        QVERIFY(importHistory(dir.path(), imported));

        GitRepo repo(dir.path());
        HistoryView history(&repo);
        history.resize(ui::space(945), ui::space(560));
        history.show();
        QVERIFY(QTest::qWaitForWindowExposed(&history));
        QTableView *table = history.commitsTable();
        auto *model = qobject_cast<HistoryModel *>(table->model());
        QVERIFY(model);
        model->setBatchSize(5);
        history.reload();
        settle();
        history.filterField()->setText(QStringLiteral("hit"));
        QVERIFY(QMetaObject::invokeMethod(&history, "onFilterChanged"));
        QTRY_VERIFY(!model->searching());
        for (int page = 2; page <= 3; ++page) {
            QVERIFY(model->loadMore());
            QTRY_VERIFY(!model->searching());
        }
        QCOMPARE(model->rowCount(), 15);
        const auto current = [&history] {
            bool ok = false;
            const Commit c = history.currentCommit(&ok);
            return ok ? c.subject : QString();
        };
        const auto hashes = [model] {
            QStringList out;
            for (int row = 0; row < model->rowCount(); ++row)
                out << model->commit(row).hash;
            return out;
        };
        QCOMPARE(current(), QStringLiteral("hit 60"));

        // The current commit amended: the refresh's search looks for it in
        // vain through the fifteen and five more, and stops there.
        QVERIFY(git(dir.path(), {"commit", "--amend", "--allow-empty", "-q", "-m", "hit 60, amended"}));
        history.reload();
        QVERIFY(model->searching());
        QCOMPARE(history.details()->title(), QStringLiteral("hit 60"));
        QTRY_VERIFY(!model->searching());
        QCOMPARE(model->rowCount(), 15 + 5);
        QVERIFY(model->moreMatches());
        QVERIFY(!model->searchFailed());
        QCOMPARE(table->currentIndex().row(), 0);
        QCOMPARE(current(), QStringLiteral("hit 60, amended"));
        QCOMPARE(history.details()->title(), QStringLiteral("hit 60, amended"));
        QTRY_VERIFY(repo.findChildren<QProcess *>().isEmpty());

        // The pages after it.
        QStringList unpaged;
        for (const Commit &c : repo.log(repo.logStartPoints(false), 0, 1000))
            if (commitMatches(c, QStringLiteral("hit")))
                unpaged << c.hash;
        QCOMPARE(unpaged.size(), 60);
        QVERIFY(model->loadMore());
        QTRY_VERIFY(!model->searching());
        QCOMPARE(hashes(), unpaged.first(25));
        for (int pages = 0; model->moreMatches() && pages < 20; ++pages) {
            QVERIFY(model->loadMore());
            QTRY_VERIFY(!model->searching());
        }
        QVERIFY(!model->moreMatches() && !model->searchFailed());
        QCOMPARE(hashes(), unpaged);
        QCOMPARE(current(), QStringLiteral("hit 60, amended"));
        QTRY_VERIFY(repo.findChildren<QProcess *>().isEmpty());
    }

    // The user scrolling the commit list while a refresh brings it back —
    // the commit back already, the rows still coming in — leaves it where
    // they scrolled it: the offset the refresh put aside is not put back
    // once the rows are in.
    void theUsersScrollingOutlastsARefresh()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // Every match 8 KB long: git's pipe holds 64 KB, so the refresh's
        // forty come in pieces of eight at most, and the list has more rows
        // than it shows before the last.
        const QString filler(8000, QLatin1Char('x'));
        QList<ImportedCommit> imported;
        for (int i = 1; i <= 60; ++i)
            imported.append({QStringLiteral("hit %1\n\n%2").arg(i).arg(filler)});
        QVERIFY(importHistory(dir.path(), imported));

        GitRepo repo(dir.path());
        HistoryView history(&repo);
        history.resize(ui::space(945), ui::space(560));
        history.show();
        QVERIFY(QTest::qWaitForWindowExposed(&history));
        QTableView *table = history.commitsTable();
        auto *model = qobject_cast<HistoryModel *>(table->model());
        QVERIFY(model);
        model->setBatchSize(5);
        history.reload();
        settle();
        history.filterField()->setText(QStringLiteral("hit"));
        QVERIFY(QMetaObject::invokeMethod(&history, "onFilterChanged"));
        QTRY_VERIFY(!model->searching());
        while (model->rowCount() < 40) {
            QVERIFY(model->loadMore());
            QTRY_VERIFY(!model->searching());
        }
        QCOMPARE(model->rowCount(), 40);
        const auto currentHash = [&history] {
            bool ok = false;
            const Commit c = history.currentCommit(&ok);
            return ok ? c.hash : QString();
        };

        // The third match current, the list scrolled well past it.
        table->selectRow(2);
        const QString kept = currentHash();
        QCOMPARE(model->commit(2).hash, kept);
        QScrollBar *bar = table->verticalScrollBar();
        QVERIFY(bar->maximum() >= 20);
        bar->setValue(20);
        settle();
        QCOMPARE(bar->value(), 20);

        // A ref moves. The commit is back with the first rows, and the user
        // scrolls by a step while the others are still coming.
        int scrolledTo = -1;
        const QMetaObject::Connection scroller = connect(model, &HistoryModel::searchChanged, &history, [&] {
            if (scrolledTo >= 0 || !model->searching() || currentHash() != kept)
                return;
            table->doItemsLayout(); // the range of the rows in so far
            if (bar->maximum() == 0)
                return;
            bar->triggerAction(QAbstractSlider::SliderSingleStepAdd);
            scrolledTo = bar->value();
        });
        QVERIFY(git(dir.path(), {"branch", "moved", "main~3"}));
        history.reload();
        QTRY_VERIFY(!model->searching());
        disconnect(scroller);
        QVERIFY(scrolledTo > 0);
        QVERIFY(scrolledTo != 20);
        QCOMPARE(model->rowCount(), 40);
        QVERIFY(model->moreMatches());
        QCOMPARE(currentHash(), kept);
        settle();
        QCOMPARE(bar->value(), scrolledTo);
        QTRY_VERIFY(repo.findChildren<QProcess *>().isEmpty());
    }

    // The user scrolling the commit list before a refresh's search has
    // brought the commit back leaves it where they scrolled it: the commit is
    // current again, with its file, and the list does not move to it. A
    // second refresh meanwhile that starts the search over puts the list back
    // where the user had it, not where the first refresh found it; one where
    // nothing moved changes nothing of it, and the commit coming back does
    // not move the list either.
    void theUsersScrollingOutlastsTheCommitComingBack_data()
    {
        QTest::addColumn<bool>("again");
        QTest::addColumn<bool>("moved");
        QTest::newRow("one refresh") << false << false;
        QTest::newRow("a second, a ref moved") << true << true;
        QTest::newRow("a second, nothing moved") << true << false;
    }

    void theUsersScrollingOutlastsTheCommitComingBack()
    {
        QFETCH(bool, again);
        QFETCH(bool, moved);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // Every match 8 KB long: git's pipe holds 64 KB, so the refresh's
        // forty come in pieces of eight at most, and the list has more rows
        // than it shows pieces before the thirty-first, the commit, is in.
        const QString filler(8000, QLatin1Char('x'));
        QList<ImportedCommit> imported;
        for (int i = 1; i <= 60; ++i)
            imported.append({QStringLiteral("hit %1\n\n%2").arg(i).arg(filler)});
        imported[29].files = {QStringLiteral("a.txt"), QStringLiteral("b.txt")};
        QVERIFY(importHistory(dir.path(), imported));

        GitRepo repo(dir.path());
        HistoryView history(&repo);
        history.resize(ui::space(945), ui::space(560));
        history.show();
        QVERIFY(QTest::qWaitForWindowExposed(&history));
        QTableView *table = history.commitsTable();
        QTableView *files = history.filesTable();
        auto *model = qobject_cast<HistoryModel *>(table->model());
        QVERIFY(model);
        model->setBatchSize(5);
        history.reload();
        settle();
        history.filterField()->setText(QStringLiteral("hit"));
        QVERIFY(QMetaObject::invokeMethod(&history, "onFilterChanged"));
        QTRY_VERIFY(!model->searching());
        while (model->rowCount() < 40) {
            QVERIFY(model->loadMore());
            QTRY_VERIFY(!model->searching());
        }
        QCOMPARE(model->rowCount(), 40);
        const auto currentHash = [&history] {
            bool ok = false;
            const Commit c = history.currentCommit(&ok);
            return ok ? c.hash : QString();
        };
        const auto currentFile = [&history] {
            Commit c;
            FileChange f;
            return history.currentFile(&c, &f) ? f.path : QString();
        };

        // "hit 30" current with its second file, the list scrolled to it.
        table->selectRow(30);
        QCOMPARE(model->commit(30).subject, QStringLiteral("hit 30"));
        const QString kept = currentHash();
        QCOMPARE(files->model()->rowCount(), 2);
        files->selectRow(1);
        const QString file = currentFile();
        QVERIFY(!file.isEmpty());
        QScrollBar *bar = table->verticalScrollBar();
        QVERIFY(bar->maximum() >= 25);
        bar->setValue(25);
        settle();
        QCOMPARE(bar->value(), 25);

        // A ref moves. Before the commit is back the user scrolls the list by
        // a step, then refreshes again where the row says so.
        int scrolledTo = -1, backAt = -1;
        const QMetaObject::Connection user = connect(model, &HistoryModel::searchChanged, &history, [&] {
            if (currentHash() == kept) {
                if (backAt < 0)
                    backAt = bar->value();
                return;
            }
            if (scrolledTo >= 0 || !model->searching())
                return;
            table->doItemsLayout(); // the range of the rows in so far
            if (bar->maximum() == 0)
                return;
            bar->triggerAction(QAbstractSlider::SliderSingleStepAdd);
            scrolledTo = bar->value();
            if (!again)
                return;
            if (moved)
                QVERIFY(git(dir.path(), {"tag", "again", "main~7"}));
            history.reload();
        });
        QVERIFY(git(dir.path(), {"branch", "moved", "main~3"}));
        history.reload();
        QTRY_VERIFY(!model->searching());
        disconnect(user);
        QVERIFY(scrolledTo > 0);
        QVERIFY(backAt >= 0);
        QCOMPARE(model->rowCount(), 40);
        QVERIFY(model->moreMatches());
        QCOMPARE(currentHash(), kept);
        QCOMPARE(currentFile(), file);
        // Back where the user had the list; only a second refresh's reset
        // moved it meanwhile, and that one's end put it back.
        if (!(again && moved))
            QCOMPARE(backAt, scrolledTo);
        settle();
        QCOMPARE(bar->value(), scrolledTo);
        QVERIFY(!table->viewport()->rect().intersects(table->visualRect(table->currentIndex())));
        QTRY_VERIFY(repo.findChildren<QProcess *>().isEmpty());
    }

    // A search git fails says so: "Search failed" with nothing found, "N
    // matches · search failed" under what it did find, which stays, and
    // "The search failed." in the card and the diff where no commit is
    // current. A failure has nothing more to load, and is never "nothing
    // moved": a refresh starts the search over. Git fails here for want of
    // an old commit, which a walk in date order reads before it prints
    // anything.
    void aFailedSearchSaysSoAndARefreshTriesAgain()
    {
        QTemporaryDir dir, aside;
        QVERIFY(dir.isValid() && aside.isValid());
        QList<ImportedCommit> imported;
        for (int i = 1; i <= 12; ++i)
            imported.append({QStringLiteral("hit %1").arg(i)});
        QVERIFY(importHistory(dir.path(), imported));
        GitRepo repo(dir.path());
        const QString oldest = repo.log(repo.logStartPoints(false), 0, 100).last().hash;
        const QString object = QDir(dir.path()).filePath(QStringLiteral(".git/objects/%1/%2").arg(oldest.left(2), oldest.mid(2)));
        const QString hidden = QDir(aside.path()).filePath(QStringLiteral("object"));
        QVERIFY(QFileInfo(object).isFile());
        const auto hide = [&] { QVERIFY(QFile::rename(object, hidden)); };
        // Once git is done: the commit-graph a second page asks for fails
        // for want of the commit too, where one written later would let git
        // print the newer commits before it came to the missing one.
        const auto restore = [&] {
            QTRY_VERIFY(repo.findChildren<QProcess *>().isEmpty());
            QVERIFY(QFile::rename(hidden, object));
        };

        HistoryView history(&repo);
        history.resize(ui::space(945), ui::space(800));
        history.show();
        QVERIFY(QTest::qWaitForWindowExposed(&history));
        QTableView *table = history.commitsTable();
        auto *model = qobject_cast<HistoryModel *>(table->model());
        QVERIFY(model);
        model->setBatchSize(3);
        history.reload();
        settle();
        auto *count = history.findChild<QLabel *>(QStringLiteral("historyCount"));
        QVERIFY(count);
        const auto current = [&history] {
            bool ok = false;
            const Commit c = history.currentCommit(&ok);
            return ok ? c.subject : QString();
        };

        // Before any match.
        hide();
        history.filterField()->setText(QStringLiteral("hit"));
        QVERIFY(QMetaObject::invokeMethod(&history, "onFilterChanged"));
        QTRY_VERIFY(!model->searching());
        QVERIFY(model->searchFailed());
        QVERIFY(!model->moreMatches());
        QVERIFY(!model->loadMore());
        QCOMPARE(model->rowCount(), 0);
        QCOMPARE(count->text(), QStringLiteral("Search failed"));
        QCOMPARE(history.emptyMessage(), QStringLiteral("The search failed."));
        QCOMPARE(history.details()->body()->placeholderText(), QStringLiteral("The search failed."));
        history.reload(); // the repository as it was: git fails again
        QTRY_VERIFY(!model->searching());
        QVERIFY(model->searchFailed());
        QCOMPARE(count->text(), QStringLiteral("Search failed"));
        restore();
        history.reload();
        QTRY_VERIFY(!model->searching());
        QVERIFY(!model->searchFailed());
        QCOMPARE(model->rowCount(), 3);
        QCOMPARE(count->text(), QStringLiteral("3 matches loaded"));
        QCOMPARE(current(), QStringLiteral("hit 12"));

        // After some: the next page fails, the rows and the one the user
        // picked stay, and scrolling to the end asks for nothing more.
        model->setBatchSize(1);
        history.filterField()->setText(QStringLiteral("hit 1"));
        QVERIFY(QMetaObject::invokeMethod(&history, "onFilterChanged"));
        QTRY_VERIFY(!model->searching());
        QCOMPARE(model->rowCount(), 1);
        QCOMPARE(current(), QStringLiteral("hit 12"));
        hide();
        QVERIFY(model->loadMore());
        QTRY_VERIFY(!model->searching());
        QVERIFY(model->searchFailed());
        QVERIFY(!model->moreMatches());
        QCOMPARE(model->rowCount(), 1);
        QCOMPARE(count->text(), QStringLiteral("1 match · search failed"));
        QCOMPARE(current(), QStringLiteral("hit 12"));
        restore();
        history.reload();
        QTRY_VERIFY(!model->searching());
        QVERIFY(!model->searchFailed() && model->moreMatches());
        QVERIFY(model->loadMore());
        QTRY_VERIFY(!model->searching());
        QCOMPARE(model->rowCount(), 2);
        table->selectRow(1);
        QCOMPARE(current(), QStringLiteral("hit 11"));
        hide();
        QVERIFY(model->loadMore());
        QTRY_VERIFY(!model->searching());
        QVERIFY(model->searchFailed());
        QCOMPARE(model->rowCount(), 2);
        QCOMPARE(count->text(), QStringLiteral("2 matches · search failed"));
        QCOMPARE(current(), QStringLiteral("hit 11"));
        QScrollBar *bar = table->verticalScrollBar();
        bar->setValue(bar->maximum());
        QTest::qWait(100);
        QVERIFY(!model->searching());
        QCOMPARE(model->rowCount(), 2);
        restore();
        history.reload();
        QTRY_VERIFY(!model->searching());
        QVERIFY(!model->searchFailed());
        QCOMPARE(model->rowCount(), 2);
        QCOMPARE(count->text(), QStringLiteral("2 matches loaded"));
        QCOMPARE(current(), QStringLiteral("hit 11"));
        QTRY_VERIFY(repo.findChildren<QProcess *>().isEmpty());
    }

    // A refresh whose search starts over leaves the history's diff alone:
    // while the search looks for the commit, and once it is back — on the
    // first page, or on one further down — the diff is the same file
    // scrolled to the same place, and the files keep their selection.
    void aRefreshKeepsTheSearchsDiffWhereItWas_data()
    {
        QTest::addColumn<int>("row");
        QTest::newRow("first page") << 1;
        QTest::newRow("third page") << 7;
    }

    void aRefreshKeepsTheSearchsDiffWhereItWas()
    {
        QFETCH(int, row);
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        settle();
        MainWindow *w = f.window.get();
        auto *history = w->findChild<HistoryView *>();
        QTableView *table = history->commitsTable();
        auto *model = qobject_cast<HistoryModel *>(table->model());
        QVERIFY(model);
        model->setBatchSize(3);
        // Twelve matches, each changing every fifth line of a long file: the
        // diffs have somewhere to scroll to.
        const QString path = f.dir->path();
        for (int i = 1; i <= 12; ++i) {
            QByteArray text;
            for (int line = 0; line < 400; ++line)
                text += (line % 5 ? QByteArray("line ") : "v" + QByteArray::number(i) + ' ') + QByteArray::number(line) + '\n';
            QVERIFY(writeFixture(QDir(path).filePath(QStringLiteral("long.txt")), text));
            QVERIFY(git(path, {"add", "long.txt"}));
            QVERIFY(git(path, {"commit", "-q", "-m", QStringLiteral("hit %1").arg(i)}, i + 1));
        }
        w->refresh();
        w->setMode(MainWindow::HistoryMode);
        settle();
        history->filterField()->setText(QStringLiteral("hit"));
        QVERIFY(QMetaObject::invokeMethod(history, "onFilterChanged"));
        QTRY_VERIFY(!model->searching());
        while (model->rowCount() <= row) {
            QVERIFY(model->loadMore());
            QTRY_VERIFY(!model->searching());
        }
        const QString selected = model->commit(row).hash;
        table->selectRow(row);
        settle();
        const auto currentHash = [history] {
            bool ok = false;
            const Commit c = history->currentCommit(&ok);
            return ok ? c.hash : QString();
        };
        const auto files = [history] {
            const QModelIndex current = history->filesTable()->currentIndex();
            return QStringLiteral("%1 of %2, %3")
                .arg(current.row())
                .arg(history->filesTable()->model()->rowCount())
                .arg(current.data().toString());
        };
        const auto diff = [&f] {
            const DiffView::ViewState d = f.diff()->viewState();
            return QStringLiteral("%1,%2,%3 of %4 lines").arg(d.row).arg(d.column).arg(d.block).arg(f.diff()->document().lines.size());
        };
        QCOMPARE(currentHash(), selected);
        DiffView::ViewState at;
        at.row = 120;
        at.block = 1;
        f.diff()->restoreViewState(at);
        settle();
        const QString diffBefore = diff(), filesBefore = files();
        QVERIFY2(diffBefore.startsWith(QLatin1String("120,")), qPrintable(diffBefore));
        QVERIFY2(filesBefore.startsWith(QLatin1String("0 of 1")), qPrintable(filesBefore));

        QVERIFY(git(path, {"branch", "moved", "HEAD~2"}));
        w->refresh();
        QVERIFY(model->searching());
        QVERIFY(currentHash().isEmpty());
        QCOMPARE(diff(), diffBefore);
        QCOMPARE(files(), filesBefore);
        QTRY_VERIFY(!model->searching() && currentHash() == selected);
        settle();
        QCOMPARE(diff(), diffBefore);
        QCOMPARE(files(), filesBefore);

        // Back from the Changes page while the search looks for the commit:
        // the diff is at once the file the card and the files still show,
        // the commit's own, and stays so once it is back.
        w->setMode(MainWindow::CommitMode);
        settle();
        QVERIFY(git(path, {"branch", "moved-again", "HEAD~4"}));
        w->refresh();
        w->setMode(MainWindow::HistoryMode);
        QVERIFY(model->searching());
        QVERIFY(currentHash().isEmpty());
        QCOMPARE(files(), filesBefore);
        const QString diffShown = diff();
        QVERIFY2(diffShown.endsWith(QLatin1String(" of 480 lines")), qPrintable(diffShown));
        QTRY_VERIFY(!model->searching() && currentHash() == selected);
        settle();
        QCOMPARE(files(), filesBefore);
        QCOMPARE(diff(), diffShown);
    }

    // While a refresh's search brings the commit back, the files it keeps
    // showing are still that commit's: a file the user picks among them
    // shows its diff at once, the rail naming the commit, and once the
    // commit is back that file is the one selected, its diff where the user
    // left it.
    void aFilePickedWhileTheCommitComesBackIsTheUsers()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        settle();
        MainWindow *w = f.window.get();
        auto *history = w->findChild<HistoryView *>();
        QTableView *table = history->commitsTable();
        QTableView *files = history->filesTable();
        auto *model = qobject_cast<HistoryModel *>(table->model());
        QVERIFY(model);
        model->setBatchSize(3);
        // Twelve matches, each changing every fifth line of two long files,
        // the lines naming the file and the version.
        const QString path = f.dir->path();
        for (int i = 1; i <= 12; ++i) {
            for (const char *name : {"one", "two"}) {
                QByteArray text;
                for (int line = 0; line < 400; ++line)
                    text += name + (line % 5 ? QByteArray(" line ") : " v" + QByteArray::number(i) + ' ')
                        + QByteArray::number(line) + '\n';
                QVERIFY(writeFixture(QDir(path).filePath(QString::fromLatin1(name) + QStringLiteral(".txt")), text));
            }
            QVERIFY(git(path, {"add", "one.txt", "two.txt"}));
            QVERIFY(git(path, {"commit", "-q", "-m", QStringLiteral("hit %1").arg(i)}, i + 1));
        }
        w->refresh();
        w->setMode(MainWindow::HistoryMode);
        settle();
        history->filterField()->setText(QStringLiteral("hit"));
        QVERIFY(QMetaObject::invokeMethod(history, "onFilterChanged"));
        QTRY_VERIFY(!model->searching());
        while (model->rowCount() <= 7) {
            QVERIFY(model->loadMore());
            QTRY_VERIFY(!model->searching());
        }
        // "hit 5", on the third page.
        const Commit selected = model->commit(7);
        QCOMPARE(selected.subject, QStringLiteral("hit 5"));
        table->selectRow(7);
        settle();
        const auto currentHash = [history] {
            bool ok = false;
            const Commit c = history->currentCommit(&ok);
            return ok ? c.hash : QString();
        };
        const auto fileAt = [files](int row) {
            return files->model()->index(row, ChangesModel::Name).data().toString();
        };
        // The first line the diff adds, and where the diff is.
        const auto diff = [&f] {
            const DiffView::ViewState d = f.diff()->viewState();
            QString added;
            for (const DiffLine &line : f.diff()->document().lines)
                if (line.state == DiffLine::Added) {
                    added = line.text;
                    break;
                }
            return QStringLiteral("%1 at %2,%3,%4").arg(added).arg(d.row).arg(d.column).arg(d.block);
        };
        const auto railCommit = [&f] {
            for (QLabel *label : f.rail()->findChildren<QLabel *>(QStringLiteral("dimLabel")))
                if (!label->toolTip().isEmpty())
                    return label->toolTip().section(QLatin1Char('\n'), 0, 0);
            return QString();
        };
        QCOMPARE(currentHash(), selected.hash);
        QCOMPARE(files->model()->rowCount(), 2);
        const int picked = 1 - files->currentIndex().row();
        const QString pickedName = fileAt(picked);
        QVERIFY(diff().startsWith(fileAt(files->currentIndex().row()).chopped(4) + QLatin1String(" v5 0 at ")));
        QCOMPARE(railCommit(), selected.shortHash);

        // A ref moves and the search starts over; before the commit is back
        // the user picks the other file and scrolls its diff.
        QVERIFY(git(path, {"branch", "moved", "HEAD~2"}));
        w->refresh();
        QVERIFY(model->searching());
        QVERIFY(currentHash().isEmpty());
        files->selectRow(picked);
        QCOMPARE(fileAt(files->currentIndex().row()), pickedName);
        QVERIFY2(diff().startsWith(pickedName.chopped(4) + QLatin1String(" v5 0 at ")), qPrintable(diff()));
        QCOMPARE(railCommit(), selected.shortHash);
        DiffView::ViewState at;
        at.row = 120;
        at.block = 1;
        f.diff()->restoreViewState(at);
        const QString diffPicked = diff();
        QVERIFY2(diffPicked.contains(QLatin1String(" at 120,")), qPrintable(diffPicked));
        QVERIFY(model->searching());

        QTRY_VERIFY(!model->searching() && currentHash() == selected.hash);
        settle();
        QCOMPARE(fileAt(files->currentIndex().row()), pickedName);
        QCOMPARE(diff(), diffPicked);
        Commit c;
        FileChange file;
        QVERIFY(history->currentFile(&c, &file));
        QCOMPARE(c.hash, selected.hash);
        QCOMPARE(file.path, pickedName);
        QCOMPARE(railCommit(), selected.shortHash);
    }

    // Two commits whose files compare equal — the same file, the same
    // status, as many lines in and out, the same size — are still two
    // diffs: selecting one after the other shows the other's.
    void anotherCommitWithEqualFilesShowsItsOwnDiff()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        settle();
        MainWindow *w = f.window.get();
        auto *history = w->findChild<HistoryView *>();
        const QString path = f.dir->path();
        int hour = 2;
        for (const QString &version : {QStringLiteral("1.0.0"), QStringLiteral("1.0.1"), QStringLiteral("1.0.2")}) {
            QVERIFY(writeFixture(QDir(path).filePath(QStringLiteral("version.txt")), version.toUtf8() + '\n'));
            QVERIFY(git(path, {"add", "version.txt"}));
            QVERIFY(git(path, {"commit", "-q", "-m", QStringLiteral("Version %1").arg(version)}, hour++));
        }
        w->refresh();
        w->setMode(MainWindow::HistoryMode);
        settle();
        const auto current = [history] {
            bool ok = false;
            const Commit c = history->currentCommit(&ok);
            return ok ? c.subject : QString();
        };
        const auto added = [&f] {
            QStringList out;
            for (const DiffLine &line : f.diff()->document().lines)
                if (line.state == DiffLine::Added)
                    out << line.text;
            return out;
        };
        QCOMPARE(current(), QStringLiteral("Version 1.0.2"));
        QCOMPARE(added(), QStringList({"1.0.2"}));
        history->commitsTable()->selectRow(1);
        QCOMPARE(current(), QStringLiteral("Version 1.0.1"));
        QCOMPARE(added(), QStringList({"1.0.1"}));
        history->commitsTable()->selectRow(0);
        QCOMPARE(added(), QStringList({"1.0.2"}));
    }
};

UI_TEST(HistoryTest);

#include "history_test.moc"
