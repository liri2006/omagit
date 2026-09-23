#pragma once

#include <QPlainTextEdit>

class QToolButton;

// The commit message box with a button in its top right corner, the way an
// editor's commit input carries its "generate message" button. The text
// keeps clear of the button: the viewport ends where the button's column
// starts, so the button never sits on a line of the message (or on the
// scrollbar).
class MessageEdit : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit MessageEdit(QWidget *parent = nullptr);

    QToolButton *cornerButton() const { return m_button; }
    // Re-measures the button after a font change.
    void applyTheme();

    // The height the box would need to show the whole message at its current
    // width, wrapped lines counted one by one. Whoever decides how tall the
    // box may be (CommitPage, which owns the splitter) grows it to this.
    int contentHeight() const;

    // Replaces the text as one undoable edit (Ctrl+Z brings the old text
    // back); with `join`, merged into the previous replacement so a stream
    // of partial answers is one undo step.
    void replaceText(const QString &text, bool join = false);
    // Puts a message the app has for the user (the commit being amended, a
    // merge's) into the box; counts as pasted, not typed.
    void setMessage(const QString &text);

    // What a burst of edits amounted to: text typed in (or re-wrapped by a
    // resize), text that came in one go (a paste, a drop, the agent,
    // setMessage()), or text taken out so the message got shorter.
    enum class Edit { Typed, Pasted, Deleted };
    Q_ENUM(Edit)

signals:
    // contentHeight() may have changed. Emitted from the event loop, not from
    // the document's layout pass, and once per burst of edits: the agent
    // streams a message in many partial answers.
    void contentHeightChanged(MessageEdit::Edit edit);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void changeEvent(QEvent *event) override;
    void insertFromMimeData(const QMimeData *source) override;

private:
    void placeButton();
    void scheduleHeightCheck();
    // Two boxes may edit one document (the commit page's and the Mini
    // layout's popover), and its layout wraps at one width only: the one the
    // box shown last has to be the one it wraps for.
    void scheduleWrapClaim();
    void claimWrapWidth();

    QToolButton *m_button;
    bool m_pasting = false;          // inside a paste-like edit
    bool m_pastePending = false;     // the queued check saw one
    bool m_heightCheckQueued = false;
    bool m_wrapClaimQueued = false;
    int m_chars = 0;                 // characterCount() at the last check
};
