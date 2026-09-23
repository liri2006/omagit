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
#include <QPainter>
#include <QResizeEvent>
#include <QStyle>
#include <QTimerEvent>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>

using namespace ui;

namespace {
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
constexpr int kSyncDropdown = 92; // the stacked sync dropdown at its narrowest, whatever its size hint says

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

// Stacked, the row has three levels of its own. The repository is the bare
// folder at every one of them and the right group is the sync dropdown and
// More, so only the tabs and the branch name are left to fold:
//
//   0  tab labels
//   1  tab glyphs
//   2  tab glyphs; the branch elides
constexpr int kStackedFoldCount = 3;

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

// The dropdown's inline content (screens.js topBar(), the SyncDropdown group),
// in 12 px-base pixels from its left edge. The design's positions are the
// least each field gets: a wider count pushes whatever follows it along.
constexpr int kDownX = 8, kPullX = 24, kUpX = 38, kChevronX = 68;
constexpr int kArrowGap = 16;  // from an arrow to its count
constexpr int kFieldGap = 4;   // the least room after a count
constexpr int kEndPad = 10;    // after the chevron's box
constexpr int kMarkRoom = 6;   // keeps Merge's corner mark clear of the chevron
constexpr int kGlyphBox = 14;  // the least room a glyph gets, as a tab's
constexpr int kBusyStepMs = 350; // BadgeButton's walking dots, at their cadence

// Pull and Push in one control for the stacked row: ↓2 ↑1 and a chevron,
// painted over the base button's chrome, and a menu with the four actions.
// It keeps no state of its own: the counts, the busy state and Merge's mark
// are read off the buttons it stands for whenever they change.
class SyncDropdown : public BadgeButton
{
    Q_OBJECT
public:
    SyncDropdown(BadgeButton *pull, BadgeButton *push, BadgeButton *merge)
        : m_pull(pull), m_push(push), m_merge(merge)
    {
        for (BadgeButton *source : {pull, push})
            connect(source, &BadgeButton::badgeChanged, this, [this] { followSources(); });
        connect(merge, &BadgeButton::badgeChanged, this, [this] { followMark(); });
    }

    // The theme's colours and glyphs, and Merge's mark as it is now.
    void refresh()
    {
        followMark();
        updateWidth();
        update();
    }

    // The design's width, or more when a count or the mark needs the room.
    int preferredWidth() const { return m_preferredWidth; }

signals:
    void widthChanged();

protected:
    void paintEvent(QPaintEvent *event) override
    {
        // The chrome, and Merge's mark in the corner where a badge would be.
        BadgeButton::paintEvent(event);
        const OmarchyTheme *t = OmarchyTheme::instance();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QFont plain = t->uiFont();
        const int h = height();
        const auto glyph = [&](int x, const QString &text, const QColor &colour) {
            p.setFont(plain);
            p.setPen(colour);
            // TextDontClip: the box is the room the glyph takes, not a crop of it.
            p.drawText(QRect(x, 0, glyphBox(text), h), Qt::AlignCenter | Qt::TextDontClip, text);
        };
        const Fields f = fields();
        glyph(space(kDownX), downText(), t->text());
        paintCount(&p, space(kPullX), m_pull);
        glyph(f.up, upText(), t->text());
        paintCount(&p, f.push, m_push);
        QColor dim = t->text();
        dim.setAlphaF(0.7);
        glyph(f.chevron, chevronText(), dim);
    }

    void timerEvent(QTimerEvent *event) override
    {
        if (event->timerId() == m_busyTimer) {
            m_busyPhase = (m_busyPhase + 1) % 3;
            update();
            return;
        }
        BadgeButton::timerEvent(event);
    }

private:
    // Where the fields after Pull's count start, and where the content ends.
    struct Fields {
        int up, push, chevron, end;
    };

    static QString downText() { return ui::icon(kArrowDown, QStringLiteral("↓")).trimmed(); }
    static QString upText() { return ui::icon(kArrowUp, QStringLiteral("↑")).trimmed(); }
    static QString chevronText() { return chevron().trimmed(); }

    static int glyphBox(const QString &text)
    {
        return qMax(QFontMetrics(OmarchyTheme::instance()->uiFont()).horizontalAdvance(text), space(kGlyphBox));
    }

    // Two digits and 99+ past them (the menu spells the number out).
    static QString countText(int n) { return n > 99 ? QStringLiteral("99+") : QString::number(n); }

    static QFont countFont(bool bold)
    {
        QFont font = OmarchyTheme::instance()->uiFont();
        font.setBold(bold);
        return font;
    }

    // A side's field: its count in the bold font, whichever font paints it,
    // or the walking dots' room while it is busy.
    static int countAdvance(const BadgeButton *source)
    {
        if (source->isBusy())
            return space(kGlyphBox);
        return QFontMetrics(countFont(true)).horizontalAdvance(countText(source->count()));
    }

    Fields fields() const
    {
        Fields f;
        const int pullEnd = space(kPullX) + countAdvance(m_pull);
        f.up = qMax(space(kUpX), pullEnd + space(kFieldGap));
        f.push = f.up + space(kArrowGap);
        const int pushEnd = f.push + countAdvance(m_push);
        f.chevron = qMax(space(kChevronX), pushEnd + space(kFieldGap));
        f.end = f.chevron + glyphBox(chevronText());
        return f;
    }

    // Recomputed whenever a count, a busy state, the mark or the theme
    // changes; the bar relays itself out when the answer does.
    void updateWidth()
    {
        const int w = qMax(space(kSyncDropdown), fields().end + space(kEndPad))
            + (markText().isEmpty() ? 0 : space(kMarkRoom));
        if (w == m_preferredWidth)
            return;
        m_preferredWidth = w;
        emit widthChanged();
    }

    // One timer for both sides: it runs while either of them is busy.
    void followSources()
    {
        const bool busy = m_pull->isBusy() || m_push->isBusy();
        if (busy && !m_busyTimer) {
            m_busyPhase = 0;
            m_busyTimer = startTimer(kBusyStepMs);
        } else if (!busy && m_busyTimer) {
            killTimer(m_busyTimer);
            m_busyTimer = 0;
        }
        updateWidth();
        update();
    }

    void followMark()
    {
        setMark(m_merge->markText(), m_merge->markColor());
        updateWidth();
    }

    // A side's count, or the walking dots while it is busy, each in a box of
    // its own advance so nothing it paints reaches the next field.
    void paintCount(QPainter *p, int x, const BadgeButton *source) const
    {
        const OmarchyTheme *t = OmarchyTheme::instance();
        if (source->isBusy()) {
            const qreal r = space(3) / 2.0, step = space(4);
            const qreal y = height() / 2.0;
            p->setPen(Qt::NoPen);
            for (int i = 0; i < 3; ++i) {
                p->setBrush(i == m_busyPhase ? t->accent() : t->mutedText());
                p->drawEllipse(QPointF(x + space(2) + step * i, y), r, r);
            }
            return;
        }
        const int n = source->count();
        const QFont font = countFont(n > 0);
        const QString text = countText(n);
        p->setFont(font);
        p->setPen(n > 0 ? t->accent() : t->text());
        p->drawText(QRect(x, 0, QFontMetrics(font).horizontalAdvance(text), height()),
                    Qt::AlignLeft | Qt::AlignVCenter | Qt::TextDontClip, text);
    }

    BadgeButton *m_pull;
    BadgeButton *m_push;
    BadgeButton *m_merge;
    int m_busyTimer = 0;
    int m_busyPhase = 0;
    int m_preferredWidth = 0;
};

// The bar keeps the dropdown as its base class; this is the one place that
// asks it for more.
int dropdownWidth(const BadgeButton *dropdown)
{
    return static_cast<const SyncDropdown *>(dropdown)->preferredWidth();
}

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
    // Diff stands between the two while stacked and is left out otherwise.
    m_changesTab = toolButton<SegmentButton>(tr("Changes"), tr("Pending changes and commit dialog (Ctrl+1)"));
    m_changesTab->setGlyph(kCommit, tr("C"));
    m_diffTab = toolButton<SegmentButton>(tr("Diff"), tr("The diff of the current file, with the file rail (Ctrl+Shift+B)"));
    m_diffTab->setGlyph(kDiff, tr("D"));
    m_historyTab = toolButton<SegmentButton>(tr("History"), tr("Commit history of the repository (Ctrl+2)"));
    m_historyTab->setGlyph(kHistory, tr("H"));
    auto *tabs = new QButtonGroup(this);
    tabs->setExclusive(true);
    for (QToolButton *b : QList<QToolButton *>{m_changesTab, m_diffTab, m_historyTab}) {
        b->setCheckable(true);
        b->setAccessibleName(b->text()); // the plain name, without the glyph or the count
        tabs->addButton(b);
    }
    m_changesTab->setChecked(true);
    connect(m_changesTab, &QToolButton::clicked, this, [this] { emit tabRequested(Tab::Changes); });
    connect(m_diffTab, &QToolButton::clicked, this, [this] { emit tabRequested(Tab::Diff); });
    connect(m_historyTab, &QToolButton::clicked, this, [this] { emit tabRequested(Tab::History); });
    m_tabs = new SegmentStrip({m_changesTab, m_diffTab, m_historyTab});
    m_tabs->setParent(m_row);
    m_tabs->setSegmentVisible(m_diffTab, false);

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
    keepMenuInWindow(m_moreMenu, m_more);

    // The stacked row's one sync control. Its text stays empty and it never
    // carries a count badge of its own: it paints the two counts inline.
    auto *dropdown = new SyncDropdown(m_pull, m_push, m_merge);
    m_syncDropdown = dropdown;
    m_syncDropdown->setParent(m_row);
    m_syncDropdown->setCursor(Qt::PointingHandCursor);
    m_syncDropdown->setFocusPolicy(Qt::NoFocus);
    m_syncDropdown->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_syncDropdown->setAccessibleName(tr("Sync"));
    m_syncDropdown->setToolTip(tr("Pull, push, fetch or merge (Ctrl+P, Ctrl+Shift+P, Ctrl+F, Ctrl+Shift+M)"));
    m_syncDropdown->setPopupMode(QToolButton::InstantPopup);
    m_syncMenu = new TickMenu(m_syncDropdown);
    m_syncMenu->setToolTipsVisible(true);
    m_syncDropdown->setMenu(m_syncMenu);
    m_syncDropdown->hide();
    // A wider count or Merge's mark widens it, and the row makes room. The
    // bar's own size hints count the dropdown too.
    connect(dropdown, &SyncDropdown::widthChanged, this, [this] {
        updateGeometry();
        relayout();
    });
    connect(m_syncMenu, &QMenu::aboutToShow, this, &TopBar::fillSyncMenu);
    keepMenuInWindow(m_syncMenu, m_syncDropdown);

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

void TopBar::setCurrentTab(Tab tab)
{
    QToolButton *const target = tab == Tab::Changes ? static_cast<QToolButton *>(m_changesTab)
        : tab == Tab::Diff                         ? m_diffTab
                                                   : m_historyTab;
    // The group unchecks the others; the window's own word asks for nothing.
    QSignalBlocker a(m_changesTab), b(m_diffTab), c(m_historyTab);
    target->setChecked(true);
}

TopBar::Tab TopBar::currentTab() const
{
    if (m_diffTab->isChecked())
        return Tab::Diff;
    return m_historyTab->isChecked() ? Tab::History : Tab::Changes;
}

void TopBar::setStacked(bool on)
{
    if (m_stacked == on)
        return;
    m_stacked = on;
    m_tabs->setSegmentVisible(m_diffTab, on);
    measure();
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
    m_more->setText(icon(kDotsHorizontal, QStringLiteral("…")).trimmed());
    m_diffToggle->setText(icon(kDockRight, tr("D")).trimmed());
    m_changesTab->refreshGlyph();
    m_diffTab->refreshGlyph();
    m_historyTab->refreshGlyph();
    static_cast<SyncDropdown *>(m_syncDropdown)->refresh();
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

    // The Diff segment takes part only while stacked, and the strip measures
    // the segments taking part.
    for (const bool labels : {false, true}) {
        m_changesTab->setLabelled(labels);
        m_diffTab->setLabelled(labels);
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

int TopBar::levelCount() const
{
    return m_stacked ? kStackedFoldCount : kFoldCount;
}

bool TopBar::elides(int level) const
{
    return m_stacked ? level == kStackedFoldCount - 1 : kFolds[level].branchElides;
}

bool TopBar::tabLabels(int level) const
{
    return m_stacked ? level == 0 : kFolds[level].tabLabels;
}

int TopBar::rightGroupWidth(int level) const
{
    // Stacked: the dropdown and More, whatever the level.
    if (m_stacked)
        return dropdownWidth(m_syncDropdown) + space(kSyncGap) + m_metrics.more;
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
    const bool repoLabel = !m_stacked && kFolds[level].repoLabel;
    const int left = (repoLabel ? m_metrics.repoFull : m_metrics.repoFolded) + space(kChipGap)
        + m_metrics.branchChrome + branchLabelWidth;
    const int tabs = tabLabels(level) ? m_metrics.tabsLabels : m_metrics.tabsGlyphs;
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
    return QSize(totalWidth(levelCount() - 1, minBranchLabel()), m_metrics.height + space(kBarGap) + 1);
}

void TopBar::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    relayout();
}

void TopBar::relayout()
{
    const int width = m_row->width();
    const int count = levelCount();
    m_level = count - 1;
    for (int i = 0; i < count - 1; ++i) {
        if (totalWidth(i, m_metrics.branchLabel) <= width) {
            m_level = i;
            break;
        }
    }
    // Only the last level elides, and only by as much as it has to.
    int label = m_metrics.branchLabel;
    if (elides(m_level))
        label = qBound(minBranchLabel(), width - totalWidth(m_level, 0), m_metrics.branchLabel);
    apply(m_level, label);
    place(m_level, label);
}

// The presentation of every control at `level`, the displayed text included:
// measuring and applying stay apart, so no candidate text reaches the screen.
void TopBar::apply(int level, int branchLabelWidth)
{
    const bool repoLabel = !m_stacked && kFolds[level].repoLabel;
    setTextOnce(m_repoButton, repoLabel ? icon(kFolder) + m_repositoryName + chevron()
                                        : icon(kFolder, tr("…")).trimmed());
    const QString label = branchLabelWidth < m_metrics.branchLabel
        ? m_branchButton->fontMetrics().elidedText(m_branchLabel, Qt::ElideRight, branchLabelWidth)
        : m_branchLabel;
    setTextOnce(m_branchButton, icon(kBranch) + label + chevron());

    // Stacked, all four belong to the dropdown: none of them is folded into
    // More, which is there anyway for its own entries.
    m_foldedSync.clear();
    if (m_stacked) {
        for (const SyncControl &c : std::as_const(m_syncControls))
            c.button->setVisible(false);
        m_more->setVisible(true);
        m_syncDropdown->setVisible(true);
        m_divider->setVisible(false);
        m_layoutButton->setVisible(false);
        m_diffToggle->setVisible(false);
    } else {
        const Fold &fold = kFolds[level];
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
        m_syncDropdown->setVisible(false);
    }
    updateMoreMark();

    for (SegmentButton *tab : {m_changesTab, m_diffTab, m_historyTab})
        tab->setLabelled(tabLabels(level));
}

void TopBar::place(int level, int branchLabelWidth)
{
    const int height = m_row->height(), width = m_row->width();
    const auto put = [height](QWidget *w, int x, int width) {
        const int h = qMin(height, w->sizeHint().height());
        w->setGeometry(x, (height - h) / 2, width, h);
        w->show();
    };

    const bool repoLabel = !m_stacked && kFolds[level].repoLabel;
    int x = 0;
    put(m_repoButton, x, repoLabel ? m_metrics.repoFull : m_metrics.repoFolded);
    x += (repoLabel ? m_metrics.repoFull : m_metrics.repoFolded) + space(kChipGap);
    const int branch = m_metrics.branchChrome + branchLabelWidth;
    put(m_branchButton, x, branch);
    const int leftEnd = x + branch;

    // The right group hangs off the right edge, in the order it reads in:
    // the sync buttons, the more menu, the divider, then the two toggles —
    // or, stacked, the sync dropdown and More.
    x = width - rightGroupWidth(level);
    const int rightStart = x;
    if (m_stacked) {
        // As tall as the row: the design's dropdown is the row's height.
        const int dropdown = dropdownWidth(m_syncDropdown);
        m_syncDropdown->setGeometry(x, 0, dropdown, height);
        m_syncDropdown->show();
        x += dropdown + space(kSyncGap);
        put(m_more, x, m_metrics.more);
        placeTabs(leftEnd, rightStart, level);
        return;
    }
    const Fold &fold = kFolds[level];
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

    placeTabs(leftEnd, rightStart, level);
}

// The tabs sit in the middle of the whole bar, nudged aside as far as they
// have to be to keep clear of either group.
void TopBar::placeTabs(int leftEnd, int rightStart, int level)
{
    const int height = m_row->height(), width = m_row->width();
    const int tabs = tabLabels(level) ? m_metrics.tabsLabels : m_metrics.tabsGlyphs;
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

void TopBar::addSyncEntry(QMenu *menu, const SyncControl &c, const QString &label)
{
    QString text = c.iconText.isEmpty() ? label : c.iconText + QStringLiteral("  ") + label;
    if (c.button->count() > 0)
        text += QStringLiteral("  (%1)").arg(c.button->count());
    QAction *a = menu->addAction(text);
    a->setToolTip(c.button->toolTip());
    a->setEnabled(c.button->isEnabled());
    connect(a, &QAction::triggered, c.button, &QAbstractButton::click);
}

// The folded buttons as menu entries, with the badge counts spelled out, then
// what the window has no other button for on a narrow row.
void TopBar::fillMoreMenu()
{
    m_moreMenu->clear();
    for (const SyncControl &c : std::as_const(m_syncControls)) {
        if (m_foldedSync.contains(c.button))
            addSyncEntry(m_moreMenu, c, c.label);
    }
    if (!m_foldedSync.isEmpty())
        m_moreMenu->addSeparator();
    const auto add = [this](uint glyph, const QString &label, const QString &tip, void (TopBar::*signal)()) {
        QAction *a = m_moreMenu->addAction(icon(glyph) + label);
        a->setToolTip(tip);
        connect(a, &QAction::triggered, this, signal);
    };
    add(kRefresh, tr("Refresh"), tr("Re-read the repository (F5)"), &TopBar::refreshRequested);
    add(kFolderOpen, tr("Open repository…"), tr("Pick a folder inside a git repository (Ctrl+O)"),
        &TopBar::openRepositoryRequested);
    add(kFetch, tr("Clone…"), tr("Download a repository from a URL or GitHub (Ctrl+Shift+O)"), &TopBar::cloneRequested);
    m_moreMenu->addSeparator();
    add(kInfo, tr("Keybindings"), tr("Every keyboard shortcut (Ctrl+K)"), &TopBar::keybindingsRequested);
}

// The stacked row's four sync actions: the buttons' own clicks, in the order
// they stand in on a wide row, Merge (which opens a view) after a separator.
void TopBar::fillSyncMenu()
{
    m_syncMenu->clear();
    for (const SyncControl &c : std::as_const(m_syncControls)) {
        if (c.button == m_merge) {
            m_syncMenu->addSeparator();
            addSyncEntry(m_syncMenu, c, tr("Merge…"));
        } else {
            addSyncEntry(m_syncMenu, c, c.label);
        }
    }
}

#include "TopBar.moc"
