#include "HistoryModel.h"
#include "ProcessUtil.h"

bool commitMatches(const Commit &c, const QString &text)
{
    return c.subject.contains(text, Qt::CaseInsensitive) || c.body.contains(text, Qt::CaseInsensitive)
        || c.author.contains(text, Qt::CaseInsensitive) || c.email.contains(text, Qt::CaseInsensitive)
        || c.hash.startsWith(text, Qt::CaseInsensitive);
}

HistoryModel::HistoryModel(GitRepo *repo, QObject *parent)
    : QAbstractTableModel(parent), m_repo(repo)
{
    if (m_repo)
        connect(m_repo, &GitRepo::rootChanged, this, &HistoryModel::onRootChanged);
}

HistoryModel::~HistoryModel()
{
    stopSearch();
}

bool HistoryModel::reload(bool force, const QString &keep, int rows)
{
    return reread(force, qMax(qMax(m_batch, rows), int(m_matches.size())), keep);
}

// A search that failed is never "nothing moved": a refresh is how it is tried
// again.
bool HistoryModel::reread(bool force, int room, const QString &keep)
{
    const QHash<QString, QList<RefLabel>> refs = m_repo->refs();
    int code = 0;
    QString head = QString::fromUtf8(m_repo->run({QStringLiteral("rev-parse"), QStringLiteral("HEAD")}, &code)).trimmed();
    if (code != 0)
        head.clear();
    if (!force && m_loaded && !m_failed && !(filtering() && m_searchFailed) && refs == m_refs && head == m_head)
        return false; // nothing moved: keep the rows, the selection and the scroll position
    m_loaded = true;
    if (filtering()) {
        // The matches wear the refs of the moment; the loaded commits, out of
        // sight, are read again when the filter is cleared.
        m_refs = refs;
        m_head = head;
        m_logStale = true;
        startOver(m_filter, room, keep);
        return true;
    }
    beginResetModel();
    m_refs = refs;
    m_head = head;
    readLog();
    endResetModel();
    return true;
}

void HistoryModel::readLog()
{
    const int wanted = qMax(m_batch, m_commits.size());
    m_commits.clear();
    m_rows.clear();
    m_lanes.clear();
    m_laneColors.clear();
    m_nextColor = 0;
    m_maxLanes = 0;
    m_exhausted = false;
    m_failed = false;
    m_logStale = false;
    // The walk's start points, read here once for this read and every
    // loadMore() after it: a ref that moves in between (a commit or a fetch
    // no reload has noticed yet) must not shift the commits the next batch
    // skips. The next reload reads them again.
    bool ok = false;
    m_logScope = m_repo->logStartPoints(m_allRefs, &ok);
    const QList<Commit> commits = ok ? m_repo->log(m_logScope, 0, wanted, &ok) : QList<Commit>();
    m_failed = !ok;
    for (const Commit &c : commits) {
        m_commits.append(c);
        layoutRow(c);
    }
    m_exhausted = commits.size() < wanted;
}

bool HistoryModel::loadMore()
{
    if (filtering()) {
        if (!m_moreMatches || m_searching)
            return false;
        askForCommitGraph();
        m_moreMatches = false;
        m_searchFailed = false;
        m_searching = true;
        m_room = m_batch;
        m_until.clear();
        runSearch();
        emit searchChanged();
        return true;
    }
    if (m_exhausted || m_failed)
        return false;
    if (m_commits.size() >= m_batch)
        askForCommitGraph();
    bool ok = false;
    const QList<Commit> commits = m_repo->log(m_logScope, m_commits.size(), m_batch, &ok);
    if (!ok) {
        m_failed = true;
        return false;
    }
    if (commits.size() < m_batch)
        m_exhausted = true;
    append(commits);
    return !commits.isEmpty();
}

// Filtering, the new scope is a new search: a page of it, as for a new text.
void HistoryModel::setAllRefs(bool on)
{
    if (m_allRefs == on)
        return;
    m_allRefs = on;
    reread(true, m_batch, QString());
}

void HistoryModel::setFilter(const QString &text)
{
    if (text != m_filter)
        startOver(text, m_batch);
}

int HistoryModel::rowOf(const QString &hash) const
{
    const QList<Commit> &list = shown();
    for (int row = 0; row < list.size(); ++row)
        if (list.at(row).hash == hash)
            return row;
    return -1;
}

// The search's start points are read here, once, for all its pages: a ref
// that moves while the user reads (a commit, a fetch, before any reload
// notices) must not shift the walk the next page skips into. None at all (no
// commits yet) is no match, not HEAD's history.
void HistoryModel::startOver(const QString &filter, int room, const QString &keep)
{
    stopSearch();
    bool resolved = true;
    const QStringList scope = filter.isEmpty() ? QStringList() : m_repo->logStartPoints(m_allRefs, &resolved);
    beginResetModel();
    m_filter = filter;
    m_matches.clear();
    m_scope = scope;
    m_searchFailed = !resolved;
    m_searching = !m_scope.isEmpty();
    m_moreMatches = false;
    m_walked = 0;
    m_room = room;
    m_until = keep;
    if (!filtering() && m_logStale)
        readLog();
    endResetModel();
    if (m_searching)
        runSearch();
    emit searchChanged();
}

// One `git log` over the search's start points, every commit it prints held
// against the filter as it comes: git cannot do the matching itself, as it
// ANDs a --grep with an --author where the filter means either. The batch
// that fills the page keeps what fits and ends the walk there, with more to
// load; the commits of that batch after its last match are left to the next
// page, whose walk starts right after it. The pages together are the matches
// of one walk to the end: the same commits in the same order. A page that
// goes on for m_until ends right after it where it comes within a batch of
// matches past the room; where it does not (a commit that is gone), the page
// ends a batch past its room, the batches git printed meanwhile counted
// together, instead of walking the whole history for it.
void HistoryModel::runSearch()
{
    const QString text = m_filter;
    const int run = m_searchRun;
    m_overrun = m_until.isEmpty() ? 0 : m_batch;
    QProcess *process = m_repo->logStream(
        m_scope, m_walked, this,
        [this, text, run](const QList<Commit> &commits) {
            if (run != m_searchRun)
                return;
            QList<Commit> found;
            int walked = 0;
            for (const Commit &c : commits) {
                ++walked;
                if (!commitMatches(c, text))
                    continue;
                found.append(c);
                if (c.hash == m_until)
                    m_until.clear();
                if (found.size() < m_room)
                    continue;
                if (m_until.isEmpty())
                    break;
                if (found.size() >= m_room + m_overrun) {
                    m_until.clear(); // not coming: the page is full
                    break;
                }
            }
            m_walked += walked;
            m_overrun -= qMax(0, int(found.size()) - m_room);
            m_room = qMax(0, m_room - int(found.size()));
            if (!found.isEmpty()) {
                beginInsertRows(QModelIndex(), m_matches.size(), m_matches.size() + found.size() - 1);
                m_matches += found;
                endInsertRows();
            }
            if (m_room == 0 && m_until.isEmpty()) {
                stopSearch();
                m_moreMatches = true;
            }
            emit searchChanged();
        },
        [this, run](bool ok) {
            // Git's last batch may have filled the page just before: over already.
            if (run != m_searchRun)
                return;
            // At the end of the history, or failed: nothing more to load
            // either way. A failed page leaves what it found, and says it
            // failed; a reload tries again.
            m_search = nullptr;
            m_searching = false;
            m_searchFailed = !ok;
            m_until.clear();
            emit searchChanged();
        });
    if (m_searching)
        m_search = process; // else git could not even be started, and said so already
}

// The callbacks come off before the kill, so nothing of this search can
// arrive after; one under way (the last batch filling the page, done() to
// follow) finds m_searchRun moved on. The process is the repository's and
// goes by itself.
void HistoryModel::stopSearch()
{
    abandonProcess(m_search, this);
    m_search = nullptr;
    m_searching = false;
    ++m_searchRun;
}

// A page past the first says the user reads on: with a commit-graph, every
// page after it costs git a walk of the commits it skips, not a sort of the
// whole history. Once per repository, and nothing waits for it.
void HistoryModel::askForCommitGraph()
{
    if (m_commitGraphAsked)
        return;
    m_commitGraphAsked = true;
    m_repo->ensureCommitGraph();
}

// Everything read so far is the old repository's: the reload that follows a
// switch (MainWindow refreshes right away) reads it all again even where
// nothing seems to have moved, and a search of the old repository must not
// add its commits to the new one's list.
void HistoryModel::onRootChanged()
{
    m_loaded = false;
    m_commitGraphAsked = false;
    if (!filtering())
        return;
    stopSearch();
    beginResetModel();
    m_matches.clear();
    m_scope.clear();
    m_moreMatches = false;
    m_searchFailed = false;
    m_walked = 0;
    m_until.clear();
    m_logStale = true;
    endResetModel();
    emit searchChanged();
}

void HistoryModel::append(const QList<Commit> &commits)
{
    if (commits.isEmpty())
        return;
    beginInsertRows(QModelIndex(), m_commits.size(), m_commits.size() + commits.size() - 1);
    for (const Commit &c : commits) {
        m_commits.append(c);
        layoutRow(c);
    }
    endInsertRows();
}

int HistoryModel::allocateLane(const QString &hash)
{
    int lane = m_lanes.indexOf(QString());
    if (lane < 0) {
        lane = m_lanes.size();
        m_lanes.append(QString());
        m_laneColors.append(0);
    }
    m_lanes[lane] = hash;
    m_laneColors[lane] = m_nextColor++;
    return lane;
}

// Lane layout, one row at a time. Every lane carries the hash of the commit it
// is waiting for. When that commit arrives it takes the lane, its first parent
// inherits it, and every further parent either joins an existing lane or opens
// a new one. Rows come in an order where parents never precede children.
void HistoryModel::layoutRow(const Commit &commit)
{
    GraphRow row;
    int lane = m_lanes.indexOf(commit.hash);
    const bool hasChildAbove = lane >= 0;
    if (!hasChildAbove)
        lane = allocateLane(commit.hash);
    row.lane = lane;
    row.color = m_laneColors[lane];

    // Other lanes: either pass straight through, or end here because they
    // were waiting for this very commit (a merge seen from below).
    for (int j = 0; j < m_lanes.size(); ++j) {
        if (j == lane || m_lanes[j].isEmpty())
            continue;
        if (m_lanes[j] == commit.hash) {
            row.edges.append({j, -1, m_laneColors[j]});
            m_lanes[j].clear();
        } else {
            row.edges.append({j, j, m_laneColors[j]});
        }
    }

    // Own lane continues with the first parent unless another lane is already
    // waiting for it; then this line ends by joining that lane.
    bool continues = false;
    if (!commit.parents.isEmpty()) {
        const int existing = m_lanes.indexOf(commit.parents.first());
        if (existing < 0 || existing == lane) {
            m_lanes[lane] = commit.parents.first();
            continues = true;
        } else {
            row.edges.append({-1, existing, m_laneColors[existing]});
            m_lanes[lane].clear();
        }
    } else {
        m_lanes[lane].clear();
    }
    if (hasChildAbove)
        row.edges.append({lane, continues ? lane : -1, row.color});
    else if (continues)
        row.edges.append({-1, lane, row.color});

    // Further parents (merges): branch out to their lanes.
    for (int i = 1; i < commit.parents.size(); ++i) {
        int k = m_lanes.indexOf(commit.parents[i]);
        if (k < 0)
            k = allocateLane(commit.parents[i]);
        row.edges.append({-1, k, m_laneColors[k]});
    }

    m_maxLanes = qMax(m_maxLanes, m_lanes.size());
    while (!m_lanes.isEmpty() && m_lanes.last().isEmpty()) {
        m_lanes.removeLast();
        m_laneColors.removeLast();
    }
    m_rows.append(row);
}

int HistoryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : shown().size();
}

int HistoryModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant HistoryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= shown().size())
        return {};
    const Commit &c = shown().at(index.row());
    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case Message: return c.subject;
        case Author: return c.author;
        case Date: return c.date.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
        case Hash: return c.shortHash;
        }
        break;
    case Qt::ToolTipRole:
        if (index.column() == Author)
            return QStringLiteral("%1 <%2>").arg(c.author, c.email);
        if (index.column() == Hash)
            return c.hash;
        if (index.column() == Message)
            return c.body.isEmpty() ? c.subject : c.subject + QStringLiteral("\n\n") + c.body;
        break;
    }
    return {};
}

QVariant HistoryModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal)
        return {};
    // The design draws no header over the graph, which is too narrow for its
    // name anyway; the column keeps it for tooltips and screen readers.
    if (section == Graph) {
        if (role == Qt::ToolTipRole || role == Qt::AccessibleTextRole)
            return tr("Graph");
        return role == Qt::DisplayRole ? QVariant(QString()) : QVariant();
    }
    // The titles read from the left, 8 px in (the section's padding), like
    // the text of the column under them.
    if (role == Qt::TextAlignmentRole)
        return int(Qt::AlignLeft | Qt::AlignVCenter);
    if (role != Qt::DisplayRole)
        return {};
    switch (section) {
    case Message: return tr("Message");
    case Author: return tr("Author");
    case Date: return tr("Date");
    case Hash: return tr("SHA");
    }
    return {};
}
