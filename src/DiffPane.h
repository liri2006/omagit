#pragma once

#include <QColor>
#include <QString>
#include <QWidget>

class DiffView;
class QHBoxLayout;
class QLabel;
class QMenu;
class QResizeEvent;
class QToolButton;

// The right pane: the Prev/Next navigation row with the view options above,
// the diff view below. The window keeps the shortcuts that drive it — a
// shortcut on a hidden widget is inactive and this pane can be hidden — and
// the top bar carries the toggle that hides it.
//
// The row folds with the pane's width, in the design's three forms: Prev,
// Next and the view dropdown labelled; the dropdown as its glyph and chevron;
// and, narrowest, Prev and Next as glyphs, the counter as "n/m" and the view
// options behind one "…" menu. Whitespace and Syntax are 28 px squares in
// every form that shows them.
class DiffPane : public QWidget
{
    Q_OBJECT
public:
    explicit DiffPane(QWidget *parent = nullptr);

    DiffView *view() const { return m_diff; }

    // The file's status and line counts, shown after "Change n of m": the
    // status in its colour, the added lines green and the removed ones red.
    // Counts below zero are left out (a binary file, a diff of no lines).
    struct Summary {
        QString status;
        QColor colour;
        int added = -1, removed = -1;
    };
    void setSummary(const Summary &summary) { m_summary = summary; }
    void clearSummary() { m_summary = Summary(); }

    void applyTheme();

    // Stacked (the window's narrowest widths), the diff is unified, as the
    // design's Diff tab shows it: two panes do not fit. The split or unified
    // choice saved is the wide window's and comes back with it; Ctrl+T and
    // the menus still switch while stacked, for as long as it lasts, and
    // save nothing.
    void setStacked(bool on);

    // Opens the "…" menu of the narrowest form (--screenshot-menu diff); only
    // while that button is on screen, so nothing hangs from a hidden one.
    void showOptionsMenu();

public slots:
    void togglePaneMode();
    void toggleWhitespace();
    void toggleSyntax();
    void nextChange();
    void previousChange();
    void zoomBy(int step);
    void resetZoom();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    enum class Form { Labelled, Middle, Compact };

    // The form for the pane's width of the moment, and putting it on the row.
    Form formForWidth() const;
    void applyForm();
    // The view dropdown's face: the split or unified glyph, its name where
    // the form has the room, the chevron.
    void updateViewButton();
    void updateChangeLabel();
    // The Split and Unified entries of a menu, the current one ticked.
    void addViewEntries(QMenu *menu);
    void fillViewMenu();
    void fillOptionsMenu();

    DiffView *m_diff;
    QHBoxLayout *m_navRow;
    QLabel *m_changeLabel;
    QToolButton *m_prevButton;
    QToolButton *m_nextButton;
    QToolButton *m_viewButton;   // the view dropdown: split (two panes) or unified (Ctrl+T)
    QMenu *m_viewMenu;
    QToolButton *m_wsButton;     // whitespace markers (Ctrl+W)
    QToolButton *m_syntaxButton; // syntax colouring (Ctrl+L)
    QToolButton *m_optionsButton; // the options behind "…", compact only
    QMenu *m_optionsMenu;
    Summary m_summary;
    // The last change position the view reported, so a form change can
    // re-render the counter without waiting for the next one.
    int m_changeIndex = -1;
    int m_changeTotal = 0;
    Form m_form = Form::Labelled;
    bool m_stacked = false;
    bool m_wideTwoPane = true; // the wide window's view, kept while stacked
};
