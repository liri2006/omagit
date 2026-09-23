#pragma once


#include <QList>
#include <QListView>
#include <QMetaObject>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QWidget>

class QAbstractItemModel;
class QItemSelectionModel;
class QLabel;
class QSpacerItem;
class QToolButton;
class QVBoxLayout;

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
// history mode), Refresh underneath and, in commit mode, the commit tile at
// the very bottom, badged with the number of checked files. The modes and the
// sync buttons live in the top bar, which stays above the rail in every layout.
class MiniRail : public QWidget
{
    Q_OBJECT
public:
    // The design's rail width, in the pixels of a 12 px text size.
    static constexpr int kWidth = 52;
    // The rail's width at the text size of the moment.
    static int railWidth();

    explicit MiniRail(QWidget *parent = nullptr);

    // The files to show; `selection` must belong to `model`. The tile's badge
    // counts the rows of `model` whose Check column is ticked.
    void setSource(QAbstractItemModel *model, QItemSelectionModel *selection);
    // Short hash of the commit whose files are listed (history mode); empty hides it.
    void setCommitLabel(const QString &hash, const QString &tip);
    void applyTheme();

    MiniRailList *list() const { return m_list; }
    // The commit tile: the whole widget answers a click, the accent square is
    // painted at its bottom with the badge overhanging the square's corner.
    QToolButton *commitTile() const;
    // The tile with its separator and gaps: in commit mode only.
    void setCommitTileVisible(bool on);
    // Lit while the commit popover it opens is on screen.
    void setCommitTileActive(bool on);
    // Where the tile paints its badge, in the tile's coordinates: the
    // miniatures' circle, widened leftwards into a pill for a count wider than
    // it. Null when nothing is checked.
    QRect commitBadgeRect() const;
    // What the badge says: the checked rows of the source.
    int checkedCount() const { return m_checked; }

signals:
    void refreshRequested();
    void activated(const QModelIndex &index);
    void commitRequested();

private:
    // Every button with the glyph it wears, so a theme change re-fetches them
    // from one table instead of restating each pair a second time.
    struct RailGlyph {
        QToolButton *button;
        uint code;
        QString fallback;
    };
    QToolButton *addButton(uint glyph, const QString &fallback, const QString &tip = QString());
    // Recounts the checked rows of the source for the badge.
    void countChecked();
    // The design pixels of the commit section, at the text size of the moment.
    void applyCommitMetrics();

    QToolButton *m_refreshButton;
    void updateHashLabel();
    QWidget *m_hashRule; // the separator under the hash, shown with it
    QLabel *m_hashLabel;
    QString m_hash, m_hashTip;
    QList<RailGlyph> m_glyphs;
    MiniRailList *m_list;
    QVBoxLayout *m_layout;
    QWidget *m_commitSection;      // the gaps, the separator and the tile, shown together
    QSpacerItem *m_ruleGap;        // Refresh to the separator
    QSpacerItem *m_tileGap;        // the separator to the tile's badge room
    QToolButton *m_commitTile;
    QPointer<QAbstractItemModel> m_badgeSource;
    QList<QMetaObject::Connection> m_badgeConnections; // the badge's own, and only those
    int m_checked = 0;
};
