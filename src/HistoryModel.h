#pragma once

#include "GitRepo.h"

#include <QAbstractTableModel>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QStringList>
#include <QVector>

class QProcess;

// One line segment of the commit graph inside a row. Lane indices count from
// the left; -1 means the segment starts/ends at this row's own commit node.
struct GraphEdge {
    int from = -1; // lane at the top edge of the row, -1 = starts at the node
    int to = -1;   // lane at the bottom edge of the row, -1 = ends at the node
    int color = 0; // index into the graph palette
};

struct GraphRow {
    int lane = 0;
    int color = 0;
    QVector<GraphEdge> edges;
};

// The history filter's rule: the subject, the body, the author's name or
// e-mail contain `text`, or the full SHA starts with it, all without regard
// to case. `text` is the filter as typed, trimmed.
bool commitMatches(const Commit &c, const QString &text);

// Commits of `git log`, loaded in batches, with a lane layout for the graph
// column that is computed incrementally as batches are appended. With a
// filter set, the rows are the commits it matches out of the whole history
// instead, as `git log` walks it; the loaded commits wait unchanged for the
// filter to be cleared. The matches come in pages of a batch each, like the
// commits, so that a broad filter on a huge repository neither holds its
// whole history nor has git walk it all at once.
class HistoryModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column { Graph, Message, Author, Date, Hash, ColumnCount };

    explicit HistoryModel(GitRepo *repo, QObject *parent = nullptr);
    ~HistoryModel() override;

    // Re-reads refs and the commits (at least as many as were loaded before),
    // and returns whether it did: false where nothing moved (and nothing
    // failed), the rows as they were. Filtering, a moved ref starts the
    // search over, and the loaded commits are read again only once the
    // filter is cleared. The new first page takes as many matches as the
    // pages had before, or `rows` where that is more (a refresh while the
    // last one's are still coming in asks for what that one had), and goes
    // on past them until the commit `keep` is among them or the walk ends:
    // the list the user was looking at comes back whole. It goes on for a
    // batch of matches at most, though: a `keep` that is gone (amended,
    // rebased, its branch deleted) ends the page there, with more to load.
    bool reload(bool force = false, const QString &keep = QString(), int rows = 0); // force: even if no ref moved
    // Appends the next batch: the log's next commits, of the history the
    // last read walked (however the refs moved since), or, filtering, starts
    // the search's next page where there is one (moreMatches()) and none is
    // running. Returns whether it loaded (started) anything: false when the
    // history is exhausted. A second batch of either list has git write its
    // commit-graph where the repository has none, once per repository.
    bool loadMore();
    // How many commits a batch of the log holds, and how many matches a page
    // of the search: 500 to begin with. For the tests.
    void setBatchSize(int size) { m_batch = qMax(1, size); }
    int batchSize() const { return m_batch; }
    // The loaded commits' state, whether or not a filter shows them.
    bool exhausted() const { return m_exhausted; }
    bool failed() const { return m_failed; }

    void setAllRefs(bool on);
    bool allRefs() const { return m_allRefs; }

    // A non-empty filter empties the list and starts the search, whose
    // matches (commitMatches()) are appended batch by batch while git is
    // still walking, up to a page of them; an empty one drops the search and
    // shows the loaded commits again. The same text again changes nothing.
    void setFilter(const QString &text);
    QString filter() const { return m_filter; }
    bool filtering() const { return !m_filter.isEmpty(); }
    // True while git walks the history for a page of the filter's matches.
    bool searching() const { return m_searching; }
    // True where a page stopped full, git's walk abandoned with more history
    // to go: loadMore() goes on from there. False while a page runs, and
    // once a walk reached the end or failed.
    bool moreMatches() const { return m_moreMatches; }
    // True where git failed the last page (or could not say where the search
    // starts): the matches found before stay, and there is nothing more to
    // load until a reload starts the search over. False again as soon as a
    // search or a page starts.
    bool searchFailed() const { return m_searchFailed; }

    // The rows below are the matches while filtering, the loaded commits
    // otherwise; the graph is the loaded commits' alone (its column is not
    // shown while filtering).
    const Commit &commit(int row) const { return shown().at(row); }
    const GraphRow &graph(int row) const { return m_rows.at(row); }
    QList<RefLabel> labels(int row) const { return m_refs.value(shown().at(row).hash); }
    bool isHead(int row) const { return shown().at(row).hash == m_head; }
    // The row of the commit `hash`, -1 where the list does not have it.
    int rowOf(const QString &hash) const;
    int laneCount() const { return m_maxLanes; }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

signals:
    // A search or its next page started, took in a batch of commits, ended
    // (full, with more to load, or at the end of the history), or was
    // dropped with the repository it was for.
    void searchChanged();

private:
    const QList<Commit> &shown() const { return filtering() ? m_matches : m_commits; }
    // Reads the first batches again, as many commits as were loaded before.
    void readLog();
    void append(const QList<Commit> &commits);
    void layoutRow(const Commit &commit);
    int allocateLane(const QString &hash);
    // reload() with the room its restarted search's first page has.
    bool reread(bool force, int room, const QString &keep);
    // Shows `filter` from scratch: the matches of a new search, the first
    // page with room for `room` of them and for `keep`, or the loaded
    // commits for an empty one (read again first where they are stale).
    void startOver(const QString &filter, int room, const QString &keep = QString());
    // A page of matches, from m_walked commits into the walk on, taking
    // m_room of them (and going on for m_until, a batch more at most).
    void runSearch();
    void stopSearch();
    // A batch past the first is under way: git's commit-graph, once.
    void askForCommitGraph();
    void onRootChanged();

    GitRepo *m_repo;
    QList<Commit> m_commits;
    QVector<GraphRow> m_rows;
    QHash<QString, QList<RefLabel>> m_refs;
    QString m_head;
    bool m_allRefs = false;
    bool m_loaded = false;
    bool m_exhausted = false;
    bool m_failed = false;
    bool m_logStale = false; // a ref moved while filtering: m_commits is read again on the way back
    QStringList m_logScope; // the commits m_commits' walk starts from, fixed by readLog(): every batch walks the same history
    bool m_commitGraphAsked = false; // ensureCommitGraph() asked for this repository already
    int m_batch = 500;

    QString m_filter;
    QList<Commit> m_matches;
    QStringList m_scope; // the commits the search walks from, fixed when it started: every page walks the same history
    QPointer<QProcess> m_search; // git's walk for a page of m_filter's matches, while it runs
    bool m_searching = false;
    bool m_moreMatches = false;
    bool m_searchFailed = false;
    int m_walked = 0;    // commits of the walk the pages so far took in: the next page skips them
    int m_room = 0;      // matches the running page still takes
    QString m_until;     // a commit the running page goes on for, past its room, until it is in
    int m_overrun = 0;   // the matches past its room the page still takes for m_until before it gives up on it
    int m_searchRun = 0; // bumped by stopSearch(): callbacks of a search stopped meanwhile ignore themselves

    // Graph state carried from one row to the next: which commit each lane is
    // waiting for, and the colour of the line travelling in it.
    QStringList m_lanes;
    QVector<int> m_laneColors;
    int m_nextColor = 0;
    int m_maxLanes = 0;
};
