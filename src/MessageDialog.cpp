#include "MessageDialog.h"
#include "OmarchyTheme.h"
#include "UiHelpers.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QShowEvent>
#include <QTextEdit>
#include <QVBoxLayout>

#include <cmath>

namespace {
// As wide as the sign-in and the settings, where the window allows it.
constexpr int kDialogWidth = 480;
// Lines of text shown before the rest scrolls: git's push hints fit, a
// merge's list of conflicting files does not have to.
constexpr int kMaxLines = 14;
// md-alert_circle, md-alert, md-help_circle_outline: what kind of message it is.
constexpr uint kErrorGlyph = 0xF0028, kWarningGlyph = 0xF0026, kQuestionGlyph = 0xF0625;
} // namespace

MessageDialog::MessageDialog(Kind kind, const QString &title, const QString &text, QWidget *parent)
    : QDialog(parent), m_kind(kind)
{
    setObjectName(QStringLiteral("messageDialog"));
    setWindowTitle(title);
    setSizeGripEnabled(false);

    auto *layout = new QVBoxLayout(this);
    // The glyph beside the title, the text under the title, as the merge
    // view's verdict has them.
    m_grid = new QGridLayout;
    m_icon = new QLabel;
    m_icon->setObjectName(QStringLiteral("bigLabel"));
    m_icon->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    m_icon->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_title = new QLabel(title);
    m_title->setWordWrap(true);
    m_title->setTextInteractionFlags(Qt::TextSelectableByMouse);
    // A text box rather than a label: a label only wraps between words, and
    // git's messages carry URLs and paths longer than the dialog is wide.
    m_text = new QTextEdit;
    m_text->setObjectName(QStringLiteral("messageText"));
    m_text->setReadOnly(true);
    m_text->setUndoRedoEnabled(false);
    m_text->setFrameShape(QFrame::NoFrame);
    m_text->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    m_text->setFocusPolicy(Qt::ClickFocus); // the keyboard starts on the buttons
    m_text->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_text->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    m_text->document()->setDocumentMargin(0);
    m_text->setPlainText(text);
    m_text->setVisible(!text.isEmpty());
    m_grid->addWidget(m_icon, 0, 0, 2, 1, Qt::AlignTop);
    m_grid->addWidget(m_title, 0, 1);
    m_grid->addWidget(m_text, 1, 1);
    m_grid->setColumnStretch(1, 1);
    layout->addLayout(m_grid);
    layout->addStretch(1);

    m_buttonRow = new QHBoxLayout;
    m_buttonRow->addStretch();
    m_acceptButton = new QPushButton(tr("OK"));
    m_acceptButton->setCursor(Qt::PointingHandCursor);
    connect(m_acceptButton, &QPushButton::clicked, this, &QDialog::accept);
    if (kind == Question) {
        m_cancelButton = new QPushButton(tr("Cancel"));
        m_cancelButton->setCursor(Qt::PointingHandCursor);
        m_cancelButton->setDefault(true);
        connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
        m_acceptButton->setAutoDefault(false);
        m_buttonRow->addWidget(m_cancelButton);
    } else {
        m_acceptButton->setDefault(true);
        ui::setPrimary(m_acceptButton);
    }
    m_buttonRow->addWidget(m_acceptButton);
    layout->addLayout(m_buttonRow);

    applyTheme();
    connect(OmarchyTheme::instance(), &OmarchyTheme::changed, this, &MessageDialog::applyTheme);
}

void MessageDialog::setAcceptText(const QString &text)
{
    m_acceptButton->setText(text);
}

QString MessageDialog::title() const
{
    return m_title->text();
}

QString MessageDialog::text() const
{
    return m_text->toPlainText();
}

QPushButton *MessageDialog::defaultButton() const
{
    return m_cancelButton ? m_cancelButton : m_acceptButton;
}

void MessageDialog::applyTheme()
{
    const OmarchyTheme *t = OmarchyTheme::instance();
    const int pad = ui::space(ui::pad::dialog);
    layout()->setContentsMargins(pad, pad, pad, pad);
    layout()->setSpacing(ui::space(ui::gap::group));
    m_grid->setHorizontalSpacing(ui::space(ui::gap::item));
    m_grid->setVerticalSpacing(ui::space(ui::gap::item));
    m_buttonRow->setSpacing(ui::space(ui::gap::item));
    m_title->setFont(t->titleFont());
    m_text->setFont(t->uiFont());
    QFont big = t->uiFont();
    big.setPixelSize(qRound(t->fontBase() * 1.5));
    m_icon->setFont(big);
    m_icon->setFixedWidth(ui::space(ui::box::icon));

    uint glyph = kErrorGlyph;
    QString fallback = QStringLiteral("✗");
    QColor color = t->color(QStringLiteral("red"));
    if (m_kind == Warning) {
        glyph = kWarningGlyph;
        fallback = QStringLiteral("!");
        color = t->color(QStringLiteral("yellow"));
    } else if (m_kind == Question) {
        glyph = kQuestionGlyph;
        fallback = QStringLiteral("?");
        color = t->accent();
    }
    const QString mark = t->glyph(glyph);
    m_icon->setText(mark.isEmpty() ? fallback : mark);
    const QString sheet = QStringLiteral("color: %1;").arg(color.name());
    if (m_iconColor != sheet) {
        m_iconColor = sheet;
        m_icon->setStyleSheet(sheet);
    }
    if (isVisible())
        fitToContent();
}

// As wide as the window it opens over allows (ui::fitDialogWidth()), and as
// tall as its content: the text as many lines as it wraps into at that width,
// up to kMaxLines, after which it scrolls.
void MessageDialog::fitToContent()
{
    ui::fitDialogWidth(this, kDialogWidth);
    QLayout *l = layout();
    l->invalidate();
    l->activate(); // gives the text its width
    if (m_text->isVisibleTo(this)) {
        QTextDocument *doc = m_text->document();
        doc->setTextWidth(m_text->width());
        const int lines = m_text->fontMetrics().lineSpacing() * kMaxLines;
        m_text->setFixedHeight(qMin(int(std::ceil(doc->size().height())), lines));
    }
    l->invalidate();
    l->activate();
    const int content = l->hasHeightForWidth() ? l->totalHeightForWidth(width()) : l->totalSizeHint().height();
    const int target = qMax(content, l->totalMinimumSize().height());
    if (target != height() || maximumHeight() != target)
        setFixedHeight(target);
}

// Fonts and frame widths from the stylesheet are only final once the widgets
// are polished, which is later than the constructor.
void MessageDialog::showEvent(QShowEvent *event)
{
    ensurePolished();
    fitToContent();
    QDialog::showEvent(event);
    defaultButton()->setFocus();
}

void MessageDialog::error(QWidget *parent, const QString &title, const QString &text)
{
    MessageDialog(Error, title, text, parent).exec();
}

void MessageDialog::warning(QWidget *parent, const QString &title, const QString &text)
{
    MessageDialog(Warning, title, text, parent).exec();
}

bool MessageDialog::confirm(QWidget *parent, const QString &title, const QString &text, const QString &acceptText)
{
    MessageDialog box(Question, title, text, parent);
    box.setAcceptText(acceptText);
    return box.exec() == QDialog::Accepted;
}
