#include "Toolbar.h"
#include "BadgeButton.h"
#include "OmarchyTheme.h"
#include "TickMenu.h"
#include "UiHelpers.h"

#include <QAction>
#include <QMenu>
#include <QResizeEvent>
#include <QToolButton>

namespace {
constexpr uint kDots = 0xF01D8; // md-dots_horizontal
// A separator is shorter than the buttons beside it, like the shell's own.
constexpr int kSeparatorHeight = 22;
}

Toolbar::Toolbar(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_more = new BadgeButton(this);
    m_more->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_more->setCursor(Qt::PointingHandCursor);
    m_more->setFocusPolicy(Qt::NoFocus);
    m_more->setPopupMode(QToolButton::InstantPopup);
    m_more->setToolTip(tr("More — the buttons that do not fit"));
    m_moreMenu = new TickMenu(m_more);
    m_moreMenu->setToolTipsVisible(true);
    m_more->setMenu(m_moreMenu);
    m_more->hide();
    connect(m_moreMenu, &QMenu::aboutToShow, this, &Toolbar::fillMoreMenu);
    applyTheme();
}

void Toolbar::setLeading(QToolButton *button)
{
    m_leading = button;
    button->setParent(this);
    button->show();
    measure();
}

void Toolbar::addButton(QToolButton *button, const QString &fullText, const QString &iconText, const QString &menuText)
{
    Item item;
    item.button = button;
    item.full = fullText;
    item.icon = iconText;
    item.menu = menuText;
    button->setParent(this);
    button->show();
    if (auto *badge = qobject_cast<BadgeButton *>(button))
        connect(badge, &BadgeButton::badgeChanged, this, &Toolbar::updateMoreMark);
    m_items << item;
    measure();
}

void Toolbar::addSeparator()
{
    Item item;
    item.separator = ui::hairline(Qt::Vertical);
    item.separator->setParent(this);
    item.separator->show();
    m_items << item;
    applyTheme();
}

void Toolbar::applyTheme()
{
    const QString dots = OmarchyTheme::instance()->glyph(kDots);
    m_more->setText(dots.isEmpty() ? QStringLiteral("…") : dots);
    measure();
}

// Both widths of every button come from its own sizeHint, so the stylesheet's
// padding and the font are accounted for without second-guessing the style.
void Toolbar::measure()
{
    m_height = 0;
    for (Item &item : m_items) {
        if (!item.button)
            continue;
        item.button->setText(item.full);
        const QSize full = item.button->sizeHint();
        item.button->setText(item.icon);
        const QSize icon = item.button->sizeHint();
        item.fullWidth = full.width();
        item.iconWidth = icon.width();
        m_height = qMax(m_height, qMax(full.height(), icon.height()));
    }
    if (m_leading)
        m_height = qMax(m_height, m_leading->sizeHint().height());
    m_height = qMax(m_height, m_more->sizeHint().height());
    updateGeometry();
    relayout();
}

QSize Toolbar::sizeHint() const
{
    int w = m_leading ? m_leading->sizeHint().width() + kSpacing : 0;
    for (const Item &item : m_items)
        w += item.width(true) + kSpacing;
    return QSize(qMax(0, w - kSpacing), m_height);
}

// Never wider than the leading button plus the more button: the buttons
// fold away instead of forcing a minimum width on the pane.
QSize Toolbar::minimumSizeHint() const
{
    const int w = (m_leading ? m_leading->sizeHint().width() + kSpacing : 0) + m_more->sizeHint().width();
    return QSize(w, m_height);
}

void Toolbar::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    relayout();
}

void Toolbar::relayout()
{
    auto place = [this](QWidget *w, int x, int width) {
        const int h = w->sizeHint().height();
        w->setGeometry(x, (m_height - h) / 2, width, h);
        w->show();
    };
    int x = 0;
    if (m_leading) {
        const int lw = m_leading->sizeHint().width();
        place(m_leading, 0, lw);
        x = lw + kSpacing;
    }
    const int avail = width() - x;

    int fullTotal = 0, iconTotal = 0;
    for (const Item &item : std::as_const(m_items)) {
        fullTotal += item.width(true) + kSpacing;
        iconTotal += item.width(false) + kSpacing;
    }
    fullTotal -= kSpacing;
    iconTotal -= kSpacing;

    const bool full = fullTotal <= avail;
    const bool overflow = !full && iconTotal > avail;
    const int moreWidth = m_more->sizeHint().width();
    const int limit = overflow ? width() - moreWidth - kSpacing : width();

    // Which items fit, in order; a separator only counts when a button follows it.
    QList<bool> shown(m_items.size(), false);
    int cursor = x, lastButton = -1;
    for (int i = 0; i < m_items.size(); ++i) {
        const int w = m_items[i].width(full);
        if (cursor + w > limit)
            break;
        shown[i] = true;
        cursor += w + kSpacing;
        if (m_items[i].button)
            lastButton = i;
    }
    for (int i = lastButton + 1; i < m_items.size(); ++i)
        shown[i] = false; // trailing separators
    for (int i = 0; i < m_items.size(); ++i)
        if (m_items[i].separator && shown[i] && i > 0 && !shown[i - 1])
            shown[i] = false;

    for (int i = 0; i < m_items.size(); ++i) {
        const Item &item = m_items[i];
        QWidget *w = item.button ? static_cast<QWidget *>(item.button) : item.separator;
        if (!shown[i]) {
            w->hide();
            continue;
        }
        if (item.button) {
            item.button->setText(full ? item.full : item.icon);
            place(item.button, x, item.width(full));
        } else {
            item.separator->setGeometry(x, (m_height - kSeparatorHeight) / 2, 1, kSeparatorHeight);
            item.separator->show();
        }
        x += item.width(full) + kSpacing;
    }
    if (overflow) {
        place(m_more, width() - moreWidth, moreWidth);
        updateMoreMark();
    } else {
        m_more->hide();
    }
}

// A dot stands in for the badges of the buttons that moved into the menu.
void Toolbar::updateMoreMark()
{
    bool counts = false;
    for (const Item &item : std::as_const(m_items))
        if (auto *b = qobject_cast<BadgeButton *>(item.button); b && !b->isVisible() && b->count() > 0)
            counts = true;
    m_more->setMark(counts ? QStringLiteral("•") : QString(), OmarchyTheme::instance()->accent());
}

// The hidden buttons as menu entries, with the badge counts spelled out.
void Toolbar::fillMoreMenu()
{
    m_moreMenu->clear();
    for (const Item &item : std::as_const(m_items)) {
        if (!item.button) {
            if (!m_moreMenu->actions().isEmpty() && !m_moreMenu->actions().last()->isSeparator())
                m_moreMenu->addSeparator();
            continue;
        }
        if (item.button->isVisible())
            continue;
        QString text = item.icon.isEmpty() ? item.menu : item.icon + QStringLiteral("  ") + item.menu;
        if (auto *badge = qobject_cast<BadgeButton *>(item.button); badge && badge->count() > 0)
            text += QStringLiteral("  (%1)").arg(badge->count());
        QAction *a = m_moreMenu->addAction(text);
        a->setToolTip(item.button->toolTip());
        a->setEnabled(item.button->isEnabled());
        if (item.button->isCheckable()) {
            a->setCheckable(true);
            a->setChecked(item.button->isChecked());
        }
        connect(a, &QAction::triggered, item.button, &QAbstractButton::click);
    }
    while (!m_moreMenu->actions().isEmpty() && m_moreMenu->actions().last()->isSeparator())
        m_moreMenu->removeAction(m_moreMenu->actions().last());
    while (!m_moreMenu->actions().isEmpty() && m_moreMenu->actions().first()->isSeparator())
        m_moreMenu->removeAction(m_moreMenu->actions().first());
}
