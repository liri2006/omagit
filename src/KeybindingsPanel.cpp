#include "KeybindingsPanel.h"
#include "OmarchyTheme.h"

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

namespace {

// The shell's spacing scale: its pixel values are meant for a 12 px base
// font and grow with it (Style.space() in omarchy-shell).
int space(int px)
{
    return qMax(1, qRound(px * OmarchyTheme::instance()->fontBase() / 12.0));
}

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

// One row: the keys in a fixed column, "→ action", the context dim at the
// right. The cursor row gets the menu's highlight: a faint fill, accent text.
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
        const QString action = QStringLiteral("→ ") + index.data(BindingModel::ActionRole).toString();
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
// that hide more, the way the shell's menu does.
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
        const QColor clear(bg.red(), bg.green(), bg.blue(), 0);
        const int h = qMin(space(28), viewport()->height() / 2);
        if (bar->value() > 0) {
            QLinearGradient g(0, 0, 0, h);
            g.setColorAt(0, bg);
            g.setColorAt(1, clear);
            p.fillRect(QRect(0, 0, viewport()->width(), h), g);
        }
        if (bar->value() < bar->maximum()) {
            const int top = viewport()->height() - h;
            QLinearGradient g(0, top, 0, top + h);
            g.setColorAt(0, clear);
            g.setColorAt(1, bg);
            p.fillRect(QRect(0, top, viewport()->width(), h), g);
        }
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

    // The shell's menu metrics: 18 px padding, a 34 px header, 50 px rows
    // 3 px apart, all at the 12 px base and scaled with it.
    const QFont heading = theme->headingFont();
    const int headingPx = heading.pixelSize();
    m_padding = space(18);
    m_headerHeight = qMax(space(34), headingPx + space(6) * 2);
    m_spacing = space(6);
    m_rowHeight = qMax(space(50), headingPx + space(12) * 2);
    m_rowGap = space(3);

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
    const int width = qMin(space(800), host ? host->width() - space(40) : space(800));
    // The keys column is 35 characters, as the shell pads them, but no wider
    // than the longest keys need and never most of the row.
    const QFontMetrics fm(OmarchyTheme::instance()->headingFont());
    int longest = 0;
    for (const Binding &b : m_model->all())
        longest = qMax(longest, fm.horizontalAdvance(b.keys));
    const int ch = fm.horizontalAdvance(QLatin1Char('0'));
    const int inset = space(18);
    const int keyColumn = qMin(qMin(ch * 35, longest + ch * 3), (width - 2 * m_padding - 2 * inset) * 3 / 5);
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
    const int rowsHeight = rows * (m_rowHeight + m_rowGap); // each row's size hint includes its gap
    int cap = space(500);
    if (host)
        cap = qMin(cap, host->height() - space(40));
    const int chrome = 2 * m_padding + m_headerHeight;
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
    const QModelIndex index = m_model->index(row);
    m_list->setCurrentIndex(index);
    m_list->scrollTo(index);
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
