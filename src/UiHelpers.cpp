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
#include <QPushButton>
#include <QStyle>
#include <QStyleOptionButton>
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

// The design's chevron (kit.js button()): the plain foreground at 70 %
// whatever colour the label wears.
constexpr qreal kChevronOpacity = 0.7;
// What windowMargin() reads off a window (setWindowMargin()), in design px.
const char *const kWindowMarginProperty = "gridMargin";
// What popupTop() hangs a bar's popups from (setPopupEdge()), in the bar's px.
const char *const kPopupEdgeProperty = "popupEdge";
// What QLineEdit keeps between its contents rectangle and the text on its own
// (QLineEditPrivate::horizontalMargin), under any style: a prompt's text
// stands that much closer to the magnifier than the design's 4, so the two
// together come to it.
constexpr int kLineEditMargin = 2;

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
        return QSize(space(int(IconButtonSize::Toolbar)), space(box::control));
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

int gridUnit()
{
    return qMax(1, qRound(4 * OmarchyTheme::instance()->fontBase() / 12.0));
}

int space(int px)
{
    const int unit = gridUnit();
    return qMax(1, px / 4 * unit + qRound(px % 4 * unit / 4.0));
}

int fontPx(int px)
{
    return qMax(1, qRound(px * OmarchyTheme::instance()->fontBase() / 12.0));
}

int windowMargin(const QWidget *widget)
{
    const QVariant margin = widget ? widget->window()->property(kWindowMarginProperty) : QVariant();
    return space(margin.isValid() ? margin.toInt() : kRegularDensity.margin);
}

void setWindowMargin(QWidget *window, int px)
{
    window->setProperty(kWindowMarginProperty, px);
}

void fitDialogWidth(QWidget *dialog, int designPx)
{
    const QWidget *host = dialog->parentWidget() ? dialog->parentWidget()->window() : nullptr;
    int width = host ? qMin(space(designPx), host->width() - 2 * windowMargin(host)) : space(designPx);
    if (QLayout *l = dialog->layout()) {
        l->invalidate();
        width = qMax(width, l->totalMinimumSize().width());
    }
    if (width != dialog->width() || dialog->minimumWidth() != width || dialog->maximumWidth() != width)
        dialog->setFixedWidth(width);
}

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
    if (secs < 120)
        return QCoreApplication::translate("ui", "1 minute ago");
    if (secs < 3600)
        return QCoreApplication::translate("ui", "%1 minutes ago").arg(secs / 60);
    if (secs < 7200)
        return QCoreApplication::translate("ui", "1 hour ago");
    return QCoreApplication::translate("ui", "%1 hours ago").arg(secs / 3600);
}

QLabel *sectionLabel(const QString &text)
{
    auto *l = new QLabel(text.toUpper());
    l->setObjectName(QStringLiteral("sectionLabel"));
    l->setFont(OmarchyTheme::instance()->captionFont());
    return l;
}

void placeOnLine(QLabel *label, const QFont &font, int px)
{
    const int baseline = qRound(space(px) / 2.0 + 0.36 * font.pixelSize());
    const QMargins m = label->contentsMargins();
    label->setContentsMargins(m.left(), qMax(0, baseline - QFontMetrics(font).ascent()), m.right(), 0);
    label->setAlignment((label->alignment() & Qt::AlignHorizontal_Mask) | Qt::AlignTop);
    label->setFixedHeight(space(px));
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

void setIconForm(QToolButton *button, bool on, int px)
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
        button->setProperty("iconFormWidth", px);
        button->setFixedWidth(space(px));
    } else {
        button->setMinimumWidth(0);
        button->setMaximumWidth(QWIDGETSIZE_MAX);
    }
}

void setPrimary(QAbstractButton *button, bool on)
{
    if (button->property("primary").toBool() == on)
        return;
    button->setProperty("primary", on);
    button->style()->unpolish(button);
    button->style()->polish(button);
    button->updateGeometry();
    button->update();
}

QLineEdit *promptField(const QString &placeholder)
{
    auto *field = new QLineEdit;
    field->setObjectName(QStringLiteral("promptField"));
    field->setPlaceholderText(placeholder);
    field->setFrame(false);
    onThemeScale(field, [field] {
        field->setFixedHeight(space(box::control));
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
    // The magnifier stands where a menu entry's icon does and the typed text
    // where its label does (kit.js field(), state prompt): [8][16][4][text].
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
        row->setContentsMargins(space(pad::control), 0, space(pad::control), 0);
        row->setSpacing(qMax(0, space(gap::icon) - kLineEditMargin));
        if (magnifier)
            magnifier->setFixedWidth(space(box::icon));
        // Under the field, the band of a menu separator (screens.js
        // menuCard()): 8 high, the hairline in its middle, inset 4.
        hairRow->setContentsMargins(space(4), space(4), space(4), space(8) - space(4) - 1);
    });
    return box;
}

QHBoxLayout *sectionHeaderRow(QLabel *label)
{
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    // A strut of no width holds the row at the design's 24 px, so the label
    // and the icon buttons the caller adds share one line.
    auto *strut = new QSpacerItem(0, space(box::row), QSizePolicy::Fixed, QSizePolicy::Fixed);
    row->addItem(strut);
    // The label takes the row's height and puts its baseline where the
    // design's text has it (placeOnLine()).
    label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    row->addWidget(label);
    onThemeScale(row, [row, strut, label] {
        row->setSpacing(0); // the caller's buttons keep gaps of their own
        strut->changeSize(0, space(box::row), QSizePolicy::Fixed, QSizePolicy::Fixed);
        placeOnLine(label, OmarchyTheme::instance()->captionFont(), box::row);
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

namespace {

// The padding at either side: the primary action's 16, everyone else's 8.
int kitPad(const QWidget *button)
{
    return space(button->property("primary").toBool() ? pad::primary : pad::control);
}

// Whether a face is the square: the icon form, or a glyph and nothing else.
bool kitSquare(const QWidget *button, const KitParts &parts)
{
    return button->property("iconForm").toBool() || (parts.label.isEmpty() && !parts.chevron);
}

// kit.js measureButton(): [pad][glyph box 16][4][label][4][chevron box 12]
// [pad], 28 high; the square as wide as its form says.
QSize kitSize(const QWidget *button, const QString &text)
{
    const int height = space(box::control);
    const KitParts parts = kitParts(text);
    if (kitSquare(button, parts)) {
        const QVariant px = button->property("iconFormWidth");
        return QSize(space(px.isValid() ? px.toInt() : int(IconButtonSize::Toolbar)), height);
    }
    int width = 2 * kitPad(button);
    if (!parts.glyph.isEmpty())
        width += space(box::icon) + (parts.label.isEmpty() ? 0 : space(gap::icon));
    // Rounded up: the label is drawn whole only in a box at least as wide as
    // its fractional advance, and a rounded-down width would elide it.
    width += qCeil(QFontMetricsF(button->font()).horizontalAdvance(parts.label));
    if (parts.chevron)
        width += space(gap::icon) + space(box::chevron);
    return QSize(width, height);
}

// The three parts on the design's grid rather than centred as one string: the
// glyph's 16 px box `pad` in, the glyph keeping its font size and centred by
// its ink in the box, the label 4 after it; the chevron's 12 px box `pad` from
// the right edge, the glyph at 12/16 of the others' size, in the plain
// foreground at 70 % whatever colour the label wears. A button wider than it
// asks keeps its content at the left, as the design's, unless `centred`: then
// the content is centred as one.
void paintKitFace(QPainter &p, const QWidget *button, const QString &text, const QColor &pen, bool centred = false)
{
    if (text.isEmpty())
        return;
    p.setFont(button->font());
    p.setPen(pen);
    int width = button->width();
    const int height = button->height();
    const KitParts parts = kitParts(text);
    if (kitSquare(button, parts)) {
        drawCentred(p, parts.glyph.isEmpty() ? parts.label : parts.glyph, QRectF(button->rect()));
        return;
    }
    if (centred) {
        const int slack = width - kitSize(button, text).width();
        if (slack > 0) {
            p.translate(slack / 2, 0);
            width -= slack;
        }
    }
    const int pad = kitPad(button);
    int x = pad;
    if (!parts.glyph.isEmpty()) {
        drawCentred(p, parts.glyph, QRectF(x, 0, space(box::icon), height));
        x += space(box::icon) + space(gap::icon);
    }
    const int chevronLeft = width - pad - space(box::chevron);
    if (!parts.label.isEmpty()) {
        const int end = width - pad - (parts.chevron ? space(gap::icon) + space(box::chevron) : 0);
        const QString label = button->fontMetrics().elidedText(parts.label, Qt::ElideRight, qMax(0, end - x));
        p.drawText(QRect(x, 0, qMax(0, end - x), height), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextDontClip, label);
    }
    if (parts.chevron) {
        QColor dim = button->isEnabled() ? OmarchyTheme::instance()->text() : pen;
        dim.setAlphaF(dim.alphaF() * kChevronOpacity);
        p.setPen(dim);
        QFont small = button->font();
        if (small.pixelSize() > 0)
            small.setPixelSize(qMax(1, qRound(small.pixelSize() * box::chevron / double(box::icon))));
        else
            small.setPointSizeF(small.pointSizeF() * box::chevron / box::icon);
        p.setFont(small);
        const QString glyph = chevron().trimmed();
        const QRectF chevronBox(chevronLeft, 0, space(box::chevron), height);
        p.drawText(chevronBox.center() - inkRect(small, glyph).center(), glyph);
    }
}

} // namespace

QSize KitButton::sizeHint() const
{
    return kitSize(this, text());
}

QSize KitButton::minimumSizeHint() const
{
    return sizeHint();
}

// The chrome (fill, border, the state of the moment) is the style's; the
// three parts are the kit's (paintKitFace()).
void KitButton::paintEvent(QPaintEvent *)
{
    QStylePainter p(this);
    QStyleOptionToolButton option;
    initStyleOption(&option);
    const QColor pen = paintChrome(this, p, option);
    paintKitFace(p, this, text(), pen);
}

KitPushButton::KitPushButton(QWidget *parent)
    : QPushButton(parent)
{
}

QSize KitPushButton::sizeHint() const
{
    return kitSize(this, text());
}

QSize KitPushButton::minimumSizeHint() const
{
    return sizeHint();
}

// The bevel is the style's, for the state of the moment; the pen the
// stylesheet's for a push button: the accent on the default one, the
// disabled pen of the palette on a disabled one. Stretched, its content is
// centred.
void KitPushButton::paintEvent(QPaintEvent *)
{
    QStylePainter p(this);
    QStyleOptionButton option;
    initStyleOption(&option);
    option.text.clear();
    option.icon = QIcon();
    p.drawControl(QStyle::CE_PushButton, option);
    const QColor pen = !isEnabled() ? palette().color(QPalette::Disabled, QPalette::ButtonText)
        : isDefault()               ? OmarchyTheme::instance()->accent()
                                    : palette().color(QPalette::Active, QPalette::ButtonText);
    paintKitFace(p, this, text(), pen, true);
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
    // On the row's grid (screens.js menuCard(), a section): 8 in, and the
    // baseline where kit.js text() puts it for the row's middle (the middle
    // plus 0.36 of the caption size).
    label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    onThemeScale(label, [label] {
        label->setContentsMargins(space(pad::control), 0, space(pad::control), 0);
        placeOnLine(label, OmarchyTheme::instance()->captionFont(), box::row);
    });
    action->setDefaultWidget(label);
    menu->addAction(action);
    return action;
}

namespace {
class MenuInWindow : public QObject
{
public:
    MenuInWindow(QMenu *menu, QWidget *button, QWidget *bar, bool above)
        : QObject(menu), m_button(button), m_bar(bar), m_above(above)
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
            pos.setY(popupTop(m_bar, m_button));
        if (pos.x() + menu->width() > area.x() + area.width())
            pos.setX(qMax(area.x(), area.x() + area.width() - windowMargin(window) - menu->width()));
        // Upwards, 4 over the button (screens.js: the OptionsMenu card).
        const int above = button.y() - space(gap::cluster) - menu->height();
        if ((m_above || pos.y() + menu->height() > area.y() + area.height()) && above >= area.y())
            pos.setY(above);
        if (pos != menu->pos())
            menu->move(pos);
        return false;
    }

private:
    QWidget *m_button;
    QPointer<QWidget> m_bar;
    bool m_above;
};

// Qt's own rule for a submenu at the screen's edges — beside its menu, on
// the other side where it does not fit, over the menu where neither side
// has the room — with the window for the screen.
class SubmenuInWindow : public QObject
{
public:
    SubmenuInWindow(QMenu *submenu, QWidget *inWindow)
        : QObject(submenu), m_inWindow(inWindow)
    {
        submenu->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() != QEvent::Show)
            return false;
        auto *menu = static_cast<QMenu *>(watched);
        auto *parent = qobject_cast<QMenu *>(menu->parentWidget());
        if (!parent || !m_inWindow)
            return false;
        const QWidget *window = m_inWindow->window();
        const int margin = windowMargin(window);
        const QRect area(window->mapToGlobal(QPoint(0, 0)), window->size());
        const QRect beside(parent->mapToGlobal(QPoint(0, 0)), parent->size());
        const int left = area.x() + margin, right = area.x() + area.width() - margin;
        QPoint pos = menu->pos();
        if (pos.x() + menu->width() > right || pos.x() < left) {
            if (beside.x() + beside.width() + menu->width() <= right)
                pos.setX(beside.x() + beside.width());
            else if (beside.x() - menu->width() >= left)
                pos.setX(beside.x() - menu->width());
            else
                pos.setX(left);
        }
        if (pos.y() + menu->height() > area.y() + area.height() - margin)
            pos.setY(qMax(area.y(), area.y() + area.height() - margin - menu->height()));
        if (pos != menu->pos())
            menu->move(pos);
        return false;
    }

private:
    QPointer<QWidget> m_inWindow;
};

} // namespace

void keepMenuInWindow(QMenu *menu, QWidget *button, QWidget *bar, bool above)
{
    new MenuInWindow(menu, button, bar, above);
}

void keepSubmenuInWindow(QMenu *submenu, QWidget *inWindow)
{
    new SubmenuInWindow(submenu, inWindow);
}

int popupTop(const QWidget *bar, const QWidget *anchor)
{
    const QVariant edge = bar->property(kPopupEdgeProperty);
    int y = edge.isValid() ? edge.toInt() : bar->height();
    if (anchor && bar->isAncestorOf(anchor) && anchor->mapTo(bar, QPoint(0, anchor->height())).y() > y)
        y = bar->height();
    return bar->mapToGlobal(QPoint(0, y)).y() + space(gap::cluster);
}

void setPopupEdge(QWidget *bar, int y)
{
    bar->setProperty(kPopupEdgeProperty, y < 0 ? QVariant() : QVariant(y));
}

int popupWidth(const QWidget *window, int px)
{
    return qMax(1, qMin(space(px), window->width() - 2 * windowMargin(window)));
}

int rowHeight()
{
    return space(box::row);
}

// screens.js changesTable(): hh = 24 with its hairline at y + hh − 1, the
// table's border its first row. The header view sits inside that border.
int tableHeaderHeight()
{
    return space(box::row) - 1;
}

void fitStretchColumn(QTableView *table, int column, int others, int floor)
{
    table->setColumnWidth(column, qMax(space(floor), table->viewport()->width() - others));
}

void onHeaderResize(QTableView *table, std::function<void()> fit)
{
    QHeaderView *header = table->horizontalHeader();
    header->installEventFilter(new ResizeWatcher(header, std::move(fit)));
}

} // namespace ui
