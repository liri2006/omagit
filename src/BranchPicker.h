#pragma once

#include <QToolButton>

// A field-like button showing a branch (glyph, name, a chevron at the right
// edge) that drops a branch list down on click: the two sides of the merge
// view, and where the New branch card starts from. Like a combo box, Return
// goes on to the surface's default action and the list opens on Space, Down
// or F4. It only shows the name; the caller opens the list on clicked().
class BranchPicker : public QToolButton
{
    Q_OBJECT
public:
    // Big is the merge view's 36 px picker with a big control's 12 of
    // padding; Control a 28 px one with a control's 8 (the New branch card).
    enum class Size { Big, Control };
    // The glyph in front of the name: what the name is (Key: the clone
    // dialog's ssh key, which borrows the picker for a field of its own).
    enum class Kind { Branch, Tag, Commit, Key };

    explicit BranchPicker(Size size = Size::Big, QWidget *parent = nullptr);

    void setBranch(const QString &name, Kind kind = Kind::Branch);
    QString branch() const { return m_name; }
    Kind kind() const { return m_kind; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;
    void paintEvent(QPaintEvent *) override;

private:
    Size m_size;
    Kind m_kind = Kind::Branch;
    QString m_name;
};
