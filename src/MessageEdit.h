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

    // Replaces the text as one undoable edit (Ctrl+Z brings the old text
    // back); with `join`, merged into the previous replacement so a stream
    // of partial answers is one undo step.
    void replaceText(const QString &text, bool join = false);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void placeButton();

    QToolButton *m_button;
};
