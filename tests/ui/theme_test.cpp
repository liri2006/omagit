// The theme and the kit: colors.toml and its fallbacks, the kit's controls in
// grid units, the density of the window classes, and the whole UI following a
// live change of the desktop's text size.
#include "UiTestCase.h"
#include "fixtures.h"

#include "../../src/Segmented.h"
#include "../../src/UiHelpers.h"

#include <QApplication>
#include <QFontMetricsF>
#include <QLineEdit>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QVBoxLayout>

namespace {

// One of each control the kit measures in scaled pixels, in a shown host so
// the stylesheet's own padding is part of the numbers. The live text-size
// test builds one, changes the desktop's text size under it, and holds it
// against a kit built fresh at the new size.
struct Kit
{
    std::unique_ptr<QWidget> host;
    QToolButton *inlineButton = nullptr;
    QToolButton *toolbarButton = nullptr;
    QToolButton *textButton = nullptr; // what a toolbar icon button has to be as tall as
    QLineEdit *promptField = nullptr;
    QWidget *promptBox = nullptr;
    QWidget *header = nullptr;

    QList<int> metrics() const
    {
        return {inlineButton->width(),       inlineButton->height(),
                toolbarButton->width(),      toolbarButton->height(),
                promptField->height(),       promptBox->sizeHint().width(),
                promptBox->sizeHint().height(), header->height()};
    }
};

Kit buildKit()
{
    Kit kit;
    kit.host.reset(new QWidget);
    auto *rows = new QVBoxLayout(kit.host.get());
    kit.inlineButton = ui::iconButton(ui::kCog, QStringLiteral("⚙"), QStringLiteral("Agent"));
    kit.toolbarButton = ui::iconButton(ui::kRefresh, QStringLiteral("R"), QStringLiteral("Refresh"),
                                       ui::IconButtonSize::Toolbar, false);
    kit.textButton = ui::toolButton<ui::KitButton>(QStringLiteral("x"));
    kit.promptField = ui::promptField(QStringLiteral("Search branches…"));
    kit.promptBox = ui::promptBox(kit.promptField);
    kit.header = new QWidget;
    kit.header->setLayout(ui::sectionHeaderRow(ui::sectionLabel(QStringLiteral("MESSAGE"))));
    rows->addWidget(kit.inlineButton);
    rows->addWidget(kit.toolbarButton);
    rows->addWidget(kit.textButton);
    rows->addWidget(kit.promptBox);
    rows->addWidget(kit.header);
    rows->addStretch(1); // the leftover height is the stretch's, not a control's
    kit.host->resize(400, 400); // both kits are laid out in the same box
    kit.host->show();
    return kit;
}

// What a kit built from scratch at the text size of the moment measures.
QList<int> freshKitMetrics()
{
    Kit kit = buildKit();
    (void)QTest::qWaitForWindowExposed(kit.host.get());
    settle();
    return kit.metrics();
}

// What a commit page measures in scaled pixels, spelled out so a mismatch
// names itself: the design's checkbox column, the gaps of the section grid,
// the handle the list and the message share, the dividers of the CHANGES row
// and the gaps between its buttons, and the tree's own two narrow columns.
QStringList pageMetrics(CommitPage *page)
{
    auto *splitter = page->findChild<QSplitter *>(QStringLiteral("commitMessageSplitter"));
    QWidget *changes = splitter ? splitter->widget(0) : nullptr;
    // The children a single pixel wide are the two dividers of the CHANGES
    // row (switcher | eye | Refresh); their heights are the page's to set.
    QStringList dividers;
    for (const QWidget *w : page->findChildren<QWidget *>())
        if (w->minimumWidth() == 1 && w->maximumWidth() == 1)
            dividers << QString::number(w->height());
    // The gaps the switcher keeps: between the three buttons, and around the
    // dividers. Measured off the laid-out row, not off the spacers.
    QStringList gaps;
    const QList<QToolButton *> row{page->treeButton(), page->compactButton(), page->tableButton(),
                                   page->unversionedButton()};
    for (int i = 1; i < row.size(); ++i)
        gaps << QString::number(row.at(i)->mapTo(page, QPoint(0, 0)).x()
                                - (row.at(i - 1)->mapTo(page, QPoint(0, 0)).x() + row.at(i - 1)->width()));
    const QCheckBox *amend = page->findChild<QCheckBox *>();
    const auto entry = [](const char *name, int value) { return QStringLiteral("%1=%2").arg(QLatin1String(name)).arg(value); };
    return {entry("check", page->table()->columnWidth(ChangesModel::Check)),
            entry("row", page->table()->verticalHeader()->defaultSectionSize()),
            entry("actionBarGap", page->layout()->spacing()),
            entry("headerGap", changes ? changes->layout()->spacing() : -1),
            entry("handle", splitter ? splitter->handle(1)->height() : -1),
            QStringLiteral("dividers=") + dividers.join(QLatin1Char(',')),
            QStringLiteral("switcherGaps=") + gaps.join(QLatin1Char(',')),
            entry("treeCheck", page->tree()->columnWidth(ChangesTreeModel::Check)),
            entry("treeStatus", page->tree()->columnWidth(ChangesTreeModel::Status)),
            entry("switcher", page->treeButton()->width()),
            entry("amendWidth", amend->width()),
            QStringLiteral("amend=") + amend->text()};
}

// What the tree measures at the text size of the moment: its row height and
// where the depth geometry of a nested row lands.
QStringList treeMetrics(CommitPage *page)
{
    QTreeView *tree = page->tree();
    auto *model = qobject_cast<ChangesTreeModel *>(tree->model());
    const auto entry = [](const char *name, int value) { return QStringLiteral("%1=%2").arg(QLatin1String(name)).arg(value); };
    QStringList out{entry("check", tree->columnWidth(ChangesTreeModel::Check)),
                    entry("status", tree->columnWidth(ChangesTreeModel::Status))};
    for (const QString &path : {QStringLiteral("src"), QStringLiteral("src/deep"), QStringLiteral("src/a.txt")}) {
        const QModelIndex index = treeRow(model, path);
        if (!index.isValid())
            continue;
        const QRect rect = tree->visualRect(index.siblingAtColumn(ChangesTreeModel::Name));
        out << QStringLiteral("%1=%2,%3,%4").arg(path).arg(rect.left()).arg(rect.height()).arg(model->depth(index));
    }
    return out;
}

// The narrowest page that still spells "Amend last commit" out: what the
// action bar folds at, and so the width to compare two pages at.
int amendFoldWidth(CommitPage *page)
{
    int folded = 160, whole = 900; // the label is short at one end, long at the other
    while (folded + 1 < whole) {
        const int middle = (folded + whole) / 2;
        page->resize(middle, 600);
        settle();
        if (page->findChild<QCheckBox *>()->text() == QLatin1String("Amend"))
            folded = middle;
        else
            whole = middle;
    }
    return whole;
}

// What the rail's commit section and the card measure at the text size of
// the moment, spelled out so a mismatch names itself.
QStringList popoverMetrics(const WindowFixture &f)
{
    MiniRail *rail = f.rail();
    QToolButton *tile = f.tile();
    CommitPopover *card = f.popover();
    QWidget *host = f.host();
    QWidget *rule = tileRule(f);
    const QRect square = tileSquare(tile).translated(rectIn(tile, rail).topLeft());
    const QRect ruleRect = rule ? rectIn(rule, rail) : QRect();
    const QRect cardRect = card->geometry();
    const QRect tileRect = rectIn(tile, host);
    const QRect railRect = rail->geometry();
    const QRect editor = rectIn(card->editor(), card);
    const QMargins m = card->layout()->contentsMargins();
    const QFont hint = card->hintLabel()->font();
    const auto entry = [](const char *name, int value) { return QStringLiteral("%1=%2").arg(QLatin1String(name)).arg(value); };
    return {entry("rail", rail->width()),
            QStringLiteral("tile=%1x%2").arg(tile->width()).arg(tile->height()),
            entry("square", square.width()),
            entry("ruleGap", ruleRect.top() - (rectIn(f.railRefresh(), rail).bottom() + 1)),
            entry("tileGap", square.top() - (ruleRect.bottom() + 1)),
            entry("cardGap", cardRect.x() - (railRect.x() + railRect.width())),
            entry("cardWidth", cardRect.width()),
            entry("anchor", (cardRect.y() + cardRect.height()) - (tileRect.y() + tileRect.height())),
            QStringLiteral("margins=%1,%2,%3,%4").arg(m.left()).arg(m.top()).arg(m.right()).arg(m.bottom()),
            entry("editorTop", editor.top()),
            entry("editorLeft", editor.left()),
            entry("editorHeight", editor.height()),
            entry("hintHeight", card->hintLabel()->height()),
            entry("hintPx", hint.pixelSize()),
            entry("hintBold", hint.bold()),
            entry("cardHeight", cardRect.height())};
}

// What the agent card measures at the text size of the moment, spelled out
// so a mismatch names itself: the installed state's parts, or the command
// rows of the none-installed one.
QStringList agentCardMetrics(const AgentPopover *card)
{
    QStringList out;
    const auto entry = [&out](const char *name, int value) { out << QStringLiteral("%1=%2").arg(QLatin1String(name)).arg(value); };
    const QMargins m = card->layout()->contentsMargins();
    out << QStringLiteral("margins=%1,%2,%3,%4").arg(m.left()).arg(m.top()).arg(m.right()).arg(m.bottom());
    entry("width", card->width());
    entry("height", card->height());
    if (card->agentPicker()) {
        entry("picker", card->agentPicker()->height());
        entry("pickerTop", card->agentPicker()->y());
    }
    for (const QAbstractButton *row : card->modelRows()) {
        entry("row", row->height());
        entry("rowBold", row->font().bold());
    }
    if (card->otherModelButton()) {
        entry("otherTop", card->otherModelButton()->y());
        entry("other", card->otherModelButton()->height());
        entry("otherWidth", card->otherModelButton()->width());
    }
    if (const LevelTrack *track = card->levelTrack()) {
        entry("track", track->height());
        entry("trackTop", track->y());
        entry("stop0", qRound(track->stopCentre(0).x()));
        entry("stopY", qRound(track->stopCentre(0).y()));
    }
    for (const QLabel *l : card->findChildren<QLabel *>()) {
        if (!l->isVisible())
            continue;
        if (l->objectName() == QLatin1String("agentPopoverNote") || l->objectName() == QLatin1String("agentPopoverSmall")) {
            entry("notePx", l->font().pixelSize());
            entry("noteBold", l->font().bold());
        }
    }
    for (const QFrame *row : card->findChildren<QFrame *>(QStringLiteral("commandRow"))) {
        if (!row->isVisible())
            continue;
        entry("command", row->height());
        entry("commandTop", row->y());
        const QMargins rm = row->layout()->contentsMargins();
        entry("commandPad", rm.left());
    }
    for (const QAbstractButton *copy : card->copyButtons())
        entry("copy", copy->width());
    return out;
}

} // namespace

class ThemeTest : public UiTestCase
{
    Q_OBJECT
private slots:
    // A segment leaves the strip and comes back through setSegmentVisible():
    // the hint, the placement and the dividers follow at once, even though
    // the strip's own rectangle does not move.
    void segmentStripHidesAndShowsASegment()
    {
        QWidget host;
        QList<SegmentButton *> segments;
        for (const QString &name : {QStringLiteral("One"), QStringLiteral("Two"), QStringLiteral("Three")}) {
            auto *s = ui::toolButton<SegmentButton>(name);
            s->setGlyph(ui::kCommit, QStringLiteral("C"));
            segments << s;
        }
        auto *strip = new SegmentStrip(segments, &host);
        const QSize all = strip->sizeHint();
        const int middle = segments.at(1)->sizeHint().width();
        strip->setGeometry(0, 0, all.width(), all.height());
        host.resize(all.width() + 40, all.height() + 40);
        host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        settle();

        // The columns painted in the frame's colour between the segments.
        const auto dividers = [strip] {
            const QImage shot = strip->grab().toImage();
            const QRgb line = OmarchyTheme::instance()->normalBorder().rgb() | 0xff000000;
            QList<int> out;
            for (int x = 1; x < strip->width() - 1; ++x) {
                bool inside = false;
                for (const SegmentButton *s : strip->segments())
                    if (!s->isHidden() && x >= s->x() && x < s->x() + s->width())
                        inside = true;
                if (!inside && (shot.pixel(x, strip->height() / 2) | 0xff000000) == line)
                    out << x;
            }
            return out;
        };
        QCOMPARE(dividers(), QList<int>({segments.at(1)->x() - 1, segments.at(2)->x() - 1}));

        const QRect frame = strip->geometry();
        strip->setSegmentVisible(segments.at(1), false);
        QCOMPARE(strip->geometry(), frame);
        QVERIFY(segments.at(1)->isHidden());
        // The boxes side by side, the lines inside them: the hidden one's box
        // and nothing more goes.
        QCOMPARE(strip->sizeHint(), QSize(all.width() - middle, all.height()));
        QCOMPARE(segments.at(0)->x(), 1);
        QCOMPARE(segments.at(2)->x(), segments.at(0)->x() + segments.at(0)->width() + 1);
        QCOMPARE(segments.at(2)->x() + segments.at(2)->width(), frame.width() - 1); // the last takes the rest
        settle();
        QCOMPARE(dividers(), QList<int>({segments.at(2)->x() - 1}));

        // A hidden ancestor changes nothing about the hint.
        const QSize two = strip->sizeHint();
        host.hide();
        QCOMPARE(strip->sizeHint(), two);
        host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));

        // Stretch: the two taking part share the width.
        strip->setStretch(true);
        settle();
        const int each = frame.width() / 2;
        QCOMPARE(segments.at(0)->geometry(), QRect(1, 1, each - 1, frame.height() - 2));
        QCOMPARE(segments.at(2)->geometry(), QRect(each + 1, 1, frame.width() - 1 - (each + 1), frame.height() - 2));

        // And back: three again, in their order, one divider per pair.
        strip->setStretch(false);
        strip->setSegmentVisible(segments.at(1), true);
        QCOMPARE(strip->sizeHint(), all);
        QCOMPARE(strip->geometry(), frame);
        QVERIFY(segments.at(0)->x() < segments.at(1)->x());
        QVERIFY(segments.at(1)->x() < segments.at(2)->x());
        settle();
        QCOMPARE(dividers(), QList<int>({segments.at(1)->x() - 1, segments.at(2)->x() - 1}));
    }

    // The segmented control the top bar and the agent picker share: at its
    // hints the first segment takes its own width and the last the rest; in
    // stretch mode each gets floor(width / n) and the last the remainder,
    // with its content centred; a single segment fills the strip.
    void segmentStripLaysOutItsSegments()
    {
        const auto segment = [](const QString &label) {
            auto *s = ui::toolButton<SegmentButton>(label);
            s->setGlyph(ui::kRobot, QStringLiteral("R"));
            s->setCheckable(true);
            return s;
        };
        QWidget host;
        host.resize(600, 100);
        auto *first = segment(QStringLiteral("Claude Code"));
        auto *second = segment(QStringLiteral("Codex"));
        auto *strip = new SegmentStrip({first, second}, &host);
        host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        // The segments' boxes side by side, the frame and the divider drawn
        // inside them (kit.js segmented()): each widget is its box less the
        // line it starts with, the last one less the frame's right edge too.
        QCOMPARE(strip->sizeHint().width(), first->sizeHint().width() + second->sizeHint().width());
        QCOMPARE(strip->sizeHint().height(), ui::space(ui::box::control));
        QCOMPARE(first->sizeHint().height(), ui::space(ui::box::control));
        strip->setGeometry(0, 0, strip->sizeHint().width() + 40, 30);
        QCOMPARE(first->geometry(), QRect(1, 1, first->sizeHint().width() - 1, 28));
        QCOMPARE(second->geometry(), QRect(first->sizeHint().width() + 1, 1,
                                           strip->width() - first->sizeHint().width() - 2, 28));

        strip->setStretch(true);
        QVERIFY(first->isCentred() && second->isCentred());
        strip->setGeometry(0, 0, 301, 30);
        const int each = 301 / 2;
        QCOMPARE(first->geometry(), QRect(1, 1, each - 1, 28));
        QCOMPARE(second->geometry(), QRect(each + 1, 1, 301 - 1 - (each + 1), 28));
        // The ink of a centred segment keeps the same distance from either side.
        const auto inkMargins = [](QWidget *w) {
            const QImage image = w->grab().toImage();
            const QColor fill = image.pixelColor(0, 0);
            int left = image.width(), right = -1;
            for (int x = 0; x < image.width(); ++x)
                for (int y = 0; y < image.height(); ++y)
                    if (!closeTo(image.pixelColor(x, y), fill)) {
                        left = qMin(left, x);
                        right = qMax(right, x);
                    }
            return qMakePair(left, image.width() - 1 - right);
        };
        const auto margins = inkMargins(second);
        // The glyph's ink is narrower than the box it is laid out in.
        QVERIFY2(qAbs(margins.first - margins.second) <= ui::space(ui::box::icon) / 2,
                 qPrintable(QStringLiteral("%1 %2").arg(margins.first).arg(margins.second)));

        auto *third = segment(QStringLiteral("Three"));
        SegmentStrip three({segment(QStringLiteral("One")), segment(QStringLiteral("Two")), third});
        three.setStretch(true);
        three.resize(302, 30);
        three.show();
        QVERIFY(QTest::qWaitForWindowExposed(&three));
        QCOMPARE(three.segments().size(), 3);
        QCOMPARE(three.segments().at(0)->geometry(), QRect(1, 1, 99, 28));
        QCOMPARE(three.segments().at(1)->geometry(), QRect(101, 1, 99, 28));
        QCOMPARE(third->geometry(), QRect(201, 1, 100, 28));

        auto *only = segment(QStringLiteral("Claude Code"));
        SegmentStrip one({only});
        one.resize(250, 30);
        one.show();
        QVERIFY(QTest::qWaitForWindowExposed(&one));
        QCOMPARE(only->geometry(), QRect(1, 1, 248, 28));
        one.setStretch(true);
        QCOMPARE(only->geometry(), QRect(1, 1, 248, 28));
    }

    // The kit's text button measures like the design's measureButton(): 10,
    // the 14 px glyph, 6, the label, then 6 and the 12 px chevron of a
    // dropdown, 10; 28 tall. A glyph alone, and the icon form, is the 28 px
    // square.
    void kitButtonsMeasureLikeTheDesign()
    {
        const auto label = [](const QAbstractButton *b, const QString &text) {
            return qCeil(QFontMetricsF(b->font()).horizontalAdvance(text));
        };
        // kit.js measureButton(): [8][glyph box 16][4][label][4][chevron box
        // 12][8], 28 high; the primary action pads 16.
        std::unique_ptr<ui::KitButton> plain(ui::toolButton<ui::KitButton>(QStringLiteral("Load more")));
        QCOMPARE(plain->sizeHint(), QSize(ui::space(8 + 8) + label(plain.get(), QStringLiteral("Load more")), ui::space(28)));
        ui::setPrimary(plain.get());
        QCOMPARE(plain->sizeHint(), QSize(ui::space(16 + 16) + label(plain.get(), QStringLiteral("Load more")), ui::space(28)));
        if (ui::icon(ui::kPull).isEmpty())
            QSKIP("the font has no Nerd Font glyphs");
        std::unique_ptr<ui::KitButton> pull(ui::toolButton<ui::KitButton>(ui::icon(ui::kPull) + QStringLiteral("Pull")));
        QCOMPARE(pull->sizeHint(), QSize(ui::space(8 + 16 + 4 + 8) + label(pull.get(), QStringLiteral("Pull")), ui::space(28)));
        std::unique_ptr<ui::KitButton> chip(
            ui::toolButton<ui::KitButton>(ui::icon(ui::kBranch) + QStringLiteral("main") + ui::chevron()));
        QCOMPARE(chip->sizeHint().width(), ui::space(8 + 16 + 4 + 4 + 12 + 8) + label(chip.get(), QStringLiteral("main")));
        std::unique_ptr<ui::KitButton> dropdown(ui::toolButton<ui::KitButton>(ui::icon(ui::kSplit) + ui::chevron()));
        QCOMPARE(dropdown->sizeHint().width(), ui::space(8 + 16 + 4 + 12 + 8));
        std::unique_ptr<ui::KitButton> glyph(ui::toolButton<ui::KitButton>(ui::icon(ui::kPull).trimmed()));
        QCOMPARE(glyph->sizeHint(), QSize(ui::space(28), ui::space(28)));
        ui::setIconForm(pull.get(), true);
        QCOMPARE(pull->sizeHint(), QSize(ui::space(28), ui::space(28)));
        ui::setIconForm(pull.get(), true, 32); // the top bar's sync buttons: 8 + 16 + 8
        QCOMPARE(pull->sizeHint(), QSize(ui::space(32), ui::space(28)));
        QCOMPARE(pull->width(), ui::space(32));
        // A push button with the kit's face: the primary action's 16.
        ui::KitPushButton commit;
        commit.setText(ui::icon(ui::kCommit) + QStringLiteral("Commit 5 files"));
        ui::setPrimary(&commit);
        QCOMPARE(commit.sizeHint(),
                 QSize(ui::space(16 + 16 + 4 + 16) + label(&commit, QStringLiteral("Commit 5 files")), ui::space(28)));
    }

    // The popup prompt carries no box of its own — the popup's accent frame
    // is the focus cue — and its magnifier is a widget, not a prefix of the
    // placeholder, so it stays while something is typed.
    void promptFieldIsBorderlessAndKeepsItsMagnifier()
    {
        QLineEdit *field = ui::promptField(QStringLiteral("Search branches…"));
        std::unique_ptr<QWidget> box(ui::promptBox(field));
        QCOMPARE(field->objectName(), QString("promptField"));
        QCOMPARE(field->placeholderText(), QString("Search branches…"));
        QVERIFY(!field->hasFrame());
        QCOMPARE(field->height(), ui::space(28));
        QCOMPARE(box->findChild<QLineEdit *>(QStringLiteral("promptField")), field);
        // The magnifier, if the font has one...
        if (!ui::icon(ui::kMagnify).isEmpty())
            QVERIFY(box->findChild<QLabel *>(QStringLiteral("promptIcon")));
        // ...and the hairline under the row, which is part of the box so the
        // filtering below it can never take it away.
        QWidget *hair = nullptr;
        for (QWidget *child : box->findChildren<QWidget *>())
            if (child != field && !qobject_cast<QLabel *>(child))
                hair = child;
        QVERIFY(hair);
        QCOMPARE(hair->height(), 1);
    }

    // Every measurement of the kit is in 12 px-base pixels and grows with the
    // desktop's text size, on one grid unit scaled once: round(4 × base / 12),
    // so sums of the design's multiples of 4 add up exactly at every size.
    // Text sizes are not grid values: they scale on their own.
    void spacingFollowsTheBaseFontSize()
    {
        QCOMPARE(ui::space(12), OmarchyTheme::instance()->fontBase());
        QTemporaryDir dir, home;
        QVERIFY(dir.isValid() && home.isValid());
        for (const int base : {9, 12, 14, 18}) {
            QVERIFY(writeFixture(QDir(dir.path()).filePath(QStringLiteral("shell.toml")),
                                 QStringLiteral("[font]\nbase-size = %1\n").arg(base).toUtf8()));
            ScopedEnv themeDir("OMAGIT_THEME_DIR", dir.path().toUtf8());
            // The scratch home keeps the desktop's own shell.toml, which would
            // be read after the theme's, out of the way.
            ScopedEnv scratchHome("HOME", home.path().toUtf8());
            OmarchyTheme theme;
            const QByteArray where = QByteArray::number(base);
            QVERIFY2(theme.fontBase() == base, where.constData());
            const int unit = qMax(1, qRound(4 * base / 12.0));
            QVERIFY2(ui::gridUnit() == unit, where.constData());
            QVERIFY2(ui::space(4) == unit, where.constData());
            QVERIFY2(ui::space(4) + ui::space(4) == ui::space(8), where.constData());
            QVERIFY2(ui::space(24) == 6 * ui::space(4), where.constData());
            QVERIFY2(ui::space(28) == ui::space(24) + ui::space(4), where.constData());
            QVERIFY2(ui::space(ui::box::row) + ui::space(ui::gap::header)
                         == ui::space(ui::box::control) + ui::space(ui::gap::controlRow),
                     where.constData());
            QVERIFY2(ui::space(ui::kBar + ui::box::control + ui::kBar) == 11 * unit, where.constData());
            QVERIFY2(ui::fontPx(11) == qRound(11 * base / 12.0), where.constData());
            QVERIFY2(ui::space(0) == 1, where.constData()); // never nothing at all
        }
        {
            QVERIFY(writeFixture(QDir(dir.path()).filePath(QStringLiteral("shell.toml")), "[font]\nbase-size = 18\n"));
            ScopedEnv themeDir("OMAGIT_THEME_DIR", dir.path().toUtf8());
            ScopedEnv scratchHome("HOME", home.path().toUtf8());
            OmarchyTheme theme;
            QCOMPARE(ui::space(12), 18);
            QCOMPARE(ui::space(24), 36);
            QCOMPARE(ui::space(1), 2); // the rest of a unit, rounded
            QCOMPARE(ui::space(2), 3);
        }
        g_theme.reset(new OmarchyTheme);
        g_theme->apply(*qApp);
    }

    // The window's density (screens.js density()): the side margins by the
    // width class, the block gap by the height class.
    void theDensityFollowsTheWindowsClasses()
    {
        const QList<QPair<WidthClass, int>> margins{
            {WidthClass::Wide, 16}, {WidthClass::Large, 12}, {WidthClass::Medium, 12}, {WidthClass::Stacked, 8}};
        const QList<QPair<HeightClass, int>> blocks{
            {HeightClass::Shallow, 4}, {HeightClass::Normal, 8}, {HeightClass::Tall, 12}};
        for (const auto &[width, margin] : margins) {
            for (const auto &[height, block] : blocks) {
                const ui::Density d = ui::densityFor(width, height);
                QCOMPARE(d.margin, margin);
                QCOMPARE(d.block, block);
            }
        }
        QCOMPARE(ui::kRegularDensity.margin, 12);
        QCOMPARE(ui::kRegularDensity.block, 8);
        // Outside the window, the regular margin.
        QWidget lone;
        QCOMPARE(ui::windowMargin(&lone), ui::space(12));
        ui::setWindowMargin(&lone, 16);
        QCOMPARE(ui::windowMargin(&lone), ui::space(16));
    }

    // `omarchy display text size` rewrites shell.toml under a running window:
    // every control already on screen has to end up where one built fresh at
    // the new size starts, and find its way back down again.
    void theKitFollowsALiveTextSizeChange()
    {
        QTemporaryDir dir, home, noPath;
        QVERIFY(dir.isValid() && home.isValid() && noPath.isValid());
        const QString toml = QDir(dir.path()).filePath(QStringLiteral("shell.toml"));
        QVERIFY(writeFixture(toml, "[font]\nbase-size = 12\n"));
        {
            ScopedEnv themeDir("OMAGIT_THEME_DIR", dir.path().toUtf8());
            // The scratch home keeps the desktop's own shell.toml, which would
            // be read after the theme's, out of the way; a PATH with nothing on
            // it hides omarchy-font-current, so a reload's font query answers
            // on the spot instead of from a process.
            ScopedEnv scratchHome("HOME", home.path().toUtf8());
            ScopedEnv emptyPath("PATH", noPath.path().toUtf8());
            OmarchyTheme theme;
            QCOMPARE(theme.fontBase(), 12);
            theme.apply(*qApp);
            QSignalSpy changed(&theme, &OmarchyTheme::changed);

            Kit live = buildKit(); // built once, at 12, and never built again
            QVERIFY(QTest::qWaitForWindowExposed(live.host.get()));
            settle();
            QCOMPARE(live.metrics(), freshKitMetrics());
            const QList<int> atTwelve = live.metrics();

            // The desktop's text size goes up under the live controls: the
            // watcher notices the rewritten file and the theme reloads.
            QVERIFY(writeFixture(toml, "[font]\nbase-size = 18\n"));
            QTRY_COMPARE_WITH_TIMEOUT(theme.fontBase(), 18, 10000);
            settle();
            QCOMPARE(changed.count(), 1);
            QCOMPARE(live.metrics(), freshKitMetrics());
            QVERIFY(live.metrics() != atTwelve); // everything measured did move
            QCOMPARE(live.inlineButton->size(), QSize(ui::space(24), ui::space(24)));
            QCOMPARE(live.toolbarButton->width(), ui::space(28));
            QCOMPARE(live.promptField->height(), ui::space(28));
            QCOMPARE(live.header->height(), ui::space(ui::box::row));
            // The toolbar one is as tall as a text button, whatever that is
            // with this font, not the 28 px the design names for its width.
            QCOMPARE(live.toolbarButton->sizeHint().height(), live.textButton->sizeHint().height());
            QCOMPARE(live.toolbarButton->height(), live.textButton->height());

            // ...and back down to where it started.
            QVERIFY(writeFixture(toml, "[font]\nbase-size = 12\n"));
            QTRY_COMPARE_WITH_TIMEOUT(theme.fontBase(), 12, 10000);
            settle();
            QCOMPARE(live.metrics(), freshKitMetrics());
            QCOMPARE(live.metrics(), atTwelve);
            QCOMPARE(live.toolbarButton->height(), live.textButton->height());

            live.host.reset(); // the controls go before the theme they follow
        }
        g_theme.reset(new OmarchyTheme);
        g_theme->apply(*qApp);
        QVERIFY(OmarchyTheme::instance() == g_theme.get());
    }

    // The same live change, for the top bar: one bar built at 12 and left
    // standing has to measure, fold and spell itself out like a bar built
    // fresh at every size the desktop moves to.
    void theTopBarFollowsALiveTextSizeChange()
    {
        QTemporaryDir dir, home, noPath;
        QVERIFY(dir.isValid() && home.isValid() && noPath.isValid());
        const QString toml = QDir(dir.path()).filePath(QStringLiteral("shell.toml"));
        QVERIFY(writeFixture(toml, "[font]\nbase-size = 12\n"));
        {
            ScopedEnv themeDir("OMAGIT_THEME_DIR", dir.path().toUtf8());
            ScopedEnv scratchHome("HOME", home.path().toUtf8());
            ScopedEnv emptyPath("PATH", noPath.path().toUtf8());
            OmarchyTheme theme;
            QCOMPARE(theme.fontBase(), 12);
            theme.apply(*qApp);

            // TopBar::applyTheme() on every change is what MainWindow does.
            BarFixture live = topBar();
            QObject::connect(&theme, &OmarchyTheme::changed, live.bar, [bar = live.bar] { bar->applyTheme(); });
            QVERIFY(QTest::qWaitForWindowExposed(live.host.get()));
            settle();

            const auto matchesAFreshBar = [&live] {
                BarFixture fresh = topBar();
                QVERIFY(QTest::qWaitForWindowExposed(fresh.host.get()));
                settle();
                // Both at their size hint: a segment's hint is the form it
                // wears, and the live bar kept the width of the old size.
                live.levelAt(live.bar->sizeHint().width());
                QCOMPARE(barMetrics(live.bar), barMetrics(fresh.bar));
                // ...and both fold at the same width, wherever that is.
                for (const int width : {760, 430})
                    QCOMPARE(live.levelAt(width), fresh.levelAt(width));
                // The icon form is the design's width at whatever text size
                // this is (8 + 16 + 8, More the 28 px square), and the names
                // never follow the folding at all.
                QCOMPARE(live.levelAt(live.widthForLevel(2)), 2);
                for (QToolButton *b : {live.bar->pullButton(), live.bar->pushButton()})
                    QCOMPARE(live.rectOf(b).width(), iconFormWidth());
                QCOMPARE(live.rectOf(live.bar->moreButton()).width(), ui::space(ui::box::control));
                QCOMPARE(barNames(live.bar), kBarNames);
                QCOMPARE(barNames(fresh.bar), kBarNames);
                live.levelAt(live.bar->sizeHint().width());
            };

            const QStringList atTwelve = barMetrics(live.bar);
            matchesAFreshBar();

            // The desktop's text size goes up under the live bar.
            QVERIFY(writeFixture(toml, "[font]\nbase-size = 16\n"));
            QTRY_COMPARE_WITH_TIMEOUT(theme.fontBase(), 16, 10000);
            settle();
            matchesAFreshBar();
            QVERIFY(barMetrics(live.bar) != atTwelve); // the measurements did move

            // ...and back down to where it started.
            QVERIFY(writeFixture(toml, "[font]\nbase-size = 12\n"));
            QTRY_COMPARE_WITH_TIMEOUT(theme.fontBase(), 12, 10000);
            settle();
            matchesAFreshBar();
            QCOMPARE(barMetrics(live.bar), atTwelve);

            live.host.reset(); // the bar goes before the theme it follows
        }
        g_theme.reset(new OmarchyTheme);
        g_theme->apply(*qApp);
        QVERIFY(OmarchyTheme::instance() == g_theme.get());
    }

    // The same live change, for the commit page: one page built at 12 and left
    // standing, held against a page built from scratch at every size the
    // desktop moves to.
    void theCommitPageFollowsALiveTextSizeChange()
    {
        QTemporaryDir dir, home, tools;
        QVERIFY(dir.isValid() && home.isValid() && tools.isValid());
        const QString toml = QDir(dir.path()).filePath(QStringLiteral("shell.toml"));
        QVERIFY(writeFixture(toml, "[font]\nbase-size = 12\n"));
        // A PATH with nothing on it but git: omarchy-font-current stays out of
        // reach, so a reload's font query answers on the spot, and no coding
        // agent is found either — while the page can still read a repository.
        const QString gitBinary = QStandardPaths::findExecutable(QStringLiteral("git"));
        QVERIFY(!gitBinary.isEmpty());
        QVERIFY(QFile::link(gitBinary, QDir(tools.path()).filePath(QStringLiteral("git"))));
        {
            ScopedEnv themeDir("OMAGIT_THEME_DIR", dir.path().toUtf8());
            ScopedEnv scratchHome("HOME", home.path().toUtf8());
            ScopedEnv onlyGit("PATH", tools.path().toUtf8());
            OmarchyTheme theme;
            QCOMPARE(theme.fontBase(), 12);
            theme.apply(*qApp);

            // MainWindow::applyTheme() is what drives a page in production: it
            // calls CommitPage::applyTheme() — once as the window is built and
            // again on every OmarchyTheme::changed — and gives every section
            // label the caption font of the moment.
            const auto applyThemeAsMainWindowDoes = [](CommitPage *p) {
                p->applyTheme();
                for (QLabel *l : p->findChildren<QLabel *>())
                    if (l->objectName() == QLatin1String("sectionLabel")
                        || l->objectName() == QLatin1String("dimLabel"))
                        l->setFont(OmarchyTheme::instance()->captionFont());
            };

            CommitFixture live = nestedFixture(); // directories, so the tree has depth to measure
            QVERIFY(live.page);
            CommitPage *page = live.page.get();
            applyThemeAsMainWindowDoes(page);
            QObject::connect(&theme, &OmarchyTheme::changed, page,
                             [page, applyThemeAsMainWindowDoes] { applyThemeAsMainWindowDoes(page); });
            QVERIFY(QTest::qWaitForWindowExposed(page));
            settle();

            // Both pages at two widths around the one the action bar folds at,
            // so the label and its box are measured where they change — and in
            // all three presentations, so the tree's depth geometry and the
            // compact table's narrow columns are measured too.
            const auto matchesAFreshPage = [&live, applyThemeAsMainWindowDoes] {
                std::unique_ptr<CommitPage> fresh = commitPage(live.repo.get());
                applyThemeAsMainWindowDoes(fresh.get());
                QVERIFY(QTest::qWaitForWindowExposed(fresh.get()));
                settle();
                const int fold = amendFoldWidth(fresh.get());
                QCOMPARE(amendFoldWidth(live.page.get()), fold);
                for (const int width : {fold - 1, fold + 1}) {
                    live.page->resize(width, 600);
                    fresh->resize(width, 600);
                    settle();
                    QCOMPARE(pageMetrics(live.page.get()), pageMetrics(fresh.get()));
                }
                live.page->resize(760, 600); // the width the page was measured at
                fresh->resize(760, 600);
                settle();
                for (const CommitPage::FilesView view : {CommitPage::FilesView::Tree,
                                                         CommitPage::FilesView::Compact,
                                                         CommitPage::FilesView::Table}) {
                    live.page->setFilesView(view, false);
                    fresh->setFilesView(view, false);
                    settle();
                    QCOMPARE(treeMetrics(live.page.get()), treeMetrics(fresh.get()));
                    QCOMPARE(pageMetrics(live.page.get()), pageMetrics(fresh.get()));
                }
                settle();
            };

            const QStringList atTwelve = pageMetrics(page);
            matchesAFreshPage();

            // The desktop's text size goes up under the live page.
            QVERIFY(writeFixture(toml, "[font]\nbase-size = 18\n"));
            QTRY_COMPARE_WITH_TIMEOUT(theme.fontBase(), 18, 10000);
            settle();
            matchesAFreshPage();
            QVERIFY(pageMetrics(page) != atTwelve); // the measurements did move

            // ...and back down to where it started.
            QVERIFY(writeFixture(toml, "[font]\nbase-size = 12\n"));
            QTRY_COMPARE_WITH_TIMEOUT(theme.fontBase(), 12, 10000);
            settle();
            matchesAFreshPage();
            QCOMPARE(pageMetrics(page), atTwelve);

            live.page.reset(); // the page goes before the theme it follows
        }
        g_theme.reset(new OmarchyTheme);
        g_theme->apply(*qApp);
        QVERIFY(OmarchyTheme::instance() == g_theme.get());
    }

    // The same live change, for the Mini rail's commit section and the card:
    // a window built at 12 and left standing measures like one built fresh at
    // 16 and back — the rail, the tile and its square, the gaps around the
    // separator, the card's width, margins, gaps, editor offsets and hint,
    // its anchor, the wrapping of the shared message, and where the body
    // puts the splitter.
    void theCommitPopoverFollowsALiveTextSizeChange()
    {
        QTemporaryDir dir, home, tools;
        QVERIFY(dir.isValid() && home.isValid() && tools.isValid());
        const QString toml = QDir(dir.path()).filePath(QStringLiteral("shell.toml"));
        QVERIFY(writeFixture(toml, "[font]\nbase-size = 12\n"));
        const QString gitBinary = QStandardPaths::findExecutable(QStringLiteral("git"));
        QVERIFY(!gitBinary.isEmpty());
        QVERIFY(QFile::link(gitBinary, QDir(tools.path()).filePath(QStringLiteral("git"))));
        {
            ScopedEnv themeDir("OMAGIT_THEME_DIR", dir.path().toUtf8());
            ScopedEnv scratchHome("HOME", home.path().toUtf8());
            ScopedEnv onlyGit("PATH", tools.path().toUtf8());
            OmarchyTheme theme;
            QCOMPARE(theme.fontBase(), 12);
            theme.apply(*qApp);

            WindowFixture live = mainWindow();
            QVERIFY(live.window);
            QVERIFY(QTest::qWaitForWindowExposed(live.window.get()));
            QVERIFY(live.openCard());
            const QString text = longParagraph() + QLatin1Char(' ') + longParagraph();
            live.pageEditor()->replaceText(text);
            settle();

            const auto matchesAFreshWindow = [&live, &text] {
                WindowFixture fresh = mainWindow(0, true); // Mini from its first show
                QVERIFY(fresh.window);
                QVERIFY(QTest::qWaitForWindowExposed(fresh.window.get()));
                QVERIFY(fresh.openCard());
                fresh.pageEditor()->replaceText(text);
                settle();
                QTRY_COMPARE(popoverMetrics(live), popoverMetrics(fresh));
                // The design's exact offsets, whatever the text size: the
                // card's 12 of padding, the 24 px MESSAGE row and its 8.
                const QStringList metrics = popoverMetrics(live);
                QVERIFY(metrics.contains(QStringLiteral("editorTop=%1")
                                             .arg(ui::space(ui::pad::popover + ui::box::row + ui::gap::header))));
                QVERIFY(metrics.contains(QStringLiteral("editorLeft=%1").arg(ui::space(ui::pad::popover))));
                QVERIFY(metrics.contains(QStringLiteral("rail=%1").arg(MiniRail::railWidth())));
                QVERIFY(metrics.contains(QStringLiteral("square=%1").arg(ui::space(ui::box::tile))));
                QVERIFY(metrics.contains(QStringLiteral("hintPx=%1").arg(qRound(OmarchyTheme::instance()->fontBase() * 11 / 12.0))));
                QVERIFY(metrics.contains(QStringLiteral("hintBold=0")));
                MessageEdit *editor = live.popover()->editor();
                QTRY_COMPARE(editor->contentHeight(), freshContentHeight(editor, text));
                // The body puts the splitter after the rail's scaled width and
                // the window's margin.
                QSplitter *splitter = nullptr;
                for (QSplitter *s : fresh.window->findChildren<QSplitter *>())
                    if (s->orientation() == Qt::Horizontal && s->objectName().isEmpty())
                        splitter = s;
                QVERIFY(splitter);
                QCOMPARE(rectIn(splitter, fresh.host()).x(),
                         fresh.rail()->geometry().x() + MiniRail::railWidth() + ui::windowMargin(fresh.window.get()));
                // ...and the left section's first width is the design's for
                // the window's width class, on the text size of the moment.
                fresh.window->setPaneLayout(PaneLayout::Docked, false);
                settle();
                QCOMPARE(splitter->sizes().first(), designLeftWidth(fresh.window.get()));
                fresh.window.reset();
            };

            matchesAFreshWindow();
            const QStringList atTwelve = popoverMetrics(live);
            QVERIFY(atTwelve.contains(QStringLiteral("rail=40")));
            QVERIFY(atTwelve.contains(QStringLiteral("editorTop=44")));

            QVERIFY(writeFixture(toml, "[font]\nbase-size = 16\n"));
            QTRY_COMPARE_WITH_TIMEOUT(theme.fontBase(), 16, 10000);
            settle();
            matchesAFreshWindow();
            const QStringList atSixteen = popoverMetrics(live);
            // At 16 the grid's unit is 5 (round(4 × 16 / 12)).
            QVERIFY(atSixteen.contains(QStringLiteral("rail=50")));
            QVERIFY(atSixteen.contains(QStringLiteral("square=50")));
            QVERIFY(atSixteen.contains(QStringLiteral("tile=50x50")));
            QVERIFY(atSixteen.contains(QStringLiteral("editorTop=55")));
            QVERIFY(atSixteen.contains(QStringLiteral("editorLeft=15")));
            QVERIFY(live.popover()->isVisible()); // a text size is no reason to close

            QVERIFY(writeFixture(toml, "[font]\nbase-size = 12\n"));
            QTRY_COMPARE_WITH_TIMEOUT(theme.fontBase(), 12, 10000);
            settle();
            matchesAFreshWindow();
            QCOMPARE(popoverMetrics(live), atTwelve);

            live.window.reset(); // the window goes before the theme it follows
        }
        g_theme.reset(new OmarchyTheme);
        g_theme->apply(*qApp);
        QVERIFY(OmarchyTheme::instance() == g_theme.get());
    }

    // The agent card after a live text-size change measures like one built
    // at the new size: its width, margins, rows, track, notes and — with no
    // agent installed — its command rows.
    void theAgentCardFollowsALiveTextSizeChange()
    {
        QTemporaryDir dir, home, tools, bare;
        QVERIFY(dir.isValid() && home.isValid() && tools.isValid() && bare.isValid());
        const QString toml = QDir(dir.path()).filePath(QStringLiteral("shell.toml"));
        QVERIFY(writeFixture(toml, "[font]\nbase-size = 12\n"));
        const QString gitBinary = QStandardPaths::findExecutable(QStringLiteral("git"));
        QVERIFY(!gitBinary.isEmpty());
        QVERIFY(QFile::link(gitBinary, QDir(bare.path()).filePath(QStringLiteral("git"))));
        QVERIFY(writeFakeClaude(tools.path()));
        QVERIFY(QFile::link(gitBinary, QDir(tools.path()).filePath(QStringLiteral("git"))));
        {
            ScopedEnv themeDir("OMAGIT_THEME_DIR", dir.path().toUtf8());
            ScopedEnv scratchHome("HOME", home.path().toUtf8());
            OmarchyTheme theme;
            QCOMPARE(theme.fontBase(), 12);
            theme.apply(*qApp);
            AgentScope scope(tools.path());
            CommitMessageAgent::saveChoice(AgentChoice{QStringLiteral("claude"), QStringLiteral("opus"), QStringLiteral("high")});

            // A card open in either state: the installed one with the fake
            // claude, the other with git alone on PATH.
            const auto open = [&](WindowFixture &f, bool installed) {
                ScopedEnv path("PATH", (installed ? tools : bare).path().toUtf8());
                f.window->resize(945, 1234);
                QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
                QVERIFY(QMetaObject::invokeMethod(f.window.get(), "showAgentMenu"));
                settle();
                QVERIFY(f.agentCard()->isVisible());
            };
            WindowFixture live = mainWindow();
            QVERIFY(live.window);
            open(live, true);
            WindowFixture liveBare = mainWindow();
            QVERIFY(liveBare.window);
            open(liveBare, false);

            const auto matchesAFreshWindow = [&] {
                for (const bool installed : {true, false}) {
                    WindowFixture fresh = mainWindow();
                    QVERIFY(fresh.window);
                    open(fresh, installed);
                    const AgentPopover *card = (installed ? live : liveBare).agentCard();
                    QTRY_COMPARE(agentCardMetrics(card), agentCardMetrics(fresh.agentCard()));
                    QCOMPARE(card->geometry(), fresh.agentCard()->geometry());
                    const QStringList metrics = agentCardMetrics(card);
                    const int pad = ui::space(ui::pad::popover) - 2; // the 2 px frame inside the padding
                    QVERIFY(metrics.contains(QStringLiteral("margins=%1,%1,%1,%1").arg(pad)));
                    QVERIFY(metrics.contains(QStringLiteral("width=%1").arg(ui::space(360))));
                    QVERIFY(metrics.contains(QStringLiteral("notePx=%1").arg(OmarchyTheme::instance()->captionFont().pixelSize()))
                            || !installed);
                    QVERIFY(metrics.contains(QStringLiteral("noteBold=0")));
                    if (installed) {
                        // screens.js agentPopover(): 24 px model rows, the
                        // picker a 28 px control under its caption's 16 px
                        // line and 4, the 40 px track's stops 24 in and 12 down.
                        QVERIFY(metrics.contains(QStringLiteral("row=%1").arg(ui::space(ui::box::row))));
                        QVERIFY(metrics.contains(QStringLiteral("picker=%1").arg(ui::space(ui::box::control))));
                        QVERIFY(metrics.contains(QStringLiteral("pickerTop=%1")
                                                     .arg(ui::space(ui::pad::popover + ui::box::line + ui::gap::caption))));
                        QVERIFY(metrics.contains(QStringLiteral("track=%1").arg(ui::space(40))));
                        QVERIFY(metrics.contains(QStringLiteral("stop0=%1").arg(ui::space(24))));
                        QVERIFY(metrics.contains(QStringLiteral("stopY=%1").arg(ui::space(12))));
                    } else {
                        QVERIFY(metrics.contains(QStringLiteral("notePx=%1").arg(qRound(OmarchyTheme::instance()->fontBase() * 11 / 12.0))));
                        QVERIFY(metrics.contains(QStringLiteral("command=%1").arg(ui::space(ui::box::control))));
                        QVERIFY(metrics.contains(QStringLiteral("copy=%1").arg(ui::space(ui::box::row))));
                    }
                    fresh.window.reset();
                }
            };

            matchesAFreshWindow();
            const QStringList atTwelve = agentCardMetrics(live.agentCard());
            const QStringList bareAtTwelve = agentCardMetrics(liveBare.agentCard());
            QVERIFY(atTwelve.contains(QStringLiteral("width=360")));

            QVERIFY(writeFixture(toml, "[font]\nbase-size = 16\n"));
            QTRY_COMPARE_WITH_TIMEOUT(theme.fontBase(), 16, 10000);
            settle();
            matchesAFreshWindow();
            // At 16 the grid's unit is 5 (round(4 × 16 / 12)).
            QVERIFY(agentCardMetrics(live.agentCard()).contains(QStringLiteral("width=450")));
            QVERIFY(agentCardMetrics(live.agentCard()).contains(QStringLiteral("row=30")));
            QVERIFY(live.agentCard()->isVisible()); // a text size is no reason to close

            QVERIFY(writeFixture(toml, "[font]\nbase-size = 12\n"));
            QTRY_COMPARE_WITH_TIMEOUT(theme.fontBase(), 12, 10000);
            settle();
            matchesAFreshWindow();
            QCOMPARE(agentCardMetrics(live.agentCard()), atTwelve);
            QCOMPARE(agentCardMetrics(liveBare.agentCard()), bareAtTwelve);

            live.window.reset(); // the windows go before the theme they follow
            liveBare.window.reset();
        }
        g_theme.reset(new OmarchyTheme);
        g_theme->apply(*qApp);
        QVERIFY(OmarchyTheme::instance() == g_theme.get());
    }

    void themeReadsColorsTomlAndFallsBack()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile toml(QDir(dir.path()).filePath(QStringLiteral("colors.toml")));
        QVERIFY(toml.open(QIODevice::WriteOnly | QIODevice::Truncate));
        toml.write("# a scratch theme\n"
                   "mode = \"light\"\n"
                   "accent = \"#ff0000\"\n"
                   "background = \"#101010\"\n"
                   "foreground = \"#eeeeee\"\n"
                   "red = \"#c00000\"\n"
                   "green = #00ff00\n" // malformed: no quotes, so the line is ignored
                   "= \"#123456\"\n");  // malformed: no key
        toml.close();
        qputenv("OMAGIT_THEME_DIR", dir.path().toUtf8());

        {
            OmarchyTheme theme;
            QCOMPARE(theme.color(QStringLiteral("accent")), QColor(QStringLiteral("#ff0000")));
            QCOMPARE(theme.color(QStringLiteral("background")), QColor(QStringLiteral("#101010")));
            QCOMPARE(theme.color(QStringLiteral("red")), QColor(QStringLiteral("#c00000")));
            QVERIFY(!theme.isDark()); // mode = "light"
            // A key the file never names comes from Tokyo Night...
            QCOMPARE(theme.color(QStringLiteral("bright_yellow")), QColor(QStringLiteral("#ff9e64")));
            // ...and so does one whose line the parser could not read.
            QCOMPARE(theme.color(QStringLiteral("green")), QColor(QStringLiteral("#9ece6a")));
            // Shades the file leaves out are derived from what it does name.
            QVERIFY(theme.color(QStringLiteral("lighter_background")).isValid());
            QVERIFY(theme.mutedText() != theme.text());

            theme.apply(*qApp);
            const QString sheet = qApp->styleSheet();
            QVERIFY(!sheet.isEmpty());
            static const QRegularExpression token(QStringLiteral("%[A-Za-z0-9]+%"));
            const QRegularExpressionMatch leftover = token.match(sheet);
            QVERIFY2(!leftover.hasMatch(), qPrintable(QStringLiteral("stylesheet still holds ") + leftover.captured()));
            QVERIFY(sheet.contains(theme.accent().name()));
        }

        // Put the desktop's own theme back for whatever runs after this.
        qunsetenv("OMAGIT_THEME_DIR");
        g_theme.reset(new OmarchyTheme);
        g_theme->apply(*qApp);
        QVERIFY(OmarchyTheme::instance() == g_theme.get());
    }
};

UI_TEST(ThemeTest);

#include "theme_test.moc"
