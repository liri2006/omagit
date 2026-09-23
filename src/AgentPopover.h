#pragma once

#include "CommitMessageAgent.h"

#include <QFrame>
#include <QList>
#include <QPair>
#include <QPointer>
#include <QString>
#include <QStringList>

class CommitPage;
class LevelTrack;
class QAbstractButton;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpacerItem;
class QToolButton;
class QVBoxLayout;
class SegmentStrip;

// The agent settings: which coding agent writes the commit message, with
// which model and which reasoning level. AGENT is a segmented control of the
// installed agents, MODEL a list of the models the agent's CLI names with
// their ids and a tick on the chosen one, `Other model…` a field that opens
// only on demand, REASONING a track of the levels the model has, and
// `Generate now` asks at once. With no agent installed the card says so and
// offers the two commands that install one.
//
// Like CommitPopover it is a face of the commit page: the choice is the
// page's (CommitPage::applyAgentChoice(), saved the moment it is made), and
// the card re-reads it after every change instead of keeping a copy. It is an
// overlay inside the window, hanging under the cog it was opened from, or
// beside the overlay that cog is on when there is room for it there.
class AgentPopover : public QFrame
{
    Q_OBJECT
public:
    // `host` is the window's central widget: the card is placed in its
    // coordinates, outside any of its layouts.
    AgentPopover(CommitPage *page, QWidget *host);

    QWidget *anchor() const { return m_anchor; }
    // The overlay (the Mini commit card) whose cog may be the anchor: opened
    // from a cog on it, the card sits beside it rather than over it. Named
    // here rather than worked out from the layouts, which nest differently in
    // every layout of the window.
    void setBeside(QWidget *overlay) { m_beside = overlay; }

    // The card's parts, for the tests. Empty or null in the state that has
    // none of them (nothing installed, or a model without levels).
    SegmentStrip *agentPicker() const { return m_picker; }
    QList<QAbstractButton *> modelRows() const { return m_rows; }
    QAbstractButton *otherModelButton() const;
    QLineEdit *otherModelField() const { return m_otherField; }
    LevelTrack *levelTrack() const { return m_track; }
    QPushButton *generateButton() const { return m_generate; }
    QList<QAbstractButton *> copyButtons() const { return m_copyButtons; }

    // Re-measures the design's pixels at the text size of the moment.
    void applyTheme();

public slots:
    // Builds the card from the saved choice and the agent's catalog, hangs it
    // under `anchor` and gives it the keyboard. Open at the same anchor it is
    // only placed and focused again; at another one it moves there.
    void popup(QWidget *anchor);
    // Hides the card, the keyboard back where it was before it opened; the
    // one way it closes, whatever closes it.
    void dismiss();

signals:
    void opened();
    void dismissed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // Clears the layout and builds whichever state applies from the installed
    // agents, the saved choice and the chosen agent's catalog.
    void rebuild();
    void buildInstalled(const QList<AgentSpec> &installed);
    void buildNoneInstalled();
    // A section's header row: the caption at the left, a note at the right.
    void addHeader(const QString &caption, const QString &note);
    // A gap of `px` design pixels, re-measured by applyTheme().
    void addGap(int px);
    // A widget kept `px` design pixels tall by applyTheme().
    void fixHeight(QWidget *w, int px);

    void chooseAgent(const QString &agent);
    void chooseModel(const AgentModel &model);
    void chooseEffort(int stop);
    // Saves `choice` through the page and builds the card again for it,
    // keeping the keyboard on the part that had it.
    void apply(const AgentChoice &choice);
    void openOtherField();
    // Escape in the field: the button is back, nothing saved.
    void closeOtherField();
    void acceptOtherField();
    void generate();
    // Generate now is off while the agent is already writing: the sparkle
    // stops that run, and this button would only start another.
    void followGenerating();

    void setAnchor(QWidget *anchor);
    // Beside the overlay the anchor is on, or under the anchor, its right
    // edge on the anchor's; clamped to the host either way.
    void place();
    void scheduleLayout();

    CommitPage *m_page;
    QPointer<QWidget> m_anchor;
    QPointer<QWidget> m_beside; // setBeside()
    QList<QPointer<QWidget>> m_watched; // the anchor and its parents up to the host
    QPointer<QWidget> m_returnFocus;    // who had the keyboard before the card opened
    QVBoxLayout *m_layout;
    AgentChoice m_choice; // what the card was last built from
    QStringList m_levels; // the track's stops, "" first for the default

    // What the current build holds; rebuild() replaces all of it.
    QList<QPair<QSpacerItem *, int>> m_gaps;
    QList<QPair<QWidget *, int>> m_heights;
    QList<QLabel *> m_notes;  // QLabel#agentPopoverNote and #agentPopoverSmall: sized by the stylesheet
    QList<QWidget *> m_glyphs; // the none-installed robot
    QList<QLabel *> m_bold;   // the none-installed headline
    QList<QWidget *> m_commandRows;
    SegmentStrip *m_picker = nullptr;
    QList<QAbstractButton *> m_rows;
    QToolButton *m_otherButton = nullptr;
    QLineEdit *m_otherField = nullptr;
    LevelTrack *m_track = nullptr;
    QPushButton *m_generate = nullptr;
    QList<QAbstractButton *> m_copyButtons;

    bool m_layoutQueued = false;
    bool m_placing = false;
};

// The REASONING scale: the levels as stops on a line, the chosen one lit.
// Left and Right move along it, a press picks the nearest stop.
class LevelTrack : public QWidget
{
    Q_OBJECT
public:
    explicit LevelTrack(const QStringList &labels, QWidget *parent = nullptr);

    QStringList labels() const { return m_labels; }
    int selected() const { return m_selected; }
    void setSelected(int stop);
    // Where stop `i` sits, in the track's coordinates.
    QPointF stopCentre(int i) const;

signals:
    // A stop was picked by the pointer or the keys.
    void picked(int stop);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QStringList m_labels;
    int m_selected = 0;
};
