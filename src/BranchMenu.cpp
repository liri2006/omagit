#include "BranchMenu.h"
#include "UiHelpers.h"

#include <QAction>
#include <QKeyEvent>
#include <QLineEdit>
#include <QTimer>
#include <QWidgetAction>

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
        m_noMatch = nullptr;
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

    void apply()
    {
        const QString text = m_edit->text().trimmed();
        for (const Entry &e : std::as_const(m_entries))
            e.action->setVisible(e.name.contains(text, Qt::CaseInsensitive));
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
        const QList<QAction *> shown = visible();
        if (m_noMatch)
            setVisible(m_noMatch, shown.isEmpty() && !text.isEmpty());
        m_menu->setActiveAction(text.isEmpty() ? nullptr : shown.value(0));
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched != m_edit || event->type() != QEvent::KeyPress)
            return false;
        auto *key = static_cast<QKeyEvent *>(event);
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
    QList<QAction *> visible() const
    {
        QList<QAction *> list;
        for (const Entry &e : m_entries)
            if (e.action->isVisible() && e.action->isEnabled())
                list.append(e.action);
        return list;
    }

    BranchMenu *m_menu;
    QLineEdit *m_edit;
    QList<Entry> m_entries;
    QList<Section> m_sections;
    QAction *m_noMatch = nullptr;
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
                             const QString &disabled)
{
    m_filter->clear(); // filling the menu again starts from an empty list
    // Every entry is checkable so the tick can mark the current branch.
    auto add = [this, &checked, &disabled, &tip](const QString &name, bool isRemote) {
        QAction *a = addAction(name);
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
    if (branches.local.isEmpty())
        addAction(tr("No branches yet"))->setEnabled(false);
    for (const QString &name : branches.local)
        add(name, false);
    if (remote && !branches.remote.isEmpty()) {
        QAction *const separator = addSeparator();
        m_filter->addHeader(ui::addMenuHeader(this, tr("Remote")), separator);
        for (const QString &name : branches.remote)
            add(name, true);
    }
    QAction *noMatch = addAction(tr("No matching branch"));
    noMatch->setEnabled(false);
    noMatch->setVisible(false);
    m_filter->setNoMatch(noMatch);
}

void BranchMenu::popupAt(QWidget *anchor, bool above)
{
    // The field has the keyboard from the start, so typing filters right away.
    // Only once the menu is up: QMenu takes the focus for itself when it opens.
    QTimer::singleShot(0, m_search, [this] { m_search->setFocus(); });
    // At least as wide as the button it hangs from, so the two line up.
    setMinimumWidth(qMax(minimumWidth(), anchor->width()));
    const int y = above ? -sizeHint().height() : anchor->height();
    exec(anchor->mapToGlobal(QPoint(0, y)));
}
