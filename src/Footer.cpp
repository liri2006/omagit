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
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(8);
    rootLayout->addWidget(hairline());
    auto *footer = new QHBoxLayout;
    footer->setSpacing(8);
    // A dim label, styled and themed like every other (the object name), that
    // elides a path too long for a narrow window instead of cutting it off.
    m_statusLabel = new ElidedLabel;
    m_statusLabel->setObjectName(QStringLiteral("dimLabel"));
    m_statusLabel->setFont(OmarchyTheme::instance()->captionFont());
    m_statusTimer = new QTimer(this);
    m_statusTimer->setSingleShot(true);
    connect(m_statusTimer, &QTimer::timeout, this, [this] { m_statusLabel->setFullText(m_idleText); });
    footer->addWidget(m_statusLabel, 1);
    m_keybindingsButton = toolButton(icon(kInfo, tr("i")).trimmed(), tr("Keybindings (Ctrl+K)"));
    m_keybindingsButton->setObjectName(QStringLiteral("keybindingsButton"));
    m_keybindingsButton->setAccessibleName(tr("Keybindings"));
    footer->addWidget(m_keybindingsButton);
    rootLayout->addLayout(footer);
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
