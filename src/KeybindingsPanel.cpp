#include "KeybindingsPanel.h"
#include "OmarchyTheme.h"
#include "UiHelpers.h"

#include <QAbstractListModel>
#include <QEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLinearGradient>
#include <QListView>
#include <QMouseEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

using ui::space;

namespace {

// The design's panel (screens.js keybindingsPanel()), on the grid of Grid.h:
// a dialog's 16 of padding, the search on a 24 px row, a group gap to the
// rows. Sizes the design gives the panel alone: 800 by 500 at the most, the
// window's margins clear of its edges; 40 px rows a cluster (4) apart, the
// keys 16 into a row, the chevron's 16 px box 152 after the keys' start
// (further where the keys in the theme's font need it) and the action 4
// after the box.
constexpr int kWidth = 800, kHeight = 500;
constexpr int kRowHeight = 40, kKeysInset = 16, kChevronX = 152;

struct Binding {
    QString keys;
    QString action;
    QString context;
    std::function<void()> run;
};

} // namespace

// All bindings, with the rows the filter currently shows.
class BindingModel : public QAbstractListModel
{
public:
    enum Roles { KeysRole = Qt::UserRole, ActionRole, ContextRole };

    explicit BindingModel(QObject *parent) : QAbstractListModel(parent) {}

    void add(Binding binding)
    {
        m_all.append(std::move(binding));
        m_shown.append(m_all.size() - 1);
    }

    void filter(const QString &query)
    {
        const QStringList terms = query.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        beginResetModel();
        m_shown.clear();
        for (int i = 0; i < m_all.size(); ++i) {
            const Binding &b = m_all.at(i);
            // "ctrl+f" and "ctrl f" both find "CTRL + F".
            const QString haystack = b.keys + ' ' + QString(b.keys).remove(' ') + ' ' + b.action + ' ' + b.context;
            const bool matches = std::all_of(terms.cbegin(), terms.cend(), [&haystack](const QString &term) {
                return haystack.contains(term, Qt::CaseInsensitive);
            });
            if (matches)
                m_shown.append(i);
        }
        endResetModel();
    }

    const Binding &binding(int row) const { return m_all.at(m_shown.at(row)); }
    const QVector<Binding> &all() const { return m_all; }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : m_shown.size();
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() >= m_shown.size())
            return QVariant();
        const Binding &b = binding(index.row());
        switch (role) {
        case Qt::DisplayRole:
        case ActionRole:
            return b.action;
        case KeysRole:
            return b.keys;
        case ContextRole:
            return b.context;
        default:
            return QVariant();
        }
    }

private:
    QVector<Binding> m_all;
    QVector<int> m_shown;
};

namespace {

// One row: the keys in a fixed column, the chevron, the action, the context
// dim at the right. The cursor row gets the menu's highlight: a faint fill,
// accent text.
class BindingDelegate : public QStyledItemDelegate
{
public:
    BindingDelegate(QListView *view, int rowHeight, int rowGap, int inset, int keyColumn)
        : QStyledItemDelegate(view), m_view(view), m_rowHeight(rowHeight), m_rowGap(rowGap), m_inset(inset),
          m_keyColumn(keyColumn)
    {
        m_font = OmarchyTheme::instance()->headingFont();
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const override
    {
        return QSize(option.rect.width(), m_rowHeight + m_rowGap);
    }

    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const OmarchyTheme *theme = OmarchyTheme::instance();
        QRect r = option.rect;
        r.setHeight(m_rowHeight);
        const bool cursor = index.row() == m_view->currentIndex().row();
        p->save();
        if (cursor)
            p->fillRect(r, theme->hoverFill());
        p->setFont(m_font);
        const QFontMetrics fm(m_font);
        const QColor fg = cursor ? theme->accent() : theme->text();
        const int textFlags = Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine;
        // Keys
        int x = r.left() + m_inset;
        const int right = r.right() - m_inset;
        p->setPen(fg);
        const QString keys = index.data(BindingModel::KeysRole).toString();
        const int keyWidth = qMin(m_keyColumn, right - x);
        p->drawText(QRect(x, r.top(), keyWidth, r.height()), textFlags, fm.elidedText(keys, Qt::ElideRight, keyWidth));
        x += m_keyColumn;
        // The chevron, dim, centred by its ink in its box; the action 4 after it.
        {
            const QFont glyphFont = theme->uiFont();
            const QString chevron = ui::icon(ui::kChevronRight, QStringLiteral("›")).trimmed();
            const int box = space(ui::box::icon);
            p->setFont(glyphFont);
            p->setPen(theme->mutedText());
            p->drawText(QRectF(x, r.top(), box, r.height()).center() - ui::inkRect(glyphFont, chevron).center(), chevron);
            p->setFont(m_font);
            p->setPen(fg);
            x += box + space(ui::gap::icon);
        }
        // Context, dim, at the right end; the action gets what is left.
        const QString context = index.data(BindingModel::ContextRole).toString();
        int actionRight = right;
        if (!context.isEmpty()) {
            const int w = fm.horizontalAdvance(context);
            const int gap = fm.horizontalAdvance(QLatin1Char(' ')) * 3;
            if (x + gap + w <= right) {
                p->setPen(cursor ? theme->accent() : theme->fill(0.5));
                p->drawText(QRect(right - w, r.top(), w, r.height()), textFlags, context);
                actionRight = right - w - gap;
                p->setPen(fg);
            }
        }
        const QString action = index.data(BindingModel::ActionRole).toString();
        if (actionRight > x)
            p->drawText(QRect(x, r.top(), actionRight - x, r.height()), textFlags,
                        fm.elidedText(action, Qt::ElideRight, actionRight - x));
        p->restore();
    }

private:
    QListView *m_view;
    QFont m_font;
    int m_rowHeight, m_rowGap, m_inset, m_keyColumn;
};

// The rows, without a scrollbar: the card fades the rows out at the edges
// that hide more, the way the shell's menu does. The fade's strength tracks
// how much is still hidden past each edge, so the row peeking at the fold
// is the dimmed one and a fully scrolled edge carries no fade at all.
class FadeList : public QListView
{
public:
    explicit FadeList(QWidget *parent = nullptr) : QListView(parent) {}

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QListView::paintEvent(event);
        QScrollBar *bar = verticalScrollBar();
        if (bar->maximum() <= 0)
            return;
        QPainter p(viewport());
        const QColor bg = OmarchyTheme::instance()->window();
        const int h = qMin(space(28), viewport()->height() / 2);
        if (h <= 0)
            return;
        const auto fade = [&p, &bg, h, this](int top, qreal strength, bool down) {
            if (strength <= 0)
                return;
            QColor solid = bg;
            solid.setAlphaF(qMin<qreal>(1, strength));
            QColor clear = bg;
            clear.setAlpha(0);
            QLinearGradient g(0, top, 0, top + h);
            g.setColorAt(0, down ? clear : solid);
            g.setColorAt(1, down ? solid : clear);
            p.fillRect(QRect(0, top, viewport()->width(), h), g);
        };
        fade(0, qreal(bar->value()) / h, false);
        fade(viewport()->height() - h, qreal(bar->maximum() - bar->value()) / h, true);
    }
};

} // namespace

KeybindingsPanel::KeybindingsPanel(QWidget *parent)
    : QDialog(parent, Qt::Popup)
{
    const OmarchyTheme *theme = OmarchyTheme::instance();
    setObjectName(QStringLiteral("keybindingsPanel"));
    setWindowTitle(tr("Omagit keybindings"));
    setAttribute(Qt::WA_DeleteOnClose);

    // The design's metrics (see the top of the file), scaled with the text size.
    const QFont heading = theme->headingFont();
    m_padding = space(ui::pad::dialog);
    m_headerHeight = space(ui::box::row);
    m_spacing = space(ui::gap::group);
    m_rowHeight = space(kRowHeight);
    m_rowGap = space(ui::gap::cluster);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(m_padding, m_padding, m_padding, m_padding);
    layout->setSpacing(m_spacing);

    // The header is the search: the prompt shows until something is typed.
    m_search = new QLineEdit;
    m_search->setObjectName(QStringLiteral("keybindingsSearch"));
    m_search->setPlaceholderText(tr("Omagit keybindings…"));
    m_search->setAccessibleName(tr("Search keybindings"));
    m_search->setFont(heading);
    m_search->setFixedHeight(m_headerHeight);
    m_search->setFrame(false);
    QPalette pal = m_search->palette();
    pal.setColor(QPalette::PlaceholderText, theme->fill(0.58));
    m_search->setPalette(pal);
    layout->addWidget(m_search);

    m_model = new BindingModel(this);
    m_list = new FadeList;
    m_list->setObjectName(QStringLiteral("keybindingsList"));
    m_list->setModel(m_model);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setFocusPolicy(Qt::NoFocus);
    m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setUniformItemSizes(true);
    m_list->setMouseTracking(true);
    m_list->viewport()->setAttribute(Qt::WA_Hover);
    layout->addWidget(m_list, 1);

    // The cursor follows the mouse, a click is Enter. Mouse moves are watched
    // directly rather than through QListView::entered: the view re-emits
    // entered for the row under a resting pointer after every scroll, which
    // would snap the cursor back there when Down or Up scroll the rows.
    m_list->viewport()->installEventFilter(this);
    m_search->installEventFilter(this); // after m_list exists: the filter looks at it
    connect(m_list, &QListView::clicked, this, &KeybindingsPanel::activate);
    connect(m_search, &QLineEdit::textChanged, this, &KeybindingsPanel::applyFilter);
    connect(m_search, &QLineEdit::returnPressed, this, [this] { activate(m_list->currentIndex()); });
}

KeybindingsPanel::~KeybindingsPanel() = default;

void KeybindingsPanel::add(const QString &keys, const QString &action, const QString &context,
                           std::function<void()> run)
{
    m_model->add({keys, action, context, std::move(run)});
}

void KeybindingsPanel::popup()
{
    QWidget *host = parentWidget();
    const int width = qMin(space(kWidth), host ? host->width() - 2 * ui::windowMargin(host) : space(kWidth));
    // The keys column: the design's 152, or as wide as the longest keys need
    // and an item gap, but never most of the row.
    const QFontMetrics fm(OmarchyTheme::instance()->headingFont());
    int longest = 0;
    for (const Binding &b : m_model->all())
        longest = qMax(longest, fm.horizontalAdvance(b.keys));
    const int inset = space(kKeysInset);
    const int keyColumn = qMin(qMax(space(kChevronX), longest + space(ui::gap::item)),
                               (width - 2 * m_padding - 2 * inset) * 3 / 5);
    m_list->setItemDelegate(new BindingDelegate(m_list, m_rowHeight, m_rowGap, inset, keyColumn));

    resize(width, height());
    fitHeight();
    if (host)
        move(host->mapToGlobal(host->rect().center()) - rect().center());
    m_top = y();
    if (m_model->rowCount() > 0)
        m_list->setCurrentIndex(m_model->index(0));
    show();
    m_search->setFocus();
}

void KeybindingsPanel::fitHeight()
{
    QWidget *host = parentWidget();
    const int rows = m_model->rowCount();
    const int pitch = m_rowHeight + m_rowGap; // each row's size hint includes its gap
    int cap = space(kHeight);
    if (host)
        cap = qMin(cap, host->height() - 2 * ui::windowMargin(host));
    const int chrome = 2 * m_padding + m_headerHeight;
    int rowsHeight = rows * pitch;
    const int available = cap - chrome - m_spacing;
    if (rows > 0 && rowsHeight > available) {
        // Like the shell's foldedListHeight: when the rows do not all fit, the
        // card ends mid-row, whole rows and then a peek of the next one, so a
        // clipped row tells the eye there is more below the fold.
        const int full = qMax(1, (available - rowPeek()) / pitch);
        rowsHeight = qMax(full * pitch + rowPeek(), qMin(available, m_rowHeight));
    }
    const int height = qMin(cap, chrome + (rows > 0 ? m_spacing + rowsHeight : 0));
    m_list->setVisible(rows > 0);
    setFixedHeight(height);
    if (m_top >= 0)
        move(x(), m_top);
}

void KeybindingsPanel::applyFilter(const QString &query)
{
    m_model->filter(query);
    if (m_model->rowCount() > 0)
        m_list->setCurrentIndex(m_model->index(0));
    m_list->scrollToTop();
    fitHeight();
}

void KeybindingsPanel::moveCursor(int delta, bool absolute, bool wrap)
{
    const int count = m_model->rowCount();
    if (count == 0)
        return;
    int row = absolute ? delta : m_list->currentIndex().row() + delta;
    if (wrap)
        row = ((row % count) + count) % count; // Down past the last row wraps to the first, Up past the first to the last
    else
        row = qBound(0, row, count - 1);
    m_list->setCurrentIndex(m_model->index(row));
    revealRow(row);
}

int KeybindingsPanel::rowPeek() const
{
    return qRound(m_rowHeight * 0.55);
}

// The shell's revealCursor: the cursor row is shown whole, and past it, in
// either direction that has more rows, the next row keeps peeking in under
// the fade; scrollTo would park the cursor row itself against the edge.
void KeybindingsPanel::revealRow(int row)
{
    QScrollBar *bar = m_list->verticalScrollBar();
    const int viewHeight = m_list->viewport()->height();
    const int top = row * (m_rowHeight + m_rowGap);
    const int reach = rowPeek() + m_rowGap;
    int value = bar->value();
    // Contain first, then make room for the peeking neighbours.
    if (top < value)
        value = top;
    else if (top + m_rowHeight > value + viewHeight)
        value = top + m_rowHeight - viewHeight;
    if (row < m_model->rowCount() - 1)
        value = qMax(value, top + m_rowHeight + reach - viewHeight);
    if (row > 0)
        value = qMin(value, top - reach);
    bar->setValue(qBound(bar->minimum(), value, bar->maximum()));
}

void KeybindingsPanel::activate(const QModelIndex &index)
{
    std::function<void()> run;
    if (index.isValid() && index.row() < m_model->rowCount())
        run = m_model->binding(index.row()).run;
    QWidget *host = parentWidget();
    close();
    // After the popup let go of the mouse and keyboard, so a menu the
    // action opens gets them.
    if (run)
        QTimer::singleShot(0, host ? static_cast<QObject *>(host) : qApp, run);
}

bool KeybindingsPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_list->viewport() && event->type() == QEvent::MouseMove) {
        const QModelIndex index = m_list->indexAt(static_cast<QMouseEvent *>(event)->pos());
        if (index.isValid() && index != m_list->currentIndex())
            m_list->setCurrentIndex(index);
        return false;
    }
    if (watched == m_search && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        const int page = qMax(1, m_list->viewport()->height() / (m_rowHeight + m_rowGap));
        switch (key->key()) {
        case Qt::Key_Down:
            moveCursor(1, false, true);
            return true;
        case Qt::Key_Up:
            moveCursor(-1, false, true);
            return true;
        case Qt::Key_PageDown:
            moveCursor(page, false);
            return true;
        case Qt::Key_PageUp:
            moveCursor(-page, false);
            return true;
        case Qt::Key_Home:
            if (key->modifiers() & Qt::ControlModifier) {
                moveCursor(0, true);
                return true;
            }
            break;
        case Qt::Key_End:
            if (key->modifiers() & Qt::ControlModifier) {
                moveCursor(m_model->rowCount() - 1, true);
                return true;
            }
            break;
        default:
            break;
        }
    }
    return QDialog::eventFilter(watched, event);
}
