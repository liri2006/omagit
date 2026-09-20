#pragma once


#include <QList>
#include <QListView>
#include <QPersistentModelIndex>
#include <QString>
#include <QWidget>

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

// The narrow strip shown instead of the left section in the Mini layout: the
// file miniatures, with the hash of the commit they belong to above them (in
// history mode) and Refresh underneath. The modes and the sync buttons live in
// the top bar, which stays above the rail in every layout.
class MiniRail : public QWidget
{
    Q_OBJECT
public:
    static constexpr int kWidth = 52;

    explicit MiniRail(QWidget *parent = nullptr);

    // The files to show; `selection` must belong to `model`.
    void setSource(QAbstractItemModel *model, QItemSelectionModel *selection);
    // Short hash of the commit whose files are listed (history mode); empty hides it.
    void setCommitLabel(const QString &hash, const QString &tip);
    void applyTheme();

    MiniRailList *list() const { return m_list; }

signals:
    void refreshRequested();
    void activated(const QModelIndex &index);

private:
    // Every button with the glyph it wears, so a theme change re-fetches them
    // from one table instead of restating each pair a second time.
    struct RailGlyph {
        QToolButton *button;
        uint code;
        QString fallback;
    };
    QToolButton *addButton(uint glyph, const QString &fallback, const QString &tip = QString());

    QToolButton *m_refreshButton;
    void updateHashLabel();
    QWidget *m_hashRule; // the separator under the hash, shown with it
    QLabel *m_hashLabel;
    QString m_hash, m_hashTip;
    QList<RailGlyph> m_glyphs;
    MiniRailList *m_list;
};
