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
#include <QHBoxLayout>
#include <QMenu>
#include <QPainter>
#include <QResizeEvent>
#include <QStyle>
#include <QTimerEvent>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtMath>

#include <functional>

using namespace ui;

namespace {
// The design's bar (design/figma-gen/screens.js topBar()), on the grid of
// Grid.h: the row 8 under the window's top edge and 8 over the bar's last
// pixel row, the hairline (kBar); the repository and the branch chip a
// cluster apart; the sync buttons and More an item apart, the badges hanging
// 4 into those gaps; a group gap with a divider at its middle (8 | 8) before
// the Mini and diff toggles, a cluster apart; the tabs a group gap clear of
// either group. A button's badge rises 4 over its top edge, which the 8 above
// the row keeps inside the bar: the badge layer covering the bar paints it
// unclipped. Sizes the design gives the bar alone:
// A glyph-only button measured without a width (kit.js measureButton()): the
// bare folder chip, and a sync button in its icon form — 8 + 16 + 8.
constexpr int kBareButton = pad::control + box::icon + pad::control;
constexpr int kBranchFloor = 72;  // the least of the branch name the ordinary row's last level keeps
constexpr int kSyncMenuWidth = 260, kMoreMenuWidth = 240; // screens.js: the SyncMenu and MoreMenu cards

// How the row folds, from everything spelled out to the narrowest form. The
// first level that fits the width wins; level 0 only in a wide window
// (setSyncLabels()).
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
// More, so only the tabs and the branch name are left to fold. The branch
// keeps its whole name on one row; the first level whose row fits wins:
//
//   0  tab labels, one row
//   1  tab glyphs, one row
//   2  two rows (screens.js topBar(), extra narrow): the controls, then a
//      space(kBar) gap and the tabs alone, stretched over the row's width,
//      labelled while the widest labelled segment fits floor(width / n)
//      and glyphs otherwise; the branch elides by what the first row lacks,
//      down to a lone ellipsis if it has to
constexpr int kStackedFoldCount = 3;

// What a control takes sideways, its fixed width included: the layout toggles
// are as wide as ui::iconButton() made them, whatever their glyph measures.
int widthOf(const QWidget *w)
{
    return qBound(w->minimumWidth(), w->sizeHint().width(), w->maximumWidth());
}

// A sync button wearing its glyph alone: the bare button's 32 px
// (ui::setIconForm()), the glyph centred in it. The badge hangs over the
// corner outside it, so nothing is kept free inside for one. The row states
// the width rather than asking the button, whose form may not be on yet.
int iconFormWidth()
{
    return space(kBareButton);
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
// the row to hand each resize back, and to ask the bar how tall a width makes
// it: one row, or two where the tabs take one of their own.
class BarRow : public QWidget
{
public:
    BarRow(std::function<void()> onResize, std::function<int(int)> heightForWidth)
        : m_onResize(std::move(onResize)), m_heightForWidth(std::move(heightForWidth))
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return m_heightForWidth(width); }

protected:
    void resizeEvent(QResizeEvent *) override { m_onResize(); }

private:
    std::function<void()> m_onResize;
    std::function<int(int)> m_heightForWidth;
};

// The dropdown's inline content (screens.js syncDropdown()): [8][↓ 16][4]
// [count in 8][8][↑ 16][4][count in 8][4][chevron 12][8], 96 wide. A count's
// 8 is the least it gets: a wider one pushes whatever follows it along.
// On two rows it is a borderless miniature of itself: [4][↓ 16][count in 8]
// [8][↑ 16][count in 8][4], 64 wide, the counts against their arrows' boxes
// and no chevron.
constexpr int kSyncDropdown = 96, kSyncMini = 64;
constexpr int kMiniPad = 4;
constexpr int kCountSlot = 8;
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

    // The miniature (two rows) or the full form, and the width either of
    // them wants: the design's, or more when a count needs the room. The bar
    // weighs its levels against both, whichever is on.
    void setMini(bool on)
    {
        if (m_mini == on)
            return;
        m_mini = on;
        setProperty("ghost", on); // the stylesheet's borderless form
        style()->unpolish(this);
        style()->polish(this);
        update();
    }
    bool isMini() const { return m_mini; }
    int preferredWidth(bool mini) const { return mini ? m_miniWidth : m_preferredWidth; }

signals:
    void widthChanged();

protected:
    void paintEvent(QPaintEvent *event) override
    {
        // The chrome. Merge's mark is this button's badge, which the bar's
        // badge layer paints over the corner like any other.
        BadgeButton::paintEvent(event);
        const OmarchyTheme *t = OmarchyTheme::instance();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const int h = height();
        // A glyph centred by its ink in its box, as KitButton centres one.
        const auto glyph = [&](int x, int box, const QFont &font, const QString &text, const QColor &colour) {
            p.setFont(font);
            p.setPen(colour);
            p.drawText(QRectF(x, 0, box, h).center() - inkRect(font, text).center(), text);
        };
        const Fields f = fields(m_mini);
        glyph(f.down, space(box::icon), t->uiFont(), downText(), t->text());
        paintCount(&p, f.pull, m_pull);
        glyph(f.up, space(box::icon), t->uiFont(), upText(), t->text());
        paintCount(&p, f.push, m_push);
        if (m_mini)
            return;
        // The chevron as KitButton draws its own: at 12/16 of the glyphs' size.
        QFont small = t->uiFont();
        small.setPixelSize(qMax(1, qRound(small.pixelSize() * box::chevron / double(box::icon))));
        QColor dim = t->text();
        dim.setAlphaF(0.7);
        glyph(f.chevron, space(box::chevron), small, chevronText(), dim);
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
    // Where the fields start, and where the content ends.
    struct Fields {
        int down, pull, up, push, chevron, end;
    };

    static QString downText() { return ui::icon(kArrowDown, QStringLiteral("↓")).trimmed(); }
    static QString upText() { return ui::icon(kArrowUp, QStringLiteral("↑")).trimmed(); }
    static QString chevronText() { return chevron().trimmed(); }

    // Two digits and 99+ past them (the menu spells the number out).
    static QString countText(int n) { return n > 99 ? QStringLiteral("99+") : QString::number(n); }

    static QFont countFont(bool bold)
    {
        QFont font = OmarchyTheme::instance()->uiFont();
        font.setBold(bold);
        return font;
    }

    // A side's field: its count in the bold font, whichever font paints it,
    // at least the design's slot, or the walking dots' box while it is busy.
    static int countWidth(const BadgeButton *source)
    {
        if (source->isBusy())
            return space(box::icon);
        return qMax(space(kCountSlot), QFontMetrics(countFont(true)).horizontalAdvance(countText(source->count())));
    }

    Fields fields(bool mini) const
    {
        Fields f;
        f.down = space(mini ? kMiniPad : pad::control);
        f.pull = f.down + space(box::icon) + (mini ? 0 : space(gap::icon));
        f.up = f.pull + countWidth(m_pull) + space(gap::item);
        f.push = f.up + space(box::icon) + (mini ? 0 : space(gap::icon));
        if (mini) {
            f.chevron = f.end = f.push + countWidth(m_push);
            return f;
        }
        f.chevron = f.push + countWidth(m_push) + space(gap::icon);
        f.end = f.chevron + space(box::chevron);
        return f;
    }

    // Recomputed whenever a count, a busy state, the mark or the theme
    // changes; the bar relays itself out when an answer does. Merge's mark
    // hangs over the corner like a badge, clear of the chevron's box.
    void updateWidth()
    {
        const int full = qMax(space(kSyncDropdown), fields(false).end + space(pad::control));
        const int mini = qMax(space(kSyncMini), fields(true).end + space(kMiniPad));
        if (full == m_preferredWidth && mini == m_miniWidth)
            return;
        m_preferredWidth = full;
        m_miniWidth = mini;
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

    // A side's count, or the walking dots while it is busy (three 3 px dots
    // a quarter of their 16 px box apart), each in a box of its own advance so
    // nothing it paints reaches the next field.
    void paintCount(QPainter *p, int x, const BadgeButton *source) const
    {
        const OmarchyTheme *t = OmarchyTheme::instance();
        if (source->isBusy()) {
            const qreal r = 1.5 * t->fontBase() / 12.0, step = space(box::icon) / 4.0;
            const qreal y = height() / 2.0;
            p->setPen(Qt::NoPen);
            for (int i = 0; i < 3; ++i) {
                p->setBrush(i == m_busyPhase ? t->accent() : t->mutedText());
                p->drawEllipse(QPointF(x + step * (i + 1), y), r, r);
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
    int m_miniWidth = 0;
    bool m_mini = false;
};

// The bar keeps the dropdown as its base class; these are the places that
// ask it for more.
int dropdownWidth(const BadgeButton *dropdown, bool mini)
{
    return static_cast<const SyncDropdown *>(dropdown)->preferredWidth(mini);
}

void setDropdownMini(BadgeButton *dropdown, bool mini)
{
    static_cast<SyncDropdown *>(dropdown)->setMini(mini);
}

} // namespace

TopBar::TopBar(QWidget *parent)
    : QWidget(parent)
{
    // At least the one row of the size hint, and as tall as heightForWidth()
    // makes it: a Fixed height would hold the layout to the hint's one row.
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

    m_row = new BarRow([this] { relayout(); }, [this](int width) { return rowsHeight(width); });
    // The badges hang over the buttons' corners, out of the buttons' own
    // rects, so a layer over the whole bar paints them (see the end of the
    // constructor); the room above the row (kBar) is where their tops
    // land. It exists before anything can relay the row out.
    m_badges = new BadgeLayer(this);

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
    setIconForm(m_more, true); // the 28 px square: it never wears a label, in either presentation
    m_more->setPopupMode(QToolButton::InstantPopup);
    m_moreMenu = new TickMenu(m_more);
    m_moreMenu->setToolTipsVisible(true);
    m_more->setMenu(m_moreMenu);
    m_more->hide();
    connect(m_moreMenu, &QMenu::aboutToShow, this, &TopBar::fillMoreMenu);
    keepMenuInWindow(m_moreMenu, m_more, this); // hanging 4 under the bar

    // The stacked row's one sync control. Its text stays empty and it never
    // carries a count badge of its own: it paints the two counts inline.
    auto *dropdown = new SyncDropdown(m_pull, m_push, m_merge);
    m_syncDropdown = dropdown;
    m_syncDropdown->setObjectName(QStringLiteral("syncDropdown"));
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
        invalidateHeight();
        relayout();
    });
    connect(m_syncMenu, &QMenu::aboutToShow, this, &TopBar::fillSyncMenu);
    keepMenuInWindow(m_syncMenu, m_syncDropdown, this);

    m_divider = hairline(Qt::Vertical);
    m_divider->setParent(m_row);
    // Docked/Mini and show/hide the diff pane: the window sets their glyph and
    // their tooltip, which follow the layout of the moment.
    m_layoutButton = iconButton(kViewCompact, paneLayoutName(PaneLayout::Mini).left(1), QString(),
                                IconButtonSize::Toolbar, true);
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

    // The row keeps the window's side margin; the hairline under it runs from
    // edge to edge, like the footer's, and is the bar's last pixel row.
    m_rootLayout = new QVBoxLayout(this);
    m_rowLayout = new QHBoxLayout;
    m_rowLayout->addWidget(m_row);
    m_rootLayout->addLayout(m_rowLayout);
    m_rootLayout->addWidget(hairline(Qt::Horizontal, HairlineTone::Chrome));

    // The badge layer over everything else in the bar.
    for (BadgeButton *b : {m_pull, m_push, m_fetch, m_merge, m_more, m_syncDropdown})
        m_badges->watch(b);
    m_badges->raise();

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
    // 8 + 28 + 8, the hairline the last of the 8 under the row.
    m_rootLayout->setContentsMargins(0, space(kBar), 0, 0);
    m_rootLayout->setSpacing(space(kBar) - 1);
    applyMargins();
    measure();
    updateMoreMark();
}

void TopBar::setDensity(const Density &density)
{
    if (m_density == density)
        return;
    m_density = density;
    applyMargins();
    invalidateHeight(); // the row's width, and so its level, moved with the margins
}

void TopBar::setSyncLabels(bool allowed)
{
    if (m_syncLabels == allowed)
        return;
    m_syncLabels = allowed;
    relayout();
}

// The window's side margin, which the row keeps and the hairline does not.
void TopBar::applyMargins()
{
    const int margin = space(m_density.margin);
    m_rowLayout->setContentsMargins(margin, 0, margin, 0);
}

// Every layout on the way up keeps its own answer for a width: the row's
// updateGeometry() clears its item's and the bar's layout's, the bar's the
// window's, and the row layout nested between them is invalidated by hand.
void TopBar::invalidateHeight()
{
    m_row->updateGeometry();
    m_rowLayout->invalidate();
    updateGeometry();
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
    setTextOnce(m_probeRepo, icon(kFolderOpen) + m_repositoryName + chevron());
    m_metrics.repoFull = m_probeRepo->sizeHint().width();
    m_metrics.repoFolded = space(kBareButton);

    m_probeBranch->ensurePolished();
    setTextOnce(m_probeBranch, icon(kBranch) + m_branchLabel + chevron());
    m_metrics.branchFull = m_probeBranch->sizeHint().width();
    // The name alone, in the font the stylesheet gives the chip: what is left
    // of the button is the glyph, the chevron and the padding around them.
    m_metrics.branchLabel = qCeil(QFontMetricsF(m_probeBranch->font()).horizontalAdvance(m_branchLabel));
    m_metrics.branchChrome = m_metrics.branchFull - m_metrics.branchLabel;
    // A lone ellipsis, rounded up: elidedText() gives nothing at all in a
    // width a fraction short of it.
    m_metrics.branchEllipsis = qCeil(QFontMetricsF(m_probeBranch->font()).horizontalAdvance(QChar(0x2026)));

    // A probe wears no iconForm property, so it measures the labelled form.
    for (SyncControl &c : m_syncControls) {
        c.full = icon(c.glyph) + c.label;
        c.iconText = icon(c.glyph, c.fallback).trimmed();
        c.probe->ensurePolished();
        setTextOnce(c.probe, c.full);
        c.fullWidth = c.probe->sizeHint().width();
        c.iconWidth = iconFormWidth();
        m_metrics.height = qMax(m_metrics.height, c.probe->sizeHint().height());
    }
    m_metrics.more = space(box::control);

    // The Diff segment takes part only while stacked, and the strip measures
    // the segments taking part.
    for (const bool labels : {false, true}) {
        m_changesTab->setLabelled(labels);
        m_diffTab->setLabelled(labels);
        m_historyTab->setLabelled(labels);
        (labels ? m_metrics.tabsLabels : m_metrics.tabsGlyphs) = m_tabs->sizeHint().width();
    }
    // Still labelled: each segment's own hint, which a share of row 2 holds or not.
    for (const SegmentButton *tab : {m_changesTab, m_diffTab, m_historyTab}) {
        if (tab->isHidden())
            continue;
        ++m_metrics.tabSegments;
        m_metrics.tabLabelled = qMax(m_metrics.tabLabelled, tab->sizeHint().width());
    }
    m_metrics.toggles = widthOf(m_layoutButton) + space(gap::cluster) + widthOf(m_diffToggle);
    m_metrics.divider = space(gap::group);
    for (const QWidget *w : QList<const QWidget *>{m_probeRepo, m_probeBranch, m_layoutButton, m_diffToggle})
        m_metrics.height = qMax(m_metrics.height, w->sizeHint().height());

    // One row, or two a space(kBar) apart (rowsHeight()).
    m_row->setMinimumHeight(m_metrics.height);
    m_row->setMaximumHeight(2 * m_metrics.height + space(kBar));
    invalidateHeight();
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

// Two rows: each segment gets floor(width / n) of the second, and the labels
// stay while the widest of them fits its share (screens.js topBar():
// measureSegmented([it]) > each).
bool TopBar::tabLabels(int level) const
{
    if (twoRows(level))
        return m_metrics.tabLabelled <= m_row->width() / qMax(1, m_metrics.tabSegments);
    return m_stacked ? level == 0 : kFolds[level].tabLabels;
}

bool TopBar::twoRows(int level) const
{
    return m_stacked && level == kStackedFoldCount - 1;
}

int TopBar::rightGroupWidth(int level) const
{
    // Stacked: the dropdown and More, whatever the level; More is the bare
    // square there, as nothing folds into it that could hang a badge on it.
    if (m_stacked)
        return dropdownWidth(m_syncDropdown, twoRows(level)) + space(gap::item) + m_metrics.more;
    const Fold &fold = kFolds[level];
    int w = 0;
    for (int i = 0; i < m_syncControls.size(); ++i) {
        if (i >= fold.syncShown)
            continue;
        w += (fold.syncLabels ? m_syncControls.at(i).fullWidth : m_syncControls.at(i).iconWidth) + space(gap::item);
    }
    if (fold.syncShown < m_syncControls.size())
        w += m_metrics.more + space(gap::item);
    // The gap after the last button gives way to the group gap.
    return w - space(gap::item) + m_metrics.divider + m_metrics.toggles;
}

int TopBar::totalWidth(int level, int branchLabelWidth) const
{
    const bool repoLabel = !m_stacked && kFolds[level].repoLabel;
    const int left = (repoLabel ? m_metrics.repoFull : m_metrics.repoFolded) + space(gap::cluster)
        + m_metrics.branchChrome + branchLabelWidth;
    if (twoRows(level))
        return left + space(gap::group) + rightGroupWidth(level);
    const int tabs = tabLabels(level) ? m_metrics.tabsLabels : m_metrics.tabsGlyphs;
    return left + space(gap::group) + tabs + space(gap::group) + rightGroupWidth(level);
}

// The last level keeps this much of the branch name, and no less. Stacked,
// that is a lone ellipsis, so the bar never forces a width on a narrow tile;
// an elided text narrower than the ellipsis would be no text at all.
int TopBar::minBranchLabel() const
{
    return qMin(m_metrics.branchLabel, m_stacked ? m_metrics.branchEllipsis : space(kBranchFloor));
}

// The height: the room above the row, which the badges rise into, the row,
// and the room under it, whose last pixel row is the hairline (8 + 28 + 8),
// on one row (heightForWidth() says when there are two); the width, the
// row's and the window's side margins.
QSize TopBar::sizeHint() const
{
    return QSize(totalWidth(0, m_metrics.branchLabel) + 2 * space(m_density.margin),
                 space(kBar) + m_metrics.height + space(kBar));
}

// Never wider than the last level at its shortest branch name, nor, on two
// rows, than the tabs as glyphs: the bar folds instead of forcing a width on
// the window.
QSize TopBar::minimumSizeHint() const
{
    const int last = levelCount() - 1;
    int width = totalWidth(last, minBranchLabel());
    if (twoRows(last))
        width = qMax(width, m_metrics.tabsGlyphs);
    return QSize(width + 2 * space(m_density.margin), space(kBar) + m_metrics.height + space(kBar));
}

void TopBar::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    m_badges->setGeometry(rect());
    relayout();
}

// The first level that fits, with the whole branch name; the last otherwise.
int TopBar::levelFor(int width) const
{
    const int count = levelCount();
    // Level 0 is the only one wearing the sync labels; stacked, there are none.
    const int first = !m_stacked && !m_syncLabels ? 1 : 0;
    for (int i = first; i < count - 1; ++i) {
        if (totalWidth(i, m_metrics.branchLabel) <= width)
            return i;
    }
    return count - 1;
}

// What the row answers the layouts: it knows its level from its width alone,
// so the height they give it is the one relayout() then fills.
int TopBar::rowsHeight(int width) const
{
    return twoRows(levelFor(width)) ? 2 * m_metrics.height + space(kBar) : m_metrics.height;
}

void TopBar::relayout()
{
    const int width = m_row->width();
    m_level = levelFor(width);
    // Only the last level elides, and only by as much as it has to.
    int label = m_metrics.branchLabel;
    if (elides(m_level))
        label = qBound(minBranchLabel(), width - totalWidth(m_level, 0), m_metrics.branchLabel);
    apply(m_level, label);
    place(m_level, label);
    // On two rows the popups hang from the first, over the tabs.
    setPopupEdge(this, twoRows(m_level) ? m_row->y() + m_metrics.height : -1);
    m_badges->update(); // the buttons may have moved without a badge changing
    if (twoRows(m_level) != m_twoRows) {
        m_twoRows = twoRows(m_level);
        emit twoRowsChanged(m_twoRows);
    }
}

// The presentation of every control at `level`, the displayed text included:
// measuring and applying stay apart, so no candidate text reaches the screen.
void TopBar::apply(int level, int branchLabelWidth)
{
    const bool repoLabel = !m_stacked && kFolds[level].repoLabel;
    setTextOnce(m_repoButton, repoLabel ? icon(kFolderOpen) + m_repositoryName + chevron()
                                        : icon(kFolderOpen, tr("…")).trimmed());
    const QString label = branchLabelWidth < m_metrics.branchLabel
        ? m_branchButton->fontMetrics().elidedText(m_branchLabel, Qt::ElideRight, branchLabelWidth)
        : m_branchLabel;
    setTextOnce(m_branchButton, icon(kBranch) + label + chevron());

    // Stacked, all four belong to the dropdown: none of them is folded into
    // More, which is there anyway for its own entries. More is the design's
    // 28 px square in both presentations, a sync button's icon form the bare
    // button's 32; setting them again on every level keeps their widths on
    // the text size of the moment.
    m_foldedSync.clear();
    setIconForm(m_more, true);
    if (m_stacked) {
        for (const SyncControl &c : std::as_const(m_syncControls))
            c.button->setVisible(false);
        m_more->setVisible(true);
        setDropdownMini(m_syncDropdown, twoRows(level));
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
            setIconForm(c.button, !fold.syncLabels, kBareButton); // the glyph centred
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
    // The controls are centred in the first row, however many the row holds.
    const int height = m_metrics.height, width = m_row->width();
    const auto put = [height](QWidget *w, int x, int width) {
        const int h = qMin(height, w->sizeHint().height());
        w->setGeometry(x, (height - h) / 2, width, h);
        w->show();
    };

    const bool repoLabel = !m_stacked && kFolds[level].repoLabel;
    int x = 0;
    put(m_repoButton, x, repoLabel ? m_metrics.repoFull : m_metrics.repoFolded);
    x += (repoLabel ? m_metrics.repoFull : m_metrics.repoFolded) + space(gap::cluster);
    const int branch = m_metrics.branchChrome + branchLabelWidth;
    put(m_branchButton, x, branch);
    const int leftEnd = x + branch;

    // The right group hangs off the right edge, in the order it reads in:
    // the sync buttons, the more menu, the divider, then the two toggles —
    // or, stacked, the sync dropdown and More. A badge overhangs its button's
    // right edge by space(4), into the gap after it, which always lands inside
    // the row: the ordinary row ends with the toggles, which carry none, and
    // the stacked one with More, which carries none there either (nothing is
    // folded into it, so updateMoreMark() leaves it bare).
    x = width - rightGroupWidth(level);
    const int rightStart = x;
    if (m_stacked) {
        // As tall as the first row: the design's dropdown is its height.
        const int dropdown = dropdownWidth(m_syncDropdown, twoRows(level));
        m_syncDropdown->setGeometry(x, 0, dropdown, height);
        m_syncDropdown->show();
        x += dropdown + space(gap::item);
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
        x += w + space(gap::item);
    }
    if (!m_foldedSync.isEmpty()) {
        put(m_more, x, m_metrics.more);
        x += m_metrics.more + space(gap::item);
    }
    // The group gap from the last button's edge, the divider standing at the
    // start of its right half (8 | 8).
    const int groupStart = x - space(gap::item);
    const int dividerHeight = space(box::divider);
    m_divider->setGeometry(groupStart + space(gap::group / 2), (height - dividerHeight) / 2, 1, dividerHeight);
    m_divider->show();
    x = groupStart + m_metrics.divider;
    put(m_layoutButton, x, widthOf(m_layoutButton));
    x += widthOf(m_layoutButton) + space(gap::cluster);
    put(m_diffToggle, x, widthOf(m_diffToggle));

    placeTabs(leftEnd, rightStart, level);
}

// The tabs sit in the middle of the whole bar, nudged aside as far as they
// have to be to keep clear of either group; on two rows they are the second
// one, its segments sharing the row's whole width.
void TopBar::placeTabs(int leftEnd, int rightStart, int level)
{
    const int height = m_metrics.height, width = m_row->width();
    const bool own = twoRows(level);
    if (m_tabs->isStretch() != own)
        m_tabs->setStretch(own);
    if (own) {
        m_tabs->setGeometry(0, height + space(kBar), width, height);
    } else {
        const int tabs = tabLabels(level) ? m_metrics.tabsLabels : m_metrics.tabsGlyphs;
        const int low = leftEnd + space(gap::group), high = rightStart - space(gap::group) - tabs;
        m_tabs->setGeometry(qMax(low, qMin(qRound((width - tabs) / 2.0), high)), 0, tabs, height);
    }
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
    // clear() leaves the submenus the last fill made behind, as children.
    qDeleteAll(m_moreMenu->findChildren<QMenu *>(Qt::FindDirectChildrenOnly));
    m_moreMenu->setFixedWidth(popupWidth(window(), kMoreMenuWidth));
    // Whatever the window moved in here at this width comes first.
    emit fillingMoreMenu(m_moreMenu);
    if (!m_moreMenu->actions().isEmpty())
        m_moreMenu->addSeparator();
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
    add(kKeyboard, tr("Keybindings"), tr("Every keyboard shortcut (Ctrl+K)"), &TopBar::keybindingsRequested);
}

// The stacked row's four sync actions: the buttons' own clicks, in the order
// they stand in on a wide row, Merge (which opens a view) after a separator.
void TopBar::fillSyncMenu()
{
    m_syncMenu->clear();
    m_syncMenu->setFixedWidth(popupWidth(window(), kSyncMenuWidth));
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
