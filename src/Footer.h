#pragma once

#include "Grid.h"

#include <QWidget>

class QHBoxLayout;
class QTimer;
class QToolButton;
namespace ui {
class ElidedLabel;
}

// The bar under the body: a hairline, then the current path (or the latest
// message) and the keybindings button. The repository and branch selectors
// moved to the top bar with the layout toggles.
class Footer : public QWidget
{
    Q_OBJECT
public:
    explicit Footer(QWidget *parent = nullptr);

    QToolButton *keybindingsButton() const { return m_keybindingsButton; }

    // The footer's message; `ms` > 0 brings the idle text back after that long.
    void showStatus(const QString &text, int ms = 0);
    // What the label falls back to: the repository path.
    void setIdleText(const QString &text);
    // The window's density: the row keeps its side margin.
    void setDensity(const ui::Density &density);

private:
    void applyTheme(); // the design's distances, on the text size of the moment

    QHBoxLayout *m_row; // the status and the button, under the hairline
    QToolButton *m_keybindingsButton;
    ui::ElidedLabel *m_statusLabel; // the footer's message, the repository path when there is none
    QTimer *m_statusTimer;
    QString m_idleText;
    ui::Density m_density = ui::kRegularDensity;
};
