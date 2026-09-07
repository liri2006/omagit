#pragma once

#include "DiffModel.h"

#include <QAbstractScrollArea>
#include <QFont>
#include <QPoint>
#include <QVector>

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

    void setDocument(const DiffDocument &doc, const QString &title, const QString &subtitle = QString(),
                     const QString &leftLabel = QString(), const QString &rightLabel = QString());
    void clear(const QString &message = QString());
    const DiffDocument &document() const { return m_doc; }

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
    void setTabWidth(int spaces);
    int tabWidth() const { return m_tabWidth; }
    void setShowWhitespace(bool on);
    bool showWhitespace() const { return m_showWhitespace; }

public slots:
    void setMode(Mode mode);
    void setTwoPane(bool on) { setMode(on ? TwoPane : OnePane); }
    void nextChange();
    void previousChange();
    void firstChange();
    void copySelection();
    void selectAll();
    void refreshTheme();

signals:
    void changeIndexChanged(int index, int total);
    void modeChanged(Mode mode);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
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

    void rebuildLayout();
    void updateMetrics();
    void updateScrollBars();
    int headerHeight() const;
    int marginWidth() const;
    int paneCount() const { return m_panes.size(); }
    QRect paneRect(int pane) const;  // full pane incl. margin, below the header
    int lineAt(int pane, int row) const; // index into m_doc.lines or -1 for filler
    QString expanded(const DiffLine &l) const;
    QString cellText(int pane, int row) const;
    QVector<int> columnMap(const QString &raw) const; // raw index -> expanded column
    Pos posAt(const QPoint &p, int forcePane = -1) const;
    bool hasSelection() const { return !(m_selAnchor == m_selCursor); }
    QString selectedText() const;
    void scrollToRow(int row);
    void drawMarginIcon(class QPainter &p, const QRect &r, DiffLine::State state) const;
    void drawCell(class QPainter &p, int pane, int row, int y, const QRect &pr);
    void drawMargin(class QPainter &p, int pane, int row, int y, const QRect &pr);
    void goToBlock(int index);

    DiffDocument m_doc;
    QString m_title;
    QString m_subtitle;
    QString m_leftLabel;
    QString m_rightLabel;
    QString m_emptyMessage;
    QFont m_font;
    Mode m_mode = TwoPane;
    QVector<QVector<int>> m_panes;   // panes[pane][row] = line index or -1
    QVector<int> m_blockStarts;      // row indices
    qreal m_charWidth = 8;
    int m_lineHeight = 16;
    int m_digits = 1;
    int m_maxCols = 0;
    int m_tabWidth = 4;
    bool m_showWhitespace = false;
    int m_currentBlock = -1;
    Pos m_selAnchor, m_selCursor;
    bool m_dragging = false;
    QScrollBar *m_paneScrollBars[2] = {};
};
