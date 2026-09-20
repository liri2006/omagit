#include "UiHelpers.h"
#include "OmarchyTheme.h"

#include <QAction>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPalette>
#include <QTableView>
#include <QVBoxLayout>
#include <QWidgetAction>

namespace ui {
namespace {

// Runs `apply` now and once more on every theme change: whatever was measured
// in space() pixels has to follow a live text-size change.
void onThemeScale(QObject *owner, const std::function<void()> &apply)
{
    apply();
    QObject::connect(OmarchyTheme::instance(), &OmarchyTheme::changed, owner, apply);
}

// The square of iconButton(), which re-fits itself with the base font.
class IconButton : public QToolButton
{
public:
    // An inline one is a fixed square. A toolbar one is only fixed sideways;
    // its height is left to the stylesheet's button padding, which is what
    // makes the fields and text buttons beside it the height they are.
    void fit(IconButtonSize size)
    {
        const int px = int(size);
        if (size == IconButtonSize::Toolbar)
            setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);
        onThemeScale(this, [this, px, size] {
            if (size == IconButtonSize::Toolbar)
                setFixedWidth(space(px));
            else
                setFixedSize(space(px), space(px));
        });
    }
};

// A separator that re-colours itself on a theme change: every section of the
// window holds one or more, and none of them wants its own applyTheme().
class Hairline : public QWidget
{
public:
    explicit Hairline(Qt::Orientation orientation)
    {
        setAutoFillBackground(true);
        if (orientation == Qt::Horizontal)
            setFixedHeight(1);
        else
            setFixedWidth(1);
        recolor();
        connect(OmarchyTheme::instance(), &OmarchyTheme::changed, this, [this] { recolor(); });
    }

private:
    void recolor()
    {
        QPalette pal = palette();
        pal.setColor(QPalette::Window, OmarchyTheme::instance()->border());
        setPalette(pal);
    }
};

// Calls back on every resize of the widget it watches.
class ResizeWatcher : public QObject
{
public:
    ResizeWatcher(QObject *parent, std::function<void()> run)
        : QObject(parent), m_run(std::move(run))
    {
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Resize)
            m_run();
        return QObject::eventFilter(watched, event);
    }

private:
    std::function<void()> m_run;
};

} // namespace

int space(int px)
{
    return qMax(1, qRound(px * OmarchyTheme::instance()->fontBase() / 12.0));
}

int headerRowHeight() { return space(24); }
int headerGap() { return space(6); }
int sectionGap() { return space(16); }

QString icon(uint cp, const QString &fallback)
{
    const QString g = OmarchyTheme::instance()->glyph(cp);
    return g.isEmpty() ? fallback : g + QStringLiteral("  ");
}

QStringList spinnerFrames(const QFont &font)
{
    const QFontMetrics fm(font);
    if (fm.inFont(QChar(0x280B)))
        return {QStringLiteral("⠋"), QStringLiteral("⠙"), QStringLiteral("⠹"), QStringLiteral("⠸"), QStringLiteral("⠼"),
                QStringLiteral("⠴"), QStringLiteral("⠦"), QStringLiteral("⠧"), QStringLiteral("⠇"), QStringLiteral("⠏")};
    if (fm.inFont(QChar(0x25D0)))
        return {QStringLiteral("◐"), QStringLiteral("◓"), QStringLiteral("◑"), QStringLiteral("◒")};
    return {QStringLiteral("|"), QStringLiteral("/"), QStringLiteral("-"), QStringLiteral("\\")};
}

QString chevron()
{
    const QString g = OmarchyTheme::instance()->glyph(kChevron);
    return QStringLiteral("  ") + (g.isEmpty() ? QStringLiteral("▾") : g);
}

QString tildePath(const QString &path)
{
    const QString home = QDir::homePath();
    if (path == home)
        return QStringLiteral("~");
    if (path.startsWith(home + QLatin1Char('/')))
        return QStringLiteral("~") + path.mid(home.size());
    return path;
}

QString ago(const QDateTime &when)
{
    const qint64 secs = when.secsTo(QDateTime::currentDateTime());
    if (secs < 60)
        return QCoreApplication::translate("ui", "just now");
    if (secs < 3600)
        return QCoreApplication::translate("ui", "%n minute(s) ago", nullptr, int(secs / 60));
    return QCoreApplication::translate("ui", "%n hour(s) ago", nullptr, int(secs / 3600));
}

QLabel *sectionLabel(const QString &text)
{
    auto *l = new QLabel(text.toUpper());
    l->setObjectName(QStringLiteral("sectionLabel"));
    l->setFont(OmarchyTheme::instance()->captionFont());
    return l;
}

QLabel *dimLabel(const QString &text)
{
    auto *l = new QLabel(text);
    l->setObjectName(QStringLiteral("dimLabel"));
    l->setFont(OmarchyTheme::instance()->captionFont());
    return l;
}

QToolButton *iconButton(uint glyph, const QString &fallback, const QString &tip, IconButtonSize size, bool ghost)
{
    auto *b = toolButton<IconButton>(icon(glyph, fallback).trimmed(), tip);
    b->fit(size);
    b->setObjectName(QStringLiteral("iconButton"));
    // The stylesheet tells the two kinds apart by these: chrome or none, and
    // the vertical padding that puts a toolbar one on the row's height.
    b->setProperty("ghost", ghost);
    b->setProperty("toolbar", size == IconButtonSize::Toolbar);
    return b;
}

QLineEdit *promptField(const QString &placeholder)
{
    auto *field = new QLineEdit;
    field->setObjectName(QStringLiteral("promptField"));
    field->setPlaceholderText(placeholder);
    field->setFrame(false);
    onThemeScale(field, [field] {
        field->setFixedHeight(space(28));
        // The prompt reads like a menu entry waiting to be typed over, so its
        // placeholder is as dim as the magnifier beside it.
        QPalette pal = field->palette();
        pal.setColor(QPalette::PlaceholderText, OmarchyTheme::instance()->mutedText());
        field->setPalette(pal);
    });
    return field;
}

QWidget *promptBox(QLineEdit *field)
{
    auto *box = new QWidget;
    auto *rows = new QVBoxLayout(box);
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(0);

    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    // The magnifier stands where a menu entry's text starts, and the typed
    // text 22 px after it, the way the design puts an entry's icon and label.
    QLabel *magnifier = nullptr;
    const QString glyph = icon(kMagnify).trimmed();
    if (!glyph.isEmpty()) {
        magnifier = new QLabel(glyph);
        magnifier->setObjectName(QStringLiteral("promptIcon"));
        magnifier->setAlignment(Qt::AlignCenter);
        row->addWidget(magnifier);
    }
    row->addWidget(field, 1);
    rows->addLayout(row);

    auto *hairRow = new QHBoxLayout;
    hairRow->addWidget(hairline());
    rows->addLayout(hairRow);

    onThemeScale(box, [row, hairRow, magnifier] {
        row->setContentsMargins(space(14), 0, space(14), space(3));
        row->setSpacing(space(8));
        if (magnifier)
            magnifier->setFixedWidth(space(14));
        // Inset like the menu's own separators, and the same 2 px of air
        // under it before the first entry.
        hairRow->setContentsMargins(space(4), 0, space(4), space(2));
    });
    return box;
}

QHBoxLayout *sectionHeaderRow(QLabel *label)
{
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    // A strut of no width holds the row at the design's 24 px, so the label
    // and the icon buttons the caller adds share one line.
    auto *strut = new QSpacerItem(0, headerRowHeight(), QSizePolicy::Fixed, QSizePolicy::Fixed);
    row->addItem(strut);
    row->addWidget(label, 0, Qt::AlignVCenter);
    onThemeScale(row, [row, strut] {
        row->setSpacing(headerGap());
        strut->changeSize(0, headerRowHeight(), QSizePolicy::Fixed, QSizePolicy::Fixed);
        row->invalidate();
    });
    return row;
}

QToolButton *dropdownButton(const QString &objectName)
{
    auto *b = toolButton(QString());
    b->setObjectName(objectName);
    b->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    return b;
}

QWidget *hairline(Qt::Orientation orientation)
{
    return new Hairline(orientation);
}

QAction *addMenuHeader(QMenu *menu, const QString &text)
{
    auto *action = new QWidgetAction(menu);
    QLabel *label = sectionLabel(text);
    label->setContentsMargins(14, 6, 14, 3);
    action->setDefaultWidget(label);
    menu->addAction(action);
    return action;
}

// The shell's list row: 2.33 × the base font, so the rows grow with the text size.
int tableRowHeight()
{
    return qRound(OmarchyTheme::instance()->fontBase() * 2.33);
}

void fitStretchColumn(QTableView *table, int column, int others)
{
    table->setColumnWidth(column, qMax(kMinStretchColumn, table->viewport()->width() - others));
}

void onHeaderResize(QTableView *table, std::function<void()> fit)
{
    QHeaderView *header = table->horizontalHeader();
    header->installEventFilter(new ResizeWatcher(header, std::move(fit)));
}

} // namespace ui
