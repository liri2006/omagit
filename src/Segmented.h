#pragma once

#include <QList>
#include <QString>
#include <QToolButton>
#include <QWidget>

// One segment of a segmented control: the top bar's page tabs and the agent
// popover's agent picker. It paints its fill, its glyph, its label and its
// count pill itself: the shared frame around the segments stays a single line
// that way, and the fonts come from the theme rather than from the
// application stylesheet, which is what decides a plain QToolButton's.
class SegmentButton : public QToolButton
{
public:
    // The fill under the pointer is painted here, so the segment has to hear
    // about the pointer arriving and leaving.
    SegmentButton();

    void setGlyph(uint code, const QString &fallback);
    void refreshGlyph();
    // Folded: the glyph and the pill, without the label.
    void setLabelled(bool on);
    bool isLabelled() const { return m_labelled; }
    void setCount(int count);
    int count() const { return m_count; }
    // Stretch mode: the content is centred in whatever width the strip
    // gives, instead of starting at the segment's left padding.
    void setCentred(bool on);
    bool isCentred() const { return m_centred; }

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString countText() const;
    int glyphBox() const;
    QFont pillFont() const;
    int pillWidth() const;
    int contentWidth() const;

    uint m_glyph = 0;
    QString m_fallback;
    QString m_glyphText;
    bool m_labelled = true;
    bool m_centred = false;
    int m_count = 0;
};

// The frame the segments share: one border around them all and one divider
// between each pair, painted here so no segment doubles a line of its own.
// The segments sit inside those lines, which is where the extra n + 1 px of
// the size hint go.
class SegmentStrip : public QWidget
{
public:
    explicit SegmentStrip(const QList<SegmentButton *> &segments, QWidget *parent = nullptr);

    // Stretch: every segment gets an equal share of the strip, floor(width / n)
    // with the last one taking the remainder (kit.js segmented({stretch: true})),
    // and centres its content. Otherwise each is as wide as its hint and the
    // last takes whatever is left.
    void setStretch(bool on);
    bool isStretch() const { return m_stretch; }
    QList<SegmentButton *> segments() const { return m_segments; }
    // The one way a segment leaves the strip or comes back (the top bar's
    // Diff tab): the hint, the placement and the dividers all follow the
    // segments taking part, and are laid out again at once, even when the
    // strip's own rectangle stays where it is.
    void setSegmentVisible(SegmentButton *segment, bool on);

    QSize sizeHint() const override;

protected:
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    // The segments taking part, in their order: those not hidden on purpose.
    // isHidden() rather than isVisible(), so the answer is the same while an
    // ancestor (the bar, the card) is hidden.
    QList<SegmentButton *> participating() const;
    void layoutSegments();

    QList<SegmentButton *> m_segments;
    bool m_stretch = false;
};
