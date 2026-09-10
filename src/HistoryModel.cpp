#include "HistoryModel.h"

HistoryModel::HistoryModel(GitRepo *repo, QObject *parent)
    : QAbstractTableModel(parent), m_repo(repo)
{
}

void HistoryModel::reload(bool force)
{
    const QHash<QString, QList<RefLabel>> refs = m_repo->refs();
    int code = 0;
    QString head = QString::fromUtf8(m_repo->run({QStringLiteral("rev-parse"), QStringLiteral("HEAD")}, &code)).trimmed();
    if (code != 0)
        head.clear();
    if (!force && m_loaded && !m_failed && refs == m_refs && head == m_head)
        return; // nothing moved: keep the rows, the selection and the scroll position
    const int wanted = qMax(m_batch, m_commits.size());
    beginResetModel();
    m_commits.clear();
    m_rows.clear();
    m_lanes.clear();
    m_laneColors.clear();
    m_nextColor = 0;
    m_maxLanes = 0;
    m_exhausted = false;
    m_failed = false;
    m_refs = refs;
    m_head = head;
    m_loaded = true;
    bool ok = false;
    const QList<Commit> commits = m_repo->log(0, wanted, m_allRefs, &ok);
    m_failed = !ok;
    for (const Commit &c : commits) {
        m_commits.append(c);
        layoutRow(c);
    }
    m_exhausted = commits.size() < wanted;
    endResetModel();
}

bool HistoryModel::loadMore()
{
    if (m_exhausted || m_failed)
        return false;
    bool ok = false;
    const QList<Commit> commits = m_repo->log(m_commits.size(), m_batch, m_allRefs, &ok);
    if (!ok) {
        m_failed = true;
        return false;
    }
    if (commits.size() < m_batch)
        m_exhausted = true;
    append(commits);
    return !commits.isEmpty();
}

void HistoryModel::setAllRefs(bool on)
{
    if (m_allRefs == on)
        return;
    m_allRefs = on;
    reload(true);
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
    return parent.isValid() ? 0 : m_commits.size();
}

int HistoryModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant HistoryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_commits.size())
        return {};
    const Commit &c = m_commits[index.row()];
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
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case Graph: return tr("Graph");
    case Message: return tr("Message");
    case Author: return tr("Author");
    case Date: return tr("Date");
    case Hash: return tr("SHA");
    }
    return {};
}
