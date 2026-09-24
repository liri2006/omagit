#include "DiffPane.h"
#include "DiffView.h"
#include "OmarchyTheme.h"
#include "Settings.h"
#include "TickMenu.h"
#include "UiHelpers.h"

#include <QAction>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>

using namespace ui;

namespace {

void saveOption(QLatin1StringView key, const QVariant &value)
{
    QSettings().setValue(key, value);
}

// The row's forms change at these pane widths (screens.js diffPane()), in
// 12 px-base pixels: fixed steps rather than measured fits, because the row's
// content is bounded and they scale with the text size like the rest.
constexpr int kLabelledWidth = 900; // the view dropdown labelled from here up
constexpr int kMiddleWidth = 560;   // Prev, Next and the view options on the row from here up
// The design's gaps: between Prev and Next and between the view options, and
// on either side of the counter.
constexpr int kButtonGap = 4;
constexpr int kCounterGap = 10;

// A resize that keeps the form must not relay out or repaint a button.
void setTextOnce(QToolButton *b, const QString &text)
{
    if (b->text() != text)
        b->setText(text);
}

} // namespace

DiffPane::DiffPane(QWidget *parent)
    : QWidget(parent)
{
    // Like the left section, the diff pane may be dragged as narrow as the
    // user likes: the row folds down to its narrowest form (applyForm()), and
    // below that its buttons just get cut off at the edge.
    setMinimumWidth(1);
    auto *rightLayout = new QVBoxLayout(this);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(barGap()); // the toolbar to the diff (applyTheme() rescales it)

    QHBoxLayout *navRow = m_navRow = new QHBoxLayout;
    m_prevButton = toolButton<KitButton>(icon(kArrowUp) + tr("Prev"), tr("Previous change (Shift+F8)"));
    m_nextButton = toolButton<KitButton>(icon(kArrowDown) + tr("Next"), tr("Next change (F8)"));
    m_changeLabel = dimLabel();
    // Let the label shrink instead of forcing the splitter to widen the diff pane.
    m_changeLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_changeLabel->setMinimumWidth(0);
    m_changeLabel->setTextFormat(Qt::RichText); // the summary's colours
    navRow->addWidget(m_prevButton);
    navRow->addWidget(m_nextButton);
    navRow->addWidget(m_changeLabel, 1);
    // The view dropdown (screens.js diffPane(): ViewMode): Split or Unified
    // from a menu, the current one ticked.
    m_viewButton = toolButton<KitButton>(QString(),
                                         tr("Split (side by side) or unified (one pane) view (Ctrl+T)"));
    m_viewButton->setPopupMode(QToolButton::InstantPopup);
    m_viewMenu = new TickMenu(m_viewButton);
    m_viewMenu->setToolTipsVisible(true);
    m_viewButton->setMenu(m_viewMenu);
    connect(m_viewMenu, &QMenu::aboutToShow, this, &DiffPane::fillViewMenu);
    keepMenuInWindow(m_viewMenu, m_viewButton);
    navRow->addWidget(m_viewButton);
    QToolButton *wsButton = m_wsButton = toolButton<KitButton>(icon(kPilcrow, QStringLiteral("¶")).trimmed(),
                                                              tr("Show whitespace and line endings (Ctrl+W)"));
    wsButton->setCheckable(true);
    navRow->addWidget(wsButton);
    QToolButton *syntaxButton = m_syntaxButton = toolButton<KitButton>(
        icon(kCodeTags, QStringLiteral("<>")).trimmed(), tr("Colour the diff by the file's syntax (Ctrl+L)"));
    syntaxButton->setCheckable(true);
    navRow->addWidget(syntaxButton);
    // A glyph alone says nothing to a screen reader: each button keeps its
    // label as its name in every form.
    m_prevButton->setAccessibleName(tr("Prev"));
    m_nextButton->setAccessibleName(tr("Next"));
    m_viewButton->setAccessibleName(tr("View"));
    wsButton->setAccessibleName(tr("Whitespace"));
    syntaxButton->setAccessibleName(tr("Syntax"));
    // The narrowest form's stand-in for the view options, the way the
    // stacked action bar keeps its own behind "…".
    m_optionsButton = iconButton(kDotsHorizontal, QStringLiteral("…"), tr("View options"), IconButtonSize::Toolbar, false);
    m_optionsButton->setAccessibleName(tr("View options"));
    m_optionsButton->setPopupMode(QToolButton::InstantPopup);
    m_optionsMenu = new TickMenu(m_optionsButton);
    m_optionsMenu->setToolTipsVisible(true);
    m_optionsButton->setMenu(m_optionsMenu);
    connect(m_optionsMenu, &QMenu::aboutToShow, this, &DiffPane::fillOptionsMenu);
    keepMenuInWindow(m_optionsMenu, m_optionsButton); // the button is at the pane's right edge
    m_optionsButton->hide(); // until the pane is narrow enough
    navRow->addWidget(m_optionsButton);
    rightLayout->addLayout(navRow);

    m_diff = new DiffView;
    m_diff->setFrameShape(QFrame::NoFrame);
    rightLayout->addWidget(m_diff, 1);
    connect(m_prevButton, &QToolButton::clicked, m_diff, &DiffView::previousChange);
    connect(m_nextButton, &QToolButton::clicked, m_diff, &DiffView::nextChange);
    connect(wsButton, &QToolButton::toggled, m_diff, &DiffView::setShowWhitespace);
    {
        QSettings conf;
        const bool twoPane = conf.value(settings::kDiffTwoPane, true).toBool();
        m_diff->setMode(twoPane ? DiffView::TwoPane : DiffView::OnePane);
        const bool syntax = conf.value(settings::kDiffSyntaxHighlighting, true).toBool();
        m_diff->setSyntaxHighlighting(syntax);
        syntaxButton->setChecked(syntax);
    }
    connect(syntaxButton, &QToolButton::toggled, m_diff, &DiffView::setSyntaxHighlighting);
    connect(m_diff, &DiffView::syntaxHighlightingChanged, this, [syntaxButton](bool on) {
        QSignalBlocker blocker(syntaxButton);
        syntaxButton->setChecked(on);
        saveOption(settings::kDiffSyntaxHighlighting, on);
    });
    // However the view changes (the menus, Ctrl+T, the view's own context
    // menu), the dropdown says so and the choice is kept.
    connect(m_diff, &DiffView::modeChanged, this, [this](DiffView::Mode mode) {
        updateViewButton();
        saveOption(settings::kDiffTwoPane, mode == DiffView::TwoPane);
    });
    connect(m_diff, &DiffView::changeIndexChanged, this, [this](int index, int total) {
        m_prevButton->setEnabled(total > 0 && index > 0);
        m_nextButton->setEnabled(total > 0 && index < total - 1);
        m_changeIndex = index;
        m_changeTotal = total;
        updateChangeLabel();
    });
    applyForm();
}

void DiffPane::applyTheme()
{
    m_diff->refreshTheme();
    layout()->setSpacing(barGap());
    // The glyphs are looked up in the font of the moment.
    m_wsButton->setText(icon(kPilcrow, QStringLiteral("¶")).trimmed());
    m_syntaxButton->setText(icon(kCodeTags, QStringLiteral("<>")).trimmed());
    // The thresholds, the gaps and the 28 px square all follow the text size.
    applyForm();
}

void DiffPane::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // A resize within the form of the moment changes nothing on the row.
    if (formForWidth() != m_form)
        applyForm();
}

DiffPane::Form DiffPane::formForWidth() const
{
    const int w = width();
    if (w >= space(kLabelledWidth))
        return Form::Labelled;
    return w >= space(kMiddleWidth) ? Form::Middle : Form::Compact;
}

// One set of buttons in every form: only their texts, widths and the
// visibility of the options change, so their connections and checked states
// (and the window's Ctrl+T / Ctrl+W / Ctrl+L on a hidden one) carry on.
void DiffPane::applyForm()
{
    m_form = formForWidth();
    const bool compact = m_form == Form::Compact;
    const auto wear = [](QToolButton *b, uint glyph, const QString &label, bool glyphOnly) {
        setTextOnce(b, glyphOnly ? icon(glyph).trimmed() : icon(glyph) + label);
        setIconForm(b, glyphOnly);
    };
    // The toolbar's counter carries the status and the +/− in every form
    // but the narrowest, which keeps only "n/m": only then does the header
    // under it say them.
    m_diff->setSubtitleShown(compact);
    wear(m_prevButton, kArrowUp, tr("Prev"), compact);
    wear(m_nextButton, kArrowDown, tr("Next"), compact);
    // Whitespace and Syntax are squares wherever they show.
    setIconForm(m_wsButton, true);
    setIconForm(m_syntaxButton, true);
    updateViewButton();
    for (QToolButton *b : {m_viewButton, m_wsButton, m_syntaxButton})
        if (b->isHidden() != compact)
            b->setHidden(compact);
    if (m_optionsButton->isHidden() == compact)
        m_optionsButton->setHidden(!compact);

    // The layout's own spacing is the gap between buttons; the counter's
    // wider gap is that plus the label's side margins.
    const int gap = space(kButtonGap), side = space(kCounterGap) - gap;
    if (m_navRow->spacing() != gap)
        m_navRow->setSpacing(gap);
    if (m_changeLabel->contentsMargins() != QMargins(side, 0, side, 0))
        m_changeLabel->setContentsMargins(side, 0, side, 0);
    updateChangeLabel();
}

void DiffPane::updateViewButton()
{
    const bool split = m_diff->mode() == DiffView::TwoPane;
    const uint glyph = split ? kSplit : kUnified;
    const QString name = split ? tr("Split") : tr("Unified");
    // The name where the pane has the room, the glyph and the chevron always.
    const QString face = m_form == Form::Labelled ? icon(glyph) + name : icon(glyph, name.left(1));
    setTextOnce(m_viewButton, face + chevron());
}

// "Change n of m   ·   summary", or just "n/m" in the narrowest form. The
// summary is small regular text in the colours of the design: the status in
// its own, the added lines green, the removed ones red.
void DiffPane::updateChangeLabel()
{
    const int n = m_changeIndex < 0 ? 0 : m_changeIndex + 1;
    QString text;
    if (m_form == Form::Compact) {
        if (m_changeTotal > 0)
            text = QStringLiteral("%1/%2").arg(n).arg(m_changeTotal);
    } else {
        text = m_changeTotal == 0 ? QString() : tr("Change %1 of %2").arg(n).arg(m_changeTotal);
        if (!m_summary.status.isEmpty()) {
            const OmarchyTheme *theme = OmarchyTheme::instance();
            const QString span = QStringLiteral("<span style=\"color:%1; font-size:%2px; font-weight:normal\">%3</span>");
            const int small = space(11);
            QString summary = span.arg(m_summary.colour.name()).arg(small).arg(m_summary.status.toHtmlEscaped());
            if (m_summary.added >= 0 && m_summary.removed >= 0) {
                summary += QStringLiteral("&nbsp;&nbsp;")
                    + span.arg(theme->diffAddedIcon().name()).arg(small).arg(QStringLiteral("+%1").arg(m_summary.added))
                    + QStringLiteral("&nbsp;")
                    + span.arg(theme->diffRemovedIcon().name()).arg(small).arg(QStringLiteral("−%1").arg(m_summary.removed));
            }
            text = text.toHtmlEscaped();
            text += (text.isEmpty() ? QString() : QStringLiteral("&nbsp;&nbsp;&nbsp;·&nbsp;&nbsp;&nbsp;")) + summary;
        }
    }
    if (m_changeLabel->text() != text)
        m_changeLabel->setText(text);
}

void DiffPane::addViewEntries(QMenu *menu)
{
    const bool split = m_diff->mode() == DiffView::TwoPane;
    const struct {
        uint glyph;
        QString label, tip;
        bool twoPane;
    } entries[] = {{kSplit, tr("Split"), tr("The two versions side by side (Ctrl+T)"), true},
                   {kUnified, tr("Unified"), tr("One pane, the removed lines over the added ones (Ctrl+T)"), false}};
    for (const auto &entry : entries) {
        QAction *action = menu->addAction(icon(entry.glyph) + entry.label);
        action->setCheckable(true);
        action->setChecked(split == entry.twoPane);
        action->setToolTip(entry.tip);
        connect(action, &QAction::triggered, this, [this, twoPane = entry.twoPane] { m_diff->setTwoPane(twoPane); });
    }
}

void DiffPane::fillViewMenu()
{
    m_viewMenu->clear();
    addViewEntries(m_viewMenu);
}

// What the narrowest form keeps behind "…": the view dropdown's choice, then
// the two options as they are at the moment the menu opens, each entry
// taking the button's own path.
void DiffPane::fillOptionsMenu()
{
    m_optionsMenu->clear();
    addViewEntries(m_optionsMenu);
    m_optionsMenu->addSeparator();
    const struct {
        uint glyph;
        QString label;
        QToolButton *button;
    } entries[] = {{kPilcrow, tr("Whitespace"), m_wsButton}, {kCodeTags, tr("Syntax"), m_syntaxButton}};
    for (const auto &entry : entries) {
        QAction *action = m_optionsMenu->addAction(icon(entry.glyph) + entry.label);
        action->setCheckable(true);
        action->setChecked(entry.button->isChecked());
        action->setToolTip(entry.button->toolTip());
        connect(action, &QAction::triggered, entry.button, &QAbstractButton::click);
    }
}

void DiffPane::showOptionsMenu()
{
    if (m_optionsButton->isVisible())
        m_optionsButton->showMenu();
}

void DiffPane::togglePaneMode()
{
    m_diff->setTwoPane(m_diff->mode() != DiffView::TwoPane);
}

void DiffPane::toggleWhitespace()
{
    m_wsButton->toggle();
}

void DiffPane::toggleSyntax()
{
    m_syntaxButton->toggle();
}

void DiffPane::nextChange()
{
    m_diff->nextChange();
}

void DiffPane::previousChange()
{
    m_diff->previousChange();
}

void DiffPane::zoomBy(int step)
{
    m_diff->zoomBy(step);
}

void DiffPane::resetZoom()
{
    m_diff->resetZoom();
}
