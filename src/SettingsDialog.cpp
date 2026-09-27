#include "SettingsDialog.h"
#include "NautilusMenu.h"
#include "OmarchyTheme.h"
#include "UiHelpers.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>

namespace {
// As wide as the sign-in dialog; a narrower window gets a narrower dialog,
// the way the merge view fits itself (fitWidth()).
constexpr int kDialogWidth = 480;

// A colour set by hand on a label, remembered so that the same colour twice
// does not re-polish the widget (CloneDialog does the same). An invalid
// colour hands the label back to the stylesheet's own dim rule.
void setTextColor(QLabel *label, QString *applied, const QColor &color)
{
    const QString sheet = color.isValid() ? QStringLiteral("color: %1;").arg(color.name()) : QString();
    if (*applied == sheet)
        return;
    *applied = sheet;
    label->setStyleSheet(sheet);
}
} // namespace

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Settings"));
    setObjectName(QStringLiteral("settingsDialog"));
    setWindowModality(Qt::WindowModal);
    setSizeGripEnabled(false);
    buildUi();
    applyTheme();
    connect(OmarchyTheme::instance(), &OmarchyTheme::changed, this, &SettingsDialog::applyTheme);
    // A copy an older build wrote is brought up to date. Nautilus loaded the
    // old one, which restartNeeded() tells from the file's new time.
    QString error;
    if (!nautilusmenu::refreshIfInstalled(&error))
        m_error = error;
    updateNautilusNote();
    m_closeButton->setFocus();
}

void SettingsDialog::buildUi()
{
    // The groups and the button row, a group gap apart (applyTheme() scales
    // every gap); a group is its caption over its controls.
    auto *layout = new QVBoxLayout(this);

    auto *fileManager = m_fileManagerLayout = new QVBoxLayout;
    m_fileManagerCaption = ui::sectionLabel(tr("File manager"));
    fileManager->addWidget(m_fileManagerCaption);
    m_nautilusBox = new QCheckBox(tr("Show “Open in Omagit” in Nautilus"));
    m_nautilusBox->setObjectName(QStringLiteral("nautilusMenuBox"));
    m_nautilusBox->setCursor(Qt::PointingHandCursor);
    m_nautilusBox->setToolTip(tr("Adds a Nautilus extension to %1")
                                  .arg(ui::tildePath(nautilusmenu::extensionPath())));
    m_nautilusBox->setChecked(nautilusmenu::isInstalled());
    connect(m_nautilusBox, &QCheckBox::clicked, this, &SettingsDialog::setNautilusMenu);
    fileManager->addWidget(m_nautilusBox);
    // The note and the restart button hang under the checkbox's text.
    auto *detail = m_nautilusDetail = new QVBoxLayout;
    m_nautilusNote = new QLabel;
    // Not dimLabel, whose captions are bold: the note is the regular small
    // text of its own stylesheet rule.
    m_nautilusNote->setObjectName(QStringLiteral("settingsNote"));
    m_nautilusNote->setWordWrap(true);
    m_nautilusNote->setAccessibleName(tr("Nautilus menu state"));
    detail->addWidget(m_nautilusNote);
    m_restartButton = new QPushButton(tr("Restart Nautilus"));
    m_restartButton->setObjectName(QStringLiteral("restartNautilusButton"));
    m_restartButton->setCursor(Qt::PointingHandCursor);
    m_restartButton->setAutoDefault(false);
    m_restartButton->setToolTip(tr("nautilus -q, then start it again: its open windows close"));
    connect(m_restartButton, &QPushButton::clicked, this, &SettingsDialog::restartNautilus);
    detail->addWidget(m_restartButton, 0, Qt::AlignLeft);
    fileManager->addLayout(detail);
    layout->addLayout(fileManager);
    layout->addStretch(1);

    auto *buttons = m_buttonRow = new QHBoxLayout;
    m_closeButton = new QPushButton(tr("Close"));
    m_closeButton->setCursor(Qt::PointingHandCursor);
    m_closeButton->setDefault(true);
    ui::setPrimary(m_closeButton);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addStretch();
    buttons->addWidget(m_closeButton);
    layout->addLayout(buttons);

    setFixedWidth(ui::space(kDialogWidth));
}

void SettingsDialog::applyTheme()
{
    const OmarchyTheme *t = OmarchyTheme::instance();
    const int pad = ui::space(ui::pad::dialog);
    layout()->setContentsMargins(pad, pad, pad, pad);
    layout()->setSpacing(ui::space(ui::gap::group));
    m_fileManagerLayout->setSpacing(ui::space(ui::gap::caption));
    m_buttonRow->setSpacing(ui::space(ui::gap::item));
    m_fileManagerCaption->setFont(t->captionFont());
    ui::placeOnLine(m_fileManagerCaption, t->captionFont(), ui::box::line);
    m_nautilusDetail->setContentsMargins(ui::space(ui::box::check) + ui::space(ui::gap::check), 0, 0, 0);
    m_nautilusDetail->setSpacing(ui::space(ui::gap::item));
    applyNoteColor();
    fitWidth();
    fitToContent();
}

void SettingsDialog::setNautilusMenu(bool on)
{
    QString error;
    const bool done = on ? nautilusmenu::install(&error) : nautilusmenu::remove(&error);
    m_changed = m_changed || done;
    m_error = done ? QString() : error;
    // The box says what is on disk, whatever was asked.
    m_nautilusBox->setChecked(nautilusmenu::isInstalled());
    updateNautilusNote();
}

// Nautilus loads its extensions when it starts, so it is restarted; the
// restart outlives this dialog (nautilusmenu::restart()). What is left to
// restart for stays until a restart has worked.
void SettingsDialog::restartNautilus()
{
    if (m_restarting)
        return;
    m_restarting = true;
    // The last one's reason is not this one's.
    m_error.clear();
    updateNautilusNote();
    nautilusmenu::restart(this, [this](const QString &error) {
        m_restarting = false;
        if (error.isEmpty())
            m_changed = false;
        m_error = error;
        updateNautilusNote();
    });
}

void SettingsDialog::updateNautilusNote()
{
    const bool installed = nautilusmenu::isInstalled();
    const bool nautilus = nautilusmenu::nautilusFound();
    // Without Nautilus there is nothing to add to, but a copy already there
    // can still be taken away.
    m_nautilusBox->setEnabled(nautilus || installed);
    // Changed here, or on disk since the running Nautilus started (a newer
    // build refreshed it, or another dialog wrote it).
    const bool restartDue = nautilus && nautilusmenu::nautilusRunning()
        && (m_changed || nautilusmenu::restartNeeded());

    QString note;
    if (!m_error.isEmpty())
        note = m_error;
    else if (m_restarting)
        note = tr("Restarting Nautilus…");
    else if (!nautilus)
        note = tr("Nautilus is not installed.");
    else if (!nautilusmenu::pythonSupportFound())
        note = tr("Nautilus needs nautilus-python to load it: omarchy pkg add nautilus-python");
    else if (restartDue)
        note = installed ? tr("Nautilus picks it up once it restarts.") : tr("Nautilus drops it once it restarts.");
    // Only informational: a copy can still be taken away without one.
    else if (!nautilusmenu::omagitFound())
        note = tr("The entry starts an installed omagit, and none is on PATH or in ~/.local/bin — run ./install.sh.");
    else if (installed)
        note = tr("Right-click a folder or file inside a repository in Nautilus to open it here.");
    else
        note = tr("Adds “Open in Omagit” to Nautilus’s right-click menu for folders and files inside a repository.");
    m_nautilusNote->setText(note);
    applyNoteColor();
    m_restartButton->setVisible(restartDue || m_restarting);
    m_restartButton->setEnabled(!m_restarting);
    fitToContent();
}

void SettingsDialog::applyNoteColor()
{
    setTextColor(m_nautilusNote, &m_noteSheet,
                 m_error.isEmpty() ? QColor() : OmarchyTheme::instance()->color(QStringLiteral("red")));
}

// As wide as the design asks, over the window of the moment, and never
// narrower than the layout can take.
void SettingsDialog::fitWidth()
{
    const QWidget *host = parentWidget() ? parentWidget()->window() : nullptr;
    int width = host ? qMin(ui::space(kDialogWidth), host->width() - 2 * ui::windowMargin(host))
                     : ui::space(kDialogWidth);
    QLayout *l = layout();
    l->invalidate();
    width = qMax(width, l->totalMinimumSize().width());
    if (width != this->width() || minimumWidth() != width || maximumWidth() != width)
        setFixedWidth(width);
}

// As tall as its content, like the other dialogs: a note that takes a
// second line or the restart button coming and going moves nothing else.
void SettingsDialog::fitToContent()
{
    QLayout *l = layout();
    l->invalidate();
    l->activate();
    const int content = l->hasHeightForWidth() ? l->totalHeightForWidth(width()) : l->totalSizeHint().height();
    const int target = qMax(content, l->totalMinimumSize().height());
    if (target != height() || maximumHeight() != target)
        setFixedHeight(target);
}

// Fonts and frame widths from the stylesheet are only final once the widgets
// are polished, which is later than the constructor.
void SettingsDialog::showEvent(QShowEvent *event)
{
    ensurePolished();
    fitWidth();
    fitToContent();
    QDialog::showEvent(event);
}
