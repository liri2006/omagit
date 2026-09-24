#pragma once

#include "GitRepo.h"

#include <QFrame>
#include <QList>
#include <QStringList>

class QTextEdit;
class QToolButton;

// The selected commit, in the card under the commit list (screens.js
// commitDetails()): its subject as the title; the short SHA, the author and
// the date on the line under it; its parents and every ref it wears on the
// next; and the body of its message below, selectable, scrolling where it
// does not fit. A ghost button in the top right corner copies the full SHA.
// Stacked, where the files table has no room, a files button at the bottom
// right asks for the commit's files instead (the rail of the Diff tab).
class CommitDetails : public QFrame
{
    Q_OBJECT
public:
    explicit CommitDetails(QWidget *parent = nullptr);

    // The commit on show, the refs it wears and how many files it touches.
    void setCommit(const Commit &commit, const QList<RefLabel> &refs, int files);
    // No commit: the three lines empty, and `message` where the body goes.
    void clear(const QString &message);
    // The window's narrow presentation: no date on the meta line, the body a
    // little shorter and the files button under it.
    void setStacked(bool on);
    void applyTheme();

    // What the card says, line by line: the tests read it, and so does a
    // screen reader through the card's accessible name and description.
    QString title() const;
    QStringList metaParts() const; // the short SHA, "author <email>", the date (not stacked)
    QString parentsText() const;   // "Parent 6bd79ab", "Parents a b", "Root commit"
    QList<RefLabel> refs() const { return m_refs; }
    QTextEdit *body() const { return m_body; }
    QToolButton *copyButton() const { return m_copy; }
    QToolButton *filesButton() const { return m_filesButton; }

    // Never less than the three lines of the header over the body's bottom
    // room (and, stacked, the files button's).
    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

signals:
    void filesRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    // The children on the design's grid, for the size and the text size of
    // the moment.
    void place();
    // The body's text in its 17 px lines.
    void setBodyText(const QString &text);
    void updateFilesButton();
    void updateAccessibleText();

    Commit m_commit;
    bool m_hasCommit = false;
    QList<RefLabel> m_refs;
    int m_files = 0;
    bool m_stacked = false;
    QTextEdit *m_body;
    QToolButton *m_copy;
    QToolButton *m_filesButton;
};
