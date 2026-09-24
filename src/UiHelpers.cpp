#include "UiHelpers.h"
#include "OmarchyTheme.h"

#include <QAction>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QtMath>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainterPath>
#include <QPalette>
#include <QPointer>
#include <QStyle>
#include <QStyleOptionToolButton>
#include <QStylePainter>
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

// The design's button height (kit.js button(): h = 28), the square of an
// icon-only one, and its padding, glyph box and chevron box.
constexpr int kButtonHeight = 28;
constexpr int kButtonPad = 10, kButtonGlyph = 14, kButtonGap = 6;
constexpr int kChevronGap = 6, kChevronBox = 12, kChevronNudge = 2;
constexpr qreal kChevronOpacity = 0.7;

// A button's chrome by the style (fill, border, the state of the moment),
// without its text, and the pen the stylesheet gives the text in that state:
// a checked button wears the accent, a disabled one the disabled pen of the
// palette it polished. `option` is the button's own (initStyleOption() is
// protected).
QColor paintChrome(const QToolButton *button, QStylePainter &p, QStyleOptionToolButton option)
{
    option.text.clear();
    option.icon = QIcon();
    p.drawComplexControl(QStyle::CC_ToolButton, option);
    const bool enabled = button->isEnabled();
    if (enabled && button->isChecked())
        return OmarchyTheme::instance()->accent();
    return button->palette().color(enabled ? QPalette::Active : QPalette::Disabled, QPalette::ButtonText);
}

// A glyph centred by its ink in `box`: a Nerd Font glyph's ink hangs over the
// advance its metrics report, which the style's centring by advance shows.
void drawCentred(QPainter &p, const QString &glyph, const QRectF &box)
{
    p.drawText(box.center() - inkRect(p.font(), glyph).center(), glyph);
}

// The square of iconButton(), which re-fits itself with the base font.
class IconButton : public GlyphButton
{
public:
    // An inline one is a fixed square. A toolbar one is only fixed sideways
    // and asks for the design's 28 px height, which the text buttons beside
    // it come to as well; a row taller than that may still stretch it.
    void fit(IconButtonSize size)
    {
        const int px = int(size);
        m_toolbar = size == IconButtonSize::Toolbar;
        if (m_toolbar)
            setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);
        onThemeScale(this, [this, px] {
            if (m_toolbar)
                setFixedWidth(space(px));
            else
                setFixedSize(space(px), space(px));
            updateGeometry();
        });
    }

    QSize sizeHint() const override
    {
        if (!m_toolbar)
            return QToolButton::sizeHint();
        return QSize(space(int(IconButtonSize::Toolbar)), space(kButtonHeight));
    }
    QSize minimumSizeHint() const override { return sizeHint(); }

private:
    bool m_toolbar = false;
};

// A separator that re-colours itself on a theme change: every section of the
// window holds one or more, and none of them wants its own applyTheme().
class Hairline : public QWidget
{
public:
    Hairline(Qt::Orientation orientation, HairlineTone tone)
        : m_tone(tone)
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
        const OmarchyTheme *theme = OmarchyTheme::instance();
        QPalette pal = palette();
        pal.setColor(QPalette::Window, m_tone == HairlineTone::Chrome ? theme->hairline() : theme->border());
        setPalette(pal);
    }

    HairlineTone m_tone;
};

// A KitButton's text taken apart again: the glyph icon() put in front (with
// its two spaces), the chevron() at the end, and the label between them.
struct KitParts {
    QString glyph;
    QString label;
    bool chevron = false;
};

// A Nerd Font glyph lives in a Private Use Area: the BMP's, or the
// supplementary planes 15 and 16 that the Material Design icons use.
bool isGlyph(char32_t cp)
{
    return (cp >= 0xE000 && cp <= 0xF8FF) || cp >= 0xF0000;
}

KitParts kitParts(const QString &text)
{
    KitParts parts;
    QString rest = text;
    const QString suffix = chevron();
    if (rest.endsWith(suffix)) {
        parts.chevron = true;
        rest.chop(suffix.size());
    }
    const QList<uint> points = rest.toUcs4();
    if (!points.isEmpty() && isGlyph(points.first())) {
        const QString glyph = QString::fromUcs4(reinterpret_cast<const char32_t *>(points.constData()), 1);
        const QString after = rest.mid(glyph.size());
        // icon() puts two spaces after its glyph; a glyph with nothing after
        // it is an icon-only face.
        if (after.isEmpty() || after.startsWith(QStringLiteral("  "))) {
            parts.glyph = glyph;
            rest = after.mid(qMin(qsizetype(2), after.size()));
        }
    }
    parts.label = rest;
    return parts;
}

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
int barGap() { return space(5); }
int buttonHeight() { return space(kButtonHeight); }
int sectionGap() { return space(16); }
int windowMargin() { return space(12); }

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

QRectF inkRect(const QFont &font, const QString &text)
{
    QPainterPath path;
    path.addText(0, 0, font, text);
    const QRectF ink = path.boundingRect();
    return ink.isEmpty() ? QFontMetricsF(font).tightBoundingRect(text) : ink;
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

ElidedLabel::ElidedLabel(QWidget *parent)
    : QLabel(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
}

void ElidedLabel::setFullText(const QString &text)
{
    m_fullText = text;
    updateGeometry(); // the size hint is the full text's
    elide();
}

QSize ElidedLabel::sizeHint() const
{
    const QMargins m = contentsMargins();
    return QSize(fontMetrics().horizontalAdvance(m_fullText) + m.left() + m.right() + 2 * margin(),
                 QLabel::sizeHint().height());
}

QSize ElidedLabel::minimumSizeHint() const
{
    return QSize(0, sizeHint().height());
}

void ElidedLabel::resizeEvent(QResizeEvent *event)
{
    QLabel::resizeEvent(event);
    elide();
}

void ElidedLabel::changeEvent(QEvent *event)
{
    QLabel::changeEvent(event);
    if (event->type() == QEvent::FontChange) {
        updateGeometry();
        elide();
    }
}

// The copy on screen, and the tooltip that spells out what it leaves off.
// Neither is set again when it already is what it should be: a resize that
// stays inside one elision relays nothing out.
void ElidedLabel::elide()
{
    const int room = qMax(0, contentsRect().width() - 2 * margin());
    const QString shown = fontMetrics().elidedText(m_fullText, Qt::ElideRight, room);
    if (text() != shown)
        setText(shown);
    const QString tip = shown == m_fullText ? QString() : m_fullText;
    if (toolTip() != tip)
        setToolTip(tip);
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

void setIconForm(QToolButton *button, bool on)
{
    // Repolishing is a whole style pass, so only a real change pays for one.
    if (button->property("iconForm").toBool() != on) {
        button->setProperty("iconForm", on);
        button->style()->unpolish(button);
        button->style()->polish(button);
        button->update();
    }
    // Both are no-ops when the width already is what the form asks, and a
    // text-size change gets the new square from the same call.
    if (on) {
        button->setFixedWidth(space(int(IconButtonSize::Toolbar)));
    } else {
        button->setMinimumWidth(0);
        button->setMaximumWidth(QWIDGETSIZE_MAX);
    }
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
    // The label takes the row's height and puts its baseline where the
    // design's text has it: the row's middle plus 0.36 of the caption size
    // (kit.js text()), which centring the text box by Qt's metrics leaves a
    // pixel high.
    label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    row->addWidget(label);
    onThemeScale(row, [row, strut, label] {
        row->setSpacing(headerGap());
        strut->changeSize(0, headerRowHeight(), QSizePolicy::Fixed, QSizePolicy::Fixed);
        const QFont caption = OmarchyTheme::instance()->captionFont();
        const int baseline = qRound(headerRowHeight() / 2.0 + 0.36 * caption.pixelSize());
        label->setContentsMargins(0, qMax(0, baseline - QFontMetrics(caption).ascent()), 0, 0);
        row->invalidate();
    });
    return row;
}

GlyphButton::GlyphButton(QWidget *parent)
    : QToolButton(parent)
{
}

void GlyphButton::paintEvent(QPaintEvent *)
{
    QStylePainter p(this);
    QStyleOptionToolButton option;
    initStyleOption(&option);
    const QColor pen = paintChrome(this, p, option);
    const QString glyph = text();
    if (glyph.isEmpty())
        return;
    p.setFont(font());
    p.setPen(pen);
    // Every braille frame by the full cell's ink, so the spinner stays put.
    const bool braille = glyph.size() == 1 && glyph.at(0).unicode() >= 0x2800 && glyph.at(0).unicode() <= 0x28FF;
    const QRectF ink = inkRect(font(), braille ? QString(QChar(0x28FF)) : glyph);
    p.drawText(QRectF(rect()).center() - ink.center(), glyph);
}

KitButton::KitButton(QWidget *parent)
    : QToolButton(parent)
{
}

QSize KitButton::sizeHint() const
{
    const int height = space(kButtonHeight);
    const KitParts parts = kitParts(text());
    // The square: the icon form, and a face that is a glyph and nothing else.
    if (property("iconForm").toBool() || (parts.label.isEmpty() && !parts.chevron))
        return QSize(space(int(IconButtonSize::Toolbar)), height);
    int width = 2 * space(kButtonPad);
    if (!parts.glyph.isEmpty())
        width += space(kButtonGlyph) + (parts.label.isEmpty() ? 0 : space(kButtonGap));
    // Rounded up: the label is drawn whole only in a box at least as wide as
    // its fractional advance, and a rounded-down width would elide it.
    width += qCeil(QFontMetricsF(font()).horizontalAdvance(parts.label));
    if (parts.chevron)
        width += space(kChevronGap) + space(kChevronBox);
    return QSize(width, height);
}

QSize KitButton::minimumSizeHint() const
{
    return sizeHint();
}

// The chrome (fill, border, the state of the moment) is the style's; the
// three parts are placed here, on the design's grid rather than centred as
// one string, so the glyph sits 10 px in and the label 6 px after its box.
void KitButton::paintEvent(QPaintEvent *)
{
    QStylePainter p(this);
    QStyleOptionToolButton option;
    initStyleOption(&option);
    const QColor pen = paintChrome(this, p, option);
    const QString text = this->text();
    if (text.isEmpty())
        return;
    p.setFont(font());
    p.setPen(pen);
    const auto centred = [&](const QString &glyph, const QRectF &box) { drawCentred(p, glyph, box); };

    const KitParts parts = kitParts(text);
    if (property("iconForm").toBool() || (parts.label.isEmpty() && !parts.chevron)) {
        centred(parts.glyph.isEmpty() ? parts.label : parts.glyph, QRectF(rect()));
        return;
    }
    int x = space(kButtonPad);
    if (!parts.glyph.isEmpty()) {
        centred(parts.glyph, QRectF(x, 0, space(kButtonGlyph), height()));
        x += space(kButtonGlyph) + space(kButtonGap);
    }
    const int chevronLeft = width() - space(kButtonPad) - space(kChevronBox) + space(kChevronNudge);
    if (!parts.label.isEmpty()) {
        const int end = width() - space(kButtonPad)
            - (parts.chevron ? space(kChevronGap) + space(kChevronBox) : 0);
        const QString label = fontMetrics().elidedText(parts.label, Qt::ElideRight, qMax(0, end - x));
        p.drawText(QRect(x, 0, qMax(0, end - x), height()), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextDontClip, label);
    }
    if (parts.chevron) {
        // The design's chevron is a 12 px icon beside the 14 px ones, in the
        // plain foreground at 70 % whatever colour the label wears.
        QColor dim = isEnabled() ? OmarchyTheme::instance()->text() : pen;
        dim.setAlphaF(dim.alphaF() * kChevronOpacity);
        p.setPen(dim);
        QFont small = font();
        if (small.pixelSize() > 0)
            small.setPixelSize(qMax(1, qRound(small.pixelSize() * kChevronBox / double(kButtonGlyph))));
        else
            small.setPointSizeF(small.pointSizeF() * kChevronBox / kButtonGlyph);
        p.setFont(small);
        const QString glyph = chevron().trimmed();
        const QRectF box(chevronLeft, 0, space(kChevronBox), height());
        p.drawText(box.center() - inkRect(small, glyph).center(), glyph);
    }
}

QToolButton *dropdownButton(const QString &objectName)
{
    auto *b = toolButton<KitButton>(QString());
    b->setObjectName(objectName);
    b->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    return b;
}

QWidget *hairline(Qt::Orientation orientation, HairlineTone tone)
{
    return new Hairline(orientation, tone);
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

namespace {
class MenuInWindow : public QObject
{
public:
    MenuInWindow(QMenu *menu, QWidget *button, QWidget *bar)
        : QObject(menu), m_button(button), m_bar(bar)
    {
        menu->installEventFilter(this);
    }

protected:
    // The Show event comes after popup() has placed the menu and before the
    // window system maps it: the one moment it can still be moved.
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() != QEvent::Show)
            return false;
        auto *menu = static_cast<QMenu *>(watched);
        const QWidget *window = m_button->window();
        const QRect area(window->mapToGlobal(QPoint(0, 0)), window->size());
        const QRect button(m_button->mapToGlobal(QPoint(0, 0)), m_button->size());
        QPoint pos = menu->pos();
        if (m_bar && pos.y() >= button.y() + button.height())
            pos.setY(popupTop(m_bar));
        if (pos.x() + menu->width() > area.x() + area.width())
            pos.setX(qMax(area.x(), area.x() + area.width() - windowMargin() - menu->width()));
        if (pos.y() + menu->height() > area.y() + area.height() && button.y() - menu->height() >= area.y())
            pos.setY(button.y() - menu->height());
        if (pos != menu->pos())
            menu->move(pos);
        return false;
    }

private:
    QWidget *m_button;
    QPointer<QWidget> m_bar;
};

// The design's gap between the top bar and a popup hanging from it.
constexpr int kPopupDrop = 4;
} // namespace

void keepMenuInWindow(QMenu *menu, QWidget *button, QWidget *bar)
{
    new MenuInWindow(menu, button, bar);
}

int popupTop(const QWidget *bar)
{
    return bar->mapToGlobal(QPoint(0, bar->height())).y() + space(kPopupDrop);
}

int popupWidth(const QWidget *window, int px)
{
    return qMax(1, qMin(space(px), window->width() - 2 * windowMargin()));
}

// The shell's list row: 2.33 × the base font, so the rows grow with the text size.
int tableRowHeight()
{
    return qRound(OmarchyTheme::instance()->fontBase() * 2.33);
}

// The design's file row and table header (screens.js changesTable(): rh and hh).
int fileRowHeight()
{
    return space(26);
}

int tableHeaderHeight()
{
    return space(26);
}

void fitStretchColumn(QTableView *table, int column, int others, int floor)
{
    table->setColumnWidth(column, qMax(floor, table->viewport()->width() - others));
}

void onHeaderResize(QTableView *table, std::function<void()> fit)
{
    QHeaderView *header = table->horizontalHeader();
    header->installEventFilter(new ResizeWatcher(header, std::move(fit)));
}

} // namespace ui
