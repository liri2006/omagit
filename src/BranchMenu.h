#pragma once

#include "GitRepo.h"
#include "TickMenu.h"

#include <functional>

class QLineEdit;

// The branch dropdown: a search field that has the keyboard from the
// start, then the LOCAL and the REMOTE branches (and the TAGS, where it is
// given some) with the current one ticked. Typing narrows the list, Up/Down
// move the highlight without leaving the field, Return picks the highlighted
// (else the first) match, Escape closes. Used by the top bar's branch chip,
// by both sides of the merge view and by the New branch card's From picker.
// Every entry wears its glyph, a branch for a local one, a cloud for a
// remote one and a tag for a tag, where the design's menu rows put an icon.
//
// The top bar's menu ends with a New branch… row (setNewBranchRow()): while
// the search has text it offers a branch of that name instead, and where no
// branch matches it is the only row left, current, so Return takes the name
// to the New branch card.
class BranchMenu : public TickMenu
{
    Q_OBJECT
public:
    // The tooltip of an entry; `remote` tells "origin/x" from "x".
    using TipFunction = std::function<QString(const QString &name, bool remote)>;

    explicit BranchMenu(QWidget *parent = nullptr);

    // The New branch… row after the lists, behind a separator; off by
    // default. Set before setBranches(), which builds the rows.
    void setNewBranchRow(bool on) { m_newBranchRow = on; }

    // Fills the menu: `checked` gets the tick, `disabled` is listed but
    // cannot be picked (the other side of a merge), remote branches only
    // when `remote` is set, and a TAGS section after them for `tags`.
    void setBranches(const BranchList &branches, const QString &checked, bool remote, const TipFunction &tip,
                     const QString &disabled = QString(), const QStringList &tags = {});
    // Opens the menu hanging below `anchor` (or rising above it) and runs
    // until it closes; picked() fires for the chosen branch. With a `bar`,
    // it hangs from the bar instead (ui::popupTop()), at the anchor's left.
    // `gap` pixels are left between the anchor and the menu (the bar's own
    // gap is popupTop()'s).
    void popupAt(QWidget *anchor, bool above, QWidget *bar = nullptr, int gap = 0);

signals:
    void picked(const QString &name);
    // The New branch row, or Ctrl+N in the search field: the menu is closed
    // and `name` is what the search held, trimmed (empty when it held nothing).
    void newBranchRequested(const QString &name);

protected:
    // The entries' glyphs in front of their names, and the right-aligned
    // shortcut text of a row that has one (the part of its text after a tab).
    void paintEvent(QPaintEvent *event) override;
    // A row's shortcut text is painted by paintEvent(), dim and small where
    // the design puts a menu row's hint, so the style is not given it.
    void initStyleOption(QStyleOptionMenuItem *option, const QAction *action) const override;

private:
    class Filter;
    // Closes the menu, then asks for a branch named after the search.
    void requestNewBranch();

    QLineEdit *m_search;
    Filter *m_filter;
    bool m_newBranchRow = false;
};
