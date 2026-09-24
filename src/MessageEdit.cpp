#include "MessageEdit.h"
#include "UiHelpers.h"

#include <QAbstractTextDocumentLayout>
#include <QEvent>
#include <QScrollBar>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QtMath>

namespace {
// The design's generate button (screens.js changesPage(), the MessageBox
// group), in 12 px-base pixels: a 24 px square whose right edge is 2 px
// inside the box's and whose top is 2 px under the box's top.
constexpr int kButtonSide = 24;
constexpr int kInset = 2;
constexpr int kTextGap = 4; // between the text's right edge and the button's column
constexpr int kClaimWidth = 1 << 16; // wider than any other box of the document can be
} // namespace

MessageEdit::MessageEdit(QWidget *parent)
    : QPlainTextEdit(parent)
{
    m_button = new ui::GlyphButton(this);
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
    // The design's square, whatever the button shows: the sparkle or a
    // spinner frame, so a frame does not make it jiggle.
    const int side = ui::space(kButtonSide);
    m_button->setFixedSize(side, side);
    // The text keeps clear of the button's column: the button, its inset
    // and a gap before the text.
    setViewportMargins(0, 0, side + ui::space(kInset) + ui::space(kTextGap), 0);
    placeButton();
    // A theme change re-measures every box of a shared document, the hidden
    // ones too; the one on screen takes the wrapping back afterwards.
    scheduleWrapClaim();
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
    scheduleWrapClaim();
}

// A new font re-wraps the document in every box that shows it, the hidden one
// too, which takes the wrapping width over when it is the wider of the two.
void MessageEdit::changeEvent(QEvent *event)
{
    QPlainTextEdit::changeEvent(event);
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
        scheduleWrapClaim();
}

// After whatever else the same turn of the event loop does to the fonts and
// the geometry, so the box on screen has the last word.
void MessageEdit::scheduleWrapClaim()
{
    if (m_wrapClaimQueued)
        return;
    m_wrapClaimQueued = true;
    QTimer::singleShot(0, this, [this] {
        m_wrapClaimQueued = false;
        claimWrapWidth();
    });
}

// QPlainTextDocumentLayout takes its width from the first box that laid it
// out, and from another one only when that one is wider — so a narrower box
// sharing the document would wrap at the other box's width. Being briefly
// wider than anything makes this box the one the layout listens to; the real
// width, set right after in the same call, is then the width it wraps at.
// Nothing is painted in between.
void MessageEdit::claimWrapWidth()
{
    if (!isVisible())
        return;
    const QSize size = this->size();
    resize(kClaimWidth, size.height());
    resize(size);
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

// The button lives in the margin to the right of the viewport, in the box's
// top right corner: 2 px inside its right edge (or the scrollbar's left one)
// and 2 px under its top.
void MessageEdit::placeButton()
{
    int right = width();
    if (verticalScrollBar()->isVisible())
        right = verticalScrollBar()->geometry().left();
    const int inset = ui::space(kInset);
    m_button->move(right - inset - m_button->width(), inset);
}
