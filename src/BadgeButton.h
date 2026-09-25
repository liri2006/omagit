#pragma once

#include "UiHelpers.h"

#include <QList>
#include <QPointer>
#include <QToolButton>

class QPainter;
class QVariantAnimation;

// A tool button with a small count badge over its top-right corner, the way
// the Pull and Push buttons show how many commits wait on either side.
// Setting a larger count pops the badge in briefly; while busy the badge
// shows a walking ellipsis instead of the (stale) number.
//
// The badge hangs over the button's top and right edges (design/figma-gen/
// kit.js badge()), where no widget can paint in its own rect, so the button
// only keeps the badge's state and says where and how it is drawn: a
// BadgeLayer over the row paints it. The button itself is a ui::KitButton:
// it measures and lays out its glyph and label like the design's buttons.
class BadgeButton : public ui::KitButton
{
    Q_OBJECT
public:
    explicit BadgeButton(QWidget *parent = nullptr);

    void setCount(int count); // < 1 hides the badge
    int count() const { return m_count; }
    // A text badge in a colour of its own, e.g. "!" in red; empty clears it.
    void setMark(const QString &text, const QColor &color);
    // The mark as it was set, so another control can wear the same one.
    QString markText() const;
    QColor markColor() const;
    void setBusy(bool busy);
    bool isBusy() const { return m_busy; }

    // Whether there is a badge to paint: a count, a mark or the busy dots.
    bool hasBadge() const;
    // The badge's box for this button drawn at `button`: its right edge
    // space(4) past the button's, its top space(4) above the button's, a
    // square of space(12) at the least and wider for a longer text.
    QRect badgeRect(const QRect &button) const;
    // Paints the badge for this button drawn at `button`, pop and all.
    void paintBadge(QPainter *p, const QRect &button) const;

signals:
    void badgeChanged(); // count, mark or busy state
    // The painted badge moved on while its content stayed: a tick of the pop
    // animation or of the walking dots.
    void badgeRepaint();

protected:
    void timerEvent(QTimerEvent *event) override;

private:
    QString badgeText() const;
    QFont badgeFont() const;
    int m_count = 0;
    QString m_mark;
    QColor m_markColor;
    bool m_busy = false;
    int m_busyTimer = 0;
    int m_busyPhase = 0;
    QVariantAnimation *m_pop;
};

// A transparent overlay that paints the badges of the buttons it watches,
// each over its button's corner: the badges overhang their buttons, so they
// are painted on a widget that covers the room around them. It takes no
// input; the buttons under it get every click.
class BadgeLayer : public QWidget
{
    Q_OBJECT
public:
    explicit BadgeLayer(QWidget *parent = nullptr);

    // Paints `button`'s badge from now on, and repaints whenever the badge or
    // the button's place changes.
    void watch(BadgeButton *button);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    QList<QPointer<BadgeButton>> m_buttons;
};
