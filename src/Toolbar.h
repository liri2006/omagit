#pragma once

#include <QList>
#include <QString>
#include <QWidget>

class BadgeButton;
class QMenu;
class QToolButton;

// A row of tool buttons that fits itself into whatever width it gets: with
// room to spare every button shows its icon and label, with less room only
// the icons, and when even those do not fit the rightmost buttons move into
// a "more" menu. The leading button (the layout switcher) is always shown.
class Toolbar : public QWidget
{
    Q_OBJECT
public:
    explicit Toolbar(QWidget *parent = nullptr);

    void setLeading(QToolButton *button);
    // `fullText` is shown when there is room, `iconText` otherwise; `menuText`
    // names the button in the more menu.
    void addButton(QToolButton *button, const QString &fullText, const QString &iconText, const QString &menuText);
    void addSeparator();
    // Re-measures the buttons (after a font change) and re-colours separators.
    void applyTheme();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    struct Item {
        QToolButton *button = nullptr; // or
        QWidget *separator = nullptr;
        QString full, icon, menu;
        int fullWidth = 0, iconWidth = 0;
        int width(bool full) const { return button ? (full ? fullWidth : iconWidth) : 1; }
    };
    void measure();
    void relayout();
    void updateMoreMark();
    void fillMoreMenu();

    QList<Item> m_items;
    QToolButton *m_leading = nullptr;
    BadgeButton *m_more;
    QMenu *m_moreMenu;
    int m_height = 0;
    static constexpr int kSpacing = 8;
};
