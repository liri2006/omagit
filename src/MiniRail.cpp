#include "MiniRail.h"
#include "BadgeButton.h"
#include "ChangesModel.h"
#include "OmarchyTheme.h"
#include "UiHelpers.h"

#include <QButtonGroup>
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
constexpr int kRowHeight = 46, kTile = 32, kTileRadius = 5;
// The status badge and the row number sit on the tile's corners, each
// overhanging it a little so the extension inside stays readable.
constexpr int kBadgeSize = 13, kBadgeInsetX = 8, kBadgeRise = 5;
constexpr int kNumberHeight = 12, kNumberPadding = 6, kNumberRadius = 3;
constexpr int kNumberInsetX = 4, kNumberRise = 7;

QChar kindLetter(int kind)
{
    switch (kind) {
    case FileChange::Modified: return QLatin1Char('M');
    case FileChange::Added: return QLatin1Char('A');
    case FileChange::Deleted: return QLatin1Char('D');
    case FileChange::Renamed: return QLatin1Char('R');
    case FileChange::Copied: return QLatin1Char('C');
    case FileChange::TypeChanged: return QLatin1Char('T');
    case FileChange::Unmerged: return QLatin1Char('!');
    default: return QLatin1Char('?');
    }
}

// What a file is reduced to on a 32 px tile: its extension, or the first
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

// Paints one miniature: a rounded tile in the file's status colour with the
// extension inside and a status letter badge; dimmed when not checked.
class MiniDelegate : public QStyledItemDelegate
{
public:
    explicit MiniDelegate(QListView *view)
        : QStyledItemDelegate(view), m_view(view)
    {
    }

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return QSize(m_view->viewport()->width(), kRowHeight);
    }

    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index) const override
    {
        const OmarchyTheme *t = OmarchyTheme::instance();
        const QRect row(0, opt.rect.top(), m_view->viewport()->width(), opt.rect.height());
        const bool selected = opt.state & QStyle::State_Selected;
        const bool hover = opt.state & QStyle::State_MouseOver;

        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        if (selected)
            p->fillRect(row, t->selectedFill());
        else if (hover)
            p->fillRect(row, t->hoverFill());
        if (selected)
            p->fillRect(QRect(row.left(), row.top(), 2, row.height()), t->accent());

        const QVariant check = index.data(Qt::CheckStateRole);
        if (check.isValid() && check.toInt() != Qt::Checked)
            p->setOpacity(0.4);

        QColor status = index.data(Qt::ForegroundRole).value<QColor>();
        if (!status.isValid())
            status = t->text();

        QRect tile(0, 0, kTile, kTile);
        tile.moveCenter(row.center());
        QColor fill = status, edge = status;
        fill.setAlphaF(0.16);
        edge.setAlphaF(0.6);
        p->setPen(QPen(edge, 1));
        p->setBrush(fill);
        p->drawRoundedRect(QRectF(tile).adjusted(0.5, 0.5, -0.5, -0.5), kTileRadius, kTileRadius);

        QFont f = t->captionFont();
        f.setBold(true);
        p->setFont(f);
        p->setPen(selected ? t->accent() : status);
        p->drawText(tile, Qt::AlignCenter, tileLabel(index));

        const QRect badge(tile.right() - kBadgeInsetX, tile.top() - kBadgeRise, kBadgeSize, kBadgeSize);
        p->setPen(Qt::NoPen);
        p->setBrush(status);
        p->drawEllipse(badge);
        QFont bf = f;
        bf.setPixelSize(qMax(7, f.pixelSize() - 2));
        p->setFont(bf);
        p->setPen(t->window());
        p->drawText(badge, Qt::AlignCenter, QString(kindLetter(index.data(ChangesModel::KindRole).toInt())));

        // Row number, bottom-left, mirroring the status badge
        const QString number = QString::number(index.row() + 1);
        const int w = QFontMetrics(bf).horizontalAdvance(number) + kNumberPadding;
        const QRect numberRect(tile.left() - kNumberInsetX, tile.bottom() - kNumberRise, w, kNumberHeight);
        QColor numberEdge = t->text();
        numberEdge.setAlphaF(0.4);
        p->setPen(QPen(numberEdge, 1));
        p->setBrush(t->window());
        p->drawRoundedRect(QRectF(numberRect).adjusted(0.5, 0.5, -0.5, -0.5), kNumberRadius, kNumberRadius);
        p->setPen(selected ? t->accent() : t->mutedText());
        p->drawText(numberRect, Qt::AlignCenter, number);
        p->restore();
    }

private:
    QListView *m_view;
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
    // sit in any column; the check state lives in the Name column.
    const QModelIndex index = current.siblingAtColumn(ChangesModel::Name);
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
    const QString dir = info.path();
    if (!dir.isEmpty() && dir != QLatin1String("."))
        html += QStringLiteral("<br><span style=\"color:%1\">%2/</span>").arg(dim, dir.toHtmlEscaped());
    return html;
}

// ---------------------------------------------------------------- rail

// Every rail button spans the rail and wears a glyph for its whole label.
template <typename Button>
Button *MiniRail::addButton(uint glyph, const QString &fallback, const QString &tip)
{
    auto *b = ui::toolButton<Button>(ui::icon(glyph, fallback).trimmed(), tip);
    b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_glyphs.append({b, glyph, fallback});
    return b;
}

MiniRail::MiniRail(QWidget *parent)
    : QWidget(parent)
{
    setFixedWidth(kWidth);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    m_commitButton = addButton(ui::kCommit, QStringLiteral("C"), tr("Commit — pending changes (Ctrl+1)"));
    m_historyButton = addButton(ui::kHistory, QStringLiteral("H"), tr("History — commits of the repository (Ctrl+2)"));
    auto *modes = new QButtonGroup(this);
    modes->setExclusive(true);
    for (QToolButton *b : {m_commitButton, m_historyButton}) {
        b->setCheckable(true);
        modes->addButton(b);
        layout->addWidget(b);
    }
    m_commitButton->setChecked(true);
    connect(m_commitButton, &QToolButton::clicked, this, &MiniRail::commitModeRequested);
    connect(m_historyButton, &QToolButton::clicked, this, &MiniRail::historyModeRequested);

    m_hashLabel = new QLabel;
    m_hashLabel->setObjectName(QStringLiteral("dimLabel"));
    m_hashLabel->setAlignment(Qt::AlignCenter);
    m_hashLabel->hide();
    layout->addWidget(m_hashLabel);

    layout->addWidget(ui::hairline());

    m_list = new MiniRailList;
    connect(m_list, &QListView::doubleClicked, this, &MiniRail::activated);
    layout->addWidget(m_list, 1);

    layout->addWidget(ui::hairline());

    m_fetchButton = addButton<BadgeButton>(ui::kFetch, QStringLiteral("F"));
    m_pullButton = addButton<BadgeButton>(ui::kPull, QStringLiteral("↓"));
    m_pushButton = addButton<BadgeButton>(ui::kPush, QStringLiteral("↑"));
    m_mergeButton = addButton<BadgeButton>(ui::kMerge, QStringLiteral("M"));
    for (BadgeButton *b : {m_pullButton, m_pushButton, m_fetchButton, m_mergeButton}) // same order as the toolbar
        layout->addWidget(b);

    layout->addWidget(ui::hairline());

    m_refreshButton = addButton(ui::kRefresh, QStringLiteral("R"), tr("Re-read the repository (F5)"));
    connect(m_refreshButton, &QToolButton::clicked, this, &MiniRail::refreshRequested);
    layout->addSpacing(4);
    layout->addWidget(m_refreshButton);

    applyTheme();
}

void MiniRail::setSource(QAbstractItemModel *model, QItemSelectionModel *selection)
{
    if (m_list->model() != model) {
        m_list->setModel(model);
        // Only once a model is set: QListView ignores a column its model does
        // not have. Name is the column carrying the check state.
        m_list->setModelColumn(ChangesModel::Name);
    }
    if (m_list->selectionModel() != selection)
        m_list->setSelectionModel(selection);
}

void MiniRail::setCommitMode(bool commit)
{
    QSignalBlocker a(m_commitButton), b(m_historyButton);
    m_commitButton->setChecked(commit);
    m_historyButton->setChecked(!commit);
    if (commit)
        setCommitLabel(QString(), QString());
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
    m_hashLabel->setText(m_hashLabel->fontMetrics().elidedText(m_hash, Qt::ElideRight, kWidth - 2));
    m_hashLabel->setToolTip(m_hash.isEmpty() ? QString() : QStringLiteral("%1\n%2").arg(m_hash, m_hashTip));
    m_hashLabel->setVisible(!m_hash.isEmpty());
}

void MiniRail::applyTheme()
{
    m_hashLabel->setFont(OmarchyTheme::instance()->captionFont());
    updateHashLabel();
    for (const RailGlyph &g : std::as_const(m_glyphs))
        g.button->setText(ui::icon(g.code, g.fallback).trimmed());
    m_list->viewport()->update();
}
