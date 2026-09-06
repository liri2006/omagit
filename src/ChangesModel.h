#pragma once

#include "GitRepo.h"

#include <QAbstractTableModel>
#include <QList>
#include <QSet>

// The list of changes in the commit dialog.
class ChangesModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column { Path, Extension, Status, LinesAdded, LinesRemoved, ColumnCount };

    explicit ChangesModel(QObject *parent = nullptr);

    void setChanges(const QList<FileChange> &changes);
    const FileChange &change(int row) const { return m_changes.at(row); }
    int count() const { return m_changes.size(); }

    QStringList checkedPaths() const;
    int checkedCount() const;
    void setAllChecked(bool checked);
    void setUnversionedChecked(bool checked);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

signals:
    void checkedChanged();

private:
    QList<FileChange> m_changes;
    QSet<QString> m_checked;
};
