#pragma once

#include <QColor>
#include <QFont>
#include <QHash>
#include <QObject>
#include <QString>

class QApplication;
class QFileSystemWatcher;
class QTimer;

// Reads the active Omarchy theme (~/.local/state/omarchy/current/theme/colors.toml)
// and turns it into a Qt palette + stylesheet. Re-applies itself when the
// theme is switched with `omarchy theme set`.
class OmarchyTheme : public QObject
{
    Q_OBJECT
public:
    explicit OmarchyTheme(QObject *parent = nullptr);

    static OmarchyTheme *instance();

    void apply(QApplication &app);

    bool isDark() const { return m_dark; }
    QString themeName() const { return m_name; }

    // Raw colors.toml keys (accent, background, red, ...). Falls back to Tokyo Night.
    QColor color(const QString &key) const;

    // Derived UI colors
    QColor window() const;      // main window chrome
    QColor base() const;        // lists / editors
    QColor alternateBase() const;
    QColor text() const;
    QColor mutedText() const;
    QColor border() const;
    QColor accent() const;
    QColor selection() const;

    // Diff view colors (classic diff semantics, tinted with theme colors)
    QColor diffNormalBg() const;
    QColor diffRemovedBg() const;
    QColor diffAddedBg() const;
    QColor diffInlineRemovedBg() const;
    QColor diffInlineAddedBg() const;
    QColor diffMarginBg() const;
    QColor diffHeaderBg() const;
    QColor diffEmptyBg() const;
    QColor diffAddedIcon() const;
    QColor diffRemovedIcon() const;

    QFont monoFont() const { return m_mono; }

    static QColor mix(const QColor &a, const QColor &b, qreal amount);

signals:
    void changed();

private:
    void load();
    void loadFont();
    void setupWatcher();
    void reapplyLater();
    QString buildStyleSheet() const;

    QHash<QString, QString> m_colors;
    bool m_dark = true;
    QString m_name;
    QFont m_mono;
    QApplication *m_app = nullptr;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_debounce = nullptr;
};
