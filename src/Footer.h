#pragma once

#include <QWidget>

class QLabel;
class QTimer;
class QToolButton;

// The bar under the body: a hairline, then the layout toggle, the repository
// and branch selectors, the current path (or the latest message) and the
// keybindings button.
class Footer : public QWidget
{
    Q_OBJECT
public:
    explicit Footer(QWidget *parent = nullptr);

    QToolButton *layoutButton() const { return m_layoutButton; }
    QToolButton *repoButton() const { return m_repoButton; }
    QToolButton *branchButton() const { return m_branchButton; }
    QToolButton *keybindingsButton() const { return m_keybindingsButton; }

    // The footer's message; `ms` > 0 brings the idle text back after that long.
    void showStatus(const QString &text, int ms = 0);
    // What the label falls back to: the repository path.
    void setIdleText(const QString &text);

    void applyTheme();

private:
    QToolButton *m_layoutButton; // Docked/Mini toggle, checked in Mini
    QToolButton *m_repoButton;   // the repository name; clicking it lists recent ones
    QToolButton *m_branchButton; // the branch name; clicking it lists the branches
    QToolButton *m_keybindingsButton;
    QLabel *m_statusLabel;       // the footer's message, the repository path when there is none
    QTimer *m_statusTimer;
    QString m_idleText;
};
