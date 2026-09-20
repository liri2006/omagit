#pragma once

#include <QWidget>

class DiffView;
class QHBoxLayout;
class QLabel;
class QToolButton;

// The right pane: the Prev/Next navigation row with the view options above,
// the diff view below. The window keeps the shortcuts that drive it — a
// shortcut on a hidden widget is inactive and this pane can be hidden — and
// the top bar carries the toggle that hides it.
class DiffPane : public QWidget
{
    Q_OBJECT
public:
    explicit DiffPane(QWidget *parent = nullptr);

    DiffView *view() const { return m_diff; }

    // The file's status and line counts, shown after "Change n of m".
    void setSummary(const QString &summary) { m_summary = summary; }

    void applyTheme();

public slots:
    void togglePaneMode();
    void toggleWhitespace();
    void toggleSyntax();
    void nextChange();
    void previousChange();
    void zoomBy(int step);
    void resetZoom();

private:
    DiffView *m_diff;
    QLabel *m_changeLabel;
    QToolButton *m_prevButton;
    QToolButton *m_nextButton;
    QToolButton *m_paneButton;   // one / two panes (Ctrl+T)
    QToolButton *m_wsButton;     // whitespace markers (Ctrl+W)
    QToolButton *m_syntaxButton; // syntax colouring (Ctrl+L)
    QString m_summary;
};
