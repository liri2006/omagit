#include "CommitPage.h"
#include "ChangesTreeModel.h"
#include "DesktopExec.h"
#include "MessageEdit.h"
#include "OmarchyTheme.h"
#include "Settings.h"
#include "TickMenu.h"
#include "UiHelpers.h"

#include <QAction>
#include <QButtonGroup>
#include <QCheckBox>
#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

using namespace ui;

namespace {
// The design hangs the action bar 8 px under the changes list — its own gap,
// not the 6 px a section header row keeps to its content.
int actionBarGap()
{
    return space(8);
}

// The stacked action bar's gap between the options button and Commit.
constexpr int kStackedActionGap = 6;

// The stacked action bar's options menu (screens.js: the OptionsMenu card).
constexpr int kOptionsMenuWidth = 240;
// The header rows' icon buttons stand this far inside the page's right edge
// (screens.js changesPage(): a 24 px button at x + w − 26).
constexpr int kHeaderInset = 2;

// The message box's resting heights (screens.js changesPage(): mh), in
// 12 px-base pixels: by the window's width class, and one line when the
// window is shallow. The one-line box is also the least it may be dragged to.
constexpr int kMessageWide = 96, kMessageLarge = 84, kMessageNarrow = 68, kMessageShallow = 34;

// The eye's filter, and with it the check-all box of the table's header: the
// box stands for the rows the list is showing, so with the unversioned files
// hidden it neither counts them nor ticks them.
class UnversionedFilter : public QSortFilterProxyModel
{
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    void setShowUnversioned(bool on)
    {
        if (m_showUnversioned == on)
            return;
        m_showUnversioned = on;
        invalidate();
        // The source forwards its own headerDataChanged through the proxy, but
        // rows coming and going is ours to report: the header would otherwise
        // keep painting the state of the list it saw last.
        emit headerDataChanged(Qt::Horizontal, ChangesModel::Check, ChangesModel::Check);
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        const QVariant base = QSortFilterProxyModel::headerData(section, orientation, role);
        // An invalid state is a list without checkboxes at all (the history's
        // files): it has no box to paint, filtered or not.
        if (role != Qt::CheckStateRole || orientation != Qt::Horizontal || section != ChangesModel::Check
            || !base.isValid())
            return base;
        const int rows = rowCount(), checked = checkedRows();
        return int(checked == 0      ? Qt::Unchecked
                   : checked == rows ? Qt::Checked
                                     : Qt::PartiallyChecked);
    }

    bool setHeaderData(int section, Qt::Orientation orientation, const QVariant &value, int role) override
    {
        if (role != Qt::CheckStateRole || orientation != Qt::Horizontal || section != ChangesModel::Check)
            return QSortFilterProxyModel::setHeaderData(section, orientation, value, role);
        auto *source = qobject_cast<ChangesModel *>(sourceModel());
        if (!source || !QSortFilterProxyModel::headerData(section, orientation, role).isValid())
            return false;
        QStringList paths;
        paths.reserve(rowCount());
        for (int row = 0, rows = rowCount(); row < rows; ++row)
            paths << index(row, ChangesModel::Check).data(ChangesModel::PathRole).toString();
        // Exactly the rows on show: a rename source that happens to spell a
        // filtered-out file's path is not one of them.
        source->setPathsChecked(paths, value.toInt() == Qt::Checked, ChangesModel::CurrentPathsOnly);
        return true;
    }

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override
    {
        if (m_showUnversioned)
            return true;
        auto *m = static_cast<ChangesModel *>(sourceModel());
        Q_UNUSED(parent)
        return !m->change(row).isUntracked();
    }

private:
    // How many of the rows on show are ticked.
    int checkedRows() const
    {
        int checked = 0;
        for (int row = 0, rows = rowCount(); row < rows; ++row)
            if (index(row, ChangesModel::Check).data(Qt::CheckStateRole).toInt() == Qt::Checked)
                ++checked;
        return checked;
    }

    bool m_showUnversioned = true;
};

// The commit list's own Space: the current cell may sit in any column — a
// click on a file name leaves it in Name, which carries no checkbox — so the
// key goes to the row's box, the way the Mini rail's does.
class ChangesTable : public QTableView
{
public:
    using QTableView::QTableView;

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Space && event->modifiers() == Qt::NoModifier && currentIndex().isValid()) {
            const QModelIndex box = currentIndex().siblingAtColumn(ChangesModel::Check);
            const QVariant check = box.data(Qt::CheckStateRole);
            if (check.isValid()) {
                model()->setData(box, check.toInt() == Qt::Checked ? Qt::Unchecked : Qt::Checked,
                                 Qt::CheckStateRole);
                event->accept();
                return;
            }
        }
        QTableView::keyPressEvent(event);
    }
};

// ---- The tree and compact presentations ------------------------------------

// The design's geometry of a file list, in the pixels of a 12 px font — every
// one of them goes through space(), so the tree follows the text size like
// everything else. A level of the tree is 14 px, and the whole of it is
// painted in the Name column: the checkbox column stays one straight line,
// however deep a row sits.
constexpr int kNarrowColumn = 30; // the checkbox and the status pill
constexpr int kLevel = 14;
constexpr int kChevronX = 6, kChevronGlyph = 12;
constexpr int kFolderX = 20, kFolderGlyph = 14;
constexpr int kDirNameX = 38, kFileNameX = 8;
constexpr int kNameInset = 10;    // the compact table's Name cell
constexpr int kSuffixGap = 8, kSuffixText = 11;

// Where a directory's chevron and folder sit in its Name cell, measured from
// the cell's left edge. Painting and the click that opens the branch share
// it, so a row can never open somewhere other than where it says it will.
QRect branchRect(const QRect &cell, int depth)
{
    const int left = cell.left() + space(kChevronX + kLevel * depth);
    const int right = cell.left() + space(kFolderX + kLevel * depth) + space(kFolderGlyph);
    return QRect(left, cell.top(), right - left, cell.height());
}

// One Nerd Font glyph, `px` design pixels tall, at the left of `box`.
void paintGlyph(QPainter *painter, const QRect &box, uint code, const QString &fallback, int px,
                const QColor &colour)
{
    const OmarchyTheme *theme = OmarchyTheme::instance();
    const QString glyph = theme->glyph(code);
    QFont font = theme->uiFont();
    font.setPixelSize(space(px));
    painter->setFont(font);
    painter->setPen(colour);
    painter->drawText(box, Qt::AlignLeft | Qt::AlignVCenter, glyph.isEmpty() ? fallback : glyph);
}

// A name with a dim, smaller note after it: the file count of a folded
// directory, the folder of a compact row. The name is what the row is about,
// so it takes the width it needs and the note lives on what is left; both are
// elided rather than spilling into the column beside them.
void paintNameWithSuffix(QPainter *painter, const QRect &box, const QString &name, const QFont &nameFont,
                         const QColor &nameColour, const QString &suffix)
{
    const OmarchyTheme *theme = OmarchyTheme::instance();
    const QFontMetrics nameMetrics(nameFont);
    // Measured, not guessed at: where the name ends is where the note begins.
    // Text that fits is drawn as it is — asking for it to be elided into
    // exactly its own width can still cost it a character.
    const int width = nameMetrics.horizontalAdvance(name);
    const int room = qMin(width, box.width());
    painter->setFont(nameFont);
    painter->setPen(nameColour);
    painter->drawText(QRect(box.left(), box.top(), room, box.height()), Qt::AlignLeft | Qt::AlignVCenter,
                      width <= room ? name : nameMetrics.elidedText(name, Qt::ElideMiddle, room));
    if (suffix.isEmpty())
        return;
    const int left = box.left() + room + space(kSuffixGap);
    const int rest = box.right() + 1 - left;
    if (rest <= 0)
        return;
    QFont small = theme->uiFont();
    small.setPixelSize(space(kSuffixText));
    const QFontMetrics smallMetrics(small);
    const int suffixWidth = smallMetrics.horizontalAdvance(suffix);
    painter->setFont(small);
    painter->setPen(theme->mutedText());
    painter->drawText(QRect(left, box.top(), rest, box.height()), Qt::AlignLeft | Qt::AlignVCenter,
                      suffixWidth <= rest ? suffix : smallMetrics.elidedText(suffix, Qt::ElideRight, rest));
}

// Everything a delegate of these two presentations has in common: it paints
// the cell itself, so the base class is only ever asked for the selection and
// hover background, and every row is the shared file-list height.
class PresentationDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(fileRowHeight());
        return size;
    }

protected:
    // The font the row itself asks for — bold for a conflicted file — which
    // the option the view hands the delegate has not got: only
    // initStyleOption() resolves Qt::FontRole over it. Everything these
    // delegates paint and measure by hand goes through this rather than
    // through option.font.
    QFont rowFont(const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        QStyleOptionViewItem resolved = option;
        initStyleOption(&resolved, index); // the override below clears the text only
        return resolved.font;
    }

    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::initStyleOption(option, index);
        option->text.clear();
    }
};

// The narrow status column of the tree and of the compact table.
class StatusPillDelegate : public PresentationDelegate
{
public:
    using PresentationDelegate::PresentationDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::paint(painter, option, index);
        painter->save();
        paintStatusPill(painter, option.rect, index);
        painter->restore();
    }
};

// The checkbox column: the themed 14 px indicator the table has always had,
// and nothing else — no text, and no depth, however deep the row sits.
class CheckColumnDelegate : public PresentationDelegate
{
public:
    using PresentationDelegate::PresentationDelegate;
};

// The Name column of the compact table: the file name, then the dim, smaller
// folder it is in. A file in the root of the repository has neither the
// suffix nor the gap before it.
class CompactNameDelegate : public PresentationDelegate
{
public:
    using PresentationDelegate::PresentationDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::paint(painter, option, index);
        const QString path = index.data(ChangesModel::PathRole).toString();
        const int separator = path.lastIndexOf(QLatin1Char('/'));
        const QString folder = separator < 0 ? QString() : path.left(separator) + QLatin1Char('/');
        const QRect box = option.rect.adjusted(space(kNameInset), 0, -space(kNameInset), 0);
        painter->save();
        paintNameWithSuffix(painter, box, index.data(Qt::DisplayRole).toString(), rowFont(option, index),
                            option.state & QStyle::State_Selected ? OmarchyTheme::instance()->accent()
                                                                  : statusColour(index),
                            folder);
        painter->restore();
    }
};

// The Name column of the tree, which carries the whole depth geometry: the
// chevron and the folder of a directory, the file names under them, and the
// "N files" a folded directory says instead of showing them.
class TreeNameDelegate : public PresentationDelegate
{
public:
    TreeNameDelegate(QTreeView *tree, ChangesTreeModel *model)
        : PresentationDelegate(tree), m_tree(tree), m_model(model)
    {
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::paint(painter, option, index);
        const OmarchyTheme *theme = OmarchyTheme::instance();
        const bool selected = option.state & QStyle::State_Selected;
        const int depth = m_model->depth(index);
        const QRect cell = option.rect;
        const QFont font = rowFont(option, index);
        painter->save();
        if (m_model->isDirectory(index)) {
            const QModelIndex branch = index.siblingAtColumn(ChangesTreeModel::Check);
            const bool open = m_tree->isExpanded(branch);
            const QColor dim = theme->mutedText();
            paintGlyph(painter,
                       QRect(cell.left() + space(kChevronX + kLevel * depth), cell.top(),
                             space(kFolderX - kChevronX), cell.height()),
                       open ? kChevron : kChevronRight, open ? QStringLiteral("▾") : QStringLiteral("▸"),
                       kChevronGlyph, dim);
            paintGlyph(painter,
                       QRect(cell.left() + space(kFolderX + kLevel * depth), cell.top(),
                             space(kDirNameX - kFolderX), cell.height()),
                       kFolderOutline, QStringLiteral("/"), kFolderGlyph, dim);
            const int left = cell.left() + space(kDirNameX + kLevel * depth);
            const int files = m_model->fileCount(index);
            // A folded directory says how many files it is keeping from view;
            // an open one has them all on screen already.
            const QString count = open ? QString()
                : files == 1          ? tr("1 file")
                                      : tr("%1 files").arg(files);
            paintNameWithSuffix(painter, QRect(left, cell.top(), cell.right() + 1 - left, cell.height()),
                                index.data(Qt::DisplayRole).toString(), font,
                                selected ? theme->accent() : theme->text(), count);
        } else {
            const int left = cell.left() + space(kFileNameX + kLevel * depth);
            paintNameWithSuffix(painter, QRect(left, cell.top(), cell.right() + 1 - left, cell.height()),
                                index.data(Qt::DisplayRole).toString(), font,
                                selected ? theme->accent() : statusColour(index), QString());
        }
        painter->restore();
    }

private:
    QTreeView *m_tree;
    ChangesTreeModel *m_model;
};

// The tree presentation of the changes list. Its indentation is zero and it
// has no root decoration, so Qt contributes no branch geometry of its own:
// the Name delegate paints every pixel of the depth, and the checkbox column
// is a straight line at whatever level a row sits.
class ChangesTree : public QTreeView
{
public:
    explicit ChangesTree(QWidget *parent = nullptr)
        : QTreeView(parent)
    {
        setObjectName(QStringLiteral("changesTree"));
        setIndentation(0);
        setRootIsDecorated(false);
        setExpandsOnDoubleClick(true);
        setItemsExpandable(true);
        setUniformRowHeights(true);
        setAllColumnsShowFocus(true);
        setSelectionBehavior(SelectRows);
        setSelectionMode(SingleSelection);
        setEditTriggers(NoEditTriggers);
        setFrameShape(QFrame::NoFrame);
        setWordWrap(false);
        setTextElideMode(Qt::ElideMiddle);
    }

    // The rows have to exist before anybody measures a scroll range against
    // them: a layout still queued would clamp a restored offset to the tree
    // as it was before its branches were opened again.
    void layoutNow() { executeDelayedItemsLayout(); }

    // A new text size is a new row height, which the cached uniform one would
    // otherwise keep at the old value.
    void refreshRowHeights()
    {
        setUniformRowHeights(false);
        doItemsLayout();
        setUniformRowHeights(true);
        doItemsLayout();
    }

protected:
    // The list's own Space, as the table has it: the current cell may sit in
    // any column, so the key goes to the row's box — a file's or a whole
    // directory's.
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Space && event->modifiers() == Qt::NoModifier && currentIndex().isValid()) {
            const QModelIndex box = currentIndex().siblingAtColumn(ChangesTreeModel::Check);
            const QVariant check = box.data(Qt::CheckStateRole);
            if (check.isValid()) {
                model()->setData(box, check.toInt() == Qt::Checked ? Qt::Unchecked : Qt::Checked,
                                 Qt::CheckStateRole);
                event->accept();
                return;
            }
        }
        QTreeView::keyPressEvent(event);
    }

    // A left click on the chevron or the folder opens the branch and does
    // nothing else — the checkbox beside it is where check marks are made.
    // Any other button is Qt's: a right click there is the file menu's.
    void mousePressEvent(QMouseEvent *event) override
    {
        const QModelIndex branch = branchOf(event);
        if (branch.isValid()) {
            setExpanded(branch, !isExpanded(branch));
            event->accept();
            return;
        }
        QTreeView::mousePressEvent(event);
    }

    // The press above already opened the branch; letting Qt's expand-on-
    // double-click have it too would close it again in the same gesture.
    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (branchOf(event).isValid()) {
            event->accept();
            return;
        }
        QTreeView::mouseDoubleClickEvent(event);
    }

private:
    // The branch a left click of `event` is on; nothing at all for the other
    // buttons, which never open or close a directory.
    QModelIndex branchOf(const QMouseEvent *event) const
    {
        if (event->button() != Qt::LeftButton)
            return {};
        return branchAt(event->position().toPoint());
    }

    // The directory whose chevron-and-folder rectangle `pos` is in, if any.
    QModelIndex branchAt(const QPoint &pos) const
    {
        auto *tree = qobject_cast<ChangesTreeModel *>(model());
        const QModelIndex index = indexAt(pos);
        if (!tree || !index.isValid() || index.column() != ChangesTreeModel::Name || !tree->isDirectory(index))
            return {};
        if (!branchRect(visualRect(index), tree->depth(index)).contains(pos))
            return {};
        return index.siblingAtColumn(ChangesTreeModel::Check);
    }
};
} // namespace

CommitPage::CommitPage(GitRepo *repo, QWidget *parent)
    : QWidget(parent), m_repo(repo)
{
    setupAgent();

    // The page is the two sections over the action bar: inside, a header row
    // is 6 px above its content; the bar itself sits 8 px under the list and
    // ends with the page, on the line the diff pane beside it ends on (the
    // user's choice over the design's 12 px under it; applyTheme() scales the
    // gaps).
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(actionBarGap());
    auto *sections = new QVBoxLayout;
    sections->setContentsMargins(0, 0, 0, 0);
    sections->setSpacing(headerGap());
    m_sectionsLayout = sections;
    sections->addLayout(buildMessageSection());

    // The message box and the changes list share the height; where the user
    // last put the handle between them is remembered.
    m_messageSplitter = new QSplitter(Qt::Vertical);
    m_messageSplitter->setObjectName(QStringLiteral("commitMessageSplitter"));
    m_messageSplitter->setChildrenCollapsible(false);
    m_messageSplitter->addWidget(m_message);
    QWidget *const changes = buildChangesSection();
    m_messageSplitter->addWidget(changes);
    m_messageSplitter->setStretchFactor(0, 0);
    m_messageSplitter->setStretchFactor(1, 1); // the changes list takes window resizes
    connect(m_messageSplitter, &QSplitter::splitterMoved, this, [this] {
        m_messageSizedByHand = true;
        QSettings().setValue(settings::kWindowCommitMessageSplitter, m_messageSplitter->saveState());
    });
    connect(m_message, &MessageEdit::contentHeightChanged, this, &CommitPage::fitMessage);
    sections->addWidget(m_messageSplitter, 1);
    layout->addLayout(sections, 1);
    layout->addLayout(buildActionBar());
    // The resting height of the window's classes until the user drags the
    // handle; a height dragged in an earlier session comes back instead.
    m_messageSplitter->setSizes({restingMessageHeight(), changes->sizeHint().height()});
    m_messageSplitter->restoreState(QSettings().value(settings::kWindowCommitMessageSplitter).toByteArray());
    // The handle is all that stands between the two sections, so it carries
    // the gap between them — after restoreState(), which brings the handle
    // width of whatever text size saved the state back with it.
    m_messageSplitter->setHandleWidth(sectionGap());

    // How the last run left the files listed; anything unreadable, or nothing
    // at all, is the table. Reading a choice back never writes it again.
    setFilesView(viewFromKey(QSettings().value(settings::kWindowFilesView).toString(), nullptr), false);
}

QTextDocument *CommitPage::messageDocument() const
{
    return m_message->document();
}

CommitPage::CommitControls CommitPage::commitControls() const
{
    CommitControls c;
    // The card's button always has the key on it, whatever the page's says.
    c.commitText = commitButtonText(true);
    c.commitName = m_commitButton->accessibleName();
    c.commitTip = m_commitButton->toolTip();
    c.commitEnabled = m_commitButton->isEnabled();
    c.amendChecked = m_amend->isChecked();
    c.amendEnabled = m_amend->isEnabled();
    c.amendTip = m_amend->toolTip();
    c.checked = m_model->checkedCount();
    c.shown = m_proxy->rowCount();
    c.generateText = m_message->cornerButton()->text();
    c.generateTip = m_message->cornerButton()->toolTip();
    c.generating = m_agent->running();
    return c;
}

void CommitPage::requestAgentSettings(QWidget *anchor)
{
    emit agentSettingsRequested(anchor);
}

QString CommitPage::agentButtonTip()
{
    return tr("Which coding agent writes the commit message, with which model and reasoning level");
}

void CommitPage::setupAgent()
{
    m_agent = new CommitMessageAgent(this);
    connect(m_agent, &CommitMessageAgent::partial, this, [this](const QString &text) {
        m_message->replaceText(text, m_streaming);
        m_streaming = true;
    });
    connect(m_agent, &CommitMessageAgent::finished, this, &CommitPage::onMessageGenerated);
    // The CLIs are asked for their models and levels ahead of the cog menu.
    for (const AgentSpec &agent : CommitMessageAgent::installedAgents())
        CommitMessageAgent::probeAsync(agent.id, this);
    m_spinner = new QTimer(this);
    m_spinner->setInterval(80);
    connect(m_spinner, &QTimer::timeout, this, [this] {
        const QStringList frames = spinnerFrames(m_message->font());
        m_spinnerFrame = (m_spinnerFrame + 1) % frames.size();
        setGenerateFace(frames.at(m_spinnerFrame), m_message->cornerButton()->toolTip());
    });
}

// The MESSAGE row stands on the diff pane's toolbar line beside it: centred
// on a row of buttons, and the message box as far under that row as the diff
// is under the toolbar, so the two boxes start on one line.
QMargins CommitPage::messageRowMargins()
{
    const int top = (buttonHeight() - headerRowHeight()) / 2;
    const int bottom = buttonHeight() + barGap() - headerGap() - headerRowHeight() - top;
    return QMargins(0, top, space(kHeaderInset), qMax(0, bottom));
}

// MESSAGE, with the agent settings at the far right; the message box (which
// the caller puts in the splitter) has the generate button in its top right
// corner.
QLayout *CommitPage::buildMessageSection()
{
    auto *messageRow = m_messageRow = sectionHeaderRow(sectionLabel(tr("Message")));
    messageRow->setContentsMargins(messageRowMargins());
    messageRow->addStretch();
    m_agentButton = iconButton(kCog, tr("⚙"), agentButtonTip());
    connect(m_agentButton, &QToolButton::clicked, this, [this] { requestAgentSettings(m_agentButton); });
    messageRow->addWidget(m_agentButton, 0, Qt::AlignVCenter);

    m_message = new MessageEdit;
    m_message->setPlaceholderText(tr("Commit message"));
    m_message->setMinimumHeight(space(kMessageShallow));
    QToolButton *const generate = m_message->cornerButton();
    generate->setText(icon(kSparkle, QStringLiteral("✨")).trimmed());
    connect(generate, &QToolButton::clicked, this, &CommitPage::generateMessage);
    setGenerating(false);
    return messageRow;
}

// A message taller than its box grows the box instead of scrolling, the way a
// chat input does — at most to half of what the message and the changes list
// share, so the files never disappear. Typing only ever grows it (shrinking
// under the cursor would be unsettling); deleting text so the message is
// shorter than the box shrinks it back to the text, never below the height
// it had before any text grew it. The size taken this way is not the user's
// choice, so (unlike a dragged handle) it is not saved. Once the user has
// dragged the handle, typing keeps that size (otherwise the box could never
// be made smaller than its text); a paste or a message the agent wrote is a
// new message and grows the box again, and a deletion that leaves the text
// shorter than the box hands the size back to the text.
void CommitPage::fitMessage(MessageEdit::Edit edit)
{
    const int total = m_messageSplitter->height();
    const QList<int> sizes = m_messageSplitter->sizes();
    if (total <= 0 || sizes.size() != 2)
        return; // not laid out yet; the box asks again once it is shown
    const int current = sizes.at(0);
    if (m_messageRestHeight < 0)
        m_messageRestHeight = current;
    const int content = m_message->contentHeight();
    int wanted = current;
    if (edit == MessageEdit::Edit::Deleted && content < current) {
        m_messageSizedByHand = false;
        wanted = qMax(content, m_messageRestHeight);
        if (wanted >= current)
            return;
    } else {
        if (m_messageSizedByHand && edit != MessageEdit::Edit::Pasted)
            return;
        wanted = qMin(content, total / 2);
        if (wanted <= current)
            return;
    }
    m_messageSplitter->setSizes({wanted, sizes.at(1) + (current - wanted)});
}

// CHANGES, whose title carries the count, with the unversioned-files eye and
// Refresh at the right of the row, over the file list itself.
QWidget *CommitPage::buildChangesSection()
{
    auto *changes = new QWidget;
    auto *changesLayout = new QVBoxLayout(changes);
    changesLayout->setContentsMargins(0, 0, 0, 0);
    changesLayout->setSpacing(headerGap());
    m_changesLayout = changesLayout;

    m_changesLabel = sectionLabel(tr("Changes"));
    auto *changesRow = m_changesRow = sectionHeaderRow(m_changesLabel);
    changesRow->setContentsMargins(0, 0, space(kHeaderInset), 0);
    changesRow->addStretch();

    m_model = new ChangesModel(this);
    auto *proxy = new UnversionedFilter(this);
    proxy->setSourceModel(m_model);
    proxy->setSortRole(ChangesModel::SortRole);
    proxy->setSortCaseSensitivity(Qt::CaseInsensitive);
    m_proxy = proxy;

    changesRow->addLayout(buildChangesTools());
    changesLayout->addLayout(changesRow);

    m_table = new ChangesTable;
    m_table->setObjectName(QStringLiteral("changesTable"));
    m_table->setModel(m_proxy);
    // The model is on the table first: the setup reads the checkboxes off it.
    m_tableSetup = new ChangesTableSetup(m_table);
    m_tableSetup->setCompactDelegates(new CompactNameDelegate(m_table), new StatusPillDelegate(m_table));
    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &CommitPage::currentRowChanged);
    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            &CommitPage::onTableCurrentChanged);
    // Double-click: with the diff pane hidden, show it for the file (which the
    // click already made current); otherwise open the file in its own program.
    connect(m_table, &QTableView::doubleClicked, this, [this] {
        if (m_diffPaneVisible)
            emit openRequested();
        else
            emit showDiffPaneRequested();
    });
    connect(m_model, &ChangesModel::checkedChanged, this, &CommitPage::onCheckedChanged);
    // File actions apply to the clicked row, independent of checked files.
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QTableView::customContextMenuRequested, this,
            [this](const QPoint &pos) { showFileMenu(m_table, m_table->indexAt(pos), pos); });

    // Both presentations live in the list's slot at once: switching is a
    // change of what is on show, never a new model or a new selection.
    m_listStack = new QStackedWidget;
    m_listStack->addWidget(m_table);
    m_listStack->addWidget(buildChangesTree());
    changesLayout->addWidget(m_listStack, 1);
    return changes;
}

// title, stretch, compact, tree, table, divider, eye, divider, Refresh — in a
// layout of its own, with the design's 2 / 6 / 7 px gaps as spacers, so the
// section row's own spacing is not added on top of them.
QHBoxLayout *CommitPage::buildChangesTools()
{
    m_changesTools = new QHBoxLayout;
    m_changesTools->setContentsMargins(0, 0, 0, 0);
    m_changesTools->setSpacing(0);
    const auto gap = [this](int px) {
        auto *spacer = new QSpacerItem(space(px), 0, QSizePolicy::Fixed, QSizePolicy::Fixed);
        m_toolSpacers.append({spacer, px});
        m_changesTools->addItem(spacer);
    };
    const auto add = [this](QToolButton *button) {
        m_changesTools->addWidget(button, 0, Qt::AlignVCenter);
    };

    // How the files are listed: one of the three is always selected, and
    // picking the one already on changes nothing.
    m_viewButtons = new QButtonGroup(this);
    m_viewButtons->setExclusive(true);
    const auto switcher = [this, add, gap](FilesView view, uint glyph, const QString &fallback,
                                           const QString &name) {
        if (view != FilesView::Compact)
            gap(2); // the first of the three needs nothing before it
        QToolButton *button = iconButton(glyph, fallback, name);
        button->setCheckable(true);
        button->setAccessibleName(name);
        m_viewButtons->addButton(button, int(view));
        add(button);
        return button;
    };
    m_compactButton = switcher(FilesView::Compact, kFormatListBulleted, tr("C"), tr("Compact list"));
    m_treeButton = switcher(FilesView::Tree, kFileTree, tr("T"), tr("Tree"));
    m_tableButton = switcher(FilesView::Table, kTable, tr("L"), tr("Table"));
    connect(m_viewButtons, &QButtonGroup::idToggled, this, [this](int id, bool on) {
        if (on && int(m_filesView) != id)
            setFilesView(FilesView(id));
    });

    // The eye acts on the list, Refresh reloads it: a divider tells them apart.
    gap(6);
    m_toolsDivider = hairline(Qt::Vertical);
    m_toolsDivider->setFixedHeight(space(18)); // applyTheme() keeps it on the text size
    m_changesTools->addWidget(m_toolsDivider, 0, Qt::AlignVCenter);
    gap(7);
    m_unversioned = iconButton(kEye, tr("U"), tr("Show unversioned files"));
    m_unversioned->setCheckable(true);
    m_unversioned->setChecked(true);
    m_unversioned->setAccessibleName(tr("Show unversioned files"));
    connect(m_unversioned, &QToolButton::toggled, this, [this](bool on) {
        static_cast<UnversionedFilter *>(m_proxy)->setShowUnversioned(on);
        // Hiding the unversioned files unticks them; showing them again leaves
        // them unticked, the way a freshly read list does.
        untickHidden();
        onCheckedChanged(); // the title counts what the list shows
    });
    add(m_unversioned);
    gap(6);
    m_changesDivider = hairline(Qt::Vertical);
    m_changesDivider->setFixedHeight(space(18));
    m_changesTools->addWidget(m_changesDivider, 0, Qt::AlignVCenter);
    gap(7);
    auto *refreshButton = iconButton(kRefresh, tr("R"), tr("Re-read the repository (F5)"));
    connect(refreshButton, &QToolButton::clicked, this, &CommitPage::refreshRequested);
    add(refreshButton);
    return m_changesTools;
}

// The tree over the same proxy: three columns, the two narrow ones fixed at
// the design's 30 px and Name taking the rest.
QWidget *CommitPage::buildChangesTree()
{
    m_treeModel = new ChangesTreeModel(m_proxy, this);
    auto *tree = new ChangesTree;
    m_tree = tree;
    tree->setModel(m_treeModel);
    // Before the columns: a view hands its section settings to the header it
    // has at the time.
    tree->setHeader(new ChangesHeader(tree));
    QHeaderView *header = tree->header();
    // setHeader() turns the sections' clicks off with the sorting a tree does
    // not do for itself; the header needs them for check-all and for routing
    // a sort to the flat list below.
    header->setSectionsClickable(true);
    header->setSectionsMovable(false);
    header->setHighlightSections(false);
    header->setStretchLastSection(false);
    header->setSectionResizeMode(ChangesTreeModel::Check, QHeaderView::Fixed);
    header->setSectionResizeMode(ChangesTreeModel::Name, QHeaderView::Stretch);
    header->setSectionResizeMode(ChangesTreeModel::Status, QHeaderView::Fixed);
    tree->setItemDelegateForColumn(ChangesTreeModel::Check, new CheckColumnDelegate(tree));
    tree->setItemDelegateForColumn(ChangesTreeModel::Name, new TreeNameDelegate(tree, m_treeModel));
    tree->setItemDelegateForColumn(ChangesTreeModel::Status, new StatusPillDelegate(tree));
    applyTreeMetrics(); // the design's widths, before anything is laid out

    // Sorting is the flat list's: Name and St route to the columns the table
    // sorts by, and the order the proxy settles on is the order the tree's
    // files are rebuilt in. Check is a column of checkboxes and sorts nothing
    // — the header answers a click on it with check-all and never gets here.
    connect(header, &QHeaderView::sectionClicked, this, [this](int section) {
        const int flat = section == ChangesTreeModel::Name     ? int(ChangesModel::Name)
                         : section == ChangesTreeModel::Status ? int(ChangesModel::Status)
                                                               : -1;
        if (flat < 0)
            return;
        // Through the table, not the proxy: the table's header is where the
        // sort of the flat list is kept, so a sort made from the tree is the
        // one the table's own sections go on toggling afterwards.
        const QHeaderView *flatHeader = m_table->horizontalHeader();
        const bool again = flatHeader->sortIndicatorSection() == flat
            && flatHeader->sortIndicatorOrder() == Qt::AscendingOrder;
        m_table->sortByColumn(flat, again ? Qt::DescendingOrder : Qt::AscendingOrder);
    });

    // A rebuild throws the rows away and makes them again: the collapsed set
    // and the row the keyboard was on are put back the moment they exist,
    // before whatever caused the rebuild has returned.
    connect(m_treeModel, &QAbstractItemModel::modelAboutToBeReset, this, [this] {
        rememberTreeCurrent(m_tree->currentIndex());
        m_restoringTree = true; // the view's own current-row churn is nobody's choice
    });
    connect(m_treeModel, &ChangesTreeModel::reloaded, this, &CommitPage::restoreTreeState);
    connect(tree, &QTreeView::expanded, this, [this](const QModelIndex &index) { onTreeExpanded(index, true); });
    connect(tree, &QTreeView::collapsed, this, [this](const QModelIndex &index) { onTreeExpanded(index, false); });
    connect(tree->selectionModel(), &QItemSelectionModel::currentChanged, this, &CommitPage::onTreeCurrentChanged);
    connect(tree, &QTreeView::doubleClicked, this, [this](const QModelIndex &index) {
        // A directory only opens and closes; that is Qt's own double-click.
        if (m_treeModel->isDirectory(index))
            return;
        if (m_diffPaneVisible)
            emit openRequested();
        else
            emit showDiffPaneRequested();
    });
    tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree, &QTreeView::customContextMenuRequested, this,
            [this](const QPoint &pos) { showFileMenu(m_tree, m_tree->indexAt(pos), pos); });
    return tree;
}

// The action bar: "Amend last commit" at the left, where it stands by the
// Commit button it renames, and that button at the right. Stacked, the
// options button stands at the left instead, with Amend in its menu, and
// Commit takes the rest of the row.
QLayout *CommitPage::buildActionBar()
{
    m_actionBar = new QHBoxLayout;
    m_actionBar->setContentsMargins(0, 0, 0, 0);
    m_actionBar->setSpacing(sectionGap());
    m_optionsButton = iconButton(kDotsHorizontal, QStringLiteral("…"), tr("Options"), IconButtonSize::Toolbar, false);
    m_optionsButton->setAccessibleName(tr("Options"));
    m_optionsButton->setPopupMode(QToolButton::InstantPopup);
    m_optionsMenu = new TickMenu(m_optionsButton);
    m_optionsMenu->setToolTipsVisible(true);
    m_optionsButton->setMenu(m_optionsMenu);
    connect(m_optionsMenu, &QMenu::aboutToShow, this, &CommitPage::fillOptionsMenu);
    keepMenuInWindow(m_optionsMenu, m_optionsButton); // the row is at the window's bottom
    m_optionsButton->hide(); // until the window stacks
    m_actionBar->addWidget(m_optionsButton);
    m_amend = new QCheckBox(tr("Amend last commit"));
    // The label is the row's to shorten (updateAmendLabel), so the long one is
    // no floor under the page: a checkbox asks for its whole label and never
    // less, so this one asks for nothing and is capped at what it needs.
    m_amend->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    connect(m_amend, &QCheckBox::toggled, this, &CommitPage::onAmendToggled);
    m_actionBar->addWidget(m_amend, 1);
    m_actionStretch = new QSpacerItem(0, 0, QSizePolicy::Expanding, QSizePolicy::Minimum); // addStretch()'s own
    m_actionBar->addItem(m_actionStretch);
    m_commitButton = new QPushButton;
    m_commitButton->setDefault(true);
    m_commitButton->setCursor(Qt::PointingHandCursor);
    // No shortcut of its own: Ctrl+Enter is the window's, which presses this
    // button, or opens the commit popover in the Mini layout where the button
    // is hidden.
    connect(m_commitButton, &QPushButton::clicked, this, &CommitPage::commit);
    updateCommitButton(); // its wording counts the checked files
    m_actionBar->addWidget(m_commitButton);
    return m_actionBar;
}

QTreeView *CommitPage::tree() const
{
    return m_tree;
}

QAbstractItemView *CommitPage::activeListView() const
{
    return m_filesView == FilesView::Tree ? static_cast<QAbstractItemView *>(m_tree) : m_table;
}

QString CommitPage::viewKey(FilesView view)
{
    switch (view) {
    case FilesView::Compact: return QStringLiteral("compact");
    case FilesView::Tree: return QStringLiteral("tree");
    case FilesView::Table: break;
    }
    return QStringLiteral("table");
}

CommitPage::FilesView CommitPage::viewFromKey(const QString &key, bool *ok)
{
    if (ok)
        *ok = true;
    if (key == QLatin1String("compact"))
        return FilesView::Compact;
    if (key == QLatin1String("tree"))
        return FilesView::Tree;
    if (key == QLatin1String("table"))
        return FilesView::Table;
    if (ok)
        *ok = false;
    return FilesView::Table; // a saved value nobody recognises is the table
}

// Switching is a change of presentation and nothing besides: the same proxy,
// the same selection model, the same check marks and the same current file —
// which the list coming forward simply scrolls to.
void CommitPage::setFilesView(FilesView view, bool persist)
{
    QToolButton *const button = view == FilesView::Compact ? m_compactButton
        : view == FilesView::Tree                          ? m_treeButton
                                                           : m_tableButton;
    {
        // The group clears whichever was on. Its signals are held back over
        // the press, so a switch made from here is not announced back into
        // this same call with a persistence of the group's own choosing.
        const QSignalBlocker quiet(m_viewButtons);
        button->setChecked(true);
    }
    // Picking the presentation already on is nothing at all: not a saved
    // choice, not a reveal, not a focus change.
    if (m_filesView == view)
        return;
    if (persist && !m_filesViewLocked)
        QSettings().setValue(settings::kWindowFilesView, viewKey(view));
    const bool hadFocus = m_table->hasFocus() || m_tree->hasFocus();
    m_filesView = view;
    m_tableSetup->setCompact(view == FilesView::Compact);
    m_listStack->setCurrentWidget(view == FilesView::Tree ? static_cast<QWidget *>(m_tree) : m_table);
    const QString path = currentPath();
    if (view == FilesView::Tree) {
        static_cast<ChangesTree *>(m_tree)->layoutNow();
        // Nothing is made current that was not current already: a list with
        // no file in it stays that way.
        if (!path.isEmpty())
            revealInTree(path, true);
    } else if (m_table->currentIndex().isValid()) {
        m_table->scrollTo(m_table->currentIndex(), QAbstractItemView::EnsureVisible);
    }
    if (hadFocus)
        activeListView()->setFocus();
}

void CommitPage::setFilesViewOverride(FilesView view)
{
    m_filesViewLocked = true; // nothing this run does writes window/filesView
    setFilesView(view, false);
}

QString CommitPage::currentPath() const
{
    const QModelIndex current = m_table->currentIndex();
    return current.isValid() ? current.data(ChangesModel::PathRole).toString() : QString();
}

// A file in the tree becomes the canonical current file, which is what drives
// the diff and the file actions; a directory is a place in the tree and
// leaves the canonical file — and the diff — where they are.
void CommitPage::onTreeCurrentChanged(const QModelIndex &index)
{
    if (m_restoringTree)
        return;
    rememberTreeCurrent(index);
    if (m_syncingCurrent || !index.isValid() || m_treeModel->isDirectory(index))
        return;
    const QModelIndex flat = m_treeModel->mapToSource(index.siblingAtColumn(ChangesTreeModel::Check));
    // The file, not the row: moving between the columns of one file is no
    // change of file, and must not make the window read its diff again.
    if (!flat.isValid() || currentPath() == m_treeCurrentPath)
        return;
    QScopedValueRollback<bool> guard(m_syncingCurrent, true);
    m_syncedCanonicalPath = m_treeCurrentPath;
    m_table->selectRow(flat.row());
}

void CommitPage::rememberTreeCurrent(const QModelIndex &index)
{
    m_treeCurrentPath = m_treeModel->path(index);
    m_treeCurrentIsDirectory = m_treeModel->isDirectory(index);
}

// The other way round, wherever the change came from — the table, the Mini
// rail, selectPath() or the selection a refresh restores.
void CommitPage::onTableCurrentChanged(const QModelIndex &current)
{
    if (m_syncingCurrent || m_restoringTree)
        return;
    const QString path = current.isValid() ? current.data(ChangesModel::PathRole).toString() : QString();
    // Against the file the tree was last put on, not against the row the
    // keyboard is on: that row may be a directory the user walked to, and a
    // reload empties the table's selection before the window selects the same
    // file again. Neither is a new file, so neither moves the tree.
    if (path.isEmpty() || path == m_syncedCanonicalPath)
        return;
    revealInTree(path, true);
}

void CommitPage::revealInTree(const QString &path, bool makeCurrent)
{
    const QModelIndex index = m_treeModel->indexForPath(path);
    if (!index.isValid())
        return;
    for (QModelIndex up = index.parent(); up.isValid(); up = up.parent())
        m_tree->expand(up); // onTreeExpanded() takes it out of the collapsed set
    if (makeCurrent) {
        QScopedValueRollback<bool> guard(m_syncingCurrent, true);
        m_tree->setCurrentIndex(index);
        m_treeCurrentPath = path;
        m_treeCurrentIsDirectory = false;
        m_syncedCanonicalPath = path;
    }
    m_tree->scrollTo(index, QAbstractItemView::EnsureVisible);
}

// Which directories are folded away is this session's, by exact path: a
// directory that comes and goes with a refresh comes back the way the user
// left it, and none of it is written to the settings.
void CommitPage::onTreeExpanded(const QModelIndex &index, bool expanded)
{
    const QString path = m_treeModel->path(index);
    if (path.isEmpty())
        return;
    if (expanded)
        m_collapsed.remove(path);
    else
        m_collapsed.insert(path);
}

// The nodes have just been built again: put the folded directories and the
// row the keyboard was on back on them, and bring the layout up to date, all
// before the reload that caused this has returned — a scroll offset restored
// after it has to be measured against the rows the user will see.
void CommitPage::restoreTreeState()
{
    const std::function<void(const QModelIndex &)> walk = [&](const QModelIndex &parent) {
        for (int row = 0, rows = m_treeModel->rowCount(parent); row < rows; ++row) {
            const QModelIndex index = m_treeModel->index(row, ChangesTreeModel::Check, parent);
            if (!m_treeModel->isDirectory(index))
                continue;
            // A directory nobody folded is open: that is what a new one is.
            m_tree->setExpanded(index, !m_collapsed.contains(m_treeModel->path(index)));
            walk(index);
        }
    };
    walk(QModelIndex());
    // By the kind it was, not by the name alone: a file and a directory may
    // spell the same path, and the row the keyboard was on is one of them.
    const QModelIndex current = m_treeCurrentPath.isEmpty() ? QModelIndex()
        : m_treeCurrentIsDirectory                          ? m_treeModel->indexForDirectory(m_treeCurrentPath)
                                                            : m_treeModel->indexForPath(m_treeCurrentPath);
    if (current.isValid()) {
        // A current row is scrolled to by the view itself, and scrolling to a
        // row under a folded directory opens every directory above it — which
        // would undo the collapsed set the walk above has just put back. The
        // row is made current without that, and the tree is laid out below.
        const bool autoScroll = m_tree->hasAutoScroll();
        m_tree->setAutoScroll(false);
        m_tree->setCurrentIndex(current);
        m_tree->setAutoScroll(autoScroll);
    } else {
        m_treeCurrentPath.clear(); // it is gone from the list; nothing to put back
        m_treeCurrentIsDirectory = false;
    }
    // A canonical file that left the list is no longer what the tree is
    // synchronised to: should it come back, it is a new file to reveal.
    if (!m_treeModel->indexForPath(m_syncedCanonicalPath).isValid())
        m_syncedCanonicalPath.clear();
    m_restoringTree = false;
    static_cast<ChangesTree *>(m_tree)->layoutNow();
}

void CommitPage::showFileMenu(QAbstractItemView *view, const QModelIndex &index, const QPoint &pos)
{
    if (!index.isValid())
        return; // empty space has no file menu, and neither has a directory
    QModelIndex flat = index;
    if (view == m_tree) {
        if (m_treeModel->isDirectory(index))
            return;
        flat = m_treeModel->mapToSource(index.siblingAtColumn(ChangesTreeModel::Check));
        if (!flat.isValid())
            return;
    }
    // The actions are the clicked file's, so the click makes it current first.
    view->setCurrentIndex(index);
    const FileChange &c = m_model->change(m_proxy->mapToSource(flat).row());
    const QString path = QDir(m_repo->root()).filePath(c.path);
    const DefaultApp app = c.kind == FileChange::Deleted ? DefaultApp() : defaultAppFor(path);
    QMenu menu(view);
    QAction *open = menu.addAction(app.name.isEmpty() ? tr("Open") : tr("Open with %1").arg(app.name));
    if (!app.icon.isEmpty())
        open->setIcon(QIcon::fromTheme(app.icon));
    open->setEnabled(c.kind != FileChange::Deleted);
    open->setToolTip(c.kind == FileChange::Deleted ? tr("The file no longer exists") : path);
    connect(open, &QAction::triggered, this, &CommitPage::openRequested);
    menu.addSeparator();
    QAction *discard = menu.addAction(tr("Discard changes"));
    discard->setToolTip(c.isUntracked() ? tr("Delete this untracked file")
                                      : tr("Restore this file to the latest commit, including staged changes"));
    connect(discard, &QAction::triggered, this, [this, change = c] { emit discardRequested(change); });
    menu.setToolTipsVisible(true);
    menu.exec(view->viewport()->mapToGlobal(pos));
}

void CommitPage::applyTheme()
{
    const OmarchyTheme *theme = OmarchyTheme::instance();
    m_message->setFont(theme->uiFont());
    m_message->setMinimumHeight(space(kMessageShallow));
    m_message->applyTheme();
    m_tableSetup->applyTheme();
    // The gaps of the section grid are in scaled pixels, so a new text size
    // has to lay them out again.
    layout()->setSpacing(actionBarGap());
    m_messageRow->setContentsMargins(messageRowMargins());
    m_changesRow->setContentsMargins(0, 0, space(kHeaderInset), 0);
    m_sectionsLayout->setSpacing(headerGap());
    m_changesLayout->setSpacing(headerGap());
    m_actionBar->setSpacing(actionBarStacked() ? space(kStackedActionGap) : sectionGap());
    m_optionsButton->setText(icon(kDotsHorizontal, QStringLiteral("…")).trimmed());
    applyRestingMessageHeight(); // in the pixels of the new text size
    m_messageSplitter->setHandleWidth(sectionGap());
    m_changesDivider->setFixedHeight(space(18));
    m_toolsDivider->setFixedHeight(space(18));
    for (const auto &spacer : std::as_const(m_toolSpacers))
        spacer.first->changeSize(space(spacer.second), 0, QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_changesTools->invalidate();
    applyTreeMetrics();
    // The glyphs of the generate and Commit buttons are looked up in the font
    // of the moment; both go through the helpers that tell the popover.
    if (!m_agent->running())
        setGenerating(false);
    updateCommitButton(); // also re-fits the amend label, whose width moved with the font
}

// The tree's own design pixels: the two narrow columns and the row height. It
// follows the text size whether it is the list on show or not.
void CommitPage::applyTreeMetrics()
{
    m_tree->header()->setMinimumSectionSize(space(kNarrowColumn));
    m_tree->setColumnWidth(ChangesTreeModel::Check, space(kNarrowColumn));
    m_tree->setColumnWidth(ChangesTreeModel::Status, space(kNarrowColumn));
    m_tree->header()->setFixedHeight(tableHeaderHeight());
    static_cast<ChangesTree *>(m_tree)->refreshRowHeights();
}

void CommitPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateAmendLabel();
}

// The action bar is one row: where the page is too narrow to hold the whole
// checkbox beside the Commit button, the label drops to its short form.
void CommitPage::updateAmendLabel()
{
    const QString full = tr("Amend last commit");
    const QFontMetrics fm(m_amend->font());
    // What the checkbox costs beyond its text (the box and the spacing), so
    // the shortened label is still measured against the full one.
    const int chrome = m_amend->sizeHint().width() - fm.horizontalAdvance(m_amend->text());
    const int wide = chrome + fm.horizontalAdvance(full) + m_actionBar->spacing() + m_commitButton->sizeHint().width();
    // The medium width class says "Amend" whatever the page's width, as the
    // design does; the wider ones spell it out wherever it fits.
    m_amend->setText(width() >= wide && m_widthClass != WidthClass::Medium ? full : tr("Amend"));
    // Only the label and its box answer a click, not the empty half of the row.
    m_amend->setMaximumWidth(m_amend->sizeHint().width());
}

void CommitPage::setStacked(bool on)
{
    if (m_stacked == on)
        return;
    m_stacked = on;
    applyActionBarForm();
    updateCommitButton(); // the key comes off or back on
    // The width's own files view, while nobody has picked one: a saved choice
    // (readable or not) and a --files-view run are the user's, and stay.
    if (!m_filesViewLocked && !QSettings().contains(settings::kWindowFilesView)) {
        // Compact and Table are the same table, so the scroll offset carries
        // over; setFilesView() would scroll to the current row instead.
        const QPoint offset = scrollOffset();
        setFilesView(on ? FilesView::Compact : FilesView::Table, false);
        setScrollOffset(offset);
    }
}

void CommitPage::setWindowClass(WidthClass width, bool shallow)
{
    if (m_widthClass == width && m_shallow == shallow)
        return;
    const bool barWasStacked = actionBarStacked();
    m_widthClass = width;
    m_shallow = shallow;
    if (actionBarStacked() != barWasStacked) {
        applyActionBarForm();
        updateCommitButton(); // the key comes off or back on
    }
    applyRestingMessageHeight();
    updateAmendLabel();
}

int CommitPage::restingMessageHeight() const
{
    if (m_shallow)
        return space(kMessageShallow);
    switch (m_widthClass) {
    case WidthClass::Wide: return space(kMessageWide);
    case WidthClass::Large: return space(kMessageLarge);
    case WidthClass::Medium:
    case WidthClass::Stacked: break;
    }
    return space(kMessageNarrow);
}

// The box goes to the classes' resting height, or to its text where that is
// taller (the growing rule of fitMessage(), up to half of the room) — unless
// the user has dragged the handle, this session or an earlier one: that
// height is theirs, and so is the resting height fitMessage() measured on it.
void CommitPage::applyRestingMessageHeight()
{
    if (m_messageSizedByHand || QSettings().contains(settings::kWindowCommitMessageSplitter))
        return;
    const int rest = restingMessageHeight();
    m_messageRestHeight = rest;
    const QList<int> sizes = m_messageSplitter->sizes();
    const int total = sizes.size() == 2 ? sizes.at(0) + sizes.at(1) : 0;
    if (total <= 0 || !m_messageSplitter->isVisible()) {
        // Not laid out yet: the changes list takes whatever the first layout
        // adds (its stretch), so the box comes out at `rest`.
        m_messageSplitter->setSizes({rest, qMax(1, total - rest)});
        return;
    }
    const int wanted = qMax(rest, qMin(m_message->contentHeight(), total / 2));
    if (wanted != sizes.at(0))
        m_messageSplitter->setSizes({wanted, qMax(1, total - wanted)});
}

// Stacked (or shallow): the options button, the design's 6 px, and Commit
// stretching over what Amend and the stretch between them had. Otherwise the
// row as built: Amend and its stretch, the section gap, and Commit at its own
// width.
void CommitPage::applyActionBarForm()
{
    const bool stacked = actionBarStacked();
    m_optionsButton->setVisible(stacked);
    m_amend->setVisible(!stacked);
    if (stacked)
        m_actionStretch->changeSize(0, 0, QSizePolicy::Fixed, QSizePolicy::Fixed);
    else
        m_actionStretch->changeSize(0, 0, QSizePolicy::Expanding, QSizePolicy::Minimum);
    m_actionBar->setStretchFactor(m_commitButton, stacked ? 1 : 0);
    m_commitButton->setSizePolicy(stacked ? QSizePolicy::Expanding : QSizePolicy::Minimum, QSizePolicy::Fixed);
    m_actionBar->setSpacing(stacked ? space(kStackedActionGap) : sectionGap());
    m_actionBar->invalidate();
    updateAmendLabel();
}

// What the stacked row keeps behind "…": the page's own controls, read as
// they are at the moment the menu opens, each entry taking the path the
// control itself takes.
void CommitPage::fillOptionsMenu()
{
    m_optionsMenu->clear();
    m_optionsMenu->setFixedWidth(popupWidth(window(), kOptionsMenuWidth));
    QAction *unversioned = m_optionsMenu->addAction(icon(kEye) + tr("Show unversioned files"));
    unversioned->setCheckable(true);
    unversioned->setChecked(m_unversioned->isChecked());
    unversioned->setToolTip(m_unversioned->toolTip());
    connect(unversioned, &QAction::triggered, m_unversioned, &QAbstractButton::click);

    QAction *amend = m_optionsMenu->addAction(icon(kUndo, QStringLiteral("A  ")) + tr("Amend last commit"));
    amend->setCheckable(true);
    amend->setChecked(m_amend->isChecked());
    amend->setEnabled(m_amend->isEnabled());
    amend->setToolTip(m_amend->toolTip());
    connect(amend, &QAction::triggered, m_amend, &QAbstractButton::click);
}

FileChange CommitPage::currentChange(bool *ok) const
{
    const QModelIndex idx = m_table->currentIndex();
    if (!idx.isValid()) {
        *ok = false;
        return FileChange();
    }
    *ok = true;
    return m_model->change(m_proxy->mapToSource(idx).row());
}

void CommitPage::reload()
{
    m_model->setChanges(m_repo->status());
    // Check marks survive a reload by path, so a checked file that has become
    // unversioned since the last one would come back checked behind the eye.
    untickHidden();
}

// A file nobody can see is a file nobody meant to commit: whatever ticks
// files by path — a reload, the paths of HEAD for an amend — runs this after
// it, so nothing the eye hides stays ticked.
void CommitPage::untickHidden()
{
    if (!m_unversioned->isChecked())
        m_model->setUnversionedChecked(false);
}

QStringList CommitPage::paths() const
{
    QStringList paths;
    paths.reserve(m_model->count());
    for (int i = 0; i < m_model->count(); ++i)
        paths << m_model->change(i).path;
    return paths;
}

bool CommitPage::selectPath(const QString &path)
{
    for (int r = 0; r < m_proxy->rowCount(); ++r) {
        const int src = m_proxy->mapToSource(m_proxy->index(r, 0)).row();
        if (m_model->change(src).path != path)
            continue;
        if (m_table->currentIndex().row() != r) // unchanged rows keep their current cell
            m_table->selectRow(r);
        return true;
    }
    return false;
}

bool CommitPage::selectFirstRow()
{
    if (m_proxy->rowCount() == 0)
        return false;
    m_table->selectRow(0);
    return true;
}

void CommitPage::selectFirstConflict()
{
    for (int r = 0; r < m_proxy->rowCount(); ++r) {
        if (m_model->change(m_proxy->mapToSource(m_proxy->index(r, 0)).row()).kind == FileChange::Unmerged) {
            m_table->selectRow(r);
            break;
        }
    }
}

QPoint CommitPage::scrollOffset() const
{
    QAbstractItemView *const view = activeListView();
    return QPoint(view->horizontalScrollBar()->value(), view->verticalScrollBar()->value());
}

void CommitPage::setScrollOffset(const QPoint &offset)
{
    QAbstractItemView *const view = activeListView();
    // The tree's rows have to be laid out before its scroll range is: a
    // layout still queued would clamp the offset against a folded tree.
    if (view == m_tree)
        static_cast<ChangesTree *>(m_tree)->layoutNow();
    view->verticalScrollBar()->setValue(offset.y());
    view->horizontalScrollBar()->setValue(offset.x());
}

void CommitPage::toggleAllChecked()
{
    // The very path the check-all box in the header takes, over the same rows
    // (the eye may be hiding some): all but the last file checked still means
    // "check them all".
    const bool all =
        m_proxy->headerData(ChangesModel::Check, Qt::Horizontal, Qt::CheckStateRole).toInt() == Qt::Checked;
    m_proxy->setHeaderData(ChangesModel::Check, Qt::Horizontal, int(all ? Qt::Unchecked : Qt::Checked),
                           Qt::CheckStateRole);
}

void CommitPage::toggleAmend()
{
    if (m_amend->isEnabled())
        m_amend->click();
}

void CommitPage::setAmendChecked(bool on)
{
    if (m_amend->isEnabled())
        m_amend->setChecked(on);
}

void CommitPage::resetAmend()
{
    {
        // onAmendToggled(false) below also drops its message from the box and
        // makes the window refresh, so the box itself changes quietly.
        QSignalBlocker blocker(m_amend);
        m_amend->setChecked(false);
    }
    onAmendToggled(false);
}

void CommitPage::checkHeadPaths()
{
    m_model->setPathsChecked(m_repo->headPaths(), true);
    // HEAD may have deleted a file that is back as an unversioned one.
    untickHidden();
}

void CommitPage::clickCommit()
{
    m_commitButton->click();
}

// A merge in progress rules out amending, renames the Commit button, and
// offers git's own message for the merge commit.
void CommitPage::setMergeState(const MergeState &merge, const Commit &head)
{
    m_merging = merge.inProgress;
    m_amend->setEnabled(head.isValid() && !merge.inProgress);
    m_amend->setToolTip(merge.inProgress ? tr("Not while a merge is in progress")
                        : head.isValid() ? tr("Rewrite the last commit (%1: %2) with the checked files and the message above")
                                               .arg(head.shortHash, head.subject)
                                         : tr("There is no commit to amend yet"));
    updateCommitButton();
    // Git's own message goes in the box while it is empty (or still holds the
    // previous proposal) and leaves with the merge.
    if (merge.inProgress) {
        const QString text = m_message->toPlainText();
        if ((text.trimmed().isEmpty() || text == m_mergeMessage) && text != merge.message)
            m_message->setMessage(merge.message);
        m_mergeMessage = merge.message;
    } else if (!m_mergeMessage.isEmpty()) {
        if (m_message->toPlainText() == m_mergeMessage)
            m_message->clear();
        m_mergeMessage.clear();
    }
}

// The button says how many files it would commit, and ends with the key that
// presses it. The two counts are spelled out: no translation catalogue is
// loaded, so %n would come out as "file(s)".
QString CommitPage::commitWording() const
{
    const int checked = m_model->checkedCount();
    return m_amend->isChecked() ? tr("Amend")
        : m_merging             ? tr("Commit merge")
        : checked == 0          ? tr("Commit")
        : checked == 1          ? tr("Commit 1 file")
                                : tr("Commit %1 files").arg(checked);
}

// The stacked row's button spans the window, so the key would sit far from
// the wording; the design leaves it off there, and the card keeps it.
QString CommitPage::commitButtonText(bool withKey) const
{
    return icon(kCommit) + commitWording() + (withKey ? QStringLiteral("  ⏎") : QString());
}

void CommitPage::updateCommitButton()
{
    const bool amend = m_amend->isChecked();
    const QString what = commitWording();
    m_commitButton->setText(commitButtonText(!actionBarStacked()));
    // Read out as the wording alone: the glyph and the key that presses it are
    // no part of the name of the button.
    m_commitButton->setAccessibleName(what);
    m_commitButton->setToolTip(amend ? tr("Rewrite the last commit with the checked files (Ctrl+Enter)")
                               : m_merging ? tr("Finish the merge: commit the checked (resolved) files together with what git merged on its own (Ctrl+Enter)")
                                           : tr("Commit the checked files (Ctrl+Enter)"));
    updateAmendLabel(); // a longer button leaves the checkbox less room
    emit commitControlsChanged();
}

void CommitPage::onCheckedChanged()
{
    // One count for the title, the button and its wording: while the eye
    // hides the unversioned files none of them is checked (the eye itself and
    // every reload see to that), so what the shown files have ticked is
    // exactly what a commit would take.
    const int shown = m_proxy->rowCount();
    const int checked = m_model->checkedCount();
    m_changesLabel->setText((shown == 0 ? tr("Changes") : tr("Changes · %1/%2").arg(checked).arg(shown)).toUpper());
    m_commitButton->setEnabled(checked > 0);
    updateCommitButton();
}

// Amend: the message box gets the last commit's message
// and the changes list is compared against the commit before it, so the files
// of the last commit show up (checked) next to the new changes.
void CommitPage::onAmendToggled(bool on)
{
    m_repo->setAmend(on);
    if (on) {
        m_headMessage = m_repo->headMessage();
        if (m_message->toPlainText().trimmed().isEmpty())
            m_message->setMessage(m_headMessage);
    } else if (m_message->toPlainText() == m_headMessage) {
        m_message->clear();
    }
    updateCommitButton();
    emit amendToggled(on); // the window refreshes and ticks the files of the commit
}

bool CommitPage::commit()
{
    const QString message = m_message->toPlainText().trimmed();
    if (message.isEmpty()) {
        QMessageBox::warning(this, tr("Commit"), tr("Please enter a commit message."));
        m_message->setFocus();
        return false;
    }
    const QStringList paths = m_model->checkedPaths();
    const bool amend = m_amend->isChecked();
    if (amend) {
        const QStringList published = m_repo->remoteBranchesContainingHead();
        if (!published.isEmpty()) {
            const auto answer = QMessageBox::warning(
                this, tr("Amend last commit"),
                tr("The last commit is already part of %1.\n\nAmending it rewrites published history; "
                   "you will have to force-push, and others who have it must rebase.\n\nAmend anyway?")
                    .arg(published.join(QStringLiteral(", "))),
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
            if (answer != QMessageBox::Yes)
                return false;
        }
    }
    QString error;
    const bool ok = amend ? m_repo->amendCommit(message, paths, &error) : m_repo->commit(message, paths, &error);
    if (!ok) {
        QMessageBox::critical(this, amend ? tr("Amend failed") : tr("Commit failed"),
                              error.isEmpty() ? tr("git commit failed.") : error);
        return false;
    }
    const int count = m_model->checkedCount();
    const bool merged = m_merging;
    m_message->clear();
    if (amend) {
        m_amend->setChecked(false); // also refreshes
        emit statusMessage(tr("Amended the last commit on %1 with %2 file(s)").arg(m_repo->branch()).arg(count), 5000);
    } else if (merged) {
        emit statusMessage(tr("Merge committed on %1").arg(m_repo->branch()), 5000);
        emit refreshRequested();
    } else {
        emit statusMessage(tr("Committed %1 file(s) to %2").arg(count).arg(m_repo->branch()), 5000);
        emit refreshRequested();
    }
    return true;
}

// ---- Commit message from a coding agent -----------------------------------

void CommitPage::setGenerating(bool on)
{
    const AgentChoice choice = CommitMessageAgent::savedChoice();
    const AgentSpec agent = CommitMessageAgent::spec(choice.agent);
    if (on) {
        m_spinnerFrame = 0;
        setGenerateFace(spinnerFrames(m_message->font()).first(),
                        tr("%1 is writing the message… click to stop").arg(agent.name));
        m_spinner->start();
    } else {
        m_spinner->stop();
        const QString sparkle = icon(kSparkle, QStringLiteral("✨")).trimmed();
        if (agent.isValid()) {
            const QString model = choice.model.isEmpty() ? tr("default model") : choice.model;
            setGenerateFace(sparkle, tr("Let %1 (%2) write a commit message for the checked changes (Ctrl+G)").arg(agent.name, model));
        } else {
            setGenerateFace(sparkle, tr("Write a commit message with a coding agent — none is installed (Ctrl+G)"));
        }
    }
}

void CommitPage::setGenerateFace(const QString &text, const QString &tip)
{
    QToolButton *b = m_message->cornerButton();
    b->setText(text);
    b->setToolTip(tip);
    emit commitControlsChanged();
}

void CommitPage::generateMessage()
{
    if (m_agent->running()) {
        m_agent->cancel();
        setGenerating(false);
        emit statusMessage(tr("Stopped"), 3000);
        return;
    }
    emit modeRequested();
    const AgentChoice choice = CommitMessageAgent::savedChoice();
    if (choice.agent.isEmpty()) {
        emit statusMessage(tr("Neither claude nor codex is installed — `omarchy default agent claude` sets one up"), 8000);
        return;
    }
    // The checked files are what the message is for; with nothing checked,
    // everything in the list (as an editor describes all changes when
    // nothing is staged).
    QList<FileChange> changes = m_model->checkedChanges();
    if (changes.isEmpty()) {
        for (int i = 0; i < m_model->count(); ++i) {
            const FileChange &c = m_model->change(i);
            if (m_unversioned->isChecked() || !c.isUntracked())
                changes << c;
        }
    }
    if (changes.isEmpty()) {
        emit statusMessage(tr("No changes to describe"), 4000);
        return;
    }
    QString diff = tr("Branch: %1\n").arg(m_repo->branch());
    if (m_amend->isChecked() && !m_headMessage.isEmpty())
        diff += tr("The message of the commit being amended (rewrite it to cover the whole change):\n%1\n").arg(m_headMessage);
    diff += QLatin1Char('\n') + m_repo->patch(changes);
    m_streaming = false;
    m_messageBefore = m_message->toPlainText();
    setGenerating(true);
    emit statusMessage(tr("Asking %1 for a commit message…").arg(CommitMessageAgent::spec(choice.agent).name), 0);
    m_agent->generate(choice, m_repo->root(), diff);
    // The snapshot's `generating` is the process itself, which only exists
    // now: the spinner above said so before it did.
    emit commitControlsChanged();
}

void CommitPage::onMessageGenerated(bool ok, const QString &text)
{
    setGenerating(false);
    if (!ok) {
        // Whatever the agent streamed before failing is not a message.
        if (m_streaming)
            m_message->replaceText(m_messageBefore, true);
        emit statusMessage(tr("No commit message: %1").arg(text), 12000);
        return;
    }
    m_message->replaceText(text, m_streaming);
    m_message->setFocus();
    emit statusMessage(tr("Commit message written by %1").arg(CommitMessageAgent::spec(CommitMessageAgent::savedChoice().agent).name),
                       4000);
}

// The agent popover's choices land here: the page's own generate button names
// the agent and the model, so it is told as the choice is saved.
void CommitPage::applyAgentChoice(const AgentChoice &choice)
{
    CommitMessageAgent::saveChoice(choice);
    setGenerating(m_agent->running());
}
