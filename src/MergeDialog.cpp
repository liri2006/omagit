#include "MergeDialog.h"
#include "BranchMenu.h"
#include "OmarchyTheme.h"
#include "Settings.h"
#include "UiHelpers.h"

#include <QApplication>
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

#include <algorithm>
#include <memory>

namespace {
// md-swap_horizontal
constexpr uint kSwap = 0xF04E1;
// md-check_circle, md-alert_circle, md-alert
constexpr uint kGood = 0xF05E0, kBad = 0xF0028, kWarn = 0xF0026;

// Typing in a picker (or holding the swap button down) must not start a git
// run per keystroke; the spinner turns while one is out.
constexpr int kPreviewDebounceMs = 120;
constexpr int kSpinnerIntervalMs = 80;
constexpr int kPopupPollMs = 50;      // how often a verdict held back by an open menu asks again
// The design's dialog (design/figma-gen/screens.js mergeDialog()), on the grid
// of Grid.h: a dialog's 16 of padding and its parts a group gap (16) apart;
// the captions 16 px lines 4 over the pickers; the pickers side by side with
// the swap between them an item gap (8) either side — or, narrow, stacked,
// the swap a 28 px ghost button between them 8 from either, INTO 8 after it;
// Cancel, 8, Merge. Sizes the design gives the dialog alone: 640 wide, or the
// window it opens over less its margins (screens.js screen(): min(640,
// W − 2m)); stacked under 520; the branch pickers 36 high with a big
// control's 12 of padding, and the swap as tall; the verdict card at least
// 80, its icon's 16 px box and the text 8 after it.
constexpr int kDialogWidth = 640;
constexpr int kStackBelow = 520;
constexpr int kPicker = 36, kVerdict = 80;
// The conflict list scrolls beyond this many rows instead of growing on.
constexpr int kFileRows = 6;
// Blocked paths named in the warning before it says "and N more".
constexpr int kBlockedShown = 4;
// Wide and tall enough that wrapping never bites when a line is measured.
constexpr int kMeasureLimit = 2000;

// The theme's glyph, or `fallback` when the font has none. Unlike ui::icon()
// nothing follows it: these glyphs stand on their own.
QString glyph(uint cp, const QString &fallback)
{
    const QString g = OmarchyTheme::instance()->glyph(cp);
    return g.isEmpty() ? fallback : g;
}

// A colour set by hand on a label, remembered so that a verdict which says
// the same thing again does not re-polish the widget on every spinner tick.
void setTextColor(QLabel *label, QString *applied, const QColor &color)
{
    const QString sheet = QStringLiteral("color: %1;").arg(color.name());
    if (*applied == sheet)
        return;
    *applied = sheet;
    label->setStyleSheet(sheet);
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

// --- What the card says ----------------------------------------------------

MergeVerdict MergeVerdict::checking(const QString &headline)
{
    MergeVerdict v;
    v.kind = Checking;
    v.headline = headline;
    return v;
}

MergeVerdict MergeVerdict::problem(const QString &headline, const QString &detail)
{
    MergeVerdict v;
    v.kind = Bad;
    v.headline = headline;
    if (!detail.isEmpty())
        v.detail << detail;
    return v;
}

MergeVerdict mergeVerdict(const MergePreview &preview, bool noFastForward, const QString &currentBranch)
{
    using V = MergeVerdict; // its tr() keeps the strings in the dialog's context
    const MergePreview &p = preview;
    const QString s = p.source, d = p.destination;
    const QString commits = counted(p.commits, V::tr("commit"), V::tr("commits"));
    const QString stat = counted(p.files, V::tr("file changed"), V::tr("files changed"))
        + V::tr("   +%1 −%2").arg(p.added).arg(p.removed);
    MergeVerdict v;
    switch (p.outcome) {
    case MergePreview::Same:
        v.headline = V::tr("Pick two different branches.");
        v.detail << V::tr("The same branch is on both sides.");
        break;
    case MergePreview::UpToDate:
        v.headline = V::tr("Nothing to merge.");
        v.detail << V::tr("%2 already has every commit of %1.").arg(s, d);
        break;
    case MergePreview::FastForward:
        v.kind = V::Good;
        if (noFastForward) {
            v.headline = V::tr("Merges cleanly — no conflicts.");
            v.detail << V::tr("A merge commit brings %1 from %2 into %3 (it could simply move up, but the box below asks for a commit)")
                            .arg(commits, s, d);
        } else {
            v.headline = V::tr("Fast-forward — no conflicts possible.");
            v.detail << V::tr("%3 simply moves up %1 to the tip of %2").arg(commits, s, d);
        }
        v.detail << stat;
        break;
    case MergePreview::Clean:
        v.kind = V::Good;
        v.headline = V::tr("Merges cleanly — no conflicts.");
        v.detail << V::tr("%1 from %2 meet %3 of %4's own in a merge commit").arg(commits, s).arg(p.diverged).arg(d) << stat;
        break;
    case MergePreview::Conflicts:
        v.kind = V::Bad;
        v.headline = p.conflicts.size() == 1 ? V::tr("1 file would conflict.")
                                             : V::tr("%1 files would conflict.").arg(p.conflicts.size());
        v.detail << V::tr("%1 from %2 against %3 of %4's own").arg(commits, s).arg(p.diverged).arg(d) << stat
                 << V::tr("Git leaves conflict markers in these files for you to resolve, then Commit merge:");
        v.files = p.conflicts;
        break;
    case MergePreview::Failed:
        v.kind = V::Bad;
        v.headline = s.isEmpty() || d.isEmpty() ? p.error : V::tr("Could not check the merge.");
        if (!s.isEmpty() && !d.isEmpty())
            v.detail << p.error;
        break;
    }
    if (p.isValid() && p.outcome != MergePreview::Same && !d.isEmpty() && d != currentBranch)
        v.detail.insert(qMin(1, v.detail.size()), V::tr("%1 is checked out first").arg(d));
    if (!p.blocked.isEmpty())
        v.warning = (p.blocked.size() == 1
                         ? V::tr("Local changes to %1 are in the way — commit or stash them first.")
                         : V::tr("Local changes to %1 files are in the way: %2 — commit or stash them first.").arg(p.blocked.size()))
                        .arg(fewOf(p.blocked, kBlockedShown));

    v.canMerge = p.canMerge();
    if (!p.blocked.isEmpty())
        v.buttonTip = V::tr("Blocked by local changes — see above");
    else if (p.outcome == MergePreview::Conflicts)
        v.buttonTip = V::tr("Start the merge; the conflicted files wait in the Changes list (Enter)");
    else if (p.outcome == MergePreview::FastForward && !noFastForward)
        v.buttonTip = V::tr("Fast-forward %2 to %1 (Enter)").arg(s, d);
    else if (v.canMerge)
        v.buttonTip = V::tr("Merge %1 into %2 with a merge commit (Enter)").arg(s, d);
    return v;
}

// --- The two sides ---------------------------------------------------------

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

    void setBranch(const QString &name)
    {
        m_name = name;
        setAccessibleName(name);
        updateGeometry();
        update();
    }
    QString branch() const { return m_name; }

    QSize sizeHint() const override { return QSize(ui::space(260), ui::space(kPicker)); }
    QSize minimumSizeHint() const override { return QSize(ui::space(120), ui::space(kPicker)); }

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

    // A big control's kit layout (kit.js button({px: PAD.big})): [12][glyph
    // box 16][4][name][4][chevron box 12][12], the glyphs centred by their ink.
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
        const int pad = ui::space(ui::pad::big), h = height();
        const auto centred = [&p](const QFont &font, const QString &glyph, const QRectF &box) {
            p.setFont(font);
            p.drawText(box.center() - ui::inkRect(font, glyph).center(), glyph);
        };
        p.setPen(dim);
        const QString mark = glyph(ui::kBranch, QString());
        int x = pad;
        if (!mark.isEmpty()) {
            centred(t->uiFont(), mark, QRectF(x, 0, ui::space(ui::box::icon), h));
            x += ui::space(ui::box::icon) + ui::space(ui::gap::icon);
        }
        QFont small = t->uiFont();
        small.setPixelSize(qMax(1, qRound(small.pixelSize() * ui::box::chevron / double(ui::box::icon))));
        const int chevronLeft = width() - pad - ui::space(ui::box::chevron);
        centred(small, glyph(ui::kChevron, QStringLiteral("▾")), QRectF(chevronLeft, 0, ui::space(ui::box::chevron), h));
        p.setFont(t->titleFont());
        const QRect nameRect(x, 0, chevronLeft - ui::space(ui::gap::icon) - x, h);
        if (m_name.isEmpty()) {
            p.setPen(dim);
            p.drawText(nameRect, Qt::AlignVCenter, tr("No branch"));
            return;
        }
        p.setPen(on ? t->accent() : t->fill(0.45));
        p.drawText(nameRect, Qt::AlignVCenter, p.fontMetrics().elidedText(m_name, Qt::ElideMiddle, nameRect.width()));
    }

private:
    QString m_name;
};

// --- The verdict card ------------------------------------------------------

// The panel under the pickers: a large icon on the left, the headline and
// the detail lines beside it, the files git would leave conflicted, and a
// warning at the foot. It knows nothing of merging — it shows a MergeVerdict.
class VerdictCard : public QFrame
{
public:
    explicit VerdictCard(QWidget *parent = nullptr);

    void applyTheme();
    void setVerdict(const MergeVerdict &verdict);
    // True while the spinner turns, so the dialog can hold the card's height.
    bool checking() const { return m_spinner->isActive(); }
    // The height of a card showing a headline and two lines of detail, what
    // a clean merge or a fast-forward reports.
    int typicalHeight() const;

private:
    QTimer *m_spinner;
    int m_spinnerFrame = 0;
    QLabel *m_icon, *m_headline, *m_detail, *m_warningIcon, *m_warning;
    QWidget *m_warningRow;
    QHBoxLayout *m_warningLayout;
    QListWidget *m_files;
    QString m_iconColor, m_headlineColor, m_warningIconColor; // the stylesheets in force
};

VerdictCard::VerdictCard(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("mergeVerdict"));
    // A card's 12 of padding; the icon's 16 px box, the text 8 after it; the
    // parts under the headline 8 apart. applyTheme() scales them.
    auto *card = new QGridLayout(this);
    m_icon = new QLabel;
    m_icon->setObjectName(QStringLiteral("bigLabel"));
    m_icon->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    m_icon->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
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
    auto *warningLayout = m_warningLayout = new QHBoxLayout(m_warningRow);
    warningLayout->setContentsMargins(0, 0, 0, 0);
    m_warningIcon = new QLabel;
    m_warningIcon->setAlignment(Qt::AlignTop);
    m_warning = new QLabel;
    m_warning->setObjectName(QStringLiteral("captionLabel"));
    m_warning->setWordWrap(true);
    m_warning->setTextInteractionFlags(Qt::TextSelectableByMouse);
    warningLayout->addWidget(m_warningIcon);
    warningLayout->addWidget(m_warning, 1);
    m_warningRow->hide();
    card->addWidget(m_icon, 0, 0, 2, 1, Qt::AlignTop);
    card->addWidget(m_headline, 0, 1);
    card->addWidget(m_detail, 1, 1);
    card->addWidget(m_files, 2, 1);
    card->addWidget(m_warningRow, 3, 1);
    card->setColumnStretch(1, 1);
    // Spare height (the card held at its previous size while checking) goes
    // below the text, so the spinner and its message stay at the top together.
    card->setRowStretch(4, 1);

    m_spinner = new QTimer(this);
    m_spinner->setInterval(kSpinnerIntervalMs);
    connect(m_spinner, &QTimer::timeout, this, [this] {
        const QStringList frames = ui::spinnerFrames(m_icon->font());
        m_spinnerFrame = (m_spinnerFrame + 1) % frames.size();
        m_icon->setText(frames.at(m_spinnerFrame));
    });
}

void VerdictCard::applyTheme()
{
    const OmarchyTheme *t = OmarchyTheme::instance();
    auto *card = static_cast<QGridLayout *>(layout());
    const int pad = ui::space(ui::pad::popover);
    card->setContentsMargins(pad, pad, pad, pad);
    card->setHorizontalSpacing(ui::space(ui::gap::item));
    card->setVerticalSpacing(ui::space(ui::gap::item));
    m_warningLayout->setSpacing(ui::space(ui::gap::item));
    m_detail->setFont(t->captionFont());
    QFont bold = t->uiFont();
    bold.setBold(true);
    m_headline->setFont(bold);
    QFont big = t->uiFont();
    big.setPixelSize(qRound(t->fontBase() * 1.5));
    m_icon->setFont(big);
    m_icon->setFixedWidth(ui::space(ui::box::icon));
    m_warningIcon->setFont(t->uiFont());
    m_warningIcon->setText(glyph(kWarn, QStringLiteral("!")));
    setTextColor(m_warningIcon, &m_warningIconColor, t->color(QStringLiteral("yellow")));
    m_warning->setFont(t->captionFont());
    m_files->setFont(t->monoFont());
}

void VerdictCard::setVerdict(const MergeVerdict &verdict)
{
    const OmarchyTheme *t = OmarchyTheme::instance();
    QColor color = t->mutedText();
    QString mark;
    switch (verdict.kind) {
    case MergeVerdict::Checking: mark = ui::spinnerFrames(m_icon->font()).first(); break;
    case MergeVerdict::Info: mark = glyph(ui::kInfo, QStringLiteral("i")); break;
    case MergeVerdict::Good: color = t->color(QStringLiteral("green")); mark = glyph(kGood, QStringLiteral("✓")); break;
    case MergeVerdict::Bad: color = t->color(QStringLiteral("red")); mark = glyph(kBad, QStringLiteral("✗")); break;
    }
    if (verdict.kind == MergeVerdict::Checking)
        m_spinner->start();
    else
        m_spinner->stop();
    m_icon->setText(mark);
    setTextColor(m_icon, &m_iconColor, color);
    m_headline->setText(verdict.headline);
    setTextColor(m_headline, &m_headlineColor,
                 verdict.kind == MergeVerdict::Good || verdict.kind == MergeVerdict::Bad ? color : t->text());
    const QString detail = verdict.detail.join(QLatin1Char('\n'));
    m_detail->setText(detail);
    m_detail->setVisible(!detail.isEmpty());
    m_files->clear();
    for (const QString &path : verdict.files) {
        auto *item = new QListWidgetItem(path, m_files);
        item->setToolTip(path);
    }
    if (!verdict.files.isEmpty()) {
        const int rowH = m_files->sizeHintForRow(0) > 0 ? m_files->sizeHintForRow(0) : ui::space(ui::box::line);
        m_files->setFixedHeight(rowH * qMin(verdict.files.size(), kFileRows));
    }
    m_files->setVisible(!verdict.files.isEmpty());
    m_warning->setText(verdict.warning);
    m_warningRow->setVisible(!verdict.warning.isEmpty());
}

int VerdictCard::typicalHeight() const
{
    const QMargins m = layout()->contentsMargins();
    const int frame = rect().height() - contentsRect().height();
    const int spacing = static_cast<QGridLayout *>(layout())->verticalSpacing();
    // Measured the way QLabel lays out wrapped text (a bounding rect, not
    // lineSpacing(): the two differ by the font's leading at some sizes).
    auto textHeight = [](const QLabel *label, const QString &text) {
        return label->fontMetrics().boundingRect(0, 0, kMeasureLimit, kMeasureLimit, Qt::TextWordWrap, text).height();
    };
    // The icon spans the headline and detail rows, so it only counts when
    // it is taller than both of them together.
    const int text = textHeight(m_headline, QStringLiteral("x")) + spacing + textHeight(m_detail, QStringLiteral("x\nx"));
    return frame + m.top() + qMax(text, m_icon->sizeHint().height()) + m.bottom();
}

// --- The dialog ------------------------------------------------------------

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
    m_debounce->setInterval(kPreviewDebounceMs);
    connect(m_debounce, &QTimer::timeout, this, &MergeDialog::runPreview);

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

void MergeDialog::buildUi()
{
    // The pickers, the verdict card, the option and the buttons, a group gap
    // apart inside a dialog's padding (applyTheme() scales them).
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(buildBranchRow());
    m_card = new VerdictCard;
    layout->addWidget(m_card);
    layout->addWidget(buildNoFastForwardBox());
    // All spare height belongs between the preview controls and the footer.
    // Otherwise QBoxLayout distributes it among the header and verdict rows.
    layout->addStretch(1);
    layout->addLayout(buildButtonRow());

    // The width is fixed, to the window the dialog opens over once it is shown
    // (fitWidth()); the height follows the content (fitToContent()).
    setFixedWidth(ui::space(kDialogWidth));
    setTabOrder(m_sourcePicker, m_swapButton);
    setTabOrder(m_swapButton, m_destinationPicker);
    setTabOrder(m_destinationPicker, m_noFastForward);
    setTabOrder(m_noFastForward, m_mergeButton);
    setTabOrder(m_mergeButton, m_cancelButton);
    m_sourcePicker->setFocus();
}

// MERGE [source ▾]  ⇄  INTO [destination ▾], or stacked (arrangePickers()).
QGridLayout *MergeDialog::buildBranchRow()
{
    auto *grid = new QGridLayout;
    m_branchGrid = grid;
    // The gaps are rows and columns of their own (arrangePickers()).
    grid->setHorizontalSpacing(0);
    grid->setVerticalSpacing(0);
    m_sourceCaption = ui::sectionLabel(tr("Merge"));
    m_destinationCaption = ui::sectionLabel(tr("Into"));
    m_sourcePicker = new BranchPicker;
    m_sourcePicker->setToolTip(tr("The branch whose commits are brought in — click to pick another"));
    connect(m_sourcePicker, &QToolButton::clicked, this, &MergeDialog::pickSource);
    m_destinationPicker = new BranchPicker;
    m_destinationPicker->setToolTip(tr("The branch that receives them — checked out first when it is not the current one"));
    connect(m_destinationPicker, &QToolButton::clicked, this, &MergeDialog::pickDestination);
    m_swapButton = new ui::GlyphButton;
    m_swapButton->setObjectName(QStringLiteral("swapButton"));
    m_swapButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_swapButton->setCursor(Qt::PointingHandCursor);
    m_swapButton->setFocusPolicy(Qt::TabFocus);
    m_swapButton->setToolTip(tr("Swap the two sides — merge the other way round (Ctrl+S)"));
    m_swapButton->setAccessibleName(tr("Swap"));
    connect(m_swapButton, &QToolButton::clicked, this, &MergeDialog::swap);
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_S), this, this, &MergeDialog::swap);
    // The widgets are the dialog's own before the grid places them: the
    // arrangement takes them out of it and puts them back.
    for (QWidget *w : QList<QWidget *>{m_sourceCaption, m_destinationCaption, m_sourcePicker, m_swapButton,
                                       m_destinationPicker})
        w->setParent(this);
    arrangePickers(false);
    return grid;
}

// Side by side:            Stacked:
//   MERGE        INTO        MERGE
//   [source] ⇄ [dest]        [source          ]
//                            ⇄  INTO
//                            [dest            ]
// The grid's gaps are rows and columns of their own: 4 under a caption, 8
// between the pickers and the swap row, 8 either side of the swap.
void MergeDialog::arrangePickers(bool stacked)
{
    if (m_pickersStacked == int(stacked))
        return;
    m_pickersStacked = int(stacked);

    QGridLayout *grid = m_branchGrid;
    for (QWidget *w : QList<QWidget *>{m_sourceCaption, m_destinationCaption, m_sourcePicker, m_swapButton,
                                       m_destinationPicker})
        grid->removeWidget(w);
    // The swap row goes with its spacer; deleting a layout leaves its widgets
    // alone, and it takes itself out of the grid.
    delete m_swapRow;
    m_swapRow = nullptr;

    if (stacked) {
        grid->addWidget(m_sourceCaption, 0, 0, 1, 5);
        grid->addWidget(m_sourcePicker, 2, 0, 1, 5);
        m_swapRow = new QHBoxLayout;
        m_swapRow->addWidget(m_swapButton, 0, Qt::AlignVCenter);
        m_swapRow->addWidget(m_destinationCaption, 0, Qt::AlignVCenter);
        m_swapRow->addStretch(1);
        grid->addLayout(m_swapRow, 4, 0, 1, 5);
        grid->addWidget(m_destinationPicker, 6, 0, 1, 5);
        // The pickers are Expanding and span the grid: no column needs a say.
        for (int column = 0; column < 5; ++column)
            grid->setColumnStretch(column, 0);
    } else {
        grid->addWidget(m_sourceCaption, 0, 0);
        grid->addWidget(m_destinationCaption, 0, 4);
        grid->addWidget(m_sourcePicker, 2, 0);
        grid->addWidget(m_swapButton, 2, 2, Qt::AlignCenter);
        grid->addWidget(m_destinationPicker, 2, 4);
        grid->setColumnStretch(0, 1);
        grid->setColumnStretch(2, 0);
        grid->setColumnStretch(4, 1);
    }
    applyPickerMetrics();
}

// The grid's gaps and the swap button for the arrangement and the text size
// of the moment: side by side, the swap is the pickers' 36 px square; stacked,
// a 28 px ghost button with INTO 8 after it.
void MergeDialog::applyPickerMetrics()
{
    QGridLayout *grid = m_branchGrid;
    const bool stacked = m_pickersStacked == 1;
    const int item = ui::space(ui::gap::item);
    for (int row = 0; row < 7; ++row)
        grid->setRowMinimumHeight(row, 0);
    for (int column = 0; column < 5; ++column)
        grid->setColumnMinimumWidth(column, 0);
    grid->setRowMinimumHeight(1, ui::space(ui::gap::caption));
    if (stacked) {
        grid->setRowMinimumHeight(3, item);
        grid->setRowMinimumHeight(5, item);
    } else {
        grid->setColumnMinimumWidth(1, item);
        grid->setColumnMinimumWidth(3, item);
    }
    if (m_swapRow)
        m_swapRow->setSpacing(item);
    const int swap = ui::space(stacked ? ui::box::control : kPicker);
    m_swapButton->setFixedSize(swap, swap);
    m_swapButton->setProperty("ghost", stacked);
    m_swapButton->style()->unpolish(m_swapButton);
    m_swapButton->style()->polish(m_swapButton);
    grid->invalidate();
}

QCheckBox *MergeDialog::buildNoFastForwardBox()
{
    m_noFastForward = new QCheckBox(tr("Always create a merge commit"));
    m_noFastForward->setToolTip(tr("git merge --no-ff: record a merge commit even when the branch could simply move up"));
    m_noFastForward->setChecked(QSettings().value(settings::kMergeNoFastForward, false).toBool());
    connect(m_noFastForward, &QCheckBox::toggled, this, [this](bool on) {
        QSettings().setValue(settings::kMergeNoFastForward, on);
        if (m_previewReady)
            showPreview();
    });
    return m_noFastForward;
}

QHBoxLayout *MergeDialog::buildButtonRow()
{
    auto *buttons = m_buttonRow = new QHBoxLayout;
    m_abortButton = new QPushButton(tr("Abort merge"));
    m_abortButton->setCursor(Qt::PointingHandCursor);
    m_abortButton->setToolTip(tr("git merge --abort: the branch and the working tree go back to how they were"));
    m_abortButton->hide();
    connect(m_abortButton, &QPushButton::clicked, this, &MergeDialog::abortMerge);
    m_cancelButton = new QPushButton(tr("Cancel"));
    m_cancelButton->setCursor(Qt::PointingHandCursor);
    m_cancelButton->setAutoDefault(false);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    m_mergeButton = new ui::KitPushButton;
    m_mergeButton->setCursor(Qt::PointingHandCursor);
    m_mergeButton->setDefault(true);
    ui::setPrimary(m_mergeButton); // 16 in, the dialog's primary action
    m_mergeButton->setEnabled(false);
    connect(m_mergeButton, &QPushButton::clicked, this, &MergeDialog::startMerge);
    buttons->addWidget(m_abortButton);
    buttons->addStretch();
    buttons->addWidget(m_cancelButton);
    buttons->addWidget(m_mergeButton);
    return buttons;
}

void MergeDialog::applyTheme()
{
    const OmarchyTheme *t = OmarchyTheme::instance();
    const int pad = ui::space(ui::pad::dialog);
    layout()->setContentsMargins(pad, pad, pad, pad);
    layout()->setSpacing(ui::space(ui::gap::group));
    m_buttonRow->setSpacing(ui::space(ui::gap::item));
    for (QLabel *l : {m_sourceCaption, m_destinationCaption}) {
        l->setFont(t->captionFont());
        ui::placeOnLine(l, t->captionFont(), ui::box::line);
    }
    m_card->applyTheme();
    m_swapButton->setText(glyph(kSwap, QStringLiteral("⇄")));
    applyPickerMetrics();
    m_mergeButton->setText(ui::icon(ui::kMerge) + tr("Merge"));
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
    m_sourcePicker->setBranch(m_source);
    m_destinationPicker->setBranch(m_destination);
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
    setVerdict(MergeVerdict::checking(tr("Checking what merging %1 into %2 would do…").arg(m_source, m_destination)));
    m_mergeButton->setEnabled(false);
    m_debounce->start();
}

// The check runs git a few times (merge-tree among them, which can take a
// moment on a large repository): off the UI thread on a GitRepo of its own,
// since the window may point the shared one at another repository while
// this runs. Results are told apart by generation so a stale one is dropped.
void MergeDialog::runPreview()
{
    const int generation = m_generation;
    const QString root = m_repo->root(), source = m_source, destination = m_destination;
    const auto result = std::make_shared<MergePreview>();
    QThread *thread = QThread::create([root, source, destination, result] {
        const GitRepo repo(root);
        *result = repo.mergePreview(source, destination);
    });
    // Both the thread and the result outlive a dialog closed mid-run and go
    // when the thread does; `this` as the context drops the answer instead.
    connect(thread, &QThread::finished, this, [this, result, generation] { applyPreview(*result, generation); });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void MergeDialog::applyPreview(const MergePreview &preview, int generation)
{
    if (generation != m_generation || m_state.inProgress)
        return;
    // A branch list is open over the dialog: showing the verdict resizes the
    // window under it, so the answer waits until the menu has closed. A short
    // interval rather than 0: a zero timer would spin the menu's event loop.
    if (QApplication::activePopupWidget()) {
        QTimer::singleShot(kPopupPollMs, this, [this, preview, generation] { applyPreview(preview, generation); });
        return;
    }
    m_preview = preview;
    m_previewReady = true;
    showPreview();
}

void MergeDialog::showPreview()
{
    const MergeVerdict verdict = mergeVerdict(m_preview, m_noFastForward->isChecked(), m_branches.current);
    setVerdict(verdict);
    m_mergeButton->setEnabled(verdict.canMerge && !m_merging);
    m_mergeButton->setToolTip(verdict.buttonTip);
}

void MergeDialog::showMergeState()
{
    const MergeState &st = m_state;
    const QString d = m_destination.isEmpty() ? tr("the current branch") : m_destination;
    MergeVerdict verdict;
    verdict.kind = MergeVerdict::Bad;
    verdict.headline = tr("A merge of %1 into %2 is in progress.").arg(st.source, d);
    if (st.conflicts.isEmpty())
        verdict.detail << tr("Every conflict is resolved — Commit merge in the Changes list finishes it, "
                             "or abort to put %1 back as it was.")
                              .arg(d);
    else
        verdict.detail << tr("%1 conflict markers — resolve them in the Changes list and Commit merge, "
                             "or abort to put %2 back as it was:")
                              .arg(st.conflicts.size() == 1 ? tr("1 file still carries")
                                                            : tr("%1 files still carry").arg(st.conflicts.size()), d);
    verdict.files = st.conflicts;
    setVerdict(verdict);
    m_mergeButton->hide();
    m_abortButton->show();
    m_cancelButton->setText(tr("Close"));
    m_cancelButton->setDefault(true);
    m_noFastForward->hide();
    updatePickers();
}

void MergeDialog::setVerdict(const MergeVerdict &verdict)
{
    m_card->setVerdict(verdict);
    fitToContent();
}

// Size the window to its content: the pickers at the top, the buttons at
// the bottom and the verdict card between them at the height its wrapped
// text needs — taller or shorter than before, as the case may be. The
// one-line checking message stands in for a verdict only briefly, so the
// card then holds the height of the verdict it replaces (or of a typical
// result, before there is one) instead of collapsing and growing again.
void MergeDialog::fitToContent()
{
    if (m_card->checking())
        m_card->setMinimumHeight(std::max({ui::space(kVerdict), m_verdictHeight, m_card->typicalHeight()}));
    else
        m_card->setMinimumHeight(ui::space(kVerdict)); // the design's card, taller as its text asks
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
    if (!m_card->checking() && isVisible())
        m_verdictHeight = m_card->height();
}

// As wide as the design asks, over the window of the moment: the width is
// picked on every show, and the arrangement goes with it. Never narrower than
// the layout can take, whatever the window.
void MergeDialog::fitWidth()
{
    const QWidget *host = parentWidget() ? parentWidget()->window() : nullptr;
    int width = host ? qMin(ui::space(kDialogWidth), host->width() - 2 * ui::windowMargin(host))
                     : ui::space(kDialogWidth);
    arrangePickers(width < ui::space(kStackBelow));
    QLayout *l = layout();
    l->invalidate();
    width = qMax(width, l->totalMinimumSize().width());
    if (width != this->width() || minimumWidth() != width || maximumWidth() != width)
        setFixedWidth(width);
}

// Fonts and frame widths from the stylesheet are only final once the widgets
// are polished, which is later than the constructor's first verdict.
void MergeDialog::showEvent(QShowEvent *e)
{
    ensurePolished();
    fitWidth();
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

// git merges into the branch that is checked out, so a destination that is
// not the current one is switched to first.
bool MergeDialog::checkoutDestination(const QString &destination)
{
    if (destination == m_branches.current)
        return true;
    QString error;
    if (!m_repo->checkout(destination, &error)) {
        setBusy(false);
        setVerdict(MergeVerdict::problem(tr("Could not switch to %1.").arg(destination), error));
        return false;
    }
    m_branches = m_repo->branches();
    return true;
}

void MergeDialog::startMerge()
{
    if (m_merging || m_state.inProgress || !m_previewReady || !m_preview.canMerge())
        return;
    const QString source = m_source, destination = m_destination;
    setBusy(true);
    if (!checkoutDestination(destination))
        return;
    setVerdict(MergeVerdict::checking(tr("Merging %1 into %2…").arg(source, destination)));
    const bool noFF = m_noFastForward->isChecked();
    const bool fastForward = m_preview.outcome == MergePreview::FastForward && !noFF;
    m_repo->mergeAsync(source, noFF, this,
                       [this, source, destination, fastForward](GitRepo::MergeResult result, const QString &error) {
                           finishMerge(source, destination, fastForward, result, error);
                       });
}

// What git left behind: the view closes on a merge that ran, conflicts and
// all (the Changes list takes over), and stays open on one that did not.
void MergeDialog::finishMerge(const QString &source, const QString &destination, bool fastForward,
                              GitRepo::MergeResult result, const QString &error)
{
    if (result == GitRepo::Merged) {
        emit merged(source, destination, 0, fastForward);
        accept();
        return;
    }
    if (result == GitRepo::MergeConflicts) {
        // The paths come from the merge git left in place, not from its output.
        emit merged(source, destination, m_repo->mergeState().conflicts.size(), false);
        accept();
        return;
    }
    setBusy(false);
    setVerdict(MergeVerdict::problem(tr("The merge failed."), error));
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
        setVerdict(MergeVerdict::problem(tr("Could not abort the merge."), error));
        return;
    }
    emit mergeAborted(source, destination);
    accept();
}
