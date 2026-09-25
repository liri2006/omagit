#pragma once

#include <QFrame>
#include <QPointer>
#include <QString>

class CommitPage;
class MessageEdit;
class MiniRail;
class QCheckBox;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QSpacerItem;
class QToolButton;
class QVBoxLayout;

// The commit controls of the Mini layout, on a card beside the rail's commit
// tile: MESSAGE with the agent cog, the message box with its generate button,
// how many of the listed files are selected, Amend and a primary Commit.
//
// It is a face of the commit page and nothing more: the box edits the page's
// own document (one message, one undo stack), the buttons say what the page's
// buttons say, and every action is the page's. The card is an overlay inside
// the window, not a window of its own, hanging beside the tile with its
// bottom on the tile's bottom and growing upwards with its text.
class CommitPopover : public QFrame
{
    Q_OBJECT
public:
    // `host` is the window's central widget: the card is placed in its
    // coordinates, outside any of its layouts.
    CommitPopover(CommitPage *page, QWidget *host);

    // The rail whose commit tile the card hangs beside. Presses on the rail
    // leave the card open.
    void setAnchor(MiniRail *rail);
    // An overlay that opens from this card (the agent settings): a press on
    // it, while it is shown, counts as a press on the card.
    void setCompanion(QWidget *companion);

    MessageEdit *editor() const { return m_editor; }
    QToolButton *agentButton() const { return m_agentButton; }
    QCheckBox *amendBox() const { return m_amend; }
    QPushButton *commitButton() const { return m_commitButton; }
    QLabel *hintLabel() const { return m_hint; }
    // The hint as a whole; the label shows it elided to its width.
    QString hintText() const { return m_hintText; }

    // Re-measures the design's pixels at the text size of the moment.
    void applyTheme();

public slots:
    // Shows the card with the page's controls as they are now, placed beside
    // the tile, with the keyboard in the message box. Showing it again only
    // does that once more. Whether the layout and the mode allow a card at
    // all is the window's to decide before calling this.
    void popup();
    // Hides the card; the one way it closes, whatever closes it.
    void dismiss();
    // Presses Commit: the page commits, and a commit made closes the card.
    // False (and the card stays, the keyboard back in its box) when the
    // button is disabled or the page did not commit.
    bool commit();

signals:
    void opened();
    void dismissed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // Copies the page's controls onto the card's.
    void applyControls();
    // Width, then the message box's height at that width, then the place:
    // the bottom on the tile's bottom.
    void place();
    // place() once the current turn of the event loop has settled.
    void scheduleLayout();
    void setHint(int checked, int shown);
    void elideHint();
    // "Amend last commit" where the action row has room for it, "Amend" where not.
    void updateAmendLabel();

    CommitPage *m_page;
    MiniRail *m_rail = nullptr;
    QPointer<QWidget> m_companion;
    QVBoxLayout *m_layout;
    QHBoxLayout *m_header; // MESSAGE and the cog
    QSpacerItem *m_editorGap;
    QSpacerItem *m_hintGap;
    QSpacerItem *m_ruleGap;
    QSpacerItem *m_actionGap;
    QHBoxLayout *m_actions;
    QToolButton *m_agentButton;
    MessageEdit *m_editor;
    QLabel *m_hint;
    QString m_hintText;
    QCheckBox *m_amend;
    QPushButton *m_commitButton;
    bool m_layoutQueued = false;
    bool m_placing = false;
};
