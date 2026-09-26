#include "NewBranchCard.h"
#include "BranchMenu.h"
#include "BranchPicker.h"
#include "OmarchyTheme.h"
#include "UiHelpers.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QEvent>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QShortcut>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

using namespace ui;

namespace {
// The stylesheet's accent frame (QFrame#newBranchCard), two pixels at every
// text size. The design measures its padding from the card's outer edge, so
// the layout's margins are that padding less the frame.
constexpr int kFrame = 2;
// The design's card (design/figma-gen/screens.js newBranchCard()), on the
// grid of Grid.h: a popover's 12 of padding; captions on 16 px lines 4 over
// their control; the name field, the From picker and the action row 28 px
// controls; a red 24 px line 4 under the field where the name will not do;
// the start's commit on a 16 px line 4 under the picker; 16 on, the note on
// the changed files — a 16 px line, or a card of its own; 12, a hairline,
// 12, then Switch to it and Create branch. Sizes the design gives the card
// alone:
constexpr int kMaxWidth = 360;
constexpr int kRuleGap = 12; // screens.js SEP: on either side of a card's hairline
// The note card: the small lines under its title.
constexpr int kBlockedLines = 2;
// Changed files the note card names before it says "and N more".
constexpr int kBlockedNamed = 2;
// The design's small text, 11 px at base 12.
constexpr int kSmallPx = 11;

QFont smallFont()
{
    QFont f = OmarchyTheme::instance()->uiFont();
    f.setPixelSize(fontPx(kSmallPx));
    f.setBold(false);
    return f;
}

// Where kit.js text() puts the baseline of text centred on a line `height`
// tall from `top`: the line's middle plus 0.36 of the size.
qreal baselineOn(qreal top, qreal height, const QFont &font)
{
    return top + qRound(height / 2.0 + 0.36 * font.pixelSize());
}

// A glyph centred by its ink in `box`.
void drawGlyph(QPainter &p, uint cp, const QRectF &box)
{
    const QString glyph = OmarchyTheme::instance()->glyph(cp);
    if (glyph.isEmpty())
        return;
    QFont f = OmarchyTheme::instance()->uiFont();
    f.setBold(false);
    p.setFont(f);
    p.drawText(box.center() - inkRect(f, glyph).center(), glyph);
}

QColor themeColor(const char *key)
{
    return OmarchyTheme::instance()->color(QLatin1String(key));
}
} // namespace

namespace newbranch {

// A glyph in its 16 px box and small text 4 after it, on one line: the red
// line under the name field (24 px) and the note on the changed files (16).
class IconLine : public QWidget
{
public:
    enum class Tone { Dim, Red };

    IconLine(uint glyph, Tone tone, int line, QWidget *parent)
        : QWidget(parent), m_glyph(glyph), m_tone(tone), m_line(line)
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setText(const QString &text)
    {
        m_text = text;
        setToolTip(text);
        updateGeometry();
        update();
    }
    QString text() const { return m_text; }
    int line() const { return m_line; }

    QSize sizeHint() const override
    {
        const int text = QFontMetrics(smallFont()).horizontalAdvance(m_text);
        return QSize(space(box::icon) + space(gap::icon) + text, space(m_line));
    }
    QSize minimumSizeHint() const override { return QSize(space(box::icon), space(m_line)); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const OmarchyTheme *t = OmarchyTheme::instance();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(m_tone == Tone::Red ? themeColor("red") : t->mutedText());
        drawGlyph(p, m_glyph, QRectF(0, 0, space(box::icon), height()));
        const QFont f = smallFont();
        p.setFont(f);
        const int x = space(box::icon) + space(gap::icon);
        const QString shown = QFontMetrics(f).elidedText(m_text, Qt::ElideRight, qMax(0, width() - x));
        p.drawText(QPointF(x, baselineOn(0, height(), f)), shown);
    }

private:
    uint m_glyph;
    Tone m_tone;
    int m_line; // design px
    QString m_text;
};

// The start's commit on a 16 px line under the picker: its short hash in the
// accent, 8, and its subject, dim and elided; a commit start (the picker
// names its hash already) gives the subject alone.
class StartLine : public QWidget
{
public:
    explicit StartLine(QWidget *parent) : QWidget(parent) { setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); }

    void set(const QString &sha, const QString &subject)
    {
        m_sha = sha;
        m_subject = subject;
        setToolTip(subject);
        update();
    }
    QString sha() const { return m_sha; }
    QString subject() const { return m_subject; }

    QSize sizeHint() const override { return QSize(space(120), space(box::line)); }
    QSize minimumSizeHint() const override { return QSize(0, space(box::line)); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const OmarchyTheme *t = OmarchyTheme::instance();
        QPainter p(this);
        const QFont f = smallFont();
        const QFontMetrics fm(f);
        p.setFont(f);
        const qreal baseline = baselineOn(0, height(), f);
        int x = 0;
        if (!m_sha.isEmpty()) {
            p.setPen(t->accent());
            p.drawText(QPointF(0, baseline), m_sha);
            x = fm.horizontalAdvance(m_sha) + space(gap::item);
        }
        p.setPen(t->mutedText());
        p.drawText(QPointF(x, baseline), fm.elidedText(m_subject, Qt::ElideRight, qMax(0, width() - x)));
    }

private:
    QString m_sha, m_subject;
};

// The warning where the changed files are in the way of a switch: a card of
// its own, 12 of padding, the yellow alert in its 16 px box, 8, then the
// title in bold yellow on a 16 px line and, 4 under it, small dim lines.
// The fill is the foreground's 4 %, the border the yellow at 60 %.
class BlockedNote : public QWidget
{
public:
    explicit BlockedNote(QWidget *parent) : QWidget(parent) { setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); }

    void set(const QString &title, const QStringList &lines)
    {
        m_title = title;
        m_lines = lines;
        update();
    }
    QString title() const { return m_title; }
    QStringList lines() const { return m_lines; }

    // Always two lines, the files and what to do about them.
    static int designHeight()
    {
        return pad::popover + box::line + gap::caption + kBlockedLines * box::line + pad::popover;
    }
    QSize sizeHint() const override { return QSize(space(240), space(designHeight())); }
    QSize minimumSizeHint() const override { return QSize(0, space(designHeight())); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const OmarchyTheme *t = OmarchyTheme::instance();
        QPainter p(this);
        p.fillRect(rect(), t->normalFill());
        QColor edge = themeColor("yellow");
        edge.setAlphaF(0.6);
        p.setPen(Qt::NoPen);
        p.setBrush(edge);
        // The 1 px border inside the box.
        p.drawRect(QRect(0, 0, width(), 1));
        p.drawRect(QRect(0, height() - 1, width(), 1));
        p.drawRect(QRect(0, 1, 1, height() - 2));
        p.drawRect(QRect(width() - 1, 1, 1, height() - 2));
        p.setRenderHint(QPainter::Antialiasing);
        const int pad = space(pad::popover), line = space(box::line);
        p.setPen(themeColor("yellow"));
        drawGlyph(p, kAlert, QRectF(pad, pad, space(box::icon), space(box::icon)));
        const int x = pad + space(box::icon) + space(gap::item), room = qMax(0, width() - pad - x);
        QFont bold = t->uiFont();
        bold.setBold(true);
        p.setFont(bold);
        p.drawText(QPointF(x, baselineOn(pad, line, bold)), QFontMetrics(bold).elidedText(m_title, Qt::ElideRight, room));
        const QFont small = smallFont();
        p.setFont(small);
        p.setPen(t->mutedText());
        int y = pad + line + space(gap::caption);
        for (const QString &l : std::as_const(m_lines)) {
            p.drawText(QPointF(x, baselineOn(y, line, small)), QFontMetrics(small).elidedText(l, Qt::ElideRight, room));
            y += line;
        }
    }

private:
    QString m_title;
    QStringList m_lines;
};

} // namespace newbranch

using newbranch::BlockedNote;
using newbranch::IconLine;
using newbranch::StartLine;

// ---------------------------------------------------------------- NewBranchCard

NewBranchCard::NewBranchCard(GitRepo *repo, QWidget *host)
    : QFrame(host), m_repo(repo)
{
    setObjectName(QStringLiteral("newBranchCard"));
    // The shape that makes the stylesheet's border a frame the contents
    // stay inside of.
    setFrameShape(QFrame::StyledPanel);
    // A press on the card is the card's: none reaches the window beneath it.
    setAttribute(Qt::WA_NoMousePropagation);
    setFocusPolicy(Qt::StrongFocus);

    m_layout = new QVBoxLayout(this);
    m_layout->setSpacing(0); // every gap is a spacer of the design's own
    build();

    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, &NewBranchCard::dismiss);

    host->installEventFilter(this);
    hide();
    applyTheme();
}

void NewBranchCard::build()
{
    // NAME, and the field with the keyboard.
    m_layout->addLayout(captionRow(tr("Name"), nullptr));
    addGap(gap::caption);
    m_name = new QLineEdit(this);
    m_name->setObjectName(QStringLiteral("newBranchName"));
    m_name->setPlaceholderText(tr("feature/…"));
    m_name->setToolTip(tr("The new branch's name; a space becomes a dash"));
    m_layout->addWidget(m_name);
    // A space is not a name's: it goes in as a dash, where it was typed.
    connect(m_name, &QLineEdit::textEdited, this, [this](const QString &text) {
        const QString name = GitRepo::typedBranchName(text);
        if (name == text)
            return;
        const int cursor = m_name->cursorPosition();
        m_name->setText(name);
        m_name->setCursorPosition(cursor);
    });
    connect(m_name, &QLineEdit::textChanged, this, [this] {
        m_createError.clear(); // it was about the name before
        validate();
    });

    // Why Create is off, 4 under the field, and for a taken name the way out.
    m_errorRow = new QWidget(this);
    m_errorRow->setObjectName(QStringLiteral("newBranchError"));
    auto *errorLayout = new QHBoxLayout(m_errorRow);
    errorLayout->setSpacing(0);
    m_error = new IconLine(kAlertCircleOutline, IconLine::Tone::Red, box::row, m_errorRow);
    errorLayout->addWidget(m_error, 1);
    m_switchToExisting = toolButton(tr("Switch to it"), tr("Switch to the branch of this name instead"));
    m_switchToExisting->setParent(m_errorRow);
    m_switchToExisting->setObjectName(QStringLiteral("ghostButton"));
    m_switchToExisting->setProperty("ghost", true);
    m_switchToExisting->setFocusPolicy(Qt::TabFocus);
    m_switchToExisting->setAccessibleName(tr("Switch to the existing branch"));
    connect(m_switchToExisting, &QToolButton::clicked, this, [this] {
        const QString name = m_name->text();
        dismiss();
        emit switchRequested(name);
    });
    errorLayout->addWidget(m_switchToExisting);
    m_errorRow->hide();
    m_layout->addWidget(m_errorRow);
    addGap(gap::group);

    // FROM: the picker, and the commit it stands for under it.
    m_fromNote = new QLabel(this);
    // The regular caption-sized note of the popovers (QLabel#newBranchNote).
    m_fromNote->setObjectName(QStringLiteral("newBranchNote"));
    m_layout->addLayout(captionRow(tr("From"), m_fromNote));
    addGap(gap::caption);
    m_picker = new BranchPicker(BranchPicker::Size::Control, this);
    m_picker->setToolTip(tr("Where the branch starts — click to pick another branch or a tag"));
    connect(m_picker, &QToolButton::clicked, this, &NewBranchCard::pickStart);
    m_layout->addWidget(m_picker);
    addGap(gap::caption);
    m_startLine = new StartLine(this);
    m_layout->addWidget(m_startLine);

    // 16 on, what becomes of the changed files.
    m_noteBox = new QWidget(this);
    auto *noteLayout = new QVBoxLayout(m_noteBox);
    noteLayout->setSpacing(0);
    m_carry = new IconLine(kInfoOutline, IconLine::Tone::Dim, box::line, m_noteBox);
    noteLayout->addWidget(m_carry);
    m_blocked = new BlockedNote(m_noteBox);
    m_blocked->setObjectName(QStringLiteral("newBranchBlocked"));
    noteLayout->addWidget(m_blocked);
    m_layout->addWidget(m_noteBox);

    // 12, a hairline, 12: the hairline is the first row of the second 12.
    addGap(kRuleGap);
    QWidget *rule = hairline();
    rule->setParent(this);
    m_layout->addWidget(rule);
    addGap(kRuleGap, 1);

    // Switch to it at the left, the card's primary action at the right.
    m_actionRow = new QWidget(this);
    auto *actions = new QHBoxLayout(m_actionRow);
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(0);
    m_switch = new QCheckBox(tr("Switch to it"), m_actionRow);
    m_switch->setObjectName(QStringLiteral("newBranchSwitch"));
    // What the user leaves it at, where it is theirs to set.
    connect(m_switch, &QCheckBox::clicked, this, [this](bool on) { m_switchWanted = on; });
    actions->addWidget(m_switch, 0, Qt::AlignVCenter);
    actions->addStretch(1);
    m_create = new KitPushButton(m_actionRow);
    m_create->setObjectName(QStringLiteral("newBranchCreate"));
    m_create->setDefault(true);
    setPrimary(m_create); // 16 in, the card's primary action
    m_create->setCursor(Qt::PointingHandCursor);
    connect(m_create, &QPushButton::clicked, this, &NewBranchCard::create);
    actions->addWidget(m_create, 0, Qt::AlignVCenter);
    m_layout->addWidget(m_actionRow);

    setTabOrder(m_name, m_switchToExisting);
    setTabOrder(m_switchToExisting, m_picker);
    setTabOrder(m_picker, m_switch);
    setTabOrder(m_switch, m_create);
}

QHBoxLayout *NewBranchCard::captionRow(const QString &caption, QLabel *note)
{
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    // A strut of no width holds the row at a 16 px line; the caption and the
    // note sit at its top, their baselines where the design centres them
    // (applyTheme()).
    auto *strut = new QSpacerItem(0, space(box::line), QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_gaps.append({strut, box::line, 0});
    row->addItem(strut);
    QLabel *label = sectionLabel(caption);
    label->setParent(this);
    label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_captions << label;
    row->addWidget(label, 0, Qt::AlignTop);
    row->addStretch();
    if (note) {
        note->setAlignment(Qt::AlignRight | Qt::AlignTop);
        m_captions << note;
        row->addWidget(note, 0, Qt::AlignTop);
    }
    return row;
}

void NewBranchCard::addGap(int px, int less)
{
    auto *gap = new QSpacerItem(0, space(px) - less, QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_gaps.append({gap, px, less});
    m_layout->addItem(gap);
}

void NewBranchCard::applyTheme()
{
    const OmarchyTheme *theme = OmarchyTheme::instance();
    const int pad = space(pad::popover) - kFrame;
    m_layout->setContentsMargins(pad, pad, pad, pad);
    for (const Gap &gap : std::as_const(m_gaps))
        gap.spacer->changeSize(0, space(gap.px) - gap.less, QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_name->setFixedHeight(space(box::control));
    // 4 under the field, a 24 px line.
    m_errorRow->layout()->setContentsMargins(0, space(gap::caption), 0, 0);
    m_errorRow->setFixedHeight(space(gap::caption + box::row));
    m_error->setFixedHeight(space(box::row));
    // The design's ghost button on the line: its label 8 in from either side.
    m_switchToExisting->ensurePolished();
    m_switchToExisting->setFixedSize(
        m_switchToExisting->fontMetrics().horizontalAdvance(m_switchToExisting->text()) + 2 * space(pad::control),
        space(box::row));
    // The note beside FROM: the family is the theme's, the size and the
    // weight the stylesheet's. A font set by hand outranks the sheet until
    // the label is polished again.
    m_fromNote->setFont(theme->uiFont());
    m_fromNote->style()->unpolish(m_fromNote);
    m_fromNote->style()->polish(m_fromNote);
    // The captions and the note on their 16 px lines, where kit.js text()
    // puts a line's baseline (its middle plus 0.36 of the size).
    for (QLabel *label : std::as_const(m_captions)) {
        label->ensurePolished();
        placeOnLine(label, label->font(), box::line);
    }
    m_picker->updateGeometry();
    m_startLine->setFixedHeight(space(box::line));
    // 16 under the start line, the note.
    m_noteBox->layout()->setContentsMargins(0, space(gap::group), 0, 0);
    m_carry->setFixedHeight(space(box::line));
    m_blocked->setFixedHeight(space(BlockedNote::designHeight()));
    m_actionRow->setFixedHeight(space(box::control));
    m_create->setText(icon(kBranchPlus, QStringLiteral("+ ")) + tr("Create branch  ⏎"));
    for (QWidget *w : QList<QWidget *>{m_error, m_carry, m_startLine, m_blocked})
        w->update();
    m_layout->invalidate();
    if (isVisible()) {
        place();
        scheduleLayout(); // and once more after the fonts have settled everywhere
    }
}

// ---- The parts' state --------------------------------------------------------

QAbstractButton *NewBranchCard::switchToExistingButton() const
{
    return m_switchToExisting;
}

QString NewBranchCard::errorText() const
{
    return m_errorRow->isVisibleTo(this) ? m_error->text() : QString();
}

QString NewBranchCard::fromNote() const
{
    return m_fromNote->text();
}

QString NewBranchCard::startSha() const
{
    return m_startLine->sha();
}

QString NewBranchCard::startSubject() const
{
    return m_startLine->subject();
}

QString NewBranchCard::carryNote() const
{
    if (m_blocked->isVisibleTo(this))
        return m_blocked->title();
    return m_carry->isVisibleTo(this) ? m_carry->text() : QString();
}

QWidget *NewBranchCard::blockedNote() const
{
    return m_blocked;
}

QStringList NewBranchCard::blockedLines() const
{
    return m_blocked->lines();
}

// ---- Where it starts ---------------------------------------------------------

void NewBranchCard::setStart(const QString &ref, bool picked)
{
    Start s;
    const QString current = m_branches.current;
    if (ref.isEmpty() || (!current.isEmpty() && ref == current) || !m_hasHead) {
        if (current.isEmpty() && m_hasHead) {
            // Detached: HEAD's commit, named by its hash.
            s.kind = Start::Commit;
            s.ref = QStringLiteral("HEAD");
        } else {
            // The checked-out branch; without commits, the one that is yet to be.
            s.kind = Start::Branch;
            s.current = true;
            s.label = current.isEmpty() ? m_repo->branch() : current;
        }
    } else if (m_branches.local.contains(ref)) {
        s.kind = Start::Branch;
        s.ref = s.label = ref;
    } else if (m_branches.remote.contains(ref)) {
        s.kind = Start::Remote;
        s.ref = s.label = ref;
    } else if (m_tags.contains(ref)) {
        s.kind = Start::Tag;
        s.ref = s.label = ref;
    } else {
        s.kind = Start::Commit;
        s.ref = ref;
        s.picked = picked;
    }
    if (m_hasHead) {
        m_repo->describeCommit(s.ref.isEmpty() ? QStringLiteral("HEAD") : s.ref, &s.sha, &s.subject);
        // The current branch is where the changes are already.
        if (!s.current)
            s.blocked = m_repo->pathsBlockingSwitch(s.ref, m_dirty);
    } else {
        s.subject = tr("No commits yet");
    }
    if (s.kind == Start::Commit)
        s.label = s.sha.isEmpty() ? s.ref : s.sha;
    m_start = s;
    showStart();
}

void NewBranchCard::showStart()
{
    const Start &s = m_start;
    const BranchPicker::Kind kind = s.kind == Start::Commit ? BranchPicker::Kind::Commit
        : s.kind == Start::Tag                              ? BranchPicker::Kind::Tag
                                                            : BranchPicker::Kind::Branch;
    m_picker->setBranch(s.label, kind);
    // Without commits there is nowhere else to start from.
    m_picker->setEnabled(m_hasHead);
    m_fromNote->setText(s.current ? tr("the current branch") : s.picked ? tr("the commit picked in History") : QString());
    m_startLine->set(s.kind == Start::Commit ? QString() : s.sha, s.subject);

    // The changed files: in the way of the switch, or coming along.
    if (!s.blocked.isEmpty()) {
        const int n = int(s.blocked.size());
        const QString title = n == 1 ? tr("1 changed file differs at %1").arg(s.sha)
                                     : tr("%1 changed files differ at %2").arg(n).arg(s.sha);
        QStringList names;
        for (const QString &path : s.blocked.mid(0, kBlockedNamed))
            names << QFileInfo(path).fileName();
        QString files = names.join(QStringLiteral(", "));
        if (n > kBlockedNamed)
            files = tr("%1 and %2 more").arg(files).arg(n - kBlockedNamed);
        m_blocked->set(title, {files, tr("Commit them first to switch to it.")});
        m_blocked->setToolTip(s.blocked.join(QLatin1Char('\n')));
    } else if (m_changed > 0) {
        m_carry->setText(m_changed == 1 ? tr("Your 1 changed file comes along.")
                                        : tr("Your %1 changed files come along.").arg(m_changed));
    }
    m_blocked->setVisible(!s.blocked.isEmpty());
    m_carry->setVisible(s.blocked.isEmpty() && m_changed > 0);
    m_noteBox->setVisible(!s.blocked.isEmpty() || m_changed > 0);

    // Switch to it: git will not where changed files differ at the start;
    // without commits nothing but switching (renaming the unborn branch) is
    // possible at all.
    if (!m_hasHead) {
        m_switch->setChecked(true);
        m_switch->setEnabled(false);
        m_switch->setToolTip(tr("Without commits, a new branch can only be switched to"));
    } else if (!s.blocked.isEmpty()) {
        m_switch->setChecked(false);
        m_switch->setEnabled(false);
        m_switch->setToolTip(tr("Changed files differ there — commit them first to switch to it"));
    } else {
        m_switch->setEnabled(true);
        m_switch->setChecked(m_switchWanted);
        m_switch->setToolTip(tr("Check the new branch out, the changed files along with it"));
    }
    if (isVisible())
        place();
}

void NewBranchCard::pickStart()
{
    BranchMenu menu(this);
    const QString ticked = m_start.current ? m_branches.current : m_start.kind == Start::Commit ? QString() : m_start.ref;
    menu.setBranches(m_branches, ticked, true, [](const QString &name, bool) { return tr("Start the branch at %1").arg(name); },
                     QString(), m_tags);
    QString chosen;
    connect(&menu, &BranchMenu::picked, this, [&chosen](const QString &name) { chosen = name; });
    // It hangs 4 under the picker (screens.js, the From picker's menu).
    menu.popupAt(m_picker, false, nullptr, space(gap::cluster));
    if (chosen.isEmpty())
        return;
    setStart(chosen, false);
    // A remote branch names the branch that tracks it, where no name is typed yet.
    if (m_start.kind == Start::Remote && m_name->text().isEmpty())
        m_name->setText(localName(chosen));
}

QString NewBranchCard::localName(const QString &remoteBranch) const
{
    for (const QString &remote : m_repo->remotes())
        if (remoteBranch.startsWith(remote + QLatin1Char('/')))
            return remoteBranch.mid(remote.size() + 1);
    return remoteBranch.section(QLatin1Char('/'), 1);
}

// ---- The name ------------------------------------------------------------------

void NewBranchCard::validate()
{
    const QString name = m_name->text();
    const bool taken = !name.isEmpty() && m_branches.local.contains(name);
    const bool valid = !name.isEmpty() && !taken && m_repo->isValidBranchName(name);
    QString error;
    if (taken)
        error = tr("Already a branch");
    else if (!name.isEmpty() && !valid)
        error = tr("Not a valid branch name");
    else if (!m_createError.isEmpty())
        error = m_createError;
    m_error->setText(error);
    m_switchToExisting->setVisible(taken);
    const bool changed = m_errorRow->isVisibleTo(this) != !error.isEmpty();
    m_errorRow->setVisible(!error.isEmpty());
    m_create->setEnabled(valid);
    m_create->setToolTip(valid ? tr("Create %1 (Return)").arg(name)
                               : name.isEmpty() ? tr("Type a name for the branch") : error);
    if (changed && isVisible())
        place();
}

void NewBranchCard::create()
{
    if (!m_create->isEnabled())
        return;
    const QString name = m_name->text();
    const bool switchTo = m_switch->isChecked();
    QString error;
    if (!m_repo->createBranch(name, m_start.ref, switchTo, &error)) {
        m_createError = error.section(QLatin1Char('\n'), 0, 0);
        validate();
        return;
    }
    QString sha;
    m_repo->describeCommit(name, &sha, nullptr);
    dismiss();
    emit created(name, sha, switchTo);
}

void NewBranchCard::keyPressEvent(QKeyEvent *event)
{
    // The field and the picker leave Return to the card, as a dialog's
    // controls leave it to the default button.
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        && (event->modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier) {
        create();
        return;
    }
    QFrame::keyPressEvent(event);
}

// ---- Opening and closing -------------------------------------------------------

void NewBranchCard::popup(const QString &start, const QString &name)
{
    if (!m_anchor)
        return;
    const bool shown = isVisible();
    if (!shown) {
        QWidget *focus = QApplication::focusWidget();
        m_returnFocus = focus && focus->window() == window() ? focus : nullptr;
    }
    readRepository();
    m_switchWanted = true; // on every time the card opens
    m_createError.clear();
    setStart(start, true);
    m_name->setText(GitRepo::typedBranchName(name.trimmed()));
    validate(); // the same text as before says nothing by itself
    place();
    if (!shown) {
        show();
        // Presses anywhere in the application are looked at while the card
        // is up, and only then.
        qApp->installEventFilter(this);
    }
    place(); // now that it can be measured on screen
    raise();
    // The caret after whatever came along from the menu's search.
    m_name->setFocus(Qt::PopupFocusReason);
    m_name->end(false);
    if (!shown)
        emit opened();
}

void NewBranchCard::reload()
{
    if (!isVisible())
        return;
    readRepository();
    // The same start, looked up again; a detached HEAD is the checkout,
    // wherever that is now.
    const QString ref = m_start.ref == QLatin1String("HEAD") ? QString() : m_start.ref;
    setStart(ref, m_start.picked);
    // Gone (a branch deleted, a commit no longer there): the current branch.
    if (m_hasHead && !ref.isEmpty() && m_start.sha.isEmpty())
        setStart(QString(), false);
    validate();
}

void NewBranchCard::readRepository()
{
    m_branches = m_repo->branches();
    m_tags = m_repo->tags();
    m_hasHead = m_repo->hasHead();
    // The changed files, read here once for whatever start is picked until
    // the next read: the paths the start's blockers are found among, and
    // how many files the note says come along.
    m_dirty = m_repo->changedPaths();
    m_changed = m_repo->changedFileCount();
}

void NewBranchCard::dismiss()
{
    if (!isVisible())
        return;
    // The keyboard goes back where it came from, unless whatever closed the
    // card has taken it already.
    QWidget *const focus = QApplication::focusWidget();
    const bool focusOnCard = !focus || focus == this || isAncestorOf(focus);
    hide();
    qApp->removeEventFilter(this);
    if (focusOnCard && m_returnFocus && m_returnFocus->isVisible())
        m_returnFocus->setFocus(Qt::OtherFocusReason);
    m_returnFocus = nullptr;
    emit dismissed();
}

void NewBranchCard::setAnchor(QWidget *anchor, QWidget *bar)
{
    for (const QPointer<QWidget> &w : std::as_const(m_watched))
        if (w)
            w->removeEventFilter(this);
    m_watched.clear();
    m_anchor = anchor;
    m_bar = bar;
    // The chip moves with the bar's folding and the bar with the window; the
    // card follows them.
    QWidget *host = parentWidget();
    for (QWidget *w = anchor; w && w != host; w = w->parentWidget()) {
        w->installEventFilter(this);
        m_watched << w;
    }
}

void NewBranchCard::scheduleLayout()
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

// screens.js screen(), the newBranch overlay: 360 wide at the most, never
// wider than the window's margins allow; at the branch chip's left, moved
// left to stay a margin inside the window; 4 under the bar (ui::popupTop()).
// Moved up when the window is too short for it.
void NewBranchCard::place()
{
    QWidget *host = parentWidget();
    if (!m_anchor || !m_bar || !host || m_placing)
        return;
    m_placing = true;
    const int margin = windowMargin(host);
    const int width = qMax(1, qMin(space(kMaxWidth), host->width() - 2 * margin));
    const int chip = m_anchor->mapTo(host, QPoint(0, 0)).x();
    const int left = qMax(0, qMin(chip, host->width() - margin - width));
    int top = host->mapFromGlobal(QPoint(0, popupTop(m_bar))).y();
    if (this->width() != width)
        resize(width, height());
    m_layout->activate();
    const int height = sizeHint().height();
    const int bottom = host->height() - margin;
    if (top + height > bottom)
        top = qMax(margin, bottom - height);
    setGeometry(left, top, width, height);
    m_placing = false;
}

bool NewBranchCard::eventFilter(QObject *watched, QEvent *event)
{
    const QEvent::Type type = event->type();
    if (type == QEvent::MouseButtonPress) {
        // A press in this window, outside the card, closes it and goes on to
        // whatever it was for — the branch chip's too. Menus (the From
        // picker's) are windows of their own.
        if (isVisible() && watched->isWidgetType() && static_cast<QWidget *>(watched)->window() == window()) {
            const QPoint global = static_cast<QMouseEvent *>(event)->globalPosition().toPoint();
            if (!rect().contains(mapFromGlobal(global)))
                dismiss();
        }
        return false;
    }
    if (watched == m_anchor && type == QEvent::Hide) {
        // The chip went: the card has nothing left to hang from.
        dismiss();
        return false;
    }
    if (watched == parentWidget() || std::any_of(m_watched.cbegin(), m_watched.cend(), [watched](const QPointer<QWidget> &w) {
            return w == watched;
        })) {
        if (type == QEvent::Resize || type == QEvent::Move || type == QEvent::LayoutRequest)
            scheduleLayout();
    }
    return QFrame::eventFilter(watched, event);
}
