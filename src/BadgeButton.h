#pragma once

#include <QToolButton>

class QVariantAnimation;

// A tool button with a small count badge in its top-right corner, the way
// the Pull and Push buttons show how many commits wait on either side.
// Setting a larger count pops the badge in briefly; while busy the badge
// shows a walking ellipsis instead of the (stale) number.
class BadgeButton : public QToolButton
{
    Q_OBJECT
public:
    explicit BadgeButton(QWidget *parent = nullptr);

    void setCount(int count); // < 1 hides the badge
    int count() const { return m_count; }
    // A text badge in a colour of its own, e.g. "!" in red; empty clears it.
    void setMark(const QString &text, const QColor &color);
    void setBusy(bool busy);
    bool isBusy() const { return m_busy; }

    QSize sizeHint() const override; // leaves room for a two-digit badge

signals:
    void badgeChanged(); // count, mark or busy state

protected:
    void paintEvent(QPaintEvent *event) override;
    void timerEvent(QTimerEvent *event) override;

private:
    QString badgeText() const;
    int m_count = 0;
    QString m_mark;
    QColor m_markColor;
    bool m_busy = false;
    int m_busyTimer = 0;
    int m_busyPhase = 0;
    QVariantAnimation *m_pop;
};
