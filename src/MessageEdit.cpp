#include "MessageEdit.h"

#include <QEvent>
#include <QScrollBar>
#include <QTextCursor>
#include <QToolButton>

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
    QTextCursor c(document());
    if (join)
        c.joinPreviousEditBlock();
    else
        c.beginEditBlock();
    c.select(QTextCursor::Document);
    c.insertText(text);
    c.endEditBlock();
    // The subject line is what to look at first.
    QTextCursor start = textCursor();
    start.movePosition(QTextCursor::Start);
    setTextCursor(start);
}

bool MessageEdit::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == viewport() && (event->type() == QEvent::Resize || event->type() == QEvent::Move))
        placeButton();
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
