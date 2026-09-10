#include "MessageEdit.h"

#include <QAbstractTextDocumentLayout>
#include <QEvent>
#include <QScrollBar>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QtMath>

namespace {
constexpr int kInset = 3; // between the frame and the button
} // namespace

MessageEdit::MessageEdit(QWidget *parent)
    : QPlainTextEdit(parent)
{
    m_button = new QToolButton(this);
    m_button->setObjectName(QStringLiteral("cornerButton"));
    m_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_button->setCursor(Qt::PointingHandCursor);
    m_button->setFocusPolicy(Qt::NoFocus);
    m_button->raise();
    viewport()->installEventFilter(this);
    // Typing, pasting, the agent's partials and setPlainText() all end up as
    // a document change, so one connection covers every way text arrives.
    connect(this, &QPlainTextEdit::textChanged, this, &MessageEdit::scheduleHeightCheck);
    applyTheme();
}

void MessageEdit::applyTheme()
{
    m_button->setFont(font());
    // A square, sized for the widest text the button shows, so a spinner
    // frame does not make it jiggle.
    m_button->setFixedSize(QSize());
    m_button->setMinimumSize(0, 0);
    m_button->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    const QSize hint = m_button->sizeHint();
    const int side = qMax(hint.width(), hint.height());
    m_button->setFixedSize(side, side);
    // Room for the button plus the gap on both of its sides (the top gap is
    // the frame and the padding, about kInset + the padding).
    setViewportMargins(0, 0, side + 2 * kInset + 4, 0);
    placeButton();
}

void MessageEdit::replaceText(const QString &text, bool join)
{
    m_pasting = true;
    QTextCursor c(document());
    if (join)
        c.joinPreviousEditBlock();
    else
        c.beginEditBlock();
    c.select(QTextCursor::Document);
    c.insertText(text);
    c.endEditBlock();
    m_pasting = false;
    // The subject line is what to look at first.
    QTextCursor start = textCursor();
    start.movePosition(QTextCursor::Start);
    setTextCursor(start);
}

void MessageEdit::setMessage(const QString &text)
{
    m_pasting = true;
    setPlainText(text);
    m_pasting = false;
}

// Ctrl+V, Shift+Insert, the middle button and a drop all land here.
void MessageEdit::insertFromMimeData(const QMimeData *source)
{
    m_pasting = true;
    QPlainTextEdit::insertFromMimeData(source);
    m_pasting = false;
}

// QPlainTextDocumentLayout measures its document in lines rather than pixels,
// and those are the wrapped ones: the box wraps at the viewport width, so a
// single long paragraph counts for as many lines as it takes.
int MessageEdit::contentHeight() const
{
    const int lines = qMax(1, qCeil(document()->documentLayout()->documentSize().height()));
    const QMargins margins = viewportMargins();
    return lines * fontMetrics().lineSpacing()
        + qCeil(2 * document()->documentMargin())
        + margins.top() + margins.bottom()
        + 2 * frameWidth();
}

void MessageEdit::showEvent(QShowEvent *event)
{
    QPlainTextEdit::showEvent(event);
    // Text can arrive before there is a laid-out window to measure against
    // (`--amend` fills the box at startup), so ask once more now.
    scheduleHeightCheck();
}

void MessageEdit::scheduleHeightCheck()
{
    m_pastePending = m_pastePending || m_pasting;
    if (m_heightCheckQueued)
        return;
    m_heightCheckQueued = true;
    QTimer::singleShot(0, this, [this] {
        m_heightCheckQueued = false;
        const bool pasted = m_pastePending;
        m_pastePending = false;
        const int chars = document()->characterCount();
        const bool shorter = chars < m_chars;
        m_chars = chars;
        emit contentHeightChanged(pasted ? Edit::Pasted : shorter ? Edit::Deleted : Edit::Typed);
    });
}

bool MessageEdit::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == viewport() && (event->type() == QEvent::Resize || event->type() == QEvent::Move)) {
        placeButton();
        // A narrower box wraps the same text into more lines.
        if (event->type() == QEvent::Resize)
            scheduleHeightCheck();
    }
    return QPlainTextEdit::eventFilter(watched, event);
}

// The button lives in the margin to the right of the viewport, top-aligned
// with the first line and as far from the right edge (or the scrollbar) as
// the first line is from the top.
void MessageEdit::placeButton()
{
    const QRect v = viewport()->geometry();
    int right = width();
    if (verticalScrollBar()->isVisible())
        right = verticalScrollBar()->geometry().left();
    m_button->move(right - v.top() - m_button->width(), v.top());
}
