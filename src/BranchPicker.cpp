#include "BranchPicker.h"
#include "OmarchyTheme.h"
#include "UiHelpers.h"

#include <QKeyEvent>
#include <QStyleOptionToolButton>
#include <QStylePainter>

namespace {
// The design's pickers (kit.js button()): the merge view's 36 px high with a
// big control's 12 of padding (screens.js mergeDialog(), PICKER), the New
// branch card's a 28 px control with a control's 8 (newBranchCard()).
constexpr int kBigHeight = 36;
// What a picker asks for sideways, and the least it gets.
constexpr int kHintWidth = 260, kMinWidth = 120;

uint glyphFor(BranchPicker::Kind kind)
{
    switch (kind) {
    case BranchPicker::Kind::Tag: return ui::kTagOutline;
    case BranchPicker::Kind::Commit: return ui::kCommit;
    case BranchPicker::Kind::Key: return ui::kKey;
    case BranchPicker::Kind::Branch: break;
    }
    return ui::kBranch;
}
} // namespace

BranchPicker::BranchPicker(Size size, QWidget *parent)
    : QToolButton(parent), m_size(size)
{
    setObjectName(QStringLiteral("branchPicker"));
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::TabFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void BranchPicker::setBranch(const QString &name, Kind kind)
{
    m_name = name;
    m_kind = kind;
    setAccessibleName(name);
    updateGeometry();
    update();
}

QSize BranchPicker::sizeHint() const
{
    return QSize(ui::space(kHintWidth), ui::space(m_size == Size::Big ? kBigHeight : ui::box::control));
}

QSize BranchPicker::minimumSizeHint() const
{
    return QSize(ui::space(kMinWidth), sizeHint().height());
}

// Like a combo box: Enter goes to the surface's default action, the list
// opens on Space, Down or Alt+Down.
void BranchPicker::keyPressEvent(QKeyEvent *e)
{
    switch (e->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        e->ignore();
        return;
    case Qt::Key_Down:
    case Qt::Key_F4:
        click();
        e->accept();
        return;
    default:
        QToolButton::keyPressEvent(e);
    }
}

void BranchPicker::keyReleaseEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
        e->ignore();
        return;
    }
    QToolButton::keyReleaseEvent(e);
}

// The kit's button layout (kit.js button()): [pad][glyph box 16][4][name][4]
// [chevron box 12][pad], the glyphs centred by their ink. The big picker's
// name is in the title font, the control-sized one's in the control's own
// font, bold.
void BranchPicker::paintEvent(QPaintEvent *)
{
    const OmarchyTheme *t = OmarchyTheme::instance();
    QStylePainter p(this);
    QStyleOptionToolButton opt;
    initStyleOption(&opt);
    opt.text.clear();
    opt.icon = QIcon();
    p.drawComplexControl(QStyle::CC_ToolButton, opt); // the stylesheet's box and states
    const bool on = isEnabled();
    const QColor dim = on ? t->mutedText() : t->fill(0.45);
    const bool big = m_size == Size::Big;
    const int pad = ui::space(big ? ui::pad::big : ui::pad::control), h = height();
    const auto centred = [&p](const QFont &font, const QString &glyph, const QRectF &box) {
        p.setFont(font);
        p.drawText(box.center() - ui::inkRect(font, glyph).center(), glyph);
    };
    p.setPen(dim);
    const QString mark = t->glyph(glyphFor(m_kind));
    int x = pad;
    if (!mark.isEmpty()) {
        centred(t->uiFont(), mark, QRectF(x, 0, ui::space(ui::box::icon), h));
        x += ui::space(ui::box::icon) + ui::space(ui::gap::icon);
    }
    QFont small = t->uiFont();
    small.setPixelSize(qMax(1, qRound(small.pixelSize() * ui::box::chevron / double(ui::box::icon))));
    const QString chevron = t->glyph(ui::kChevron);
    const int chevronLeft = width() - pad - ui::space(ui::box::chevron);
    centred(small, chevron.isEmpty() ? QStringLiteral("▾") : chevron, QRectF(chevronLeft, 0, ui::space(ui::box::chevron), h));
    QFont name = big ? t->titleFont() : t->uiFont();
    name.setBold(true);
    p.setFont(name);
    const QRect nameRect(x, 0, chevronLeft - ui::space(ui::gap::icon) - x, h);
    if (m_name.isEmpty()) {
        p.setPen(dim);
        p.drawText(nameRect, Qt::AlignVCenter, tr("No branch"));
        return;
    }
    p.setPen(on ? t->accent() : t->fill(0.45));
    p.drawText(nameRect, Qt::AlignVCenter, p.fontMetrics().elidedText(m_name, Qt::ElideMiddle, nameRect.width()));
}
