#pragma once

#include "GitRepo.h"
#include "TickMenu.h"

#include <functional>

class QLineEdit;

// The branch dropdown: a search field that has the keyboard from the
// start, then the LOCAL and the REMOTE branches with the current one
// ticked. Typing narrows the list, Up/Down move the highlight without
// leaving the field, Return picks the highlighted (else the first) match,
// Escape closes. Used by the footer's branch button and by both sides of
// the merge view.
class BranchMenu : public TickMenu
{
    Q_OBJECT
public:
    // The tooltip of an entry; `remote` tells "origin/x" from "x".
    using TipFunction = std::function<QString(const QString &name, bool remote)>;

    explicit BranchMenu(QWidget *parent = nullptr);

    // Fills the menu: `checked` gets the tick, `disabled` is listed but
    // cannot be picked (the other side of a merge), remote branches only
    // when `remote` is set.
    void setBranches(const BranchList &branches, const QString &checked, bool remote, const TipFunction &tip,
                     const QString &disabled = QString());
    // Opens the menu hanging below `anchor` (or rising above it) and runs
    // until it closes; picked() fires for the chosen branch.
    void popupAt(QWidget *anchor, bool above);

signals:
    void picked(const QString &name);

private:
    class Filter;
    QLineEdit *m_search;
    Filter *m_filter;
};
