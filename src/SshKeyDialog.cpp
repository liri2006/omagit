#include "SshKeyDialog.h"
#include "OmarchyTheme.h"
#include "SshKeys.h"
#include "UiHelpers.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>

namespace {
// As wide as the sign-in, where the window allows it.
constexpr int kDialogWidth = 480;
// Rows shown before the list scrolls.
constexpr int kVisibleRows = 6;
} // namespace

SshKeyDialog::SshKeyDialog(const QString &repoName, const QString &currentCommand, QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("sshKeyDialog"));
    setWindowTitle(tr("SSH key"));
    setWindowModality(Qt::WindowModal);
    setSizeGripEnabled(false);

    auto *layout = new QVBoxLayout(this);
    auto *head = m_headLayout = new QVBoxLayout;
    head->setContentsMargins(0, 0, 0, 0);
    m_heading = new QLabel(tr("Choose the SSH key for %1").arg(repoName));
    m_heading->setWordWrap(true);
    m_hint = ui::dimLabel(tr("The server turned down the key ssh offered. The key chosen here is the only one "
                             "ssh offers for this repository from now on (core.sshCommand in its git configuration)."));
    m_hint->setWordWrap(true);
    head->addWidget(m_heading);
    head->addWidget(m_hint);
    layout->addLayout(head);

    m_list = new QListWidget;
    m_list->setObjectName(QStringLiteral("sshKeys"));
    m_list->setUniformItemSizes(true);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setTextElideMode(Qt::ElideMiddle);
    addKeyRow(QString(), tr("ssh's choice — its configuration and agent"),
              tr("No core.sshCommand: ssh offers the keys it would anywhere else"));
    for (const sshkeys::Key &key : sshkeys::find()) {
        const QString text = key.comment.isEmpty() ? QStringLiteral("%1 · %2").arg(key.name(), key.type)
                                                   : QStringLiteral("%1 · %2 · %3").arg(key.name(), key.type, key.comment);
        addKeyRow(key.path, text, ui::tildePath(key.path) + QLatin1Char('\n') + key.fingerprint);
    }
    layout->addWidget(m_list);

    // What would get in the way of the choice, said before it is made: an
    // exported GIT_SSH_COMMAND, which git goes by instead of core.sshCommand,
    // or a core.sshCommand of the user's own that the choice would replace.
    QString warning;
    const QString overriding = sshkeys::overridingVariable();
    const QString currentKey = sshkeys::keyOf(currentCommand);
    if (!overriding.isEmpty())
        warning = tr("%1 is set in Omagit's environment, and git goes by it instead of this choice.").arg(overriding);
    else if (!currentCommand.isEmpty() && currentKey.isEmpty())
        warning = tr("This replaces the repository's core.sshCommand: %1").arg(currentCommand);
    m_warning = ui::dimLabel(warning);
    m_warning->setWordWrap(true);
    m_warning->setVisible(!warning.isEmpty());
    layout->addWidget(m_warning);

    auto *buttons = m_buttonRow = new QHBoxLayout;
    m_browse = new QPushButton(tr("Other key file…"));
    m_browse->setCursor(Qt::PointingHandCursor);
    m_browse->setAutoDefault(false);
    connect(m_browse, &QPushButton::clicked, this, &SshKeyDialog::browse);
    m_cancel = new QPushButton(tr("Cancel"));
    m_cancel->setCursor(Qt::PointingHandCursor);
    m_cancel->setAutoDefault(false);
    connect(m_cancel, &QPushButton::clicked, this, &QDialog::reject);
    m_use = new QPushButton(tr("Use key"));
    m_use->setCursor(Qt::PointingHandCursor);
    m_use->setDefault(true);
    ui::setPrimary(m_use);
    connect(m_use, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(m_browse);
    buttons->addStretch();
    buttons->addWidget(m_cancel);
    buttons->addWidget(m_use);
    layout->addLayout(buttons);

    connect(m_list, &QListWidget::itemDoubleClicked, this, &QDialog::accept);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        m_use->setText(row == 0 ? tr("Use ssh's choice") : tr("Use key"));
    });
    selectKey(currentKey);

    applyTheme();
    connect(OmarchyTheme::instance(), &OmarchyTheme::changed, this, &SshKeyDialog::applyTheme);
    m_list->setFocus();
}

int SshKeyDialog::addKeyRow(const QString &path, const QString &text, const QString &tip)
{
    auto *item = new QListWidgetItem(text, m_list);
    item->setData(Qt::UserRole, path);
    item->setToolTip(tip);
    return m_list->count() - 1;
}

QString SshKeyDialog::chosenKey() const
{
    const QListWidgetItem *item = m_list->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void SshKeyDialog::selectKey(const QString &path)
{
    for (int row = 0; row < m_list->count(); ++row) {
        if (m_list->item(row)->data(Qt::UserRole).toString() == path) {
            m_list->setCurrentRow(row);
            return;
        }
    }
    // A key kept somewhere else than ~/.ssh: a row of its own.
    const QFileInfo info(path);
    m_list->setCurrentRow(addKeyRow(path, info.fileName(), ui::tildePath(path)));
    if (isVisible())
        fitToContent();
}

void SshKeyDialog::browse()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("SSH key"), sshkeys::directory());
    if (!path.isEmpty())
        selectKey(path.endsWith(QLatin1String(".pub")) ? path.chopped(4) : path);
}

void SshKeyDialog::applyTheme()
{
    const OmarchyTheme *t = OmarchyTheme::instance();
    const int pad = ui::space(ui::pad::dialog);
    layout()->setContentsMargins(pad, pad, pad, pad);
    layout()->setSpacing(ui::space(ui::gap::group));
    m_headLayout->setSpacing(ui::space(ui::gap::caption));
    m_buttonRow->setSpacing(ui::space(ui::gap::item));
    m_heading->setFont(t->titleFont());
    for (QLabel *label : {m_hint, m_warning})
        label->setFont(t->captionFont());
    m_list->setFont(t->uiFont());
    const QString sheet = QStringLiteral("color: %1;").arg(t->color(QStringLiteral("yellow")).name());
    if (sheet != m_warningColor) {
        m_warningColor = sheet;
        m_warning->setStyleSheet(sheet);
    }
    if (isVisible())
        fitToContent();
}

// As wide as the window it opens over allows, the list as tall as its rows
// up to kVisibleRows, and the dialog as tall as its content.
void SshKeyDialog::fitToContent()
{
    ui::fitDialogWidth(this, kDialogWidth);
    const int rows = qMin(m_list->count(), kVisibleRows);
    const int rowHeight = m_list->sizeHintForRow(0);
    m_list->setFixedHeight(rows * rowHeight + 2 * m_list->frameWidth());
    QLayout *l = layout();
    l->invalidate();
    l->activate();
    const int content = l->hasHeightForWidth() ? l->totalHeightForWidth(width()) : l->totalSizeHint().height();
    const int target = qMax(content, l->totalMinimumSize().height());
    if (target != height() || maximumHeight() != target)
        setFixedHeight(target);
}

void SshKeyDialog::showEvent(QShowEvent *event)
{
    ensurePolished();
    fitToContent();
    QDialog::showEvent(event);
}
