#pragma once

#include "GitRepo.h"

#include <QAbstractTableModel>
#include <QList>
#include <QObject>
#include <QSet>

class QTableView;

// The list of changes in the commit dialog. Also used, without
// checkboxes, for the files of a commit in the history view.
class ChangesModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column { Path, Extension, Status, LinesAdded, LinesRemoved, ColumnCount };

    explicit ChangesModel(QObject *parent = nullptr);

    void setChanges(const QList<FileChange> &changes);
    const FileChange &change(int row) const { return m_changes.at(row); }
    int count() const { return m_changes.size(); }

    void setCheckable(bool on);
    bool checkable() const { return m_checkable; }

    QStringList checkedPaths() const;
    int checkedCount() const;
    void setAllChecked(bool checked);
    void setUnversionedChecked(bool checked);
    void setPathsChecked(const QStringList &paths, bool checked);

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
    bool m_checkable = true;
};

// Applies the shared look of a changes table (column widths, row height, no
// grid) and keeps the Path column filling the leftover width, never narrower
// than 240 px (then the view scrolls horizontally).
class ChangesTableSetup : public QObject
{
    Q_OBJECT
public:
    explicit ChangesTableSetup(QTableView *table);
    void applyTheme();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void fitPathColumn();
    QTableView *m_table;
};
