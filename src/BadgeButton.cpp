#include "BadgeButton.h"
#include "OmarchyTheme.h"

#include <QPainter>
#include <QTimerEvent>
#include <QVariantAnimation>

BadgeButton::BadgeButton(QWidget *parent)
    : QToolButton(parent)
{
    m_pop = new QVariantAnimation(this);
    m_pop->setDuration(420);
    m_pop->setEasingCurve(QEasingCurve::OutBack);
    m_pop->setStartValue(0.0);
    m_pop->setEndValue(1.0);
    connect(m_pop, &QVariantAnimation::valueChanged, this, qOverload<>(&QWidget::update));
}

QSize BadgeButton::sizeHint() const
{
    return QToolButton::sizeHint() + QSize(14, 0);
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
    update();
    emit badgeChanged();
}

void BadgeButton::setMark(const QString &text, const QColor &color)
{
    if (m_mark == text && m_markColor == color)
        return;
    m_mark = text;
    m_markColor = color;
    update();
    emit badgeChanged();
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
    update();
    emit badgeChanged();
}

void BadgeButton::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == m_busyTimer) {
        m_busyPhase = (m_busyPhase + 1) % 3;
        update();
        return;
    }
    QToolButton::timerEvent(event);
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

void BadgeButton::paintEvent(QPaintEvent *event)
{
    QToolButton::paintEvent(event);
    const QString text = badgeText();
    if (text.isEmpty() && !m_busy)
        return;

    const OmarchyTheme *t = OmarchyTheme::instance();
    QFont f = t->captionFont();
    f.setBold(true);
    f.setPixelSize(qMax(8, f.pixelSize() - 1));
    const QFontMetrics fm(f);
    const int h = fm.height() + 2;
    const int w = m_busy ? h + 6 : qMax(h, fm.horizontalAdvance(text) + 8);
    // Inside the button, overlapping the top-right corner of its border.
    QRectF badge(width() - w - 3, 2, w, h);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    qreal scale = 1.0;
    if (m_pop->state() == QVariantAnimation::Running)
        scale = 0.4 + 0.6 * m_pop->currentValue().toReal();
    if (scale != 1.0) {
        const QPointF c = badge.center();
        p.translate(c);
        p.scale(scale, scale);
        p.translate(-c);
    }
    QColor fill = m_busy ? t->fill(0.30) : (!m_mark.isEmpty() ? m_markColor : t->accent());
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawRoundedRect(badge, h / 2.0, h / 2.0);
    if (m_busy) {
        // Three dots, the active one in the accent colour, walking left to right.
        const qreal r = qMax(1.5, h / 7.0);
        const qreal step = badge.width() / 4.0;
        for (int i = 0; i < 3; ++i) {
            p.setBrush(i == m_busyPhase ? t->accent() : t->mutedText());
            p.drawEllipse(QPointF(badge.left() + step * (i + 1), badge.center().y()), r, r);
        }
        return;
    }
    p.setFont(f);
    p.setPen(t->window());
    p.drawText(badge, Qt::AlignCenter, text);
}
