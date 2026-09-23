#pragma once

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
// The row folds with the pane's width, in the design's three forms: every
// button labelled; the three view options as glyphs; and, narrowest, Prev and
// Next as glyphs, the counter as "n/m" and the options behind one "…" menu.
class DiffPane : public QWidget
{
    Q_OBJECT
public:
    explicit DiffPane(QWidget *parent = nullptr);

    DiffView *view() const { return m_diff; }

    // The file's status and line counts, shown after "Change n of m".
    void setSummary(const QString &summary) { m_summary = summary; }

    void applyTheme();

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
    void updateChangeLabel();
    void fillOptionsMenu();

    DiffView *m_diff;
    QHBoxLayout *m_navRow;
    QLabel *m_changeLabel;
    QToolButton *m_prevButton;
    QToolButton *m_nextButton;
    QToolButton *m_paneButton;   // one / two panes (Ctrl+T)
    QToolButton *m_wsButton;     // whitespace markers (Ctrl+W)
    QToolButton *m_syntaxButton; // syntax colouring (Ctrl+L)
    QToolButton *m_optionsButton; // the options behind "…", compact only
    QMenu *m_optionsMenu;
    QString m_summary;
    // The last change position the view reported, so a form change can
    // re-render the counter without waiting for the next one.
    int m_changeIndex = -1;
    int m_changeTotal = 0;
    Form m_form = Form::Labelled;
};
