#pragma once

#include <QDialog>
#include <QVector>

#include <functional>

class QLineEdit;
class QListView;
class QModelIndex;
class BindingModel;

// The keybindings panel (Ctrl+K), drawn like Omarchy's own Super+K menu: a
// bordered card whose header line is the search, then one row per binding
// in the menu's spelling — "CTRL + F   → Fetch". Typing filters the rows,
// Up/Down move the cursor (wrapping at the ends), Enter (or a click) runs the binding's action when
// it has one, Esc or a click outside closes the panel.
class KeybindingsPanel : public QDialog
{
    Q_OBJECT
public:
    explicit KeybindingsPanel(QWidget *parent);
    ~KeybindingsPanel() override;

    // `keys` as the menu spells them ("CTRL SHIFT + P", "F5 / CTRL SHIFT + R");
    // `context` (dim, at the right end of the row) says where the keys work
    // when that is not everywhere; `run` is what Enter on the row does.
    void add(const QString &keys, const QString &action, const QString &context = QString(),
             std::function<void()> run = {});
    // Sizes the card for its rows, centers it on the parent and shows it.
    void popup();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void applyFilter(const QString &query);
    void activate(const QModelIndex &index);
    // `wrap` steps from the last row to the first and back; otherwise the ends clamp.
    void moveCursor(int delta, bool absolute, bool wrap = false);
    // Scrolls `row` fully into view with a peek of its neighbour past it.
    void revealRow(int row);
    // How much of the first hidden row shows at the fold, as the shell sizes it.
    int rowPeek() const;
    void fitHeight();

    QLineEdit *m_search;
    QListView *m_list;
    BindingModel *m_model;
    int m_padding;      // inside the card's border
    int m_headerHeight; // the search line
    int m_spacing;      // header to rows
    int m_rowHeight;    // one row, without the gap below it
    int m_rowGap;
    int m_top = -1;     // the card's top edge once shown: the card only grows and shrinks downwards
};
