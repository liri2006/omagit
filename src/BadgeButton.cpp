#include "BadgeButton.h"
#include "OmarchyTheme.h"
#include "UiHelpers.h"

#include <QEvent>
#include <QPainter>
#include <QTimerEvent>
#include <QVariantAnimation>

namespace {
// The design's badge (design/figma-gen/kit.js badge()), in 12 px-base pixels:
// its right edge this far past the button's, its top this far above it.
constexpr int kBadgeRight = 4;
constexpr int kBadgeRise = 5;
constexpr int kBadgeHeight = 14;
constexpr int kBadgeTextPad = 8; // both sides of the text together
constexpr int kBusyExtra = 6;    // the walking dots' box over a square
} // namespace

BadgeButton::BadgeButton(QWidget *parent)
    : ui::KitButton(parent)
{
    m_pop = new QVariantAnimation(this);
    m_pop->setDuration(420);
    m_pop->setEasingCurve(QEasingCurve::OutBack);
    m_pop->setStartValue(0.0);
    m_pop->setEndValue(1.0);
    connect(m_pop, &QVariantAnimation::valueChanged, this, &BadgeButton::badgeRepaint);
}

// The caption font, bold and a shade smaller: small enough for a corner,
// still legible against the accent colour.
QFont BadgeButton::badgeFont() const
{
    QFont f = OmarchyTheme::instance()->captionFont();
    f.setBold(true);
    f.setPixelSize(qMax(8, f.pixelSize() - 1));
    return f;
}

void BadgeButton::setCount(int count)
{
    count = qMax(0, count);
    if (count == m_count)
        return;
    const bool grew = count > m_count;
    m_count = count;
    if (grew && !m_busy && isVisible())
        m_pop->start();
    emit badgeChanged();
}

void BadgeButton::setMark(const QString &text, const QColor &color)
{
    if (m_mark == text && m_markColor == color)
        return;
    m_mark = text;
    m_markColor = color;
    emit badgeChanged();
}

QString BadgeButton::markText() const
{
    return m_mark;
}

QColor BadgeButton::markColor() const
{
    return m_markColor;
}

void BadgeButton::setBusy(bool busy)
{
    if (busy == m_busy)
        return;
    m_busy = busy;
    m_busyPhase = 0;
    if (busy) {
        m_pop->stop();
        m_busyTimer = startTimer(350);
    } else {
        killTimer(m_busyTimer);
        m_busyTimer = 0;
        if (m_count > 0 && isVisible())
            m_pop->start(); // the number is fresh: show it off
    }
    emit badgeChanged();
}

void BadgeButton::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == m_busyTimer) {
        m_busyPhase = (m_busyPhase + 1) % 3;
        emit badgeRepaint();
        return;
    }
    ui::KitButton::timerEvent(event);
}

QString BadgeButton::badgeText() const
{
    if (m_busy)
        return QString();
    if (!m_mark.isEmpty())
        return m_mark;
    if (m_count > 999)
        return QStringLiteral("999+");
    return m_count > 0 ? QString::number(m_count) : QString();
}

bool BadgeButton::hasBadge() const
{
    return m_busy || !badgeText().isEmpty();
}

QRect BadgeButton::badgeRect(const QRect &button) const
{
    const int h = ui::space(kBadgeHeight);
    const int w = m_busy ? h + ui::space(kBusyExtra)
                         : qMax(h, QFontMetrics(badgeFont()).horizontalAdvance(badgeText()) + ui::space(kBadgeTextPad));
    const int right = button.right() + 1 + ui::space(kBadgeRight); // the edge, one past the last column
    return QRect(right - w, button.top() - ui::space(kBadgeRise), w, h);
}

void BadgeButton::paintBadge(QPainter *p, const QRect &button) const
{
    if (!hasBadge())
        return;
    const OmarchyTheme *t = OmarchyTheme::instance();
    const QRectF badge = badgeRect(button);

    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    qreal scale = 1.0;
    if (m_pop->state() == QVariantAnimation::Running)
        scale = 0.4 + 0.6 * m_pop->currentValue().toReal();
    if (scale != 1.0) {
        const QPointF c = badge.center();
        p->translate(c);
        p->scale(scale, scale);
        p->translate(-c);
    }
    // Square, like the design's: a plain box over the corner.
    const QColor fill = m_busy ? t->fill(0.30) : (!m_mark.isEmpty() ? m_markColor : t->accent());
    p->setPen(Qt::NoPen);
    p->setBrush(fill);
    p->drawRect(badge);
    if (m_busy) {
        // Three dots, the active one in the accent colour, walking left to right.
        const qreal r = qMax(1.5, badge.height() / 7.0);
        const qreal step = badge.width() / 4.0;
        for (int i = 0; i < 3; ++i) {
            p->setBrush(i == m_busyPhase ? t->accent() : t->mutedText());
            p->drawEllipse(QPointF(badge.left() + step * (i + 1), badge.center().y()), r, r);
        }
    } else {
        p->setFont(badgeFont());
        p->setPen(t->window());
        p->drawText(badge, Qt::AlignCenter, badgeText());
    }
    p->restore();
}

BadgeLayer::BadgeLayer(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::NoFocus);
}

void BadgeLayer::watch(BadgeButton *button)
{
    m_buttons << button;
    connect(button, &BadgeButton::badgeChanged, this, qOverload<>(&QWidget::update));
    connect(button, &BadgeButton::badgeRepaint, this, qOverload<>(&QWidget::update));
    button->installEventFilter(this);
    update();
}

// The button's place on the layer is what the badge hangs off.
bool BadgeLayer::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::Move:
    case QEvent::Resize:
    case QEvent::Show:
    case QEvent::Hide:
        update();
        break;
    default:
        break;
    }
    return QWidget::eventFilter(watched, event);
}

void BadgeLayer::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    for (const QPointer<BadgeButton> &b : std::as_const(m_buttons)) {
        if (!b || !b->isVisible() || !b->hasBadge())
            continue;
        // Through the window both share: the layer lies over the buttons'
        // row, beside it rather than above it in the widget tree.
        const QPoint at = mapFrom(window(), b->mapTo(window(), QPoint(0, 0)));
        b->paintBadge(&p, QRect(at, b->size()));
    }
}
