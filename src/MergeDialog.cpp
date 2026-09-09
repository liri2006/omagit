#include "MergeDialog.h"
#include "BranchMenu.h"
#include "OmarchyTheme.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QShowEvent>
#include <QStyleOptionToolButton>
#include <QStylePainter>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
// md-source_branch, md-chevron_down, md-swap_horizontal, md-source_merge
constexpr uint kBranch = 0xF062C, kChevron = 0xF0140, kSwap = 0xF04E1, kMerge = 0xF062D;
// md-check_circle, md-alert_circle, md-alert, md-information, md-close
constexpr uint kGood = 0xF05E0, kBad = 0xF0028, kWarn = 0xF0026, kInfo = 0xF02FC;
const auto kNoFastForwardSetting = QStringLiteral("merge/noFastForward");

QString glyph(uint cp, const QString &fallback)
{
    const QString g = OmarchyTheme::instance()->glyph(cp);
    return g.isEmpty() ? fallback : g;
}

// Glyph plus the two spaces the toolbar buttons put after theirs.
QString icon(uint cp, const QString &fallback = QString())
{
    const QString g = OmarchyTheme::instance()->glyph(cp);
    return g.isEmpty() ? fallback : g + QStringLiteral("  ");
}

QLabel *sectionLabel(const QString &text)
{
    auto *l = new QLabel(text.toUpper());
    l->setObjectName(QStringLiteral("sectionLabel"));
    l->setFont(OmarchyTheme::instance()->captionFont());
    return l;
}

QStringList spinnerFrames(const QFont &font)
{
    const QFontMetrics fm(font);
    if (fm.inFont(QChar(0x280B)))
        return {QStringLiteral("⠋"), QStringLiteral("⠙"), QStringLiteral("⠹"), QStringLiteral("⠸"), QStringLiteral("⠼"),
                QStringLiteral("⠴"), QStringLiteral("⠦"), QStringLiteral("⠧"), QStringLiteral("⠇"), QStringLiteral("⠏")};
    if (fm.inFont(QChar(0x25D0)))
        return {QStringLiteral("◐"), QStringLiteral("◓"), QStringLiteral("◑"), QStringLiteral("◒")};
    return {QStringLiteral("|"), QStringLiteral("/"), QStringLiteral("-"), QStringLiteral("\\")};
}

// "1 commit" / "2 commits"
QString counted(int n, const QString &one, const QString &many)
{
    return QString::number(n) + QLatin1Char(' ') + (n == 1 ? one : many);
}

// "a, b, c and 4 more"
QString fewOf(const QStringList &list, int limit)
{
    if (list.size() <= limit)
        return list.join(QStringLiteral(", "));
    return QCoreApplication::translate("MergeDialog", "%1 and %2 more")
        .arg(list.mid(0, limit).join(QStringLiteral(", ")))
        .arg(list.size() - limit);
}
} // namespace

// One side of the merge: a field-like button showing the branch (glyph,
// name in the title font, a chevron at the right edge) that drops the
// branch list down on click.
class BranchPicker : public QToolButton
{
public:
    explicit BranchPicker(QWidget *parent = nullptr)
        : QToolButton(parent)
    {
        setObjectName(QStringLiteral("branchPicker"));
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::TabFocus);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setBranch(const QString &name, bool remote)
    {
        m_name = name;
        m_remote = remote;
        setAccessibleName(name);
        updateGeometry();
        update();
    }
    QString branch() const { return m_name; }

    QSize sizeHint() const override
    {
        const OmarchyTheme *t = OmarchyTheme::instance();
        const int h = QFontMetrics(t->titleFont()).height() + 2 * kPad + 2;
        return QSize(260, h);
    }
    QSize minimumSizeHint() const override { return QSize(120, sizeHint().height()); }

protected:
    // Like a combo box: Enter goes to the dialog's Merge button, the list
    // opens on Space, Down or Alt+Down.
    void keyPressEvent(QKeyEvent *e) override
    {
        switch (e->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            e->ignore();
            return;
        case Qt::Key_Down:
        case Qt::Key_F4:
            click();
            e->accept();
            return;
        default:
            QToolButton::keyPressEvent(e);
        }
    }
    void keyReleaseEvent(QKeyEvent *e) override
    {
        if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
            e->ignore();
            return;
        }
        QToolButton::keyReleaseEvent(e);
    }

    void paintEvent(QPaintEvent *) override
    {
        const OmarchyTheme *t = OmarchyTheme::instance();
        QStylePainter p(this);
        QStyleOptionToolButton opt;
        initStyleOption(&opt);
        opt.text.clear();
        opt.icon = QIcon();
        p.drawComplexControl(QStyle::CC_ToolButton, opt); // the stylesheet's box and states
        const bool on = isEnabled();
        const QColor dim = on ? t->mutedText() : t->fill(0.45);
        const QRect r = rect().adjusted(kPad + 4, 0, -(kPad + 4), 0);
        p.setFont(t->uiFont());
        const QString mark = glyph(kBranch, QString());
        int x = r.left();
        if (!mark.isEmpty()) {
            p.setPen(dim);
            p.drawText(QRect(x, r.top(), p.fontMetrics().horizontalAdvance(mark), r.height()), Qt::AlignVCenter, mark);
            x += p.fontMetrics().horizontalAdvance(mark) + 10;
        }
        const QString chevron = glyph(kChevron, QStringLiteral("▾"));
        const int chevronW = p.fontMetrics().horizontalAdvance(chevron);
        p.setPen(dim);
        p.drawText(QRect(r.right() - chevronW, r.top(), chevronW, r.height()), Qt::AlignVCenter, chevron);
        p.setFont(t->titleFont());
        const QRect nameRect(x, r.top(), r.right() - chevronW - 10 - x, r.height());
        if (m_name.isEmpty()) {
            p.setPen(dim);
            p.drawText(nameRect, Qt::AlignVCenter, tr("No branch"));
            return;
        }
        p.setPen(on ? t->accent() : t->fill(0.45));
        p.drawText(nameRect, Qt::AlignVCenter, p.fontMetrics().elidedText(m_name, Qt::ElideMiddle, nameRect.width()));
    }

private:
    static constexpr int kPad = 8;
    QString m_name;
    bool m_remote = false;
};

MergeDialog::MergeDialog(GitRepo *repo, QWidget *parent)
    : QDialog(parent), m_repo(repo)
{
    setWindowTitle(tr("Merge"));
    setObjectName(QStringLiteral("mergeDialog"));
    setWindowModality(Qt::WindowModal);
    setAttribute(Qt::WA_DeleteOnClose);
    setSizeGripEnabled(false);

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(120);
    connect(m_debounce, &QTimer::timeout, this, &MergeDialog::runPreview);
    m_spinner = new QTimer(this);
    m_spinner->setInterval(80);
    connect(m_spinner, &QTimer::timeout, this, [this] {
        const QStringList frames = spinnerFrames(m_verdictIcon->font());
        m_spinnerFrame = (m_spinnerFrame + 1) % frames.size();
        m_verdictIcon->setText(frames.at(m_spinnerFrame));
    });

    buildUi();
    applyTheme();
    connect(OmarchyTheme::instance(), &OmarchyTheme::changed, this, &MergeDialog::applyTheme);

    m_branches = m_repo->branches();
    m_state = m_repo->mergeState();
    if (m_state.inProgress) {
        m_source = m_state.source;
        m_destination = m_branches.current;
        updatePickers();
        showMergeState();
    } else {
        QString destination = m_branches.current;
        if (destination.isEmpty()) // detached HEAD: the main line, else the first branch
            destination = m_repo->defaultBranch().isEmpty() ? m_branches.local.value(0) : m_repo->defaultBranch();
        if (m_branches.isRemote(destination))
            destination.clear();
        setBranches(defaultSource(destination), destination);
    }
}

MergeDialog::~MergeDialog()
{
    // A preview still running holds nothing of this object, but a QThread
    // must not be destroyed while it runs.
    for (QThread *t : std::as_const(m_threads))
        t->wait();
}

void MergeDialog::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(10);

    // MERGE [source ▾]  ⇄  INTO [destination ▾]
    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(6);
    m_sourceCaption = sectionLabel(tr("Merge"));
    m_destinationCaption = sectionLabel(tr("Into"));
    m_sourcePicker = new BranchPicker;
    m_sourcePicker->setToolTip(tr("The branch whose commits are brought in — click to pick another"));
    connect(m_sourcePicker, &QToolButton::clicked, this, &MergeDialog::pickSource);
    m_destinationPicker = new BranchPicker;
    m_destinationPicker->setToolTip(tr("The branch that receives them — checked out first when it is not the current one"));
    connect(m_destinationPicker, &QToolButton::clicked, this, &MergeDialog::pickDestination);
    m_swapButton = new QToolButton;
    m_swapButton->setObjectName(QStringLiteral("swapButton"));
    m_swapButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_swapButton->setCursor(Qt::PointingHandCursor);
    m_swapButton->setFocusPolicy(Qt::TabFocus);
    m_swapButton->setToolTip(tr("Swap the two sides — merge the other way round (Ctrl+S)"));
    m_swapButton->setAccessibleName(tr("Swap"));
    connect(m_swapButton, &QToolButton::clicked, this, &MergeDialog::swap);
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_S), this, this, &MergeDialog::swap);
    grid->addWidget(m_sourceCaption, 0, 0);
    grid->addWidget(m_destinationCaption, 0, 2);
    grid->addWidget(m_sourcePicker, 1, 0);
    grid->addWidget(m_swapButton, 1, 1, Qt::AlignCenter);
    grid->addWidget(m_destinationPicker, 1, 2);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(2, 1);
    layout->addLayout(grid);
    layout->addSpacing(2);

    // The verdict: icon, headline, detail, the files, a warning.
    m_verdict = new QFrame;
    m_verdict->setObjectName(QStringLiteral("mergeVerdict"));
    auto *card = new QGridLayout(m_verdict);
    card->setContentsMargins(14, 12, 14, 12);
    card->setHorizontalSpacing(12);
    card->setVerticalSpacing(6);
    m_verdictIcon = new QLabel;
    m_verdictIcon->setObjectName(QStringLiteral("bigLabel"));
    m_verdictIcon->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    m_verdictIcon->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_headline = new QLabel;
    m_headline->setWordWrap(true);
    m_headline->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_detail = new QLabel;
    m_detail->setObjectName(QStringLiteral("dimLabel"));
    m_detail->setWordWrap(true);
    m_detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_files = new QListWidget;
    m_files->setObjectName(QStringLiteral("mergeFiles"));
    m_files->setSelectionMode(QAbstractItemView::NoSelection);
    m_files->setFocusPolicy(Qt::NoFocus);
    m_files->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_files->setTextElideMode(Qt::ElideMiddle);
    m_files->setUniformItemSizes(true);
    m_files->hide();
    m_warningRow = new QWidget;
    auto *warningLayout = new QHBoxLayout(m_warningRow);
    warningLayout->setContentsMargins(0, 2, 0, 0);
    warningLayout->setSpacing(8);
    m_warningIcon = new QLabel;
    m_warningIcon->setAlignment(Qt::AlignTop);
    m_warning = new QLabel;
    m_warning->setObjectName(QStringLiteral("captionLabel"));
    m_warning->setWordWrap(true);
    m_warning->setTextInteractionFlags(Qt::TextSelectableByMouse);
    warningLayout->addWidget(m_warningIcon);
    warningLayout->addWidget(m_warning, 1);
    m_warningRow->hide();
    card->addWidget(m_verdictIcon, 0, 0, 2, 1, Qt::AlignTop);
    card->addWidget(m_headline, 0, 1);
    card->addWidget(m_detail, 1, 1);
    card->addWidget(m_files, 2, 1);
    card->addWidget(m_warningRow, 3, 1);
    card->setColumnStretch(1, 1);
    // Spare height (the card held at its previous size while checking) goes
    // below the text, so the spinner and its message stay at the top together.
    card->setRowStretch(4, 1);
    layout->addWidget(m_verdict);

    m_noFastForward = new QCheckBox(tr("Always create a merge commit"));
    m_noFastForward->setToolTip(tr("git merge --no-ff: record a merge commit even when the branch could simply move up"));
    m_noFastForward->setChecked(QSettings().value(kNoFastForwardSetting, false).toBool());
    connect(m_noFastForward, &QCheckBox::toggled, this, [this](bool on) {
        QSettings().setValue(kNoFastForwardSetting, on);
        if (m_previewReady)
            showPreview();
    });
    layout->addWidget(m_noFastForward);
    layout->addSpacing(4);
    // All spare height belongs between the preview controls and the footer.
    // Otherwise QBoxLayout distributes it among the header and verdict rows.
    layout->addStretch(1);

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(10);
    m_abortButton = new QPushButton(tr("Abort merge"));
    m_abortButton->setCursor(Qt::PointingHandCursor);
    m_abortButton->setToolTip(tr("git merge --abort: the branch and the working tree go back to how they were"));
    m_abortButton->hide();
    connect(m_abortButton, &QPushButton::clicked, this, &MergeDialog::abortMerge);
    m_cancelButton = new QPushButton(tr("Cancel"));
    m_cancelButton->setCursor(Qt::PointingHandCursor);
    m_cancelButton->setAutoDefault(false);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    m_mergeButton = new QPushButton;
    m_mergeButton->setCursor(Qt::PointingHandCursor);
    m_mergeButton->setDefault(true);
    m_mergeButton->setEnabled(false);
    connect(m_mergeButton, &QPushButton::clicked, this, &MergeDialog::startMerge);
    buttons->addWidget(m_abortButton);
    buttons->addStretch();
    buttons->addWidget(m_cancelButton);
    buttons->addWidget(m_mergeButton);
    layout->addLayout(buttons);

    // The width is fixed; the height follows the content (fitToContent()).
    setFixedWidth(640);
    setTabOrder(m_sourcePicker, m_swapButton);
    setTabOrder(m_swapButton, m_destinationPicker);
    setTabOrder(m_destinationPicker, m_noFastForward);
    setTabOrder(m_noFastForward, m_mergeButton);
    setTabOrder(m_mergeButton, m_cancelButton);
    m_sourcePicker->setFocus();
}

void MergeDialog::applyTheme()
{
    const OmarchyTheme *t = OmarchyTheme::instance();
    for (QLabel *l : {m_sourceCaption, m_destinationCaption, m_detail})
        l->setFont(t->captionFont());
    QFont bold = t->uiFont();
    bold.setBold(true);
    m_headline->setFont(bold);
    QFont big = t->uiFont();
    big.setPixelSize(qRound(t->fontBase() * 1.5));
    m_verdictIcon->setFont(big);
    m_verdictIcon->setFixedWidth(QFontMetrics(big).horizontalAdvance(glyph(kGood, QStringLiteral("✓"))) + 4);
    m_warningIcon->setFont(t->uiFont());
    m_warningIcon->setText(glyph(kWarn, QStringLiteral("!")));
    m_warningIcon->setStyleSheet(QStringLiteral("color: %1;").arg(t->color(QStringLiteral("yellow")).name()));
    m_warning->setFont(t->captionFont());
    m_files->setFont(t->monoFont());
    m_swapButton->setText(glyph(kSwap, QStringLiteral("⇄")));
    m_swapButton->setFixedSize(m_sourcePicker->sizeHint().height(), m_sourcePicker->sizeHint().height());
    m_mergeButton->setText(icon(kMerge) + tr("Merge"));
    m_abortButton->setText(tr("Abort merge"));
    updatePickers();
    if (m_state.inProgress)
        showMergeState();
    else if (m_previewReady)
        showPreview();
}

void MergeDialog::setBranches(QString source, QString destination)
{
    if (m_state.inProgress)
        return;
    // Own both arguments before assigning: swap() passes our members in reverse.
    m_source = source;
    m_destination = destination;
    updatePickers();
    m_previewReady = false;
    schedulePreview();
}

void MergeDialog::swap()
{
    if (m_state.inProgress || m_merging || !m_swapButton->isEnabled())
        return;
    setBranches(m_destination, m_source);
}

// A remote branch can be merged from but not into: the swap waits until
// both sides are local.
void MergeDialog::updatePickers()
{
    m_sourcePicker->setBranch(m_source, m_branches.isRemote(m_source));
    m_destinationPicker->setBranch(m_destination, false);
    const bool frozen = m_state.inProgress || m_merging;
    m_sourcePicker->setEnabled(!frozen);
    m_destinationPicker->setEnabled(!frozen);
    const bool swappable = !frozen && !m_source.isEmpty() && !m_destination.isEmpty() && !m_branches.isRemote(m_source);
    m_swapButton->setEnabled(swappable);
    m_swapButton->setToolTip(!frozen && m_branches.isRemote(m_source)
                                 ? tr("%1 is a remote branch: it can be merged from, not into").arg(m_source)
                                 : tr("Swap the two sides — merge the other way round (Ctrl+S)"));
    m_noFastForward->setEnabled(!frozen);
}

// The branch most likely wanted: the main line (main, or what origin/HEAD
// points at) unless that is the destination, then the local branch
// committed to most recently, then a remote one.
QString MergeDialog::defaultSource(const QString &destination) const
{
    const QString mainLine = m_repo->defaultBranch();
    if (!mainLine.isEmpty() && mainLine != destination)
        return mainLine;
    for (const QString &name : m_repo->branchesByActivity())
        if (name != destination)
            return name;
    for (const QString &name : m_branches.remote)
        if (name.section(QLatin1Char('/'), 1) != destination)
            return name;
    return QString();
}

void MergeDialog::pickSource()
{
    BranchMenu menu(this);
    menu.setBranches(m_branches, m_source, true, [this](const QString &name, bool) {
        return m_destination.isEmpty() ? tr("Merge %1").arg(name) : tr("Merge %1 into %2").arg(name, m_destination);
    }, m_destination);
    connect(&menu, &BranchMenu::picked, this, [this](const QString &name) { setBranches(name, m_destination); });
    menu.popupAt(m_sourcePicker, false);
}

void MergeDialog::pickDestination()
{
    BranchMenu menu(this);
    menu.setBranches(m_branches, m_destination, false, [this](const QString &name, bool) {
        QString tip = m_source.isEmpty() ? tr("Merge into %1").arg(name) : tr("Merge %1 into %2").arg(m_source, name);
        if (name != m_branches.current)
            tip += tr(" — %1 is checked out first").arg(name);
        return tip;
    }, m_source);
    connect(&menu, &BranchMenu::picked, this, [this](const QString &name) { setBranches(m_source, name); });
    menu.popupAt(m_destinationPicker, false);
}

// --- The verdict -----------------------------------------------------------

void MergeDialog::schedulePreview()
{
    // Invalidate in-flight results now, including during the debounce interval.
    ++m_generation;
    m_debounce->stop();
    if (m_source.isEmpty() || m_destination.isEmpty()) {
        m_preview = MergePreview();
        m_preview.error = m_branches.local.size() < 2 && m_branches.remote.isEmpty()
            ? tr("There is no other branch to merge.")
            : tr("Pick a branch on both sides.");
        m_previewReady = true;
        showPreview();
        return;
    }
    setVerdict(Checking, tr("Checking what merging %1 into %2 would do…").arg(m_source, m_destination), QString());
    m_mergeButton->setEnabled(false);
    m_debounce->start();
}

// The check runs git a few times (merge-tree among them, which can take a
// moment on a large repository): off the UI thread, results told apart by
// generation so a stale one is dropped.
void MergeDialog::runPreview()
{
    const int generation = m_generation;
    const QString source = m_source, destination = m_destination;
    GitRepo *repo = m_repo;
    auto *result = new MergePreview;
    QThread *thread = QThread::create([repo, source, destination, result] { *result = repo->mergePreview(source, destination); });
    m_threads << thread;
    connect(thread, &QThread::finished, this, [this, thread, result, generation] {
        m_threads.removeAll(thread);
        if (generation == m_generation && !m_state.inProgress) {
            m_preview = *result;
            m_previewReady = true;
            showPreview();
        }
        delete result;
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void MergeDialog::showPreview()
{
    const MergePreview &p = m_preview;
    const QString s = p.source, d = p.destination;
    const bool noFF = m_noFastForward->isChecked();
    const QString commits = counted(p.commits, tr("commit"), tr("commits"));
    const QString stat = counted(p.files, tr("file changed"), tr("files changed")) + tr("   +%1 −%2").arg(p.added).arg(p.removed);
    QStringList detail;
    QStringList files;
    QString warning;
    Kind kind = Info;
    QString headline;
    switch (p.outcome) {
    case MergePreview::Same:
        headline = tr("Pick two different branches.");
        detail << tr("The same branch is on both sides.");
        break;
    case MergePreview::UpToDate:
        headline = tr("Nothing to merge.");
        detail << tr("%2 already has every commit of %1.").arg(s, d);
        break;
    case MergePreview::FastForward:
        kind = Good;
        if (noFF) {
            headline = tr("Merges cleanly — no conflicts.");
            detail << tr("A merge commit brings %1 from %2 into %3 (it could simply move up, but the box below asks for a commit)")
                          .arg(commits, s, d);
        } else {
            headline = tr("Fast-forward — no conflicts possible.");
            detail << tr("%3 simply moves up %1 to the tip of %2").arg(commits, s, d);
        }
        detail << stat;
        break;
    case MergePreview::Clean:
        kind = Good;
        headline = tr("Merges cleanly — no conflicts.");
        detail << tr("%1 from %2 meet %3 of %4's own in a merge commit").arg(commits, s).arg(p.diverged).arg(d) << stat;
        break;
    case MergePreview::Conflicts:
        kind = Bad;
        headline = p.conflicts.size() == 1 ? tr("1 file would conflict.") : tr("%1 files would conflict.").arg(p.conflicts.size());
        detail << tr("%1 from %2 against %3 of %4's own").arg(commits, s).arg(p.diverged).arg(d) << stat
               << tr("Git leaves conflict markers in these files for you to resolve, then Commit merge:");
        files = p.conflicts;
        break;
    case MergePreview::Failed:
        kind = Bad;
        headline = s.isEmpty() || d.isEmpty() ? p.error : tr("Could not check the merge.");
        if (!s.isEmpty() && !d.isEmpty())
            detail << p.error;
        break;
    }
    if (p.isValid() && p.outcome != MergePreview::Same && !d.isEmpty() && d != m_branches.current)
        detail.insert(qMin(1, detail.size()), tr("%1 is checked out first").arg(d));
    if (!p.blocked.isEmpty())
        warning = (p.blocked.size() == 1 ? tr("Local changes to %1 are in the way — commit or stash them first.")
                                         : tr("Local changes to %1 files are in the way: %2 — commit or stash them first.").arg(p.blocked.size()))
                      .arg(fewOf(p.blocked, 4));
    setVerdict(kind, headline, detail.join(QLatin1Char('\n')), files, warning);

    const bool can = p.canMerge() && !m_merging;
    m_mergeButton->setEnabled(can);
    QString tip;
    if (!p.blocked.isEmpty())
        tip = tr("Blocked by local changes — see above");
    else if (p.outcome == MergePreview::Conflicts)
        tip = tr("Start the merge; the conflicted files wait in the Changes list (Enter)");
    else if (p.outcome == MergePreview::FastForward && !noFF)
        tip = tr("Fast-forward %2 to %1 (Enter)").arg(s, d);
    else if (can)
        tip = tr("Merge %1 into %2 with a merge commit (Enter)").arg(s, d);
    m_mergeButton->setToolTip(tip);
}

void MergeDialog::showMergeState()
{
    const MergeState &st = m_state;
    const QString d = m_destination.isEmpty() ? tr("the current branch") : m_destination;
    QString detail;
    if (st.conflicts.isEmpty())
        detail = tr("Every conflict is resolved — Commit merge in the Changes list finishes it, or abort to put %1 back as it was.").arg(d);
    else
        detail = tr("%1 conflict markers — resolve them in the Changes list and Commit merge, "
                    "or abort to put %2 back as it was:")
                     .arg(st.conflicts.size() == 1 ? tr("1 file still carries") : tr("%1 files still carry").arg(st.conflicts.size()), d);
    setVerdict(Bad, tr("A merge of %1 into %2 is in progress.").arg(st.source, d), detail, st.conflicts);
    m_mergeButton->hide();
    m_abortButton->show();
    m_cancelButton->setText(tr("Close"));
    m_cancelButton->setDefault(true);
    m_noFastForward->hide();
    updatePickers();
}

void MergeDialog::setVerdict(Kind kind, const QString &headline, const QString &detail, const QStringList &files,
                             const QString &warning)
{
    const OmarchyTheme *t = OmarchyTheme::instance();
    QColor color = t->mutedText();
    QString mark;
    switch (kind) {
    case Checking: mark = spinnerFrames(m_verdictIcon->font()).first(); break;
    case Info: mark = glyph(kInfo, QStringLiteral("i")); break;
    case Good: color = t->color(QStringLiteral("green")); mark = glyph(kGood, QStringLiteral("✓")); break;
    case Bad: color = t->color(QStringLiteral("red")); mark = glyph(kBad, QStringLiteral("✗")); break;
    }
    if (kind == Checking)
        m_spinner->start();
    else
        m_spinner->stop();
    m_verdictIcon->setText(mark);
    m_verdictIcon->setStyleSheet(QStringLiteral("color: %1;").arg(color.name()));
    m_headline->setText(headline);
    m_headline->setStyleSheet(QStringLiteral("color: %1;").arg((kind == Good || kind == Bad ? color : t->text()).name()));
    m_detail->setText(detail);
    m_detail->setVisible(!detail.isEmpty());
    m_files->clear();
    for (const QString &path : files) {
        auto *item = new QListWidgetItem(path, m_files);
        item->setToolTip(path);
    }
    if (!files.isEmpty()) {
        const int rowH = m_files->sizeHintForRow(0) > 0 ? m_files->sizeHintForRow(0) : m_files->fontMetrics().height() + 4;
        m_files->setFixedHeight(rowH * qMin(files.size(), 6) + 4);
    }
    m_files->setVisible(!files.isEmpty());
    m_warning->setText(warning);
    m_warningRow->setVisible(!warning.isEmpty());
    fitToContent();
}

// The height of the verdict card showing a headline and two lines of detail,
// what a clean merge or a fast-forward reports.
int MergeDialog::typicalVerdictHeight() const
{
    const QMargins m = m_verdict->layout()->contentsMargins();
    const int frame = m_verdict->rect().height() - m_verdict->contentsRect().height();
    const int spacing = static_cast<QGridLayout *>(m_verdict->layout())->verticalSpacing();
    // Measured the way QLabel lays out wrapped text (a bounding rect, not
    // lineSpacing(): the two differ by the font's leading at some sizes).
    auto textHeight = [](const QLabel *label, const QString &text) {
        return label->fontMetrics().boundingRect(0, 0, 2000, 2000, Qt::TextWordWrap, text).height();
    };
    // The icon spans the headline and detail rows, so it only counts when
    // it is taller than both of them together.
    const int text = textHeight(m_headline, QStringLiteral("x")) + spacing + textHeight(m_detail, QStringLiteral("x\nx"));
    return frame + m.top() + qMax(text, m_verdictIcon->sizeHint().height()) + m.bottom();
}

// Size the window to its content: the pickers at the top, the buttons at
// the bottom and the verdict card between them at the height its wrapped
// text needs — taller or shorter than before, as the case may be. The
// one-line checking message stands in for a verdict only briefly, so the
// card then holds the height of the verdict it replaces (or of a typical
// result, before there is one) instead of collapsing and growing again.
void MergeDialog::fitToContent()
{
    if (m_spinner->isActive())
        m_verdict->setMinimumHeight(qMax(m_verdictHeight, typicalVerdictHeight()));
    else
        m_verdict->setMinimumHeight(0);
    QLayout *l = layout();
    l->invalidate();
    l->activate();
    const int content = l->hasHeightForWidth() ? l->totalHeightForWidth(width()) : l->totalSizeHint().height();
    const int target = qMax(content, l->totalMinimumSize().height());
    // A fixed height, not a resize(): the compositor honours the window's
    // constraints, whereas a plain resize may be answered with the old size
    // (and, shorter than the content, a clipped card).
    if (target != height() || maximumHeight() != target)
        setFixedHeight(target);
    if (!m_spinner->isActive() && isVisible())
        m_verdictHeight = m_verdict->height();
}

// Fonts and frame widths from the stylesheet are only final once the widgets
// are polished, which is later than the constructor's first verdict.
void MergeDialog::showEvent(QShowEvent *e)
{
    ensurePolished();
    fitToContent();
    QDialog::showEvent(e);
}

// --- The merge -------------------------------------------------------------

void MergeDialog::setBusy(bool busy)
{
    m_merging = busy;
    updatePickers();
    m_mergeButton->setEnabled(!busy && m_previewReady && m_preview.canMerge());
    m_cancelButton->setEnabled(!busy);
    m_abortButton->setEnabled(!busy);
}

void MergeDialog::startMerge()
{
    if (m_merging || m_state.inProgress || !m_previewReady || !m_preview.canMerge())
        return;
    const QString source = m_source, destination = m_destination;
    setBusy(true);
    if (destination != m_branches.current) {
        QString error;
        if (!m_repo->checkout(destination, &error)) {
            setBusy(false);
            setVerdict(Bad, tr("Could not switch to %1.").arg(destination), error);
            return;
        }
        m_branches = m_repo->branches();
    }
    setVerdict(Checking, tr("Merging %1 into %2…").arg(source, destination), QString());
    const bool noFF = m_noFastForward->isChecked();
    const bool fastForward = m_preview.outcome == MergePreview::FastForward && !noFF;
    m_repo->runAsync(GitRepo::mergeArgs(source, noFF), this,
                     [this, source, destination, fastForward](int code, const QByteArray &, const QByteArray &err) {
                         if (code == 0) {
                             emit merged(source, destination, 0, fastForward);
                             accept();
                             return;
                         }
                         const MergeState state = m_repo->mergeState();
                         if (state.inProgress) {
                             emit merged(source, destination, state.conflicts.size(), false);
                             accept();
                             return;
                         }
                         setBusy(false);
                         setVerdict(Bad, tr("The merge failed."), QString::fromUtf8(err).trimmed());
                     },
                     300000, {QStringLiteral("GIT_EDITOR=true")});
}

void MergeDialog::abortMerge()
{
    if (m_merging || !m_state.inProgress)
        return;
    setBusy(true);
    QString error;
    const QString source = m_state.source, destination = m_destination;
    if (!m_repo->abortMerge(&error)) {
        setBusy(false);
        setVerdict(Bad, tr("Could not abort the merge."), error);
        return;
    }
    emit mergeAborted(source, destination);
    accept();
}
