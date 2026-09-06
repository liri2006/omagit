#pragma once

#include "GitRepo.h"

#include <QAbstractTableModel>
#include <QHash>
#include <QList>
#include <QStringList>
#include <QVector>

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

// Commits of `git log`, loaded in batches, with a lane layout for the graph
// column that is computed incrementally as batches are appended.
class HistoryModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column { Graph, Message, Author, Date, Hash, ColumnCount };

    explicit HistoryModel(GitRepo *repo, QObject *parent = nullptr);

    // Re-reads refs and the commits (at least as many as were loaded before).
    void reload();
    // Appends the next batch. Returns false when the history is exhausted.
    bool loadMore();
    bool exhausted() const { return m_exhausted; }
    bool failed() const { return m_failed; }

    void setAllRefs(bool on);
    bool allRefs() const { return m_allRefs; }

    const Commit &commit(int row) const { return m_commits.at(row); }
    const GraphRow &graph(int row) const { return m_rows.at(row); }
    QList<RefLabel> labels(int row) const { return m_refs.value(m_commits.at(row).hash); }
    bool isHead(int row) const { return m_commits.at(row).hash == m_head; }
    int laneCount() const { return m_maxLanes; }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
    void append(const QList<Commit> &commits);
    void layoutRow(const Commit &commit);
    int allocateLane(const QString &hash);

    GitRepo *m_repo;
    QList<Commit> m_commits;
    QVector<GraphRow> m_rows;
    QHash<QString, QList<RefLabel>> m_refs;
    QString m_head;
    bool m_allRefs = false;
    bool m_exhausted = false;
    bool m_failed = false;
    int m_batch = 500;

    // Graph state carried from one row to the next: which commit each lane is
    // waiting for, and the colour of the line travelling in it.
    QStringList m_lanes;
    QVector<int> m_laneColors;
    int m_nextColor = 0;
    int m_maxLanes = 0;
};
