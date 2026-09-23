#include "AgentPopover.h"
#include "CommitPage.h"
#include "OmarchyTheme.h"
#include "Segmented.h"
#include "UiHelpers.h"

#include <QAbstractButton>
#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
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
// The stylesheet's accent frame (QFrame#agentPopover), two pixels at every
// text size. The design measures its padding from the card's outer edge, so
// the layout's margins are that padding less the frame.
constexpr int kFrame = 2;
// The design's pixels (design/figma-gen/screens.js agentPopover()): the card
// at most 360 wide and 6 under its cog, padded 10 all round; a section's
// header row 22 tall, a picker, a model row and the other-model row 28, the
// level track 44; 16 between sections, 6 above the other-model row, 12 on
// either side of the rule above Generate now.
constexpr int kMaxWidth = 360, kCogGap = 6, kPad = 10;
constexpr int kHeaderRow = 22, kRowHeight = 28, kTrackHeight = 44;
constexpr int kSectionGap = 16, kOtherGap = 6, kRuleGap = 12;
// Inside a model row: the name 10 in, the id ending 30 from the right edge,
// the 14 px tick ending 10 from it.
constexpr int kRowPad = 10, kDetailEnd = 30, kTick = 14;
// The level track: its line 28 in from either side, 12 down; the labels 22
// under the line; the stops' radii.
constexpr int kTrackInset = 28, kTrackLine = 12, kTrackLabel = 22;
constexpr qreal kStopRadius = 4, kSelectedRadius = 4.5, kHaloRadius = 7;
// Nothing installed: the robot's 20 px glyph 2 px in a 32 px column, the
// headline row 24 tall and the line under it 12, 16 down to INSTALL ONE,
// whose row is 20; command rows 30 tall and 6 apart, their copy button 3 in;
// the closing note's row 20.
constexpr int kRobotSize = 20, kRobotInset = 2, kRobotColumn = 32, kHeadline = 24, kSubline = 12;
constexpr int kInstallHeader = 20, kCommandRow = 30, kCommandGap = 6, kCommandPad = 10, kCopyInset = 3;
constexpr int kClosingNote = 20;
// Opened from the Mini commit card's cog: 8 right of that card, and only
// while that leaves the card 240 or more; narrower, it hangs under the cog.
constexpr int kBesideGap = 8, kBesideMinWidth = 240;

// Fractional design pixels on the scale of ui::space(), for the radii the
// painter draws antialiased.
qreal spaceF(qreal px)
{
    return px * OmarchyTheme::instance()->fontBase() / 12.0;
}

// The design's regular 11 px small text (the stylesheet's %small%). The
// weight is spelled out: a painter fills in what a font leaves unset from the
// widget's own, which is bold on a chosen model row.
QFont smallFont()
{
    QFont f = OmarchyTheme::instance()->uiFont();
    f.setPixelSize(qRound(OmarchyTheme::instance()->fontBase() * 11 / 12.0));
    f.setBold(false);
    return f;
}

QString capitalised(const QString &s)
{
    return s.isEmpty() ? s : s.at(0).toUpper() + s.mid(1);
}

// One model: its name at the left, the id the CLI takes at the right, and the
// tick on the chosen one. Painted, so the three sit on the design's grid
// whatever the stylesheet says about buttons.
class ModelRow : public QAbstractButton
{
public:
    ModelRow(const QString &name, const QString &detail, bool checked)
    {
        setText(name);
        // What the row says beside the name, for whoever reads it aloud.
        setAccessibleDescription(detail);
        setCheckable(true);
        setAutoExclusive(true);
        setChecked(checked);
        setAttribute(Qt::WA_Hover);
        setFocusPolicy(Qt::TabFocus);
        setCursor(Qt::PointingHandCursor);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        refreshFont();
    }

    // The name's font: the theme's, bold on the chosen row.
    void refreshFont()
    {
        QFont f = OmarchyTheme::instance()->uiFont();
        f.setBold(isChecked());
        setFont(f);
        update();
    }

    QSize sizeHint() const override { return QSize(space(120), space(kRowHeight)); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const OmarchyTheme *t = OmarchyTheme::instance();
        QPainter p(this);
        const bool lit = isEnabled() && (underMouse() || hasFocus());
        if (lit)
            p.fillRect(rect(), t->hoverFill());

        const int nameX = space(kRowPad);
        p.setFont(font());
        p.setPen(!isEnabled() ? t->mutedText() : (isChecked() || lit) ? t->accent() : t->text());
        const int nameWidth = QFontMetrics(font()).horizontalAdvance(text());
        p.drawText(QRect(nameX, 0, qMax(0, width() - nameX), height()), Qt::AlignVCenter | Qt::AlignLeft, text());

        const QString detail = accessibleDescription();
        if (!detail.isEmpty()) {
            const QFont small = smallFont();
            const int right = width() - space(kDetailEnd);
            // The id gives way to the name at a narrow card, never the other way.
            const int room = right - (nameX + nameWidth + space(kRowPad));
            const QString shown = QFontMetrics(small).elidedText(detail, Qt::ElideRight, qMax(0, room));
            p.setFont(small);
            p.setPen(t->mutedText());
            p.drawText(QRect(0, 0, qMax(0, right), height()), Qt::AlignVCenter | Qt::AlignRight, shown);
        }

        if (isChecked()) {
            const QString tick = ui::icon(kCheck, QStringLiteral("✓")).trimmed();
            QFont glyph = t->uiFont();
            glyph.setBold(false);
            p.setFont(glyph);
            p.setPen(t->accent());
            const int box = space(kTick);
            p.drawText(QRect(width() - space(kRowPad) - box, 0, box, height()), Qt::AlignCenter | Qt::TextDontClip, tick);
        }
    }

    // A second click on the chosen row changes nothing, and neither does the
    // group's exclusivity, which would otherwise refuse to uncheck it anyway.
    void nextCheckState() override
    {
        if (!isChecked())
            setChecked(true);
    }
};

// A Nerd Font glyph at a size of its own. The stylesheet's font-size for every
// widget would win over a label's font, so this one paints its glyph itself.
class GlyphLabel : public QWidget
{
public:
    explicit GlyphLabel(uint glyph) : m_glyph(glyph) {}

protected:
    void paintEvent(QPaintEvent *) override
    {
        const OmarchyTheme *t = OmarchyTheme::instance();
        QFont f = t->uiFont();
        f.setPixelSize(space(kRobotSize));
        QPainter p(this);
        p.setRenderHint(QPainter::TextAntialiasing);
        p.setFont(f);
        p.setPen(t->mutedText());
        const int side = space(kRobotSize), inset = space(kRobotInset);
        p.drawText(QRect(inset, inset, side, side), Qt::AlignCenter | Qt::TextDontClip, ui::icon(m_glyph).trimmed());
    }

private:
    uint m_glyph;
};

// Where the MODEL section's list comes from, as the CLI is asked for it
// (AgentCli::probeArgs(), spelled out for the note).
QString probeNote(const QString &agent)
{
    if (agent == QLatin1String("claude"))
        return AgentPopover::tr("from claude --help");
    if (agent == QLatin1String("codex"))
        return AgentPopover::tr("from codex debug models");
    return QString();
}

// The flag the CLI takes a model name by, for the field's placeholder.
QString modelFlag(const QString &agent)
{
    return agent == QLatin1String("codex") ? QStringLiteral("-m") : QStringLiteral("--model");
}
} // namespace

// ---------------------------------------------------------------- LevelTrack

LevelTrack::LevelTrack(const QStringList &labels, QWidget *parent)
    : QWidget(parent), m_labels(labels)
{
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void LevelTrack::setSelected(int stop)
{
    stop = qBound(0, stop, qMax(0, int(m_labels.size()) - 1));
    if (m_selected == stop)
        return;
    m_selected = stop;
    update();
}

QPointF LevelTrack::stopCentre(int i) const
{
    const int n = int(m_labels.size());
    const qreal left = space(kTrackInset), right = width() - space(kTrackInset);
    const qreal step = n > 1 ? (right - left) / (n - 1) : 0;
    return QPointF(left + step * i, space(kTrackLine));
}

void LevelTrack::paintEvent(QPaintEvent *)
{
    const OmarchyTheme *t = OmarchyTheme::instance();
    const int n = int(m_labels.size());
    if (n == 0)
        return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal y = space(kTrackLine);
    const qreal left = space(kTrackInset), right = width() - space(kTrackInset);

    // The line, then the part of it up to the chosen level in the accent.
    p.fillRect(QRectF(left, y, right - left, 1), t->hairline());
    if (m_selected > 0) {
        QColor accent = t->accent();
        accent.setAlphaF(0.7);
        p.fillRect(QRectF(left, y - 0.5, stopCentre(m_selected).x() - left, 2), accent);
    }

    QFont plain = t->uiFont();
    plain.setPixelSize(t->captionFont().pixelSize());
    plain.setBold(false);
    QFont bold = plain;
    bold.setBold(true);
    const qreal labelY = y + space(kTrackLabel);
    for (int i = 0; i < n; ++i) {
        const QPointF c = stopCentre(i);
        const bool selected = i == m_selected;
        if (selected) {
            QColor halo = t->accent();
            // The keyboard is on the track: the halo says so a little louder.
            halo.setAlphaF(hasFocus() ? 0.4 : 0.25);
            p.setPen(Qt::NoPen);
            p.setBrush(halo);
            p.drawEllipse(c, spaceF(kHaloRadius), spaceF(kHaloRadius));
            p.setBrush(t->accent());
            p.drawEllipse(c, spaceF(kSelectedRadius), spaceF(kSelectedRadius));
        } else {
            QColor stroke = t->text();
            stroke.setAlphaF(i < m_selected ? 0.9 : 0.5);
            p.setPen(QPen(stroke, 1.5));
            p.setBrush(t->window());
            p.drawEllipse(c, spaceF(kStopRadius), spaceF(kStopRadius));
        }
        const QFont &f = selected ? bold : plain;
        p.setFont(f);
        p.setPen(selected ? t->accent() : t->mutedText());
        const int w = QFontMetrics(f).horizontalAdvance(m_labels.at(i)) + 2;
        const int h = QFontMetrics(f).height();
        p.drawText(QRectF(c.x() - w / 2.0, labelY - h / 2.0, w, h), Qt::AlignCenter | Qt::TextDontClip, m_labels.at(i));
    }
}

void LevelTrack::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || m_labels.isEmpty()) {
        QWidget::mousePressEvent(event);
        return;
    }
    const qreal x = event->position().x();
    int nearest = 0;
    for (int i = 1; i < m_labels.size(); ++i)
        if (qAbs(stopCentre(i).x() - x) < qAbs(stopCentre(nearest).x() - x))
            nearest = i;
    // Every click is a pick, the chosen stop's too: a saved level the track
    // does not have shows as Default, and a click there is what clears it.
    setSelected(nearest);
    emit picked(nearest);
}

void LevelTrack::keyPressEvent(QKeyEvent *event)
{
    const int step = event->key() == Qt::Key_Left ? -1 : event->key() == Qt::Key_Right ? 1 : 0;
    if (step == 0 || event->modifiers() != Qt::NoModifier) {
        QWidget::keyPressEvent(event);
        return;
    }
    const int next = qBound(0, m_selected + step, int(m_labels.size()) - 1);
    if (next != m_selected) {
        setSelected(next);
        emit picked(next);
    }
}

// ---------------------------------------------------------------- AgentPopover

AgentPopover::AgentPopover(CommitPage *page, QWidget *host)
    : QFrame(host), m_page(page)
{
    setObjectName(QStringLiteral("agentPopover"));
    // The shape that makes the stylesheet's border a frame the contents
    // stay inside of.
    setFrameShape(QFrame::StyledPanel);
    // A press on the card is the card's: none reaches the window beneath it.
    setAttribute(Qt::WA_NoMousePropagation);
    // The card itself takes the keyboard as it opens, so Escape works at once
    // with nothing inside it focused yet.
    setFocusPolicy(Qt::StrongFocus);

    m_layout = new QVBoxLayout(this);
    m_layout->setSpacing(0); // every gap is a spacer of the design's own

    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, &AgentPopover::dismiss);

    // Only the button's state follows a run starting or ending: a rebuild
    // would close an open other-model field under the user's fingers.
    connect(page, &CommitPage::commitControlsChanged, this, [this] {
        if (isVisible())
            followGenerating();
    });

    host->installEventFilter(this);
    hide();
    applyTheme();
}

QAbstractButton *AgentPopover::otherModelButton() const
{
    return m_otherButton;
}

void AgentPopover::applyTheme()
{
    const OmarchyTheme *theme = OmarchyTheme::instance();
    const int pad = space(kPad) - kFrame;
    m_layout->setContentsMargins(pad, pad, pad, pad);
    for (const auto &gap : std::as_const(m_gaps))
        gap.first->changeSize(0, space(gap.second), QSizePolicy::Fixed, QSizePolicy::Fixed);
    for (const auto &fixed : std::as_const(m_heights))
        fixed.first->setFixedHeight(space(fixed.second));
    // The family is the theme's; the size and the weight are the stylesheet's
    // (QLabel#agentPopoverNote, #agentPopoverSmall). A font set by hand
    // outranks the sheet until the label is polished again.
    for (QLabel *note : std::as_const(m_notes)) {
        note->setFont(theme->uiFont());
        note->style()->unpolish(note);
        note->style()->polish(note);
    }
    for (QLabel *label : std::as_const(m_bold)) {
        QFont f = theme->uiFont();
        f.setBold(true);
        label->setFont(f);
    }
    for (QWidget *glyph : std::as_const(m_glyphs)) {
        glyph->setFixedSize(space(kRobotColumn), space(kHeadline));
        glyph->update();
    }
    for (QAbstractButton *row : std::as_const(m_rows))
        static_cast<ModelRow *>(row)->refreshFont();
    if (m_picker) {
        for (SegmentButton *s : m_picker->segments())
            s->refreshGlyph();
        m_picker->update();
    }
    for (QWidget *row : std::as_const(m_commandRows)) {
        row->layout()->setContentsMargins(space(kCommandPad) - 1, 0, space(kCopyInset) - 1, 0);
    }
    if (m_otherButton) {
        // The design's text button: its label 10 in from either side. The
        // stylesheet leaves it no padding, so the width is all there is.
        m_otherButton->ensurePolished();
        m_otherButton->setFixedWidth(m_otherButton->fontMetrics().horizontalAdvance(m_otherButton->text())
                                     + 2 * space(kRowPad));
    }
    if (m_generate)
        m_generate->setText(icon(kSparkle, QStringLiteral("✨")) + tr("Generate now  Ctrl+G"));
    if (m_track)
        m_track->update();
    m_layout->invalidate();
    if (isVisible()) {
        place();
        scheduleLayout(); // and once more after the fonts have settled everywhere
    }
}

// ---- Building --------------------------------------------------------------

void AgentPopover::rebuild()
{
    // The keyboard stays on the card while its parts are replaced; whoever
    // asked for the rebuild puts it on the new part it belongs to.
    QWidget *focus = QApplication::focusWidget();
    if (focus && isAncestorOf(focus))
        setFocus(Qt::OtherFocusReason);

    while (QLayoutItem *item = m_layout->takeAt(0))
        delete item; // spacers and sub-layouts; the widgets are the card's children
    // The widget a click came from may still be on the stack: hidden now,
    // deleted once the event loop is back.
    const QList<QWidget *> old = findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget *w : old) {
        w->hide();
        w->deleteLater();
    }
    m_gaps.clear();
    m_heights.clear();
    m_notes.clear();
    m_glyphs.clear();
    m_bold.clear();
    m_commandRows.clear();
    m_picker = nullptr;
    m_rows.clear();
    m_otherButton = nullptr;
    m_otherField = nullptr;
    m_track = nullptr;
    m_generate = nullptr;
    m_copyButtons.clear();
    m_levels.clear();

    m_choice = CommitMessageAgent::savedChoice();
    const QList<AgentSpec> installed = CommitMessageAgent::installedAgents();
    if (installed.isEmpty())
        buildNoneInstalled();
    else
        buildInstalled(installed);
    applyTheme();
}

void AgentPopover::addGap(int px)
{
    auto *gap = new QSpacerItem(0, space(px), QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_gaps.append({gap, px});
    m_layout->addItem(gap);
}

void AgentPopover::fixHeight(QWidget *w, int px)
{
    m_heights.append({w, px});
    w->setFixedHeight(space(px));
}

void AgentPopover::addHeader(const QString &caption, const QString &note)
{
    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    // A strut of no width holds the row at the design's height; the caption
    // and the note sit at its top, on the line the design centres them on.
    auto *strut = new QSpacerItem(0, space(kHeaderRow), QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_gaps.append({strut, kHeaderRow});
    row->addItem(strut);
    QLabel *label = sectionLabel(caption);
    label->setParent(this);
    row->addWidget(label, 0, Qt::AlignTop);
    row->addStretch();
    if (!note.isEmpty()) {
        auto *n = new QLabel(note, this);
        // Not dimLabel, whose captions are bold: the note is the regular
        // caption-sized text of its own stylesheet rule.
        n->setObjectName(QStringLiteral("agentPopoverNote"));
        m_notes << n;
        row->addWidget(n, 0, Qt::AlignTop);
    }
    m_layout->addLayout(row);
}

void AgentPopover::buildInstalled(const QList<AgentSpec> &installed)
{
    const AgentSpec agent = CommitMessageAgent::spec(m_choice.agent);

    // AGENT: the installed ones, Omarchy's default named.
    const QString omarchy = CommitMessageAgent::omarchyDefaultAgent();
    const bool omarchyInstalled
        = std::any_of(installed.cbegin(), installed.cend(), [&](const AgentSpec &a) { return a.id == omarchy; });
    addHeader(tr("Agent"), omarchyInstalled ? tr("%1 is the Omarchy default").arg(CommitMessageAgent::spec(omarchy).binary)
                                            : QString());
    QList<SegmentButton *> segments;
    for (const AgentSpec &a : installed) {
        auto *s = toolButton<SegmentButton>(a.name, a.binary);
        s->setGlyph(kRobot, QString());
        s->setCheckable(true);
        s->setAutoExclusive(true);
        s->setChecked(a.id == m_choice.agent);
        s->setAccessibleName(a.name);
        connect(s, &QToolButton::clicked, this, [this, id = a.id] { chooseAgent(id); });
        segments << s;
    }
    m_picker = new SegmentStrip(segments, this);
    m_picker->setStretch(true);
    m_picker->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    fixHeight(m_picker, kRowHeight);
    m_layout->addWidget(m_picker);
    addGap(kSectionGap);

    // MODEL: Default, the CLI's own list, and a name typed earlier that the
    // list does not know, ahead of it.
    addHeader(tr("Model"), probeNote(m_choice.agent));
    const AgentCatalog catalog = CommitMessageAgent::catalog(m_choice.agent);
    QList<AgentModel> models = catalog.models;
    const bool known
        = std::any_of(models.cbegin(), models.cend(), [&](const AgentModel &m) { return m.id == m_choice.model; });
    if (!m_choice.model.isEmpty() && !known)
        models.prepend(AgentModel{m_choice.model, capitalised(m_choice.model), {}, {}});

    auto *def = new ModelRow(tr("Default"), tr("whatever %1 uses").arg(agent.binary), m_choice.model.isEmpty());
    def->setToolTip(tr("Whatever %1 is set to use").arg(agent.name));
    connect(def, &QAbstractButton::clicked, this, [this] { chooseModel(AgentModel()); });
    m_rows << def;
    for (const AgentModel &m : std::as_const(models)) {
        auto *row = new ModelRow(m.name, m.id, m.id == m_choice.model);
        row->setToolTip(m.id);
        connect(row, &QAbstractButton::clicked, this, [this, m] { chooseModel(m); });
        m_rows << row;
    }
    if (models.isEmpty() && !catalog.error.isEmpty()) {
        auto *error = new ModelRow(tr("Could not read the models"), QString(), false);
        error->setCheckable(false);
        error->setEnabled(false);
        error->setToolTip(catalog.error);
        m_rows << error;
    }
    for (QAbstractButton *row : std::as_const(m_rows)) {
        row->setParent(this);
        fixHeight(row, kRowHeight);
        m_layout->addWidget(row);
    }
    addGap(kOtherGap);

    // A name of one's own: a text button, and the field only once asked for.
    m_otherButton = toolButton(tr("Other model…"), tr("A model by name, as %1 %2 takes it").arg(agent.binary, modelFlag(agent.id)));
    m_otherButton->setParent(this);
    m_otherButton->setObjectName(QStringLiteral("ghostButton"));
    m_otherButton->setProperty("ghost", true);
    m_otherButton->setFocusPolicy(Qt::TabFocus);
    fixHeight(m_otherButton, kRowHeight);
    connect(m_otherButton, &QToolButton::clicked, this, &AgentPopover::openOtherField);
    m_layout->addWidget(m_otherButton, 0, Qt::AlignLeft);
    m_otherField = new QLineEdit(this);
    m_otherField->setPlaceholderText(tr("Model name, as %1 %2 takes it").arg(agent.binary, modelFlag(agent.id)));
    m_otherField->installEventFilter(this);
    fixHeight(m_otherField, kRowHeight);
    m_otherField->hide();
    connect(m_otherField, &QLineEdit::returnPressed, this, &AgentPopover::acceptOtherField);
    m_layout->addWidget(m_otherField);

    // REASONING, when the model has levels at all: an ordered scale, so a
    // track of stops rather than a list.
    const QStringList efforts = catalog.effortsFor(m_choice.model);
    if (!efforts.isEmpty()) {
        addGap(kSectionGap);
        addHeader(tr("Reasoning"), tr("more thinking, slower answer"));
        m_levels = QStringList{QString()} + efforts;
        QStringList labels{tr("Default")};
        for (const QString &level : efforts)
            labels << capitalised(level);
        m_track = new LevelTrack(labels, this);
        m_track->setSelected(qMax(0, int(m_levels.indexOf(m_choice.effort))));
        m_track->setToolTip(tr("Left and Right move along the levels"));
        fixHeight(m_track, kTrackHeight);
        connect(m_track, &LevelTrack::picked, this, &AgentPopover::chooseEffort);
        m_layout->addWidget(m_track);
    }

    addGap(kRuleGap);
    QWidget *rule = hairline();
    rule->setParent(this);
    m_layout->addWidget(rule);
    addGap(kRuleGap);

    // The window's Ctrl+G does the same; the label only names it.
    m_generate = new QPushButton(this);
    m_generate->setObjectName(QStringLiteral("agentGenerate"));
    m_generate->setDefault(true);
    m_generate->setCursor(Qt::PointingHandCursor);
    connect(m_generate, &QPushButton::clicked, this, &AgentPopover::generate);
    m_layout->addWidget(m_generate);
    followGenerating();

    setTabOrder(this, m_rows.first());
}

void AgentPopover::buildNoneInstalled()
{
    // What is missing, beside a robot.
    auto *top = new QHBoxLayout;
    top->setContentsMargins(0, 0, 0, 0);
    top->setSpacing(0);
    auto *robot = new GlyphLabel(kRobot);
    robot->setParent(this);
    m_glyphs << robot;
    top->addWidget(robot, 0, Qt::AlignTop);
    auto *lines = new QVBoxLayout;
    lines->setContentsMargins(0, 0, 0, 0);
    lines->setSpacing(0);
    auto *headline = new QLabel(tr("No coding agent installed"), this);
    headline->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    fixHeight(headline, kHeadline);
    m_bold << headline;
    lines->addWidget(headline);
    auto *subline = new QLabel(tr("Claude Code or Codex writes it for you."), this);
    subline->setObjectName(QStringLiteral("agentPopoverSmall"));
    subline->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    fixHeight(subline, kSubline);
    m_notes << subline;
    lines->addWidget(subline);
    top->addLayout(lines, 1);
    m_layout->addLayout(top);
    addGap(kSectionGap);

    // How to get one: the commands, each copyable.
    addHeader(tr("Install one"), QString());
    m_gaps.last().second = kInstallHeader; // the strut of that row: the design's 20, not 22
    const QStringList commands{QStringLiteral("omarchy default agent claude"), QStringLiteral("omarchy default agent codex")};
    for (const QString &command : commands) {
        auto *row = new QFrame(this);
        row->setObjectName(QStringLiteral("commandRow"));
        row->setFrameShape(QFrame::StyledPanel);
        auto *layout = new QHBoxLayout(row);
        layout->setSpacing(0);
        auto *text = new QLabel(QStringLiteral("$ ") + command);
        text->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(text, 1, Qt::AlignVCenter);
        QToolButton *copy = iconButton(kContentCopy, tr("Copy"), tr("Copy `%1`").arg(command));
        copy->setAccessibleName(tr("Copy %1").arg(command));
        connect(copy, &QToolButton::clicked, this, [this, command] {
            QGuiApplication::clipboard()->setText(command);
            emit m_page->statusMessage(tr("Copied"), 2000);
        });
        layout->addWidget(copy, 0, Qt::AlignVCenter);
        m_copyButtons << copy;
        m_commandRows << row;
        fixHeight(row, kCommandRow);
        m_layout->addWidget(row);
        addGap(kCommandGap);
    }
    auto *closing = new QLabel(tr("Reopen this menu once one is installed."), this);
    closing->setObjectName(QStringLiteral("agentPopoverSmall"));
    closing->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    fixHeight(closing, kClosingNote);
    m_notes << closing;
    m_layout->addWidget(closing);
}

// ---- Choosing --------------------------------------------------------------

void AgentPopover::chooseAgent(const QString &agent)
{
    if (agent == m_choice.agent)
        return;
    // A model and a level belong to the agent they were picked for.
    apply(AgentChoice{agent, QString(), QString()});
}

void AgentPopover::chooseModel(const AgentModel &model)
{
    if (model.id == m_choice.model)
        return;
    // A level the new model does not have goes back to its default.
    const QStringList levels = CommitMessageAgent::catalog(m_choice.agent).effortsFor(model.id);
    const QString effort = levels.isEmpty() || levels.contains(m_choice.effort) ? m_choice.effort : QString();
    apply(AgentChoice{m_choice.agent, model.id, effort});
}

void AgentPopover::chooseEffort(int stop)
{
    const QString effort = m_levels.value(stop);
    if (effort == m_choice.effort)
        return;
    apply(AgentChoice{m_choice.agent, m_choice.model, effort});
}

void AgentPopover::apply(const AgentChoice &choice)
{
    // Which part had the keyboard, so the new one of that part gets it back.
    QWidget *focus = QApplication::focusWidget();
    const bool onRow = m_rows.contains(qobject_cast<QAbstractButton *>(focus));
    const bool onTrack = focus && focus == m_track;
    const bool onCard = focus && (focus == this || isAncestorOf(focus));

    m_page->applyAgentChoice(choice);
    rebuild();

    if (onRow) {
        for (QAbstractButton *row : std::as_const(m_rows))
            if (row->isChecked())
                row->setFocus(Qt::OtherFocusReason);
    } else if (onTrack && m_track) {
        m_track->setFocus(Qt::OtherFocusReason);
    } else if (onCard) {
        setFocus(Qt::OtherFocusReason);
    }
    // An agent or a model with other levels makes the card another height.
    if (isVisible())
        place();
}

void AgentPopover::openOtherField()
{
    if (!m_otherField)
        return;
    const AgentCatalog catalog = CommitMessageAgent::catalog(m_choice.agent);
    const bool inCatalog = std::any_of(catalog.models.cbegin(), catalog.models.cend(),
                                       [this](const AgentModel &m) { return m.id == m_choice.model; });
    // Prefilled with a name of one's own, never with one the list offers.
    m_otherField->setText(inCatalog ? QString() : m_choice.model);
    m_otherButton->hide();
    m_otherField->show();
    m_otherField->setFocus(Qt::OtherFocusReason);
    m_otherField->selectAll();
    place();
}

void AgentPopover::closeOtherField()
{
    if (!m_otherField || !m_otherField->isVisible())
        return;
    m_otherField->hide();
    m_otherButton->show();
    setFocus(Qt::OtherFocusReason);
    place();
}

void AgentPopover::acceptOtherField()
{
    // An unknown model has no level list of its own, so the level stays.
    const QString model = m_otherField->text().trimmed();
    setFocus(Qt::OtherFocusReason); // the field goes with the rebuild
    apply(AgentChoice{m_choice.agent, model, m_choice.effort});
}

void AgentPopover::followGenerating()
{
    if (!m_generate)
        return;
    const bool generating = m_page->commitControls().generating;
    m_generate->setEnabled(!generating);
    m_generate->setToolTip(generating ? tr("The agent is writing the message — the sparkle stops it")
                                      : tr("Write the commit message now, with this agent, model and level"));
}

void AgentPopover::generate()
{
    dismiss();
    m_page->generateMessage();
}

// ---- Opening and closing ---------------------------------------------------

void AgentPopover::popup(QWidget *anchor)
{
    if (!anchor)
        return;
    const bool shown = isVisible();
    if (!shown) {
        QWidget *focus = QApplication::focusWidget();
        m_returnFocus = focus && focus->window() == window() ? focus : nullptr;
        rebuild();
    }
    if (anchor != m_anchor)
        setAnchor(anchor);
    place();
    if (!shown) {
        show();
        // Presses anywhere in the application are looked at while the card
        // is up, and only then.
        qApp->installEventFilter(this);
    }
    place(); // now that it can be measured on screen
    raise();
    setFocus(Qt::PopupFocusReason);
    if (!shown)
        emit opened();
}

void AgentPopover::dismiss()
{
    if (!isVisible())
        return;
    hide();
    qApp->removeEventFilter(this);
    if (m_returnFocus && m_returnFocus->isVisible())
        m_returnFocus->setFocus(Qt::OtherFocusReason);
    m_returnFocus = nullptr;
    emit dismissed();
}

void AgentPopover::setAnchor(QWidget *anchor)
{
    for (const QPointer<QWidget> &w : std::as_const(m_watched))
        if (w)
            w->removeEventFilter(this);
    m_watched.clear();
    m_anchor = anchor;
    // The cog moves with whatever holds it — the page's layout, or the commit
    // card growing upwards with its text — and the card follows it.
    QWidget *host = parentWidget();
    for (QWidget *w = anchor; w && w != host; w = w->parentWidget()) {
        w->installEventFilter(this);
        m_watched << w;
    }
}

void AgentPopover::scheduleLayout()
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

// At most 360 wide and never wider than the window's margins allow. From a
// cog on the overlay it is beside: 8 right of that overlay, level with its
// top, as long as that leaves it 240. Otherwise its right edge on the cog's
// right edge, as far left as the left margin lets it go, 6 under the cog.
// Either way moved up when the window is too short for it.
void AgentPopover::place()
{
    QWidget *host = parentWidget();
    if (!m_anchor || !host || m_placing)
        return;
    m_placing = true;
    const QMargins margins = host->layout() ? host->layout()->contentsMargins() : QMargins();
    const QRect cog(m_anchor->mapTo(host, QPoint(0, 0)), m_anchor->size());
    int width = qMax(1, qMin(space(kMaxWidth), host->width() - margins.left() - margins.right()));
    int left = qMax(margins.left(), cog.x() + cog.width() - width);
    int top = cog.y() + cog.height() + space(kCogGap);
    // The overlay is a child of the host too, so its geometry is in the
    // host's coordinates already.
    if (m_beside && m_beside->isVisible() && m_beside->isAncestorOf(m_anchor)) {
        const QRect beside = m_beside->geometry();
        const int besideLeft = beside.x() + beside.width() + space(kBesideGap);
        const int besideWidth = qMin(space(kMaxWidth), host->width() - margins.right() - besideLeft);
        if (besideWidth >= space(kBesideMinWidth)) {
            left = besideLeft;
            width = besideWidth;
            top = beside.y();
        }
    }
    if (this->width() != width)
        resize(width, height());
    m_layout->activate();
    const int height = sizeHint().height();
    const int bottom = host->height() - margins.bottom();
    if (top + height > bottom)
        top = qMax(margins.top(), bottom - height);
    setGeometry(left, top, width, height);
    m_placing = false;
}

bool AgentPopover::eventFilter(QObject *watched, QEvent *event)
{
    const QEvent::Type type = event->type();
    if (type == QEvent::MouseButtonPress) {
        // A press in this window, outside the card, closes it and goes on to
        // whatever it was for. The cog is left to its own click, which
        // toggles the card. Menus and dialogs are windows of their own.
        if (isVisible() && watched->isWidgetType() && static_cast<QWidget *>(watched)->window() == window()) {
            const QPoint global = static_cast<QMouseEvent *>(event)->globalPosition().toPoint();
            const bool onCard = rect().contains(mapFromGlobal(global));
            const bool onAnchor
                = m_anchor && m_anchor->isVisible() && m_anchor->rect().contains(m_anchor->mapFromGlobal(global));
            if (!onCard && !onAnchor)
                dismiss();
        }
        return false;
    }
    if (watched == m_otherField) {
        // Escape closes the field before it closes the card: the field takes
        // the key ahead of the card's shortcut.
        if (type == QEvent::ShortcutOverride || type == QEvent::KeyPress) {
            auto *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Escape && key->modifiers() == Qt::NoModifier) {
                if (type == QEvent::ShortcutOverride)
                    event->accept();
                else
                    closeOtherField();
                return type == QEvent::KeyPress;
            }
        }
        return false;
    }
    if (watched == m_anchor && type == QEvent::Hide) {
        // The cog went — the commit card closed, the history came up, the
        // layout changed: the card has nothing left to hang from.
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
