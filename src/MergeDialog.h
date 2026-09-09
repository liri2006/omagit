#pragma once

#include "GitRepo.h"

#include <QDialog>
#include <QList>

class BranchPicker;
class QCheckBox;
class QFrame;
class QLabel;
class QListWidget;
class QPushButton;
class QThread;
class QTimer;
class QToolButton;

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
    ~MergeDialog() override;

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
    enum Kind { Checking, Info, Good, Bad };

    void buildUi();
    void applyTheme();
    void updatePickers();
    void pickSource();
    void pickDestination();
    QString defaultSource(const QString &destination) const;
    void schedulePreview();
    void runPreview();
    void showPreview();
    void showMergeState();
    void setVerdict(Kind kind, const QString &headline, const QString &detail, const QStringList &files = QStringList(),
                    const QString &warning = QString());
    void setBusy(bool busy);
    int typicalVerdictHeight() const;
    void fitToContent();
    void startMerge();
    void abortMerge();

    GitRepo *m_repo;
    BranchList m_branches;
    QString m_source, m_destination;
    MergePreview m_preview;
    bool m_previewReady = false; // m_preview describes the branches on screen
    int m_generation = 0;        // preview runs still in flight are told apart by this
    QList<QThread *> m_threads;
    QTimer *m_debounce;
    QTimer *m_spinner;
    int m_spinnerFrame = 0;
    MergeState m_state; // the merge already in progress when the view opened
    bool m_merging = false;
    int m_verdictHeight = 0; // of the card showing the last verdict, held while the next is checked

    QLabel *m_sourceCaption, *m_destinationCaption;
    BranchPicker *m_sourcePicker, *m_destinationPicker;
    QToolButton *m_swapButton;
    QFrame *m_verdict;
    QLabel *m_verdictIcon, *m_headline, *m_detail, *m_warningIcon, *m_warning;
    QWidget *m_warningRow;
    QListWidget *m_files;
    QCheckBox *m_noFastForward;
    QPushButton *m_mergeButton, *m_cancelButton, *m_abortButton;
};
