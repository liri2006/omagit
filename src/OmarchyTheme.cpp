#include "OmarchyTheme.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileSystemWatcher>
#include <QFontDatabase>
#include <QIcon>
#include <QPalette>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QTextStream>
#include <QTimer>

static OmarchyTheme *s_instance = nullptr;

static QString omarchyStateDir()
{
    return QDir::homePath() + QStringLiteral("/.local/state/omarchy/current");
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
    loadFont();
    setupWatcher();
}

OmarchyTheme *OmarchyTheme::instance()
{
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
    ensure("orange", mix(color("red"), color("yellow"), 0.5));

    // Icon theme chosen by the Omarchy theme (icons.theme holds e.g. "Yaru-blue").
    QFile icons(themeDir() + QStringLiteral("/icons.theme"));
    if (icons.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString name = QString::fromUtf8(icons.readAll()).trimmed();
        if (!name.isEmpty())
            QIcon::setThemeName(name);
    }
}

void OmarchyTheme::loadFont()
{
    QString family;
    const QString bin = QStandardPaths::findExecutable(QStringLiteral("omarchy-font-current"));
    if (!bin.isEmpty()) {
        QProcess p;
        p.start(bin, QStringList());
        if (p.waitForFinished(1500) && p.exitCode() == 0)
            family = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
    }
    if (family.isEmpty())
        family = QStringLiteral("JetBrainsMono Nerd Font");

    m_mono = QFont(family);
    if (!QFontDatabase::hasFamily(family))
        m_mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_mono.setStyleHint(QFont::Monospace);
    m_mono.setFixedPitch(true);
    m_mono.setPointSize(qMax(9, QApplication::font().pointSize()));
}

void OmarchyTheme::setupWatcher()
{
    m_watcher = new QFileSystemWatcher(this);
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(300);
    connect(m_debounce, &QTimer::timeout, this, [this] {
        load();
        if (m_app)
            apply(*m_app);
        // theme-set replaces the directory; re-arm the watch on the new files
        m_watcher->removePaths(m_watcher->files());
        m_watcher->removePaths(m_watcher->directories());
        m_watcher->addPath(omarchyStateDir());
        m_watcher->addPath(themeDir() + QStringLiteral("/colors.toml"));
        emit changed();
    });

    m_watcher->addPath(omarchyStateDir());
    m_watcher->addPath(themeDir() + QStringLiteral("/colors.toml"));
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &OmarchyTheme::reapplyLater);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &OmarchyTheme::reapplyLater);
}

void OmarchyTheme::reapplyLater()
{
    m_debounce->start();
}

QColor OmarchyTheme::color(const QString &key) const
{
    const QString v = m_colors.value(key, defaultColors().value(key, QStringLiteral("#ff00ff")));
    return QColor(v);
}

QColor OmarchyTheme::window() const { return m_dark ? color("background") : color("dark_background"); }
QColor OmarchyTheme::base() const { return m_dark ? color("dark_background") : color("background"); }
QColor OmarchyTheme::alternateBase() const { return mix(base(), window(), 0.5); }
QColor OmarchyTheme::text() const { return color("foreground"); }
QColor OmarchyTheme::mutedText() const { return color("dark_foreground"); }
QColor OmarchyTheme::border() const { return mix(window(), color("muted"), 0.7); }
QColor OmarchyTheme::accent() const { return color("accent"); }
QColor OmarchyTheme::selection() const { return color("selection"); }

// Classic diff colours: removed = RGB(255,200,100), added = RGB(255,255,0) on light;
// RGB(83,66,33) / RGB(83,83,0) on dark. We keep the orange/yellow semantics but
// tint the theme's own palette so it blends with the rest of the desktop.
QColor OmarchyTheme::diffNormalBg() const { return base(); }
QColor OmarchyTheme::diffRemovedBg() const { return mix(base(), color("orange"), m_dark ? 0.32 : 0.42); }
QColor OmarchyTheme::diffAddedBg() const { return mix(base(), color("yellow"), m_dark ? 0.32 : 0.42); }
QColor OmarchyTheme::diffInlineRemovedBg() const { return mix(base(), color("red"), m_dark ? 0.55 : 0.6); }
QColor OmarchyTheme::diffInlineAddedBg() const { return mix(base(), color("bright_yellow"), m_dark ? 0.6 : 0.75); }
QColor OmarchyTheme::diffMarginBg() const { return m_dark ? color("lighter_background") : color("lighter_background"); }
QColor OmarchyTheme::diffHeaderBg() const { return diffMarginBg(); }
QColor OmarchyTheme::diffEmptyBg() const { return mix(base(), color("muted"), 0.5); }
QColor OmarchyTheme::diffAddedIcon() const { return color("green"); }
QColor OmarchyTheme::diffRemovedIcon() const { return color("red"); }

void OmarchyTheme::apply(QApplication &app)
{
    m_app = &app;
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette pal;
    const QColor win = window(), bs = base(), txt = text(), acc = accent();
    pal.setColor(QPalette::Window, win);
    pal.setColor(QPalette::WindowText, txt);
    pal.setColor(QPalette::Base, bs);
    pal.setColor(QPalette::AlternateBase, alternateBase());
    pal.setColor(QPalette::Text, txt);
    pal.setColor(QPalette::PlaceholderText, mutedText());
    pal.setColor(QPalette::Button, color("lighter_background"));
    pal.setColor(QPalette::ButtonText, txt);
    pal.setColor(QPalette::BrightText, color("bright_foreground"));
    pal.setColor(QPalette::ToolTipBase, color("lighter_background"));
    pal.setColor(QPalette::ToolTipText, txt);
    pal.setColor(QPalette::Highlight, acc);
    pal.setColor(QPalette::HighlightedText, m_dark ? color("background") : color("background"));
    pal.setColor(QPalette::Link, color("blue"));
    pal.setColor(QPalette::LinkVisited, color("magenta"));
    pal.setColor(QPalette::Light, mix(win, Qt::white, 0.2));
    pal.setColor(QPalette::Midlight, mix(win, Qt::white, 0.1));
    pal.setColor(QPalette::Mid, border());
    pal.setColor(QPalette::Dark, mix(win, Qt::black, 0.2));
    pal.setColor(QPalette::Shadow, mix(win, Qt::black, 0.4));
    pal.setColor(QPalette::Disabled, QPalette::Text, mutedText());
    pal.setColor(QPalette::Disabled, QPalette::WindowText, mutedText());
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, mutedText());
    pal.setColor(QPalette::Inactive, QPalette::Highlight, selection());
    pal.setColor(QPalette::Inactive, QPalette::HighlightedText, txt);
    app.setPalette(pal);
    app.setStyleSheet(buildStyleSheet());
}

QString OmarchyTheme::buildStyleSheet() const
{
    const QString win = window().name(), bs = base().name(), txt = text().name();
    const QString acc = accent().name(), brd = border().name(), mut = mutedText().name();
    const QString btn = color("lighter_background").name();
    const QString sel = selection().name();
    const QString hiText = color("background").name();
    const QString hover = mix(color("lighter_background"), color("foreground"), 0.08).name();

    return QStringLiteral(R"(
QMainWindow, QDialog { background: %1; }
QWidget { color: %3; }
QToolTip { background: %7; color: %3; border: 1px solid %5; padding: 4px; }

QPlainTextEdit, QTextEdit, QLineEdit {
    background: %2; color: %3; border: 1px solid %5; border-radius: 6px;
    selection-background-color: %4; selection-color: %8; padding: 2px;
}
QPlainTextEdit:focus, QTextEdit:focus, QLineEdit:focus { border: 1px solid %4; }

QTableView, QTreeView {
    background: %2; alternate-background-color: %9; border: 1px solid %5; border-radius: 6px;
    gridline-color: %5; selection-background-color: %4; selection-color: %8; outline: 0;
}
QTableView::item, QTreeView::item { padding: 2px 6px; }
QTableView::item:selected, QTreeView::item:selected { background: %4; color: %8; }
QTableView::item:selected:!active, QTreeView::item:selected:!active { background: %10; color: %3; }
QHeaderView::section {
    background: %7; color: %3; padding: 4px 8px; border: none; border-right: 1px solid %5; border-bottom: 1px solid %5;
}
QHeaderView::section:last { border-right: none; }
QTableCornerButton::section { background: %7; border: none; }

QPushButton, QToolButton {
    background: %7; color: %3; border: 1px solid %5; border-radius: 6px; padding: 5px 12px;
}
QToolButton { padding: 4px 6px; }
QPushButton:hover, QToolButton:hover { background: %11; border-color: %4; }
QPushButton:pressed, QToolButton:pressed { background: %10; }
QPushButton:default { border: 1px solid %4; }
QToolButton:checked { background: %4; color: %8; border-color: %4; }
QToolButton:checked:hover { background: %4; color: %8; }
QPushButton:disabled, QToolButton:disabled { color: %6; border-color: %5; }

QCheckBox::indicator, QTableView::indicator, QTreeView::indicator {
    width: 14px; height: 14px; border: 1px solid %6; border-radius: 3px; background: %2;
}
QCheckBox::indicator:checked, QTableView::indicator:checked, QTreeView::indicator:checked {
    background: %4; border-color: %4;
    image: url(:/check.svg);
}
QCheckBox::indicator:indeterminate { background: %2; border-color: %4; image: url(:/partial.svg); }
QCheckBox::indicator:hover, QTableView::indicator:hover { border-color: %4; }

QSplitter::handle { background: %1; }
QSplitter::handle:horizontal { width: 6px; }
QSplitter::handle:vertical { height: 6px; }
QSplitter::handle:hover { background: %4; }

QScrollBar:vertical { background: %1; width: 12px; margin: 0; border: none; }
QScrollBar::handle:vertical { background: %6; min-height: 24px; border-radius: 4px; margin: 2px 3px; }
QScrollBar::handle:vertical:hover { background: %4; }
QScrollBar:horizontal { background: %1; height: 12px; margin: 0; border: none; }
QScrollBar::handle:horizontal { background: %6; min-width: 24px; border-radius: 4px; margin: 3px 2px; }
QScrollBar::handle:horizontal:hover { background: %4; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }

QStatusBar { background: %1; color: %6; border-top: 1px solid %5; }
QLabel#headerLabel { color: %6; }
QLabel#branchLabel { color: %4; font-weight: bold; }
QMenu { background: %7; border: 1px solid %5; border-radius: 6px; padding: 4px; }
QMenu::item { padding: 5px 20px; border-radius: 4px; }
QMenu::item:selected { background: %4; color: %8; }
QToolBar { background: %1; border: none; spacing: 4px; }
QFrame#diffHeader { background: %7; border: 1px solid %5; border-bottom: none; border-top-left-radius: 6px; border-top-right-radius: 6px; }
)")
        .arg(win, bs, txt, acc, brd, mut, btn, hiText, alternateBase().name(), sel, hover);
}
