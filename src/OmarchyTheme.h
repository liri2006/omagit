#pragma once

#include <QColor>
#include <QFont>
#include <QHash>
#include <QObject>
#include <QString>

class QApplication;
class QFileSystemWatcher;
class QProcess;
class QTimer;

// What a syntax span means, as DiffModel.h defines it; only the name is
// needed here, so the diff parser stays out of every theme user's includes.
enum class TokenKind;

// Reads the active Omarchy theme (~/.local/state/omarchy/current/theme/colors.toml)
// and turns it into a Qt palette + stylesheet. Re-applies itself when the
// theme is switched with `omarchy theme set` or the text size is changed with
// `omarchy display text size` (which rewrites ~/.config/omarchy/shell.toml).
class OmarchyTheme : public QObject
{
    Q_OBJECT
public:
    explicit OmarchyTheme(QObject *parent = nullptr);
    ~OmarchyTheme() override;

    // Never null while a theme exists; every widget in the app dereferences it.
    static OmarchyTheme *instance();

    void apply(QApplication &app);

    bool isDark() const { return m_dark; }
    QString themeName() const { return m_name; }

    // Raw colors.toml keys (accent, background, red, ...). Falls back to Tokyo Night.
    QColor color(const QString &key) const;

    // Derived UI colors. Omarchy's shell paints every surface on the one
    // theme background and builds control chrome from foreground alpha:
    // fills at 4/8/18/22 %, borders at 40/25 %, separators at 12–20 %.
    QColor window() const;      // main window chrome
    QColor base() const;        // lists / editors (same as window)
    QColor alternateBase() const;
    QColor text() const;
    QColor mutedText() const;   // Qt.darker(foreground, 1.4) like the shell's dim labels
    QColor border() const;      // hairline separator (foreground @ 20 %)
    QColor accent() const;
    QColor selection() const;
    QColor fill(qreal alpha) const;   // foreground blended over background
    QColor normalFill() const { return fill(0.04); }
    QColor hoverFill() const { return fill(0.08); }
    QColor selectedFill() const { return fill(0.18); }
    QColor pressedFill() const { return fill(0.22); }
    QColor selectionFill() const { return fill(0.35); }
    QColor normalBorder() const { return fill(0.40); }
    QColor hoverBorder() const { return fill(0.25); }
    QColor hairline() const { return fill(0.12); }

    // Typography: the shell's type scale, rooted at [font] base-size of shell.toml.
    int fontBase() const { return m_fontBase; }
    QFont uiFont() const;
    QFont captionFont() const;  // bold, ~0.833 × base, used for section labels
    QFont titleFont() const;    // bold, ~1.167 × base
    QFont headingFont() const;  // medium weight, ~1.333 × base: the shell's menu rows
    // Nerd Font glyph if the UI font has it, otherwise an empty string.
    QString glyph(uint codepoint) const;

    // Diff view colors: classic light/dark diff line colours,
    // picked by whether the Omarchy theme is dark. Margin/header follow the theme.
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

    // Syntax colouring of the diff text, kept legible over the added/removed tints.
    QColor syntaxColor(TokenKind kind) const;

    QFont monoFont() const { return m_mono; }

    static QColor mix(const QColor &a, const QColor &b, qreal amount);

signals:
    void changed();

private:
    void load();
    void loadFont();      // blocking; only the constructor can afford it
    void loadFontLater(); // and the reload path, which cannot
    void fontQueryFinished(const QString &family);
    static QString fontBinary();
    void setFontFamily(const QString &reported);
    void loadShellToml();
    void setupWatcher();
    void rearmWatcher();
    void reapplyLater();
    void reload();
    QString signature() const;
    QString buildStyleSheet() const;

    QHash<QString, QString> m_colors;
    bool m_dark = true;
    bool m_hasOrange = true;   // the theme names a warm hue of its own
    QString m_name;
    QFont m_mono;
    int m_fontBase = 12;
    QApplication *m_app = nullptr;
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_debounce = nullptr;
    QProcess *m_fontQuery = nullptr; // the font query of a reload still running
    QString m_reloadSignature;       // non-empty while that reload is pending
};
