#pragma once

#include <QDialog>

class QGridLayout;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QTextEdit;

// What the window has to say that cannot wait in the footer: a git command
// that failed, a commit that needs a message, a change about to be thrown
// away. Built like the other dialogs (the sign-in, the merge view) instead of
// a QMessageBox, which insists on a width of its own: the dialog is never
// wider than the window it opens over, because the compositor centres it on
// that window and a window tiled at the screen's edge would otherwise leave
// part of it off the screen.
//
// A glyph in the kind's colour beside the title and, under the title, the
// text — git's output as often as not, so it can be selected, breaks inside
// a long URL or path when it has to, and scrolls past a dozen lines or so.
class MessageDialog : public QDialog
{
    Q_OBJECT
public:
    enum Kind { Error, Warning, Question };

    MessageDialog(Kind kind, const QString &title, const QString &text, QWidget *parent = nullptr);

    // A question's own button, beside Cancel. Cancel stays the default: what
    // is asked here (throwing changes away, rewriting published history) is
    // not easily taken back.
    void setAcceptText(const QString &text);

    Kind kind() const { return m_kind; }
    QString title() const;
    QString text() const;
    // The button Return presses: OK, or a question's Cancel.
    QPushButton *defaultButton() const;

    // Modal over `parent`'s window, like the QMessageBox functions they replace.
    static void error(QWidget *parent, const QString &title, const QString &text);
    static void warning(QWidget *parent, const QString &title, const QString &text);
    // Whether the button named `acceptText` was the answer.
    static bool confirm(QWidget *parent, const QString &title, const QString &text, const QString &acceptText);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void applyTheme();
    void fitToContent();

    Kind m_kind;
    QGridLayout *m_grid;
    QHBoxLayout *m_buttonRow;
    QLabel *m_icon, *m_title;
    QTextEdit *m_text;
    QPushButton *m_cancelButton = nullptr, *m_acceptButton;
    QString m_iconColor; // the stylesheet in force on the glyph
};
