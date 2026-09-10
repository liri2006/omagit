#include "UiHelpers.h"
#include "OmarchyTheme.h"

#include <QAction>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFontMetrics>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPalette>
#include <QTableView>
#include <QWidgetAction>

namespace ui {
namespace {

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

QToolButton *smallButton(uint glyph, const QString &fallback, const QString &tip)
{
    auto *b = toolButton(icon(glyph, fallback).trimmed(), tip);
    b->setObjectName(QStringLiteral("smallButton"));
    return b;
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
