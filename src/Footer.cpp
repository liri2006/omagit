#include "Footer.h"
#include "OmarchyTheme.h"
#include "UiHelpers.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

using namespace ui;

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
    // Small regular dim text that elides a path too long for a narrow window
    // instead of cutting it off; the object name gives it its style.
    m_statusLabel = new ElidedLabel;
    m_statusLabel->setObjectName(QStringLiteral("footerStatus"));
    m_statusTimer = new QTimer(this);
    m_statusTimer->setSingleShot(true);
    connect(m_statusTimer, &QTimer::timeout, this, [this] { m_statusLabel->setFullText(m_idleText); });
    footer->addWidget(m_statusLabel, 1);
    m_settingsButton = iconButton(kCog, tr("⚙"), tr("Settings (Ctrl+,)"));
    m_settingsButton->setAccessibleName(tr("Settings"));
    footer->addWidget(m_settingsButton);
    m_keybindingsButton = iconButton(kKeyboard, tr("K"), tr("Keybindings (Ctrl+K)"));
    m_keybindingsButton->setAccessibleName(tr("Keybindings"));
    footer->addWidget(m_keybindingsButton);
    rootLayout->addLayout(footer);
    applyTheme();
    connect(OmarchyTheme::instance(), &OmarchyTheme::changed, this, &Footer::applyTheme);
}

void Footer::setDensity(const Density &density)
{
    if (m_density == density)
        return;
    m_density = density;
    applyTheme();
}

// The design's footer (screens.js footer()): 28 px, its hairline the top
// pixel row; the status on the window's side margin, and the keybindings
// button a 24 px ghost square flush with the other margin, centred in the 28,
// the settings button the same square an item gap before it.
void Footer::applyTheme()
{
    const int margin = space(m_density.margin);
    const int top = (space(box::footer) - space(box::row)) / 2 - 1; // under the hairline's row
    m_row->setContentsMargins(margin, top, margin, space(box::footer) - 1 - top - space(box::row));
    m_row->setSpacing(space(gap::item));
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
