// The window's top bar: its fold levels in one row and stacked, the two names
// eliding evenly, the tabs, the sync buttons, dropdown and badges, and what
// its menus and accessible names say at every width.
#include "UiTestCase.h"
#include "fixtures.h"

#include "../../src/Segmented.h"
#include "../../src/TickMenu.h"
#include "../../src/UiHelpers.h"

#include <QApplication>
#include <QFontMetrics>
#include <QFontMetricsF>
#include <QLayout>
#include <QPainter>
#include <QSignalSpy>

namespace {

// The buttons of a row that are on screen, in the order they stand in.
QList<bool> visible(const QList<QToolButton *> &buttons)
{
    QList<bool> out;
    for (const QToolButton *b : buttons)
        out << b->isVisible();
    return out;
}

// Whether `shot` has a pixel of `colour` in the columns from `fromX` up to
// (not including) `toX`, all of them by default.
bool imagePaints(const QImage &shot, const QColor &colour, int fromX = 0, int toX = INT_MAX)
{
    const QRgb wanted = colour.rgb() | 0xff000000;
    for (int y = 0; y < shot.height(); ++y)
        for (int x = qMax(0, fromX); x < qMin(shot.width(), toX); ++x)
            if ((shot.pixel(x, y) | 0xff000000) == wanted)
                return true;
    return false;
}

// Whether `w` paints `colour` anywhere, read off its own pixels.
bool paints(QWidget *w, const QColor &colour)
{
    return imagePaints(w->grab().toImage(), colour);
}

// The columns `image` leaves bare before and after what it paints: every
// pixel of a bare column is the ground's, the top-left pixel's.
QPair<int, int> bareColumns(const QImage &image)
{
    const QRgb ground = image.pixel(0, 0);
    int first = -1, last = -1;
    for (int x = 0; x < image.width(); ++x) {
        for (int y = 0; y < image.height(); ++y) {
            if (image.pixel(x, y) != ground) {
                if (first < 0)
                    first = x;
                last = x;
                break;
            }
        }
    }
    return {first, image.width() - 1 - last};
}

} // namespace

class TopBarTest : public UiTestCase
{
    Q_OBJECT
private slots:
    // The row folds in six steps as the window narrows: the sync labels,
    // then the sync buttons, the tab labels between them, and the two names
    // last. The repository chip wears its name at every level.
    void theTopBarFoldsInSixStepsAsItNarrows()
    {
        BarFixture f = topBar();
        TopBar *bar = f.bar;
        QVERIFY(QTest::qWaitForWindowExposed(f.host.get()));
        settle();

        const int wide = bar->sizeHint().width();
        QCOMPARE(f.levelAt(wide + 200), 0);
        QCOMPARE(f.levelAt(wide), 0);     // the size hint is exactly what level 0 takes
        QVERIFY(f.levelAt(wide - 1) > 0); // one pixel under it, something folds

        // Every level shows up, in order, as the bar narrows...
        QList<int> seen;
        QHash<int, int> widest; // level -> the widest width it appears at
        for (int width = wide; width >= bar->minimumSizeHint().width(); --width) {
            const int level = f.levelAt(width);
            QVERIFY2(seen.isEmpty() || level >= seen.last(), "the bar unfolded while it narrowed");
            if (seen.isEmpty() || level != seen.last()) {
                seen << level;
                widest[level] = width;
            }
        }
        QCOMPARE(seen, QList<int>({0, 1, 2, 3, 4, 5}));
        // ...and one pixel wider than a level starts is the level before it.
        for (int level = 1; level <= 5; ++level)
            QCOMPARE(f.levelAt(widest.value(level) + 1), level - 1);

        // What each level shows. The repository chip and the two toggles are
        // there at every one of them.
        const QHash<int, QList<bool>> syncShown{
            {0, {true, true, true, true}},   {1, {true, true, true, true}},   {2, {true, true, false, false}},
            {3, {true, true, false, false}}, {4, {false, false, false, false}}, {5, {false, false, false, false}}};
        const QString fullRepo = ui::icon(ui::kFolderOpen) + QStringLiteral("omagit-workspace") + ui::chevron();
        QList<int> repoWidths, tabWidths, branchWidths;
        for (int level = 0; level <= 5; ++level) {
            QCOMPARE(f.levelAt(widest.value(level, wide)), level);
            QCOMPARE(visible(f.sync()), syncShown.value(level));
            QCOMPARE(bar->moreButton()->isVisible(), level >= 2);
            QVERIFY(bar->repoButton()->isVisible());
            QVERIFY(bar->layoutButton()->isVisible());
            QVERIFY(bar->diffToggle()->isVisible());
            // Only the widest level spells the sync buttons out.
            QCOMPARE(bar->pullButton()->text().contains(QLatin1String("Pull")), level == 0);
            // The repository's name whole, but where the last level elides it.
            if (level < 5)
                QCOMPARE(bar->repoButton()->text(), fullRepo);
            repoWidths << f.rectOf(bar->repoButton()).width();
            tabWidths << f.rectOf(f.tabs()).width();
            branchWidths << f.rectOf(bar->branchButton()).width();
        }
        for (int level = 1; level <= 4; ++level)
            QCOMPARE(repoWidths.at(level), repoWidths.at(0));
        QVERIFY(tabWidths.at(2) > tabWidths.at(3)); // the tab labels go at level 3
        QCOMPARE(tabWidths.at(4), tabWidths.at(3));
        QCOMPARE(tabWidths.at(5), tabWidths.at(3));
        QVERIFY(branchWidths.at(5) <= branchWidths.at(4)); // the last level is never the wider one
        QVERIFY(repoWidths.at(5) <= repoWidths.at(4));

        // At its narrowest both names are elided — and only they: the glyphs
        // and the chevrons stay where they were. Both names are longer than
        // the 72 px the level keeps of either, so each keeps exactly that.
        QCOMPARE(f.levelAt(bar->minimumSizeHint().width()), 5);
        const QString elided = bar->branchButton()->text();
        QVERIFY2(elided.contains(QChar(0x2026)), qPrintable(elided));
        QVERIFY(elided.startsWith(ui::icon(ui::kBranch, QStringLiteral("b"))));
        QVERIFY(elided.endsWith(ui::chevron()));
        const QString repo = bar->repoButton()->text();
        QVERIFY2(repo.contains(QChar(0x2026)), qPrintable(repo));
        QVERIFY(repo.startsWith(ui::icon(ui::kFolderOpen) + QStringLiteral("omagit")));
        QVERIFY(repo.endsWith(ui::chevron()));
        QCOMPARE(repoWidths.at(0) - f.rectOf(bar->repoButton()).width(),
                 qCeil(QFontMetricsF(bar->repoButton()->font()).horizontalAdvance(QStringLiteral("omagit-workspace")))
                     - ui::space(72));

        // And the room coming back spells everything out again.
        QCOMPARE(f.levelAt(wide), 0);
        QCOMPARE(bar->repoButton()->text(), fullRepo);
        QCOMPARE(bar->branchButton()->text(),
                 ui::icon(ui::kBranch) + QStringLiteral("feature/askpass-login-dialog") + ui::chevron());
    }

    // Where the last level has to elide, the two names share what the row
    // has left as evenly as their lengths let them: a short repository name
    // stays whole while the branch gives way, and two long names give way
    // together, the row ending exactly a group gap before the tabs.
    void theTopBarElidesBothNamesAsEvenlyAsTheyAllow()
    {
        const auto labelOf = [](const QToolButton *chip, uint glyph) {
            QString text = chip->text();
            return text.mid(ui::icon(glyph).size(), text.size() - ui::icon(glyph).size() - ui::chevron().size());
        };
        const auto advance = [](const QToolButton *chip, const QString &text) {
            return qCeil(QFontMetricsF(chip->font()).horizontalAdvance(text));
        };

        // A short repository name, a long branch: only the branch elides.
        BarFixture f = topBar(QStringLiteral("omagit"), QStringLiteral("feature/askpass-login-dialog"));
        TopBar *bar = f.bar;
        QVERIFY(QTest::qWaitForWindowExposed(f.host.get()));
        settle();
        const int eliding = f.widthForLevel(5);
        QVERIFY(eliding > 0);
        QCOMPARE(f.levelAt(eliding - 20), 5);
        QCOMPARE(labelOf(bar->repoButton(), ui::kFolderOpen), QStringLiteral("omagit"));
        QVERIFY(labelOf(bar->branchButton(), ui::kBranch).endsWith(QChar(0x2026)));
        QCOMPARE(f.rectOf(bar->branchButton()).right() + 1 + ui::space(ui::gap::group), f.rectOf(f.tabs()).x());

        // Two long names: each keeps as much of itself as the other, to a
        // pixel, and more than the 72 px floor.
        const QString repoName = QStringLiteral("omagit-workspace-with-a-long-name");
        const QString branchName = QStringLiteral("feature/askpass-login-dialog");
        BarFixture both = topBar(repoName, branchName);
        bar = both.bar;
        settle();
        // What each chip puts around its name, read off the whole chips.
        QCOMPARE(both.levelAt(bar->sizeHint().width()), 0);
        const int repoChrome = both.rectOf(bar->repoButton()).width() - advance(bar->repoButton(), repoName);
        const int branchChrome = both.rectOf(bar->branchButton()).width() - advance(bar->branchButton(), branchName);
        QCOMPARE(both.levelAt(bar->minimumSizeHint().width() + ui::space(40)), 5);
        const int repoRoom = both.rectOf(bar->repoButton()).width() - repoChrome;
        const int branchRoom = both.rectOf(bar->branchButton()).width() - branchChrome;
        QVERIFY2(qAbs(repoRoom - branchRoom) <= 1, qPrintable(QStringLiteral("%1 vs %2").arg(repoRoom).arg(branchRoom)));
        QVERIFY(repoRoom > ui::space(72));
        QVERIFY(labelOf(bar->repoButton(), ui::kFolderOpen).endsWith(QChar(0x2026)));
        QVERIFY(labelOf(bar->branchButton(), ui::kBranch).endsWith(QChar(0x2026)));
        QCOMPARE(both.rectOf(bar->branchButton()).right() + 1 + ui::space(ui::gap::group), both.rectOf(both.tabs()).x());
    }

    // The tabs follow the middle of the whole bar and stop a group gap (16)
    // clear of either group; the groups themselves stand against their own
    // edges, with the design's gaps inside them.
    void theTopBarCentresTheTabsBetweenItsGroups()
    {
        BarFixture f = topBar();
        TopBar *bar = f.bar;
        QVERIFY(QTest::qWaitForWindowExposed(f.host.get()));
        settle();

        const int wide = bar->sizeHint().width() + 400;
        QCOMPARE(f.levelAt(wide), 0);
        const QRect tabs = f.rectOf(f.tabs());
        QVERIFY2(qAbs(tabs.x() + tabs.width() / 2.0 - wide / 2.0) <= 1.0, "the tabs are not in the middle of the bar");

        // The left group against the left edge, the right group against the
        // right one, both inside the window's margin (the regular 12 of a bar
        // no window has given its density), with the design's gaps inside
        // them: the chips and the toggles a cluster apart, the sync buttons an
        // item apart, a group gap with the divider at its middle before the
        // toggles.
        const QRect repo = f.rectOf(bar->repoButton()), branch = f.rectOf(bar->branchButton());
        QCOMPARE(repo.x(), ui::space(12));
        QCOMPARE(branch.x() - (repo.x() + repo.width()), ui::space(ui::gap::cluster));
        QCOMPARE(f.rectOf(bar->diffToggle()).x() + bar->diffToggle()->width(), wide - ui::space(12));
        QCOMPARE(f.rectOf(bar->diffToggle()).x() - (f.rectOf(bar->layoutButton()).x() + bar->layoutButton()->width()),
                 ui::space(ui::gap::cluster));
        QCOMPARE(f.rectOf(bar->pushButton()).x() - (f.rectOf(bar->pullButton()).x() + bar->pullButton()->width()),
                 ui::space(ui::gap::item));
        const QRect merge = f.rectOf(bar->mergeButton());
        QCOMPARE(f.rectOf(bar->layoutButton()).x() - (merge.x() + merge.width()), ui::space(ui::gap::group));
        // The divider: a 16 px line at the start of the group gap's right half.
        QWidget *divider = nullptr;
        for (QWidget *w : bar->findChildren<QWidget *>())
            if (w->isVisible() && w->width() == 1 && w->height() == ui::space(ui::box::divider))
                divider = w;
        QVERIFY(divider);
        QCOMPARE(f.rectOf(divider).x(), merge.x() + merge.width() + ui::space(ui::gap::group / 2));

        // At the narrowest width the middle is taken, so the clamp decides:
        // the tabs sit a group gap off both groups at once.
        QCOMPARE(f.levelAt(bar->minimumSizeHint().width()), 5);
        const QRect tight = f.rectOf(f.tabs());
        QCOMPARE(tight.x(), f.rectOf(bar->branchButton()).x() + bar->branchButton()->width() + ui::space(ui::gap::group));
        QCOMPARE(tight.x() + tight.width() + ui::space(ui::gap::group), f.rectOf(bar->moreButton()).x());
    }

    // Short names are never elided, and the widths of the two chips do not
    // become a minimum the window has to honour.
    void theTopBarKeepsShortNamesWholeAtEveryWidth()
    {
        BarFixture f = topBar(QStringLiteral("omagit"), QStringLiteral("main"), 3);
        TopBar *bar = f.bar;
        QVERIFY(QTest::qWaitForWindowExposed(f.host.get()));
        settle();
        const QString canonical = ui::icon(ui::kBranch) + QStringLiteral("main") + ui::chevron();

        // Names this short are under the allowance the last level keeps, so
        // eliding would buy nothing and the level before it already fits.
        QCOMPARE(f.levelAt(bar->minimumSizeHint().width()), 4);
        QCOMPARE(bar->branchButton()->text(), canonical);
        QVERIFY(bar->minimumSizeHint().width() < bar->sizeHint().width());

        // Folding and unfolding again, several times over, leaves the
        // canonical text — never a shortened one shortened once more.
        for (int i = 0; i < 3; ++i) {
            f.levelAt(bar->minimumSizeHint().width());
            f.levelAt(bar->sizeHint().width());
        }
        QCOMPARE(bar->branchButton()->text(), canonical);
        QCOMPARE(bar->repoButton()->text(), ui::icon(ui::kFolderOpen) + QStringLiteral("omagit") + ui::chevron());

        // However long either name, the last level keeps 72 px of it at most,
        // so the minimum hardly moves.
        BarFixture longName = topBar(QStringLiteral("omagit"), QString(120, QLatin1Char('x')), 3);
        QVERIFY(longName.bar->minimumSizeHint().width() - bar->minimumSizeHint().width() <= ui::space(72));
        BarFixture longRepo = topBar(QString(120, QLatin1Char('x')), QStringLiteral("main"), 3);
        QVERIFY(longRepo.bar->minimumSizeHint().width() - bar->minimumSizeHint().width() <= ui::space(72));
    }

    // The two tabs are one exclusive switch: they ask the window for a mode
    // instead of changing it, carry the changes count and nothing else.
    void theTopBarTabsCarryTheCountAndAskForTheMode()
    {
        BarFixture f = topBar();
        TopBar *bar = f.bar;
        QVERIFY(QTest::qWaitForWindowExposed(f.host.get()));
        settle();

        QCOMPARE(bar->changesTab()->accessibleName(), QStringLiteral("Changes"));
        QCOMPARE(bar->historyTab()->accessibleName(), QStringLiteral("History"));
        QVERIFY(bar->changesTab()->toolTip().contains(QLatin1String("Ctrl+1")));
        QVERIFY(bar->historyTab()->toolTip().contains(QLatin1String("Ctrl+2")));

        // The pill: gone at nothing to commit, wider with more digits, and
        // never on the History tab.
        const int history = bar->historyTab()->sizeHint().width();
        const int seven = bar->changesTab()->sizeHint().width();
        bar->setChangesCount(0);
        const int none = bar->changesTab()->sizeHint().width();
        bar->setChangesCount(128);
        QCOMPARE(bar->changesCount(), 128);
        const int many = bar->changesTab()->sizeHint().width();
        QVERIFY(none < seven);
        QVERIFY(seven < many);
        QCOMPARE(bar->historyTab()->sizeHint().width(), history);
        bar->setChangesCount(-4); // no such thing as a negative count
        QCOMPARE(bar->changesCount(), 0);

        QSignalSpy requests(bar, &TopBar::tabRequested);
        QTest::mouseClick(bar->historyTab(), Qt::LeftButton);
        QCOMPARE(requests.count(), 1);
        QCOMPARE(requests.at(0).at(0).value<TopBar::Tab>(), TopBar::Tab::History);
        QVERIFY(bar->historyTab()->isChecked());
        QVERIFY(!bar->changesTab()->isChecked());
        // The window putting the state back asks for nothing.
        bar->setCurrentTab(TopBar::Tab::Changes);
        QVERIFY(bar->changesTab()->isChecked());
        QVERIFY(!bar->historyTab()->isChecked());
        QCOMPARE(bar->currentTab(), TopBar::Tab::Changes);
        QCOMPARE(requests.count(), 1);
        QTest::mouseClick(bar->changesTab(), Qt::LeftButton);
        QCOMPARE(requests.count(), 2);
        QCOMPARE(requests.at(1).at(0).value<TopBar::Tab>(), TopBar::Tab::Changes);
    }

    // The more menu is exactly the buttons folding put in it, in the order
    // they stand in the row, with their counts spelled out.
    void theTopBarMoreMenuCarriesTheFoldedSyncButtons()
    {
        BarFixture f = topBar();
        TopBar *bar = f.bar;
        QVERIFY(QTest::qWaitForWindowExposed(f.host.get()));
        settle();
        bar->pullButton()->setCount(2);
        bar->pushButton()->setCount(1);
        bar->fetchButton()->setCount(4);
        bar->fetchButton()->setToolTip(QStringLiteral("Fetch from all remotes (Ctrl+F)"));
        bar->mergeButton()->setEnabled(false);

        // What the menu holds once it has filled itself, the way a click on
        // the more button fills it.
        const auto entries = [bar] {
            QMenu *menu = bar->moreButton()->menu();
            menu->popup(QPoint(0, 0));
            QCoreApplication::processEvents();
            const QList<QAction *> actions = menu->actions();
            menu->hide();
            return actions;
        };
        // The accent dot BadgeButton paints for a folded count, read off the
        // pixels its badge paints. The bar's badge layer only paints the
        // badges of buttons on screen, and the checks below hide the whole
        // bar, so the badge is painted here the way the layer paints it.
        const auto hasDot = [](BadgeButton *button) {
            const int room = ui::space(20);
            QImage shot(button->size() + QSize(2 * room, 2 * room), QImage::Format_ARGB32_Premultiplied);
            shot.fill(Qt::transparent);
            QPainter p(&shot);
            button->paintBadge(&p, QRect(QPoint(room, room), button->size()));
            p.end();
            return imagePaints(shot, OmarchyTheme::instance()->accent());
        };

        // The menu's own entries after the folded ones: a separator, Refresh,
        // Open repository…, Clone…, a separator, Keybindings and Settings….
        constexpr int kOwnEntries = 7;

        // Wide: nothing is folded, so there is no button.
        QCOMPARE(f.levelAt(bar->sizeHint().width()), 0);
        QVERIFY(!bar->moreButton()->isVisible());
        QCOMPARE(entries().size(), kOwnEntries - 1); // no sync group, so no separator after it

        // Narrow enough for Fetch and Merge to fold.
        QVERIFY(f.widthForLevel(2) > 0);
        QCOMPARE(f.levelAt(f.widthForLevel(2)), 2);
        QList<QAction *> actions = entries();
        QCOMPARE(actions.size(), 2 + kOwnEntries);
        QVERIFY(actions.at(2)->isSeparator());
        QVERIFY2(actions.at(0)->text().endsWith(QStringLiteral("Fetch  (4)")), qPrintable(actions.at(0)->text()));
        QVERIFY(actions.at(0)->text().startsWith(ui::icon(ui::kFetch, QStringLiteral("F")).trimmed()));
        QCOMPARE(actions.at(0)->toolTip(), QStringLiteral("Fetch from all remotes (Ctrl+F)"));
        QVERIFY(actions.at(0)->isEnabled());
        QVERIFY2(actions.at(1)->text().endsWith(QStringLiteral("Merge")), qPrintable(actions.at(1)->text()));
        QVERIFY(!actions.at(1)->isEnabled()); // as disabled as the button it stands for

        // An entry is the button's own click: whatever the window connected
        // to it happens.
        QSignalSpy fetched(bar->fetchButton(), &QToolButton::clicked);
        actions.at(0)->trigger();
        QCOMPARE(fetched.count(), 1);

        // All four fold at the narrowest levels, in the same order.
        QCOMPARE(f.levelAt(bar->minimumSizeHint().width()), 5);
        actions = entries();
        QCOMPARE(actions.size(), 4 + kOwnEntries);
        QStringList labels;
        for (const QAction *a : actions.mid(0, 4))
            labels << a->text().section(QStringLiteral("  "), 1, 1);
        QCOMPARE(labels, QStringList({QStringLiteral("Pull"), QStringLiteral("Push"), QStringLiteral("Fetch"),
                                      QStringLiteral("Merge")}));

        // The dot stands for the counts in the menu — the explicit folded set,
        // never what happens to be on screen. With the whole bar hidden every
        // button is invisible, and the dot still only counts those two.
        QCOMPARE(f.levelAt(f.widthForLevel(2)), 2);
        bar->fetchButton()->setCount(0);
        bar->mergeButton()->setCount(0);
        f.host->hide();
        QVERIFY2(!hasDot(bar->moreButton()), "the dot counted a button that is still on the row");
        bar->fetchButton()->setCount(3);
        QVERIFY(hasDot(bar->moreButton()));
    }

    // A Nerd Font glyph's ink hangs over the advance its metrics report, so a
    // tab segment gives it a box of the design's width at the least — and
    // measures itself on that very box, labelled and folded alike.
    void theTopBarTabGlyphsGetABoxOfTheirOwn()
    {
        BarFixture f = topBar();
        TopBar *bar = f.bar;
        QVERIFY(QTest::qWaitForWindowExposed(f.host.get()));
        settle();

        const OmarchyTheme *theme = OmarchyTheme::instance();
        const QFontMetrics plain(theme->uiFont());
        QFont boldFont = theme->uiFont();
        boldFont.setBold(true);
        const QFontMetrics bold(boldFont);
        QFont pillFont = theme->captionFont();
        pillFont.setBold(true);
        pillFont.setLetterSpacing(QFont::AbsoluteSpacing, 0);
        // The count pill: 16 high, never narrower than it is tall, 4 either
        // side of the digits.
        const int pill = qMax(ui::space(ui::box::pill),
                              QFontMetrics(pillFont).horizontalAdvance(QStringLiteral("7")) + 2 * ui::space(ui::pad::pill));
        const auto box = [&plain](uint glyph, const QString &fallback) {
            return qMax(plain.horizontalAdvance(ui::icon(glyph, fallback).trimmed()), ui::space(ui::box::icon));
        };
        const int changesBox = box(ui::kCommit, QStringLiteral("C"));
        const int historyBox = box(ui::kHistory, QStringLiteral("H"));
        const int pad = 2 * ui::space(ui::pad::control);
        const int gap = ui::space(ui::gap::icon);

        // Spelled out: padding, the glyph's box, the label and the pill, with
        // the design's gap between them. The label is measured bold, the
        // weight it wears while selected.
        QCOMPARE(f.levelAt(bar->sizeHint().width()), 0);
        QCOMPARE(bar->changesTab()->sizeHint().width(),
                 pad + changesBox + gap + bold.horizontalAdvance(QStringLiteral("Changes")) + gap + pill);
        QCOMPARE(bar->historyTab()->sizeHint().width(),
                 pad + historyBox + gap + bold.horizontalAdvance(QStringLiteral("History")));
        QCOMPARE(bar->changesTab()->sizeHint().height(), ui::space(ui::box::control));

        // Folded: the labels go, the box stays exactly as wide.
        QCOMPARE(f.levelAt(bar->minimumSizeHint().width()), 5);
        QCOMPARE(bar->changesTab()->sizeHint().width(), pad + changesBox + gap + pill);
        QCOMPARE(bar->historyTab()->sizeHint().width(), pad + historyBox);
        // And the box is the design's width, not what the glyph happens to
        // advance by — otherwise the clock would be drawn half outside it.
        QCOMPARE(historyBox, ui::space(ui::box::icon));
    }

    // The icon form of a sync button is the design's square, not the size
    // hint of a text button around a glyph — and that width is what the
    // levels are folded on.
    void theTopBarSyncButtonsFoldToTheDesignsSquare()
    {
        BarFixture f = topBar();
        TopBar *bar = f.bar;
        QVERIFY(QTest::qWaitForWindowExposed(f.host.get()));
        settle();

        // Level 0 spells them out; level 1 is the icon form of all four.
        const int wide = bar->sizeHint().width();
        QCOMPARE(f.levelAt(wide), 0);
        QList<int> labelled;
        for (QToolButton *b : f.sync())
            labelled << f.rectOf(b).width();
        const int labelledHeight = f.rectOf(bar->pullButton()).height();

        int saved = 0;
        QCOMPARE(f.levelAt(wide - 1), 1);
        for (int i = 0; i < f.sync().size(); ++i) {
            QCOMPARE(f.rectOf(f.sync().at(i)).width(), iconFormWidth());
            saved += labelled.at(i) - iconFormWidth();
        }
        QVERIFY2(saved > 0, "the icon form is no narrower than the labelled one");
        // Both forms are one height: the icon form's padding only takes from
        // the sides, so the icons line up with the chips and the tabs.
        QCOMPARE(f.rectOf(bar->pullButton()).height(), labelledHeight);
        QCOMPARE(f.rectOf(bar->moreButton()).height(), labelledHeight);

        // The thresholds are those widths and nothing else: level 1 stops
        // fitting exactly where the four labels' extra width runs out, and
        // level 2 trades Fetch and Merge for the more button, the 28 px
        // square.
        QCOMPARE(f.levelAt(wide - saved), 1);
        QCOMPARE(f.levelAt(wide - saved - 1), 2);
        QCOMPARE(f.rectOf(bar->moreButton()).width(), ui::space(ui::box::control));
        const int levelTwo = wide - saved - 2 * iconFormWidth() + ui::space(ui::box::control) - ui::space(ui::gap::item);
        QCOMPARE(f.levelAt(levelTwo), 2);
        QCOMPARE(f.levelAt(levelTwo - 1), 3);
    }

    // A badge is the design's square hanging over its button's top-right
    // corner (kit.js badge()): 12 high, its right edge space(4) past the
    // button's, its top space(4) above it. The bar's badge layer paints it
    // there, in the icon form and the labelled one alike.
    void theBadgeHangsOverTheButtonsCorner()
    {
        BarFixture f = topBar();
        TopBar *bar = f.bar;
        QVERIFY(QTest::qWaitForWindowExposed(f.host.get()));
        settle();
        BadgeButton *pull = bar->pullButton();
        pull->setCount(2);
        const QRgb accent = OmarchyTheme::instance()->accent().rgb() | 0xff000000;
        const int wide = bar->sizeHint().width();
        for (const int level : {1, 0}) {
            QCOMPARE(f.levelAt(level == 1 ? wide - 1 : wide), level);
            QTest::qWait(500); // the pop is over: the badge is at its full size
            const QRect button = f.rectOf(pull);
            const QRect badge = pull->badgeRect(button);
            QCOMPARE(badge.top(), button.top() - ui::space(4));
            QCOMPARE(badge.right(), button.right() + ui::space(4));
            QCOMPARE(badge.height(), ui::space(ui::box::badge));
            QVERIFY(badge.width() >= ui::space(ui::box::badge));
            QVERIFY(bar->rect().contains(badge)); // the bar keeps the room it rises into

            const QImage shot = bar->grab().toImage();
            const auto at = [&shot](int x, int y) { return shot.pixel(x, y) | 0xff000000; };
            // Above the button's top edge, and right of its right edge.
            QVERIFY(badge.top() + 1 < button.top());
            QCOMPARE(at(badge.left() + 1, badge.top() + 1), accent);
            QVERIFY(badge.right() - 1 > button.right());
            QCOMPARE(at(badge.right() - 1, badge.bottom() - 1), accent);
            // Square: a rounded pill would leave its top-right corner bare.
            QCOMPARE(at(badge.right(), badge.top()), accent);
        }
    }

    // The icon form centres its glyph in the square, both ways. The badge is
    // not the button's to paint any more, so a count cannot skew what the
    // button's own pixels show.
    void theIconFormCentresTheGlyph()
    {
        BarFixture f = topBar();
        TopBar *bar = f.bar;
        QVERIFY(QTest::qWaitForWindowExposed(f.host.get()));
        settle();
        BadgeButton *pull = bar->pullButton();
        pull->setCount(2);
        QCOMPARE(f.levelAt(bar->sizeHint().width() - 1), 1);
        QVERIFY(pull->property("iconForm").toBool());
        QCOMPARE(pull->width(), iconFormWidth());

        // The glyph's box: whatever differs from the fill, the border ring left out.
        const QImage shot = pull->grab().toImage();
        constexpr int kRing = 2;
        const QRgb fill = shot.pixel(kRing, kRing);
        int left = INT_MAX, right = -1, top = INT_MAX, bottom = -1;
        for (int y = kRing; y < shot.height() - kRing; ++y) {
            for (int x = kRing; x < shot.width() - kRing; ++x) {
                if (shot.pixel(x, y) == fill)
                    continue;
                left = qMin(left, x);
                right = qMax(right, x);
                top = qMin(top, y);
                bottom = qMax(bottom, y);
            }
        }
        QVERIFY2(right >= 0, "the button paints no glyph");
        const auto offCentre = [](int from, int to, int size) { return (from + to) / 2.0 - (size - 1) / 2.0; };
        const QString box = QStringLiteral("glyph %1..%2 x %3..%4 in %5 x %6")
                                .arg(left).arg(right).arg(top).arg(bottom).arg(shot.width()).arg(shot.height());
        QVERIFY2(qAbs(offCentre(left, right, shot.width())) <= 1.0, qPrintable(box));
        QVERIFY2(qAbs(offCentre(top, bottom, shot.height())) <= 1.0, qPrintable(box));
    }

    // The pop and the walking dots move the painted badge on without its
    // content changing: the button says so, and the layer repaints.
    void theBadgeLayerFollowsTheBusyDots()
    {
        BarFixture f = topBar();
        TopBar *bar = f.bar;
        QVERIFY(QTest::qWaitForWindowExposed(f.host.get()));
        settle();
        BadgeButton *pull = bar->pullButton();
        QWidget *layer = bar->findChild<BadgeLayer *>();
        QVERIFY(layer);
        QVERIFY(layer->isVisible());

        // Counts the layer's paint events.
        struct PaintCounter : QObject {
            int paints = 0;
            bool eventFilter(QObject *, QEvent *event) override
            {
                if (event->type() == QEvent::Paint)
                    ++paints;
                return false;
            }
        } counter;
        layer->installEventFilter(&counter);

        QSignalSpy popping(pull, &BadgeButton::badgeRepaint);
        pull->setCount(2); // a larger count on a visible button pops
        QTRY_VERIFY(popping.count() > 1);
        QTest::qWait(500);

        pull->setBusy(true);
        QSignalSpy ticks(pull, &BadgeButton::badgeRepaint);
        settle();
        const int before = counter.paints;
        QTest::qWait(400);
        QVERIFY(ticks.count() >= 1);
        QTRY_VERIFY(counter.paints > before);
        pull->setBusy(false);
        layer->removeEventFilter(&counter);
    }

    // What the bar is called stays what it is at every width: the names are
    // the canonical text, never the folded or elided one. And the widths the
    // levels are weighed on are measured on probes nobody ever sees.
    void theTopBarNamesItsControlsWhateverItIsWearing()
    {
        BarFixture f = topBar();
        TopBar *bar = f.bar;
        QVERIFY(QTest::qWaitForWindowExposed(f.host.get()));
        settle();

        for (int level = 0; level <= 5; ++level) {
            QCOMPARE(f.levelAt(f.widthForLevel(level)), level);
            QCOMPARE(barNames(bar), kBarNames);
        }
        bar->setChangesCount(42); // a remeasure changes nothing about them
        settle();
        QCOMPARE(barNames(bar), kBarNames);

        // The probes are the bar's own children, outside its layout and its
        // placement, and showing the bar leaves them behind.
        QList<QToolButton *> probes;
        for (QObject *child : bar->children())
            if (auto *b = qobject_cast<QToolButton *>(child))
                probes << b;
        QCOMPARE(probes.size(), 6); // the two chips' and one per sync button
        for (QToolButton *b : std::as_const(probes))
            QVERIFY2(!b->isVisible(), qPrintable(b->objectName()));
        // A probe's text follows the label it stands for and nothing else: a
        // remeasure that changes no label leaves every one of them as it was,
        // so the accessibility bridge hears of no name that is not news.
        const auto probeTexts = [&probes] {
            QStringList out;
            for (const QToolButton *b : std::as_const(probes))
                out << b->text();
            return out;
        };
        const QStringList measured = probeTexts();
        QVERIFY(!measured.contains(QString()));
        bar->setChangesCount(7);
        settle();
        QCOMPARE(probeTexts(), measured);
        QCOMPARE(bar->layout()->count(), 2); // the row and the hairline

        // A resize that stays inside one level leaves every live text alone:
        // no candidate a measurement tried on ever reaches the screen.
        const auto texts = [bar] {
            QStringList out;
            for (const QToolButton *b : QList<const QToolButton *>{bar->repoButton(), bar->branchButton(),
                                                                   bar->pullButton(), bar->pushButton(),
                                                                   bar->fetchButton(), bar->mergeButton(),
                                                                   bar->moreButton()})
                out << b->text();
            return out;
        };
        const int widest = f.widthForLevel(2);
        QCOMPARE(f.levelAt(widest), 2);
        const QStringList atTwo = texts();
        int narrowest = widest;
        for (int width = widest - 1; width >= bar->minimumSizeHint().width(); --width) {
            if (f.levelAt(width) != 2)
                break;
            narrowest = width;
            QCOMPARE(texts(), atTwo);
        }
        QVERIFY2(narrowest < widest, "level 2 is one width wide");
    }

    // Stacked, the row has three levels of its own: the tab labels go, then
    // the tabs take a row of their own, and only there do the names elide,
    // by what the first row lacks. The repository chip wears its name, the
    // right group is the sync dropdown and More, and the toggles are gone.
    void theStackedTopBarFoldsInThreeSteps()
    {
        BarFixture f = topBar();
        TopBar *bar = f.bar;
        QVERIFY(QTest::qWaitForWindowExposed(f.host.get()));
        settle();
        const int ordinary = bar->sizeHint().width();
        const int ordinaryMin = bar->minimumSizeHint().width();
        QVERIFY(bar->diffTab()->isHidden());

        bar->setStacked(true);
        QVERIFY(bar->isStacked());
        QVERIFY(!bar->diffTab()->isHidden());
        const int wide = bar->sizeHint().width();
        QCOMPARE(f.levelAt(wide + 200), 0);
        QCOMPARE(f.levelAt(wide), 0);     // the size hint is exactly what S0 takes
        const int labels = f.rectOf(f.tabs()).width();
        QCOMPARE(f.levelAt(wide - 1), 1); // one pixel under it, the labels go

        QList<int> seen;
        QHash<int, int> widest;
        for (int width = wide; width >= bar->minimumSizeHint().width(); --width) {
            const int level = f.levelAt(width);
            QVERIFY2(seen.isEmpty() || level >= seen.last(), "the bar unfolded while it narrowed");
            if (seen.isEmpty() || level != seen.last()) {
                seen << level;
                widest[level] = width;
            }
        }
        QCOMPARE(seen, QList<int>({0, 1, 2}));
        for (int level = 1; level <= 2; ++level)
            QCOMPARE(f.levelAt(widest.value(level) + 1), level - 1);
        // S1 is S0 less what the labels took: its exact fit, and a pixel less.
        QCOMPARE(f.levelAt(widest.value(1)), 1);
        const int glyphs = f.rectOf(f.tabs()).width();
        QVERIFY(glyphs < labels);
        QCOMPARE(f.levelAt(wide - (labels - glyphs)), 1);
        QCOMPARE(f.levelAt(wide - (labels - glyphs) - 1), 2);

        // What every level shows.
        const QString fullBranch = ui::icon(ui::kBranch) + bar->branchLabel() + ui::chevron();
        const QString fullRepo = ui::icon(ui::kFolderOpen) + bar->repositoryName() + ui::chevron();
        QList<int> dropdownWidths;
        for (int level = 0; level <= 2; ++level) {
            QCOMPARE(f.levelAt(widest.value(level, wide)), level);
            // One row, 8 + 28 + 8, or two: the tabs a space(kBar) under it.
            const int rows = level == 2 ? 2 : 1;
            QCOMPARE(bar->height(), (rows + 1) * ui::space(ui::kBar) + rows * ui::space(ui::box::control));
            QCOMPARE(visible(f.sync()), QList<bool>({false, false, false, false}));
            QVERIFY(!bar->layoutButton()->isVisible());
            QVERIFY(!bar->diffToggle()->isVisible());
            for (const QWidget *w : bar->findChildren<QWidget *>())
                QVERIFY2(!(w->isVisible() && w->minimumWidth() == 1 && w->maximumWidth() == 1 && w->height() > 1),
                         "the divider before the toggles is still there");
            QVERIFY(bar->syncDropdown()->isVisible());
            QVERIFY(bar->moreButton()->isVisible());
            QCOMPARE(bar->diffTab()->isVisible(), true);
            // The dropdown is borderless and as wide as its content at every
            // level, the first row's height, 8 under the bar's top; More is
            // the design's bare 28 px square, in the icon form.
            QCOMPARE(bar->isTwoRows(), level == 2);
            dropdownWidths << f.rectOf(bar->syncDropdown()).width();
            QCOMPARE(dropdownWidths.last(), dropdownWidths.first());
            QVERIFY(bar->syncDropdown()->property("ghost").toBool());
            QCOMPARE(f.rectOf(bar->syncDropdown()).height(), ui::space(ui::box::control));
            QCOMPARE(f.rectOf(bar->syncDropdown()).y(), ui::space(ui::kBar));
            QCOMPARE(f.rectOf(bar->moreButton()).width(), ui::space(ui::box::control));
            QVERIFY(bar->moreButton()->property("iconForm").toBool());
            // The gaps: the repository against the left edge, a cluster to the
            // branch; on one row an item between the two controls on the
            // right, on two the dropdown against the right edge, at least a
            // cluster after the branch, and More on the tabs' row; More
            // against the right edge.
            const QRect repo = f.rectOf(bar->repoButton()), branch = f.rectOf(bar->branchButton());
            const QRect sync = f.rectOf(bar->syncDropdown()), more = f.rectOf(bar->moreButton());
            QCOMPARE(repo.x(), ui::space(12));
            QCOMPARE(branch.x() - (repo.x() + repo.width()), ui::space(ui::gap::cluster));
            if (level == 2) {
                QVERIFY(sync.x() - (branch.x() + branch.width()) >= ui::space(ui::gap::cluster));
                QCOMPARE(sync.right() + 1, bar->width() - ui::space(12));
                QCOMPARE(more.y(), 2 * ui::space(ui::kBar) + ui::space(ui::box::control));
            } else {
                QCOMPARE(more.x() - (sync.x() + sync.width()), ui::space(ui::gap::item));
                QCOMPARE(more.y(), ui::space(ui::kBar));
            }
            QCOMPARE(more.x() + more.width(), bar->width() - ui::space(12));
            // Two rows this wide give every segment the room for its label.
            QCOMPARE(static_cast<SegmentButton *>(bar->changesTab())->isLabelled(), level != 1);
            QCOMPARE(static_cast<SegmentButton *>(bar->diffTab())->isLabelled(), level != 1);
            QVERIFY(bar->changesCount() > 0); // the pill stays at every level
            // The names are whole on one row, and on two while the first holds them.
            QCOMPARE(bar->repoButton()->text(), fullRepo);
            QCOMPARE(bar->branchButton()->text(), fullBranch);
        }

        // Wide, the tabs sit in the middle; at the narrowest one row the
        // clamp keeps them a group gap clear of both groups.
        QCOMPARE(f.levelAt(wide + 400), 0);
        const QRect middle = f.rectOf(f.tabs());
        QVERIFY(qAbs(middle.x() + middle.width() / 2.0 - (wide + 400) / 2.0) <= 1.0);
        QCOMPARE(f.levelAt(widest.value(2) + 1), 1);
        const QRect tight = f.rectOf(f.tabs());
        const QRect branch = f.rectOf(bar->branchButton());
        QCOMPARE(tight.x(), branch.x() + branch.width() + ui::space(ui::gap::group));
        QCOMPARE(tight.x() + tight.width() + ui::space(ui::gap::group), f.rectOf(bar->syncDropdown()).x());
        // A pixel less, the tabs are the second row, a space(kBar) under the
        // first, stretched over its width but for an item gap and More.
        QCOMPARE(f.levelAt(widest.value(2)), 2);
        const QRect own = f.rectOf(f.tabs());
        const int ownWidth = bar->width() - 2 * ui::space(12) - ui::space(ui::gap::item) - ui::space(ui::box::control);
        QCOMPARE(own, QRect(ui::space(12), 2 * ui::space(ui::kBar) + ui::space(ui::box::control), ownWidth,
                            ui::space(ui::box::control)));
        QCOMPARE(f.rectOf(bar->moreButton()), QRect(own.right() + 1 + ui::space(ui::gap::item), own.y(),
                                                     ui::space(ui::box::control), ui::space(ui::box::control)));
        QVERIFY(static_cast<SegmentStrip *>(f.tabs())->isStretch());
        // Down to the narrowest, the dropdown against the row's right edge and
        // the names eliding by exactly what the first row lacks: when they
        // do, the branch ends a cluster before the dropdown. The shorter
        // repository name gives way only once the branch has come down to as
        // little of itself.
        bool repoElided = false;
        for (int width = widest.value(2); width >= bar->minimumSizeHint().width(); --width) {
            QCOMPARE(f.levelAt(width), 2);
            const QRect sync = f.rectOf(bar->syncDropdown());
            QCOMPARE(sync.right() + 1, width - ui::space(12));
            const int end = f.rectOf(bar->branchButton()).right() + 1 + ui::space(ui::gap::cluster);
            const bool whole = bar->branchButton()->text() == fullBranch && bar->repoButton()->text() == fullRepo;
            if (whole)
                QVERIFY(end <= sync.x());
            else
                QCOMPARE(end, sync.x());
            if (bar->repoButton()->text() != fullRepo && !repoElided) {
                repoElided = true;
                QVERIFY2(bar->branchButton()->text() != fullBranch, "the shorter name gave way first");
            }
        }
        QVERIFY(repoElided);
        // The floors: a lone ellipsis between each glyph and its chevron, in
        // the chip's font, and each chip no wider than that. A name's
        // advance rounded up, as the bar and the chip measure it; the
        // ellipsis rounded up as well: elidedText() gives nothing at all a
        // fraction short of it.
        const auto advance = [](const QToolButton *chip, const QString &text) {
            return qCeil(QFontMetricsF(chip->font()).horizontalAdvance(text));
        };
        const int name = advance(bar->branchButton(), bar->branchLabel());
        QVERIFY(name > ui::space(72));
        QCOMPARE(f.levelAt(widest.value(1)), 1);
        const int branchChrome = f.rectOf(bar->branchButton()).width() - name;
        const int repoChrome = f.rectOf(bar->repoButton()).width() - advance(bar->repoButton(), bar->repositoryName());
        QCOMPARE(f.levelAt(bar->minimumSizeHint().width()), 2);
        const QString ellipsis(QChar(0x2026));
        QCOMPARE(bar->branchButton()->text(), ui::icon(ui::kBranch) + ellipsis + ui::chevron());
        QCOMPARE(f.rectOf(bar->branchButton()).width(), branchChrome + advance(bar->branchButton(), ellipsis));
        QCOMPARE(bar->repoButton()->text(), ui::icon(ui::kFolderOpen) + ellipsis + ui::chevron());
        QCOMPARE(f.rectOf(bar->repoButton()).width(), repoChrome + advance(bar->repoButton(), ellipsis));
        // No share of a row this narrow holds "Changes 7": the tabs go to glyphs.
        QVERIFY(!static_cast<SegmentButton *>(bar->changesTab())->isLabelled());
        QVERIFY(bar->minimumSizeHint().width() < bar->sizeHint().width());
        // So the stacked row asks for less than the ordinary one's last level,
        // which keeps 72 px of this long a name.
        QVERIFY2(bar->minimumSizeHint().width() < ordinaryMin,
                 qPrintable(QStringLiteral("%1 >= %2").arg(bar->minimumSizeHint().width()).arg(ordinaryMin)));

        // Unstacked at a wide width: level 0 of the ordinary row, every text
        // canonical again, the Diff tab gone and the ordinary hints back.
        bar->setStacked(false);
        QVERIFY(bar->diffTab()->isHidden());
        QCOMPARE(bar->sizeHint().width(), ordinary);
        QCOMPARE(bar->minimumSizeHint().width(), ordinaryMin);
        QCOMPARE(f.levelAt(ordinary), 0);
        QCOMPARE(bar->repoButton()->text(), ui::icon(ui::kFolderOpen) + QStringLiteral("omagit-workspace") + ui::chevron());
        QCOMPARE(bar->branchButton()->text(),
                 ui::icon(ui::kBranch) + QStringLiteral("feature/askpass-login-dialog") + ui::chevron());
        QCOMPARE(visible(f.sync()), QList<bool>({true, true, true, true}));
        QVERIFY(bar->pullButton()->text().contains(QLatin1String("Pull")));
        QVERIFY(!bar->syncDropdown()->isVisible());
        QVERIFY(!bar->moreButton()->isVisible());
        QVERIFY(bar->layoutButton()->isVisible());
        QVERIFY(bar->diffToggle()->isVisible());
        QVERIFY(!bar->diffTab()->isVisible());
        QCOMPARE(barNames(bar), kBarNames);
        // More is the same 28 px square on the ordinary row, wherever it shows.
        QCOMPARE(f.levelAt(ordinaryMin), 5);
        QVERIFY(bar->moreButton()->isVisible());
        QCOMPARE(f.rectOf(bar->moreButton()).width(), ui::space(ui::box::control));
        QVERIFY(bar->moreButton()->property("iconForm").toBool());
    }

    // The Diff tab between the other two: one exclusive switch of three, that
    // asks and is told.
    void theStackedTabsAskForTheirPresentation()
    {
        BarFixture f = topBar();
        TopBar *bar = f.bar;
        bar->setStacked(true);
        bar->resize(bar->sizeHint());
        QVERIFY(QTest::qWaitForWindowExposed(f.host.get()));
        settle();

        QCOMPARE(bar->diffTab()->accessibleName(), QStringLiteral("Diff"));
        QCOMPARE(bar->diffTab()->toolTip(), QStringLiteral("The diff of the current file, with the file rail (Ctrl+Shift+B)"));
        QCOMPARE(bar->syncDropdown()->accessibleName(), QStringLiteral("Sync"));
        QCOMPARE(bar->syncDropdown()->toolTip(),
                 QStringLiteral("Pull, push, fetch or merge (Ctrl+P, Ctrl+Shift+P, Ctrl+F, Ctrl+Shift+M)"));
        QVERIFY(bar->changesTab()->x() < bar->diffTab()->x());
        QVERIFY(bar->diffTab()->x() < bar->historyTab()->x());

        QSignalSpy requests(bar, &TopBar::tabRequested);
        const auto checked = [bar] {
            return QList<bool>{bar->changesTab()->isChecked(), bar->diffTab()->isChecked(), bar->historyTab()->isChecked()};
        };
        QTest::mouseClick(bar->diffTab(), Qt::LeftButton);
        QCOMPARE(requests.count(), 1);
        QCOMPARE(requests.last().at(0).value<TopBar::Tab>(), TopBar::Tab::Diff);
        QCOMPARE(checked(), QList<bool>({false, true, false}));
        QCOMPARE(bar->currentTab(), TopBar::Tab::Diff);

        bar->setCurrentTab(TopBar::Tab::History); // the window's word asks nothing back
        QCOMPARE(requests.count(), 1);
        QCOMPARE(checked(), QList<bool>({false, false, true}));
        bar->setCurrentTab(TopBar::Tab::Diff);
        QCOMPARE(checked(), QList<bool>({false, true, false}));
        QCOMPARE(requests.count(), 1);

        QTest::mouseClick(bar->changesTab(), Qt::LeftButton);
        QCOMPARE(requests.last().at(0).value<TopBar::Tab>(), TopBar::Tab::Changes);
        QTest::mouseClick(bar->historyTab(), Qt::LeftButton);
        QCOMPARE(requests.last().at(0).value<TopBar::Tab>(), TopBar::Tab::History);
        QCOMPARE(requests.count(), 3);
        QCOMPARE(checked(), QList<bool>({false, false, true}));
    }

    // The dropdown reads the two counts, the busy state and Merge's mark off
    // the buttons it stands for, and wears no badge of its own.
    void theSyncDropdownMirrorsTheButtons()
    {
        BarFixture f = topBar();
        TopBar *bar = f.bar;
        bar->setStacked(true);
        bar->resize(bar->sizeHint());
        QVERIFY(QTest::qWaitForWindowExposed(f.host.get()));
        settle();
        BadgeButton *sync = bar->syncDropdown();
        const QColor accent = OmarchyTheme::instance()->accent();
        const auto shot = [sync] { return sync->grab().toImage(); };

        // Zero on both sides: plain 0s, nothing in the accent colour.
        QVERIFY(sync->text().isEmpty());
        QVERIFY(!paints(sync, accent));
        const QImage zero = shot();
        bar->pullButton()->setCount(2);
        QVERIFY(paints(sync, accent)); // a positive count is accent text
        bar->pullButton()->setCount(0);
        QCOMPARE(shot(), zero);

        // Set while hidden, shown as it is.
        f.host->hide();
        bar->pullButton()->setCount(3);
        bar->pushButton()->setCount(1);
        f.host->show();
        QVERIFY(QTest::qWaitForWindowExposed(f.host.get()));
        settle();
        const QImage counted = shot();
        QVERIFY(counted != zero);
        BarFixture fresh = topBar();
        fresh.bar->setStacked(true);
        fresh.bar->resize(fresh.bar->sizeHint());
        fresh.bar->pullButton()->setCount(3);
        fresh.bar->pushButton()->setCount(1);
        QVERIFY(QTest::qWaitForWindowExposed(fresh.host.get()));
        settle();
        QCOMPARE(fresh.bar->syncDropdown()->grab().toImage(), counted);
        // Two digits fit; a hundred and up is 99+ whatever the number.
        bar->pullButton()->setCount(99);
        const QImage ninetyNine = shot();
        bar->pullButton()->setCount(100);
        const QImage hundred = shot();
        QVERIFY(hundred != ninetyNine);
        bar->pullButton()->setCount(250);
        QCOMPARE(shot(), hundred);
        bar->pullButton()->setCount(3);
        QCOMPARE(shot(), counted);

        // Busy: Pull alone, Push alone, then both, then neither; each side's
        // field is watched on its own, so one side's dots never stand in for
        // the other's. A busy Pull pushes Push's count along, so a side that
        // is not busy is known by its field holding still while the other's
        // dots walk, the count's accent in it.
        const auto field = [&shot](int from, int to) {
            const QImage all = shot();
            return all.copy(ui::space(from), 0, ui::space(to) - ui::space(from), all.height());
        };
        // screens.js syncDropdown(): Pull's count 20 in, Push's 52 in, each
        // field its 8 px slot or the dots' 16 px box.
        const auto pullField = [&field] { return field(20, 36); };
        const auto pushField = [&field] { return field(52, 68); };
        const QImage pullCounted = pullField(), pushCounted = pushField();

        bar->pullButton()->setBusy(true);
        QImage pullBusy = pullField();
        const QImage pushStill = pushField();
        QVERIFY(pullBusy != pullCounted);
        QTRY_VERIFY_WITH_TIMEOUT(pullField() != pullBusy, 2000);
        QCOMPARE(pushField(), pushStill);
        QVERIFY(imagePaints(pushStill, accent));
        bar->pullButton()->setBusy(false);
        QCOMPARE(shot(), counted);

        bar->pushButton()->setBusy(true);
        QImage pushBusy = pushField();
        QVERIFY(pushBusy != pushCounted);
        QTRY_VERIFY_WITH_TIMEOUT(pushField() != pushBusy, 2000);
        QCOMPARE(pullField(), pullCounted);
        bar->pushButton()->setBusy(false);
        QCOMPARE(shot(), counted);

        bar->pullButton()->setBusy(true);
        bar->pushButton()->setBusy(true);
        pullBusy = pullField();
        pushBusy = pushField();
        QTRY_VERIFY_WITH_TIMEOUT(pullField() != pullBusy && pushField() != pushBusy, 2000);
        bar->pullButton()->setBusy(false);
        bar->pushButton()->setBusy(false);
        QCOMPARE(pullField(), pullCounted);
        QCOMPARE(pushField(), pushCounted);
        QCOMPARE(shot(), counted); // the counts are back
        QTest::qWait(400);
        QCOMPARE(shot(), counted); // and nothing walks any more

        // Merge's mark, and its colour; none of the dropdown's own.
        const QColor red = OmarchyTheme::instance()->color(QStringLiteral("red"));
        bar->mergeButton()->setMark(QStringLiteral("!"), red);
        QCOMPARE(sync->markText(), QStringLiteral("!"));
        QCOMPARE(sync->markColor(), red);
        bar->mergeButton()->setMark(QString(), red);
        QVERIFY(sync->markText().isEmpty());
        QCOMPARE(shot(), counted);
        QCOMPARE(sync->count(), 0);
        QVERIFY(!sync->isBusy());
    }

    // The design's positions are the least each field gets: a count wider
    // than two digits pushes the rest along and widens the dropdown, which
    // keeps as much room after its content as before it; Merge's mark hangs
    // over the corner without widening it. At the design's text size and a
    // larger one.
    void theSyncDropdownMakesRoomForItsContent()
    {
        for (const int base : {12, 16}) {
            QTemporaryDir dir, home;
            QVERIFY(dir.isValid() && home.isValid());
            QVERIFY(writeFixture(QDir(dir.path()).filePath(QStringLiteral("shell.toml")),
                                 QByteArray("[font]\nbase-size = ") + QByteArray::number(base) + '\n'));
            {
                ScopedEnv themeDir("OMAGIT_THEME_DIR", dir.path().toUtf8());
                ScopedEnv scratchHome("HOME", home.path().toUtf8());
                OmarchyTheme theme;
                QCOMPARE(theme.fontBase(), base);
                theme.apply(*qApp);

                BarFixture f = topBar();
                TopBar *bar = f.bar;
                bar->setStacked(true);
                bar->resize(bar->sizeHint());
                QVERIFY(QTest::qWaitForWindowExposed(f.host.get()));
                settle();
                BadgeButton *sync = bar->syncDropdown();
                const QColor accent = theme.accent();
                const QColor red = theme.color(QStringLiteral("red"));
                const auto width = [&f, sync] { return f.rectOf(sync).width(); };
                // As much bare room after the content as before it, to a pixel.
                const auto even = [sync](const char *what) {
                    const QPair<int, int> bare = bareColumns(sync->grab().toImage());
                    QVERIFY2(bare.first > 0 && qAbs(bare.first - bare.second) <= 1,
                             qPrintable(QStringLiteral("%1: %2 before, %3 after")
                                            .arg(QLatin1String(what)).arg(bare.first).arg(bare.second)));
                };

                bar->pullButton()->setCount(2);
                bar->pushButton()->setCount(1);
                const int narrow = width();
                even("2 and 1");
                bar->pushButton()->setCount(67);
                even("2 and 67");

                // 99+ in accent: wider, and as even.
                bar->pullButton()->setCount(120);
                const int wide = width();
                QVERIFY2(wide > narrow, qPrintable(QString::number(wide)));
                QVERIFY(imagePaints(sync->grab().toImage(), accent));
                even("99+ and 67");
                bar->pushButton()->setCount(1);
                even("99+ and 1");
                const int wideOne = width();

                // The mark hangs over the corner like a badge, and the width
                // stays the content's. The bar's badge layer paints it over
                // the dropdown's corner, so it is read off the bar: the
                // dropdown's columns and the badge's overhang past them.
                bar->mergeButton()->setMark(QStringLiteral("!"), red);
                QCOMPARE(width(), wideOne);
                const QRect dropdown = f.rectOf(sync);
                const QImage grab = bar->grab().toImage().copy(dropdown.x(), 0, dropdown.width() + ui::space(4), bar->height());
                QVERIFY(imagePaints(grab, red));

                // Back to where it started.
                bar->mergeButton()->setMark(QString(), red);
                bar->pullButton()->setCount(2);
                QCOMPARE(width(), narrow);

                f.host.reset();
            }
            g_theme.reset(new OmarchyTheme);
            g_theme->apply(*qApp);
            QVERIFY(OmarchyTheme::instance() == g_theme.get());
        }
    }

    // The dropdown at the design's text size, on one row and on two alike:
    // Pull's count against its arrow's box (4 + 16 in), Push's at 4 + 16 + 8
    // + 8 + 16, each in the accent while it counts anything; the busy dots
    // walking in those fields, a busy Pull moving Push's along; nothing past
    // Push's count but the bare room that mirrors the room before the down
    // arrow; no border at rest, and the hover frame at both edges.
    void theSyncDropdownFollowsTheDesign()
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

        BarFixture f = topBar();
        TopBar *bar = f.bar;
        bar->setStacked(true);
        QVERIFY(QTest::qWaitForWindowExposed(f.host.get()));
        const int twoRows = f.widthForLevel(2);
        QVERIFY(twoRows > 0);
        QCOMPARE(f.levelAt(twoRows), 2);
        settle();
        BadgeButton *sync = bar->syncDropdown();
        QVERIFY(bar->isTwoRows());
        QVERIFY(sync->property("ghost").toBool());
        const QColor accent = theme.accent(), border = theme.normalBorder();
        const auto shot = [sync] { return sync->grab().toImage(); };
        // A field's columns, in the design's pixels.
        const auto field = [&shot](int from, int to) {
            const QImage all = shot();
            return all.copy(ui::space(from), 0, ui::space(to) - ui::space(from), all.height());
        };
        const auto paintsIn = [](const QImage &image, const QColor &colour, int from, int to) {
            return imagePaints(image, colour, ui::space(from), ui::space(to));
        };

        // Pull's count alone in the accent, then Push's too, each in its field.
        bar->pullButton()->setCount(3);
        QImage counted = shot();
        QVERIFY(paintsIn(counted, accent, 20, 28));
        QVERIFY(!imagePaints(counted, accent, 0, ui::space(20)));
        QVERIFY(!imagePaints(counted, accent, ui::space(28)));
        bar->pushButton()->setCount(1);
        counted = shot();
        const int rest = counted.width();
        QVERIFY(paintsIn(counted, accent, 20, 28));
        QVERIFY(!paintsIn(counted, accent, 28, 52));
        QVERIFY(paintsIn(counted, accent, 52, 60));
        // Past Push's count, the bare ground: no chevron, and no border at
        // either edge; as much of it after the count as before the arrow.
        const QRgb ground = counted.pixel(0, 0);
        for (int x = ui::space(60); x < counted.width(); ++x)
            for (int y = 0; y < counted.height(); ++y)
                QVERIFY2(counted.pixel(x, y) == ground, qPrintable(QStringLiteral("%1,%2").arg(x).arg(y)));
        QVERIFY(!imagePaints(counted, border, 0, 1));
        QVERIFY(!imagePaints(counted, border, counted.width() - 1, counted.width()));
        const QPair<int, int> bare = bareColumns(counted);
        QVERIFY2(qAbs(bare.first - bare.second) <= 1, qPrintable(QStringLiteral("%1 vs %2").arg(bare.first).arg(bare.second)));

        // Hovered: the frame (the stylesheet's hover border) at both edges,
        // around that same content.
        sync->setAttribute(Qt::WA_UnderMouse, true);
        const QImage hovered = shot();
        sync->setAttribute(Qt::WA_UnderMouse, false);
        const int middle = hovered.height() / 2;
        QVERIFY(hovered.pixel(0, middle) != counted.pixel(0, middle));
        QCOMPARE(hovered.pixel(hovered.width() - 1, middle), hovered.pixel(0, middle));
        QCOMPARE(shot(), counted);

        // A busy Pull: its dots walk in its 16 px box, and Push's count,
        // moved along by the 8 more they take, holds still in the accent.
        bar->pullButton()->setBusy(true);
        QCOMPARE(sync->width(), rest + ui::space(8));
        const auto pullDots = [&field] { return field(20, 36); };
        QImage pullBusy = pullDots();
        const QImage pushMoved = field(60, 68);
        QVERIFY(imagePaints(pushMoved, accent));
        QVERIFY(!paintsIn(shot(), accent, 36, 60));
        QTRY_VERIFY_WITH_TIMEOUT(pullDots() != pullBusy, 2000);
        QCOMPARE(field(60, 68), pushMoved);
        bar->pullButton()->setBusy(false);
        QCOMPARE(shot(), counted);

        // A busy Push: its dots in its own box, 52 in; Pull's count still.
        bar->pushButton()->setBusy(true);
        const int pushBusyWidth = sync->width();
        QVERIFY(pushBusyWidth > rest);
        const auto pushDots = [&field] { return field(52, 68); };
        const QImage pushBusy = pushDots(), pullStill = field(20, 28);
        QVERIFY(imagePaints(pullStill, accent));
        QTRY_VERIFY_WITH_TIMEOUT(pushDots() != pushBusy, 2000);
        QCOMPARE(field(20, 28), pullStill);

        // Both: Push's dots walk in their box moved along, 60 in.
        bar->pullButton()->setBusy(true);
        QCOMPARE(sync->width(), pushBusyWidth + ui::space(8));
        const auto movedDots = [&field] { return field(60, 76); };
        pullBusy = pullDots();
        const QImage movedBusy = movedDots();
        QTRY_VERIFY_WITH_TIMEOUT(pullDots() != pullBusy && movedDots() != movedBusy, 2000);
        bar->pullButton()->setBusy(false);
        bar->pushButton()->setBusy(false);
        QCOMPARE(shot(), counted);

        // One row: the very same control.
        QCOMPARE(f.levelAt(bar->sizeHint().width()), 0);
        QVERIFY(!bar->isTwoRows());
        QVERIFY(sync->property("ghost").toBool());
        QCOMPARE(shot(), counted);
    }

    // The dropdown's menu: the four sync buttons, spelled out, doing what the
    // buttons do.
    void theSyncMenuMirrorsTheButtons()
    {
        BarFixture f = topBar();
        TopBar *bar = f.bar;
        bar->setStacked(true);
        bar->resize(bar->sizeHint());
        QVERIFY(QTest::qWaitForWindowExposed(f.host.get()));
        settle();
        QToolButton *sync = bar->syncDropdown();
        QCOMPARE(sync->popupMode(), QToolButton::InstantPopup);
        QVERIFY(qobject_cast<TickMenu *>(sync->menu()));
        bar->pullButton()->setCount(2);
        bar->pullButton()->setToolTip(QStringLiteral("Pull 2 commits (Ctrl+P)"));
        bar->pushButton()->setToolTip(QStringLiteral("Push to origin/main (Ctrl+Shift+P)"));
        bar->fetchButton()->setEnabled(false);
        bar->mergeButton()->setToolTip(QStringLiteral("Merge another branch (Ctrl+Shift+M)"));

        const QList<QAction *> actions = filledMenu(sync->menu());
        const auto entry = [](uint glyph, const QString &fallback, const QString &label) {
            return ui::icon(glyph, fallback).trimmed() + QStringLiteral("  ") + label;
        };
        QCOMPARE(menuTexts(actions),
                 QStringList({entry(ui::kPull, QStringLiteral("↓"), QStringLiteral("Pull  (2)")),
                              entry(ui::kPush, QStringLiteral("↑"), QStringLiteral("Push")),
                              entry(ui::kFetch, QStringLiteral("F"), QStringLiteral("Fetch")), QStringLiteral("-"),
                              entry(ui::kMerge, QStringLiteral("M"), QStringLiteral("Merge…"))}));
        QCOMPARE(actions.at(0)->toolTip(), QStringLiteral("Pull 2 commits (Ctrl+P)"));
        QCOMPARE(actions.at(1)->toolTip(), QStringLiteral("Push to origin/main (Ctrl+Shift+P)"));
        QCOMPARE(actions.at(4)->toolTip(), QStringLiteral("Merge another branch (Ctrl+Shift+M)"));
        QVERIFY(actions.at(0)->isEnabled());
        QVERIFY(!actions.at(2)->isEnabled());

        const QList<QToolButton *> sources{bar->pullButton(), bar->pushButton(), bar->fetchButton(), bar->mergeButton()};
        const QList<int> rows{0, 1, 2, 4};
        bar->fetchButton()->setEnabled(true);
        const QList<QAction *> again = filledMenu(sync->menu());
        for (int i = 0; i < sources.size(); ++i) {
            QSignalSpy clicked(sources.at(i), &QToolButton::clicked);
            again.at(rows.at(i))->trigger();
            QCOMPARE(clicked.count(), 1);
        }
    }

    // More carries the window's own commands after whatever sync buttons are
    // folded into it; stacked, it is always there, with no sync entry and no
    // dot.
    void theMoreMenuCarriesTheWindowsCommands()
    {
        BarFixture f = topBar();
        TopBar *bar = f.bar;
        QVERIFY(QTest::qWaitForWindowExposed(f.host.get()));
        settle();
        const QStringList own{ui::icon(ui::kRefresh) + QStringLiteral("Refresh"),
                              ui::icon(ui::kFolderOpen) + QStringLiteral("Open repository…"),
                              ui::icon(ui::kFetch) + QStringLiteral("Clone…"), QStringLiteral("-"),
                              ui::icon(ui::kKeyboard) + QStringLiteral("Keybindings"),
                              ui::icon(ui::kCog) + QStringLiteral("Settings…")};

        // Folded: the sync entries, a separator, then the window's.
        QCOMPARE(f.levelAt(f.widthForLevel(2)), 2);
        QList<QAction *> actions = filledMenu(bar->moreButton()->menu());
        QCOMPARE(menuTexts(actions).mid(2), QStringList({QStringLiteral("-")}) + own);
        QCOMPARE(actions.at(3)->toolTip(), QStringLiteral("Re-read the repository (F5)"));
        QCOMPARE(actions.at(4)->toolTip(), QStringLiteral("Pick a folder inside a git repository (Ctrl+O)"));
        QCOMPARE(actions.at(5)->toolTip(), QStringLiteral("Download a repository from a URL or GitHub (Ctrl+Shift+O)"));
        QCOMPARE(actions.at(7)->toolTip(), QStringLiteral("Every keyboard shortcut (Ctrl+K)"));

        // Stacked: only the window's, with no separator in front.
        bar->pullButton()->setCount(4);
        bar->fetchButton()->setCount(2);
        bar->setStacked(true);
        f.levelAt(bar->sizeHint().width());
        QVERIFY(bar->moreButton()->isVisible());
        QCOMPARE(f.rectOf(bar->moreButton()).width(), ui::space(28)); // no badge to keep room for
        actions = filledMenu(bar->moreButton()->menu());
        QCOMPARE(menuTexts(actions), own);
        // No dot: the sync counts are the dropdown's to show.
        QVERIFY(bar->moreButton()->markText().isEmpty());
        QVERIFY(!paints(bar->moreButton(), OmarchyTheme::instance()->accent()));

        QSignalSpy refresh(bar, &TopBar::refreshRequested), open(bar, &TopBar::openRepositoryRequested),
            clone(bar, &TopBar::cloneRequested), keys(bar, &TopBar::keybindingsRequested);
        actions.at(0)->trigger();
        actions.at(1)->trigger();
        actions.at(2)->trigger();
        actions.at(4)->trigger();
        QCOMPARE(QList<int>({int(refresh.count()), int(open.count()), int(clone.count()), int(keys.count())}),
                 QList<int>({1, 1, 1, 1}));

        // Back on the ordinary row at level 0 there is no More at all, and the
        // dot follows the folded set again.
        bar->setStacked(false);
        QCOMPARE(f.levelAt(bar->sizeHint().width()), 0);
        QVERIFY(!bar->moreButton()->isVisible());
        QCOMPARE(f.levelAt(f.widthForLevel(2)), 2);
        QCOMPARE(bar->moreButton()->markText(), QStringLiteral("•")); // Fetch's 2, folded
    }

    // The sync buttons spell out their labels in a wide window only (the
    // design's xl): below it they are icons, however much room the row has.
    void theSyncButtonsWearLabelsInAWideWindowOnly()
    {
        WindowFixture f = mainWindow(0, false, [](MainWindow *w) { w->resize(ui::space(1400), 800); });
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        settle();
        TopBar *bar = f.bar();
        QCOMPARE(bar->foldLevel(), 0);
        QVERIFY(bar->pullButton()->text().contains(QStringLiteral("Pull")));

        // One pixel under wide, the row would still fit them.
        f.window->resize(ui::space(1400) - 1, 800);
        settle();
        QVERIFY(bar->width() >= bar->sizeHint().width());
        QCOMPARE(bar->foldLevel(), 1);
        for (QToolButton *b : {bar->pullButton(), bar->pushButton(), bar->fetchButton(), bar->mergeButton()}) {
            QVERIFY(b->isVisible());
            QVERIFY(!b->text().contains(b->accessibleName()));
        }
        QVERIFY(!bar->moreButton()->isVisible());

        f.window->resize(ui::space(1400), 800);
        settle();
        QCOMPARE(bar->foldLevel(), 0);
    }

    // One top bar, above everything, in every layout — and the controls the
    // Mini rail used to double are gone from it.
    void theWindowPutsOneTopBarAboveTheBody()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        settle();

        QCOMPARE(f.window->findChildren<TopBar *>().size(), 1);
        QLayout *root = f.window->centralWidget()->layout();
        QCOMPARE(root->itemAt(0)->widget(), static_cast<QWidget *>(f.bar()));
        QVERIFY(f.bar()->isVisible());

        f.window->setPaneLayout(PaneLayout::Mini, false);
        settle();
        QVERIFY(f.bar()->isVisible());
        QVERIFY(f.bar()->layoutButton()->isChecked());
        // The rail is the miniatures and Refresh; its sync buttons moved out.
        QVERIFY(f.rail()->findChildren<BadgeButton *>().isEmpty());

        f.window->setDiffPaneVisible(false, false);
        settle();
        QVERIFY(f.bar()->isVisible());
        QCOMPARE(f.window->paneLayout(), PaneLayout::Docked); // hiding the diff leaves Mini
        QVERIFY(!f.bar()->diffToggle()->isChecked());
        f.window->setDiffPaneVisible(true, false);
        settle();

        // The tab counts what the changes list shows, and switches the page.
        QVERIFY(f.page()->proxy()->rowCount() > 0);
        QCOMPARE(f.bar()->changesCount(), f.page()->proxy()->rowCount());
        QTest::mouseClick(f.bar()->historyTab(), Qt::LeftButton);
        settle();
        QCOMPARE(f.window->mode(), MainWindow::HistoryMode);
        QVERIFY(!f.page()->isVisible());
        QTest::mouseClick(f.bar()->changesTab(), Qt::LeftButton);
        settle();
        QCOMPARE(f.window->mode(), MainWindow::CommitMode);
        QVERIFY(f.page()->isVisible());
    }

    // The count follows the proxy's own notifications, and the update does
    // nothing else: it writes to no model and leaves the current row alone.
    void theTabCountFollowsTheChangesProxyWithoutWritingToIt()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        settle();
        QSortFilterProxyModel *const proxy = f.page()->proxy();
        QAbstractItemModel *const source = proxy->sourceModel();
        QSignalSpy dataChanged(source, &QAbstractItemModel::dataChanged);
        QSignalSpy layoutChanged(source, &QAbstractItemModel::layoutChanged);
        QSignalSpy modelReset(source, &QAbstractItemModel::modelReset);

        // Two snapshots as close around the callback as the signals allow:
        // the first is the last thing the structural change does before it,
        // the second the first thing after it — so the change's own effects
        // are not laid at the top bar's door.
        struct Snapshot {
            int dataChanged = -1, layoutChanged = -1, reset = -1;
            QString current;
        };
        Snapshot before, after;
        const auto take = [&](Snapshot &s) {
            s.dataChanged = dataChanged.count();
            s.layoutChanged = layoutChanged.count();
            s.reset = modelReset.count();
            s.current = f.page()->table()->currentIndex().data(ChangesModel::PathRole).toString();
        };
        // The eye's filter change reaches the proxy as a layout change; both
        // connections are made after the window's, so `after` is taken once
        // the window's own slot has run.
        QObject::connect(proxy, &QAbstractItemModel::layoutAboutToBeChanged, f.window.get(), [&] { take(before); });
        QObject::connect(proxy, &QAbstractItemModel::layoutChanged, f.window.get(), [&] { take(after); });

        // The eye hides the unversioned files: rows leave the proxy without
        // the source model hearing a thing about it.
        QToolButton *eye = nullptr;
        for (QToolButton *b : f.page()->findChildren<QToolButton *>(QStringLiteral("iconButton")))
            if (b->isCheckable())
                eye = b;
        QVERIFY(eye);
        const int listed = proxy->rowCount();
        f.page()->selectPath(QStringLiteral("a.txt")); // a versioned row: the eye leaves it listed
        settle();
        eye->click();
        settle();
        QVERIFY(proxy->rowCount() < listed);
        QCOMPARE(f.bar()->changesCount(), proxy->rowCount());
        QCOMPARE(after.dataChanged, before.dataChanged);
        QCOMPARE(after.layoutChanged, before.layoutChanged);
        QCOMPARE(after.reset, before.reset);
        QCOMPARE(after.current, before.current);
        QVERIFY(!after.current.isEmpty());

        // And back: the rows come again, and so does the count.
        eye->click();
        settle();
        QCOMPARE(f.bar()->changesCount(), listed);
        QCOMPARE(f.bar()->changesCount(), proxy->rowCount());

        // A whole reload of the list is a reset, and the count follows that too.
        writeFixture(QDir(f.repo->root()).filePath(QStringLiteral("c.txt")), "c\n");
        f.page()->reload();
        settle();
        QCOMPARE(f.bar()->changesCount(), proxy->rowCount());
        QCOMPARE(f.bar()->changesCount(), listed + 1);
    }
};

UI_TEST(TopBarTest);

#include "topbar_test.moc"
