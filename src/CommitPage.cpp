#include "CommitPage.h"
#include "DesktopExec.h"
#include "MessageEdit.h"
#include "OmarchyTheme.h"
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
#include <QPushButton>
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

    const OmarchyTheme *theme = OmarchyTheme::instance();
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    // MESSAGE, with the agent settings at the far right; the message box has
    // the generate button in its top right corner.
    auto *messageRow = new QHBoxLayout;
    messageRow->addWidget(sectionLabel(tr("Message")));
    messageRow->addStretch();
    m_agentButton = smallButton(kCog, tr("⚙"), tr("Which coding agent writes the commit message, with which model and reasoning level"));
    connect(m_agentButton, &QToolButton::clicked, this, &CommitPage::showAgentMenu);
    messageRow->addWidget(m_agentButton);
    layout->addLayout(messageRow);

    m_message = new MessageEdit;
    m_message->setPlaceholderText(tr("Commit message"));
    m_message->setMinimumHeight(theme->fontBase() * 3);

    auto *messageSplitter = new QSplitter(Qt::Vertical);
    messageSplitter->setObjectName(QStringLiteral("commitMessageSplitter"));
    messageSplitter->setHandleWidth(8);
    messageSplitter->setChildrenCollapsible(false);
    messageSplitter->addWidget(m_message);
    auto *changes = new QWidget;
    auto *changesLayout = new QVBoxLayout(changes);
    changesLayout->setContentsMargins(0, 0, 0, 0);
    changesLayout->setSpacing(8);
    messageSplitter->addWidget(changes);
    messageSplitter->setStretchFactor(0, 0);
    messageSplitter->setStretchFactor(1, 1); // the changes list takes window resizes
    connect(messageSplitter, &QSplitter::splitterMoved, this, [messageSplitter] {
        QSettings().setValue(QStringLiteral("window/commitMessageSplitter"), messageSplitter->saveState());
    });
    layout->addWidget(messageSplitter, 1);
    QToolButton *generate = m_message->cornerButton();
    generate->setText(icon(kSparkle, QStringLiteral("✨")).trimmed());
    connect(generate, &QToolButton::clicked, this, &CommitPage::generateMessage);
    setGenerating(false);

    auto *changesRow = new QHBoxLayout;
    changesRow->addWidget(sectionLabel(tr("Changes")));
    changesRow->addStretch();
    m_summaryLabel = dimLabel();
    changesRow->addWidget(m_summaryLabel);
    changesRow->addSpacing(4);
    auto *refreshButton = smallButton(kRefresh, tr("R"), tr("Re-read the repository (F5)"));
    connect(refreshButton, &QToolButton::clicked, this, &CommitPage::refreshRequested);
    changesRow->addWidget(refreshButton);
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
    connect(m_table, &QTableView::customContextMenuRequested, this, [this](const QPoint &pos) {
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
    });

    // Options above the list (above keeps them
    // next to the "n / m selected" count they act on).
    auto *optionsRow = new QHBoxLayout;
    optionsRow->setSpacing(16);
    m_showUnversioned = new QCheckBox(tr("Show unversioned files"));
    m_showUnversioned->setChecked(true);
    connect(m_showUnversioned, &QCheckBox::toggled, this, [this, proxy](bool on) {
        proxy->showUnversioned = on;
        proxy->invalidate();
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
    changesLayout->addLayout(optionsRow);
    changesLayout->addWidget(m_table, 1);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->setSpacing(10);
    buttonRow->addStretch();
    m_commitButton = new QPushButton(icon(kCommit) + tr("Commit"));
    m_commitButton->setDefault(true);
    m_commitButton->setCursor(Qt::PointingHandCursor);
    m_commitButton->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return));
    m_commitButton->setToolTip(tr("Commit the checked files (Ctrl+Enter)"));
    connect(m_commitButton, &QPushButton::clicked, this, &CommitPage::commitRequested);
    buttonRow->addWidget(m_commitButton);
    layout->addLayout(buttonRow);
    messageSplitter->setSizes({theme->fontBase() * 7, changes->sizeHint().height()});
    messageSplitter->restoreState(QSettings().value(QStringLiteral("window/commitMessageSplitter")).toByteArray());
}

void CommitPage::applyTheme()
{
    const OmarchyTheme *theme = OmarchyTheme::instance();
    m_message->setFont(theme->uiFont());
    m_message->setMinimumHeight(theme->fontBase() * 3);
    m_message->applyTheme();
    m_tableSetup->applyTheme();
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

void CommitPage::checkHeadPaths()
{
    m_model->setPathsChecked(m_repo->headPaths(), true);
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
            m_message->setPlainText(m_headMessage);
    } else if (m_message->toPlainText() == m_headMessage) {
        m_message->clear();
    }
    emit amendToggled(on); // the window updates the Commit button, refreshes and ticks the files
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
    AgentChoice choice = CommitMessageAgent::savedChoice();
    const auto save = [this](const AgentChoice &c) {
        CommitMessageAgent::saveChoice(c);
        setGenerating(m_agent->running());
    };

    addMenuHeader(&menu, tr("Agent"));
    const QList<AgentSpec> installed = CommitMessageAgent::installedAgents();
    if (installed.isEmpty()) {
        QAction *none = menu.addAction(tr("None installed"));
        none->setEnabled(false);
        none->setToolTip(tr("`omarchy default agent claude` (or codex) installs one"));
    }
    const QString omarchyDefault = CommitMessageAgent::omarchyDefaultAgent();
    for (const AgentSpec &agent : installed) {
        QAction *a = menu.addAction(icon(kRobot) + agent.name);
        a->setCheckable(true);
        a->setChecked(agent.id == choice.agent);
        a->setToolTip(agent.id == omarchyDefault ? tr("%1 — Omarchy's default agent").arg(agent.binary) : agent.binary);
        connect(a, &QAction::triggered, this, [save, agent] {
            // A model and a level belong to the agent they were picked for.
            save(AgentChoice{agent.id, QString(), QString()});
        });
    }

    const AgentSpec current = CommitMessageAgent::spec(choice.agent);
    if (current.isValid()) {
        const AgentCatalog catalog = CommitMessageAgent::catalog(choice.agent);
        menu.addSeparator();
        addMenuHeader(&menu, tr("Model"));
        QAction *def = menu.addAction(tr("Default"));
        def->setCheckable(true);
        def->setChecked(choice.model.isEmpty());
        def->setToolTip(tr("Whatever %1 is set to use").arg(current.name));
        connect(def, &QAction::triggered, this, [save, choice] { save(AgentChoice{choice.agent, QString(), choice.effort}); });
        QList<AgentModel> models = catalog.models;
        const bool known = std::any_of(models.cbegin(), models.cend(), [&](const AgentModel &m) { return m.id == choice.model; });
        if (!choice.model.isEmpty() && !known)
            models.prepend(AgentModel{choice.model, choice.model, {}, {}});
        for (const AgentModel &m : std::as_const(models)) {
            QAction *a = menu.addAction(m.name);
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
            QAction *err = menu.addAction(tr("Could not read the models"));
            err->setEnabled(false);
            err->setToolTip(catalog.error);
        }
        QAction *other = menu.addAction(tr("Other…"));
        other->setToolTip(tr("A model by name, as %1 --model takes it").arg(current.binary));
        connect(other, &QAction::triggered, this, [this, save, choice, current] {
            bool ok = false;
            const QString id = QInputDialog::getText(this, tr("Model"), tr("Model name for %1:").arg(current.name),
                                                     QLineEdit::Normal, choice.model, &ok)
                                   .trimmed();
            if (ok)
                save(AgentChoice{choice.agent, id, choice.effort});
        });

        const QStringList efforts = catalog.effortsFor(choice.model);
        if (!efforts.isEmpty()) {
            menu.addSeparator();
            addMenuHeader(&menu, tr("Reasoning"));
            QAction *defEffort = menu.addAction(tr("Default"));
            defEffort->setCheckable(true);
            defEffort->setChecked(choice.effort.isEmpty());
            connect(defEffort, &QAction::triggered, this,
                    [save, choice] { save(AgentChoice{choice.agent, choice.model, QString()}); });
            for (const QString &level : efforts) {
                QAction *a = menu.addAction(level.at(0).toUpper() + level.mid(1));
                a->setCheckable(true);
                a->setChecked(level == choice.effort);
                connect(a, &QAction::triggered, this,
                        [save, choice, level] { save(AgentChoice{choice.agent, choice.model, level}); });
            }
        }
    }
    // The cog sits at the right edge, so the menu hangs from its right corner.
    menu.exec(m_agentButton->mapToGlobal(QPoint(m_agentButton->width() - menu.sizeHint().width(), m_agentButton->height())));
}
