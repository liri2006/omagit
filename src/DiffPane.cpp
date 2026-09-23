#include "DiffPane.h"
#include "DiffView.h"
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
constexpr int kLabelledWidth = 900; // every button labelled from here up
constexpr int kMiddleWidth = 560;   // the view options as glyphs from here up
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
    rightLayout->setSpacing(8);

    QHBoxLayout *navRow = m_navRow = new QHBoxLayout;
    m_prevButton = toolButton(icon(kArrowUp) + tr("Prev"), tr("Previous change (Shift+F8)"));
    m_nextButton = toolButton(icon(kArrowDown) + tr("Next"), tr("Next change (F8)"));
    m_changeLabel = dimLabel();
    // Let the label shrink instead of forcing the splitter to widen the diff pane.
    m_changeLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_changeLabel->setMinimumWidth(0);
    navRow->addWidget(m_prevButton);
    navRow->addWidget(m_nextButton);
    navRow->addWidget(m_changeLabel, 1);
    QToolButton *paneButton = m_paneButton = toolButton(icon(kSplit) + tr("Two-pane"),
                                  tr("Toggle between two-pane (side by side) and one-pane view (Ctrl+T)"));
    paneButton->setCheckable(true);
    navRow->addWidget(paneButton);
    QToolButton *wsButton = m_wsButton = toolButton(icon(kPilcrow) + tr("Whitespace"), tr("Show whitespace and line endings (Ctrl+W)"));
    wsButton->setCheckable(true);
    navRow->addWidget(wsButton);
    QToolButton *syntaxButton = m_syntaxButton = toolButton(icon(kCodeTags) + tr("Syntax"),
                                  tr("Colour the diff by the file's syntax (Ctrl+L)"));
    syntaxButton->setCheckable(true);
    navRow->addWidget(syntaxButton);
    // A glyph alone says nothing to a screen reader: each button keeps its
    // label as its name in every form.
    m_prevButton->setAccessibleName(tr("Prev"));
    m_nextButton->setAccessibleName(tr("Next"));
    paneButton->setAccessibleName(tr("Two-pane"));
    wsButton->setAccessibleName(tr("Whitespace"));
    syntaxButton->setAccessibleName(tr("Syntax"));
    // The narrowest form's stand-in for the three options, the way the
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
        paneButton->setChecked(twoPane);
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
    connect(paneButton, &QToolButton::toggled, m_diff, &DiffView::setTwoPane);
    connect(m_diff, &DiffView::modeChanged, this, [paneButton](DiffView::Mode mode) {
        QSignalBlocker blocker(paneButton);
        paneButton->setChecked(mode == DiffView::TwoPane);
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
    const bool glyphOptions = m_form != Form::Labelled;
    const auto wear = [](QToolButton *b, uint glyph, const QString &label, bool glyphOnly) {
        setTextOnce(b, glyphOnly ? icon(glyph).trimmed() : icon(glyph) + label);
        setIconForm(b, glyphOnly);
    };
    wear(m_prevButton, kArrowUp, tr("Prev"), compact);
    wear(m_nextButton, kArrowDown, tr("Next"), compact);
    wear(m_paneButton, kSplit, tr("Two-pane"), glyphOptions);
    wear(m_wsButton, kPilcrow, tr("Whitespace"), glyphOptions);
    wear(m_syntaxButton, kCodeTags, tr("Syntax"), glyphOptions);
    for (QToolButton *b : {m_paneButton, m_wsButton, m_syntaxButton})
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

// "Change n of m   ·   summary", or just "n/m" in the narrowest form.
void DiffPane::updateChangeLabel()
{
    const int n = m_changeIndex < 0 ? 0 : m_changeIndex + 1;
    QString text;
    if (m_form == Form::Compact) {
        if (m_changeTotal > 0)
            text = QStringLiteral("%1/%2").arg(n).arg(m_changeTotal);
    } else {
        text = m_changeTotal == 0 ? QString() : tr("Change %1 of %2").arg(n).arg(m_changeTotal);
        if (!m_summary.isEmpty())
            text += (text.isEmpty() ? QString() : QStringLiteral("   ·   ")) + m_summary;
    }
    if (m_changeLabel->text() != text)
        m_changeLabel->setText(text);
}

// What the narrowest form keeps behind "…": the three options as they are at
// the moment the menu opens, each entry taking the button's own path.
void DiffPane::fillOptionsMenu()
{
    m_optionsMenu->clear();
    const struct {
        uint glyph;
        QString label;
        QToolButton *button;
    } entries[] = {{kSplit, tr("Two-pane"), m_paneButton},
                   {kPilcrow, tr("Whitespace"), m_wsButton},
                   {kCodeTags, tr("Syntax"), m_syntaxButton}};
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
    m_paneButton->toggle();
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
