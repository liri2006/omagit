#include "MiniRail.h"
#include "ChangesModel.h"
#include "OmarchyTheme.h"
#include "UiHelpers.h"

#include <QCoreApplication>
#include <QEvent>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
// The design's rail (screens.js miniRail()), on the grid of Grid.h: 40 px
// square tiles a cluster (4) apart, filling the rail's width; the commit tile
// at the bottom, the separator 8 over it and 8 under Refresh.
// A tile's extension label and its row number (bottom left, 4 px in, its
// centre 6 px over the tile's bottom): text sizes, and the label's optical
// drop.
constexpr int kLabelText = 10, kLabelDrop = 2;
constexpr int kNumberText = 8, kNumberX = 4, kNumberBox = 12;
constexpr qreal kUncheckedLabel = 0.45; // the label of a tile not in the commit
// A tile's corner badge — a miniature's status letter, the commit tile's
// count: a 12 px square 1 px inside the tile's top right corner, widened
// leftwards for a longer text (2 either side of it), its text 8 px bold in
// the window colour.
constexpr int kBadgeInset = 1, kBadgeText = 8, kBadgeTextPad = 2;
constexpr qreal kUncheckedBadge = 0.5; // the badge's fill on an unchecked tile
// The commit glyph's text size.
constexpr int kCommitGlyph = 16;

QFont boldFont(int px)
{
    QFont f = OmarchyTheme::instance()->uiFont();
    f.setBold(true);
    f.setPixelSize(ui::fontPx(px));
    return f;
}

// The corner badge of `tile` for `text`.
QRect cornerBadge(const QRect &tile, const QString &text)
{
    const int w = qMax(ui::space(ui::box::badge),
                       QFontMetrics(boldFont(kBadgeText)).horizontalAdvance(text) + 2 * ui::space(kBadgeTextPad));
    return QRect(tile.right() + 1 - ui::space(kBadgeInset) - w, tile.top() + ui::space(kBadgeInset), w,
                 ui::space(ui::box::badge));
}

void paintCornerBadge(QPainter *p, const QRect &badge, const QColor &fill, const QString &text)
{
    p->fillRect(badge, fill);
    p->setFont(boldFont(kBadgeText));
    p->setPen(OmarchyTheme::instance()->window());
    p->drawText(badge, Qt::AlignCenter, text);
}

// What a file is reduced to on a 40 px tile: its extension, or the first
// letters of its name when it has none (Makefile, LICENSE, ...).
QString tileLabel(const QModelIndex &index)
{
    QString ext = index.siblingAtColumn(ChangesModel::Extension).data().toString();
    if (ext.startsWith(QLatin1Char('.')))
        ext.remove(0, 1);
    QString label = ext.isEmpty() ? QFileInfo(index.data(ChangesModel::PathRole).toString()).fileName() : ext;
    if (label.size() > 4)
        label = label.left(3) + QStringLiteral("…");
    return label;
}

// Paints one miniature (screens.js miniRail()): a square tile in the chrome's
// fills with the extension inside in the file's status colour, its status
// letter on a square in the corner and its row number at the bottom left.
// A file left out of the commit wears a faded label and a half-strength badge.
class MiniDelegate : public QStyledItemDelegate
{
public:
    explicit MiniDelegate(QListView *view)
        : QStyledItemDelegate(view), m_view(view)
    {
    }

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return QSize(m_view->viewport()->width(), ui::space(ui::box::tile + ui::gap::cluster));
    }

    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index) const override
    {
        const OmarchyTheme *t = OmarchyTheme::instance();
        const bool selected = opt.state & QStyle::State_Selected;
        const bool hover = opt.state & QStyle::State_MouseOver;
        const QVariant check = index.data(Qt::CheckStateRole);
        const bool unchecked = check.isValid() && check.toInt() != Qt::Checked;
        QColor status = index.data(Qt::ForegroundRole).value<QColor>();
        if (!status.isValid())
            status = t->text();

        p->save();
        const QRect tile(0, opt.rect.top(), ui::space(ui::box::tile), ui::space(ui::box::tile));
        p->fillRect(tile, selected ? t->selectedFill() : hover ? t->hoverFill() : t->normalFill());
        p->setPen(QPen(selected ? t->accent() : t->normalBorder(), 1));
        p->setBrush(Qt::NoBrush);
        p->drawRect(tile.adjusted(0, 0, -1, -1));

        p->setFont(boldFont(kLabelText));
        p->setPen(selected ? t->accent() : status);
        p->setOpacity(unchecked ? kUncheckedLabel : 1.0);
        p->drawText(tile.translated(0, ui::space(kLabelDrop)), Qt::AlignCenter, tileLabel(index));
        p->setOpacity(1.0);

        // The letter the tree and compact presentations put in their status
        // pills, so one list never spells a status differently from another.
        const auto kind = FileChange::Kind(index.data(ChangesModel::KindRole).toInt());
        const QString letter(ChangesModel::statusLetter(kind));
        QColor fill = status;
        if (unchecked)
            fill.setAlphaF(kUncheckedBadge);
        paintCornerBadge(p, cornerBadge(tile, letter), fill, letter);

        QFont number = t->uiFont();
        number.setPixelSize(ui::fontPx(kNumberText));
        p->setFont(number);
        p->setPen(t->mutedText());
        const int numberBox = ui::space(kNumberBox);
        p->drawText(QRect(tile.left() + ui::space(kNumberX), tile.bottom() + 1 - numberBox, tile.width(), numberBox),
                    Qt::AlignLeft | Qt::AlignVCenter, QString::number(index.row() + 1));
        p->restore();
    }

private:
    QListView *m_view;
};

// The commit tile at the bottom of the rail: an accent square with the commit
// glyph, and the number of checked files in the corner badge the miniatures'
// status letters wear. The widget is the square, the rail's width.
class CommitTile : public QToolButton
{
public:
    explicit CommitTile(QWidget *parent = nullptr)
        : QToolButton(parent)
    {
        setObjectName(QStringLiteral("commitTile"));
        setAccessibleName(QCoreApplication::translate("MiniRail", "Commit"));
        setToolTip(QCoreApplication::translate("MiniRail", "Commit the checked files (Ctrl+Enter)"));
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover);
        applyMetrics();
    }

    void applyMetrics()
    {
        setFixedSize(MiniRail::railWidth(), ui::space(ui::box::tile));
        update();
    }

    // The painted square, in the widget's coordinates.
    QRect square() const
    {
        const int side = ui::space(ui::box::tile);
        return QRect((width() - side) / 2, height() - side, side, side);
    }

    // The count's badge, in the widget's coordinates. Null when nothing is
    // checked and no badge is painted.
    QRect badge() const
    {
        if (m_count <= 0)
            return QRect();
        return cornerBadge(square(), QString::number(m_count));
    }

    void setCount(int count)
    {
        if (m_count == count)
            return;
        m_count = count;
        update();
    }

    void setActive(bool on)
    {
        if (m_active == on)
            return;
        m_active = on;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const OmarchyTheme *t = OmarchyTheme::instance();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRect sq = square();
        // The accent itself at a low alpha, not the theme's foreground fills:
        // the tile is the rail's one call to action. Square, like the
        // miniatures.
        QColor fill = t->accent();
        fill.setAlphaF(isDown() ? 0.22 : (m_active || underMouse()) ? 0.18 : 0.08);
        p.fillRect(sq, fill);
        p.setPen(QPen(t->accent(), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(sq.adjusted(0, 0, -1, -1));

        QFont glyph = t->uiFont();
        glyph.setPixelSize(ui::fontPx(kCommitGlyph));
        p.setFont(glyph);
        p.setPen(t->accent());
        // Centred by its ink, as the kit's buttons do: a Nerd Font glyph's
        // ink need not sit in the middle of the advance the style centres.
        const QString commit = ui::icon(ui::kCommit, QStringLiteral("C")).trimmed();
        p.drawText(QRectF(sq).center() - ui::inkRect(glyph, commit).center(), commit);

        if (m_count > 0)
            paintCornerBadge(&p, badge(), t->accent(), QString::number(m_count));
    }

private:
    int m_count = 0;
    bool m_active = false;
};

} // namespace

// ---------------------------------------------------------------- list

MiniRailList::MiniRailList(QWidget *parent)
    : QListView(parent)
{
    setItemDelegate(new MiniDelegate(this));
    setFrameShape(QFrame::NoFrame);
    setSelectionMode(SingleSelection);
    setSelectionBehavior(SelectRows);
    setUniformItemSizes(true);
    setSpacing(0);
    setMouseTracking(true);
    viewport()->setAttribute(Qt::WA_Hover);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollMode(ScrollPerPixel);
    // The tiles fill the rail's width, which a scroll bar would take from
    // them: the list scrolls by the wheel and the keyboard instead.
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setEditTriggers(NoEditTriggers);
    setFocusPolicy(Qt::StrongFocus);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, &MiniRailList::hideTip);
}

bool MiniRailList::viewportEvent(QEvent *event)
{
    if (event->type() == QEvent::ToolTip)
        return true; // the delayed default tip is replaced by showTip()
    return QListView::viewportEvent(event);
}

void MiniRailList::mouseMoveEvent(QMouseEvent *event)
{
    showTip(indexAt(event->pos()));
    QListView::mouseMoveEvent(event);
}

void MiniRailList::leaveEvent(QEvent *event)
{
    hideTip();
    QListView::leaveEvent(event);
}

void MiniRailList::mousePressEvent(QMouseEvent *event)
{
    const QModelIndex index = indexAt(event->pos());
    if (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ControlModifier) && index.isValid()) {
        toggleChecked(index);
        // The list takes the keyboard as a plain click would, so Space goes on
        // from here — on the current file, which the Ctrl+click left alone.
        setFocus(Qt::MouseFocusReason);
        return;
    }
    QListView::mousePressEvent(event);
}

void MiniRailList::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && currentIndex().isValid()) {
        toggleChecked(currentIndex());
        return;
    }
    QListView::keyPressEvent(event);
}

void MiniRailList::toggleChecked(const QModelIndex &current)
{
    // The selection model is the changes table's, whose current index may
    // sit in any column; the check state lives in the first one.
    const QModelIndex index = current.siblingAtColumn(ChangesModel::Check);
    const QVariant check = index.data(Qt::CheckStateRole);
    if (!check.isValid())
        return;
    model()->setData(index, check.toInt() == Qt::Checked ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
    if (m_tipIndex == index) {
        m_tipIndex = QModelIndex();
        showTip(index);
    }
}

// A tooltip of our own instead of QToolTip: Qt word-wraps rich-text tips at a
// fixed character count, whereas this one stays on a single line as long as it
// fits between the rail and the window's right edge, and wraps only then.
void MiniRailList::showTip(const QModelIndex &index)
{
    if (index == m_tipIndex)
        return;
    m_tipIndex = index;
    if (!index.isValid()) {
        hideTip();
        return;
    }
    if (!m_tip) {
        m_tip = new QLabel(window(), Qt::ToolTip | Qt::FramelessWindowHint);
        m_tip->setObjectName(QStringLiteral("railTip"));
        m_tip->setTextFormat(Qt::RichText);
        m_tip->setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    // The frame and padding come from the QLabel#railTip rule of the
    // application stylesheet, which follows the theme on its own.
    m_tip->setText(tipText(index));

    const QRect rect = visualRect(index);
    const QPoint pos = viewport()->mapToGlobal(QPoint(viewport()->width() + 4, rect.top()));
    const int windowRight = window()->mapToGlobal(window()->rect().topRight()).x();
    const int available = windowRight - pos.x() - 8;

    m_tip->setWordWrap(false);
    m_tip->setMinimumSize(0, 0);
    m_tip->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    QSize size = m_tip->sizeHint();
    if (size.width() > available && available > 120) {
        m_tip->setWordWrap(true);
        m_tip->setFixedWidth(available);
        size = QSize(available, m_tip->heightForWidth(available));
    }
    m_tip->resize(size);
    m_tip->move(pos);
    m_tip->show();
}

void MiniRailList::hideTip()
{
    m_tipIndex = QModelIndex();
    if (m_tip)
        m_tip->hide();
}

QString MiniRailList::tipText(const QModelIndex &index) const
{
    const QString dim = OmarchyTheme::instance()->mutedText().name();
    const QFileInfo info(index.data(ChangesModel::PathRole).toString());
    QString html = QStringLiteral("<b>%1</b>").arg(info.fileName().toHtmlEscaped());
    FileChange change;
    change.kind = FileChange::Kind(index.data(ChangesModel::KindRole).toInt());
    html += QStringLiteral("&nbsp;&nbsp;") + ChangesModel::statusHtml(change);
    const QString dir = info.path();
    if (!dir.isEmpty() && dir != QLatin1String("."))
        html += QStringLiteral("<br><span style=\"color:%1\">%2/</span>").arg(dim, dir.toHtmlEscaped());
    return html;
}

// ---------------------------------------------------------------- rail

// Every rail button spans the rail and wears a glyph for its whole label.
QToolButton *MiniRail::addButton(uint glyph, const QString &fallback, const QString &tip)
{
    auto *b = ui::toolButton<ui::GlyphButton>(ui::icon(glyph, fallback).trimmed(), tip);
    b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_glyphs.append({b, glyph, fallback});
    return b;
}

int MiniRail::railWidth()
{
    return ui::space(ui::box::tile);
}

MiniRail::MiniRail(QWidget *parent)
    : QWidget(parent)
{
    setFixedWidth(railWidth());
    auto *layout = new QVBoxLayout(this);
    m_layout = layout;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(ui::space(ui::gap::cluster)); // applyTheme() keeps it on the text size

    // The hash of the commit whose files these are, with a rule under it; both
    // only while there is one (history mode).
    m_hashLabel = new QLabel;
    m_hashLabel->setObjectName(QStringLiteral("dimLabel"));
    m_hashLabel->setAlignment(Qt::AlignCenter);
    m_hashLabel->hide();
    layout->addWidget(m_hashLabel);
    m_hashRule = ui::hairline();
    m_hashRule->hide();
    layout->addWidget(m_hashRule);

    m_list = new MiniRailList;
    connect(m_list, &QListView::doubleClicked, this, &MiniRail::activated);
    layout->addWidget(m_list, 1);

    layout->addWidget(ui::hairline());

    m_refreshButton = addButton(ui::kRefresh, QStringLiteral("R"), tr("Re-read the repository (F5)"));
    connect(m_refreshButton, &QToolButton::clicked, this, &MiniRail::refreshRequested);
    // Refresh an item gap under the list's separator: the layout's spacing
    // and this spacer together.
    m_refreshGap = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Fixed);
    layout->addItem(m_refreshGap);
    layout->addWidget(m_refreshButton);

    // The commit tile under a separator, the design's 8 px from Refresh above
    // it and from the tile's square below it. The section is one widget so it
    // comes and goes whole, its gaps included, and its own layout keeps the
    // rail's spacing out of those two gaps.
    m_commitSection = new QWidget;
    auto *section = new QVBoxLayout(m_commitSection);
    section->setContentsMargins(0, 0, 0, 0);
    section->setSpacing(0);
    m_ruleGap = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Fixed);
    section->addItem(m_ruleGap);
    section->addWidget(ui::hairline());
    m_tileGap = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Fixed);
    section->addItem(m_tileGap);
    auto *tile = new CommitTile;
    m_commitTile = tile;
    connect(tile, &QToolButton::clicked, this, &MiniRail::commitRequested);
    section->addWidget(tile);
    layout->addWidget(m_commitSection);

    applyTheme();
}

QToolButton *MiniRail::commitTile() const
{
    return m_commitTile;
}

QRect MiniRail::commitBadgeRect() const
{
    return static_cast<CommitTile *>(m_commitTile)->badge();
}

void MiniRail::setCommitTileVisible(bool on)
{
    m_commitSection->setVisible(on);
}

void MiniRail::setCommitTileActive(bool on)
{
    static_cast<CommitTile *>(m_commitTile)->setActive(on);
}

// The gaps around the separators in scaled pixels. Those above them give back
// the rail's own spacing, which the layout puts between two widgets, so they
// measure to what is drawn.
void MiniRail::applyCommitMetrics()
{
    m_layout->setSpacing(ui::space(ui::gap::cluster));
    const int item = ui::space(ui::gap::item);
    m_refreshGap->changeSize(0, qMax(0, item - m_layout->spacing()), QSizePolicy::Minimum, QSizePolicy::Fixed);
    m_ruleGap->changeSize(0, qMax(0, item - m_layout->spacing()), QSizePolicy::Minimum, QSizePolicy::Fixed);
    m_tileGap->changeSize(0, item, QSizePolicy::Minimum, QSizePolicy::Fixed);
    m_layout->invalidate();
    static_cast<CommitTile *>(m_commitTile)->applyMetrics();
    m_commitSection->layout()->invalidate();
}

// Reads the check marks and nothing else: no selection, no sorting and no
// model of its own, so the badge can never disagree with the list.
void MiniRail::countChecked()
{
    int checked = 0;
    if (QAbstractItemModel *model = m_badgeSource.data()) {
        for (int row = 0, rows = model->rowCount(); row < rows; ++row)
            if (model->index(row, ChangesModel::Check).data(Qt::CheckStateRole).toInt() == Qt::Checked)
                ++checked;
    }
    m_checked = checked;
    static_cast<CommitTile *>(m_commitTile)->setCount(checked);
}

void MiniRail::setSource(QAbstractItemModel *model, QItemSelectionModel *selection)
{
    // The badge's own connections follow the source; the list's are its own.
    if (m_badgeSource != model) {
        for (const QMetaObject::Connection &c : std::as_const(m_badgeConnections))
            disconnect(c);
        m_badgeConnections.clear();
        m_badgeSource = model;
        if (model) {
            m_badgeConnections << connect(model, &QAbstractItemModel::dataChanged, this, &MiniRail::countChecked)
                               << connect(model, &QAbstractItemModel::rowsInserted, this, &MiniRail::countChecked)
                               << connect(model, &QAbstractItemModel::rowsRemoved, this, &MiniRail::countChecked)
                               << connect(model, &QAbstractItemModel::modelReset, this, &MiniRail::countChecked)
                               << connect(model, &QAbstractItemModel::layoutChanged, this, &MiniRail::countChecked);
        }
    }
    countChecked();
    if (m_list->model() != model) {
        m_list->setModel(model);
        // Only once a model is set: QListView ignores a column its model does
        // not have. Check is the column carrying the check state.
        m_list->setModelColumn(ChangesModel::Check);
    }
    if (m_list->selectionModel() != selection)
        m_list->setSelectionModel(selection);
}

void MiniRail::setCommitLabel(const QString &hash, const QString &tip)
{
    m_hash = hash;
    m_hashTip = tip;
    updateHashLabel();
}

// The rail is narrower than a 7-character hash in the UI font: elide it and
// keep the whole hash (with the subject) in the tooltip.
void MiniRail::updateHashLabel()
{
    m_hashLabel->ensurePolished();
    m_hashLabel->setText(m_hashLabel->fontMetrics().elidedText(m_hash, Qt::ElideRight, railWidth() - 2));
    m_hashLabel->setToolTip(m_hash.isEmpty() ? QString() : QStringLiteral("%1\n%2").arg(m_hash, m_hashTip));
    m_hashLabel->setVisible(!m_hash.isEmpty());
    m_hashRule->setVisible(!m_hash.isEmpty());
}

void MiniRail::applyTheme()
{
    setFixedWidth(railWidth());
    applyCommitMetrics();
    m_hashLabel->setFont(OmarchyTheme::instance()->captionFont());
    updateHashLabel();
    for (const RailGlyph &g : std::as_const(m_glyphs))
        g.button->setText(ui::icon(g.code, g.fallback).trimmed());
    m_list->viewport()->update();
    m_commitTile->update(); // its glyph and badge are looked up at paint time
}
