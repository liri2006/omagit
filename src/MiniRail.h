#pragma once


#include <QListView>
#include <QPersistentModelIndex>
#include <QWidget>

class BadgeButton;
class QAbstractItemModel;
class QItemSelectionModel;
class QLabel;
class QToolButton;

// The file list of the Mini layout: one miniature per file, sharing the
// selection model of the full table so both always agree on the current
// file. Hovering a miniature pops up its full path right away; Space or
// Ctrl+click toggles whether it is part of the commit.
class MiniRailList : public QListView
{
    Q_OBJECT
public:
    explicit MiniRailList(QWidget *parent = nullptr);

protected:
    bool viewportEvent(QEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    void toggleChecked(const QModelIndex &index);
    void showTip(const QModelIndex &index);
    void hideTip();
    QString tipText(const QModelIndex &index) const;

    QPersistentModelIndex m_tipIndex;
    QLabel *m_tip = nullptr; // floating name/path popup, created on first use
};

// The narrow strip shown instead of the left section in the Mini layout:
// mode buttons on top, the file miniatures in the middle, then Pull, Push
// and Fetch (with their count badges), the way back to Docked, and Refresh.
class MiniRail : public QWidget
{
    Q_OBJECT
public:
    static constexpr int kWidth = 52;

    explicit MiniRail(QWidget *parent = nullptr);

    // The files to show; `selection` must belong to `model`.
    void setSource(QAbstractItemModel *model, QItemSelectionModel *selection);
    void setCommitMode(bool commit);
    // Short hash of the commit whose files are listed (history mode); empty hides it.
    void setCommitLabel(const QString &hash, const QString &tip);
    void applyTheme();

    MiniRailList *list() const { return m_list; }
    // Icon-only twins of the toolbar's sync buttons; the window drives them.
    BadgeButton *fetchButton() const { return m_fetchButton; }
    BadgeButton *pullButton() const { return m_pullButton; }
    BadgeButton *pushButton() const { return m_pushButton; }

signals:
    void commitModeRequested();
    void historyModeRequested();
    void refreshRequested();
    void activated(const QModelIndex &index);

private:
    QToolButton *m_commitButton;
    QToolButton *m_historyButton;
    QToolButton *m_refreshButton;
    BadgeButton *m_fetchButton;
    BadgeButton *m_pullButton;
    BadgeButton *m_pushButton;
    void updateHashLabel();
    QLabel *m_hashLabel;
    QString m_hash, m_hashTip;
    QList<QWidget *> m_hairlines;
    MiniRailList *m_list;
};
