#pragma once

#include <QList>
#include <QSize>
#include <QString>
#include <QWidget>

#include "Segmented.h"

class BadgeButton;
class QMenu;
class QToolButton;
class QVBoxLayout;

// The bar above the body, in every layout: the repository and the branch chip
// on the left, the Changes | History tabs centred in the window, and Pull,
// Push, Fetch, Merge with the two layout toggles on the right, over a hairline
// like the footer's.
//
// A narrower window folds the row in steps (foldLevel()): the sync labels go
// first, then the sync buttons themselves into a "more" menu, then the
// repository label, then the tab labels, and only when nothing else is left
// does the branch name elide. The bar owns the controls and their
// presentation; the window keeps the git side of them.
//
// Stacked (setStacked(), the window's narrowest widths) is a presentation of
// its own: a third tab, Diff, between the two; the four sync buttons give way
// to one sync dropdown carrying both counts; More is always there; the layout
// toggles go. It folds in three steps of its own.
class TopBar : public QWidget
{
    Q_OBJECT
public:
    explicit TopBar(QWidget *parent = nullptr);

    // The canonical text of the two chips: the bar keeps it and folds or
    // elides a copy, so widening always spells them out again.
    void setRepositoryName(const QString &name);
    void setBranchLabel(const QString &label);
    QString repositoryName() const { return m_repositoryName; }
    QString branchLabel() const { return m_branchLabel; }

    // How many files the changes list shows, in the Changes tab's pill; 0
    // takes the pill away. The count is the changes list's in both modes.
    void setChangesCount(int count);
    int changesCount() const;

    // The body's presentation the tabs stand for. Diff is only offered while
    // stacked; the window decides which is current and says so here, which
    // asks for nothing back.
    enum class Tab { Changes, Diff, History };
    Q_ENUM(Tab)
    void setCurrentTab(Tab tab);
    Tab currentTab() const;

    // The narrow presentation; see the class comment.
    void setStacked(bool on);
    bool isStacked() const { return m_stacked; }

    QToolButton *repoButton() const { return m_repoButton; }     // the repository menu's anchor
    QToolButton *branchButton() const { return m_branchButton; } // the branch menu's anchor
    QToolButton *changesTab() const { return m_changesTab; }
    QToolButton *diffTab() const { return m_diffTab; }
    QToolButton *historyTab() const { return m_historyTab; }
    BadgeButton *fetchButton() const { return m_fetch; }
    BadgeButton *pullButton() const { return m_pull; }
    BadgeButton *pushButton() const { return m_push; }
    BadgeButton *mergeButton() const { return m_merge; }
    BadgeButton *moreButton() const { return m_more; }
    BadgeButton *syncDropdown() const { return m_syncDropdown; } // the stacked form of the four
    QToolButton *layoutButton() const { return m_layoutButton; } // Docked/Mini, checked in Mini
    QToolButton *diffToggle() const { return m_diffToggle; }     // checked while the diff pane shows

    // Re-fetches the glyphs and the fonts, re-scales every design distance and
    // measures the row again.
    void applyTheme();

    // 0 spells everything out, 6 is the narrowest form (2 while stacked); what
    // the current width fits (see the tables in TopBar.cpp).
    int foldLevel() const { return m_level; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    // A tab was clicked: the window routes it to a mode or to the Diff tab.
    void tabRequested(TopBar::Tab tab);
    // The more menu's own entries, beyond the folded sync buttons.
    void refreshRequested();
    void openRepositoryRequested();
    void cloneRequested();
    void keybindingsRequested();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    // A sync button with what it wears at each presentation, so a theme change
    // re-fetches the glyphs from one table.
    struct SyncControl {
        SyncControl(BadgeButton *button, uint glyph, QString fallback, QString label)
            : button(button), glyph(glyph), fallback(std::move(fallback)), label(std::move(label))
        {
        }

        BadgeButton *button;
        BadgeButton *probe = nullptr; // its never-shown twin, for measuring the labelled form
        uint glyph;
        QString fallback;
        QString label;
        QString full;      // glyph + label
        QString iconText;  // the glyph alone
        int fullWidth = 0;
        int iconWidth = 0;
    };
    // What the row measures, in the pixels of the moment.
    struct Metrics {
        int repoFull = 0;    // folder glyph + name + chevron
        int repoFolded = 0;  // the bare folder glyph: space(28)
        int branchFull = 0;
        int branchLabel = 0;    // the name inside it, on its own
        int branchChrome = 0;   // and what the button puts around it
        int branchEllipsis = 0; // a lone "…" in the chip's font: the stacked row's floor
        int more = 0;           // the ordinary row's; stacked, More is space(28)
        int tabsLabels = 0;
        int tabsGlyphs = 0;
        int toggles = 0; // both, with the gap between them
        int divider = 0; // the gap, the line and the gap
        int height = 0;  // the row's
    };

    void measure();
    void relayout();
    // What level `level` comes to with `branchLabelWidth` of the branch name.
    int totalWidth(int level, int branchLabelWidth) const;
    int rightGroupWidth(int level) const;
    int minBranchLabel() const;
    // How many levels the presentation of the moment has, and whether its
    // level `level` elides the branch name and shows the tab labels.
    int levelCount() const;
    bool elides(int level) const;
    bool tabLabels(int level) const;
    void apply(int level, int branchLabelWidth);
    void place(int level, int branchLabelWidth);
    void placeTabs(int leftEnd, int rightStart, int level);
    void updateMoreMark();
    void fillMoreMenu();
    void fillSyncMenu();
    // A menu entry standing for a sync button: its glyph, `label` and the
    // count, enabled and explained as the button is, clicking it.
    void addSyncEntry(QMenu *menu, const SyncControl &c, const QString &label);

    QVBoxLayout *m_rootLayout;
    QWidget *m_row;   // the controls, placed by hand: the tabs follow the window's centre
    QToolButton *m_repoButton;
    QToolButton *m_branchButton;
    SegmentStrip *m_tabs;  // the frame around the segments
    SegmentButton *m_changesTab;
    SegmentButton *m_diffTab;  // stacked only
    SegmentButton *m_historyTab;
    BadgeButton *m_pull;
    BadgeButton *m_push;
    BadgeButton *m_fetch;
    BadgeButton *m_merge;
    BadgeButton *m_more;
    QMenu *m_moreMenu;
    BadgeButton *m_syncDropdown; // a SyncDropdown (TopBar.cpp)
    QMenu *m_syncMenu;
    QWidget *m_divider;
    QToolButton *m_layoutButton;
    QToolButton *m_diffToggle;
    // Measured on, never shown: what a candidate text would come to on a chip
    // or on a labelled sync button.
    QToolButton *m_probeRepo;
    QToolButton *m_probeBranch;
    QList<SyncControl> m_syncControls;     // pull, push, fetch, merge — the order they stand in
    QList<BadgeButton *> m_foldedSync;     // the ones the more menu carries, in that same order
    QString m_repositoryName;
    QString m_branchLabel;
    Metrics m_metrics;
    int m_level = 0;
    bool m_stacked = false;
};
