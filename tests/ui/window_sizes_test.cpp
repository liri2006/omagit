// The window at the design's sizes: the 470 px tile, the extra-narrow and
// shallow windows that fold rows away, the 4 px grid and the width classes
// that size the pages, the popups and the history tables.
#include "UiTestCase.h"
#include "fixtures.h"

#include "../../src/BranchMenu.h"
#include "../../src/CommitDetails.h"
#include "../../src/DiffPane.h"
#include "../../src/Footer.h"
#include "../../src/HistoryModel.h"
#include "../../src/HistoryView.h"
#include "../../src/KeybindingsPanel.h"
#include "../../src/Segmented.h"
#include "../../src/Settings.h"
#include "../../src/TickMenu.h"
#include "../../src/UiHelpers.h"

#include <QApplication>
#include <QFontMetrics>
#include <QScrollBar>
#include <QSplitterHandle>
#include <QTimer>

class WindowSizesTest : public UiTestCase
{
    Q_OBJECT
private slots:
    // The stacked bar never forces a width on a 470 tile: with the Changes
    // pill and a branch name longer than the ordinary row's 72 px, the window
    // stays 470 wide on every tab. So long a name leaves the tabs no room on
    // one row: they take a second, and the names elide only by what the first
    // row lacks. "main" in a short-named repository keeps a 470 tile to one row.
    void theStackedWindowKeepsA470Tile()
    {
        WindowFixture f = mainWindow(0, false, [](MainWindow *w) { w->resize(470, 612); },
                                     QStringLiteral("feature/askpass-login-dialog"));
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        settle();
        MainWindow *w = f.window.get();
        TopBar *bar = f.bar();
        QVERIFY(w->isStacked());
        QVERIFY(bar->isStacked());
        QCOMPARE(bar->branchLabel(), QStringLiteral("feature/askpass-login-dialog"));
        QVERIFY(bar->changesCount() > 0);
        QVERIFY(bar->branchButton()->fontMetrics().horizontalAdvance(bar->branchLabel()) > ui::space(72));

        const auto holds = [&](QToolButton *tab, const char *name) {
            tab->click();
            settle();
            QVERIFY2(tab->isChecked(), name);
            QVERIFY2(w->width() == 470, qPrintable(QStringLiteral("%1: %2 wide").arg(QLatin1String(name)).arg(w->width())));
            QVERIFY2(w->minimumSizeHint().width() <= 470,
                     qPrintable(QStringLiteral("%1: a minimum of %2").arg(QLatin1String(name)).arg(w->minimumSizeHint().width())));
            QVERIFY2(bar->foldLevel() == 2, name);
            // The dropdown against the row's right edge; where the names give
            // way, the branch ends a cluster before it.
            const QRect branch(bar->branchButton()->mapTo(bar, QPoint(0, 0)), bar->branchButton()->size());
            const QRect sync(bar->syncDropdown()->mapTo(bar, QPoint(0, 0)), bar->syncDropdown()->size());
            QVERIFY2(sync.right() + 1 == bar->width() - ui::windowMargin(w), name);
            const int end = branch.x() + branch.width() + ui::space(ui::gap::cluster);
            if (bar->branchButton()->text() == ui::icon(ui::kBranch) + bar->branchLabel() + ui::chevron()
                && bar->repoButton()->text() == ui::icon(ui::kFolderOpen) + bar->repositoryName() + ui::chevron())
                QVERIFY2(end <= sync.x(), name);
            else
                QVERIFY2(end == sync.x(), qPrintable(bar->branchButton()->text()));
        };
        holds(bar->changesTab(), "Changes");
        if (QTest::currentTestFailed())
            return;
        holds(bar->diffTab(), "Diff");
        if (QTest::currentTestFailed())
            return;
        QVERIFY(w->diffTab());
        holds(bar->historyTab(), "History");
        if (QTest::currentTestFailed())
            return;
        QCOMPARE(w->mode(), MainWindow::HistoryMode);
        f.window.reset(); // the window goes before the theme changes under it

        // "main" in "repo" at the design's text size, where 470 holds its one row.
        QTemporaryDir dir, home;
        QVERIFY(dir.isValid() && home.isValid());
        QVERIFY(writeFixture(QDir(dir.path()).filePath(QStringLiteral("shell.toml")), "[font]\nbase-size = 12\n"));
        {
            ScopedEnv themeDir("OMAGIT_THEME_DIR", dir.path().toUtf8());
            ScopedEnv scratchHome("HOME", home.path().toUtf8());
            OmarchyTheme theme;
            QCOMPARE(theme.fontBase(), 12);
            theme.apply(*qApp);
            {
                WindowFixture main = mainWindow(0, false, [](MainWindow *w) { w->resize(470, 612); }, {},
                                                QStringLiteral("repo"));
                QVERIFY(main.window);
                QVERIFY(QTest::qWaitForWindowExposed(main.window.get()));
                settle();
                QVERIFY(main.window->isStacked());
                QCOMPARE(main.bar()->branchLabel(), QStringLiteral("main"));
                QCOMPARE(main.bar()->foldLevel(), 1);
                QCOMPARE(main.bar()->height(), 2 * ui::space(ui::kBar) + ui::space(ui::box::control));
                QCOMPARE(main.bar()->branchButton()->text(), ui::icon(ui::kBranch) + QStringLiteral("main") + ui::chevron());
                QCOMPARE(main.bar()->repoButton()->text(), ui::icon(ui::kFolderOpen) + QStringLiteral("repo") + ui::chevron());
            }
        }
        g_theme.reset(new OmarchyTheme);
        g_theme->apply(*qApp);
        QVERIFY(OmarchyTheme::instance() == g_theme.get());
    }

    // At 340 (a tile the user hit) not even the tab glyphs fit beside "repo"
    // and "main", so the tabs take a row of their own under the controls
    // (screens.js topBar(), extra narrow): the row's width but for More at
    // its end; the first row the repository and the branch a cluster apart,
    // the names whole, the dropdown at its right edge; the bar 8 + 28 + 8 +
    // 28 + 8 and the body under it from the first frame; the popups hang
    // from the first row, over the tabs, but More's from the bar, under More.
    void theExtraNarrowBarGivesTheTabsARowOfTheirOwn()
    {
        // At the design's text size, where its pixels are the bar's.
        QTemporaryDir dir, home;
        QVERIFY(dir.isValid() && home.isValid());
        const QString toml = QDir(dir.path()).filePath(QStringLiteral("shell.toml"));
        QVERIFY(writeFixture(toml, "[font]\nbase-size = 12\n"));
        {
            ScopedEnv themeDir("OMAGIT_THEME_DIR", dir.path().toUtf8());
            ScopedEnv scratchHome("HOME", home.path().toUtf8());
            OmarchyTheme theme;
            QCOMPARE(theme.fontBase(), 12);
            theme.apply(*qApp);
            {
                WindowFixture f = mainWindow(0, false, [](MainWindow *w) { w->resize(340, 612); }, {},
                                             QStringLiteral("repo"));
                QVERIFY(f.window);
                QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
                MainWindow *w = f.window.get();
                TopBar *bar = f.bar();
                const int row = ui::space(ui::box::control), gap = ui::space(ui::kBar);
                // As first laid out: no pass with the body under a one-row bar.
                QCOMPARE(bar->foldLevel(), 2);
                QCOMPARE(bar->height(), 3 * gap + 2 * row);
                settle();
                QCOMPARE(w->width(), 340);
                QVERIFY(w->isStacked());
                QCOMPARE(bar->branchLabel(), QStringLiteral("main"));
                QCOMPARE(bar->foldLevel(), 2);
                QCOMPARE(bar->height(), 3 * gap + 2 * row);
                const auto rectOf = [bar](const QWidget *c) { return QRect(c->mapTo(bar, QPoint(0, 0)), c->size()); };

                // Row 1: the controls, centred in the first 28 px, 8 under the top,
                // the chips a cluster apart, the dropdown at the right edge; the
                // names spelled out.
                for (QWidget *c : QList<QWidget *>{bar->repoButton(), bar->branchButton(), bar->syncDropdown()}) {
                    const QRect r = rectOf(c);
                    QVERIFY2(c->isVisible(), qPrintable(c->accessibleName()));
                    QVERIFY2(r.top() >= gap && r.bottom() < gap + row, qPrintable(c->accessibleName()));
                    QVERIFY2(qAbs(r.top() + r.bottom() + 1 - (2 * gap + row)) <= 1, qPrintable(c->accessibleName()));
                }
                QCOMPARE(rectOf(bar->syncDropdown()).height(), row);
                QCOMPARE(rectOf(bar->syncDropdown()).right() + 1, bar->width() - ui::space(8));
                QCOMPARE(rectOf(bar->branchButton()).x(), rectOf(bar->repoButton()).right() + 1 + ui::space(ui::gap::cluster));
                QCOMPARE(bar->branchButton()->text(), ui::icon(ui::kBranch) + QStringLiteral("main") + ui::chevron());
                QCOMPARE(bar->repoButton()->text(), ui::icon(ui::kFolderOpen) + QStringLiteral("repo") + ui::chevron());

                // Row 2: the tabs, stretched over the row's width (the window's
                // less its stacked 8 px margins) but for an item gap and More at
                // its end, a space(kBar) under row 1, clear of every other
                // control on the bar.
                auto *tabs = static_cast<SegmentStrip *>(bar->changesTab()->parentWidget());
                const QRect strip = rectOf(tabs);
                const int margin = ui::space(8), more = ui::space(ui::box::control);
                QCOMPARE(strip, QRect(margin, 2 * gap + row, bar->width() - 2 * margin - ui::space(ui::gap::item) - more, row));
                QCOMPARE(rectOf(bar->moreButton()), QRect(strip.right() + 1 + ui::space(ui::gap::item), strip.y(), more, row));
                QVERIFY(tabs->isStretch());
                for (QToolButton *b : bar->findChildren<QToolButton *>()) {
                    if (b->isVisible() && !tabs->isAncestorOf(b))
                        QVERIFY2(!rectOf(b).intersects(strip), qPrintable(b->accessibleName()));
                }
                // Labelled only where a third of the strip holds every labelled
                // segment: at 340, beside More, "Changes n" does not fit, so the
                // tabs are glyphs; at 400 the labels are back.
                const auto labelsFit = [tabs](int share) {
                    bool fit = true;
                    for (SegmentButton *segment : tabs->segments()) {
                        const bool labelled = segment->isLabelled();
                        segment->setLabelled(true);
                        fit = fit && segment->sizeHint().width() <= share;
                        segment->setLabelled(labelled);
                    }
                    return fit;
                };
                QVERIFY(!labelsFit(strip.width() / 3));
                for (const SegmentButton *segment : tabs->segments()) {
                    QVERIFY(segment->isVisible());
                    QVERIFY2(!segment->isLabelled(), qPrintable(segment->accessibleName()));
                }
                QVERIFY(bar->changesCount() > 0);
                w->resize(400, 612);
                settle();
                QCOMPARE(bar->foldLevel(), 2);
                QVERIFY(labelsFit(rectOf(tabs).width() / 3));
                for (const SegmentButton *segment : tabs->segments())
                    QVERIFY2(segment->isLabelled(), qPrintable(segment->accessibleName()));
                w->resize(340, 612);
                settle();
                // The body under the bar's hairline.
                QVERIFY(f.page()->mapTo(w, QPoint(0, 0)).y() >= bar->mapTo(w, QPoint(0, bar->height())).y());

                // The popups hang 4 under row 1, over the tabs; More's, from the
                // tabs' row, 4 under the bar.
                const int top = bar->mapToGlobal(QPoint(0, gap + row)).y() + ui::space(ui::gap::cluster);
                QCOMPARE(ui::popupTop(bar), top);
                const int under = bar->mapToGlobal(QPoint(0, bar->height())).y() + ui::space(ui::gap::cluster);
                QCOMPARE(ui::popupTop(bar, bar->moreButton()), under);
                QCOMPARE(ui::popupTop(bar, bar->syncDropdown()), top);
                for (QToolButton *anchor : QList<QToolButton *>{bar->syncDropdown(), bar->moreButton()}) {
                    QMenu *menu = anchor->menu();
                    QVERIFY(menu);
                    QTimer::singleShot(0, menu, [menu] { menu->close(); });
                    anchor->showMenu();
                    QCOMPARE(menu->y(), anchor == bar->moreButton() ? under : top);
                }

                // The row count follows the width both ways, and the popup edge with it.
                w->resize(470, 612);
                settle();
                QCOMPARE(bar->foldLevel(), 1);
                QCOMPARE(bar->height(), 2 * gap + row);
                QCOMPARE(ui::popupTop(bar), bar->mapToGlobal(QPoint(0, bar->height())).y() + ui::space(ui::gap::cluster));
                w->resize(340, 612);
                settle();
                QCOMPARE(bar->foldLevel(), 2);
                QCOMPARE(bar->height(), 3 * gap + 2 * row);
                QCOMPARE(ui::popupTop(bar), top);

                // A long name elides by exactly what row 1 lacks, the short
                // repository name staying whole, the dropdown against the row's
                // right edge; the bar stays two rows and the window its width.
                bar->setBranchLabel(QStringLiteral("feature/askpass-login-dialog"));
                settle();
                QCOMPARE(w->width(), 340);
                QCOMPARE(bar->foldLevel(), 2);
                QCOMPARE(bar->height(), 3 * gap + 2 * row);
                QVERIFY2(bar->branchButton()->text().contains(QChar(0x2026)), qPrintable(bar->branchButton()->text()));
                QVERIFY(bar->branchButton()->text().startsWith(ui::icon(ui::kBranch) + QStringLiteral("feature/")));
                QCOMPARE(bar->repoButton()->text(), ui::icon(ui::kFolderOpen) + QStringLiteral("repo") + ui::chevron());
                const QRect branch = rectOf(bar->branchButton());
                QCOMPARE(branch.x() + branch.width() + ui::space(ui::gap::cluster), rectOf(bar->syncDropdown()).x());
                QCOMPARE(rectOf(bar->syncDropdown()).right() + 1, bar->width() - margin);
                QCOMPARE(rectOf(tabs), strip);
            }
        }

        // At 16, by what the bar measures: two rows, the second a space(kBar)
        // under the first (as tall as the dropdown), the tabs alone on it and
        // across it, labelled only while every segment's label fits its
        // share, and the popups hanging from the first.
        QVERIFY(writeFixture(toml, "[font]\nbase-size = 16\n"));
        {
            ScopedEnv themeDir("OMAGIT_THEME_DIR", dir.path().toUtf8());
            ScopedEnv scratchHome("HOME", home.path().toUtf8());
            OmarchyTheme theme;
            QCOMPARE(theme.fontBase(), 16);
            theme.apply(*qApp);
            {
                WindowFixture f = mainWindow(0, false, [](MainWindow *w) { w->resize(340, 612); });
                QVERIFY(f.window);
                QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
                settle();
                MainWindow *w = f.window.get();
                TopBar *bar = f.bar();
                QVERIFY(w->isStacked());
                // Stacked before it was shown: never held to the unstacked
                // bar's minimum on the way.
                QCOMPARE(w->width(), 340);
                QCOMPARE(bar->foldLevel(), 2);
                const auto rectOf = [bar](const QWidget *c) { return QRect(c->mapTo(bar, QPoint(0, 0)), c->size()); };
                const QRect sync = rectOf(bar->syncDropdown());
                QCOMPARE(sync.y(), ui::space(ui::kBar));
                const int firstRow = sync.bottom() + 1;
                auto *tabs = static_cast<SegmentStrip *>(bar->changesTab()->parentWidget());
                const QRect strip = rectOf(tabs);
                const int margin = ui::windowMargin(w);
                QCOMPARE(strip, QRect(margin, firstRow + ui::space(ui::kBar),
                                      bar->width() - 2 * margin - ui::space(ui::gap::item) - ui::space(ui::box::control),
                                      sync.height()));
                QVERIFY(tabs->isStretch());
                for (QToolButton *b : bar->findChildren<QToolButton *>()) {
                    if (b->isVisible() && !tabs->isAncestorOf(b))
                        QVERIFY2(!rectOf(b).intersects(strip), qPrintable(b->accessibleName()));
                }
                // Each segment's hint wearing its label, whatever it wears now.
                bool fits = true;
                for (SegmentButton *segment : tabs->segments()) {
                    const bool labelled = segment->isLabelled();
                    segment->setLabelled(true);
                    fits = fits && segment->sizeHint().width() <= strip.width() / 3;
                    segment->setLabelled(labelled);
                }
                for (const SegmentButton *segment : tabs->segments())
                    QVERIFY2(segment->isLabelled() == fits, qPrintable(segment->accessibleName()));
                QCOMPARE(ui::popupTop(bar), bar->mapToGlobal(QPoint(0, firstRow)).y() + ui::space(ui::gap::cluster));
            }
        }
        g_theme.reset(new OmarchyTheme);
        g_theme->apply(*qApp);
        QVERIFY(OmarchyTheme::instance() == g_theme.get());
    }

    // Two rows are the window's narrowest presentation: the page's CHANGES
    // and MESSAGE rows fold away from the first frame, the list and the box
    // moving up into their room (the box keeping its height), and their
    // controls lead the More menu on the Changes tab — the Files view
    // submenu, the eye, and the agent settings, which then hang from More.
    // The Diff and History tabs, and a window with the rows, add nothing.
    void theExtraNarrowWindowFoldsThePagesHeaderRows()
    {
        WindowFixture f = mainWindow(0, false, [](MainWindow *w) { w->resize(340, 612); });
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        MainWindow *w = f.window.get();
        TopBar *bar = f.bar();
        CommitPage *page = f.page();
        QVERIFY(bar->isTwoRows());
        QVERIFY(page->headerRowsHidden()); // before anything settled
        settle();
        QCOMPARE(w->width(), 340);
        QVERIFY(bar->isTwoRows());
        QVERIFY(page->headerRowsHidden());
        const QList<QToolButton *> controls{page->agentButton(), page->compactButton(), page->treeButton(),
                                            page->tableButton(), page->unversionedButton()};
        for (QToolButton *b : controls)
            QVERIFY2(!b->isVisible(), qPrintable(b->accessibleName()));
        QCOMPARE(page->activeListView()->mapTo(page, QPoint(0, 0)).y(), 0);

        // The rows back and away again, by hand: the box holds its height.
        const int box = f.pageEditor()->height();
        page->setHeaderRowsHidden(false);
        settle();
        for (QToolButton *b : controls)
            QVERIFY2(b->isVisible(), qPrintable(b->accessibleName()));
        QCOMPARE(page->activeListView()->mapTo(page, QPoint(0, 0)).y(),
                 ui::space(ui::box::row) + ui::space(ui::gap::header));
        QCOMPARE(f.pageEditor()->height(), box);
        page->setHeaderRowsHidden(true);
        settle();
        QCOMPARE(f.pageEditor()->height(), box);
        QCOMPARE(page->activeListView()->mapTo(page, QPoint(0, 0)).y(), 0);

        // More on the Changes tab: the page's entries, then the bar's own.
        QMenu *menu = bar->moreButton()->menu();
        QList<QAction *> actions = filledMenu(menu);
        const QStringList texts = menuTexts(actions);
        QVERIFY2(texts.size() > 5, qPrintable(texts.join(QLatin1Char('|'))));
        QVERIFY2(texts.at(0).endsWith(QStringLiteral("Files view")), qPrintable(texts.at(0)));
        QVERIFY(actions.at(0)->menu());
        QCOMPARE(actions.at(0)->menu()->actions().size(), 3);
        QCOMPARE(texts.mid(1, 4), QStringList({ui::icon(ui::kEye) + QStringLiteral("Show unversioned files"),
                                               ui::icon(ui::kCog) + QStringLiteral("Agent settings…"),
                                               QStringLiteral("-"), ui::icon(ui::kRefresh) + QStringLiteral("Refresh")}));
        QVERIFY(actions.at(1)->isChecked());
        QCOMPARE(actions.at(2)->toolTip(), CommitPage::agentButtonTip());
        // A second fill leaves no submenu of the first behind.
        filledMenu(menu);
        QCOMPARE(menu->findChildren<QMenu *>(Qt::FindDirectChildrenOnly).size(), 1);

        // The agent settings hang from More, 4 under row 1, over the tabs; the
        // entry opens them again rather than closing them.
        filledMenu(menu).at(2)->trigger();
        settle();
        AgentPopover *card = f.agentCard();
        QVERIFY(card->isVisible());
        QCOMPARE(card->anchor(), bar->moreButton());
        const QPoint more = bar->moreButton()->mapTo(f.host(), QPoint(0, bar->moreButton()->height()));
        QCOMPARE(card->y(), more.y() + ui::space(ui::gap::cluster));
        filledMenu(menu).at(2)->trigger();
        settle();
        QVERIFY(card->isVisible());
        card->dismiss();

        // The Diff and History tabs keep More to the bar's own entries.
        for (QToolButton *tab : {bar->diffTab(), bar->historyTab()}) {
            tab->click();
            settle();
            QCOMPARE(filledMenu(menu).first()->text(), ui::icon(ui::kRefresh) + QStringLiteral("Refresh"));
        }
        bar->changesTab()->click();
        settle();
        QVERIFY(filledMenu(menu).first()->menu());

        // A wide window has the rows, and More none of their entries.
        w->resize(1000, 612);
        settle();
        QVERIFY(!bar->isTwoRows());
        QVERIFY(!page->headerRowsHidden());
        for (QToolButton *b : controls)
            QVERIFY2(b->isVisible(), qPrintable(b->accessibleName()));
        QCOMPARE(filledMenu(menu).first()->text(), ui::icon(ui::kRefresh) + QStringLiteral("Refresh"));
        w->resize(340, 612);
        settle();
        QVERIFY(bar->isTwoRows());
        QVERIFY(page->headerRowsHidden());
        QVERIFY(!page->agentButton()->isVisible());
    }

    // A shallow window folds the page's header rows the same way, at any
    // width and from the first frame. Unstacked, the ordinary row keeps More
    // for their entries with no sync button folded into it: the bare square
    // an item gap after Merge, the divider and the toggles after it, and the
    // bar's hint counting it. More leads with the Files view submenu, the eye
    // and the agent settings (hanging from More), but not in the Mini
    // layout. Made tall, the rows come back and More goes again; stacked on
    // one row, shallow folds them too. At the design's text size.
    void theShallowWindowFoldsThePagesHeaderRows()
    {
        // The desktop's theme back for whatever runs next, however this ends.
        const auto restoreTheme = qScopeGuard([] {
            g_theme.reset(new OmarchyTheme);
            g_theme->apply(*qApp);
        });
        QTemporaryDir dir, home;
        QVERIFY(dir.isValid() && home.isValid());
        QVERIFY(writeFixture(QDir(dir.path()).filePath(QStringLiteral("shell.toml")), "[font]\nbase-size = 12\n"));
        ScopedEnv themeDir("OMAGIT_THEME_DIR", dir.path().toUtf8());
        ScopedEnv scratchHome("HOME", home.path().toUtf8());
        OmarchyTheme theme;
        QCOMPARE(theme.fontBase(), 12);
        theme.apply(*qApp);

        // A short repository name, which keeps a stacked 470 to one row.
        WindowFixture f = mainWindow(0, false, [](MainWindow *w) { w->resize(1400, 400); }, {}, QStringLiteral("repo"));
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        MainWindow *w = f.window.get();
        TopBar *bar = f.bar();
        CommitPage *page = f.page();
        BadgeButton *more = bar->moreButton();
        QVERIFY(page->headerRowsHidden()); // before anything settled
        settle();
        QCOMPARE(w->size(), QSize(1400, 400));
        QVERIFY(!w->isStacked());
        QVERIFY(w->height() < ui::space(560)); // shallow
        QVERIFY(!bar->isTwoRows());
        QVERIFY(page->headerRowsHidden());
        const QList<QToolButton *> controls{page->agentButton(), page->compactButton(), page->treeButton(),
                                            page->tableButton(), page->unversionedButton()};
        for (QToolButton *b : controls)
            QVERIFY2(!b->isVisible(), qPrintable(b->accessibleName()));
        QCOMPARE(page->activeListView()->mapTo(page, QPoint(0, 0)).y(), 0);

        // The ordinary row: all four sync buttons, then More, then the group
        // gap with its divider and the toggles against the right margin.
        const auto rectOf = [bar](const QWidget *c) { return QRect(c->mapTo(bar, QPoint(0, 0)), c->size()); };
        const auto rightGroup = [&] {
            for (QToolButton *b : {bar->pullButton(), bar->pushButton(), bar->fetchButton(), bar->mergeButton()})
                QVERIFY2(b->isVisible(), qPrintable(b->accessibleName()));
            QVERIFY(more->isVisible());
            QVERIFY(more->markText().isEmpty()); // nothing folded into it
            const QRect merge = rectOf(bar->mergeButton()), square = rectOf(more);
            QCOMPARE(square.x(), merge.x() + merge.width() + ui::space(ui::gap::item));
            QCOMPARE(square.size(), QSize(ui::space(ui::box::control), ui::space(ui::box::control)));
            QWidget *divider = nullptr;
            for (QWidget *c : bar->findChildren<QWidget *>())
                if (c->isVisible() && c->width() == 1 && c->height() == ui::space(ui::box::divider)
                    && rectOf(c).x() > square.x())
                    divider = c;
            QVERIFY(divider);
            QCOMPARE(rectOf(divider).x(), square.x() + square.width() + ui::space(ui::gap::group / 2));
            QCOMPARE(rectOf(bar->layoutButton()).x(), square.x() + square.width() + ui::space(ui::gap::group));
            QCOMPARE(rectOf(bar->diffToggle()).x() + bar->diffToggle()->width(), bar->width() - ui::windowMargin(w));
            QVERIFY(rectOf(bar->historyTab()).x() + bar->historyTab()->width() < square.x());
        };
        rightGroup();
        if (QTest::currentTestFailed())
            return;
        const int keptHint = bar->sizeHint().width();

        // More's entries: the page's, then the bar's own.
        QMenu *menu = more->menu();
        QList<QAction *> actions = filledMenu(menu);
        const QStringList texts = menuTexts(actions);
        QVERIFY2(texts.size() > 5, qPrintable(texts.join(QLatin1Char('|'))));
        QVERIFY2(texts.at(0).endsWith(QStringLiteral("Files view")), qPrintable(texts.at(0)));
        QVERIFY(actions.at(0)->menu());
        QCOMPARE(actions.at(0)->menu()->actions().size(), 3);
        QCOMPARE(texts.mid(1, 4), QStringList({ui::icon(ui::kEye) + QStringLiteral("Show unversioned files"),
                                               ui::icon(ui::kCog) + QStringLiteral("Agent settings…"),
                                               QStringLiteral("-"), ui::icon(ui::kRefresh) + QStringLiteral("Refresh")}));
        filledMenu(menu).at(2)->trigger();
        settle();
        AgentPopover *card = f.agentCard();
        QVERIFY(card->isVisible());
        QCOMPARE(card->anchor(), static_cast<QWidget *>(more));
        card->dismiss();

        // The Mini layout's page is the commit card: More keeps to its own.
        w->setPaneLayout(PaneLayout::Mini, false);
        settle();
        QCOMPARE(filledMenu(menu).first()->text(), ui::icon(ui::kRefresh) + QStringLiteral("Refresh"));
        w->setPaneLayout(PaneLayout::Docked, false);
        settle();
        QVERIFY(filledMenu(menu).first()->menu());

        // Tall at the same width: the rows back, and More gone with nothing
        // folded into it, the toggles a group gap after Merge again.
        w->resize(1400, 800);
        settle();
        QVERIFY(w->height() >= ui::space(560));
        QVERIFY(!page->headerRowsHidden());
        for (QToolButton *b : controls)
            QVERIFY2(b->isVisible(), qPrintable(b->accessibleName()));
        QCOMPARE(page->activeListView()->mapTo(page, QPoint(0, 0)).y(),
                 ui::space(ui::box::row) + ui::space(ui::gap::header));
        QVERIFY(bar->mergeButton()->isVisible());
        QVERIFY(!more->isVisible());
        const QRect merge = rectOf(bar->mergeButton());
        QCOMPARE(rectOf(bar->layoutButton()).x(), merge.x() + merge.width() + ui::space(ui::gap::group));
        QCOMPARE(bar->sizeHint().width(), keptHint - ui::space(ui::box::control) - ui::space(ui::gap::item));
        QCOMPARE(filledMenu(menu).first()->text(), ui::icon(ui::kRefresh) + QStringLiteral("Refresh"));

        // Shallow again: folded again, More back where it was.
        w->resize(1400, 400);
        settle();
        QVERIFY(page->headerRowsHidden());
        rightGroup();
        if (QTest::currentTestFailed())
            return;

        // Stacked on one row, and shallow: folded too; tall there, back.
        w->resize(470, 400);
        settle();
        QVERIFY(w->isStacked());
        QVERIFY(!bar->isTwoRows());
        QVERIFY(page->headerRowsHidden());
        for (QToolButton *b : controls)
            QVERIFY2(!b->isVisible(), qPrintable(b->accessibleName()));
        QVERIFY(filledMenu(menu).first()->menu());
        w->resize(470, 612);
        settle();
        QVERIFY(w->isStacked());
        QVERIFY(!bar->isTwoRows());
        QVERIFY(!page->headerRowsHidden());
        QCOMPARE(filledMenu(menu).first()->text(), ui::icon(ui::kRefresh) + QStringLiteral("Refresh"));
    }

    // The Files view submenu keeps to the window as More does: at 340
    // neither side of More has the room for it, so it opens over More,
    // inside the window's margins; where the window has the room beside
    // More, it opens there. At the design's text size.
    void theFilesViewSubmenuKeepsInsideTheWindow()
    {
        // The desktop's theme back for whatever runs next, however this ends.
        const auto restoreTheme = qScopeGuard([] {
            g_theme.reset(new OmarchyTheme);
            g_theme->apply(*qApp);
        });
        QTemporaryDir dir, home;
        QVERIFY(dir.isValid() && home.isValid());
        QVERIFY(writeFixture(QDir(dir.path()).filePath(QStringLiteral("shell.toml")), "[font]\nbase-size = 12\n"));
        ScopedEnv themeDir("OMAGIT_THEME_DIR", dir.path().toUtf8());
        ScopedEnv scratchHome("HOME", home.path().toUtf8());
        OmarchyTheme theme;
        QCOMPARE(theme.fontBase(), 12);
        theme.apply(*qApp);

        WindowFixture f = mainWindow(0, false, [](MainWindow *w) { w->resize(340, 612); });
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        settle();
        MainWindow *w = f.window.get();
        QCOMPARE(w->width(), 340);
        QVERIFY(f.page()->headerRowsHidden());
        QToolButton *more = f.bar()->moreButton();
        // More open and its first entry made the current one, which pops the
        // submenu up as the pointer resting on it does; both are read while
        // they are open.
        QRect menu, submenu;
        const auto open = [more, &menu, &submenu] {
            menu = submenu = QRect();
            QMenu *moreMenu = more->menu();
            QTimer::singleShot(0, moreMenu, [moreMenu, &menu, &submenu] {
                QAction *views = moreMenu->actions().value(0);
                if (views && views->menu()) {
                    moreMenu->setActiveAction(views);
                    if (views->menu()->isVisible())
                        submenu = views->menu()->geometry();
                    views->menu()->close();
                }
                menu = moreMenu->geometry();
                moreMenu->close();
            });
            more->showMenu();
        };
        const int margin = ui::windowMargin(w);
        const auto windowRect = [w] { return QRect(w->mapToGlobal(QPoint(0, 0)), w->size()); };

        open();
        QVERIFY(!submenu.isNull());
        QRect window = windowRect();
        QVERIFY(menu.x() - submenu.width() < window.x() + margin);
        QVERIFY(menu.x() + menu.width() + submenu.width() > window.x() + window.width() - margin);
        QVERIFY2(submenu.x() >= window.x() + margin, qPrintable(QString::number(submenu.x())));
        QVERIFY2(submenu.x() + submenu.width() <= window.x() + window.width() - margin,
                 qPrintable(QString::number(submenu.x() + submenu.width())));

        // Wider, with the rows folded away by hand: the room is left of More.
        w->resize(560, 612);
        settle();
        QVERIFY(w->isStacked());
        QVERIFY(!f.bar()->isTwoRows());
        f.page()->setHeaderRowsHidden(true);
        settle();
        open();
        QVERIFY(!submenu.isNull());
        window = windowRect();
        QVERIFY(menu.x() + menu.width() + submenu.width() > window.x() + window.width() - margin);
        QVERIFY(menu.x() - submenu.width() >= window.x() + margin);
        QCOMPARE(submenu.x() + submenu.width(), menu.x());
    }

    // The row count follows what the first row has to hold, not only the
    // width: a longer branch or repository name, or a dropdown widened by a
    // three-digit count, takes the tabs to a row of their own in a window
    // that keeps its size, and the bar's height, the body and the popups'
    // edge move with it and back.
    void theStackedBarsRowsFollowItsContent()
    {
        QTemporaryDir dir, home;
        QVERIFY(dir.isValid() && home.isValid());
        QVERIFY(writeFixture(QDir(dir.path()).filePath(QStringLiteral("shell.toml")), "[font]\nbase-size = 12\n"));
        {
            ScopedEnv themeDir("OMAGIT_THEME_DIR", dir.path().toUtf8());
            ScopedEnv scratchHome("HOME", home.path().toUtf8());
            OmarchyTheme theme;
            QCOMPARE(theme.fontBase(), 12);
            theme.apply(*qApp);
            {
                WindowFixture f = mainWindow(0, false, [](MainWindow *w) { w->resize(470, 612); }, {},
                                             QStringLiteral("repo"));
                QVERIFY(f.window);
                QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
                settle();
                MainWindow *w = f.window.get();
                TopBar *bar = f.bar();
                QVERIFY(w->isStacked());
                QCOMPARE(bar->branchLabel(), QStringLiteral("main"));
                const int row = ui::space(ui::box::control), gap = ui::space(ui::kBar);
                // One row or two: the bar 8 + 28 + 8 or 8 + 28 + 8 + 28 + 8,
                // the body under it, where it was the last time the bar had
                // that many rows, and the popups under the bar or under row 1.
                QHash<int, int> bodyTops;
                const auto rows = [&](int count) {
                    settle();
                    QCOMPARE(bar->foldLevel() == 2, count == 2);
                    QCOMPARE(bar->height(), (count + 1) * gap + count * row);
                    const int body = f.page()->mapTo(w, QPoint(0, 0)).y();
                    QVERIFY(body >= bar->mapTo(w, QPoint(0, bar->height())).y());
                    QCOMPARE(body, bodyTops.value(count, body));
                    bodyTops.insert(count, body);
                    const int edge = count == 2 ? gap + row : bar->height();
                    QCOMPARE(ui::popupTop(bar), bar->mapToGlobal(QPoint(0, edge)).y() + ui::space(ui::gap::cluster));
                };

                // The branch name, the window held at 470.
                rows(1);
                bar->setBranchLabel(QStringLiteral("feature/askpass-login-dialog"));
                rows(2);
                QCOMPARE(w->width(), 470);
                bar->setBranchLabel(QStringLiteral("main"));
                rows(1);
                QCOMPARE(w->width(), 470);
                // The repository's name, the same way.
                bar->setRepositoryName(QStringLiteral("omagit-workspace"));
                rows(2);
                QCOMPARE(w->width(), 470);
                bar->setRepositoryName(QStringLiteral("repo"));
                rows(1);
                QCOMPARE(w->width(), 470);

                // The narrowest width "main" keeps one row at: a pixel short
                // of it, the tabs take the second.
                int narrowest = 470;
                while (narrowest > 300) {
                    w->resize(narrowest - 1, 612);
                    settle();
                    if (bar->foldLevel() == 2)
                        break;
                    --narrowest;
                }
                QVERIFY(narrowest > 300);
                w->resize(narrowest, 612);
                rows(1);
                QCOMPARE(w->width(), narrowest);

                // There, Pull's 99+ widens the dropdown past what the row has
                // left; a single digit gives it back. The window has no remote
                // to count against, so the count goes on Pull, which the
                // dropdown follows.
                const int dropdown = bar->syncDropdown()->width();
                bar->pullButton()->setCount(100);
                rows(2);
                QVERIFY(bar->syncDropdown()->width() > dropdown);
                bar->pullButton()->setCount(3);
                rows(1);
                QCOMPARE(bar->syncDropdown()->width(), dropdown);
                QCOMPARE(w->width(), narrowest);
            }
        }
        g_theme.reset(new OmarchyTheme);
        g_theme->apply(*qApp);
        QVERIFY(OmarchyTheme::instance() == g_theme.get());
    }

    // The design's window grid (screens.js screen(), topBar(), footer()) at a
    // Wide, Normal window (density: margin 16, block 8): the top bar 8 + 28 + 8
    // = 44 with its hairline the last row, its row 8 down and the window's
    // margin in from either side; the body 8 under the bar's rule, the margin
    // in from either side and 8 over the footer's; the footer 28 with its
    // hairline the first row, the keybindings a 24 px ghost square flush with
    // the margin, centred. The two rules run from edge to edge in the fainter
    // chrome tone. Every pane's first box starts 32 under the body's top: a
    // 24 px header row and its 8, a 28 px control row and its 4.
    void theWindowFollowsTheDesignsGrid()
    {
        WindowFixture f = mainWindow(0, false, [](MainWindow *w) { w->resize(ui::space(1400), ui::space(800)); });
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        settle();
        QWidget *host = f.host();
        const int width = host->width(), height = host->height();
        const ui::Density density = ui::densityFor(WidthClass::Wide, HeightClass::Normal);
        const int margin = ui::space(density.margin), block = ui::space(density.block);
        QCOMPARE(ui::windowMargin(f.window.get()), margin);
        const int barHeight = ui::space(ui::kBar + ui::box::control + ui::kBar);
        QCOMPARE(barHeight, ui::space(44));
        TopBar *bar = f.bar();
        QCOMPARE(rectIn(bar, host), QRect(0, 0, width, barHeight));
        for (QToolButton *b : {bar->repoButton(), bar->branchButton(), static_cast<QToolButton *>(bar->pullButton()),
                               bar->layoutButton(), bar->diffToggle()}) {
            QCOMPARE(rectIn(b, host).y(), ui::space(ui::kBar));
            QCOMPARE(b->height(), ui::space(ui::box::control));
        }
        QCOMPARE(rectIn(bar->repoButton(), host).x(), margin);
        QCOMPARE(rectIn(bar->diffToggle(), host).right() + 1, width - margin);

        auto *footer = f.window->findChild<Footer *>();
        QVERIFY(footer);
        const int footerHeight = ui::space(ui::box::footer);
        QCOMPARE(rectIn(footer, host), QRect(0, height - footerHeight, width, footerHeight));
        const int keys = ui::space(ui::box::row);
        QCOMPARE(rectIn(footer->keybindingsButton(), host),
                 QRect(width - margin - keys, height - footerHeight + (footerHeight - keys) / 2, keys, keys));
        QCOMPARE(footer->keybindingsButton()->text(), ui::icon(ui::kKeyboard, QStringLiteral("K")).trimmed());
        // The settings button: the same square, an item gap before it.
        QCOMPARE(rectIn(footer->settingsButton(), host),
                 rectIn(footer->keybindingsButton(), host).translated(-keys - ui::space(ui::gap::item), 0));
        QCOMPARE(footer->settingsButton()->text(), ui::icon(ui::kCog, QStringLiteral("⚙")).trimmed());

        // The rules: one row of pixels, the window's whole width, at the bar's
        // last row and the footer's first.
        const QColor chrome = OmarchyTheme::instance()->hairline();
        const auto rule = [&](QWidget *owner) -> QWidget * {
            for (QWidget *w : owner->findChildren<QWidget *>())
                if (w->height() == 1 && w->width() == width)
                    return w;
            return nullptr;
        };
        QWidget *barRule = rule(bar), *footerRule = rule(footer);
        QVERIFY(barRule && footerRule);
        QCOMPARE(rectIn(barRule, host).y(), barHeight - 1);
        QCOMPARE(rectIn(footerRule, host).y(), height - footerHeight);
        for (QWidget *r : {barRule, footerRule})
            QCOMPARE(r->palette().color(QPalette::Window).rgba(), chrome.rgba());

        // The body, and the page in it.
        const QRect page = rectIn(f.page(), host);
        QCOMPARE(page.x(), margin);
        QCOMPARE(page.y(), barHeight + ui::space(ui::kBar));
        QCOMPARE(page.bottom() + 1, height - footerHeight - ui::space(ui::kBar));
        const QRect diff = rectIn(f.window->findChild<DiffPane *>(), host);
        QCOMPARE(diff.right() + 1, width - margin);
        QCOMPARE(diff.y(), page.y());
        // The diff toolbar is a control row, 4 over the diff; the CHANGES row a
        // 24 px header row, 8 over the list: both boxes start 32 under the
        // body's top.
        QToolButton *prev = nullptr;
        for (QToolButton *b : f.window->findChild<DiffPane *>()->findChildren<QToolButton *>())
            if (b->toolTip().startsWith(QLatin1String("Previous change")))
                prev = b;
        QVERIFY(prev);
        const QRect prevRect = rectIn(prev, host);
        const QRect diffView = rectIn(f.window->findChild<DiffPane *>()->view(), host);
        QCOMPARE(prevRect.y(), diff.y());
        QCOMPARE(diffView.y(), prevRect.bottom() + 1 + ui::space(ui::gap::controlRow));
        QCOMPARE(diffView.y() - page.y(), ui::space(32));
        QCOMPARE(rectIn(f.page()->table(), host).y() - page.y(), ui::space(32));
        QCOMPARE(rectIn(f.page()->tableButton(), host).y(), page.y());
        QCOMPARE(f.page()->tableButton()->height(), ui::space(ui::box::row));
        // The splitter's gap is the window's margin.
        QCOMPARE(diff.x(), page.right() + 1 + margin);
        // The message box a block gap over the action bar, which ends with the
        // page, on the diff pane's last line; the header rows' buttons stand
        // 4 inside its right edge.
        auto *commit = f.page()->findChild<QPushButton *>();
        QVERIFY(commit);
        auto *message = f.page()->findChild<MessageEdit *>();
        QVERIFY(message);
        QCOMPARE(rectIn(message, host).bottom() + 1 + block, rectIn(commit, host).y());
        QCOMPARE(commit->height(), ui::space(ui::box::control));
        // Under the list, it still has the keyboard when the window comes up.
        QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget *>(message));
        QCOMPARE(rectIn(commit, host).bottom(), page.bottom());
        QCOMPARE(rectIn(commit, host).bottom(), diff.bottom());
        QCOMPARE(rectIn(f.page()->agentButton(), host).right() + 1, page.right() + 1 - ui::space(ui::gap::icon));
        QCOMPARE(rectIn(f.page()->unversionedButton(), host).y(), rectIn(f.page()->tableButton(), host).y());

        // The history's commit list starts on the same line.
        f.window->setMode(MainWindow::HistoryMode);
        settle();
        auto *history = f.window->findChild<HistoryView *>();
        QCOMPARE(rectIn(history->commitsTable(), host).y() - rectIn(history, host).y(), ui::space(32));
        QCOMPARE(rectIn(history->commitsTable(), host).y(), diffView.y());
        f.window->setMode(MainWindow::CommitMode);
        settle();

        // Mini: the rail is one 40 px tile wide at the margin, the diff pane a
        // margin after it; stacked, the Diff tab keeps the stacked margin.
        f.window->setPaneLayout(PaneLayout::Mini, false);
        settle();
        MiniRail *rail = f.rail();
        QCOMPARE(rectIn(rail, host), QRect(margin, barHeight + ui::space(ui::kBar), ui::space(ui::box::tile), rail->height()));
        QCOMPARE(rectIn(f.window->findChild<DiffPane *>(), host).x(), 2 * margin + ui::space(ui::box::tile));
        f.window->resize(ui::space(470), ui::space(612));
        settle();
        QVERIFY(f.window->isStacked() && f.window->diffTab());
        const int stacked = ui::space(ui::densityFor(WidthClass::Stacked, HeightClass::Normal).margin);
        QCOMPARE(ui::windowMargin(f.window.get()), stacked);
        QCOMPARE(rectIn(f.window->findChild<DiffPane *>(), host).x(), 2 * stacked + ui::space(ui::box::tile));
        f.window->setPaneLayout(PaneLayout::Docked, false);
    }

    // The block gap follows the window's height: 4 shallow, 8 normal, 12 tall
    // (screens.js density()), between the changes list and the MESSAGE row
    // (the message box, where a shallow window folds the row away) as between
    // the message box and the action bar; the rows of every list are 24.
    void theBlockGapFollowsTheWindowsHeight()
    {
        WindowFixture f = mainWindow(0, false, [](MainWindow *w) { w->resize(ui::space(945), ui::space(1234)); });
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        settle();
        QWidget *host = f.host();
        CommitPage *page = f.page();
        auto *commit = page->findChild<QPushButton *>();
        auto *message = page->findChild<MessageEdit *>();
        QLabel *messageLabel = nullptr;
        for (QLabel *l : page->findChildren<QLabel *>(QStringLiteral("sectionLabel")))
            if (l->text() == QLatin1String("MESSAGE"))
                messageLabel = l;
        QVERIFY(commit && message && messageLabel);
        const QList<QPair<int, HeightClass>> heights{
            {1234, HeightClass::Tall}, {612, HeightClass::Normal}, {400, HeightClass::Shallow}};
        for (const auto &[height, heightClass] : heights) {
            f.window->resize(ui::space(945), ui::space(height));
            settle();
            const QByteArray where = QByteArray::number(height);
            const int block = ui::space(ui::densityFor(WidthClass::Medium, heightClass).block);
            // The MESSAGE row is the 24 px header row the label sits in.
            const bool folded = heightClass == HeightClass::Shallow;
            QVERIFY2(messageLabel->isVisible() == !folded, where.constData());
            const int messageRow = rectIn(folded ? static_cast<QWidget *>(message) : messageLabel, host).y();
            const int listBottom = rectIn(page->activeListView(), host).bottom() + 1;
            QVERIFY2(messageRow - listBottom == block, where.constData());
            QVERIFY2(rectIn(commit, host).y() - (rectIn(message, host).bottom() + 1) == block, where.constData());
            if (!folded)
                QVERIFY2(messageLabel->height() == ui::space(ui::box::row), where.constData());
        }
        // Every clickable row is 24: the files, the commits, a menu's.
        QCOMPARE(page->table()->verticalHeader()->defaultSectionSize(), ui::space(ui::box::row));
        QCOMPARE(page->table()->rowHeight(0), ui::space(ui::box::row));
        f.window->setMode(MainWindow::HistoryMode);
        settle();
        auto *history = f.window->findChild<HistoryView *>();
        QCOMPARE(history->commitsTable()->rowHeight(0), ui::space(ui::box::row));
        TickMenu menu;
        menu.addAction(QStringLiteral("Refresh"));
        menu.ensurePolished();
        QCOMPARE(menu.actionGeometry(menu.actions().first()).height(), ui::space(ui::box::row));
    }

    // A shallow window has no footer: the body ends the window's side margin
    // over its bottom edge, and the action bar with it, as does the diff pane
    // or the rail beside it; messages go nowhere
    // and Ctrl+K still opens the keybindings. Back above 560 the footer
    // returns, the body ends 8 over it, the action bar with it.
    void theShallowWindowDropsTheFooter()
    {
        WindowFixture f = mainWindow(0, false, [](MainWindow *w) { w->resize(ui::space(945), ui::space(612)); });
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        settle();
        MainWindow *w = f.window.get();
        QWidget *host = f.host();
        auto *footer = w->findChild<Footer *>();
        QVERIFY(footer && footer->isVisible());
        auto *commit = f.page()->findChild<QPushButton *>();
        QVERIFY(commit);
        const auto bottomOf = [&](QWidget *widget) { return rectIn(widget, host).bottom() + 1; };
        const auto tall = [&] {
            QVERIFY(footer->isVisible());
            QCOMPARE(bottomOf(f.page()), host->height() - ui::space(ui::box::footer) - ui::space(ui::kBar));
            QCOMPARE(bottomOf(commit), bottomOf(f.page()));
            QCOMPARE(bottomOf(commit), bottomOf(w->findChild<DiffPane *>()));
        };
        tall();

        for (int width : {945, 470}) {
            w->resize(ui::space(width), ui::space(400));
            settle();
            const int margin = ui::windowMargin(w);
            QCOMPARE(margin, ui::space(width == 945 ? 12 : 8));
            QVERIFY(!footer->isVisible());
            QCOMPARE(bottomOf(f.page()), host->height() - margin);
            QCOMPARE(bottomOf(commit), host->height() - margin);
            QCOMPARE(bottomOf(f.page()->optionsButton()), host->height() - margin);
            QCOMPARE(commit->height(), ui::space(ui::box::control));
            if (width == 945) {
                QCOMPARE(bottomOf(w->findChild<DiffPane *>()), host->height() - margin);
                // The history's last row ends there too.
                w->setMode(MainWindow::HistoryMode);
                settle();
                auto *history = w->findChild<HistoryView *>();
                QCOMPARE(bottomOf(history), host->height() - margin);
                w->setMode(MainWindow::CommitMode);
                settle();
            } else {
                w->setDiffTab(true);
                settle();
                QCOMPARE(bottomOf(f.rail()), host->height() - margin);
                QCOMPARE(bottomOf(w->findChild<DiffPane *>()), host->height() - margin);
                w->setDiffTab(false);
                settle();
            }
        }
        // A message while the footer is away shows nowhere and breaks nothing.
        footer->showStatus(QStringLiteral("Fetched"), 50);
        QTest::qWait(100);
        QVERIFY(!footer->isVisible());
        // Ctrl+K still opens the keybindings.
        QVERIFY(activate(w));
        QTest::keyClick(w, Qt::Key_K, Qt::ControlModifier);
        QTRY_VERIFY(w->findChild<KeybindingsPanel *>() && w->findChild<KeybindingsPanel *>()->isVisible());
        auto *panel = w->findChild<KeybindingsPanel *>();
        panel->close();
        settle();

        w->resize(ui::space(945), ui::space(612));
        settle();
        tall();
    }

    // The top bar's popups hang 4 px under the bar, the sync and more menus
    // at the design's widths.
    void theTopBarsPopupsHangUnderTheBar()
    {
        WindowFixture f = mainWindow(0, false, [](MainWindow *w) { w->resize(ui::space(627), ui::space(612)); });
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        settle();
        TopBar *bar = f.bar();
        QVERIFY(bar->isStacked());
        const int top = bar->mapToGlobal(QPoint(0, bar->height())).y() + ui::space(4);
        QCOMPARE(ui::popupTop(bar), top);
        const QList<QPair<QToolButton *, int>> menus{{bar->syncDropdown(), 260}, {bar->moreButton(), 240}};
        for (const auto &entry : menus) {
            QMenu *menu = entry.first->menu();
            QVERIFY(menu);
            QTimer::singleShot(0, menu, [menu] { menu->close(); });
            entry.first->showMenu();
            QCOMPARE(menu->width(), ui::space(entry.second));
            QCOMPARE(menu->y(), top);
            QVERIFY(menu->geometry().right() < f.window->mapToGlobal(QPoint(f.window->width(), 0)).x());
        }
    }

    // The branch menu starts at the chip and keeps to the room right of it
    // (screens.js: Math.min(300, W - m - bx)): at 340 it ends inside the
    // window's right margin, where a window-wide room would run past it;
    // wide, it is the design's 300.
    void theBranchMenuKeepsToTheRoomRightOfTheChip()
    {
        // At base 12, where the chip's x plus the design's 300 runs past a 340 window.
        QTemporaryDir dir, home;
        QVERIFY(dir.isValid() && home.isValid());
        QVERIFY(writeFixture(QDir(dir.path()).filePath(QStringLiteral("shell.toml")), "[font]\nbase-size = 12\n"));
        {
            ScopedEnv themeDir("OMAGIT_THEME_DIR", dir.path().toUtf8());
            ScopedEnv scratchHome("HOME", home.path().toUtf8());
            OmarchyTheme theme;
            QCOMPARE(theme.fontBase(), 12);
            theme.apply(*qApp);
            {
                WindowFixture f = mainWindow(0, false, [](MainWindow *w) { w->resize(340, 612); });
                QVERIFY(f.window);
                QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
                settle();
                MainWindow *w = f.window.get();
                TopBar *bar = f.bar();
                QVERIFY(w->isStacked());
                QCOMPARE(w->width(), 340);
                QVERIFY(bar->branchButton()->mapTo(w, QPoint(0, 0)).x() + ui::space(300) > w->width());
                // The menu lives while it is open, so its geometry is read then.
                const auto open = [&] {
                    QRect geometry;
                    QTimer::singleShot(0, w, [w, &geometry] {
                        if (auto *menu = w->findChild<BranchMenu *>()) {
                            geometry = menu->geometry();
                            menu->close();
                        }
                    });
                    bar->branchButton()->click();
                    return geometry;
                };
                const QRect menu = open();
                QVERIFY(!menu.isNull());
                QCOMPARE(menu.x(), bar->branchButton()->mapToGlobal(QPoint(0, 0)).x());
                QVERIFY2(menu.x() + menu.width() <= w->mapToGlobal(QPoint(w->width(), 0)).x() - ui::windowMargin(w),
                         qPrintable(QStringLiteral("%1 + %2").arg(menu.x()).arg(menu.width())));
                QCOMPARE(menu.y(), ui::popupTop(bar));

                w->resize(ui::space(945), ui::space(612));
                settle();
                QCOMPARE(open().width(), ui::space(300));
            }
        }
        g_theme.reset(new OmarchyTheme);
        g_theme->apply(*qApp);
        QVERIFY(OmarchyTheme::instance() == g_theme.get());
    }

    // The window's classes size the commit page, and nothing of it is saved:
    // the left section's default width and the message box's resting height
    // by the width class (560 / 96 from 1400, 400 / 80 from 1000, 340 / 64
    // below: 5, 4 and 3 lines of 16 inside 8 px of padding), "Amend" in the
    // medium class, and a shallow window (under 560) gets the one-line box —
    // a 28 px field — and the stacked action bar at any width.
    void theWindowClassesSizeTheCommitPage()
    {
        WindowFixture f = mainWindow(0, false, [](MainWindow *w) { w->resize(ui::space(1400), ui::space(800)); });
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        settle();
        MainWindow *w = f.window.get();
        CommitPage *page = f.page();
        auto *amend = page->findChild<QCheckBox *>();
        auto *commit = page->findChild<QPushButton *>();
        QVERIFY(amend && commit);
        const auto check = [&](int width, int height, int left, int message, bool shortAmend, bool stackedBar) {
            w->resize(width, height);
            settle();
            QCOMPARE(w->width(), width);
            QTRY_COMPARE(bodySplitter(f)->sizes().first(), left);
            QTRY_COMPARE(f.pageEditor()->height(), message);
            QCOMPARE(page->optionsButton()->isVisible(), stackedBar);
            QCOMPARE(amend->isVisible(), !stackedBar);
            QCOMPARE(commit->text().endsWith(QStringLiteral("⏎")), !stackedBar);
            if (!stackedBar)
                QCOMPARE(amend->text(), shortAmend ? QStringLiteral("Amend") : QStringLiteral("Amend last commit"));
        };
        check(ui::space(1400), ui::space(800), ui::space(560), ui::space(96), false, false);
        if (QTest::currentTestFailed())
            return;
        check(ui::space(1200), ui::space(800), ui::space(400), ui::space(80), false, false);
        if (QTest::currentTestFailed())
            return;
        check(ui::space(900), ui::space(800), ui::space(340), ui::space(64), true, false);
        if (QTest::currentTestFailed())
            return;
        check(ui::space(900), ui::space(500), ui::space(340), ui::space(ui::box::control), true, true);
        if (QTest::currentTestFailed())
            return;
        check(ui::space(1400), ui::space(500), ui::space(560), ui::space(ui::box::control), false, true);
        if (QTest::currentTestFailed())
            return;
        check(ui::space(1400), ui::space(800), ui::space(560), ui::space(96), false, false);
        QVERIFY(!QSettings().contains(settings::kWindowLeftWidth));
        QVERIFY(!QSettings().contains(settings::kWindowCommitSplitter));

        // A width the user dragged wins over the class's.
        QSplitter *splitter = bodySplitter(f);
        QSplitterHandle *handle = splitter->handle(1);
        QTest::mousePress(handle, Qt::LeftButton, {}, handle->rect().center());
        QTest::mouseMove(handle, handle->rect().center() + QPoint(ui::space(40), 0));
        QTest::mouseRelease(handle, Qt::LeftButton, {}, handle->rect().center() + QPoint(ui::space(40), 0));
        settle();
        QVERIFY(QSettings().contains(settings::kWindowLeftWidth));
        const int dragged = QSettings().value(settings::kWindowLeftWidth).toInt();
        w->resize(ui::space(1200), ui::space(800));
        settle();
        QCOMPARE(splitter->sizes().first(), dragged);
        QSettings().remove(settings::kWindowLeftWidth);
    }

    // The commit list's columns by the window's width class (screens.js
    // commitsTable()), the SHA never among them and Message taking exactly
    // what is left, so neither table scrolls sideways at any of the design's
    // frames; the files table under it likewise (changesTable()): the 32 px
    // row numbers, Path, Status (the St pill where it is 32 px), "+ −" and
    // Size as the class has them, and Name taking the rest.
    void theHistoryTablesFollowTheWidthClass()
    {
        QSettings().remove(settings::kWindowLeftWidth);
        WindowFixture f = mainWindow(0, false, [](MainWindow *w) { w->resize(ui::space(1900), ui::space(1234)); });
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        MainWindow *w = f.window.get();
        w->setMode(MainWindow::HistoryMode);
        settle();
        auto *history = w->findChild<HistoryView *>();
        QTableView *commits = history->commitsTable();
        QTableView *files = history->filesTable();
        auto *filesHeader = qobject_cast<ChangesHeader *>(files->horizontalHeader());
        QVERIFY(filesHeader);
        const auto shown = [](QTableView *table, int column) {
            return table->isColumnHidden(column) ? 0 : table->columnWidth(column);
        };
        const auto fitsExactly = [&](QTableView *table, int stretch, int columns) {
            int others = 0;
            for (int c = 0; c < columns; ++c)
                if (c != stretch)
                    others += shown(table, c);
            return table->columnWidth(stretch) == table->viewport()->width() - others
                && table->columnWidth(stretch) >= ui::space(ui::kMinStretchColumn)
                && table->horizontalScrollBar()->maximum() == 0;
        };
        // The design measures the first and the last column from the table's
        // outer edge, whose frame the cells are inside of.
        const int edge = commits->frameWidth();
        QCOMPARE(files->frameWidth(), edge);

        // The frame's size, then the commit list's graph, author and date and
        // the files' path, status, "+ −" and size, in design pixels (0: not
        // shown); a stacked frame has no files table.
        struct Frame {
            int width, height;
            int graph, author, date;
            int path, status, lines, size;
        };
        // Author shows in every class (the user's rule, where the design's
        // narrower frames have none).
        const QList<Frame> frames{{1900, 1234, 40, 88, 128, 128, 72, 72, 72},
                                  {1200, 800, 40, 88, 120, 120, 72, 60, 0},
                                  {945, 1234, 36, 88, 88, 112, 32, 0, 0},
                                  {945, 612, 36, 88, 88, 112, 32, 0, 0},
                                  {627, 612, 32, 72, 0, -1, -1, -1, -1},
                                  {470, 612, 32, 72, 0, -1, -1, -1, -1}};
        for (const Frame &frame : frames) {
            w->resize(ui::space(frame.width), ui::space(frame.height));
            settle();
            const QByteArray where = QStringLiteral("%1x%2").arg(frame.width).arg(frame.height).toUtf8();
            QVERIFY2(commits->isColumnHidden(HistoryModel::Hash), where.constData());
            QVERIFY2(shown(commits, HistoryModel::Graph) == ui::space(frame.graph) - edge, where.constData());
            // The last column shown, Date or else Author, is the frame less.
            QVERIFY2(shown(commits, HistoryModel::Author)
                         == (ui::space(frame.author) - (frame.date > 0 ? 0 : edge)) * (frame.author > 0),
                     where.constData());
            QVERIFY2(shown(commits, HistoryModel::Date) == (ui::space(frame.date) - edge) * (frame.date > 0),
                     where.constData());
            QVERIFY2(fitsExactly(commits, HistoryModel::Message, HistoryModel::ColumnCount), where.constData());

            if (frame.path < 0) {
                QVERIFY2(files->isHidden(), where.constData());
                continue;
            }
            QVERIFY2(files->isVisible(), where.constData());
            // The last column shown: Size, "+ −" or the status, whichever the class ends on.
            const int last = frame.size > 0 ? ChangesModel::Size : frame.lines > 0 ? ChangesModel::LinesAdded
                                                                                    : ChangesModel::Status;
            const auto px = [&](int column, int design) {
                return design > 0 ? ui::space(design) - (column == last ? edge : 0) : 0;
            };
            QVERIFY2(shown(files, ChangesModel::Check) == ui::space(32) - edge, where.constData());
            QVERIFY2(shown(files, ChangesModel::Path) == px(ChangesModel::Path, frame.path), where.constData());
            QVERIFY2(shown(files, ChangesModel::Status) == px(ChangesModel::Status, frame.status), where.constData());
            QVERIFY2(shown(files, ChangesModel::LinesAdded) == px(ChangesModel::LinesAdded, frame.lines),
                     where.constData());
            QVERIFY2(shown(files, ChangesModel::Size) == px(ChangesModel::Size, frame.size), where.constData());
            QVERIFY2(files->isColumnHidden(ChangesModel::Extension), where.constData());
            QVERIFY2(files->isColumnHidden(ChangesModel::LinesRemoved), where.constData());
            QVERIFY2(fitsExactly(files, ChangesModel::Name, ChangesModel::ColumnCount), where.constData());
            QCOMPARE(filesHeader->sectionText(ChangesModel::Check), QStringLiteral("#"));
            QCOMPARE(filesHeader->sectionText(ChangesModel::LinesAdded), QStringLiteral("+ −"));
            QCOMPARE(filesHeader->sectionText(ChangesModel::Status),
                     frame.status == 32 ? QStringLiteral("St") : QStringLiteral("Status"));
            // Size reads last, after the line counts.
            QCOMPARE(filesHeader->visualIndex(ChangesModel::Size), ChangesModel::ColumnCount - 1);
        }
    }

    // A shallow window has room for the commit list alone: no details card
    // and no files table — which stays alive hidden, its current row still
    // the file whose diff the pane shows. Stacked, the card comes back
    // without the files table; the keyboard goes to the commit list.
    void theShallowHistoryKeepsTheCommitListAlone()
    {
        WindowFixture f = mainWindow(0, false, [](MainWindow *w) { w->resize(ui::space(945), ui::space(612)); });
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        MainWindow *w = f.window.get();
        w->setMode(MainWindow::HistoryMode);
        settle();
        auto *history = w->findChild<HistoryView *>();
        QTableView *files = history->filesTable();
        CommitDetails *card = history->details();
        QVERIFY(card->isVisible() && files->isVisible());
        // The card 152 and the files a header and three rows (screens.js
        // historyPage()), a block gap apart.
        QCOMPARE(card->height(), ui::space(152));
        QCOMPARE(files->height(), 4 * ui::space(ui::box::row));
        const int block = ui::space(ui::densityFor(WidthClass::Medium, HeightClass::Normal).block);
        QCOMPARE(files->mapTo(history, QPoint(0, 0)).y() - (card->mapTo(history, QPoint(0, 0)).y() + card->height()), block);
        QCOMPARE(history->activeListView(), static_cast<QAbstractItemView *>(files));
        // The last section ends 24 px above the page's bottom edge, the count
        // row taking those 24 px, its text centred on them the design's way.
        auto *count = history->findChild<QLabel *>(QStringLiteral("historyCount"));
        QVERIFY(count);
        const auto bottomIn = [history](QWidget *widget) { return widget->mapTo(history, QPoint(0, widget->height())).y(); };
        QCOMPARE(history->height() - bottomIn(files), ui::space(ui::box::row));
        QCOMPARE(count->mapTo(history, QPoint(0, 0)).y(), bottomIn(files));
        QCOMPARE(bottomIn(count), history->height());
        QCOMPARE(count->contentsMargins().top() + QFontMetrics(count->font()).ascent(),
                 qRound(ui::space(ui::box::row) / 2.0 + 0.36 * count->font().pixelSize()));
        QVERIFY(files->currentIndex().isValid());
        const QString current = files->currentIndex().data(ChangesModel::PathRole).toString();
        QCOMPARE(current, QStringLiteral("a.txt"));

        w->resize(ui::space(945), ui::space(400));
        settle();
        QVERIFY(card->isHidden() && files->isHidden());
        QCOMPARE(history->activeListView(), static_cast<QAbstractItemView *>(history->commitsTable()));
        // The commit list runs down to the count row, 24 px above the edge.
        QCOMPARE(history->height() - bottomIn(history->commitsTable()), ui::space(ui::box::row));
        QCOMPARE(count->mapTo(history, QPoint(0, 0)).y(), bottomIn(history->commitsTable()));
        Commit c;
        FileChange file;
        QVERIFY(history->currentFile(&c, &file));
        QCOMPARE(file.path, current);
        QCOMPARE(files->currentIndex().data(ChangesModel::PathRole).toString(), current);
        QCOMPARE(f.diff()->document().lines.isEmpty(), false);

        // Stacked and tall: the card, 132 px, and still no files table.
        w->resize(ui::space(627), ui::space(612));
        settle();
        QVERIFY(card->isVisible() && files->isHidden());
        QCOMPARE(card->height(), ui::space(132));
        QCOMPARE(history->height() - bottomIn(card), ui::space(ui::box::row));
        // Back to the ordinary width: both, at the design's heights.
        w->resize(ui::space(945), ui::space(612));
        settle();
        QVERIFY(card->isVisible() && files->isVisible());
        QCOMPARE(card->height(), ui::space(152));
        QCOMPARE(files->height(), 4 * ui::space(ui::box::row));
        QCOMPARE(files->currentIndex().data(ChangesModel::PathRole).toString(), current);
    }
};

UI_TEST(WindowSizesTest);

#include "window_sizes_test.moc"
