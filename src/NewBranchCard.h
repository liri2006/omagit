#pragma once

#include "GitRepo.h"

#include <QFrame>
#include <QList>
#include <QPointer>
#include <QString>
#include <QStringList>

class BranchPicker;
class QAbstractButton;
class QCheckBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpacerItem;
class QToolButton;
class QVBoxLayout;

namespace newbranch {
class IconLine;
class StartLine;
class BlockedNote;
} // namespace newbranch

// The New branch card (Ctrl+N, the branch menu's New branch row, a commit's
// menu in the history): NAME, the field the new branch's name is typed into,
// with a red line under it when git would not take the name; FROM, where the
// branch starts — the current branch, another branch, a remote branch, a tag
// or a commit — and the commit that is; what becomes of the changed files;
// then Switch to it and Create branch. Switching carries the changes along,
// unless the start has other versions of some of them: then the card says
// which, and the branch can only be made, not switched to.
//
// Like AgentPopover it is an overlay inside the window, a child of the
// central widget outside its layouts. It hangs where the branch menu does,
// under the branch chip, since that is where the new branch shows up.
class NewBranchCard : public QFrame
{
    Q_OBJECT
public:
    // `host` is the window's central widget: the card is placed in its
    // coordinates, outside any of its layouts.
    NewBranchCard(GitRepo *repo, QWidget *host);

    // The chip the card hangs under, and the bar that chip is on (the card's
    // top is ui::popupTop() of it).
    void setAnchor(QWidget *anchor, QWidget *bar);
    QWidget *anchor() const { return m_anchor; }

    // The card's parts, for the tests.
    QLineEdit *nameField() const { return m_name; }
    BranchPicker *startPicker() const { return m_picker; }
    QCheckBox *switchBox() const { return m_switch; }
    QPushButton *createButton() const { return m_create; }
    // The red line's Switch to it, for a name a local branch has already.
    QAbstractButton *switchToExistingButton() const;
    // What the red line under the name says; empty while it is hidden.
    QString errorText() const;
    // The caption note beside FROM ("the current branch", …).
    QString fromNote() const;
    // What the start line says: the short hash (none for a commit start, the
    // picker naming it already) and the subject.
    QString startSha() const;
    QString startSubject() const;
    // The note about the changed files: the line they come along with, or
    // the warning card's title and lines; empty where there is none.
    QString carryNote() const;
    QWidget *blockedNote() const;
    QStringList blockedLines() const;
    // What git is given as the start: empty for the current branch.
    QString start() const { return m_start.ref; }

    // Re-measures the design's pixels at the text size of the moment.
    void applyTheme();
    // The repository read again while the card is open (the window's
    // refresh): the branches and the tags the name is checked against, the
    // start described again — the current branch where it is gone — and
    // what becomes of the changed files. The name, the caret, Switch to it as
    // the user left it and the keyboard stay where they are.
    void reload();

public slots:
    // Opens the card with `name` in the field (spaces turned into dashes)
    // and `start` as where the branch starts: a branch, a remote branch, a
    // tag, a commit — or, empty, the current branch (HEAD when detached).
    // Open already, it starts over with the two.
    void popup(const QString &start = QString(), const QString &name = QString());
    // Hides the card, the keyboard back where it was before it opened; the
    // one way it closes, whatever closes it.
    void dismiss();

signals:
    void opened();
    void dismissed();
    // The branch is made, and checked out where `switched`; `shortSha` is
    // the commit it points at. The card has closed.
    void created(const QString &name, const QString &shortSha, bool switched);
    // The red line's Switch to it: the existing branch `name`. The card has
    // closed.
    void switchRequested(const QString &name);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    // Return creates, wherever in the card the keyboard is.
    void keyPressEvent(QKeyEvent *event) override;

private:
    // Where the branch starts, as resolved from what popup() or the From
    // menu gave.
    struct Start {
        enum Kind { Branch, Remote, Tag, Commit };
        Kind kind = Branch;
        QString ref;         // what git is given; empty for the current branch
        QString label;       // what the picker shows
        bool current = false; // the checked-out branch
        bool picked = false;  // a commit handed over from the history
        QString sha, subject; // the commit it is
        QStringList blocked;  // changed files that differ there
    };

    void build();
    // What the card works from, as the repository stands now.
    void readRepository();
    // A caption on its 16 px line, a note at its right when there is one.
    QHBoxLayout *captionRow(const QString &caption, QLabel *note);
    // A gap of `px` design pixels, less `less` pixels of a hairline standing
    // in it, re-measured by applyTheme().
    void addGap(int px, int less = 0);

    void setStart(const QString &ref, bool picked);
    // The From menu: the branches, the remote branches and the tags.
    void pickStart();
    // The name as it stands against the branches: the red line and Create.
    void validate();
    // Everything the start decides: the picker, the lines under it, the
    // note about the changes, Switch to it.
    void showStart();
    void create();
    // "origin/feature/x" → "feature/x", the remote known by its name.
    QString localName(const QString &remoteBranch) const;

    void place();
    void scheduleLayout();

    GitRepo *m_repo;
    QPointer<QWidget> m_anchor;
    QPointer<QWidget> m_bar;
    QList<QPointer<QWidget>> m_watched; // the anchor and its parents up to the host
    QPointer<QWidget> m_returnFocus;    // who had the keyboard before the card opened

    // What the card was opened on.
    BranchList m_branches;
    QStringList m_tags;
    bool m_hasHead = true;
    QStringList m_dirty; // the changed paths, the start's blockers are found among
    int m_changed = 0;   // the changed files, a rename once
    Start m_start;
    bool m_switchWanted = true; // Switch to it as the user left it, where it is theirs to set
    QString m_createError;      // git's first line after a failed create, until the name changes

    QVBoxLayout *m_layout;
    struct Gap {
        QSpacerItem *spacer;
        int px;   // design px
        int less; // px of a hairline inside it
    };
    QList<Gap> m_gaps;
    QList<QLabel *> m_captions; // the captions and the FROM note, on their 16 px lines
    QLineEdit *m_name;
    QWidget *m_errorRow;          // 4 under the field, a 24 px line
    newbranch::IconLine *m_error;
    QToolButton *m_switchToExisting;
    QLabel *m_fromNote;
    BranchPicker *m_picker;
    newbranch::StartLine *m_startLine;
    QWidget *m_noteBox;           // 16 under the start line: one of the two below
    newbranch::IconLine *m_carry;
    newbranch::BlockedNote *m_blocked;
    QWidget *m_actionRow;
    QCheckBox *m_switch;
    QPushButton *m_create;

    bool m_layoutQueued = false;
    bool m_placing = false;
};
