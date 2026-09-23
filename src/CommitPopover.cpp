#include "CommitPopover.h"
#include "CommitPage.h"
#include "MessageEdit.h"
#include "MiniRail.h"
#include "OmarchyTheme.h"
#include "UiHelpers.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QCheckBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QMouseEvent>
#include <QPushButton>
#include <QShortcut>
#include <QStyle>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

using namespace ui;

namespace {
// The stylesheet's accent frame (QFrame#commitPopover), two pixels at every
// text size. The design measures its padding from the card's outer edge, so
// the layout's margins are that padding less the frame.
constexpr int kFrame = 2;
// The design's pixels: the card at most 360 wide, 8 clear of the rail, the
// message box at least 84 tall; the padding (10 around, 4 above the header),
// the editor's top 32 below the card's, and the gaps down to the actions.
constexpr int kMaxWidth = 360, kRailGap = 8, kEditorMin = 84;
constexpr int kPad = 10, kTopPad = 4, kEditorTop = 32;
constexpr int kHintGap = 10, kHintHeight = 16, kRuleGap = 10, kActionGap = 12;
} // namespace

CommitPopover::CommitPopover(CommitPage *page, QWidget *host)
    : QFrame(host), m_page(page)
{
    setObjectName(QStringLiteral("commitPopover"));
    // The shape that makes the stylesheet's border a frame the contents
    // stay inside of.
    setFrameShape(QFrame::StyledPanel);
    // A press on the card is the card's: none reaches the window beneath it.
    setAttribute(Qt::WA_NoMousePropagation);

    m_layout = new QVBoxLayout(this);
    m_layout->setSpacing(0); // every gap below is a spacer of the design's own

    auto *header = sectionHeaderRow(sectionLabel(tr("Message")));
    header->addStretch();
    m_agentButton = iconButton(kCog, tr("⚙"), CommitPage::agentButtonTip());
    connect(m_agentButton, &QToolButton::clicked, this, [this] { m_page->showAgentMenuAt(m_agentButton); });
    header->addWidget(m_agentButton, 0, Qt::AlignVCenter);
    m_layout->addLayout(header);

    m_editorGap = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Fixed);
    m_layout->addItem(m_editorGap);
    m_editor = new MessageEdit;
    m_editor->setPlaceholderText(tr("Commit message"));
    // The page's document, once: the text, the cursor-independent undo stack
    // and the agent's streamed answer are the same in both boxes.
    m_editor->setDocument(page->messageDocument());
    m_editor->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_editor->cornerButton(), &QToolButton::clicked, page, &CommitPage::generateMessage);
    connect(m_editor, &MessageEdit::contentHeightChanged, this, &CommitPopover::scheduleLayout);
    // The document's layout finishes wrapping lazily, as the box paints; a
    // line count that grew that way is a taller message too.
    connect(page->messageDocument()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, this,
            &CommitPopover::scheduleLayout);
    m_layout->addWidget(m_editor);

    m_hintGap = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Fixed);
    m_layout->addItem(m_hintGap);
    m_hint = new QLabel;
    // Not dimLabel, whose captions are bold: the hint is the regular small
    // text of its own stylesheet rule.
    m_hint->setObjectName(QStringLiteral("commitPopoverHint"));
    // The hint is elided to the card, never the reason the card is wider.
    m_hint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_hint->installEventFilter(this);
    m_layout->addWidget(m_hint);

    m_ruleGap = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Fixed);
    m_layout->addItem(m_ruleGap);
    m_layout->addWidget(hairline());
    m_actionGap = new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Fixed);
    m_layout->addItem(m_actionGap);

    m_actions = new QHBoxLayout;
    m_actions->setContentsMargins(0, 0, 0, 0);
    m_amend = new QCheckBox(tr("Amend last commit"));
    // As on the page: the label is the row's to shorten, and only the label
    // and its box answer a click.
    m_amend->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    connect(m_amend, &QCheckBox::toggled, this, [this](bool on) {
        m_page->setAmendChecked(on);
        // The page may have said no (nothing to amend): show what it says.
        applyControls();
    });
    m_actions->addWidget(m_amend, 1);
    m_actions->addStretch();
    m_commitButton = new QPushButton;
    m_commitButton->setDefault(true);
    m_commitButton->setCursor(Qt::PointingHandCursor);
    connect(m_commitButton, &QPushButton::clicked, this, &CommitPopover::commit);
    m_actions->addWidget(m_commitButton);
    m_layout->addLayout(m_actions);

    setTabOrder(m_editor, m_amend);
    setTabOrder(m_amend, m_commitButton);

    // Escape from anywhere on the card closes it.
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, &CommitPopover::dismiss);

    // A hidden card is refreshed as it opens; only a shown one follows along.
    connect(page, &CommitPage::commitControlsChanged, this, [this] {
        if (isVisible())
            applyControls();
    });

    host->installEventFilter(this);
    hide();
    applyTheme();
}

void CommitPopover::setAnchor(MiniRail *rail)
{
    if (m_rail) {
        m_rail->removeEventFilter(this);
        m_rail->commitTile()->removeEventFilter(this);
    }
    m_rail = rail;
    // The rail and its tile move with the window's layout and the text size;
    // the card follows them.
    m_rail->installEventFilter(this);
    m_rail->commitTile()->installEventFilter(this);
}

void CommitPopover::applyTheme()
{
    const OmarchyTheme *theme = OmarchyTheme::instance();
    m_layout->setContentsMargins(space(kPad) - kFrame, space(kTopPad) - kFrame, space(kPad) - kFrame,
                                 space(kPad) - kFrame);
    // The editor's top is space(32) below the card's top edge, exactly: the
    // gap under the header takes whatever the three rounded parts above it
    // leave, instead of being rounded on its own.
    m_editorGap->changeSize(0, qMax(0, space(kEditorTop) - space(kTopPad) - headerRowHeight()), QSizePolicy::Minimum,
                            QSizePolicy::Fixed);
    m_hintGap->changeSize(0, space(kHintGap), QSizePolicy::Minimum, QSizePolicy::Fixed);
    m_ruleGap->changeSize(0, space(kRuleGap), QSizePolicy::Minimum, QSizePolicy::Fixed);
    m_actionGap->changeSize(0, space(kActionGap), QSizePolicy::Minimum, QSizePolicy::Fixed);
    m_actions->setSpacing(sectionGap());
    m_editor->setFont(theme->uiFont());
    m_editor->setMinimumHeight(space(kEditorMin));
    // The corner button is measured for the glyph it wears, so it wears the
    // page's before the box is re-measured.
    applyControls();
    m_editor->applyTheme();
    // The family is the theme's; the size and the weight are the stylesheet's
    // (QLabel#commitPopoverHint). A font set by hand outranks the sheet until
    // the label is polished again, which lays the rule over the family.
    m_hint->setFont(theme->uiFont());
    m_hint->style()->unpolish(m_hint);
    m_hint->style()->polish(m_hint);
    m_hint->setFixedHeight(space(kHintHeight));
    m_layout->invalidate();
    elideHint();
    if (isVisible()) {
        updateAmendLabel();
        place();
        scheduleLayout(); // and once more after the fonts have settled everywhere
    }
}

void CommitPopover::popup()
{
    applyControls();
    place();
    if (!isVisible()) {
        show();
        // Presses anywhere in the application are looked at while the card
        // is up, and only then.
        qApp->installEventFilter(this);
    }
    place(); // now that the box can be measured on screen
    raise();
    emit opened();
    m_editor->setFocus(Qt::OtherFocusReason);
    scheduleLayout();
}

void CommitPopover::dismiss()
{
    if (!isVisible())
        return;
    hide();
    qApp->removeEventFilter(this);
    emit dismissed();
}

bool CommitPopover::commit()
{
    if (!isVisible() || !m_commitButton->isEnabled())
        return false;
    if (m_page->commit()) {
        dismiss();
        return true;
    }
    // The page has said why (no message, a rewrite not wanted, git's error);
    // the message is still the thing to work on.
    m_editor->setFocus(Qt::OtherFocusReason);
    return false;
}

void CommitPopover::applyControls()
{
    const CommitPage::CommitControls c = m_page->commitControls();
    m_commitButton->setText(c.commitText);
    m_commitButton->setAccessibleName(c.commitName);
    m_commitButton->setToolTip(c.commitTip);
    m_commitButton->setEnabled(c.commitEnabled);
    {
        // The page's box is the one that toggles; this one only shows it.
        QSignalBlocker blocker(m_amend);
        m_amend->setChecked(c.amendChecked);
    }
    m_amend->setEnabled(c.amendEnabled);
    m_amend->setToolTip(c.amendTip);
    QToolButton *generate = m_editor->cornerButton();
    generate->setText(c.generateText);
    generate->setToolTip(c.generateTip);
    setHint(c.checked, c.shown);
    updateAmendLabel();
}

void CommitPopover::setHint(int checked, int shown)
{
    // The counts are spelled out: no translation catalogue is loaded, so %n
    // would come out as "file(s)".
    m_hintText = shown == 0 ? tr("No changes to commit")
               : shown == 1 ? tr("%1 / 1 file selected · Space on a tile toggles it").arg(checked)
                            : tr("%1 / %2 files selected · Space on a tile toggles it").arg(checked).arg(shown);
    m_hint->setToolTip(m_hintText);
    elideHint();
}

void CommitPopover::elideHint()
{
    m_hint->ensurePolished();
    m_hint->setText(m_hint->fontMetrics().elidedText(m_hintText, Qt::ElideRight, qMax(0, m_hint->width())));
}

void CommitPopover::updateAmendLabel()
{
    const QString full = tr("Amend last commit");
    const QFontMetrics fm(m_amend->font());
    // What the checkbox costs beyond its text, so the short label is still
    // measured against the long one.
    const int chrome = m_amend->sizeHint().width() - fm.horizontalAdvance(m_amend->text());
    const QMargins margins = m_layout->contentsMargins();
    const int inner = width() - 2 * kFrame - margins.left() - margins.right();
    const int wide = chrome + fm.horizontalAdvance(full) + m_actions->spacing() + m_commitButton->sizeHint().width();
    m_amend->setText(inner >= wide ? full : tr("Amend"));
    m_amend->setMaximumWidth(m_amend->sizeHint().width());
}

void CommitPopover::scheduleLayout()
{
    if (m_layoutQueued || !isVisible())
        return;
    m_layoutQueued = true;
    QTimer::singleShot(0, this, [this] {
        m_layoutQueued = false;
        if (isVisible())
            place();
    });
}

// The card beside the rail, 8 px clear of it and at most 360 wide, as far as
// the window's right margin allows; its message box as tall as the text at
// that width, between 84 px and a third of the window; its bottom on the
// tile's bottom, so a longer message moves the top up.
void CommitPopover::place()
{
    QWidget *host = parentWidget();
    if (!m_rail || !host || m_placing)
        return;
    m_placing = true;
    const QMargins hostMargins = host->layout() ? host->layout()->contentsMargins() : QMargins();
    const QRect rail = m_rail->geometry();
    QWidget *tile = m_rail->commitTile();
    const QRect tileRect(tile->mapTo(host, QPoint(0, 0)), tile->size());
    const int left = rail.x() + rail.width() + space(kRailGap);
    const int width = qMax(1, qMin(space(kMaxWidth), host->width() - hostMargins.right() - left));

    // The width first, so the text is wrapped at the width it is measured at.
    if (this->width() != width) {
        resize(width, height());
        m_layout->activate();
    }
    const int minimum = space(kEditorMin);
    const int maximum = qMax(minimum, host->height() / 3);
    const int editor = qBound(minimum, m_editor->contentHeight(), maximum);
    if (m_editor->height() != editor || m_editor->minimumHeight() != editor)
        m_editor->setFixedHeight(editor);
    m_layout->activate();
    const int height = sizeHint().height();
    const int bottom = tileRect.y() + tileRect.height();
    setGeometry(left, bottom - height, width, height);
    updateAmendLabel();
    elideHint();
    m_placing = false;
}

bool CommitPopover::eventFilter(QObject *watched, QEvent *event)
{
    const QEvent::Type type = event->type();
    if (type == QEvent::MouseButtonPress) {
        // A press in this window, outside the card and the rail, closes the
        // card and goes on to whatever it was for. Menus and dialogs are
        // windows of their own, even the ones parented inside this one: what
        // is pressed there is none of the card's business.
        if (isVisible() && watched->isWidgetType() && static_cast<QWidget *>(watched)->window() == window()) {
            const QPoint global = static_cast<QMouseEvent *>(event)->globalPosition().toPoint();
            const bool onCard = rect().contains(mapFromGlobal(global));
            const bool onRail = m_rail && m_rail->isVisible() && m_rail->rect().contains(m_rail->mapFromGlobal(global));
            if (!onCard && !onRail)
                dismiss();
        }
        return false;
    }
    if (watched == m_hint) {
        if (type == QEvent::Resize || type == QEvent::FontChange || type == QEvent::StyleChange)
            elideHint();
        return false;
    }
    if (watched == parentWidget() || watched == m_rail || (m_rail && watched == m_rail->commitTile())) {
        if (type == QEvent::Resize || type == QEvent::Move || type == QEvent::Show || type == QEvent::LayoutRequest)
            scheduleLayout();
    }
    return QFrame::eventFilter(watched, event);
}
