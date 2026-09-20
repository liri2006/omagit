#include "CommitPage.h"
#include "DesktopExec.h"
#include "MessageEdit.h"
#include "OmarchyTheme.h"
#include "Settings.h"
#include "TickMenu.h"
#include "UiHelpers.h"

#include <QAction>
#include <QCheckBox>
#include <QDir>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QTableView>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

using namespace ui;

namespace {
class UnversionedFilter : public QSortFilterProxyModel
{
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    bool showUnversioned = true;

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override
    {
        if (showUnversioned)
            return true;
        auto *m = static_cast<ChangesModel *>(sourceModel());
        Q_UNUSED(parent)
        return !m->change(row).isUntracked();
    }
};
} // namespace

CommitPage::CommitPage(GitRepo *repo, QWidget *parent)
    : QWidget(parent), m_repo(repo)
{
    setupAgent();

    const OmarchyTheme *theme = OmarchyTheme::instance();
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(headerGap()); // a section header row to its content
    layout->addLayout(buildMessageSection());

    // The message box and the changes list share the height; where the user
    // last put the handle between them is remembered. The handle is all that
    // stands between the two sections, so it carries the gap between them.
    m_messageSplitter = new QSplitter(Qt::Vertical);
    m_messageSplitter->setObjectName(QStringLiteral("commitMessageSplitter"));
    m_messageSplitter->setHandleWidth(sectionGap());
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
    layout->addWidget(m_messageSplitter, 1);
    layout->addLayout(buildButtonRow());
    m_messageSplitter->setSizes({theme->fontBase() * 7, changes->sizeHint().height()});
    m_messageSplitter->restoreState(QSettings().value(settings::kWindowCommitMessageSplitter).toByteArray());
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
        m_message->cornerButton()->setText(frames.at(m_spinnerFrame));
    });
}

// MESSAGE, with the agent settings at the far right; the message box (which
// the caller puts in the splitter) has the generate button in its top right
// corner.
QLayout *CommitPage::buildMessageSection()
{
    auto *messageRow = sectionHeaderRow(sectionLabel(tr("Message")));
    messageRow->addStretch();
    m_agentButton = iconButton(kCog, tr("⚙"), tr("Which coding agent writes the commit message, with which model and reasoning level"));
    connect(m_agentButton, &QToolButton::clicked, this, &CommitPage::showAgentMenu);
    messageRow->addWidget(m_agentButton, 0, Qt::AlignVCenter);

    m_message = new MessageEdit;
    m_message->setPlaceholderText(tr("Commit message"));
    m_message->setMinimumHeight(OmarchyTheme::instance()->fontBase() * 3);
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

// CHANGES, the "n / m selected" count and Refresh, then the options and the
// file list itself.
QWidget *CommitPage::buildChangesSection()
{
    auto *changes = new QWidget;
    auto *changesLayout = new QVBoxLayout(changes);
    changesLayout->setContentsMargins(0, 0, 0, 0);
    changesLayout->setSpacing(headerGap());
    m_changesLayout = changesLayout;

    auto *changesRow = sectionHeaderRow(sectionLabel(tr("Changes")));
    changesRow->addStretch();
    m_summaryLabel = dimLabel();
    changesRow->addWidget(m_summaryLabel, 0, Qt::AlignVCenter);
    auto *refreshButton = iconButton(kRefresh, tr("R"), tr("Re-read the repository (F5)"));
    connect(refreshButton, &QToolButton::clicked, this, &CommitPage::refreshRequested);
    changesRow->addWidget(refreshButton, 0, Qt::AlignVCenter);
    changesLayout->addLayout(changesRow);

    m_model = new ChangesModel(this);
    auto *proxy = new UnversionedFilter(this);
    proxy->setSourceModel(m_model);
    proxy->setSortRole(ChangesModel::SortRole);
    proxy->setSortCaseSensitivity(Qt::CaseInsensitive);
    m_proxy = proxy;

    m_table = new QTableView;
    m_table->setObjectName(QStringLiteral("changesTable"));
    m_table->setModel(m_proxy);
    m_tableSetup = new ChangesTableSetup(m_table);
    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged, this, &CommitPage::currentRowChanged);
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
    connect(m_table, &QTableView::customContextMenuRequested, this, &CommitPage::showFileMenu);

    changesLayout->addLayout(buildOptionsRow());
    changesLayout->addWidget(m_table, 1);
    return changes;
}

// Options above the list (above keeps them
// next to the "n / m selected" count they act on).
QLayout *CommitPage::buildOptionsRow()
{
    auto *optionsRow = new QHBoxLayout;
    optionsRow->setSpacing(16);
    m_showUnversioned = new QCheckBox(tr("Show unversioned files"));
    m_showUnversioned->setChecked(true);
    connect(m_showUnversioned, &QCheckBox::toggled, this, [this](bool on) {
        auto *filter = static_cast<UnversionedFilter *>(m_proxy);
        filter->showUnversioned = on;
        filter->invalidate();
        onCheckedChanged();
    });
    m_selectAll = new QCheckBox(tr("Select all"));
    m_selectAll->setTristate(true);
    connect(m_selectAll, &QCheckBox::clicked, this, [this](bool on) {
        m_selectAll->setTristate(false);
        m_model->setAllChecked(on);
    });
    m_amend = new QCheckBox(tr("Amend last commit"));
    connect(m_amend, &QCheckBox::toggled, this, &CommitPage::onAmendToggled);
    optionsRow->addWidget(m_selectAll);
    optionsRow->addWidget(m_showUnversioned);
    optionsRow->addWidget(m_amend);
    optionsRow->addStretch();
    return optionsRow;
}

QLayout *CommitPage::buildButtonRow()
{
    auto *buttonRow = new QHBoxLayout;
    buttonRow->setSpacing(10);
    buttonRow->addStretch();
    m_commitButton = new QPushButton(icon(kCommit) + tr("Commit"));
    m_commitButton->setDefault(true);
    m_commitButton->setCursor(Qt::PointingHandCursor);
    m_commitButton->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return));
    m_commitButton->setToolTip(tr("Commit the checked files (Ctrl+Enter)"));
    connect(m_commitButton, &QPushButton::clicked, this, &CommitPage::commit);
    buttonRow->addWidget(m_commitButton);
    return buttonRow;
}

void CommitPage::showFileMenu(const QPoint &pos)
{
    const QModelIndex index = m_table->indexAt(pos);
    if (!index.isValid())
        return;
    m_table->setCurrentIndex(index);
    const FileChange &c = m_model->change(m_proxy->mapToSource(index).row());
    const QString path = QDir(m_repo->root()).filePath(c.path);
    const DefaultApp app = c.kind == FileChange::Deleted ? DefaultApp() : defaultAppFor(path);
    QMenu menu(m_table);
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
    menu.exec(m_table->viewport()->mapToGlobal(pos));
}

void CommitPage::applyTheme()
{
    const OmarchyTheme *theme = OmarchyTheme::instance();
    m_message->setFont(theme->uiFont());
    m_message->setMinimumHeight(theme->fontBase() * 3);
    m_message->applyTheme();
    m_tableSetup->applyTheme();
    // The gaps of the section grid are in scaled pixels, so a new text size
    // has to lay them out again.
    layout()->setSpacing(headerGap());
    m_changesLayout->setSpacing(headerGap());
    m_messageSplitter->setHandleWidth(sectionGap());
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
    return QPoint(m_table->horizontalScrollBar()->value(), m_table->verticalScrollBar()->value());
}

void CommitPage::setScrollOffset(const QPoint &offset)
{
    m_table->verticalScrollBar()->setValue(offset.y());
    m_table->horizontalScrollBar()->setValue(offset.x());
}

void CommitPage::toggleAllChecked()
{
    const bool on = m_selectAll->checkState() != Qt::Checked;
    m_selectAll->setTristate(false);
    m_model->setAllChecked(on);
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

void CommitPage::updateCommitButton()
{
    const bool amend = m_amend->isChecked();
    m_commitButton->setText(icon(kCommit) + (amend ? tr("Amend") : m_merging ? tr("Commit merge") : tr("Commit")));
    m_commitButton->setToolTip(amend ? tr("Rewrite the last commit with the checked files (Ctrl+Enter)")
                               : m_merging ? tr("Finish the merge: commit the checked (resolved) files together with what git merged on its own (Ctrl+Enter)")
                                           : tr("Commit the checked files (Ctrl+Enter)"));
}

void CommitPage::onCheckedChanged()
{
    const int checked = m_model->checkedCount();
    const int total = m_model->count();
    m_summaryLabel->setText(tr("%1 / %2 selected").arg(checked).arg(total));
    m_commitButton->setEnabled(checked > 0);
    QSignalBlocker blocker(m_selectAll);
    if (checked == 0)
        m_selectAll->setCheckState(Qt::Unchecked);
    else if (checked == total)
        m_selectAll->setCheckState(Qt::Checked);
    else
        m_selectAll->setCheckState(Qt::PartiallyChecked);
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

void CommitPage::commit()
{
    const QString message = m_message->toPlainText().trimmed();
    if (message.isEmpty()) {
        QMessageBox::warning(this, tr("Commit"), tr("Please enter a commit message."));
        m_message->setFocus();
        return;
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
                return;
        }
    }
    QString error;
    const bool ok = amend ? m_repo->amendCommit(message, paths, &error) : m_repo->commit(message, paths, &error);
    if (!ok) {
        QMessageBox::critical(this, amend ? tr("Amend failed") : tr("Commit failed"),
                              error.isEmpty() ? tr("git commit failed.") : error);
        return;
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
}

// ---- Commit message from a coding agent -----------------------------------

void CommitPage::setGenerating(bool on)
{
    QToolButton *b = m_message->cornerButton();
    const AgentChoice choice = CommitMessageAgent::savedChoice();
    const AgentSpec agent = CommitMessageAgent::spec(choice.agent);
    if (on) {
        m_spinnerFrame = 0;
        b->setText(spinnerFrames(m_message->font()).first());
        b->setToolTip(tr("%1 is writing the message… click to stop").arg(agent.name));
        m_spinner->start();
    } else {
        m_spinner->stop();
        b->setText(icon(kSparkle, QStringLiteral("✨")).trimmed());
        if (agent.isValid()) {
            const QString model = choice.model.isEmpty() ? tr("default model") : choice.model;
            b->setToolTip(tr("Let %1 (%2) write a commit message for the checked changes (Ctrl+G)").arg(agent.name, model));
        } else {
            b->setToolTip(tr("Write a commit message with a coding agent — none is installed (Ctrl+G)"));
        }
    }
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
            if (m_showUnversioned->isChecked() || !c.isUntracked())
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

// The cog's menu: AGENT (Claude Code and Codex, whichever is installed),
// MODEL and REASONING as the chosen agent's CLI names them (`claude --help`,
// `codex debug models`; Codex has levels per model), or any model by name.
void CommitPage::showAgentMenu()
{
    TickMenu menu(this);
    menu.setToolTipsVisible(true);
    const AgentChoice choice = CommitMessageAgent::savedChoice();
    const auto save = [this](const AgentChoice &c) {
        CommitMessageAgent::saveChoice(c);
        setGenerating(m_agent->running());
    };

    addAgentSection(&menu, choice, save);
    const AgentSpec current = CommitMessageAgent::spec(choice.agent);
    if (current.isValid()) {
        const AgentCatalog catalog = CommitMessageAgent::catalog(choice.agent);
        addModelSection(&menu, current, catalog, choice, save);
        addReasoningSection(&menu, catalog.effortsFor(choice.model), choice, save);
    }
    // The cog sits at the right edge, so the menu hangs from its right corner.
    menu.exec(m_agentButton->mapToGlobal(QPoint(m_agentButton->width() - menu.sizeHint().width(), m_agentButton->height())));
}

void CommitPage::addAgentSection(QMenu *menu, const AgentChoice &choice,
                                 const std::function<void(const AgentChoice &)> &save)
{
    addMenuHeader(menu, tr("Agent"));
    const QList<AgentSpec> installed = CommitMessageAgent::installedAgents();
    if (installed.isEmpty()) {
        QAction *none = menu->addAction(tr("None installed"));
        none->setEnabled(false);
        none->setToolTip(tr("`omarchy default agent claude` (or codex) installs one"));
    }
    const QString omarchyDefault = CommitMessageAgent::omarchyDefaultAgent();
    for (const AgentSpec &agent : installed) {
        QAction *a = menu->addAction(icon(kRobot) + agent.name);
        a->setCheckable(true);
        a->setChecked(agent.id == choice.agent);
        a->setToolTip(agent.id == omarchyDefault ? tr("%1 — Omarchy's default agent").arg(agent.binary) : agent.binary);
        connect(a, &QAction::triggered, this, [save, agent] {
            // A model and a level belong to the agent they were picked for.
            save(AgentChoice{agent.id, QString(), QString()});
        });
    }
}

void CommitPage::addModelSection(QMenu *menu, const AgentSpec &agent, const AgentCatalog &catalog,
                                 const AgentChoice &choice, const std::function<void(const AgentChoice &)> &save)
{
    menu->addSeparator();
    addMenuHeader(menu, tr("Model"));
    QAction *def = menu->addAction(tr("Default"));
    def->setCheckable(true);
    def->setChecked(choice.model.isEmpty());
    def->setToolTip(tr("Whatever %1 is set to use").arg(agent.name));
    connect(def, &QAction::triggered, this, [save, choice] { save(AgentChoice{choice.agent, QString(), choice.effort}); });
    QList<AgentModel> models = catalog.models;
    const bool known = std::any_of(models.cbegin(), models.cend(), [&](const AgentModel &m) { return m.id == choice.model; });
    if (!choice.model.isEmpty() && !known)
        models.prepend(AgentModel{choice.model, choice.model, {}, {}});
    for (const AgentModel &m : std::as_const(models)) {
        QAction *a = menu->addAction(m.name);
        a->setCheckable(true);
        a->setChecked(m.id == choice.model);
        a->setToolTip(m.id);
        connect(a, &QAction::triggered, this, [save, choice, m] {
            // A level the new model does not have goes back to its default.
            const QString effort = m.efforts.isEmpty() || m.efforts.contains(choice.effort) ? choice.effort : QString();
            save(AgentChoice{choice.agent, m.id, effort});
        });
    }
    if (models.isEmpty() && !catalog.error.isEmpty()) {
        QAction *err = menu->addAction(tr("Could not read the models"));
        err->setEnabled(false);
        err->setToolTip(catalog.error);
    }
    QAction *other = menu->addAction(tr("Other…"));
    other->setToolTip(tr("A model by name, as %1 --model takes it").arg(agent.binary));
    connect(other, &QAction::triggered, this, [this, save, choice, agent] {
        bool ok = false;
        const QString id = QInputDialog::getText(this, tr("Model"), tr("Model name for %1:").arg(agent.name),
                                                 QLineEdit::Normal, choice.model, &ok)
                               .trimmed();
        if (ok)
            save(AgentChoice{choice.agent, id, choice.effort});
    });
}

void CommitPage::addReasoningSection(QMenu *menu, const QStringList &efforts, const AgentChoice &choice,
                                     const std::function<void(const AgentChoice &)> &save)
{
    if (efforts.isEmpty())
        return;
    menu->addSeparator();
    addMenuHeader(menu, tr("Reasoning"));
    QAction *defEffort = menu->addAction(tr("Default"));
    defEffort->setCheckable(true);
    defEffort->setChecked(choice.effort.isEmpty());
    connect(defEffort, &QAction::triggered, this,
            [save, choice] { save(AgentChoice{choice.agent, choice.model, QString()}); });
    for (const QString &level : efforts) {
        QAction *a = menu->addAction(level.at(0).toUpper() + level.mid(1));
        a->setCheckable(true);
        a->setChecked(level == choice.effort);
        connect(a, &QAction::triggered, this,
                [save, choice, level] { save(AgentChoice{choice.agent, choice.model, level}); });
    }
}
