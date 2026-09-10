#include "DiffPane.h"
#include "DiffView.h"
#include "UiHelpers.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QSettings>
#include <QToolButton>
#include <QVBoxLayout>

using namespace ui;

DiffPane::DiffPane(QWidget *parent)
    : QWidget(parent)
{
    // Like the left section, the diff pane may be dragged as narrow as the
    // user likes; its buttons just get cut off at the edge.
    setMinimumWidth(1);
    auto *rightLayout = new QVBoxLayout(this);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(8);

    auto *navRow = new QHBoxLayout;
    m_navRow = navRow;
    navRow->setSpacing(8);
    m_prevButton = toolButton(icon(kArrowUp) + tr("Prev"), tr("Previous change (Shift+F8)"));
    m_nextButton = toolButton(icon(kArrowDown) + tr("Next"), tr("Next change (F8)"));
    m_changeLabel = dimLabel();
    // Let the label shrink instead of forcing the splitter to widen the diff pane.
    m_changeLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_changeLabel->setMinimumWidth(0);
    navRow->addWidget(m_prevButton);
    navRow->addWidget(m_nextButton);
    navRow->addSpacing(4);
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
    // The diff toggle ends this row; while the pane is hidden it moves to the
    // end of the toolbar row, which is the same top-right spot (see applyPanes).
    m_diffToggle = toolButton(icon(kDockRight, tr("D")).trimmed(), tr("Show or hide the diff pane (Ctrl+Shift+B)"));
    m_diffToggle->setCheckable(true);
    navRow->addWidget(m_diffToggle);
    rightLayout->addLayout(navRow);

    m_diff = new DiffView;
    m_diff->setFrameShape(QFrame::NoFrame);
    rightLayout->addWidget(m_diff, 1);
    connect(m_prevButton, &QToolButton::clicked, m_diff, &DiffView::previousChange);
    connect(m_nextButton, &QToolButton::clicked, m_diff, &DiffView::nextChange);
    connect(wsButton, &QToolButton::toggled, m_diff, &DiffView::setShowWhitespace);
    {
        QSettings settings;
        const bool twoPane = settings.value(QStringLiteral("diff/twoPane"), true).toBool();
        m_diff->setMode(twoPane ? DiffView::TwoPane : DiffView::OnePane);
        paneButton->setChecked(twoPane);
        const bool syntax = settings.value(QStringLiteral("diff/syntaxHighlighting"), true).toBool();
        m_diff->setSyntaxHighlighting(syntax);
        syntaxButton->setChecked(syntax);
    }
    connect(syntaxButton, &QToolButton::toggled, m_diff, &DiffView::setSyntaxHighlighting);
    connect(m_diff, &DiffView::syntaxHighlightingChanged, this, [syntaxButton](bool on) {
        QSignalBlocker blocker(syntaxButton);
        syntaxButton->setChecked(on);
        QSettings().setValue(QStringLiteral("diff/syntaxHighlighting"), on);
    });
    connect(paneButton, &QToolButton::toggled, m_diff, &DiffView::setTwoPane);
    connect(m_diff, &DiffView::modeChanged, this, [paneButton](DiffView::Mode mode) {
        QSignalBlocker blocker(paneButton);
        paneButton->setChecked(mode == DiffView::TwoPane);
        QSettings().setValue(QStringLiteral("diff/twoPane"), mode == DiffView::TwoPane);
    });
    connect(m_diff, &DiffView::changeIndexChanged, this, [this](int index, int total) {
        m_prevButton->setEnabled(total > 0 && index > 0);
        m_nextButton->setEnabled(total > 0 && index < total - 1);
        QString text = total == 0 ? QString() : tr("Change %1 of %2").arg(index < 0 ? 0 : index + 1).arg(total);
        if (!m_summary.isEmpty())
            text += (text.isEmpty() ? QString() : QStringLiteral("   ·   ")) + m_summary;
        m_changeLabel->setText(text);
    });
}

void DiffPane::applyTheme()
{
    m_diff->refreshTheme();
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
