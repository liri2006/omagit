#include "Footer.h"
#include "OmarchyTheme.h"
#include "UiHelpers.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

using namespace ui;

namespace {
// The design's footer (screens.js footer()), in 12 px-base pixels: 28 px with
// its hairline, the status the window's margin in, and the keybindings button
// a 24 px ghost square the margin from the right edge, 2 px under the hairline.
constexpr int kButtonTop = 1;    // between the hairline and the button: y + 2 in the design
constexpr int kButtonBottom = 2;
} // namespace

Footer::Footer(QWidget *parent)
    : QWidget(parent)
{
    // The hairline runs from edge to edge, like the top bar's; the row under
    // it keeps the window's margin.
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    rootLayout->addWidget(hairline(Qt::Horizontal, HairlineTone::Chrome));
    auto *footer = m_row = new QHBoxLayout;
    footer->setSpacing(8);
    // Small regular dim text that elides a path too long for a narrow window
    // instead of cutting it off; the object name gives it its style.
    m_statusLabel = new ElidedLabel;
    m_statusLabel->setObjectName(QStringLiteral("footerStatus"));
    m_statusTimer = new QTimer(this);
    m_statusTimer->setSingleShot(true);
    connect(m_statusTimer, &QTimer::timeout, this, [this] { m_statusLabel->setFullText(m_idleText); });
    footer->addWidget(m_statusLabel, 1);
    m_keybindingsButton = iconButton(kKeyboard, tr("K"), tr("Keybindings (Ctrl+K)"));
    m_keybindingsButton->setAccessibleName(tr("Keybindings"));
    footer->addWidget(m_keybindingsButton);
    rootLayout->addLayout(footer);
    applyTheme();
    connect(OmarchyTheme::instance(), &OmarchyTheme::changed, this, &Footer::applyTheme);
}

void Footer::applyTheme()
{
    m_row->setContentsMargins(windowMargin(), space(kButtonTop), windowMargin(), space(kButtonBottom));
}

void Footer::showStatus(const QString &text, int ms)
{
    m_statusLabel->setFullText(text);
    if (ms > 0)
        m_statusTimer->start(ms);
    else
        m_statusTimer->stop();
}

void Footer::setIdleText(const QString &text)
{
    m_idleText = text;
    m_statusTimer->stop();
    m_statusLabel->setFullText(text);
}
