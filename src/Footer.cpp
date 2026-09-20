#include "Footer.h"
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
    m_statusLabel = dimLabel();
    m_statusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_statusLabel->setMinimumWidth(0);
    m_statusTimer = new QTimer(this);
    m_statusTimer->setSingleShot(true);
    connect(m_statusTimer, &QTimer::timeout, this, [this] { m_statusLabel->setText(m_idleText); });
    footer->addWidget(m_statusLabel, 1);
    m_keybindingsButton = toolButton(icon(kInfo, tr("i")).trimmed(), tr("Keybindings (Ctrl+K)"));
    m_keybindingsButton->setObjectName(QStringLiteral("keybindingsButton"));
    m_keybindingsButton->setAccessibleName(tr("Keybindings"));
    footer->addWidget(m_keybindingsButton);
    rootLayout->addLayout(footer);
}

void Footer::showStatus(const QString &text, int ms)
{
    m_statusLabel->setText(text);
    if (ms > 0)
        m_statusTimer->start(ms);
    else
        m_statusTimer->stop();
}

void Footer::setIdleText(const QString &text)
{
    m_idleText = text;
    m_statusTimer->stop();
    m_statusLabel->setText(text);
}
