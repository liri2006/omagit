#include "OmarchyTheme.h"
#include "DiffModel.h" // TokenKind, forward-declared in the header
#include "TickMenu.h"  // the tick it reserves room for is part of the menu metrics

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QIcon>
#include <QPalette>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QTextStream>
#include <QTimer>

static OmarchyTheme *s_instance = nullptr;

// How long omarchy-font-current gets to answer, blocking or not.
static constexpr int kFontQueryTimeout = 1500;

static QString omarchyStateDir()
{
    return QDir::homePath() + QStringLiteral("/.local/state/omarchy/current");
}

// The user's overrides (shell.toml [font] base-size lives here).
static QString omarchyConfigDir()
{
    return QDir::homePath() + QStringLiteral("/.config/omarchy");
}

static QString themeDir()
{
    // OMAGIT_THEME_DIR lets you preview any theme directory without switching the desktop theme.
    const QString override = qEnvironmentVariable("OMAGIT_THEME_DIR");
    if (!override.isEmpty())
        return override;
    return omarchyStateDir() + QStringLiteral("/theme");
}

// Tokyo Night — used for any key a theme does not define.
static const QHash<QString, QString> &defaultColors()
{
    static const QHash<QString, QString> d = {
        {"accent", "#7aa2f7"},          {"selection", "#292e42"},
        {"muted", "#414868"},           {"background", "#1a1b26"},
        {"dark_background", "#13141c"}, {"darker_background", "#0e0e14"},
        {"lighter_background", "#24283b"}, {"foreground", "#a9b1d6"},
        {"dark_foreground", "#565f89"}, {"light_foreground", "#b4bee6"},
        {"bright_foreground", "#c0caf5"}, {"red", "#f7768e"},
        {"yellow", "#e0af68"},          {"orange", "#eb927b"},
        {"green", "#9ece6a"},           {"cyan", "#449dab"},
        {"blue", "#7aa2f7"},            {"magenta", "#ad8ee6"},
        {"brown", "#75493d"},           {"bright_red", "#ff7a93"},
        {"bright_yellow", "#ff9e64"},   {"bright_green", "#b9f27c"},
        {"bright_cyan", "#0db9d7"},     {"bright_blue", "#7da6ff"},
        {"bright_magenta", "#bb9af7"},
    };
    return d;
}

OmarchyTheme::OmarchyTheme(QObject *parent)
    : QObject(parent)
{
    s_instance = this;
    load();
    loadShellToml();
    loadFont();
    setupWatcher();
}

OmarchyTheme::~OmarchyTheme()
{
    if (s_instance == this)
        s_instance = nullptr;
}

OmarchyTheme *OmarchyTheme::instance()
{
    Q_ASSERT(s_instance);
    return s_instance;
}

QColor OmarchyTheme::mix(const QColor &a, const QColor &b, qreal t)
{
    t = qBound(0.0, t, 1.0);
    return QColor::fromRgbF(a.redF() * (1 - t) + b.redF() * t,
                            a.greenF() * (1 - t) + b.greenF() * t,
                            a.blueF() * (1 - t) + b.blueF() * t);
}

void OmarchyTheme::load()
{
    m_colors.clear();
    m_dark = true;
    m_name.clear();

    QFile nameFile(omarchyStateDir() + QStringLiteral("/theme.name"));
    if (nameFile.open(QIODevice::ReadOnly | QIODevice::Text))
        m_name = QString::fromUtf8(nameFile.readAll()).trimmed();

    QFile f(themeDir() + QStringLiteral("/colors.toml"));
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        static const QRegularExpression re(
            QStringLiteral("^\\s*([A-Za-z0-9_]+)\\s*=\\s*\"([^\"]*)\""));
        QTextStream in(&f);
        while (!in.atEnd()) {
            const QString line = in.readLine();
            const auto m = re.match(line);
            if (!m.hasMatch())
                continue;
            const QString key = m.captured(1);
            const QString value = m.captured(2).trimmed();
            if (key == QLatin1String("mode"))
                m_dark = value.compare(QLatin1String("light"), Qt::CaseInsensitive) != 0;
            else
                m_colors.insert(key, value);
        }
    }

    // Whether the theme really names a warm hue: with only color0..color15 to
    // go on, the "yellow" slot may be anything, so a derived orange can come
    // out washed and syntax numbers fall back to red instead.
    m_hasOrange = m_colors.contains(QStringLiteral("orange"))
        || (m_colors.contains(QStringLiteral("red")) && m_colors.contains(QStringLiteral("yellow")));

    // Some themes only ship the base palette; derive the shades we rely on.
    auto ensure = [this](const QString &key, const QColor &value) {
        if (!m_colors.contains(key))
            m_colors.insert(key, value.name());
    };
    const QColor bg = color("background");
    const QColor fg = color("foreground");
    ensure("dark_background", mix(bg, Qt::black, 0.25));
    ensure("darker_background", mix(bg, Qt::black, 0.45));
    ensure("lighter_background", mix(bg, fg, 0.10));
    ensure("dark_foreground", mix(fg, bg, 0.45));
    ensure("light_foreground", mix(fg, m_dark ? Qt::white : Qt::black, 0.15));
    ensure("bright_foreground", mix(fg, m_dark ? Qt::white : Qt::black, 0.3));
    ensure("selection", mix(bg, fg, 0.2));
    ensure("muted", mix(bg, fg, 0.35));
    // Many themes only ship the ANSI palette (color0..color15) and no named
    // hues; map the standard slots so syntax colouring follows the theme
    // instead of falling back to Tokyo Night.
    auto ensureFromAnsi = [this](const QString &key, const QString &ansi) {
        if (!m_colors.contains(key) && m_colors.contains(ansi))
            m_colors.insert(key, m_colors.value(ansi));
    };
    ensureFromAnsi("red", "color1");
    ensureFromAnsi("green", "color2");
    ensureFromAnsi("yellow", "color3");
    ensureFromAnsi("blue", "color4");
    ensureFromAnsi("magenta", "color5");
    ensureFromAnsi("cyan", "color6");
    ensure("orange", mix(color("red"), color("yellow"), 0.5));

    // Icon theme chosen by the Omarchy theme (icons.theme holds e.g. "Yaru-blue").
    QFile icons(themeDir() + QStringLiteral("/icons.theme"));
    if (icons.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString name = QString::fromUtf8(icons.readAll()).trimmed();
        if (!name.isEmpty())
            QIcon::setThemeName(name);
    }
}

// The desktop's terminal font, which the first paint already needs: worth the
// blocking wait once at startup, but not on every theme change (see reload()).
void OmarchyTheme::loadFont()
{
    QString family;
    const QString bin = fontBinary();
    if (!bin.isEmpty()) {
        QProcess p;
        p.start(bin, QStringList());
        if (p.waitForFinished(kFontQueryTimeout) && p.exitCode() == 0)
            family = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
    }
    setFontFamily(family);
}

QString OmarchyTheme::fontBinary()
{
    return QStandardPaths::findExecutable(QStringLiteral("omarchy-font-current"));
}

// An empty family (no helper, a failed or slow run) falls back the way the
// blocking query always has.
void OmarchyTheme::setFontFamily(const QString &reported)
{
    const QString family = reported.isEmpty() ? QStringLiteral("JetBrainsMono Nerd Font") : reported;
    m_mono = QFont(family);
    if (!QFontDatabase::hasFamily(family))
        m_mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_mono.setStyleHint(QFont::Monospace);
    m_mono.setFixedPitch(true);
    m_mono.setPixelSize(m_fontBase);
}

// Same answer as loadFont(), same timeout, but off the event loop: a theme
// directory that changes must never freeze the window for it. Only one query
// is ever in flight; a newer reload replaces the pending one.
void OmarchyTheme::loadFontLater()
{
    if (QProcess *const stale = m_fontQuery) {
        m_fontQuery = nullptr; // its callbacks see the change and do nothing
        stale->kill();
        stale->deleteLater();
    }
    const QString bin = fontBinary();
    if (bin.isEmpty()) {
        fontQueryFinished(QString());
        return;
    }
    QProcess *const p = new QProcess(this);
    m_fontQuery = p;
    auto answer = [this, p](const QString &family) {
        if (m_fontQuery != p)
            return; // already superseded
        m_fontQuery = nullptr;
        if (p->state() != QProcess::NotRunning)
            p->kill(); // the timeout path: deleting a running process would wait for it
        p->deleteLater();
        fontQueryFinished(family);
    };
    connect(p, &QProcess::finished, this, [p, answer](int code, QProcess::ExitStatus status) {
        answer(code == 0 && status == QProcess::NormalExit
                   ? QString::fromUtf8(p->readAllStandardOutput()).trimmed()
                   : QString());
    });
    connect(p, &QProcess::errorOccurred, this, [answer] { answer(QString()); });
    QTimer::singleShot(kFontQueryTimeout, p, [answer] { answer(QString()); });
    p->start(bin, QStringList());
}

// The rest of reload(), once the font is known: colours, shell.toml and the
// font land together, so no paint in between sees half a theme.
void OmarchyTheme::fontQueryFinished(const QString &family)
{
    load();
    loadShellToml();
    setFontFamily(family);
    rearmWatcher();
    const QString before = m_reloadSignature;
    m_reloadSignature.clear();
    if (signature() == before)
        return;
    if (m_app)
        apply(*m_app);
    emit changed();
}

// [font] base-size from the theme's shell.toml, overridden by the user's
// ~/.config/omarchy/shell.toml — the same rem root the shell uses.
void OmarchyTheme::loadShellToml()
{
    m_fontBase = 12;
    const QStringList files{themeDir() + QStringLiteral("/shell.toml"),
                            omarchyConfigDir() + QStringLiteral("/shell.toml")};
    static const QRegularExpression sectionRe(QStringLiteral("^\\s*\\[([^\\]]+)\\]"));
    static const QRegularExpression kvRe(QStringLiteral("^\\s*([A-Za-z0-9_-]+)\\s*=\\s*([^#]+)"));
    for (const QString &path : files) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        QString section;
        QTextStream in(&f);
        while (!in.atEnd()) {
            const QString line = in.readLine();
            const auto sm = sectionRe.match(line);
            if (sm.hasMatch()) {
                section = sm.captured(1).trimmed();
                continue;
            }
            const auto km = kvRe.match(line);
            if (!km.hasMatch())
                continue;
            if (section == QLatin1String("font") && km.captured(1) == QLatin1String("base-size")) {
                bool ok = false;
                const int v = km.captured(2).trimmed().toInt(&ok);
                if (ok && v > 0)
                    m_fontBase = v;
            }
        }
    }
}

QFont OmarchyTheme::uiFont() const
{
    return m_mono;
}

QFont OmarchyTheme::captionFont() const
{
    QFont f = m_mono;
    f.setPixelSize(qMax(8, qRound(m_fontBase * 0.833)));
    f.setBold(true);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
    return f;
}

QFont OmarchyTheme::titleFont() const
{
    QFont f = m_mono;
    f.setPixelSize(qRound(m_fontBase * 1.167));
    f.setBold(true);
    return f;
}

QFont OmarchyTheme::headingFont() const
{
    QFont f = m_mono;
    f.setPixelSize(qRound(m_fontBase * 1.333));
    f.setWeight(QFont::Medium);
    return f;
}

QString OmarchyTheme::glyph(uint codepoint) const
{
    const QFontMetrics fm(m_mono);
    if (!fm.inFontUcs4(codepoint))
        return QString();
    return QString::fromUcs4(reinterpret_cast<const char32_t *>(&codepoint), 1);
}

void OmarchyTheme::setupWatcher()
{
    m_watcher = new QFileSystemWatcher(this);
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(300);
    connect(m_debounce, &QTimer::timeout, this, &OmarchyTheme::reload);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &OmarchyTheme::reapplyLater);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &OmarchyTheme::reapplyLater);
    rearmWatcher();
}

// theme-set replaces the theme directory and `omarchy display text size`
// swaps shell.toml for a fresh file, so the files we watch keep changing
// inode: watch the directories too and re-arm on every reload.
void OmarchyTheme::rearmWatcher()
{
    if (!m_watcher->files().isEmpty())
        m_watcher->removePaths(m_watcher->files());
    if (!m_watcher->directories().isEmpty())
        m_watcher->removePaths(m_watcher->directories());
    const QStringList paths{omarchyStateDir(),
                            themeDir() + QStringLiteral("/colors.toml"),
                            themeDir() + QStringLiteral("/shell.toml"),
                            omarchyConfigDir(),
                            omarchyConfigDir() + QStringLiteral("/shell.toml")};
    for (const QString &path : paths) {
        if (QFileInfo::exists(path))
            m_watcher->addPath(path);
    }
}

// Everything the palette, stylesheet and fonts are derived from; a reload
// that leaves it unchanged (shell.json edits, lock files) is not re-applied.
QString OmarchyTheme::signature() const
{
    QStringList keys = m_colors.keys();
    keys.sort();
    QString sig = m_name + QLatin1Char('|') + (m_dark ? QLatin1Char('d') : QLatin1Char('l'))
        + QLatin1Char('|') + QString::number(m_fontBase) + QLatin1Char('|') + m_mono.family()
        + QLatin1Char('|') + QIcon::themeName();
    for (const QString &k : std::as_const(keys))
        sig += QLatin1Char('|') + k + QLatin1Char('=') + m_colors.value(k);
    return sig;
}

void OmarchyTheme::reload()
{
    if (m_reloadSignature.isEmpty())
        m_reloadSignature = signature(); // what the pending reload compares against
    loadFontLater(); // reads everything and finishes in fontQueryFinished()
}

void OmarchyTheme::reapplyLater()
{
    m_debounce->start();
}

QColor OmarchyTheme::color(const QString &key) const
{
    const auto own = m_colors.constFind(key);
    if (own != m_colors.cend())
        return QColor(*own);
    const auto fallback = defaultColors().constFind(key);
    if (fallback != defaultColors().cend())
        return QColor(*fallback);
    // Neither the theme nor Tokyo Night names it: magenta is loud enough to
    // spot, but only the first sighting is worth a line in the log.
    static QSet<QString> warned;
    if (!warned.contains(key)) {
        warned.insert(key);
        qWarning("OmarchyTheme: no colour named \"%s\"", qPrintable(key));
    }
    return QColor(QStringLiteral("#ff00ff"));
}

// Keeps a colour inside a lightness band so it stays readable on the diff tints.
static QColor clampLightness(const QColor &c, qreal minL, qreal maxL)
{
    const QColor hsl = c.toHsl();
    const qreal l = hsl.lightnessF();
    const qreal want = qBound(minL, l, maxL);
    if (qFuzzyCompare(want + 1.0, l + 1.0))
        return c;
    return QColor::fromHslF(qMax(0.0f, float(hsl.hueF())), float(hsl.saturationF()), float(want)).toRgb();
}

QColor OmarchyTheme::fill(qreal alpha) const
{
    return mix(color("background"), color("foreground"), alpha);
}

QColor OmarchyTheme::window() const { return color("background"); }
QColor OmarchyTheme::base() const { return color("background"); }
QColor OmarchyTheme::alternateBase() const { return color("background"); }
QColor OmarchyTheme::text() const { return color("foreground"); }
QColor OmarchyTheme::mutedText() const { return color("foreground").darker(140); }
QColor OmarchyTheme::border() const { return fill(0.20); }
QColor OmarchyTheme::accent() const { return color("accent"); }
QColor OmarchyTheme::selection() const { return hoverFill(); }

// Classic diff colours: removed = RGB(255,200,100), added = RGB(255,255,0) on light;
// RGB(83,66,33) / RGB(83,83,0) on dark. We keep the orange/yellow semantics but
// tint the theme's own palette so it blends with the rest of the desktop.
// Added lines use a green of the same weight instead of the classic yellow.
QColor OmarchyTheme::diffNormalBg() const { return base(); }
QColor OmarchyTheme::diffRemovedBg() const { return m_dark ? QColor(83, 66, 33) : QColor(255, 200, 100); }
QColor OmarchyTheme::diffAddedBg() const { return m_dark ? QColor(33, 83, 33) : QColor(200, 255, 200); }
QColor OmarchyTheme::diffInlineRemovedBg() const { return m_dark ? QColor(100, 40, 40) : QColor(200, 100, 100); }
QColor OmarchyTheme::diffInlineAddedBg() const { return m_dark ? QColor(60, 120, 60) : QColor(150, 230, 150); }
QColor OmarchyTheme::diffMarginBg() const { return fill(0.06); }
QColor OmarchyTheme::diffHeaderBg() const { return fill(0.06); }
QColor OmarchyTheme::diffEmptyBg() const { return m_dark ? QColor(66, 66, 66) : QColor(200, 200, 200); }
QColor OmarchyTheme::diffAddedIcon() const { return color("green"); }
QColor OmarchyTheme::diffRemovedIcon() const { return color("red"); }

// Syntax colours come from the theme's own hues. The diff paints them over
// the added (green) and removed (orange) tints, which sit in the middle of
// the lightness range, so every colour is pushed away from that middle:
// bright on a dark theme, dark on a light one. Comments stay dimmer than
// plain text, just far enough from the tints to stay readable.
QColor OmarchyTheme::syntaxColor(TokenKind kind) const
{
    QColor c;
    qreal minL = 0.0, maxL = 1.0;
    switch (kind) {
    case TokenKind::Keyword: c = color("magenta"); break;
    case TokenKind::Type: c = color("blue"); break;
    case TokenKind::String: c = color("green"); break;
    case TokenKind::Number: c = m_hasOrange ? color("orange") : color("red"); break;
    case TokenKind::Preprocessor: c = color("cyan"); break;
    case TokenKind::Function: c = color("yellow"); break;
    case TokenKind::Comment:
        c = mutedText();
        // Dimmer than the text pen, but not swallowed by the line tints.
        if (m_dark)
            minL = 0.46;
        else
            maxL = 0.46;
        return clampLightness(c, minL, maxL);
    }
    if (m_dark)
        minL = 0.62;
    else
        maxL = 0.38;
    return clampLightness(c, minL, maxL);
}

void OmarchyTheme::apply(QApplication &app)
{
    m_app = &app;
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    // The application font is only the fallback for widgets a stylesheet
    // rule never reaches (dialogs before their polish). Once an application
    // stylesheet is set, Qt propagates fonts through the stylesheet and
    // ignores later setFont() calls on the application, and setStyleSheet
    // itself re-fills Qt's per-class font table from the platform theme;
    // so the QWidget rule below carries the base size and family, and the
    // roles with their own size (captions, the branch button) get a rule
    // too, or a live change would size them differently from a fresh start.
    app.setFont(uiFont());

    QPalette pal;
    const QColor bg = window(), fg = text(), acc = accent();
    pal.setColor(QPalette::Window, bg);
    pal.setColor(QPalette::WindowText, fg);
    pal.setColor(QPalette::Base, bg);
    pal.setColor(QPalette::AlternateBase, bg);
    pal.setColor(QPalette::Text, fg);
    pal.setColor(QPalette::PlaceholderText, fg.darker(160));
    pal.setColor(QPalette::Button, normalFill());
    pal.setColor(QPalette::ButtonText, fg);
    pal.setColor(QPalette::BrightText, color("bright_foreground"));
    pal.setColor(QPalette::ToolTipBase, bg);
    pal.setColor(QPalette::ToolTipText, fg);
    pal.setColor(QPalette::Highlight, hoverFill());
    pal.setColor(QPalette::HighlightedText, acc);
    pal.setColor(QPalette::Link, color("blue"));
    pal.setColor(QPalette::LinkVisited, color("magenta"));
    pal.setColor(QPalette::Light, fill(0.08));
    pal.setColor(QPalette::Midlight, fill(0.12));
    pal.setColor(QPalette::Mid, fill(0.25));
    pal.setColor(QPalette::Dark, fill(0.40));
    pal.setColor(QPalette::Shadow, fill(0.60));
    pal.setColor(QPalette::Disabled, QPalette::Text, fill(0.45));
    pal.setColor(QPalette::Disabled, QPalette::WindowText, fill(0.45));
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, fill(0.45));
    app.setPalette(pal);
    app.setStyleSheet(buildStyleSheet());
}

// Fills every %name% of the sheet from `tokens` in one pass, so no token can
// swallow the start of a longer one (%hair% inside %hair20%) and a name with
// no value is reported instead of leaking into the sheet.
static QString substitute(const QString &sheet, const QHash<QString, QString> &tokens)
{
    static const QRegularExpression tokenRe(QStringLiteral("%([A-Za-z0-9]+)%"));
    QString out;
    out.reserve(sheet.size());
    qsizetype at = 0;
    QRegularExpressionMatchIterator it = tokenRe.globalMatch(sheet);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const auto value = tokens.constFind(m.captured(1));
        if (value == tokens.cend()) {
            qWarning("OmarchyTheme: stylesheet token %%%s%% has no value", qPrintable(m.captured(1)));
            Q_ASSERT_X(false, "OmarchyTheme::buildStyleSheet", "unknown stylesheet token");
            continue; // leave it in place; the sheet is broken either way
        }
        out += QStringView(sheet).sliced(at, m.capturedStart() - at);
        out += *value;
        at = m.capturedEnd();
    }
    out += QStringView(sheet).sliced(at);
    return out;
}

// Mirrors the shell's control kit (Ui/Button.qml, TextField.qml, Menu.qml):
// square corners, transparent-ish fills from foreground alpha, 1px borders,
// accent text for the selected/current item, popups framed by the accent.
QString OmarchyTheme::buildStyleSheet() const
{
    const QHash<QString, QString> tokens{
        {QStringLiteral("bg"), window().name()},
        {QStringLiteral("fg"), text().name()},
        {QStringLiteral("acc"), accent().name()},
        {QStringLiteral("dim"), mutedText().name()},
        {QStringLiteral("fill4"), normalFill().name()},
        {QStringLiteral("fill8"), hoverFill().name()},
        {QStringLiteral("fill18"), selectedFill().name()},
        {QStringLiteral("fill22"), pressedFill().name()},
        {QStringLiteral("sel35"), selectionFill().name()},
        {QStringLiteral("bd40"), normalBorder().name()},
        {QStringLiteral("bd25"), hoverBorder().name()},
        {QStringLiteral("hair"), hairline().name()},
        {QStringLiteral("hair20"), border().name()},
        {QStringLiteral("disabled"), fill(0.45).name()},
        {QStringLiteral("caption"), QString::number(captionFont().pixelSize())},
        // TickMenu paints its check mark at the right edge of the item, inside this padding.
        {QStringLiteral("tickpad"), QString::number(14 + TickMenu::tickReserve())},
        {QStringLiteral("family"), m_mono.family()},
        {QStringLiteral("base"), QString::number(m_fontBase)},
        {QStringLiteral("heading"), QString::number(headingFont().pixelSize())},
        {QStringLiteral("big"), QString::number(qRound(m_fontBase * 1.5))},
    };

    return substitute(QStringLiteral(R"(
QMainWindow, QDialog, QMessageBox { background: %bg%; }
QWidget { color: %fg%; font-family: "%family%"; font-size: %base%px; }
/* railTip is the Mini rail's own tooltip label, framed like a real one. */
QToolTip, QLabel#railTip { background: %bg%; color: %fg%; border: 1px solid %fg%; padding: 4px 8px; }

QPlainTextEdit, QTextEdit, QLineEdit {
    background: %fill4%; color: %fg%; border: 1px solid %bd40%; border-radius: 0;
    selection-background-color: %sel35%; selection-color: %fg%; padding: 4px 6px;
}
QPlainTextEdit:hover, QTextEdit:hover, QLineEdit:hover { background: %fill8%; border-color: %bd25%; }
QPlainTextEdit:focus, QTextEdit:focus, QLineEdit:focus { background: %fill8%; border-color: %bd25%; }

QTableView, QTreeView {
    background: %bg%; border: 1px solid %bd40%; border-radius: 0; gridline-color: %hair%;
    selection-background-color: %fill8%; selection-color: %acc%; outline: 0;
}
QTableView::item, QTreeView::item { padding: 0 10px; border: none; }
QTableView::item:hover, QTreeView::item:hover { background: %fill4%; }
QTableView::item:selected, QTreeView::item:selected { background: %fill8%; color: %acc%; }
QLineEdit#cloneName {
    background: transparent; color: %dim%; padding: 2px 3px;
    border: none; border-bottom: 1px solid %bd40%;
}
QLineEdit#cloneName:hover { background: %fill4%; border-bottom-color: %bd25%; }
QLineEdit#cloneName:focus { background: %fill8%; color: %fg%; border-bottom-color: %acc%; }
QListWidget#cloneRepositories {
    background: %bg%; border: 1px solid %bd40%; border-radius: 0; outline: 0;
}
QListWidget#cloneRepositories::item { padding: 6px 10px; border: none; }
QListWidget#cloneRepositories::item:hover { background: %fill4%; }
QListWidget#cloneRepositories::item:selected { background: %fill8%; color: %acc%; }
QDialog#cloneDialog QProgressBar { background: %fill8%; border: none; }
QDialog#cloneDialog QProgressBar::chunk { background: %acc%; }
QHeaderView { background: transparent; }
QHeaderView::section {
    background: transparent; color: %dim%; font-weight: bold; font-size: %caption%px;
    padding: 6px 10px; border: none; border-bottom: 1px solid %hair20%; border-right: 1px solid %hair%;
}
QHeaderView::section:last, QHeaderView::section:only-one { border-right: none; }
QHeaderView::down-arrow, QHeaderView::up-arrow { width: 0; height: 0; }
QTableCornerButton::section { background: transparent; border: none; }

QPushButton, QToolButton {
    background: %fill4%; color: %fg%; border: 1px solid %bd40%; border-radius: 0; padding: 5px 10px;
}
QPushButton:hover, QToolButton:hover { background: %fill8%; border-color: %bd25%; }
QPushButton:pressed, QToolButton:pressed { background: %fill22%; border-color: %bd25%; }
QPushButton:checked, QToolButton:checked { background: %fill18%; color: %acc%; border-color: %fill18%; }
QPushButton:checked:hover, QToolButton:checked:hover { background: %fill18%; color: %acc%; border-color: %bd25%; }
QPushButton:default { background: %fill8%; color: %acc%; border: 1px solid %acc%; }
QPushButton:default:hover { background: %fill18%; }
QPushButton:disabled, QToolButton:disabled { color: %disabled%; background: transparent; border-color: %hair20%; }
QPushButton:focus, QToolButton:focus { border-color: %bd25%; background: %fill8%; }
QToolButton::menu-indicator { image: none; width: 0; height: 0; }
QToolButton#smallButton { padding: 1px 5px; }
QToolButton#cornerButton { background: transparent; border: 1px solid transparent; padding: 1px 3px; color: %dim%; }
QToolButton#cornerButton:hover { background: %fill8%; border-color: %bd25%; color: %fg%; }
QToolButton#cornerButton:pressed { background: %fill22%; }

QCheckBox { spacing: 8px; }
QCheckBox::indicator, QTableView::indicator, QTreeView::indicator {
    width: 14px; height: 14px; border: 1px solid %bd40%; border-radius: 0; background: %fill4%;
}
QCheckBox::indicator:hover, QTableView::indicator:hover, QTreeView::indicator:hover { border-color: %bd25%; background: %fill8%; }
QCheckBox::indicator:checked, QTableView::indicator:checked, QTreeView::indicator:checked {
    background: %acc%; border-color: %acc%; image: url(:/check.svg);
}
QCheckBox::indicator:indeterminate { background: %fill18%; border-color: %acc%; image: url(:/partial.svg); }

QSplitter::handle { background: transparent; }
QSplitter::handle:horizontal { width: 8px; }
QSplitter::handle:vertical { height: 8px; }
QSplitter::handle:hover { background: %fill8%; }

QScrollBar:vertical { background: transparent; width: 8px; margin: 0; border: none; }
QScrollBar::handle:vertical { background: %bd25%; min-height: 24px; border-radius: 0; margin: 0 2px; }
QScrollBar::handle:vertical:hover { background: %acc%; }
QScrollBar:horizontal { background: transparent; height: 8px; margin: 0; border: none; }
QScrollBar::handle:horizontal { background: %bd25%; min-width: 24px; border-radius: 0; margin: 2px 0; }
QScrollBar::handle:horizontal:hover { background: %acc%; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }

QStatusBar { background: %bg%; color: %dim%; border-top: 1px solid %hair%; }
QStatusBar::item { border: none; }
QLabel#sectionLabel, QLabel#dimLabel { color: %dim%; font-size: %caption%px; font-weight: bold; }
QLabel#captionLabel { font-size: %caption%px; font-weight: bold; }
QLabel#bigLabel { font-size: %big%px; }
QToolButton#keybindingsButton, QToolButton#layoutButton, QToolButton#branchButton, QToolButton#repoButton { background: transparent; border: 1px solid transparent; padding: 2px 6px; }
QToolButton#layoutButton { color: %fg%; }
QToolButton#branchButton { color: %acc%; font-weight: bold; }
QToolButton#keybindingsButton:hover, QToolButton#layoutButton:hover, QToolButton#branchButton:hover, QToolButton#repoButton:hover { background: %fill8%; border-color: %bd25%; }
QToolButton#keybindingsButton:pressed, QToolButton#layoutButton:pressed, QToolButton#branchButton:pressed, QToolButton#repoButton:pressed { background: %fill22%; border-color: %bd25%; }
QMenu { background: %bg%; border: 2px solid %acc%; border-radius: 0; padding: 6px; }
QMenu::item { padding: 6px 14px; border-radius: 0; }
QMenu::item:selected { background: %fill8%; color: %acc%; }
QMenu::item:disabled { color: %disabled%; }
QMenu::separator { height: 1px; background: %hair%; margin: 4px 2px; }
QMenu::indicator { width: 12px; height: 12px; border: 1px solid %bd40%; background: %fill4%; margin-left: 4px; }
QMenu::indicator:checked { background: %acc%; border-color: %acc%; image: url(:/check.svg); }
TickMenu::item { padding-right: %tickpad%px; }
TickMenu::item:checked { color: %acc%; }
TickMenu::indicator { width: 0; height: 0; margin: 0; border: none; background: none; image: none; }
TickMenu QLineEdit { margin: 0; }
QDialog#keybindingsPanel { background: %bg%; border: 2px solid %acc%; }
QLineEdit#keybindingsSearch, QLineEdit#keybindingsSearch:hover, QLineEdit#keybindingsSearch:focus {
    background: transparent; border: none; padding: 0; font-size: %heading%px; font-weight: 500;
}
QListView#keybindingsList { background: transparent; border: none; }
QToolButton#branchPicker { padding: 8px 12px; }
QToolButton#branchPicker:disabled { background: %fill4%; border-color: %hair20%; }
QToolButton#swapButton { padding: 0; }
/* The show/hide eye inside the sign-in dialog's password field: part of the
   field, so it carries no chrome of its own — only its glyph lights up. */
QToolButton#revealButton, QToolButton#revealButton:checked, QToolButton#revealButton:hover {
    background: transparent; border: 1px solid transparent; padding: 1px 3px; color: %dim%;
}
QToolButton#revealButton:hover { color: %fg%; }
QToolButton#revealButton:checked { color: %acc%; }
QFrame#mergeVerdict { background: %fill4%; border: 1px solid %bd40%; }
QListWidget#mergeFiles { background: transparent; border: none; outline: 0; }
QListWidget#mergeFiles::item { padding: 2px 4px; border: none; }
QListWidget#mergeFiles::item:hover, QListWidget#mergeFiles::item:selected { background: %fill8%; color: %fg%; }
QToolBar { background: %bg%; border: none; spacing: 8px; }
DiffView { border: 1px solid %bd40%; background: %bg%; }
QMessageBox QLabel { color: %fg%; }
QAbstractScrollArea { background: %bg%; }
)"),
                      tokens);
}
