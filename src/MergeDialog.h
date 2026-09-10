#pragma once

#include "GitRepo.h"

#include <QCoreApplication>
#include <QDialog>

class BranchPicker;
class VerdictCard;
class QCheckBox;
class QGridLayout;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QTimer;
class QToolButton;

// What the merge view has to say about a merge: the wording of the verdict
// card and what the Merge button makes of it. A plain value — worked out
// from a preview by mergeVerdict() and then only shown — so the wording can
// be read (and changed) in one place.
struct MergeVerdict {
    Q_DECLARE_TR_FUNCTIONS(MergeDialog)
public:
    enum Kind {
        Checking, // the spinner: the answer is still being worked out
        Info,     // nothing to merge, or nothing to merge into
        Good,     // the merge would go through
        Bad       // conflicts, a failure, or a merge already in progress
    };

    Kind kind = Info;
    QString headline;
    QStringList detail; // one line each
    QStringList files;  // the paths listed under the detail
    QString warning;    // beside the warning sign at the foot of the card
    QString buttonTip;  // of the Merge button
    bool canMerge = false;

    // The spinner with a message, and a failure with git's own words.
    static MergeVerdict checking(const QString &headline);
    static MergeVerdict problem(const QString &headline, const QString &detail);
};

// The verdict for `preview`, with the "always create a merge commit" box in
// mind. `currentBranch` is the checked-out one: another destination is
// checked out first, which the detail says.
MergeVerdict mergeVerdict(const MergePreview &preview, bool noFastForward, const QString &currentBranch);

// The merge view: the branch to merge on the left, the branch it goes into
// on the right (the current one to begin with), a swap button between
// them, and underneath the verdict on what `git merge` would do —
// fast-forward, a clean merge commit, or conflicts, naming the files —
// worked out on the trees alone so nothing happens to the working tree
// until Merge is pressed. Opened while a merge waits with conflicts, it
// shows those and offers to abort instead.
class MergeDialog : public QDialog
{
    Q_OBJECT
public:
    explicit MergeDialog(GitRepo *repo, QWidget *parent = nullptr);

    QString source() const { return m_source; }
    QString destination() const { return m_destination; }
    void setBranches(QString source, QString destination);

public slots:
    // Merge the other way round.
    void swap();

signals:
    // A merge ran: `conflicts` files are left to resolve (0: it went through).
    void merged(const QString &source, const QString &destination, int conflicts, bool fastForward);
    // The merge that was in progress was abandoned.
    void mergeAborted(const QString &source, const QString &destination);

protected:
    void showEvent(QShowEvent *e) override;

private:
    void buildUi();
    QGridLayout *buildBranchRow();
    QCheckBox *buildNoFastForwardBox();
    QHBoxLayout *buildButtonRow();
    void applyTheme();
    void updatePickers();
    void pickSource();
    void pickDestination();
    QString defaultSource(const QString &destination) const;
    void schedulePreview();
    void runPreview();
    void applyPreview(const MergePreview &preview, int generation);
    void showPreview();
    void showMergeState();
    void setVerdict(const MergeVerdict &verdict);
    void setBusy(bool busy);
    void fitToContent();
    bool checkoutDestination(const QString &destination);
    void startMerge();
    void finishMerge(const QString &source, const QString &destination, bool fastForward,
                     GitRepo::MergeResult result, const QString &error);
    void abortMerge();

    GitRepo *m_repo;
    BranchList m_branches;
    QString m_source, m_destination;
    MergePreview m_preview;
    bool m_previewReady = false; // m_preview describes the branches on screen
    int m_generation = 0;        // preview runs still in flight are told apart by this
    QTimer *m_debounce;
    MergeState m_state; // the merge already in progress when the view opened
    bool m_merging = false;
    int m_verdictHeight = 0; // of the card showing the last verdict, held while the next is checked

    QLabel *m_sourceCaption, *m_destinationCaption;
    BranchPicker *m_sourcePicker, *m_destinationPicker;
    QToolButton *m_swapButton;
    VerdictCard *m_card;
    QCheckBox *m_noFastForward;
    QPushButton *m_mergeButton, *m_cancelButton, *m_abortButton;
};
