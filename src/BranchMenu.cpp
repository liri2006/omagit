#include "BranchMenu.h"
#include "OmarchyTheme.h"
#include "UiHelpers.h"

#include <QAction>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPainter>
#include <QPaintEvent>
#include <QStyleOptionMenuItem>
#include <QTimer>
#include <QWidgetAction>

namespace {
// The design's menu (screens.js screen(), the branch overlay): 300 px wide,
// the window's margin clear of its edges where the window is narrower. Its
// rows are a menu's (menuCard()): the glyph's 16 px box 8 into the row, the
// name 4 after it (the stylesheet's BranchMenu::item padding).
constexpr int kWidth = 300;
const char *const kGlyphProperty = "branchGlyph";
// A row's hint (menuCard(): the text after the label, at the right): the
// design's small text, 11 px at base 12.
constexpr int kHintPx = 11;

// The part of a row's text after its tab: the keys QMenu would draw at the
// right, which this menu draws itself (BranchMenu::paintEvent()).
QString shortcutText(const QAction *action)
{
    const qsizetype tab = action->text().indexOf(QLatin1Char('\t'));
    return tab < 0 ? QString() : action->text().mid(tab + 1);
}
} // namespace

#include <algorithm>

// The entries plus the search field above them.
class BranchMenu::Filter : public QObject
{
public:
    Filter(BranchMenu *menu, QLineEdit *edit)
        : QObject(menu), m_menu(menu), m_edit(edit)
    {
        edit->installEventFilter(this);
        connect(edit, &QLineEdit::textChanged, this, &Filter::apply);
    }

    void clear()
    {
        m_entries.clear();
        m_sections.clear();
        m_placeholders.clear();
        m_noMatch = nullptr;
        m_newRow = nullptr;
        m_newSeparator = nullptr;
        m_locals.clear();
    }
    // `separator` is the line the menu draws above the section, if it has one.
    void addHeader(QAction *header, QAction *separator = nullptr) { m_sections.append(Section{header, separator, {}}); }
    void addEntry(QAction *action, const QString &name)
    {
        m_entries.append(Entry{action, name});
        if (!m_sections.isEmpty())
            m_sections.last().entries.append(action);
    }
    void setNoMatch(QAction *action) { m_noMatch = action; }
    // A row that stands in for a section's entries while the search is empty
    // ("No branches yet").
    void addPlaceholder(QAction *action) { m_placeholders.append(action); }
    // The New branch row and the separator over it; `locals` are the names
    // it hides for, a branch of that name being there already.
    void setNewBranch(QAction *separator, QAction *row, const QStringList &locals)
    {
        m_newSeparator = separator;
        m_newRow = row;
        m_locals = locals;
    }

    void apply()
    {
        const QString text = m_edit->text().trimmed();
        bool matched = false;
        for (const Entry &e : std::as_const(m_entries)) {
            const bool match = e.name.contains(text, Qt::CaseInsensitive);
            e.action->setVisible(match);
            matched = matched || match;
        }
        for (QAction *a : std::as_const(m_placeholders))
            a->setVisible(text.isEmpty());
        // A section's separator only has something to separate while a section
        // above it is showing too; left alone it would double the prompt's line.
        bool above = false;
        for (const Section &s : std::as_const(m_sections)) {
            const bool any = std::any_of(s.entries.cbegin(), s.entries.cend(), [](QAction *a) { return a->isVisible(); });
            const bool shown = any || text.isEmpty();
            setVisible(s.header, shown);
            if (s.separator)
                s.separator->setVisible(shown && above);
            above = above || shown;
        }
        if (m_newRow) {
            // A name of the search's own, unless a local branch has it already;
            // with nothing typed, the plain row and its keys.
            const bool taken = !text.isEmpty() && m_locals.contains(text);
            QString name = text;
            name.replace(QLatin1Char('&'), QStringLiteral("&&")); // an ampersand, not a mnemonic
            m_newRow->setText(text.isEmpty() ? BranchMenu::tr("New branch…") + QStringLiteral("\tCtrl+N")
                                             : BranchMenu::tr("New branch “%1”…").arg(name));
            m_newRow->setVisible(!taken);
            // Alone under the prompt, whose hairline is line enough.
            m_newSeparator->setVisible(!taken && (text.isEmpty() || matched));
        }
        const QList<QAction *> shown = visible();
        if (m_noMatch)
            setVisible(m_noMatch, !text.isEmpty() && !matched && !(m_newRow && m_newRow->isVisible()));
        m_menu->setActiveAction(text.isEmpty() ? nullptr : shown.value(0));
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched != m_edit || event->type() != QEvent::KeyPress)
            return false;
        auto *key = static_cast<QKeyEvent *>(event);
        // Ctrl+N is the window's New branch; in here, with the search's text.
        if (m_newRow && key->key() == Qt::Key_N && key->modifiers() == Qt::ControlModifier) {
            m_menu->requestNewBranch();
            return true;
        }
        switch (key->key()) {
        case Qt::Key_Down:
        case Qt::Key_Up: {
            const QList<QAction *> shown = visible();
            if (shown.isEmpty())
                return true;
            const int at = shown.indexOf(m_menu->activeAction());
            const int step = key->key() == Qt::Key_Down ? 1 : -1;
            const int next = at < 0 ? (step > 0 ? 0 : shown.size() - 1) : (at + step + shown.size()) % shown.size();
            m_menu->setActiveAction(shown.at(next));
            return true;
        }
        case Qt::Key_Return:
        case Qt::Key_Enter: {
            QAction *pick = m_menu->activeAction();
            if (!pick || !visible().contains(pick))
                pick = visible().value(0);
            if (pick) {
                m_menu->close(); // like a click: the menu is gone before the branch switches
                pick->trigger();
            }
            return true;
        }
        default:
            return false;
        }
    }

private:
    struct Entry {
        QAction *action;
        QString name;
    };
    struct Section {
        QAction *header;
        QAction *separator;
        QList<QAction *> entries;
    };
    // QMenu leaves the widget of a hidden QWidgetAction where it was, so it is hidden by hand.
    static void setVisible(QAction *action, bool on)
    {
        action->setVisible(on);
        if (auto *wa = qobject_cast<QWidgetAction *>(action))
            wa->defaultWidget()->setVisible(on);
    }
    // The rows the keys move over, in their order: the entries that can be
    // picked, then the New branch row where it shows.
    QList<QAction *> visible() const
    {
        QList<QAction *> list;
        for (const Entry &e : m_entries)
            if (e.action->isVisible() && e.action->isEnabled())
                list.append(e.action);
        if (m_newRow && m_newRow->isVisible())
            list.append(m_newRow);
        return list;
    }

    BranchMenu *m_menu;
    QLineEdit *m_edit;
    QList<Entry> m_entries;
    QList<Section> m_sections;
    QList<QAction *> m_placeholders;
    QAction *m_noMatch = nullptr;
    QAction *m_newRow = nullptr;
    QAction *m_newSeparator = nullptr;
    QStringList m_locals;
};

BranchMenu::BranchMenu(QWidget *parent)
    : TickMenu(parent)
{
    setToolTipsVisible(true);
    m_search = ui::promptField(tr("Search branches…"));
    m_search->setToolTip(tr("Type to narrow the list; Up/Down and Return pick a branch"));
    // The hairline under the prompt is part of its box, so it stays put while
    // the filter hides and shows the entries below it.
    auto *searchAction = new QWidgetAction(this);
    searchAction->setDefaultWidget(ui::promptBox(m_search));
    addAction(searchAction);
    m_filter = new Filter(this, m_search);
}

void BranchMenu::setBranches(const BranchList &branches, const QString &checked, bool remote, const TipFunction &tip,
                             const QString &disabled, const QStringList &tags)
{
    m_filter->clear(); // filling the menu again starts from an empty list
    // Every entry is checkable so the tick can mark the current branch.
    auto add = [this, &checked, &disabled, &tip](const QString &name, uint glyph, bool isRemote) {
        QAction *a = addAction(name);
        a->setProperty(kGlyphProperty, glyph);
        a->setCheckable(true);
        a->setChecked(name == checked);
        a->setEnabled(name != disabled);
        if (tip)
            a->setToolTip(tip(name, isRemote));
        connect(a, &QAction::triggered, this, [this, name] { emit picked(name); });
        m_filter->addEntry(a, name);
        return a;
    };
    m_filter->addHeader(ui::addMenuHeader(this, tr("Local")));
    if (branches.local.isEmpty()) {
        QAction *none = addAction(tr("No branches yet"));
        none->setEnabled(false);
        m_filter->addPlaceholder(none);
    }
    for (const QString &name : branches.local)
        add(name, ui::kBranch, false);
    if (remote && !branches.remote.isEmpty()) {
        QAction *const separator = addSeparator();
        m_filter->addHeader(ui::addMenuHeader(this, tr("Remote")), separator);
        for (const QString &name : branches.remote)
            add(name, ui::kCloudOutline, true);
    }
    if (!tags.isEmpty()) {
        m_search->setPlaceholderText(tr("Search branches and tags…"));
        QAction *const separator = addSeparator();
        m_filter->addHeader(ui::addMenuHeader(this, tr("Tags")), separator);
        for (const QString &name : tags)
            add(name, ui::kTagOutline, false);
    }
    QAction *noMatch = addAction(tr("No matching branch"));
    noMatch->setEnabled(false);
    noMatch->setVisible(false);
    m_filter->setNoMatch(noMatch);
    if (m_newBranchRow) {
        // Last, after a separator, like Open… and Clone… in the repository
        // menu; its text is the filter's (apply()).
        QAction *const separator = addSeparator();
        QAction *row = addAction(QString());
        row->setProperty(kGlyphProperty, ui::kBranchPlus);
        row->setToolTip(tr("Start a new branch — named after the search, when it has text (Ctrl+N)"));
        connect(row, &QAction::triggered, this, [this] { emit newBranchRequested(m_search->text().trimmed()); });
        m_filter->setNewBranch(separator, row, branches.local);
    }
    m_filter->apply();
}

void BranchMenu::requestNewBranch()
{
    const QString name = m_search->text().trimmed();
    close(); // like a click: the menu is gone before the card opens
    emit newBranchRequested(name);
}

void BranchMenu::popupAt(QWidget *anchor, bool above, QWidget *bar, int gap)
{
    // The field has the keyboard from the start, so typing filters right away.
    // Only once the menu is up: QMenu takes the focus for itself when it opens.
    QTimer::singleShot(0, m_search, [this] { m_search->setFocus(); });
    // The design's width where the window has the room, at least as wide as
    // the button it hangs from (so the two line up) and never wider than the
    // room from the anchor to the window's right margin.
    const QWidget *window = anchor->window();
    const int room = window->width() - ui::windowMargin(window) - anchor->mapTo(window, QPoint(0, 0)).x();
    setMinimumWidth(qMax(anchor->width(), qMin(ui::space(kWidth), room)));
    setMaximumWidth(qMax(anchor->width(), room));
    const int y = above ? -sizeHint().height() - gap : anchor->height() + gap;
    QPoint at = anchor->mapToGlobal(QPoint(0, y));
    if (bar && !above)
        at.setY(ui::popupTop(bar));
    exec(at);
}

void BranchMenu::initStyleOption(QStyleOptionMenuItem *option, const QAction *action) const
{
    TickMenu::initStyleOption(option, action);
    const qsizetype tab = option->text.indexOf(QLatin1Char('\t'));
    if (tab >= 0)
        option->text.truncate(tab);
    option->reservedShortcutWidth = 0;
}

void BranchMenu::paintEvent(QPaintEvent *event)
{
    TickMenu::paintEvent(event);
    const OmarchyTheme *theme = OmarchyTheme::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QFont hintFont = theme->uiFont();
    hintFont.setPixelSize(ui::fontPx(kHintPx));
    hintFont.setBold(false);
    const QList<QAction *> all = actions();
    for (QAction *a : all) {
        if (!a->isVisible())
            continue;
        const QRect r = actionGeometry(a);
        if (r.isNull() || !event->rect().intersects(r))
            continue;
        // The keys at the right (screens.js menuCard(), a row's hint): dim,
        // small, ending 8 from the row's edge, on the design's baseline.
        const QString keys = shortcutText(a);
        if (!keys.isEmpty()) {
            p.setFont(hintFont);
            p.setPen(theme->mutedText());
            const qreal right = r.right() + 1 - ui::space(ui::pad::control);
            const qreal baseline = r.top() + qRound(r.height() / 2.0 + 0.36 * hintFont.pixelSize());
            p.drawText(QPointF(right - QFontMetricsF(hintFont).horizontalAdvance(keys), baseline), keys);
        }
        const QVariant code = a->property(kGlyphProperty);
        if (!code.isValid())
            continue;
        const QString glyph = theme->glyph(code.toUInt());
        if (glyph.isEmpty())
            continue;
        p.setFont(theme->uiFont());
        // The entry's own colour: the accent under the pointer and on the
        // current branch, the disabled pen on the other side of a merge.
        p.setPen(!a->isEnabled()                              ? theme->fill(0.45)
                 : a == activeAction() || a->isChecked()      ? theme->accent()
                                                              : theme->text());
        // Centred by its ink in the design's 16 px box (TextDontClip's
        // concern: the ink overhangs the advance).
        const QRectF box(r.left() + ui::space(ui::pad::control), r.top(), ui::space(ui::box::icon), r.height());
        p.drawText(box.center() - ui::inkRect(p.font(), glyph).center(), glyph);
    }
}
