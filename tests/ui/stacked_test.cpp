// The stacked window under its stacking width: one presentation at a time
// behind the tabs, a unified diff, menus that stay inside the window, and a
// switch that keeps the selection, the scroll and the diff.
#include "UiTestCase.h"
#include "fixtures.h"

#include "../../src/CommitDetails.h"
#include "../../src/DiffModel.h"
#include "../../src/DiffPane.h"
#include "../../src/Footer.h"
#include "../../src/HistoryView.h"
#include "../../src/Settings.h"
#include "../../src/TickMenu.h"
#include "../../src/UiHelpers.h"

#include <QApplication>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTimer>

namespace {

// What the window keeps of the user's layout choices, presence included: the
// stacked presentation must leave every one of them as it found them.
QStringList windowPrefs()
{
    QSettings conf;
    QStringList out;
    for (const QLatin1StringView key : {settings::kWindowLayout, settings::kWindowDiffPane, settings::kWindowFilesView,
                                        settings::kWindowLeftWidth})
        out << QString(key) + QLatin1Char('=')
                + (conf.contains(key) ? conf.value(key).toString() : QStringLiteral("<absent>"));
    return out;
}

// One pixel under the stacking width, or back out of it.
void stack(const WindowFixture &f)
{
    f.window->resize(ui::space(700) - 1, f.window->height());
    settle();
}

void unstack(const WindowFixture &f)
{
    f.window->resize(qMax(1200, ui::space(700) + 200), f.window->height());
    settle();
}

// The stacked row's controls, where they stand in the bar.
QStringList stackedRow(const BarFixture &f)
{
    QStringList out;
    for (QWidget *w : QList<QWidget *>{f.bar->repoButton(), f.bar->branchButton(), f.tabs(), f.bar->syncDropdown(),
                                       f.bar->moreButton()}) {
        const QRect r = f.rectOf(w);
        out << QStringLiteral("%1,%2 %3x%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
    }
    return out;
}

} // namespace

class StackedTest : public UiTestCase
{
    Q_OBJECT
private slots:
    // One pixel under the scaled 700 px the body stacks, at 700 it does not;
    // what it shows follows the preferences without writing any of them.
    void theWindowStacksBelowItsStackingWidth()
    {
        QSettings().remove(settings::kWindowLeftWidth);
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        settle();
        MainWindow *w = f.window.get();
        auto *diffPane = w->findChild<DiffPane *>();
        auto *history = w->findChild<HistoryView *>();
        const QStringList prefs = windowPrefs(); // after the constructor's own migration
        QVERIFY(!w->isStacked());

        w->resize(ui::space(700), 800);
        settle();
        QVERIFY(!w->isStacked());
        QVERIFY(!f.bar()->isStacked());
        w->resize(ui::space(700) - 1, 800);
        settle();
        QVERIFY(w->isStacked());
        QVERIFY(f.bar()->isStacked());
        QVERIFY(f.page()->isStacked());

        // Docked, Commit: the Changes tab, the page filling the body.
        QCOMPARE(f.bar()->currentTab(), TopBar::Tab::Changes);
        QVERIFY(f.page()->isVisible());
        QVERIFY(!f.rail()->isVisible());
        QVERIFY(!diffPane->isVisible());
        QCOMPARE(w->paneLayout(), PaneLayout::Docked);
        QVERIFY(w->diffPaneVisible());
        // The toggles still say what the preferences are; the bar hides them.
        QVERIFY(!f.bar()->layoutButton()->isVisible());
        QVERIFY(!f.bar()->layoutButton()->isChecked());
        QVERIFY(f.bar()->diffToggle()->isChecked());
        QCOMPARE(windowPrefs(), prefs);

        // The preference setters, stacked: Mini is the Diff tab, Docked the
        // page; hiding the diff leaves both Diff and Mini; showing it again
        // leaves the tab alone.
        const auto diffShown = [&] {
            return w->diffTab() && f.rail()->isVisible() && diffPane->isVisible() && !f.page()->isVisible()
                && f.bar()->currentTab() == TopBar::Tab::Diff;
        };
        w->setPaneLayout(PaneLayout::Mini, false);
        settle();
        QVERIFY(diffShown());
        w->setDiffPaneVisible(true, false);
        QVERIFY(diffShown());
        w->setPaneLayout(PaneLayout::Docked, false);
        settle();
        QVERIFY(!w->diffTab());
        QVERIFY(f.page()->isVisible());
        QCOMPARE(f.bar()->currentTab(), TopBar::Tab::Changes);
        w->setPaneLayout(PaneLayout::Mini, false);
        w->setDiffPaneVisible(false, false);
        settle();
        QVERIFY(!w->diffTab());
        QCOMPARE(w->paneLayout(), PaneLayout::Docked); // the existing coupling
        QVERIFY(!w->diffPaneVisible());
        w->setDiffPaneVisible(true, false);
        QVERIFY(!w->diffTab());
        QVERIFY(f.page()->isVisible());
        QCOMPARE(windowPrefs(), prefs);
        // ...and the persisting ones still save, as they always did.
        w->setPaneLayout(PaneLayout::Mini);
        QCOMPARE(QSettings().value(settings::kWindowLayout).toString(), QStringLiteral("mini"));
        QVERIFY(w->diffTab());
        w->setPaneLayout(PaneLayout::Docked);
        QCOMPARE(QSettings().value(settings::kWindowLayout).toString(), QStringLiteral("docked"));
        QCOMPARE(windowPrefs(), prefs);

        // Out of it: Docked beside the diff, at the remembered left width...
        QSettings().setValue(settings::kWindowLeftWidth, 300);
        unstack(f);
        QVERIFY(!w->isStacked());
        QVERIFY(f.page()->isVisible());
        QVERIFY(diffPane->isVisible());
        QVERIFY(!f.rail()->isVisible());
        QVERIFY(f.bar()->layoutButton()->isVisible());
        QTRY_COMPARE(bodySplitter(f)->sizes().first(), 300);
        // ...or at the design's width for the window's width class with none,
        // and the stacked page's width is not what gets remembered.
        stack(f);
        QSettings().remove(settings::kWindowLeftWidth);
        unstack(f);
        QTRY_COMPARE(bodySplitter(f)->sizes().first(), designLeftWidth(w));
        QVERIFY(!QSettings().contains(settings::kWindowLeftWidth));

        // Mini and the history: the Diff tab, the rail on the commit's files.
        w->setPaneLayout(PaneLayout::Mini, false);
        w->setMode(MainWindow::HistoryMode);
        settle();
        stack(f);
        QVERIFY(w->diffTab());
        QCOMPARE(f.bar()->currentTab(), TopBar::Tab::Diff);
        QCOMPARE(w->mode(), MainWindow::HistoryMode);
        QVERIFY(f.rail()->isVisible());
        QCOMPARE(f.rail()->list()->model(), history->filesTable()->model());
        // ...and out of it, Mini again.
        unstack(f);
        QVERIFY(f.rail()->isVisible());
        QVERIFY(!f.page()->isVisible() && !history->isVisible());
        QVERIFY(diffPane->isVisible());
        QCOMPARE(w->paneLayout(), PaneLayout::Mini);
        QCOMPARE(f.bar()->currentTab(), TopBar::Tab::History);
        QCOMPARE(windowPrefs(), prefs);
    }

    // The first classification comes with the first show, after everything
    // main() applies: the flags pick the tab.
    void theStackedWindowStartsOnItsPreferredTab()
    {
        const QSize narrow(627, 612);
        QVERIFY(narrow.width() < ui::space(700));
        {
            // --mini --screenshot-size 627x612
            WindowFixture f = mainWindow(0, true, [narrow](MainWindow *w) { w->resize(narrow); });
            QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
            settle();
            QVERIFY(f.window->isStacked());
            QCOMPARE(f.bar()->currentTab(), TopBar::Tab::Diff);
            QVERIFY(f.rail()->isVisible());
        }
        {
            // Docked at the same size.
            WindowFixture f = mainWindow(0, false, [narrow](MainWindow *w) { w->resize(narrow); });
            QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
            settle();
            QCOMPARE(f.bar()->currentTab(), TopBar::Tab::Changes);
            QVERIFY(f.page()->isVisible());
        }
        {
            // --history
            WindowFixture f = mainWindow(0, false, [narrow](MainWindow *w) {
                w->setMode(MainWindow::HistoryMode);
                w->resize(narrow);
            });
            QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
            settle();
            QCOMPARE(f.bar()->currentTab(), TopBar::Tab::History);
            QVERIFY(f.window->findChild<HistoryView *>()->isVisible());
        }
        QByteArray geometry;
        {
            // --full: the hidden diff is still the preference once it widens.
            WindowFixture f = mainWindow(0, false, [narrow](MainWindow *w) {
                w->setDiffPaneVisible(false, false);
                w->resize(narrow);
            });
            QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
            settle();
            QCOMPARE(f.bar()->currentTab(), TopBar::Tab::Changes);
            geometry = f.window->saveGeometry();
            unstack(f);
            QVERIFY(!f.window->diffPaneVisible());
            QVERIFY(!f.window->findChild<DiffPane *>()->isVisible());
            QVERIFY(f.page()->isVisible());
        }
        {
            // A narrow geometry restored with a saved Mini layout.
            QSettings().setValue(settings::kWindowGeometry, geometry);
            QSettings().setValue(settings::kWindowLayout, QStringLiteral("mini"));
            WindowFixture f = mainWindow(0, false, [&geometry](MainWindow *w) { w->restoreGeometry(geometry); });
            QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
            settle();
            QVERIFY(f.window->width() < ui::space(700));
            QVERIFY(f.window->isStacked());
            QCOMPARE(f.bar()->currentTab(), TopBar::Tab::Diff);
            QVERIFY(f.rail()->isVisible());
        }
        QSettings().remove(settings::kWindowGeometry);
        QSettings().setValue(settings::kWindowLayout, QStringLiteral("docked"));
    }

    // Stacked, the diff is unified, as the design's Diff tab shows it, from
    // the first frame of a window shown narrow. The saved split or unified
    // choice is the wide window's and comes back as it widens, neither switch
    // writing it; Ctrl+T while stacked switches for as long as that lasts and
    // saves nothing.
    void theStackedDiffIsUnified()
    {
        const QSize narrow(627, 612);
        QVERIFY(narrow.width() < ui::space(700));
        QSettings().setValue(settings::kDiffTwoPane, true);
        WindowFixture f = mainWindow(0, true, [narrow](MainWindow *w) { w->resize(narrow); });
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        settle();
        auto *pane = f.window->findChild<DiffPane *>();
        DiffView *view = pane->view();
        QVERIFY(f.window->isStacked());
        QCOMPARE(f.bar()->currentTab(), TopBar::Tab::Diff);
        QCOMPARE(view->mode(), DiffView::OnePane);
        QVERIFY(QSettings().value(settings::kDiffTwoPane).toBool());

        // Widening writes nothing either: the window goes back to the split
        // it had, and the unified view another window saved meanwhile stays.
        QSettings().setValue(settings::kDiffTwoPane, false);
        unstack(f);
        QCOMPARE(view->mode(), DiffView::TwoPane);
        QVERIFY(!QSettings().value(settings::kDiffTwoPane).toBool());
        QSettings().setValue(settings::kDiffTwoPane, true);
        stack(f);
        QCOMPARE(view->mode(), DiffView::OnePane);

        pane->togglePaneMode(); // Ctrl+T
        QCOMPARE(view->mode(), DiffView::TwoPane);
        pane->togglePaneMode();
        QCOMPARE(view->mode(), DiffView::OnePane);
        pane->togglePaneMode();
        QVERIFY(QSettings().value(settings::kDiffTwoPane).toBool());
        unstack(f);
        QCOMPARE(view->mode(), DiffView::TwoPane);
        stack(f);
        QCOMPARE(view->mode(), DiffView::OnePane); // the split of the last stacked spell is gone

        // The wide window's choice is saved as ever, and is the one it widens to.
        unstack(f);
        pane->togglePaneMode();
        QCOMPARE(view->mode(), DiffView::OnePane);
        QVERIFY(!QSettings().value(settings::kDiffTwoPane).toBool());
        stack(f);
        QCOMPARE(view->mode(), DiffView::OnePane);
        pane->togglePaneMode();
        QCOMPARE(view->mode(), DiffView::TwoPane);
        unstack(f);
        QCOMPARE(view->mode(), DiffView::OnePane);
        QVERIFY(!QSettings().value(settings::kDiffTwoPane).toBool());
        QSettings().remove(settings::kDiffTwoPane);
    }

    // The tabs, Ctrl+1 / Ctrl+2 and the two layout keys move between the
    // presentations; only a real change of mode reads anything again.
    void theStackedTabsAndKeysMoveBetweenPresentations()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(activate(f.window.get()));
        settle();
        MainWindow *w = f.window.get();
        TopBar *bar = f.bar();
        auto *history = w->findChild<HistoryView *>();
        stack(f);
        const QStringList prefs = windowPrefs();

        QTest::mouseClick(bar->diffTab(), Qt::LeftButton);
        settle();
        QVERIFY(w->diffTab());
        QCOMPARE(w->mode(), MainWindow::CommitMode);
        QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget *>(f.rail()->list()));
        QTest::mouseClick(bar->historyTab(), Qt::LeftButton);
        settle();
        QCOMPARE(w->mode(), MainWindow::HistoryMode);
        QVERIFY(!w->diffTab());
        QVERIFY(history->isVisible());
        // The stacked history has no files table, so the keyboard goes to the
        // commit list.
        QVERIFY(history->filesTable()->isHidden());
        QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget *>(history->commitsTable()));
        QTest::keyClick(w, Qt::Key_1, Qt::ControlModifier);
        settle();
        QCOMPARE(w->mode(), MainWindow::CommitMode);
        QCOMPARE(bar->currentTab(), TopBar::Tab::Changes);
        QTest::keyClick(w, Qt::Key_2, Qt::ControlModifier);
        settle();
        QCOMPARE(bar->currentTab(), TopBar::Tab::History);
        QTest::keyClick(w, Qt::Key_1, Qt::ControlModifier);
        settle();
        QCOMPARE(bar->currentTab(), TopBar::Tab::Changes);

        // Both layout keys are the Diff tab here, and save nothing.
        for (const auto modifiers : {Qt::KeyboardModifiers(Qt::ControlModifier), Qt::ControlModifier | Qt::ShiftModifier}) {
            QTest::keyClick(w, Qt::Key_B, modifiers);
            settle();
            QCOMPARE(bar->currentTab(), TopBar::Tab::Diff);
            QVERIFY(f.rail()->isVisible());
            QTest::keyClick(w, Qt::Key_B, modifiers);
            settle();
            QCOMPARE(bar->currentTab(), TopBar::Tab::Changes);
            QTRY_VERIFY(QApplication::focusWidget() && QApplication::focusWidget()->isVisible());
        }
        QCOMPARE(w->paneLayout(), PaneLayout::Docked);
        QVERIFY(w->diffPaneVisible());
        QCOMPARE(windowPrefs(), prefs);

        // The tab of the mode already on reads nothing: the diff on screen is
        // the one read before the file changed. A real change of mode reads it.
        const auto diffText = [&f] {
            QStringList lines;
            for (const DiffLine &l : f.diff()->document().lines)
                lines << l.text;
            return lines.join(QLatin1Char('\n'));
        };
        QVERIFY(diffText().contains(QLatin1String("a changed")));
        QVERIFY(writeFixture(QDir(f.dir->path()).filePath(QStringLiteral("a.txt")), "a changed again\n"));
        QTest::mouseClick(bar->changesTab(), Qt::LeftButton);
        QTest::keyClick(w, Qt::Key_1, Qt::ControlModifier);
        QVERIFY(!diffText().contains(QLatin1String("again")));
        QTest::keyClick(w, Qt::Key_2, Qt::ControlModifier);
        QTest::keyClick(w, Qt::Key_1, Qt::ControlModifier);
        QVERIFY(diffText().contains(QLatin1String("again")));

        // Outside the stacked width there is no Diff tab to show.
        unstack(f);
        w->setDiffTab(true);
        QVERIFY(!w->diffTab());
        QVERIFY(!f.rail()->isVisible());

        // A double-click on a file shows its diff: the Diff tab, from the
        // changes list; and from the history's details card, whose files
        // button stands in for the files table the stacked history has no
        // room for. (The double-click is taken at the signal: the synthetic
        // events QtTest sends never reach an item view's own double-click
        // handling.)
        stack(f);
        f.page()->setFilesView(CommitPage::FilesView::Table, false);
        QTableView *table = f.page()->table();
        QVERIFY(table->isVisible());
        QMetaObject::invokeMethod(table, "doubleClicked", Q_ARG(QModelIndex, table->model()->index(0, ChangesModel::Name)));
        settle();
        QVERIFY(w->diffTab());
        QCOMPARE(w->mode(), MainWindow::CommitMode);
        QTest::mouseClick(bar->historyTab(), Qt::LeftButton);
        settle();
        QVERIFY(history->filesTable()->model()->rowCount() > 0);
        QToolButton *files = history->details()->filesButton();
        QVERIFY(files->isVisible());
        QTest::mouseClick(files, Qt::LeftButton);
        settle();
        QVERIFY(w->diffTab());
        QCOMPARE(w->mode(), MainWindow::HistoryMode);
        QCOMPARE(bar->currentTab(), TopBar::Tab::Diff);
        // The rail lists the commit's files, the history's own current one
        // among them.
        QCOMPARE(f.rail()->list()->model(), history->filesTable()->model());
        QCOMPARE(windowPrefs(), prefs);
    }

    // The Diff tab has the rail's commit tile and so the commit card; the
    // Changes tab commits from the page; the history has no card.
    void theStackedDiffTabCarriesTheCommitCard()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(activate(f.window.get()));
        settle();
        MainWindow *w = f.window.get();
        CommitPopover *card = f.popover();
        AgentPopover *agent = f.agentCard();
        stack(f);

        // Changes: Ctrl+Return presses the page's Commit, no card.
        const QString head = f.head();
        MessageEdit *editor = f.pageEditor();
        editor->setPlainText(QStringLiteral("Stacked commit"));
        editor->setFocus();
        QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget *>(editor));
        QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier);
        settle();
        QVERIFY(!card->isVisible());
        QVERIFY(f.head() != head);

        // Diff: the tile and Ctrl+Return open the card.
        QTest::mouseClick(f.bar()->diffTab(), Qt::LeftButton);
        settle();
        QVERIFY(f.tile()->isVisible());
        QTest::mouseClick(f.tile(), Qt::LeftButton);
        settle();
        QVERIFY(card->isVisible());
        QTest::mouseClick(f.tile(), Qt::LeftButton);
        settle();
        QVERIFY(!card->isVisible());
        QTest::keyClick(w, Qt::Key_Return, Qt::ControlModifier);
        settle();
        QVERIFY(card->isVisible());

        // The agent card hangs from the card's cog there; leaving Diff closes
        // both, and the keyboard lands on something on screen.
        QMetaObject::invokeMethod(w, "showAgentMenu");
        settle();
        QVERIFY(agent->isVisible());
        QCOMPARE(agent->anchor(), static_cast<QWidget *>(card->agentButton()));
        QTest::mouseClick(f.bar()->changesTab(), Qt::LeftButton);
        settle();
        QVERIFY(!card->isVisible());
        QVERIFY(!agent->isVisible());
        QTRY_VERIFY(QApplication::focusWidget() && QApplication::focusWidget()->isVisible());
        QVERIFY(QApplication::focusWidget() != f.rail()->list());
        // On the page, the page's cog.
        QMetaObject::invokeMethod(w, "showAgentMenu");
        settle();
        QVERIFY(agent->isVisible());
        QCOMPARE(agent->anchor(), static_cast<QWidget *>(f.page()->agentButton()));
        QVERIFY(!card->isVisible());
        agent->dismiss();

        // Mini, set without saving, then the slot: the card, narrow or not.
        w->setPaneLayout(PaneLayout::Mini, false);
        QMetaObject::invokeMethod(w, "showCommitPopover");
        settle();
        QVERIFY(card->isVisible());
        QVERIFY(w->diffTab());
        // Widening closes both cards.
        QMetaObject::invokeMethod(w, "showAgentMenu");
        settle();
        QVERIFY(agent->isVisible());
        unstack(f);
        QVERIFY(!card->isVisible());
        QVERIFY(!agent->isVisible());

        // The history has no card, on the Diff tab or anywhere.
        stack(f);
        w->setMode(MainWindow::HistoryMode);
        settle();
        QVERIFY(w->diffTab());
        QMetaObject::invokeMethod(w, "showCommitPopover");
        QTest::keyClick(w, Qt::Key_Return, Qt::ControlModifier);
        settle();
        QVERIFY(!card->isVisible());
        w->setPaneLayout(PaneLayout::Docked, false);
    }

    // A menu that would run off the window opens where it stays inside it:
    // the options menu above the action bar at the bottom, the sync menu
    // pulled left of the window's right edge.
    void theStackedMenusStayInsideTheWindow()
    {
        WindowFixture f = mainWindow(0, false, [](MainWindow *w) { w->resize(627, 612); });
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        settle();
        QVERIFY(f.window->isStacked());
        const QRect window(f.window->mapToGlobal(QPoint(0, 0)), f.window->size());
        const auto global = [](const QWidget *w) { return QRect(w->mapToGlobal(QPoint(0, 0)), w->size()); };

        QToolButton *options = f.page()->optionsButton();
        QVERIFY(options->isVisible());
        QMenu *menu = options->menu();
        menu->popup(options->mapToGlobal(QPoint(0, options->height())));
        QTRY_VERIFY(menu->isVisible());
        QCOMPARE(QApplication::activePopupWidget(), menu);
        QVERIFY2(menu->geometry().bottom() < global(options).top(),
                 qPrintable(QStringLiteral("menu %1..%2, button top %3")
                                .arg(menu->geometry().top()).arg(menu->geometry().bottom())
                                .arg(global(options).top())));
        QVERIFY(menu->geometry().left() >= window.left());
        menu->close();
        QTRY_VERIFY(!menu->isVisible());

        QToolButton *sync = f.bar()->syncDropdown();
        QVERIFY(sync->isVisible());
        menu = sync->menu();
        menu->popup(sync->mapToGlobal(QPoint(0, sync->height())));
        QTRY_VERIFY(menu->isVisible());
        QCOMPARE(QApplication::activePopupWidget(), menu);
        QVERIFY2(menu->geometry().right() <= window.right(),
                 qPrintable(QStringLiteral("menu right %1, window right %2")
                                .arg(menu->geometry().right()).arg(window.right())));
        menu->close();
        QTRY_VERIFY(!menu->isVisible());
    }

    // The options menu opens 4 over its button (screens.js: the OptionsMenu
    // card) even where its one entry would fit under it: a window with its
    // footer, at the design's text size, on a screen with the room.
    void theOptionsMenuOpensOverItsButton()
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

        WindowFixture f = mainWindow(0, false, [](MainWindow *w) { w->resize(470, 612); });
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        settle();
        QVERIFY(f.window->isStacked());
        auto *footer = f.window->findChild<Footer *>();
        QVERIFY(footer && footer->isVisible());
        QToolButton *options = f.page()->optionsButton();
        QVERIFY(options->isVisible());
        QMenu *menu = options->menu();
        QTimer::singleShot(0, menu, [menu] { menu->close(); });
        options->showMenu();
        QCOMPARE(menu->actions().size(), 1);
        const QRect button(options->mapToGlobal(QPoint(0, 0)), options->size());
        const QRect window(f.window->mapToGlobal(QPoint(0, 0)), f.window->size());
        // Under the button it would have had the room, in the window and on
        // the screen alike.
        const int under = button.y() + button.height() + menu->height();
        QVERIFY(under <= window.y() + window.height());
        QVERIFY(under <= options->screen()->availableGeometry().y() + options->screen()->availableGeometry().height());
        QCOMPARE(menu->y() + menu->height() + ui::space(ui::gap::cluster), button.y());
    }

    // Stacked, the history's All branches is the design's 28 px square, its
    // name in the tooltip; the ordinary width spells it out again.
    void theStackedHistoryFoldsAllBranchesIntoItsGlyph()
    {
        WindowFixture f = mainWindow(0, false, [](MainWindow *w) { w->resize(ui::space(1200), ui::space(700)); });
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        MainWindow *w = f.window.get();
        w->setMode(MainWindow::HistoryMode);
        settle();
        auto *history = w->findChild<HistoryView *>();
        QToolButton *all = nullptr;
        for (QToolButton *b : history->findChildren<QToolButton *>())
            if (b->accessibleName() == QLatin1String("All branches"))
                all = b;
        QVERIFY(all);
        QVERIFY(all->text().endsWith(QStringLiteral("All branches")));
        QCOMPARE(all->height(), ui::space(28));
        const int labelled = all->width();
        QVERIFY(labelled > ui::space(28));
        stack(f);
        QVERIFY(w->isStacked());
        QCOMPARE(all->text(), ui::icon(ui::kBranch, QStringLiteral("B")).trimmed());
        QCOMPARE(all->size(), QSize(ui::space(28), ui::space(28)));
        QCOMPARE(all->toolTip(), QStringLiteral("All branches"));
        all->click();
        QVERIFY(all->isChecked()); // the square keeps the checked state
        all->click();
        unstack(f);
        QVERIFY(all->text().endsWith(QStringLiteral("All branches")));
        QCOMPARE(all->width(), labelled);
    }

    // Stacked, the card's files button leads to the commit's files: the Diff
    // tab, whose rail lists them, the history's current file among them.
    void theStackedDetailsCardShowsTheCommitsFiles()
    {
        WindowFixture f = mainWindow(0, false, [](MainWindow *w) { w->resize(ui::space(627), ui::space(612)); });
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        settle();
        MainWindow *w = f.window.get();
        TopBar *bar = f.bar();
        QVERIFY(w->isStacked());
        QTest::mouseClick(bar->historyTab(), Qt::LeftButton);
        settle();
        QCOMPARE(w->mode(), MainWindow::HistoryMode);
        QVERIFY(!w->diffTab());
        auto *history = w->findChild<HistoryView *>();
        QVERIFY(history->filesTable()->isHidden());
        QToolButton *files = history->details()->filesButton();
        QVERIFY(files->isVisible());
        QCOMPARE(files->text(), QStringLiteral("1 file ›"));

        QTest::mouseClick(files, Qt::LeftButton);
        settle();
        QVERIFY(w->diffTab());
        QCOMPARE(w->mode(), MainWindow::HistoryMode);
        QCOMPARE(bar->currentTab(), TopBar::Tab::Diff);
        QVERIFY(f.rail()->isVisible());
        QCOMPARE(f.rail()->list()->model(), history->filesTable()->model());
        QCOMPARE(f.rail()->list()->model()->rowCount(), 1);
        Commit c;
        FileChange file;
        QVERIFY(history->currentFile(&c, &file));
        QCOMPARE(file.path, QStringLiteral("a.txt"));
        QCOMPARE(f.rail()->list()->currentIndex().data(ChangesModel::PathRole).toString(), file.path);
    }

    // Options at the left, an item gap (8), and Commit through the rest;
    // Amend goes into the menu. The ordinary row comes back exactly.
    void theStackedActionBarFoldsAmendIntoOptions()
    {
        CommitFixture f = commitFixture();
        QVERIFY(f.page);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        CommitPage *page = f.page.get();
        QPushButton *commit = f.commitButton();
        QCheckBox *amend = f.amend();
        QToolButton *options = page->optionsButton();
        const QRect commitBefore = rectIn(commit, page), amendBefore = rectIn(amend, page);
        const QString amendLabel = amend->text();
        QVERIFY(!options->isVisible());
        QCOMPARE(options->accessibleName(), QStringLiteral("Options"));
        QCOMPARE(options->popupMode(), QToolButton::InstantPopup);
        QVERIFY(qobject_cast<TickMenu *>(options->menu()));
        QCOMPARE(options->text(), ui::icon(ui::kDotsHorizontal, QStringLiteral("…")).trimmed());

        const QString key = QStringLiteral("  ⏎");
        const auto wording = [&](const QString &what, bool stacked) {
            QCOMPARE(commit->text(), ui::icon(ui::kCommit) + what + (stacked ? QString() : key));
            QCOMPARE(commit->accessibleName(), what);
            QCOMPARE(page->commitControls().commitText, ui::icon(ui::kCommit) + what + key);
            QCOMPARE(page->commitControls().commitName, what);
        };
        wording(QStringLiteral("Commit 2 files"), false);

        page->setStacked(true);
        settle();
        QVERIFY(options->isVisible());
        QVERIFY(!amend->isVisible());
        const QRect o = rectIn(options, page), c = rectIn(commit, page);
        QCOMPARE(o.x(), 0);
        QCOMPARE(o.width(), ui::space(ui::box::control));
        QCOMPARE(c.x() - (o.x() + o.width()), ui::space(ui::gap::item));
        QCOMPARE(c.x() + c.width(), page->width());
        QVERIFY(c.width() > commitBefore.width());
        wording(QStringLiteral("Commit 2 files"), true);
        page->toggleAllChecked(); // 2 of 4 checked: now all four
        wording(QStringLiteral("Commit 4 files"), true);
        page->setAmendChecked(true);
        wording(QStringLiteral("Amend"), true);
        page->setAmendChecked(false);
        MergeState merge;
        merge.inProgress = true;
        merge.source = QStringLiteral("feature");
        page->setMergeState(merge, f.repo->headCommit());
        wording(QStringLiteral("Commit merge"), true);
        page->setMergeState(MergeState(), f.repo->headCommit());
        page->toggleAllChecked(); // all checked: none
        wording(QStringLiteral("Commit"), true);
        page->toggleAllChecked();

        page->setStacked(false);
        settle();
        QVERIFY(!options->isVisible());
        QVERIFY(amend->isVisible());
        wording(QStringLiteral("Commit 4 files"), false);
        page->setMergeState(merge, f.repo->headCommit());
        wording(QStringLiteral("Commit merge"), false);
        page->setMergeState(MergeState(), f.repo->headCommit());
        // The ordinary row, exactly as it was (the count back at two).
        const QStringList paths = f.checkedPaths();
        f.model()->setPathsChecked(QStringList({QStringLiteral("u1.txt"), QStringLiteral("u2.txt")}), false);
        settle();
        wording(QStringLiteral("Commit 2 files"), false);
        QVERIFY(paths.size() == 4);
        QCOMPARE(rectIn(commit, page), commitBefore);
        QCOMPARE(rectIn(amend, page), amendBefore);
        QCOMPARE(amend->text(), amendLabel);
    }

    // Stacking, the Diff tab, the page again and unstacking are presentation
    // only: the current file, the models, the list's offset and the place in
    // the diff all stay; so does a refresh on the Diff tab. In History the
    // current commit and the commit list's offset stay too.
    void stackingKeepsTheSelectionTheScrollAndTheDiff_data()
    {
        QTest::addColumn<bool>("inHistory");
        QTest::newRow("commit") << false;
        QTest::newRow("history") << true;
    }

    void stackingKeepsTheSelectionTheScrollAndTheDiff()
    {
        QFETCH(bool, inHistory);
        QSettings().remove(settings::kWindowFilesView);
        WindowFixture f = mainWindow(30);
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        settle();
        MainWindow *w = f.window.get();
        auto *history = w->findChild<HistoryView *>();
        // A long file, committed and then changed all through: both the
        // commit's diff and the working tree's have somewhere to scroll to.
        const QString path = f.dir->path();
        QByteArray before, after;
        for (int i = 0; i < 400; ++i) {
            before += QByteArray("line ") + QByteArray::number(i) + '\n';
            after += QByteArray(i % 3 ? "line " : "changed ") + QByteArray::number(i) + '\n';
        }
        QVERIFY(writeFixture(QDir(path).filePath(QStringLiteral("long.txt")), before));
        if (inHistory) {
            // Enough commits for the commit list to scroll, and the fixture's
            // thirty extra files in the long commit for its files list to.
            for (int i = 0; i < 40; ++i)
                QVERIFY(commit(path, QStringLiteral("filler %1").arg(i), 1));
            QVERIFY(git(path, {"add", "long.txt", "f*.txt"}));
        } else {
            QVERIFY(git(path, {"add", "long.txt"}));
        }
        QVERIFY(git(path, {"commit", "-q", "-m", "long"}, 2));
        QVERIFY(writeFixture(QDir(path).filePath(QStringLiteral("long.txt")), after));
        w->refresh();
        settle();

        QAbstractItemView *list = nullptr;
        QTableView *commits = nullptr;
        if (inHistory) {
            w->setMode(MainWindow::HistoryMode);
            settle();
            for (QTableView *t : history->findChildren<QTableView *>())
                if (t != history->filesTable())
                    commits = t;
            QVERIFY(commits);
            list = history->filesTable();
            // long.txt among the long commit's thirty-one files.
            const int files = list->model()->rowCount();
            QCOMPARE(files, 31);
            for (int row = 0; row < files; ++row) {
                history->filesTable()->selectRow(row);
                Commit c;
                FileChange file;
                if (history->currentFile(&c, &file) && file.path == QLatin1String("long.txt"))
                    break;
            }
        } else {
            QVERIFY(f.page()->selectPath(QStringLiteral("long.txt")));
            list = f.page()->activeListView();
        }
        settle();
        const auto currentPath = [&] {
            if (!inHistory) {
                bool ok = false;
                const FileChange c = f.page()->currentChange(&ok);
                return ok ? c.path : QString();
            }
            Commit c;
            FileChange file;
            return history->currentFile(&c, &file) ? file.path : QString();
        };
        QCOMPARE(currentPath(), QStringLiteral("long.txt"));
        const auto currentHash = [&] {
            bool ok = false;
            const Commit c = history->currentCommit(&ok);
            return ok ? c.hash : QString();
        };
        if (!inHistory) {
            f.page()->setScrollOffset(QPoint(0, 3));
        } else {
            QCOMPARE(currentHash(), f.head());
            // Both lists off their top, by little enough that neither runs
            // out of range whatever the resizes do to the viewports.
            for (QAbstractItemView *view : QList<QAbstractItemView *>{commits, list}) {
                QScrollBar *bar = view->verticalScrollBar();
                QVERIFY(bar->maximum() >= 2);
                bar->setValue(2);
            }
            settle();
        }
        DiffView::ViewState at;
        at.row = 120;
        at.block = 1;
        f.diff()->restoreViewState(at);
        QCOMPARE(f.diff()->viewState().row, 120);
        // The working tree's long.txt, every third line changed: the split
        // view's row 120 is a later line. (The commit's adds it whole.)
        if (!inHistory)
            QVERIFY(f.diff()->topLine() > 120);
        // The place in the diff by its top line: stacked, the diff is
        // unified, where the rows are others.
        const auto state = [&] {
            const DiffView::ViewState d = f.diff()->viewState();
            QStringList out{currentPath(), QString::number(list->verticalScrollBar()->value()),
                            QStringLiteral("%1,%2,%3").arg(f.diff()->topLine()).arg(d.column).arg(d.block)};
            if (inHistory)
                out << currentHash() << QString::number(commits->verticalScrollBar()->value());
            return out;
        };
        const QStringList start = state();
        QCOMPARE(start.at(1), inHistory ? QStringLiteral("2") : QStringLiteral("3"));
        if (inHistory)
            QCOMPARE(start.at(4), QStringLiteral("2"));
        QSignalSpy listResets(list->model(), &QAbstractItemModel::modelReset);
        QSignalSpy railResets(f.rail()->list()->model(), &QAbstractItemModel::modelReset);
        // The commit list's own model: the files' spies leave it unwatched.
        std::unique_ptr<QSignalSpy> commitResets;
        if (inHistory)
            commitResets.reset(new QSignalSpy(commits->model(), &QAbstractItemModel::modelReset));

        stack(f);
        QCOMPARE(state(), start);
        w->setDiffTab(true);
        settle();
        QCOMPARE(state(), start);
        w->setDiffTab(false);
        settle();
        QCOMPARE(state(), start);
        unstack(f);
        QCOMPARE(state(), start);
        QCOMPARE(f.diff()->viewState().row, 120);
        QCOMPARE(listResets.count(), 0);
        QCOMPARE(railResets.count(), 0);
        if (commitResets)
            QCOMPARE(commitResets->count(), 0);

        if (!inHistory) {
            // F5 on the Diff tab: the file, the rail's offset and the place
            // in the diff are put back.
            stack(f);
            w->setDiffTab(true);
            settle();
            QScrollBar *rail = f.rail()->list()->verticalScrollBar();
            QVERIFY(rail->maximum() > 0);
            rail->setValue(rail->maximum() / 2);
            const int railOffset = rail->value();
            QTest::keyClick(w, Qt::Key_F5);
            settle();
            QCOMPARE(state().at(0), start.at(0));
            QCOMPARE(state().at(2), start.at(2));
            QCOMPARE(rail->value(), railOffset);
            QVERIFY(w->diffTab());
        }
    }

    // A live text size moves the stacked row like a fresh one, and moves the
    // stacking width with it: 800 px is narrow at 16 and wide at 12.
    void theStackedPresentationFollowsALiveTextSizeChange()
    {
        QTemporaryDir dir, home, tools;
        QVERIFY(dir.isValid() && home.isValid() && tools.isValid());
        const QString toml = QDir(dir.path()).filePath(QStringLiteral("shell.toml"));
        QVERIFY(writeFixture(toml, "[font]\nbase-size = 12\n"));
        const QString gitBinary = QStandardPaths::findExecutable(QStringLiteral("git"));
        QVERIFY(QFile::link(gitBinary, QDir(tools.path()).filePath(QStringLiteral("git"))));
        {
            ScopedEnv themeDir("OMAGIT_THEME_DIR", dir.path().toUtf8());
            ScopedEnv scratchHome("HOME", home.path().toUtf8());
            ScopedEnv onlyGit("PATH", tools.path().toUtf8());
            OmarchyTheme theme;
            QCOMPARE(theme.fontBase(), 12);
            theme.apply(*qApp);

            BarFixture live = topBar();
            live.bar->setStacked(true);
            QObject::connect(&theme, &OmarchyTheme::changed, live.bar, [bar = live.bar] { bar->applyTheme(); });
            QVERIFY(QTest::qWaitForWindowExposed(live.host.get()));
            settle();

            WindowFixture window = mainWindow(0, false, [](MainWindow *w) { w->resize(800, 800); });
            QVERIFY(window.window);
            QVERIFY(QTest::qWaitForWindowExposed(window.window.get()));
            settle();
            QVERIFY(!window.window->isStacked());
            const QStringList prefs = windowPrefs();

            const auto matchesAFreshBar = [&live] {
                BarFixture fresh = topBar();
                fresh.bar->setStacked(true);
                QVERIFY(QTest::qWaitForWindowExposed(fresh.host.get()));
                settle();
                // Both spelled out, so the segments' hints are the labelled ones.
                live.levelAt(live.bar->sizeHint().width());
                fresh.levelAt(fresh.bar->sizeHint().width());
                QCOMPARE(barMetrics(live.bar), barMetrics(fresh.bar));
                for (const int width : {fresh.bar->sizeHint().width(), fresh.bar->sizeHint().width() - 1,
                                        fresh.bar->minimumSizeHint().width()}) {
                    QCOMPARE(live.levelAt(width), fresh.levelAt(width));
                    QCOMPARE(stackedRow(live), stackedRow(fresh));
                    QVERIFY(live.bar->syncDropdown()->property("ghost").toBool());
                }
                live.levelAt(live.bar->sizeHint().width());
            };
            const QStringList atTwelve = barMetrics(live.bar);
            matchesAFreshBar();

            QVERIFY(writeFixture(toml, "[font]\nbase-size = 16\n"));
            QTRY_COMPARE_WITH_TIMEOUT(theme.fontBase(), 16, 10000);
            settle();
            matchesAFreshBar();
            QVERIFY(barMetrics(live.bar) != atTwelve);
            QVERIFY(ui::space(700) > 800);
            QTRY_VERIFY(window.window->isStacked());
            QCOMPARE(window.bar()->currentTab(), TopBar::Tab::Changes);
            QCOMPARE(windowPrefs(), prefs);

            QVERIFY(writeFixture(toml, "[font]\nbase-size = 12\n"));
            QTRY_COMPARE_WITH_TIMEOUT(theme.fontBase(), 12, 10000);
            settle();
            matchesAFreshBar();
            QCOMPARE(barMetrics(live.bar), atTwelve);
            QTRY_VERIFY(!window.window->isStacked());
            QVERIFY(window.window->findChild<DiffPane *>()->isVisible());
            QCOMPARE(windowPrefs(), prefs);

            window.window.reset();
            live.host.reset();
        }
        g_theme.reset(new OmarchyTheme);
        g_theme->apply(*qApp);
        QVERIFY(OmarchyTheme::instance() == g_theme.get());
    }
};

UI_TEST(StackedTest);

#include "stacked_test.moc"
