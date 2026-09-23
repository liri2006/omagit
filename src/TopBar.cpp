#include "TopBar.h"
#include "BadgeButton.h"
#include "OmarchyTheme.h"
#include "PaneLayout.h"
#include "Segmented.h"
#include "TickMenu.h"
#include "UiHelpers.h"

#include <QAction>
#include <QButtonGroup>
#include <QFontMetrics>
#include <QMenu>
#include <QResizeEvent>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>

using namespace ui;

namespace {
constexpr uint kDots = 0xF01D8; // md-dots_horizontal

// The design's distances (design/figma-gen/screens.js topBar() and kit.js
// segmented()), in 12 px-base pixels: every one of them goes through space().
constexpr int kBarGap = 6;        // between the row and its hairline
constexpr int kChipGap = 4;       // between the repository and the branch chip
constexpr int kSyncGap = 6;       // between two sync buttons, the more button included
constexpr int kGroupGap = 16;     // the clearance the tabs keep from either group
constexpr int kDividerPad = 10;   // on either side of the divider
constexpr int kDividerHeight = 16;
constexpr int kTogglesGap = 4;
constexpr int kFoldedRepo = 28;   // the bare folder chip
constexpr int kIconForm = 28;     // a sync button showing its glyph alone, and more
constexpr int kBranchFloor = 72;  // the least of the branch name the last level keeps

// How the row folds, from everything spelled out to the narrowest form. The
// first level that fits the width wins.
//
//   0  repo label   tab labels   Pull Push Fetch Merge, labelled
//   1  repo label   tab labels   the four as icons
//   2  repo label   tab labels   Pull, Push; Fetch and Merge in the more menu
//   3  folder       tab labels   as 2
//   4  folder       tab glyphs   as 2
//   5  folder       tab glyphs   all four in the more menu
//   6  folder       tab glyphs   all four in the more menu; the branch elides
struct Fold {
    bool repoLabel;
    bool tabLabels;
    bool syncLabels;
    int syncShown; // how many sync buttons stay out of the menu, counted from Pull
    bool branchElides;
};
constexpr Fold kFolds[] = {
    {true, true, true, 4, false},   {true, true, false, 4, false}, {true, true, false, 2, false},
    {false, true, false, 2, false}, {false, false, false, 2, false}, {false, false, false, 0, false},
    {false, false, false, 0, true},
};
constexpr int kFoldCount = int(sizeof(kFolds) / sizeof(kFolds[0]));

// What a control takes sideways, its fixed width included: the layout toggles
// are as wide as ui::iconButton() made them, whatever their glyph measures.
int widthOf(const QWidget *w)
{
    return qBound(w->minimumWidth(), w->sizeHint().width(), w->maximumWidth());
}

// A sync button wearing its glyph alone, and the more button: the design's
// 28 px square with the badge's reserve beside it. Their size hint is the
// wrong measure here — it is a text button's padding around a glyph, half as
// wide again as the design asks — so the row states the width instead.
int iconFormWidth()
{
    return space(kIconForm) + BadgeButton::kBadgeReserve;
}

// The stylesheet tells the two forms apart by this property. Repolishing is a
// whole style pass, so only a button that really changes form pays for one.
void setCompact(QWidget *w, bool on)
{
    if (w->property("compact").toBool() == on)
        return;
    w->setProperty("compact", on);
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
}

// The row applies a level on every resize: a button already carrying the text
// is left alone rather than relaid out and repainted for nothing.
void setTextOnce(QToolButton *b, const QString &text)
{
    if (b->text() != text)
        b->setText(text);
}

// The controls' row. The tabs follow the window's centre rather than a
// layout's idea of it, so the bar places every control by hand and only needs
// the row to hand each resize back.
class BarRow : public QWidget
{
public:
    explicit BarRow(std::function<void()> onResize)
        : m_onResize(std::move(onResize))
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

protected:
    void resizeEvent(QResizeEvent *) override { m_onResize(); }

private:
    std::function<void()> m_onResize;
};

} // namespace

TopBar::TopBar(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_row = new BarRow([this] { relayout(); });

    // Repository and branch chips: the same ghost buttons the footer used to
    // carry, with their object names and so their styling.
    m_repoButton = dropdownButton(QStringLiteral("repoButton"));
    m_repoButton->setParent(m_row);
    m_repoButton->setAccessibleName(m_repositoryName);
    m_branchButton = dropdownButton(QStringLiteral("branchButton"));
    m_branchButton->setParent(m_row);
    m_branchButton->setAccessibleName(m_branchLabel);

    // The page tabs: one segmented control, exclusive like a mode switch.
    m_changesTab = toolButton<SegmentButton>(tr("Changes"), tr("Pending changes and commit dialog (Ctrl+1)"));
    m_changesTab->setGlyph(kCommit, tr("C"));
    m_historyTab = toolButton<SegmentButton>(tr("History"), tr("Commit history of the repository (Ctrl+2)"));
    m_historyTab->setGlyph(kHistory, tr("H"));
    auto *tabs = new QButtonGroup(this);
    tabs->setExclusive(true);
    for (QToolButton *b : QList<QToolButton *>{m_changesTab, m_historyTab}) {
        b->setCheckable(true);
        b->setAccessibleName(b->text()); // the plain name, without the glyph or the count
        tabs->addButton(b);
    }
    m_changesTab->setChecked(true);
    connect(m_changesTab, &QToolButton::clicked, this, &TopBar::commitModeRequested);
    connect(m_historyTab, &QToolButton::clicked, this, &TopBar::historyModeRequested);
    m_tabs = new SegmentStrip({m_changesTab, m_historyTab});
    m_tabs->setParent(m_row);

    // Pull / Push / Fetch act on the whole repository, so they are the same in
    // both modes. The Pull badge is the number of commits waiting on the
    // upstream, the Push badge the number not pushed yet. Buttons fold into
    // the more menu from the right, so Pull is the last of them to go and
    // Merge (which opens a view of its own) the first.
    m_pull = toolButton<BadgeButton>(QString());
    m_push = toolButton<BadgeButton>(QString());
    m_fetch = toolButton<BadgeButton>(QString());
    m_merge = toolButton<BadgeButton>(QString());
    m_syncControls = {{m_pull, kPull, QStringLiteral("↓"), tr("Pull")},
                      {m_push, kPush, QStringLiteral("↑"), tr("Push")},
                      {m_fetch, kFetch, tr("F"), tr("Fetch")},
                      {m_merge, kMerge, tr("M"), tr("Merge")}};
    for (const SyncControl &c : std::as_const(m_syncControls)) {
        c.button->setParent(m_row);
        // The name says what the button does at every level, whether it is
        // wearing its label at this one or only its glyph.
        c.button->setAccessibleName(c.label);
        connect(c.button, &BadgeButton::badgeChanged, this, &TopBar::updateMoreMark);
    }

    m_more = toolButton<BadgeButton>(QString(), tr("More — the buttons that do not fit"));
    m_more->setParent(m_row);
    m_more->setAccessibleName(tr("More"));
    setCompact(m_more, true); // it never wears a label
    m_more->setPopupMode(QToolButton::InstantPopup);
    m_moreMenu = new TickMenu(m_more);
    m_moreMenu->setToolTipsVisible(true);
    m_more->setMenu(m_moreMenu);
    m_more->hide();
    connect(m_moreMenu, &QMenu::aboutToShow, this, &TopBar::fillMoreMenu);

    m_divider = hairline(Qt::Vertical);
    m_divider->setParent(m_row);
    // Docked/Mini and show/hide the diff pane: the window sets their glyph and
    // their tooltip, which follow the layout of the moment.
    m_layoutButton = iconButton(paneLayoutGlyph(PaneLayout::Docked), paneLayoutName(PaneLayout::Docked).left(1),
                                QString(), IconButtonSize::Toolbar, true);
    m_layoutButton->setParent(m_row);
    m_layoutButton->setCheckable(true); // checked = Mini
    m_layoutButton->setAccessibleName(tr("Mini layout"));
    m_diffToggle = iconButton(kDockRight, tr("D"), tr("Show or hide the diff pane (Ctrl+Shift+B)"),
                              IconButtonSize::Toolbar, true);
    m_diffToggle->setParent(m_row);
    m_diffToggle->setCheckable(true);
    m_diffToggle->setAccessibleName(tr("Diff pane"));

    // Twins of the chips and of every sync button, never shown and never placed:
    // the row tries every candidate text on these, so no text a level was only
    // weighing up reaches the screen and no live button is relaid out for a
    // measurement.
    m_probeRepo = dropdownButton(QStringLiteral("repoButton"));
    m_probeBranch = dropdownButton(QStringLiteral("branchButton"));
    // One twin per sync button rather than one for the four: a probe's text then
    // only changes when the label it stands for does, and a hidden widget's
    // setText() still tells the accessibility bridge about a new name.
    QList<QWidget *> probes{m_probeRepo, m_probeBranch};
    for (SyncControl &c : m_syncControls) {
        c.probe = toolButton<BadgeButton>(QString());
        probes << c.probe;
    }
    for (QWidget *probe : std::as_const(probes)) {
        probe->setParent(this);
        probe->hide(); // explicitly, so showing the bar leaves them behind
    }

    m_rootLayout = new QVBoxLayout(this);
    m_rootLayout->setContentsMargins(0, 0, 0, 0);
    m_rootLayout->addWidget(m_row);
    m_rootLayout->addWidget(hairline());

    applyTheme();
}

void TopBar::setRepositoryName(const QString &name)
{
    if (m_repositoryName == name)
        return;
    m_repositoryName = name;
    m_repoButton->setAccessibleName(name); // the plain name, whatever the chip is wearing
    measure();
}

void TopBar::setBranchLabel(const QString &label)
{
    if (m_branchLabel == label)
        return;
    m_branchLabel = label;
    m_branchButton->setAccessibleName(label); // un-elided, glyph and chevron aside
    measure();
}

void TopBar::setChangesCount(int count)
{
    if (m_changesTab->count() == qMax(0, count))
        return;
    m_changesTab->setCount(count);
    measure();
}

int TopBar::changesCount() const
{
    return m_changesTab->count();
}

void TopBar::setCommitMode(bool commit)
{
    QSignalBlocker a(m_changesTab), b(m_historyTab);
    m_changesTab->setChecked(commit);
    m_historyTab->setChecked(!commit);
}

void TopBar::applyTheme()
{
    const OmarchyTheme *theme = OmarchyTheme::instance();
    // The chips' font was the footer's business while they lived there; the
    // probes measure in the font the controls they stand for wear.
    for (QToolButton *b : QList<QToolButton *>{m_repoButton, m_branchButton, m_probeRepo, m_probeBranch})
        b->setFont(theme->uiFont());
    for (const SyncControl &c : std::as_const(m_syncControls))
        c.probe->setFont(theme->uiFont());
    m_more->setText(icon(kDots, QStringLiteral("…")).trimmed());
    m_diffToggle->setText(icon(kDockRight, tr("D")).trimmed());
    m_changesTab->refreshGlyph();
    m_historyTab->refreshGlyph();
    m_rootLayout->setSpacing(space(kBarGap));
    measure();
    updateMoreMark();
}

// Every label width comes from a probe's size hint, so the stylesheet's padding
// and the font are accounted for without second-guessing the style and without
// a candidate text ever sitting on a live button; the icon forms are the
// design's own width and the fixed-width controls (the toggles) are read off
// their constraints instead.
void TopBar::measure()
{
    m_metrics = Metrics();

    m_probeRepo->ensurePolished();
    setTextOnce(m_probeRepo, icon(kFolder) + m_repositoryName + chevron());
    m_metrics.repoFull = m_probeRepo->sizeHint().width();
    m_metrics.repoFolded = space(kFoldedRepo);

    m_probeBranch->ensurePolished();
    setTextOnce(m_probeBranch, icon(kBranch) + m_branchLabel + chevron());
    m_metrics.branchFull = m_probeBranch->sizeHint().width();
    // The name alone, in the font the stylesheet gives the chip: what is left
    // of the button is the glyph, the chevron and the padding around them.
    m_metrics.branchLabel = m_probeBranch->fontMetrics().horizontalAdvance(m_branchLabel);
    m_metrics.branchChrome = m_metrics.branchFull - m_metrics.branchLabel;

    // A probe wears no compact property, so it measures the labelled form.
    for (SyncControl &c : m_syncControls) {
        c.full = icon(c.glyph) + c.label;
        c.iconText = icon(c.glyph, c.fallback).trimmed();
        c.probe->ensurePolished();
        setTextOnce(c.probe, c.full);
        c.fullWidth = c.probe->sizeHint().width();
        c.iconWidth = iconFormWidth();
        m_metrics.height = qMax(m_metrics.height, c.probe->sizeHint().height());
    }
    m_metrics.more = iconFormWidth();

    for (const bool labels : {false, true}) {
        m_changesTab->setLabelled(labels);
        m_historyTab->setLabelled(labels);
        (labels ? m_metrics.tabsLabels : m_metrics.tabsGlyphs) = m_tabs->sizeHint().width();
    }
    m_metrics.toggles = widthOf(m_layoutButton) + space(kTogglesGap) + widthOf(m_diffToggle);
    m_metrics.divider = 2 * space(kDividerPad) + 1;
    for (const QWidget *w : QList<const QWidget *>{m_probeRepo, m_probeBranch, m_layoutButton, m_diffToggle})
        m_metrics.height = qMax(m_metrics.height, w->sizeHint().height());

    m_row->setFixedHeight(m_metrics.height);
    updateGeometry();
    relayout();
}

int TopBar::rightGroupWidth(int level) const
{
    const Fold &fold = kFolds[level];
    int w = 0;
    for (int i = 0; i < m_syncControls.size(); ++i) {
        if (i >= fold.syncShown)
            continue;
        w += (fold.syncLabels ? m_syncControls.at(i).fullWidth : m_syncControls.at(i).iconWidth) + space(kSyncGap);
    }
    if (fold.syncShown < m_syncControls.size())
        w += m_metrics.more + space(kSyncGap);
    // The gap after the last button is the divider's own left padding.
    return w - space(kSyncGap) + m_metrics.divider + m_metrics.toggles;
}

int TopBar::totalWidth(int level, int branchLabelWidth) const
{
    const Fold &fold = kFolds[level];
    const int left = (fold.repoLabel ? m_metrics.repoFull : m_metrics.repoFolded) + space(kChipGap)
        + m_metrics.branchChrome + branchLabelWidth;
    const int tabs = fold.tabLabels ? m_metrics.tabsLabels : m_metrics.tabsGlyphs;
    return left + space(kGroupGap) + tabs + space(kGroupGap) + rightGroupWidth(level);
}

// The last level keeps this much of the branch name, and no less.
int TopBar::minBranchLabel() const
{
    return qMin(m_metrics.branchLabel, space(kBranchFloor));
}

QSize TopBar::sizeHint() const
{
    return QSize(totalWidth(0, m_metrics.branchLabel), m_metrics.height + space(kBarGap) + 1);
}

// Never wider than the last level at its shortest branch name: the bar folds
// instead of forcing a width on the window.
QSize TopBar::minimumSizeHint() const
{
    return QSize(totalWidth(kFoldCount - 1, minBranchLabel()), m_metrics.height + space(kBarGap) + 1);
}

void TopBar::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    relayout();
}

void TopBar::relayout()
{
    const int width = m_row->width();
    m_level = kFoldCount - 1;
    for (int i = 0; i < kFoldCount - 1; ++i) {
        if (totalWidth(i, m_metrics.branchLabel) <= width) {
            m_level = i;
            break;
        }
    }
    // Only the last level elides, and only by as much as it has to.
    int label = m_metrics.branchLabel;
    if (kFolds[m_level].branchElides)
        label = qBound(minBranchLabel(), width - totalWidth(m_level, 0), m_metrics.branchLabel);
    apply(m_level, label);
    place(m_level, label);
}

// The presentation of every control at `level`, the displayed text included:
// measuring and applying stay apart, so no candidate text reaches the screen.
void TopBar::apply(int level, int branchLabelWidth)
{
    const Fold &fold = kFolds[level];
    setTextOnce(m_repoButton, fold.repoLabel ? icon(kFolder) + m_repositoryName + chevron()
                                             : icon(kFolder, tr("…")).trimmed());
    const QString label = branchLabelWidth < m_metrics.branchLabel
        ? m_branchButton->fontMetrics().elidedText(m_branchLabel, Qt::ElideRight, branchLabelWidth)
        : m_branchLabel;
    setTextOnce(m_branchButton, icon(kBranch) + label + chevron());

    m_foldedSync.clear();
    for (int i = 0; i < m_syncControls.size(); ++i) {
        const SyncControl &c = m_syncControls.at(i);
        const bool shown = i < fold.syncShown;
        setTextOnce(c.button, fold.syncLabels ? c.full : c.iconText);
        setCompact(c.button, !fold.syncLabels); // the icon form's narrower padding
        c.button->setVisible(shown);
        if (!shown)
            m_foldedSync << c.button;
    }
    m_more->setVisible(!m_foldedSync.isEmpty());
    updateMoreMark();

    m_changesTab->setLabelled(fold.tabLabels);
    m_historyTab->setLabelled(fold.tabLabels);
}

void TopBar::place(int level, int branchLabelWidth)
{
    const Fold &fold = kFolds[level];
    const int height = m_row->height(), width = m_row->width();
    const auto put = [height](QWidget *w, int x, int width) {
        const int h = qMin(height, w->sizeHint().height());
        w->setGeometry(x, (height - h) / 2, width, h);
        w->show();
    };

    int x = 0;
    put(m_repoButton, x, fold.repoLabel ? m_metrics.repoFull : m_metrics.repoFolded);
    x += (fold.repoLabel ? m_metrics.repoFull : m_metrics.repoFolded) + space(kChipGap);
    const int branch = m_metrics.branchChrome + branchLabelWidth;
    put(m_branchButton, x, branch);
    const int leftEnd = x + branch;

    // The right group hangs off the right edge, in the order it reads in:
    // the sync buttons, the more menu, the divider, then the two toggles.
    x = width - rightGroupWidth(level);
    const int rightStart = x;
    for (int i = 0; i < m_syncControls.size(); ++i) {
        if (i >= fold.syncShown)
            continue;
        const SyncControl &c = m_syncControls.at(i);
        const int w = fold.syncLabels ? c.fullWidth : c.iconWidth;
        put(c.button, x, w);
        x += w + space(kSyncGap);
    }
    if (!m_foldedSync.isEmpty()) {
        put(m_more, x, m_metrics.more);
        x += m_metrics.more + space(kSyncGap);
    }
    x += space(kDividerPad) - space(kSyncGap);
    const int dividerHeight = space(kDividerHeight);
    m_divider->setGeometry(x, (height - dividerHeight) / 2, 1, dividerHeight);
    m_divider->show();
    x += 1 + space(kDividerPad);
    put(m_layoutButton, x, widthOf(m_layoutButton));
    x += widthOf(m_layoutButton) + space(kTogglesGap);
    put(m_diffToggle, x, widthOf(m_diffToggle));

    // The tabs sit in the middle of the whole bar, nudged aside as far as they
    // have to be to keep clear of either group.
    const int tabs = fold.tabLabels ? m_metrics.tabsLabels : m_metrics.tabsGlyphs;
    const int low = leftEnd + space(kGroupGap), high = rightStart - space(kGroupGap) - tabs;
    m_tabs->setGeometry(qMax(low, qMin(qRound((width - tabs) / 2.0), high)), 0, tabs, height);
    m_tabs->show();
}

// A dot stands in for the badges of the buttons that moved into the menu. Its
// members are the ones folding put there — never what happens to be visible,
// which would count them all while the whole bar is hidden.
void TopBar::updateMoreMark()
{
    bool counts = false;
    for (const BadgeButton *b : std::as_const(m_foldedSync))
        if (b->count() > 0)
            counts = true;
    m_more->setMark(counts ? QStringLiteral("•") : QString(), OmarchyTheme::instance()->accent());
}

// The folded buttons as menu entries, with the badge counts spelled out.
void TopBar::fillMoreMenu()
{
    m_moreMenu->clear();
    for (const SyncControl &c : std::as_const(m_syncControls)) {
        if (!m_foldedSync.contains(c.button))
            continue;
        QString text = c.iconText.isEmpty() ? c.label : c.iconText + QStringLiteral("  ") + c.label;
        if (c.button->count() > 0)
            text += QStringLiteral("  (%1)").arg(c.button->count());
        QAction *a = m_moreMenu->addAction(text);
        a->setToolTip(c.button->toolTip());
        a->setEnabled(c.button->isEnabled());
        connect(a, &QAction::triggered, c.button, &QAbstractButton::click);
    }
}
