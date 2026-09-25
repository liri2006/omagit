#include "CommitDetails.h"
#include "OmarchyTheme.h"
#include "RefChip.h"
#include "UiHelpers.h"

#include <QApplication>
#include <QClipboard>
#include <QFontMetrics>
#include <QPainter>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QToolButton>

using namespace ui;

namespace {
// screens.js commitDetails(), on the grid of Grid.h: the card's padding is a
// popover's 12, its buttons standing 4 closer to its edges (8 in) so their
// glyphs sit on that padding. The title on a 24 px row 8 from the top, then
// 16 px lines: the meta line, 4, the parents with their chips, 8, the body.
// The lines are placed by their vertical centre, as the design places text:
// the title at 20, the meta line at 40, the parents at 60 and the body's
// first line at 84.
constexpr int kButtonInset = pad::popover - gap::icon;
constexpr int kTitleY = kButtonInset + box::row / 2;
constexpr int kMetaY = kButtonInset + box::row + box::line / 2;
constexpr int kParentsY = kMetaY + box::line + gap::cluster;
constexpr int kBodyY = kParentsY + box::line + gap::item;
constexpr int kTitleText = 13, kSmallText = 11; // text sizes
// What the rounding of scaled pixels may take off the body's room, at most.
constexpr int kRoundingSlack = 2;

QFont sizedFont(int px, bool bold = false)
{
    QFont font = OmarchyTheme::instance()->uiFont();
    font.setPixelSize(fontPx(px));
    font.setBold(bold);
    return font;
}

// Where text centred on `centreY` has its baseline: kit.js text() puts it
// 0.36 of the size under the centre.
int baselineAt(int centreY, const QFont &font)
{
    return qRound(space(centreY) + 0.36 * font.pixelSize());
}

QString shortOf(const QString &hash, const Commit &commit)
{
    return hash.left(commit.shortHash.isEmpty() ? 7 : commit.shortHash.size());
}
} // namespace

CommitDetails::CommitDetails(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("commitDetails"));

    // The body: the message under its subject, in the lines of the design,
    // read-only and selectable, scrolling with the thin scrollbar where it is
    // longer than its room. It has no box of its own; the card is its frame.
    m_body = new QTextEdit(this);
    m_body->setObjectName(QStringLiteral("commitBody"));
    m_body->setReadOnly(true);
    m_body->setUndoRedoEnabled(false);
    m_body->setFrameShape(QFrame::NoFrame);
    m_body->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    m_body->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_body->document()->setDocumentMargin(0);

    m_copy = iconButton(kContentCopy, QStringLiteral("⧉"), tr("Copy the full SHA"));
    m_copy->setParent(this);
    m_copy->setAccessibleName(tr("Copy full SHA"));
    connect(m_copy, &QToolButton::clicked, this, [this] {
        if (m_hasCommit)
            QApplication::clipboard()->setText(m_commit.hash);
    });

    m_filesButton = toolButton(QString(), tr("The files of this commit, on the Diff tab"));
    m_filesButton->setParent(this);
    m_filesButton->setObjectName(QStringLiteral("ghostButton"));
    m_filesButton->setProperty("ghost", true);
    connect(m_filesButton, &QToolButton::clicked, this, &CommitDetails::filesRequested);

    clear(QString());
    applyTheme();
}

void CommitDetails::setCommit(const Commit &commit, const QList<RefLabel> &refs, int files)
{
    m_commit = commit;
    m_hasCommit = true;
    m_refs = refs;
    m_files = files;
    // The subject is the title: the body is the rest of the message, and a
    // message of one line has none to show.
    m_body->setPlaceholderText(QString());
    setBodyText(commit.body);
    m_copy->setEnabled(true);
    updateFilesButton();
    updateAccessibleText();
    update();
}

void CommitDetails::clear(const QString &message)
{
    m_commit = Commit();
    m_hasCommit = false;
    m_refs.clear();
    m_files = 0;
    setBodyText(QString());
    m_body->setPlaceholderText(message);
    m_copy->setEnabled(false);
    updateFilesButton();
    updateAccessibleText();
    update();
}

void CommitDetails::setStacked(bool on)
{
    if (m_stacked == on)
        return;
    m_stacked = on;
    updateFilesButton();
    updateAccessibleText();
    place();
    updateGeometry(); // the least height moves with the files button
    update();
}

void CommitDetails::applyTheme()
{
    const OmarchyTheme *theme = OmarchyTheme::instance();
    m_body->setFont(theme->uiFont());
    // The empty card's message is as dim as the lines it stands in for.
    QPalette pal = m_body->palette();
    pal.setColor(QPalette::PlaceholderText, theme->mutedText());
    m_body->setPalette(pal);
    setBodyText(m_body->toPlainText()); // the line height is in scaled pixels
    m_copy->setText(icon(kContentCopy, QStringLiteral("⧉")).trimmed());
    m_filesButton->setFixedHeight(space(box::row));
    updateFilesButton();
    place();
    updateGeometry();
    update();
}

QString CommitDetails::title() const
{
    return m_hasCommit ? m_commit.subject : QString();
}

QStringList CommitDetails::metaParts() const
{
    if (!m_hasCommit)
        return {};
    QStringList parts{m_commit.shortHash, QStringLiteral("%1 <%2>").arg(m_commit.author, m_commit.email)};
    // Stacked, the date gives its room to the author.
    if (!m_stacked)
        parts << m_commit.date.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
    return parts;
}

QString CommitDetails::parentsText() const
{
    if (!m_hasCommit)
        return QString();
    if (m_commit.parents.isEmpty())
        return tr("Root commit");
    QStringList shorts;
    for (const QString &parent : m_commit.parents)
        shorts << shortOf(parent, m_commit);
    return m_commit.parents.size() == 1 ? tr("Parent %1").arg(shorts.first())
                                        : tr("Parents %1").arg(shorts.join(QLatin1Char(' ')));
}

QSize CommitDetails::minimumSizeHint() const
{
    // The parents line's chips end 8 px under its centre; under them, the
    // body's bottom room, or the files button and its inset.
    const int header = space(kParentsY) + refChipHeight() / 2;
    const int bottom = m_stacked ? space(box::row + kButtonInset) : space(pad::popover);
    return QSize(space(2 * pad::popover), header + bottom);
}

QSize CommitDetails::sizeHint() const
{
    return QSize(space(400), space(152));
}

void CommitDetails::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    place();
}

void CommitDetails::place()
{
    const int inset = space(pad::popover);
    // By the square the button is sized to rather than its width of the
    // moment: on a new text size it may only be resized after this.
    const int copySide = space(int(IconButtonSize::Inline));
    m_copy->move(width() - space(kButtonInset) - copySide, space(kButtonInset));
    m_filesButton->move(width() - space(kButtonInset) - m_filesButton->width(),
                        height() - space(kButtonInset) - m_filesButton->height());
    // The first line's baseline where the design has it: Qt sets a line of
    // fixed height with its baseline four fifths of the way down it
    // (QTextDocumentLayout), so the body starts that far above the baseline.
    const int line = space(box::line);
    const int baseline = baselineAt(kBodyY, m_body->font());
    const int top = qRound(baseline - 0.8 * line);
    // The body's room ends at the card's padding; stacked, the files button
    // stands in the 24 px over it.
    int room = height() - space(pad::popover) - (m_stacked ? space(box::row) : 0) - top;
    // At 12 px the stacked room is exactly two lines; the rounding of another
    // text size can leave it a pixel or two short of them, which the body
    // takes rather than scrolling its second line away.
    const int lines = (room + kRoundingSlack) / line;
    if (lines > 0 && lines * line > room)
        room = lines * line;
    m_body->setGeometry(inset, top, qMax(0, width() - 2 * inset), qMax(0, room));
}

void CommitDetails::setBodyText(const QString &text)
{
    m_body->setPlainText(text);
    QTextBlockFormat format;
    format.setLineHeight(space(box::line), QTextBlockFormat::FixedHeight);
    QTextCursor cursor(m_body->document());
    cursor.select(QTextCursor::Document);
    cursor.mergeBlockFormat(format);
}

// "2 files ›", a 24 px ghost button with its text 8 in (kit.js
// measureButton({label})); only stacked, and only for a commit that touches
// a file at all.
void CommitDetails::updateFilesButton()
{
    const QString text = m_files == 1 ? tr("1 file ›") : tr("%1 files ›").arg(m_files);
    m_filesButton->setText(text);
    const int advance = m_filesButton->fontMetrics().horizontalAdvance(text);
    m_filesButton->setFixedWidth(advance + 2 * space(pad::control));
    m_filesButton->setVisible(m_stacked && m_hasCommit && m_files > 0);
    place();
}

void CommitDetails::updateAccessibleText()
{
    setAccessibleName(title());
    QStringList lines{metaParts().join(QStringLiteral(", ")), parentsText()};
    for (const RefLabel &label : std::as_const(m_refs))
        lines << label.name;
    lines.removeAll(QString());
    setAccessibleDescription(lines.join(QStringLiteral("; ")));
}

void CommitDetails::paintEvent(QPaintEvent *event)
{
    QFrame::paintEvent(event); // the frame, which the stylesheet draws
    if (!m_hasCommit)
        return;
    const OmarchyTheme *theme = OmarchyTheme::instance();
    QPainter p(this);
    const int left = space(pad::popover);
    const int right = width() - space(pad::popover);

    // The subject, elided before the copy button.
    const QFont titleFont = sizedFont(kTitleText, true);
    const QFontMetrics titleMetrics(titleFont);
    const int titleRoom = qMax(0, m_copy->x() - space(gap::item) - left);
    p.setFont(titleFont);
    p.setPen(theme->text());
    p.drawText(QPoint(left, baselineAt(kTitleY, titleFont)),
               titleMetrics.elidedText(m_commit.subject, Qt::ElideRight, titleRoom));

    // The meta line: the short SHA in the accent, the rest dim, 12 apart;
    // whatever does not fit is elided at the card's inner edge.
    const QFont small = sizedFont(kSmallText);
    const QFontMetrics smallMetrics(small);
    p.setFont(small);
    const QStringList parts = metaParts();
    int x = left;
    const int metaBaseline = baselineAt(kMetaY, small);
    for (int i = 0; i < parts.size() && x < right; ++i) {
        const int advance = smallMetrics.horizontalAdvance(parts.at(i));
        const int room = right - x;
        p.setPen(i == 0 ? theme->accent() : theme->mutedText());
        p.drawText(QPoint(x, metaBaseline),
                   advance <= room ? parts.at(i) : smallMetrics.elidedText(parts.at(i), Qt::ElideRight, room));
        x += advance + space(pad::popover);
    }

    // The parents, then every ref the commit wears (the remote ones too,
    // at any width), centred on the line; a chip that does not fit whole is
    // left out.
    const QString parents = parentsText();
    const int parentsAdvance = smallMetrics.horizontalAdvance(parents);
    p.setPen(theme->mutedText());
    p.drawText(QPoint(left, baselineAt(kParentsY, small)),
               parentsAdvance <= right - left ? parents
                                              : smallMetrics.elidedText(parents, Qt::ElideRight, right - left));
    x = left + parentsAdvance + space(pad::popover);
    const int chipTop = space(kParentsY) - refChipHeight() / 2;
    for (const RefLabel &label : std::as_const(m_refs)) {
        const int w = refChipWidth(label);
        if (x + w > right)
            break;
        paintRefChip(&p, QPoint(x, chipTop), label);
        x += w + space(gap::cluster);
    }
}
