#pragma once

#include "DiffModel.h"
#include "SyntaxHighlighter.h"

#include <QAbstractScrollArea>
#include <QColor>
#include <QFont>
#include <QPoint>
#include <QVector>
#include <array>

class DiffScrollBar;
class QPainter;

// Side-by-side and one-pane diff viewer.
//
// Two-pane mode (default): base version on the left, working tree on the
// right, aligned row by row with grey filler lines where one side has no
// counterpart. One-pane mode: a single unified list of lines.
// Both modes share the header bar, margin (state icon + line number),
// coloured lines and inline (intra-line) highlights.
class DiffView : public QAbstractScrollArea
{
    Q_OBJECT
public:
    enum Mode { OnePane, TwoPane };

    explicit DiffView(QWidget *parent = nullptr);

    void setDocument(const DiffDocument &doc, const QString &title, const QString &subtitle,
                     const QString &leftLabel, const QString &rightLabel);
    void clear(const QString &message = QString());
    const DiffDocument &document() const { return m_doc; }
    // Whether the one-pane header writes the subtitle (status, +/−) at its
    // right: not while the toolbar above says the same.
    void setSubtitleShown(bool shown);
    bool subtitleShown() const { return m_subtitleShown; }
    // The dim text the header over `pane` wears at its right at the width of
    // the moment: the subtitle, or in two-pane mode that side's label. Empty
    // when the whole path and a group gap would not leave it the room.
    QString headerLabel(int pane = 0) const;

    // Where the user is in the document: the scroll offsets and the current
    // change. A refresh that shows the same file again restores it so the
    // view does not jump back to the first change.
    struct ViewState {
        int row = 0;
        int column = 0;
        int block = -1;
    };
    ViewState viewState() const;
    void restoreViewState(const ViewState &state);

    Mode mode() const { return m_mode; }
    void setShowWhitespace(bool on);
    void setSyntaxHighlighting(bool on);

public slots:
    void setMode(Mode mode);
    void setTwoPane(bool on) { setMode(on ? TwoPane : OnePane); }
    // The text size, in px steps from the theme's base (Ctrl+wheel, Ctrl++ / Ctrl+-); 0 is the base.
    void zoomBy(int step);
    void resetZoom() { zoomBy(-m_zoom); }
    void nextChange();
    void previousChange();
    void firstChange();
    void copySelection();
    void selectAll();
    void refreshTheme();

signals:
    void changeIndexChanged(int index, int total);
    void modeChanged(Mode mode);
    void syntaxHighlightingChanged(bool on);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    struct Pos {
        int pane = 0;
        int row = 0;
        int col = 0; // column in expanded text
        bool operator<(const Pos &o) const { return row < o.row || (row == o.row && col < o.col); }
        bool operator==(const Pos &o) const { return pane == o.pane && row == o.row && col == o.col; }
    };

    // One diff line with its tabs already expanded, built when the document is
    // set so painting and hit-testing never allocate per line.
    struct LineLayout {
        QString text;          // tabs expanded
        QVector<int> columns;  // raw index -> column; empty while the two agree
        int column(int index) const { return columns.isEmpty() ? index : columns.at(index); }
    };

    // Two-pane geometry: the width the panes share (the divider takes the rest)
    // and the least either of them may become, so neither loses its margin.
    struct PaneSplit {
        int available = 1;
        int minimum = 0;
        int clamp(int left) const { return qBound(minimum, left, available - minimum); }
    };

    // The part of a cell's geometry that every drawing step below needs.
    struct Cell {
        qreal x0 = 0;    // x of column 0, already scrolled
        int y = 0;
        int firstCol = 0;
        int lastCol = 0;
        int baseline = 0;
    };

    void rebuildLayout();
    void rebuildLineLayouts();
    void updateMetrics();
    void updateScrollBars();
    int headerHeight() const;
    QRect headerTextRect(int pane) const;
    int linesTop() const;
    int marginWidth() const;
    int edgeOffset(int pane) const;
    int paneMargin(int pane) const;
    int paneCount() const { return m_panes.size(); }
    int rowCount() const { return m_panes.isEmpty() ? 0 : m_panes[0].size(); }
    PaneSplit paneSplitMetrics() const;
    QRect paneRect(int pane) const;  // full pane incl. margin, below the header
    bool onDivider(const QPoint &point) const;
    void updateCursor(const QPoint &pos);
    void setPaneSplit(qreal split);
    int lineAt(int pane, int row) const; // index into m_doc.lines or -1 for filler
    const LineLayout &layoutAt(int pane, int row) const;
    QString cellText(int pane, int row) const;
    Pos posAt(const QPoint &p, int forcePane = -1) const;
    bool hasSelection() const { return !(m_selAnchor == m_selCursor); }
    QString selectedText() const;
    void scrollToRow(int row);
    void drawMarginIcon(QPainter &p, const QRect &r, DiffLine::State state) const;
    void drawCell(QPainter &p, int pane, int row, int y, const QRect &pr);
    QColor fillLineBackground(QPainter &p, const DiffLine &l, const QRect &box) const;
    void drawInlineHighlight(QPainter &p, const DiffLine &l, const LineLayout &layout,
                             const Cell &cell, const QColor &inlineBg) const;
    void drawSelection(QPainter &p, int pane, int row, const LineLayout &layout, const Cell &cell) const;
    void drawCellText(QPainter &p, const DiffLine &l, const LineLayout &layout, const Cell &cell) const;
    void drawWhitespaceMarkers(QPainter &p, const DiffLine &l, const LineLayout &layout, const Cell &cell) const;
    void applySyntax(); // runs the tokeniser over m_doc, or clears it
    void drawMargin(QPainter &p, int pane, int row, int y, const QRect &pr);
    void goToBlock(int index);

    DiffDocument m_doc;
    QVector<LineLayout> m_lineLayouts; // parallel to m_doc.lines
    QString m_title;
    QString m_subtitle;
    bool m_subtitleShown = true;
    QString m_leftLabel;
    QString m_rightLabel;
    QString m_emptyMessage;
    QFont m_font;
    Mode m_mode = TwoPane;
    QVector<QVector<int>> m_panes;   // panes[pane][row] = line index or -1
    QVector<int> m_blockStarts;      // row indices
    qreal m_charWidth = 8;
    int m_lineHeight = 16;
    int m_zoom = 0;                  // Ctrl+wheel offset from the theme's base size, in px
    int m_digits = 1;
    int m_maxCols = 0;
    int m_tabWidth = 4;
    bool m_showWhitespace = false;
    bool m_syntax = true;
    Language m_language = Language::None;
    std::array<QColor, size_t(kTokenKindCount)> m_syntaxPens; // one per TokenKind, refreshed with the theme
    int m_currentBlock = -1;
    Pos m_selAnchor, m_selCursor;
    bool m_dragging = false;
    bool m_resizingPanes = false;
    int m_dividerDragOffset = 0;
    qreal m_paneSplit = 0.5;
    DiffScrollBar *m_changeBar = nullptr; // the vertical bar, which also paints the change ribbons
    QScrollBar *m_paneScrollBars[2] = {};
};
