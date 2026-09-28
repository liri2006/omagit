// The diff pane: its toolbar folding in three steps, the view dropdown, the
// header and the counter, and a switch between split and unified that keeps
// the top line and the selection.
#include "UiTestCase.h"
#include "fixtures.h"

#include "../../src/DiffModel.h"
#include "../../src/DiffPane.h"
#include "../../src/Settings.h"
#include "../../src/TickMenu.h"
#include "../../src/UiHelpers.h"

#include <QApplication>
#include <QClipboard>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QScrollBar>
#include <QTextDocumentFragment>

namespace {

// A diff pane of its own, shown, with a document of two changes and the
// row's controls found by the names they keep in every form.
struct PaneFixture
{
    std::unique_ptr<DiffPane> pane;

    QToolButton *button(const QString &name) const
    {
        for (QToolButton *b : pane->findChildren<QToolButton *>())
            if (b->parentWidget() == pane.get() && b->accessibleName() == name)
                return b;
        return nullptr;
    }
    QToolButton *prev() const { return button(QStringLiteral("Prev")); }
    QToolButton *next() const { return button(QStringLiteral("Next")); }
    QToolButton *view() const { return button(QStringLiteral("View")); }
    QToolButton *whitespace() const { return button(QStringLiteral("Whitespace")); }
    QToolButton *syntax() const { return button(QStringLiteral("Syntax")); }
    QToolButton *options() const { return button(QStringLiteral("View options")); }
    QList<QToolButton *> viewOptions() const { return {whitespace(), syntax()}; }
    QLabel *counter() const
    {
        for (QLabel *l : pane->findChildren<QLabel *>())
            if (l->parentWidget() == pane.get())
                return l;
        return nullptr;
    }
    // What the counter reads, its rich text's colours and spacing aside.
    QString counterText() const
    {
        return QTextDocumentFragment::fromHtml(counter()->text()).toPlainText().replace(QChar(0x00a0), QLatin1Char(' '));
    }
    void resizeTo(int width) const
    {
        pane->resize(width, 400);
        QCoreApplication::processEvents();
    }
};

// The summary the pane is given, and what the counter reads of it.
DiffPane::Summary paneSummary()
{
    DiffPane::Summary summary;
    summary.status = QStringLiteral("Modified");
    summary.colour = ChangesModel::statusColor(FileChange::Modified);
    summary.added = 2;
    summary.removed = 2;
    return summary;
}

// The runs are painted on the design's gaps; the text keeps one space between them.
const QString kPaneSummary = QStringLiteral("Modified +2 −2");

PaneFixture diffPane()
{
    PaneFixture f;
    f.pane.reset(new DiffPane);
    f.pane->setSummary(paneSummary());
    // Two change blocks, a run of context between them.
    const DiffDocument doc = DiffModel::parse(
        QStringLiteral("@@ -1,5 +1,5 @@\n-a\n+A\n b\n c\n d\n-e\n+E\n"));
    f.pane->view()->setDocument(doc, QStringLiteral("x.txt"), QString(), QStringLiteral("HEAD"),
                                QStringLiteral("Working tree"));
    f.pane->view()->firstChange();
    f.pane->resize(ui::space(1000), 400);
    f.pane->show();
    return f;
}

// A long diff with one change: 40 lines of context, 40 lines replaced by 40
// others, 40 more of context. Each line is one word over and over ("keep07",
// "gone12", "new12"), so a double-click anywhere on it selects the word that
// names it.
DiffDocument replacementDiff()
{
    QString text = QStringLiteral("@@ -1,120 +1,120 @@\n");
    const auto add = [&text](QChar tag, const QString &kind, int from, int to) {
        for (int i = from; i < to; ++i) {
            const QString word = kind + QStringLiteral("%1").arg(i, 2, 10, QLatin1Char('0'));
            text += tag + (word + QLatin1Char(' ')).repeated(20).trimmed() + QLatin1Char('\n');
        }
    };
    add(QLatin1Char(' '), QStringLiteral("keep"), 0, 40);
    add(QLatin1Char('-'), QStringLiteral("gone"), 0, 40);
    add(QLatin1Char('+'), QStringLiteral("new"), 0, 40);
    add(QLatin1Char(' '), QStringLiteral("keep"), 40, 80);
    return DiffModel::parse(text);
}

// The document line that is `word` over and over, -1 with none.
int diffLineOf(const DiffDocument &doc, const QString &word)
{
    for (int i = 0; i < doc.lines.size(); ++i)
        if (doc.lines.at(i).text.startsWith(word + QLatin1Char(' ')))
            return i;
    return -1;
}

// A DiffView on its own showing replacementDiff() unified, taller than it
// shows at once.
std::unique_ptr<DiffView> replacementView(const DiffDocument &doc)
{
    auto view = std::make_unique<DiffView>();
    view->setDocument(doc, QStringLiteral("x.txt"), QString(), QStringLiteral("HEAD"), QStringLiteral("Working tree"));
    view->setMode(DiffView::OnePane);
    view->resize(ui::space(600), 400);
    view->show();
    return view;
}

// The row's buttons on screen, each no narrower than what it wears asks. A
// labelled button asks for its size hint. A glyph square (the icon form, and
// the "…" of ui::iconButton()) is held to its glyph instead: its size hint is
// a text button's, the glyph with two spaces of air around it, which for the
// wider glyphs (split, code tags, dots) is a pixel or two over the design's
// 28 px square that the glyph sits in with room to spare.
QStringList squeezedButtons(QWidget *row)
{
    QStringList out;
    for (QToolButton *b : row->findChildren<QToolButton *>()) {
        if (b->parentWidget() != row || !b->isVisible())
            continue;
        const QFontMetrics fm = b->fontMetrics();
        const bool square = b->objectName() == QLatin1String("iconButton") || b->property("iconForm").toBool();
        const int needs = square ? qMax(fm.boundingRect(b->text()).width(), fm.horizontalAdvance(b->text())) + 2
                                 : b->sizeHint().width();
        if (b->width() < needs)
            out << QStringLiteral("%1: %2 < %3").arg(b->accessibleName()).arg(b->width()).arg(needs);
    }
    return out;
}

} // namespace

class DiffPaneTest : public UiTestCase
{
    Q_OBJECT
private slots:
    // Prev, Next and the view dropdown labelled from 900 px of pane up; the
    // dropdown as its glyph and chevron from 560; below that Prev and Next as
    // glyphs too, the counter as "n/m" and the view options behind "…".
    // Whitespace and Syntax are 28 px squares wherever they show. The widest
    // form comes back exactly.
    void theDiffToolbarFoldsInThreeStepsAsItNarrows()
    {
        PaneFixture f = diffPane();
        QVERIFY(QTest::qWaitForWindowExposed(f.pane.get()));
        settle();
        QVERIFY(f.prev() && f.next() && f.view() && f.whitespace() && f.syntax() && f.options() && f.counter());
        QCOMPARE(f.options()->popupMode(), QToolButton::InstantPopup);
        QVERIFY(qobject_cast<TickMenu *>(f.options()->menu()));
        QCOMPARE(f.options()->text(), ui::icon(ui::kDotsHorizontal, QStringLiteral("…")).trimmed());
        QCOMPARE(f.view()->popupMode(), QToolButton::InstantPopup);
        QVERIFY(qobject_cast<TickMenu *>(f.view()->menu()));

        const bool split = f.pane->view()->mode() == DiffView::TwoPane;
        const uint viewGlyph = split ? ui::kSplit : ui::kUnified;
        const QString viewName = split ? QStringLiteral("Split") : QStringLiteral("Unified");
        const QString longCounter = QStringLiteral("Change 1 of 2 · ") + kPaneSummary;
        const auto expectForm = [&](bool labelledView, bool compact) {
            // Prev and Next: labelled, or squares in the compact form.
            const QList<QPair<QToolButton *, QString>> nav{{f.prev(), QStringLiteral("Prev")},
                                                          {f.next(), QStringLiteral("Next")}};
            for (const auto &entry : nav) {
                QToolButton *b = entry.first;
                const uint glyph = b == f.prev() ? ui::kArrowUp : ui::kArrowDown;
                QVERIFY(b->isVisible());
                QCOMPARE(b->text(), compact ? ui::icon(glyph).trimmed() : ui::icon(glyph) + entry.second);
                QCOMPARE(b->property("iconForm").toBool(), compact);
                QCOMPARE(b->height(), ui::space(28));
                if (compact)
                    QCOMPARE(b->width(), ui::space(28));
                else
                    QVERIFY(b->width() > ui::space(28));
            }
            // The dropdown, then the two squares.
            QCOMPARE(f.view()->isVisible(), !compact);
            if (!compact) {
                QCOMPARE(f.view()->text(), (labelledView ? ui::icon(viewGlyph) + viewName
                                                         : ui::icon(viewGlyph, viewName.left(1)))
                                               + ui::chevron());
                QCOMPARE(f.view()->height(), ui::space(28));
            }
            for (QToolButton *b : f.viewOptions()) {
                QCOMPARE(b->isVisible(), !compact);
                QVERIFY(b->property("iconForm").toBool());
                QCOMPARE(b->maximumWidth(), ui::space(28));
                if (!compact)
                    QCOMPARE(b->size(), QSize(ui::space(28), ui::space(28)));
            }
            QCOMPARE(f.options()->isVisible(), compact);
            QCOMPARE(f.counterText(), compact ? QStringLiteral("1/2") : longCounter);
            // The header under it says the status and the +/− only when the
            // counter has no room for them.
            QCOMPARE(f.pane->view()->subtitleShown(), compact);
        };

        f.resizeTo(ui::space(900));
        expectForm(true, false);
        // The design's dropdown: 8, the 16 px glyph box, 4, the name, 4 and
        // the 12 px chevron box, 8.
        QCOMPARE(f.view()->width(), ui::space(8 + 16 + 4) + qCeil(QFontMetricsF(f.view()->font()).horizontalAdvance(viewName))
                                        + ui::space(4 + 12 + 8));
        QList<int> widths;
        for (QToolButton *b : {f.prev(), f.next(), f.view(), f.whitespace(), f.syntax()})
            widths << b->width();
        f.resizeTo(ui::space(899));
        expectForm(false, false);
        QCOMPARE(f.view()->width(), ui::space(8 + 16 + 4 + 12 + 8));
        // The design's gaps: 4 between Prev and Next and between the options,
        // 8 from Next to the counter.
        QCOMPARE(f.next()->x() - (f.prev()->x() + f.prev()->width()), ui::space(4));
        QCOMPARE(f.whitespace()->x() - (f.view()->x() + f.view()->width()), ui::space(4));
        QCOMPARE(f.syntax()->x() - (f.whitespace()->x() + f.whitespace()->width()), ui::space(4));
        QCOMPARE(f.counter()->x() + f.counter()->contentsMargins().left() - (f.next()->x() + f.next()->width()),
                 ui::space(ui::gap::item));
        f.resizeTo(ui::space(560));
        expectForm(false, false);
        f.resizeTo(ui::space(559));
        expectForm(false, true);
        QCOMPARE(f.next()->x() - (f.prev()->x() + f.prev()->width()), ui::space(4));
        QCOMPARE(f.options()->width(), ui::space(28));
        QCOMPARE(f.options()->x() + f.options()->width(), f.pane->width());

        f.resizeTo(ui::space(900));
        expectForm(true, false);
        QList<int> after;
        for (QToolButton *b : {f.prev(), f.next(), f.view(), f.whitespace(), f.syntax()})
            after << b->width();
        QCOMPARE(after, widths);
    }

    // The point of the folding: at no width does a button of the row wear
    // less room than its text asks for.
    void noDiffToolbarButtonIsNarrowerThanItsContent()
    {
        PaneFixture f = diffPane();
        QVERIFY(QTest::qWaitForWindowExposed(f.pane.get()));
        settle();
        int visibleAtNarrowest = 0;
        for (int width = ui::space(300); width <= ui::space(1000); width += ui::space(7)) {
            f.resizeTo(width);
            const QStringList squeezed = squeezedButtons(f.pane.get());
            QVERIFY2(squeezed.isEmpty(), qPrintable(QStringLiteral("at %1: ").arg(width) + squeezed.join(", ")));
            if (width == ui::space(300))
                for (QToolButton *b : f.pane->findChildren<QToolButton *>())
                    visibleAtNarrowest += b->parentWidget() == f.pane.get() && b->isVisible();
        }
        QCOMPARE(visibleAtNarrowest, 3); // Prev, Next and "…"
    }

    // The view dropdown offers Split and Unified, the current one ticked, and
    // its face follows the view however it changes (Ctrl+T included).
    void theViewDropdownPicksSplitOrUnified()
    {
        PaneFixture f = diffPane();
        QVERIFY(QTest::qWaitForWindowExposed(f.pane.get()));
        settle();
        f.resizeTo(ui::space(900));
        DiffView *view = f.pane->view();
        const DiffView::Mode before = view->mode();
        QMenu *menu = f.view()->menu();
        QVERIFY(menu);
        QList<QAction *> entries = filledMenu(menu);
        QCOMPARE(menuTexts(entries), QStringList({ui::icon(ui::kSplit) + QStringLiteral("Split"),
                                                  ui::icon(ui::kUnified) + QStringLiteral("Unified")}));
        QCOMPARE(entries.at(0)->isChecked(), before == DiffView::TwoPane);
        QCOMPARE(entries.at(1)->isChecked(), before == DiffView::OnePane);

        entries.at(1)->trigger();
        QCOMPARE(view->mode(), DiffView::OnePane);
        QCOMPARE(f.view()->text(), ui::icon(ui::kUnified) + QStringLiteral("Unified") + ui::chevron());
        QCOMPARE(QSettings().value(settings::kDiffTwoPane).toBool(), false);
        entries = filledMenu(menu);
        QVERIFY(!entries.at(0)->isChecked() && entries.at(1)->isChecked());
        filledMenu(menu).at(0)->trigger();
        QCOMPARE(view->mode(), DiffView::TwoPane);
        QCOMPARE(f.view()->text(), ui::icon(ui::kSplit) + QStringLiteral("Split") + ui::chevron());
        QCOMPARE(QSettings().value(settings::kDiffTwoPane).toBool(), true);

        // Ctrl+T toggles, and the face follows.
        f.pane->togglePaneMode();
        QCOMPARE(view->mode(), DiffView::OnePane);
        QVERIFY(f.view()->text().startsWith(ui::icon(ui::kUnified)));
        f.pane->togglePaneMode();
        QCOMPARE(view->mode(), DiffView::TwoPane);

        // Back to what the settings held before the test.
        view->setMode(before);
        QCOMPARE(view->mode(), before);
    }

    // The window's own switch keeps the line at the top, an added line that
    // the split row pairs with a removed one too: unified, split and unified
    // again comes back to it, not to the removed line. Scrolled to another
    // row, the split view's top row is its left line again.
    void theStackingSwitchKeepsTheTopLine()
    {
        const DiffDocument doc = replacementDiff();
        std::unique_ptr<DiffView> view = replacementView(doc);
        QVERIFY(QTest::qWaitForWindowExposed(view.get()));
        settle();
        QScrollBar *bar = view->verticalScrollBar();
        // Unified, every line is a row of its own; split, "new20" shares
        // "gone20"'s row, which no filler above it moves.
        const int removed = diffLineOf(doc, QStringLiteral("gone20")), added = diffLineOf(doc, QStringLiteral("new20"));
        bar->setValue(added);
        QCOMPARE(bar->value(), added);
        QCOMPARE(view->topLine(), added);

        view->setModeKeepingTopLine(DiffView::TwoPane);
        QCOMPARE(bar->value(), removed);
        QCOMPARE(view->topLine(), added);
        view->setModeKeepingTopLine(DiffView::OnePane);
        QCOMPARE(bar->value(), added);
        QCOMPARE(view->topLine(), added);

        view->setModeKeepingTopLine(DiffView::TwoPane);
        bar->setValue(removed + 1);
        QCOMPARE(view->topLine(), removed + 1);
        view->setModeKeepingTopLine(DiffView::OnePane);
        QCOMPARE(bar->value(), removed + 1);
    }

    // The same switch keeps a selection wherever the other view shows the
    // same text: a word of context or of an added line, both ways. One from
    // a removed line to an added one, which no single pane shows, is
    // cleared, and so is the left pane's over context and removed lines,
    // which unified takes the added lines into.
    void theStackingSwitchKeepsASelectionWhereItCan()
    {
        const DiffDocument doc = replacementDiff();
        std::unique_ptr<DiffView> view = replacementView(doc);
        QVERIFY(QTest::qWaitForWindowExposed(view.get()));
        settle();
        QScrollBar *bar = view->verticalScrollBar();
        QWidget *viewport = view->viewport();
        // What Ctrl+C copies: "-" with nothing selected.
        const auto selection = [&view] {
            QApplication::clipboard()->setText(QStringLiteral("-"));
            view->copySelection();
            return QApplication::clipboard()->text();
        };
        const QPoint middle = viewport->rect().center();
        for (const QString &kind : {QStringLiteral("keep"), QStringLiteral("new")}) {
            bar->setValue(diffLineOf(doc, kind + QStringLiteral("10"))); // a page of that kind from here on
            QTest::mouseDClick(viewport, Qt::LeftButton, {}, middle);
            const QString word = selection();
            QVERIFY2(word.startsWith(kind) && word.size() == kind.size() + 2, qPrintable(word));
            view->setModeKeepingTopLine(DiffView::TwoPane);
            QCOMPARE(selection(), word);
            view->setModeKeepingTopLine(DiffView::OnePane);
            QCOMPARE(selection(), word);
        }

        // Unified, a drag from the removed lines down into the added ones.
        bar->setValue(diffLineOf(doc, QStringLiteral("new00")) - bar->pageStep() / 2);
        const QPoint from(middle.x(), viewport->height() / 5), to(middle.x(), viewport->height() * 4 / 5);
        QTest::mousePress(viewport, Qt::LeftButton, {}, from);
        QTest::mouseMove(viewport, to);
        QTest::mouseRelease(viewport, Qt::LeftButton, {}, to);
        const QString across = selection();
        QVERIFY2(across.contains(QStringLiteral("gone")) && across.contains(QStringLiteral("\nnew"))
                     && !across.contains(QStringLiteral("keep")),
                 qPrintable(across));
        view->setModeKeepingTopLine(DiffView::TwoPane);
        QCOMPARE(selection(), QStringLiteral("-"));

        view->selectAll(); // the left pane's
        const QString left = selection();
        QVERIFY(left.contains(QStringLiteral("gone")) && !left.contains(QStringLiteral("new")));
        view->setModeKeepingTopLine(DiffView::OnePane);
        QCOMPARE(selection(), QStringLiteral("-"));
    }

    // The header's dim text at its right, the one-pane subtitle or a side's
    // label, stays while the whole path, a group gap and it fit the header's
    // text room (the view less 8 at either side), and is dropped otherwise:
    // the path gets the whole room then.
    void theDiffHeaderDropsItsLabelBeforeThePath()
    {
        DiffView view;
        const DiffDocument doc = DiffModel::parse(QStringLiteral("@@ -1,2 +1,2 @@\n-a\n+A\n b\n"));
        const QString path = QStringLiteral("src/ui/Toolbar.cpp"), subtitle = QStringLiteral("Modified   +1  −1");
        const QString left = QStringLiteral("HEAD"), right = QStringLiteral("Working tree");
        view.setDocument(doc, path, subtitle, left, right);
        view.setMode(DiffView::OnePane);
        view.resize(ui::space(600), 300);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        settle();
        QCOMPARE(view.headerLabel(), subtitle);

        QFont bold = view.font();
        bold.setBold(true);
        const int needs = QFontMetrics(bold).horizontalAdvance(path) + ui::space(ui::gap::group)
            + QFontMetrics(view.font()).horizontalAdvance(subtitle);
        const auto room = [&view] { return view.viewport()->width() + view.frameWidth() - 2 * ui::space(ui::pad::control); };
        bool shown = false, dropped = false;
        for (int width = needs + ui::space(100); width >= needs - ui::space(100); --width) {
            view.resize(width, 300);
            QCoreApplication::processEvents();
            const bool fits = needs <= room();
            QCOMPARE(view.headerLabel(), fits ? subtitle : QString());
            (fits ? shown : dropped) = true;
        }
        QVERIFY(shown && dropped);
        // Not shown at all while the toolbar says the same.
        view.resize(ui::space(600), 300);
        view.setSubtitleShown(false);
        QCOMPARE(view.headerLabel(), QString());
        view.setSubtitleShown(true);

        // Two panes, each on its own: the longer label goes first while the
        // shorter one still fits beside the path, then both.
        view.setMode(DiffView::TwoPane);
        view.resize(ui::space(1000), 300);
        QCoreApplication::processEvents();
        QCOMPARE(view.headerLabel(0), left);
        QCOMPARE(view.headerLabel(1), right);
        bool split = false;
        for (int width = ui::space(1000); width >= ui::space(150); --width) {
            view.resize(width, 300);
            QCoreApplication::processEvents();
            QVERIFY(view.headerLabel(0).isEmpty() || view.headerLabel(0) == left);
            QVERIFY(view.headerLabel(1).isEmpty() || view.headerLabel(1) == right);
            QVERIFY(!(view.headerLabel(0).isEmpty() && !view.headerLabel(1).isEmpty()));
            split = split || (view.headerLabel(0) == left && view.headerLabel(1).isEmpty());
        }
        QVERIFY(split);
        QCOMPARE(view.headerLabel(0), QString());
        QCOMPARE(view.headerLabel(1), QString());
    }

    // The "…" menu offers the view dropdown's choice and says what the two
    // squares say at the moment it opens; its entries take the same paths.
    void theDiffToolbarMenuMirrorsTheOptions()
    {
        PaneFixture f = diffPane();
        QVERIFY(QTest::qWaitForWindowExposed(f.pane.get()));
        settle();
        f.resizeTo(ui::space(400));
        QVERIFY(f.options()->isVisible());
        QMenu *menu = f.options()->menu();
        QVERIFY(menu);
        DiffView *view = f.pane->view();
        const DiffView::Mode before = view->mode();

        // The two options' entries: the last two of the menu.
        const auto checks = [&] {
            const QList<QAction *> entries = filledMenu(menu);
            return QList<bool>{entries.at(3)->isChecked(), entries.at(4)->isChecked()};
        };
        const auto buttons = [&] {
            QList<bool> out;
            for (const QToolButton *b : f.viewOptions())
                out << b->isChecked();
            return out;
        };
        const QList<QAction *> entries = filledMenu(menu);
        QCOMPARE(menuTexts(entries), QStringList({ui::icon(ui::kSplit) + QStringLiteral("Split"),
                                                  ui::icon(ui::kUnified) + QStringLiteral("Unified"),
                                                  QStringLiteral("-"),
                                                  ui::icon(ui::kPilcrow) + QStringLiteral("Whitespace"),
                                                  ui::icon(ui::kCodeTags) + QStringLiteral("Syntax")}));
        for (int i = 0; i < 2; ++i) {
            QVERIFY(entries.at(3 + i)->isCheckable());
            QCOMPARE(entries.at(3 + i)->toolTip(), f.viewOptions().at(i)->toolTip());
        }
        QCOMPARE(checks(), buttons());
        QCOMPARE(entries.at(0)->isChecked(), before == DiffView::TwoPane);
        QCOMPARE(entries.at(1)->isChecked(), before == DiffView::OnePane);

        // Unified, then Split, from the menu.
        filledMenu(menu).at(1)->trigger();
        QCOMPARE(view->mode(), DiffView::OnePane);
        QVERIFY(filledMenu(menu).at(1)->isChecked());
        filledMenu(menu).at(0)->trigger();
        QCOMPARE(view->mode(), DiffView::TwoPane);
        QVERIFY(filledMenu(menu).at(0)->isChecked());

        // Ctrl+W while the button is hidden: the button flips, and the menu
        // with it the next time it opens.
        QVERIFY(!f.whitespace()->isVisible());
        f.pane->toggleWhitespace();
        QCOMPARE(checks(), buttons());
        f.pane->toggleWhitespace();
        QCOMPARE(checks(), buttons());

        // The window's own path to it opens it only where the button shows.
        f.resizeTo(ui::space(900));
        QVERIFY(!f.options()->isVisible());
        f.pane->showOptionsMenu();
        QVERIFY(!menu->isVisible());

        // Back to what the settings held before the test.
        view->setMode(before);
        QCOMPARE(view->mode(), before);
    }

    // The counter follows the change and the form, and says nothing with no
    // change to count. The summary wears the design's colours: the status in
    // its own, the added lines green, the removed ones red.
    void theCounterFollowsTheFormAndTheChange()
    {
        PaneFixture f = diffPane();
        QVERIFY(QTest::qWaitForWindowExposed(f.pane.get()));
        settle();
        f.resizeTo(ui::space(400));
        QCOMPARE(f.counterText(), QStringLiteral("1/2"));
        f.pane->nextChange();
        QCOMPARE(f.counterText(), QStringLiteral("2/2"));
        f.resizeTo(ui::space(900));
        QCOMPARE(f.counterText(), QStringLiteral("Change 2 of 2 · ") + kPaneSummary);
        f.resizeTo(ui::space(700));
        QCOMPARE(f.counterText(), QStringLiteral("Change 2 of 2 · ") + kPaneSummary);
        const OmarchyTheme *theme = OmarchyTheme::instance();
        const QString html = f.counter()->text();
        for (const QColor &colour : {ChangesModel::statusColor(FileChange::Modified), theme->diffAddedIcon(),
                                     theme->diffRemovedIcon()})
            QVERIFY2(html.contains(QStringLiteral("color:") + colour.name()), qPrintable(html));

        // A cleared view (and no file to summarise): empty in either form.
        f.pane->clearSummary();
        f.pane->view()->clear();
        QCOMPARE(f.counterText(), QString());
        f.resizeTo(ui::space(400));
        QCOMPARE(f.counterText(), QString());
        QVERIFY(!f.prev()->isEnabled());
        QVERIFY(!f.next()->isEnabled());
    }

    // In the window: the stacked Diff tab at 470 wears the compact row, Mini
    // at 945 the middle one, and neither squeezes a button.
    void theDiffToolbarFollowsTheWindowsPane()
    {
        WindowFixture f = mainWindow(0, true, [](MainWindow *w) { w->resize(470, 612); });
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        settle();
        MainWindow *w = f.window.get();
        w->setDiffTab(true);
        settle();
        auto *pane = w->findChild<DiffPane *>();
        QVERIFY(pane && pane->isVisible());
        QVERIFY2(pane->width() < ui::space(560), qPrintable(QString::number(pane->width())));
        const auto named = [pane](const QString &name) -> QToolButton * {
            for (QToolButton *b : pane->findChildren<QToolButton *>())
                if (b->parentWidget() == pane && b->accessibleName() == name)
                    return b;
            return nullptr;
        };
        QToolButton *prev = named(QStringLiteral("Prev")), *viewButton = named(QStringLiteral("View")),
                    *whitespace = named(QStringLiteral("Whitespace")), *options = named(QStringLiteral("View options"));
        QVERIFY(prev && viewButton && whitespace && options);
        QVERIFY(options->isVisible());
        QVERIFY(!viewButton->isVisible());
        QCOMPARE(prev->width(), ui::space(28));
        QVERIFY2(squeezedButtons(pane).isEmpty(), qPrintable(squeezedButtons(pane).join(", ")));

        w->resize(945, 612);
        settle();
        QVERIFY(w->findChild<MiniRail *>()->isVisible());
        QVERIFY2(pane->width() >= ui::space(560) && pane->width() < ui::space(900), qPrintable(QString::number(pane->width())));
        QVERIFY(!options->isVisible());
        QVERIFY(viewButton->isVisible());
        QCOMPARE(whitespace->width(), ui::space(28));
        QCOMPARE(prev->text(), ui::icon(ui::kArrowUp) + QStringLiteral("Prev"));
        QVERIFY2(squeezedButtons(pane).isEmpty(), qPrintable(squeezedButtons(pane).join(", ")));
    }
};

UI_TEST(DiffPaneTest);

#include "diffpane_test.moc"
