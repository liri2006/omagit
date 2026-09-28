// The Mini layout: the rail's commit tile and its badge, and the commit
// popover that opens from it, shares the page's message and controls,
// commits, closes and survives a refresh.
#include "UiTestCase.h"
#include "fixtures.h"

#include "../../src/DiffPane.h"
#include "../../src/MessageDialog.h"
#include "../../src/UiHelpers.h"

#include <QApplication>
#include <QItemSelectionModel>
#include <QListView>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QTimer>
#include <QWheelEvent>

namespace {

// MiniRail.cpp's corner badge (screens.js miniRail()): a 12 px square 1 px
// inside the tile's top right corner, in 12 px-base pixels.
constexpr int kTileBadgeSize = 12, kTileBadgeInset = 1;

// The badge the tile paints in its square's corner, for one digit.
QRect tileBadge(const QToolButton *tile)
{
    const QRect square = tileSquare(tile);
    const int side = ui::space(kTileBadgeSize), inset = ui::space(kTileBadgeInset);
    return QRect(square.right() + 1 - inset - side, square.top() + inset, side, side);
}

// A model whose connections can be counted, for the badge's own.
class CountedModel : public QStandardItemModel
{
public:
    using QStandardItemModel::QStandardItemModel;
    int dataReceivers() const
    {
        return receivers(SIGNAL(dataChanged(QModelIndex, QModelIndex, QList<int>)));
    }
    int layoutReceivers() const
    {
        return receivers(SIGNAL(layoutChanged(QList<QPersistentModelIndex>, QAbstractItemModel::LayoutChangeHint)));
    }
};

// A checkable row of the counted model.
QList<QStandardItem *> checkRow(const QString &name, bool checked)
{
    auto *item = new QStandardItem(name);
    item->setCheckable(true);
    item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    return {item};
}

// The colour a fill of `color` at `alpha` leaves on `background`.
QColor over(const QColor &background, const QColor &color, qreal alpha)
{
    return QColor::fromRgbF(background.redF() * (1 - alpha) + color.redF() * alpha,
                            background.greenF() * (1 - alpha) + color.greenF() * alpha,
                            background.blueF() * (1 - alpha) + color.blueF() * alpha);
}

// Clicks the default button of the next modal message dialog, from inside
// the event loop its exec() runs. `clicked` says whether one came.
void clickNextMessageBox(bool *clicked)
{
    auto *timer = new QTimer;
    timer->setInterval(20);
    QObject::connect(timer, &QTimer::timeout, timer, [timer, clicked] {
        auto *box = qobject_cast<MessageDialog *>(QApplication::activeModalWidget());
        if (!box)
            return;
        timer->stop();
        timer->deleteLater();
        *clicked = true;
        QAbstractButton *button = box->defaultButton();
        // A real click, in the box's own window: the card must not take it
        // for a press outside itself.
        QTest::mouseClick(button, Qt::LeftButton);
    });
    timer->start();
}

} // namespace

class MiniTest : public UiTestCase
{
    Q_OBJECT
private slots:
    // The tile, its separator and its gaps are the commit view's: gone in the
    // history, and with the whole rail in the Docked layout.
    void theCommitTileShowsInTheMiniCommitViewOnly()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        f.window->setPaneLayout(PaneLayout::Mini, false);
        settle();
        QToolButton *tile = f.tile();
        QWidget *section = tile->parentWidget();
        QVERIFY(tile->isVisible());
        QCOMPARE(tile->objectName(), QStringLiteral("commitTile"));
        QCOMPARE(tile->accessibleName(), QStringLiteral("Commit"));
        QCOMPARE(tile->toolTip(), QStringLiteral("Commit the checked files (Ctrl+Enter)"));
        QCOMPARE(tile->focusPolicy(), Qt::NoFocus);
        QCOMPARE(tile->cursor().shape(), Qt::PointingHandCursor);
        // The tile is the rail's last thing, its bottom the rail's.
        QCOMPARE(rectIn(tile, f.rail()).bottom(), f.rail()->height() - 1);

        f.window->setMode(MainWindow::HistoryMode);
        settle();
        QVERIFY(!tile->isVisible());
        QVERIFY(!section->isVisible()); // the separator and both gaps with it
        // Nothing of it is left under Refresh, not even the rail's spacing.
        QCOMPARE(rectIn(f.railRefresh(), f.rail()).bottom(), f.rail()->height() - 1);

        f.window->setMode(MainWindow::CommitMode);
        settle();
        QVERIFY(tile->isVisible());

        f.window->setPaneLayout(PaneLayout::Docked, false);
        settle();
        QVERIFY(!f.rail()->isVisible());
        QVERIFY(!tile->isVisible());
    }

    // One widget, the rail's width and a badge's rise taller than its square:
    // the square at its bottom and centred, the design's 8 px under the
    // separator, which is 8 px under Refresh; the badge inside the widget.
    void theCommitTileKeepsItsSquareAndBadgeInside()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        f.window->setPaneLayout(PaneLayout::Mini, false);
        settle();
        MiniRail *rail = f.rail();
        QToolButton *tile = f.tile();
        QCOMPARE(rail->width(), MiniRail::railWidth());
        QCOMPARE(MiniRail::railWidth(), ui::space(ui::box::tile));
        QCOMPARE(MiniRail::railWidth(), ui::space(40)); // one tile wide
        QCOMPARE(tile->size(), QSize(MiniRail::railWidth(), ui::space(40)));
        QCOMPARE(rectIn(tile, rail).x(), 0);

        const QRect square = tileSquare(tile).translated(rectIn(tile, rail).topLeft());
        QCOMPARE(square.bottom(), rail->height() - 1); // bottom-aligned
        // Centred: the room on the left is the room on the right, give or take
        // the odd pixel.
        QVERIFY(qAbs(square.left() - (rail->width() - 1 - square.right())) <= 1);

        QWidget *rule = tileRule(f);
        QVERIFY(rule);
        const QRect ruleRect = rectIn(rule, rail);
        QCOMPARE(square.top() - (ruleRect.bottom() + 1), ui::space(8));
        QCOMPARE(ruleRect.top() - (rectIn(f.railRefresh(), rail).bottom() + 1), ui::space(8));
        QVERIFY(tile->rect().contains(tileBadge(tile)));

        // A long hash is elided to the rail's scaled width.
        f.window->setMode(MainWindow::HistoryMode);
        rail->setCommitLabel(QStringLiteral("0123456789abcdef0123456789abcdef"), QStringLiteral("subject"));
        settle();
        QLabel *hash = rail->findChild<QLabel *>(QStringLiteral("dimLabel"));
        QVERIFY(hash);
        QVERIFY(hash->fontMetrics().horizontalAdvance(hash->text()) <= MiniRail::railWidth() - 2);
        QVERIFY(hash->text().endsWith(QStringLiteral("…")));
    }

    // Check-all, Ctrl+click, Space, the eye, a reload, sorting and another
    // source: the badge counts what the page counts, whatever changed it.
    void theCommitTileBadgeCountsTheCheckedFiles()
    {
        WindowFixture f = mainWindow(10); // a.txt, u1.txt and ten more: twelve files
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        f.window->setPaneLayout(PaneLayout::Mini, false);
        settle();
        MiniRail *rail = f.rail();
        CommitPage *page = f.page();
        const auto agrees = [&] { return rail->checkedCount() == page->commitControls().checked; };
        QCOMPARE(page->proxy()->rowCount(), 12);
        QCOMPARE(rail->checkedCount(), 1); // the modified file
        QVERIFY(agrees());

        // Check-all through the window's own shortcut: two digits, and the
        // page's button says the same number.
        QTest::keyClick(rail->list(), Qt::Key_Space, Qt::ControlModifier | Qt::ShiftModifier);
        settle();
        QCOMPARE(rail->checkedCount(), 12);
        QVERIFY(agrees());
        QCOMPARE(page->commitControls().commitName, QStringLiteral("Commit 12 files"));
        QTest::keyClick(rail->list(), Qt::Key_Space, Qt::ControlModifier | Qt::ShiftModifier);
        settle();
        QCOMPARE(rail->checkedCount(), 0);
        QVERIFY(agrees());

        // Ctrl+click one miniature, Space another.
        QListView *list = rail->list();
        const QModelIndex first = list->model()->index(0, ChangesModel::Check);
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::ControlModifier, list->visualRect(first).center());
        settle();
        QCOMPARE(rail->checkedCount(), 1);
        QVERIFY(agrees());
        list->setCurrentIndex(list->model()->index(1, ChangesModel::Check));
        QTest::keyClick(list, Qt::Key_Space);
        settle();
        QCOMPARE(rail->checkedCount(), 2);
        QVERIFY(agrees());

        // The eye hides the unversioned files and unticks them; showing them
        // again leaves them unticked.
        page->unversionedButton()->click();
        settle();
        QCOMPARE(rail->checkedCount(), 1);
        QVERIFY(agrees());
        page->unversionedButton()->click();
        settle();
        QCOMPARE(rail->checkedCount(), 1);
        QVERIFY(agrees());

        // A reload with a new file: the ticks stay, the newcomer is not ticked.
        page->toggleAllChecked();
        settle();
        QCOMPARE(rail->checkedCount(), 12);
        QVERIFY(writeFixture(QDir(f.repo->root()).filePath(QStringLiteral("zz.txt")), "zz\n"));
        page->reload();
        settle();
        QCOMPARE(page->proxy()->rowCount(), 13);
        QCOMPARE(rail->checkedCount(), 12);
        QVERIFY(agrees());

        // Sorting is a layout change; the count stays what it is.
        page->proxy()->sort(ChangesModel::Name, Qt::DescendingOrder);
        settle();
        QCOMPARE(rail->checkedCount(), 12);
        QVERIFY(agrees());

        // The history's files are another source, with no check marks.
        f.window->setMode(MainWindow::HistoryMode);
        settle();
        QCOMPARE(rail->checkedCount(), 0);
        f.window->setMode(MainWindow::CommitMode);
        settle();
        QCOMPARE(rail->checkedCount(), 12);
        QVERIFY(agrees());
    }

    // The badge listens to the five notifications that can change a count,
    // once each however often the same source is set, and to the source of
    // the moment only.
    void theCommitTileBadgeFollowsItsSourceAlone()
    {
        MiniRail rail;
        CountedModel model;
        model.appendRow(checkRow(QStringLiteral("a"), true));
        model.appendRow(checkRow(QStringLiteral("b"), false));
        QItemSelectionModel selection(&model);
        const int dataBefore = model.dataReceivers();
        const int layoutBefore = model.layoutReceivers();

        rail.setSource(&model, &selection);
        QCOMPARE(rail.checkedCount(), 1);
        const int dataWith = model.dataReceivers();
        const int layoutWith = model.layoutReceivers();
        QVERIFY(dataWith > dataBefore);
        rail.setSource(&model, &selection);
        QCOMPARE(model.dataReceivers(), dataWith);
        QCOMPARE(model.layoutReceivers(), layoutWith);

        model.item(1)->setCheckState(Qt::Checked); // dataChanged
        QCOMPARE(rail.checkedCount(), 2);
        model.appendRow(checkRow(QStringLiteral("c"), true)); // rowsInserted
        QCOMPARE(rail.checkedCount(), 3);
        model.removeRow(0); // rowsRemoved
        QCOMPARE(rail.checkedCount(), 2);
        {
            // A tick nobody announced, then a layout change: the count is read again.
            QSignalBlocker quiet(&model);
            model.item(0)->setCheckState(Qt::Unchecked);
        }
        QCOMPARE(rail.checkedCount(), 2);
        emit model.layoutChanged();
        QCOMPARE(rail.checkedCount(), 1);
        model.clear(); // modelReset
        QCOMPARE(rail.checkedCount(), 0);

        // Another source: the old one's notifications no longer reach the badge.
        model.appendRow(checkRow(QStringLiteral("d"), true));
        QCOMPARE(rail.checkedCount(), 1);
        CountedModel other;
        other.appendRow(checkRow(QStringLiteral("x"), true));
        other.appendRow(checkRow(QStringLiteral("y"), true));
        QItemSelectionModel otherSelection(&other);
        rail.setSource(&other, &otherSelection);
        QCOMPARE(rail.checkedCount(), 2);
        // The badge's connections went with the old source. (The list view
        // keeps a layout connection of Qt's own there; it is not the badge's,
        // which the notifications below prove.)
        QCOMPARE(model.dataReceivers(), dataBefore);
        QVERIFY(model.layoutReceivers() < layoutWith);
        QVERIFY(model.layoutReceivers() >= layoutBefore);
        model.appendRow(checkRow(QStringLiteral("e"), true));
        QCOMPARE(rail.checkedCount(), 2);
        model.item(0)->setCheckState(Qt::Unchecked);
        emit model.layoutChanged();
        model.clear();
        QCOMPARE(rail.checkedCount(), 2);
    }

    // One digit fits the miniatures' corner square; two and three widen it
    // leftwards, keeping its right edge and its top, as painted and as the
    // rail reports it.
    void theCommitTileBadgeWidensIntoAPillForLongerCounts()
    {
        MiniRail rail;
        QStandardItemModel model;
        QItemSelectionModel selection(&model);
        rail.setSource(&model, &selection);
        QToolButton *tile = rail.commitTile();
        QCOMPARE(rail.commitBadgeRect(), QRect()); // nothing checked, no badge

        const QRect square = tileBadge(tile);
        const QColor accent = OmarchyTheme::instance()->accent();
        // The painted badge's run of accent pixels one row under its top, which
        // no digit reaches: [first, last].
        const auto paintedRun = [&] {
            const QImage image = tile->grab().toImage();
            const int y = square.top() + 1;
            int first = -1, last = -1;
            for (int x = 0; x < image.width(); ++x)
                if (closeTo(image.pixelColor(x, y), accent)) {
                    if (first < 0)
                        first = x;
                    last = x;
                }
            return std::make_pair(first, last);
        };
        const auto checkRows = [&](int count) {
            while (model.rowCount() < count)
                model.appendRow(checkRow(QStringLiteral("f%1").arg(model.rowCount()), true));
            QCOMPARE(rail.checkedCount(), count);
        };

        checkRows(1);
        QCOMPARE(rail.commitBadgeRect(), square);
        const auto one = paintedRun();
        QVERIFY(one.first >= 0);
        QVERIFY(square.contains(QPoint(one.first, square.top() + 1)));

        auto previous = one;
        for (const int count : {12, 123}) {
            checkRows(count);
            const QRect badge = rail.commitBadgeRect();
            QVERIFY2(badge.width() > ui::space(kTileBadgeSize), qPrintable(QString::number(badge.width())));
            QCOMPARE(badge.height(), ui::space(kTileBadgeSize));
            QCOMPARE(badge.right(), square.right());
            QCOMPARE(badge.top(), square.top());
            QVERIFY(tile->rect().contains(badge));
            const auto run = paintedRun();
            QCOMPARE(run.second, one.second); // the right edge stays put...
            QVERIFY(run.first < previous.first); // ...and the pill grows leftwards
            QVERIFY(badge.contains(QPoint(run.first, badge.top() + 1)));
            previous = run;
        }
    }

    // Ctrl+click ticks the clicked miniature and hands the list the keyboard,
    // leaving the current file where it was; Space goes on from there.
    void aCtrlClickOnTheRailFocusesItsList()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        f.window->setPaneLayout(PaneLayout::Mini, false);
        settle();
        QVERIFY(activate(f.window.get()));
        QListView *list = f.rail()->list();
        QAbstractItemModel *model = list->model();
        list->setCurrentIndex(model->index(0, ChangesModel::Check));
        f.diff()->setFocus();
        QTRY_VERIFY(f.diff()->hasFocus());
        const QModelIndex second = model->index(1, ChangesModel::Check);
        const int before = second.data(Qt::CheckStateRole).toInt();
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::ControlModifier, list->visualRect(second).center());
        settle();
        QTRY_VERIFY(list->hasFocus());
        QCOMPARE(list->currentIndex().row(), 0);
        QVERIFY(second.data(Qt::CheckStateRole).toInt() != before);

        const QModelIndex current = model->index(0, ChangesModel::Check);
        const int currentBefore = current.data(Qt::CheckStateRole).toInt();
        QTest::keyClick(list, Qt::Key_Space);
        settle();
        QVERIFY(current.data(Qt::CheckStateRole).toInt() != currentBefore);
        QVERIFY(second.data(Qt::CheckStateRole).toInt() != before); // the other one stays ticked
    }

    // The tile, Ctrl+Return, the keypad's Ctrl+Enter and the slot by name all
    // open the card with the keyboard in its message box and the tile lit;
    // the slot a second time changes nothing but the focus.
    void theCommitPopoverOpensFromTheTileTheKeysAndItsSlot()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        f.window->setPaneLayout(PaneLayout::Mini, false);
        settle();
        QVERIFY(activate(f.window.get()));
        CommitPopover *card = f.popover();
        QVERIFY(card);
        QVERIFY(!card->isVisible());
        QCOMPARE(card->parentWidget(), f.host());
        QVERIFY(!card->isWindow());

        const OmarchyTheme *theme = OmarchyTheme::instance();
        const auto tileFill = [&f] {
            const QImage image = f.rail()->grab().toImage();
            const QRect square = tileSquare(f.tile()).translated(rectIn(f.tile(), f.rail()).topLeft());
            return image.pixelColor(square.left() + 4, square.bottom() - 4);
        };
        const QColor resting = over(theme->window(), theme->accent(), 0.08);
        const QColor lit = over(theme->window(), theme->accent(), 0.18);
        QVERIFY2(closeTo(tileFill(), resting), qPrintable(tileFill().name()));

        const auto isOpen = [&] {
            return card->isVisible() && f.window->focusWidget() == card->editor();
        };

        // The keys, from the rail.
        QTest::keyClick(f.rail()->list(), Qt::Key_Return, Qt::ControlModifier);
        settle();
        QTRY_VERIFY(isOpen());
        QVERIFY2(closeTo(tileFill(), lit), qPrintable(tileFill().name()));
        card->dismiss();
        settle();
        QVERIFY(!card->isVisible());
        QVERIFY2(closeTo(tileFill(), resting), qPrintable(tileFill().name()));

        QTest::keyClick(f.rail()->list(), Qt::Key_Enter, Qt::ControlModifier | Qt::KeypadModifier);
        settle();
        QTRY_VERIFY(isOpen());
        card->dismiss();
        settle();

        // The tile.
        QTest::mouseClick(f.tile(), Qt::LeftButton);
        settle();
        QTRY_VERIFY(isOpen());
        card->dismiss();
        settle();

        // The slot, by name as main() calls it, twice.
        QVERIFY(QMetaObject::invokeMethod(f.window.get(), "showCommitPopover"));
        settle();
        QTRY_VERIFY(isOpen());
        const QRect geometry = card->geometry();
        f.rail()->list()->setFocus();
        QVERIFY(QMetaObject::invokeMethod(f.window.get(), "showCommitPopover"));
        settle();
        QTRY_VERIFY(isOpen());
        QCOMPARE(card->geometry(), geometry);
        QCOMPARE(f.window->findChildren<CommitPopover *>().size(), 1);
    }

    // Beside the rail, 8 px clear of it, as wide as the design allows or the
    // window's right margin leaves; its bottom on the tile's bottom however
    // the window is resized or the text grows.
    void theCommitPopoverHangsBesideTheTile()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(f.openCard());
        CommitPopover *card = f.popover();
        QWidget *host = f.host();
        // The window's margin, which the card keeps on its right.
        const auto hostMargins = [&] {
            const int m = ui::windowMargin(f.window.get());
            return QMargins(m, m, m, m);
        };
        const auto anchored = [&] {
            const QRect cardRect = card->geometry();
            const QRect tileRect = rectIn(f.tile(), host);
            const QRect rail = f.rail()->geometry();
            const int left = rail.x() + rail.width() + ui::space(8);
            return cardRect.x() == left && cardRect.y() + cardRect.height() == tileRect.y() + tileRect.height()
                && cardRect.width() == qMin(ui::space(360), host->width() - hostMargins().right() - left);
        };
        QVERIFY(anchored());
        QCOMPARE(card->width(), ui::space(360));
        // The frame's coordinates (screens.js commitPopover()): the message
        // box 12 in and under the 12 of padding, the 24 px MESSAGE row and
        // its 8; 80 tall at rest.
        QCOMPARE(rectIn(card->editor(), card).topLeft(),
                 QPoint(ui::space(ui::pad::popover), ui::space(ui::pad::popover + ui::box::row + ui::gap::header)));
        QCOMPARE(card->editor()->height(), ui::space(80));

        // A narrow window clamps the width; the hint is elided rather than
        // making the card wider. The stacked top bar keeps the window wider
        // than that; a minimum of the test's own lets it be squeezed anyway
        // (the Mini layout is the stacked Diff tab there, rail and card kept).
        f.window->setMinimumSize(1, 1);
        f.window->resize(400, 800);
        settle();
        QTRY_VERIFY(anchored());
        QVERIFY(card->width() < ui::space(360));
        QVERIFY(card->hintLabel()->text() != card->hintText());
        QVERIFY(card->hintLabel()->text().endsWith(QStringLiteral("…")));
        QVERIFY(card->commitButton()->geometry().right() < card->width());
        // Leaving the stacked width closes the card; the Mini layout opens it
        // again.
        f.window->resize(1200, 640);
        settle();
        QVERIFY(!card->isVisible());
        QMetaObject::invokeMethod(f.window.get(), "showCommitPopover");
        settle();
        QVERIFY(card->isVisible());
        QTRY_VERIFY(anchored());
        // Elided to the label's width again, however much of it fits.
        QCOMPARE(card->hintLabel()->text(),
                 card->hintLabel()->fontMetrics().elidedText(card->hintText(), Qt::ElideRight,
                                                             card->hintLabel()->width()));

        // Growth moves the top up and leaves the bottom where it is.
        const QRect before = card->geometry();
        for (int line = 0; line < 8; ++line) {
            QTest::keyClicks(card->editor(), QStringLiteral("line %1").arg(line));
            QTest::keyClick(card->editor(), Qt::Key_Return);
        }
        settle();
        QTRY_VERIFY(card->editor()->height() > ui::space(80));
        QTRY_VERIFY(anchored());
        QVERIFY(card->y() < before.y());
        QCOMPARE(card->geometry().bottom(), before.bottom());
        // ...up to a third of the window.
        for (int line = 0; line < 30; ++line)
            QTest::keyClick(card->editor(), Qt::Key_Return);
        settle();
        QTRY_COMPARE(card->editor()->height(), qMax(ui::space(80), host->height() / 3));
        QTRY_VERIFY(anchored());
    }

    // One document for both boxes: typed text, the agent's streamed text and
    // the undo stack are the same wherever they are looked at.
    void theCommitPopoverSharesThePagesMessage()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(f.openCard());
        MessageEdit *card = f.popover()->editor();
        MessageEdit *page = f.pageEditor();
        QVERIFY(card != page);
        QCOMPARE(card->document(), f.page()->messageDocument());
        QCOMPARE(page->document(), f.page()->messageDocument());

        QTest::keyClicks(card, QStringLiteral("Typed"));
        QCOMPARE(page->toPlainText(), QStringLiteral("Typed"));
        page->replaceText(QStringLiteral("Streamed answer"));
        QCOMPARE(card->toPlainText(), QStringLiteral("Streamed answer"));
        card->undo();
        QCOMPARE(page->toPlainText(), QStringLiteral("Typed"));
        page->redo();
        QCOMPARE(card->toPlainText(), QStringLiteral("Streamed answer"));
    }

    // The document wraps for the box on screen: the page's in the Docked
    // layout, the card's in the Mini one, each as a box built fresh at its
    // width would — also after the hidden one's text was replaced, after a
    // theme change and after coming back.
    void theSharedMessageWrapsForTheBoxOnScreen()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        MessageEdit *page = f.pageEditor();
        const QString text = longParagraph() + QLatin1Char(' ') + longParagraph();
        page->replaceText(text);
        settle();
        QCOMPARE(page->contentHeight(), freshContentHeight(page, text));
        const int docked = page->contentHeight();

        QVERIFY(f.openCard());
        MessageEdit *card = f.popover()->editor();
        QVERIFY(card->width() < page->width());
        settle();
        QTRY_COMPARE(card->contentHeight(), freshContentHeight(card, text));
        QVERIFY(card->contentHeight() > docked); // narrower, so more lines

        // The hidden box's edit, and a theme change that reaches both boxes.
        const QString longer = text + QLatin1Char(' ') + longParagraph();
        page->replaceText(longer);
        settle();
        QTRY_COMPARE(card->contentHeight(), freshContentHeight(card, longer));
        emit OmarchyTheme::instance()->changed();
        settle();
        QTRY_COMPARE(card->contentHeight(), freshContentHeight(card, longer));

        f.window->setPaneLayout(PaneLayout::Docked, false);
        settle();
        QTRY_COMPARE(page->contentHeight(), freshContentHeight(page, longer));
        page->replaceText(text);
        settle();
        QTRY_COMPARE(page->contentHeight(), docked);
    }

    // The card says what the page's controls say: the Commit button's
    // wording, name, tip and state, the amend box both ways, the hint's
    // counts, and a merge ruling amending out.
    void theCommitPopoverMirrorsThePagesControls()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(activate(f.window.get()));
        QVERIFY(f.openCard());
        CommitPopover *card = f.popover();
        CommitPage *page = f.page();
        const auto mirrors = [&] {
            const CommitPage::CommitControls c = page->commitControls();
            QPushButton *commit = card->commitButton();
            QCheckBox *amend = card->amendBox();
            QToolButton *generate = card->editor()->cornerButton();
            return commit->text() == c.commitText && commit->accessibleName() == c.commitName
                && commit->toolTip() == c.commitTip && commit->isEnabled() == c.commitEnabled
                && amend->isChecked() == c.amendChecked && amend->isEnabled() == c.amendEnabled
                && amend->toolTip() == c.amendTip && generate->text() == c.generateText
                && generate->toolTip() == c.generateTip;
        };
        const QString space = QStringLiteral(" · Space on a tile toggles it");
        QVERIFY(mirrors());
        QCOMPARE(card->commitButton()->accessibleName(), QStringLiteral("Commit 1 file"));
        QCOMPARE(card->hintText(), QStringLiteral("1 / 2 files selected") + space);

        page->toggleAllChecked();
        settle();
        QVERIFY(mirrors());
        QCOMPARE(card->commitButton()->accessibleName(), QStringLiteral("Commit 2 files"));
        QCOMPARE(card->hintText(), QStringLiteral("2 / 2 files selected") + space);
        page->toggleAllChecked();
        settle();
        QVERIFY(mirrors());
        QVERIFY(!card->commitButton()->isEnabled());
        QCOMPARE(card->hintText(), QStringLiteral("0 / 2 files selected") + space);
        page->unversionedButton()->click();
        settle();
        QCOMPARE(card->hintText(), QStringLiteral("0 / 1 file selected") + space);
        page->unversionedButton()->click();
        page->toggleAllChecked();
        settle();

        // Amend from the card, then back from the window's shortcut.
        QTest::mouseClick(card->amendBox(), Qt::LeftButton, {}, QPoint(6, card->amendBox()->height() / 2));
        settle();
        QVERIFY(page->commitControls().amendChecked);
        QVERIFY(mirrors());
        QCOMPARE(card->commitButton()->accessibleName(), QStringLiteral("Amend"));
        QVERIFY(card->isVisible());
        QTest::keyClick(card->editor(), Qt::Key_A, Qt::ControlModifier | Qt::ShiftModifier);
        settle();
        QVERIFY(!page->commitControls().amendChecked);
        QVERIFY(!card->amendBox()->isChecked());
        QVERIFY(mirrors());

        // A merge in progress rules amending out.
        const Commit head = f.repo->headCommit();
        MergeState merge;
        merge.inProgress = true;
        merge.source = QStringLiteral("feature");
        page->setMergeState(merge, head);
        settle();
        QVERIFY(mirrors());
        QVERIFY(!card->amendBox()->isEnabled());
        QCOMPARE(card->amendBox()->toolTip(), QStringLiteral("Not while a merge is in progress"));
        QCOMPARE(card->commitButton()->accessibleName(), QStringLiteral("Commit merge"));
        page->setMergeState(MergeState(), head);
        settle();
        QVERIFY(mirrors());
        QVERIFY(card->amendBox()->isEnabled());
    }

    // The generate button in the card's corner wears what the page's wears,
    // when the card opens and after every change the page announces — also
    // after asking for a message with no agent installed.
    void theCommitPopoverMirrorsTheGenerateButton()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(f.openCard());
        CommitPage *page = f.page();
        QToolButton *generate = f.popover()->editor()->cornerButton();
        const auto mirrors = [&] {
            const CommitPage::CommitControls c = page->commitControls();
            return generate->text() == c.generateText && generate->toolTip() == c.generateTip
                && c.generateText == f.pageEditor()->cornerButton()->text()
                && c.generateTip == f.pageEditor()->cornerButton()->toolTip();
        };
        QVERIFY(mirrors());
        QCOMPARE(generate->focusPolicy(), Qt::NoFocus);
        QCOMPARE(f.popover()->agentButton()->focusPolicy(), Qt::NoFocus);
        QCOMPARE(f.popover()->agentButton()->toolTip(), CommitPage::agentButtonTip());

        QSignalSpy announced(page, &CommitPage::commitControlsChanged);
        page->toggleAllChecked();
        settle();
        QVERIFY(announced.count() > 0);
        QVERIFY(mirrors());

        {
            // Only git on PATH: no agent is found, so nothing is started.
            ScopedEnv onlyGit("PATH", f.tools->path().toUtf8());
            QSignalSpy status(page, &CommitPage::statusMessage);
            page->generateMessage();
            settle();
            QCOMPARE(status.count(), 1);
            QVERIFY(status.first().first().toString().startsWith(QStringLiteral("Neither claude nor codex")));
        }
        QVERIFY(mirrors());
        QCOMPARE(generate->text(), ui::icon(ui::kSparkle, QStringLiteral("✨")).trimmed());
    }

    // Commit from the card's button and from Ctrl+Return: HEAD moves, the
    // shared message is gone, the card closes and the rail has the keyboard.
    // An amend closes it too; with nothing left the button is off.
    void theCommitPopoverCommits()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(activate(f.window.get()));
        CommitPopover *card = f.popover();
        QListView *list = f.rail()->list();
        QVERIFY(f.openCard());

        QString head = f.head();
        QTest::keyClicks(card->editor(), QStringLiteral("Commit from the card"));
        QTest::mouseClick(card->commitButton(), Qt::LeftButton);
        settle();
        QVERIFY(!card->isVisible());
        QVERIFY(f.head() != head);
        QCOMPARE(f.repo->headCommit().subject, QStringLiteral("Commit from the card"));
        QVERIFY(f.page()->messageDocument()->isEmpty());
        QTRY_COMPARE(f.window->focusWidget(), static_cast<QWidget *>(list));

        // Ctrl+Return opens the card from the rail, and commits from it.
        head = f.head();
        QTest::keyClick(list, Qt::Key_Return, Qt::ControlModifier);
        settle();
        QVERIFY(card->isVisible());
        f.page()->toggleAllChecked(); // the unversioned file
        QTest::keyClicks(card->editor(), QStringLiteral("Second from the keys"));
        QTest::keyClick(card->editor(), Qt::Key_Return, Qt::ControlModifier);
        settle();
        QVERIFY(!card->isVisible());
        QCOMPARE(f.repo->headCommit().subject, QStringLiteral("Second from the keys"));
        QVERIFY(f.page()->messageDocument()->isEmpty());
        QTRY_COMPARE(f.window->focusWidget(), static_cast<QWidget *>(list));

        // Nothing left to commit: the button is off and the keys do nothing.
        head = f.head();
        QVERIFY(QMetaObject::invokeMethod(f.window.get(), "showCommitPopover"));
        settle();
        QCOMPARE(card->hintText(), QStringLiteral("No changes to commit"));
        QVERIFY(!card->commitButton()->isEnabled());
        QVERIFY(!card->commit());
        QTest::keyClick(card->editor(), Qt::Key_Return, Qt::ControlModifier);
        settle();
        QVERIFY(card->isVisible());
        QCOMPARE(f.head(), head);

        // Amending the last commit closes the card as well.
        QTest::mouseClick(card->amendBox(), Qt::LeftButton, {}, QPoint(6, card->amendBox()->height() / 2));
        settle();
        QTRY_VERIFY(card->commitButton()->isEnabled()); // the amended commit's files are ticked
        QCOMPARE(card->editor()->toPlainText(), QStringLiteral("Second from the keys"));
        QTest::keyClick(card->editor(), Qt::Key_End, Qt::ControlModifier);
        QTest::keyClicks(card->editor(), QStringLiteral(", amended"));
        QTest::keyClick(card->editor(), Qt::Key_Return, Qt::ControlModifier);
        settle();
        QVERIFY(!card->isVisible());
        QVERIFY(f.head() != head);
        QCOMPARE(f.repo->headCommit().subject, QStringLiteral("Second from the keys, amended"));
        QVERIFY(!f.page()->commitControls().amendChecked);
        QTRY_COMPARE(f.window->focusWidget(), static_cast<QWidget *>(list));
    }

    // No message: the page's warning comes up and is answered in its own
    // window, and the card stays with the keyboard back in its box.
    void theCommitPopoverStaysWhenThePageDoesNotCommit()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(activate(f.window.get()));
        QVERIFY(f.openCard());
        CommitPopover *card = f.popover();
        const QString head = f.head();
        bool answered = false;
        clickNextMessageBox(&answered);
        QVERIFY(!card->commit());
        QVERIFY(answered);
        settle();
        QVERIFY(card->isVisible());
        QCOMPARE(f.head(), head);
        QTRY_COMPARE(f.window->focusWidget(), static_cast<QWidget *>(card->editor()));
    }

    // Escape from anything on the card, a second click on the tile, a press
    // outside it, the history, Ctrl+B and another repository close it —
    // with the rail's list taking the keyboard where the layout stays Mini.
    void theCommitPopoverCloses()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(activate(f.window.get()));
        CommitPopover *card = f.popover();
        QListView *list = f.rail()->list();
        QVERIFY(f.openCard());
        const auto reopen = [&] {
            QVERIFY(QMetaObject::invokeMethod(f.window.get(), "showCommitPopover"));
            settle();
            QVERIFY(card->isVisible());
        };

        for (QWidget *w : QList<QWidget *>{card->editor(), card->amendBox(), card->commitButton()}) {
            reopen();
            w->setFocus();
            QTRY_COMPARE(f.window->focusWidget(), w);
            QTest::keyClick(w, Qt::Key_Escape);
            settle();
            QVERIFY2(!card->isVisible(), w->metaObject()->className());
            QTRY_COMPARE(f.window->focusWidget(), static_cast<QWidget *>(list));
        }

        // The tile toggles.
        QTest::mouseClick(f.tile(), Qt::LeftButton);
        settle();
        QVERIFY(card->isVisible());
        QTest::mouseClick(f.tile(), Qt::LeftButton);
        settle();
        QVERIFY(!card->isVisible());
        QTRY_COMPARE(f.window->focusWidget(), static_cast<QWidget *>(list));

        // A press on the diff closes it, and still reaches the diff.
        reopen();
        DiffView *diff = f.diff();
        clickAt(f.window.get(), diff->mapTo(f.window.get(), diff->rect().center()));
        settle();
        QVERIFY(!card->isVisible());
        QTRY_VERIFY(diff->hasFocus() || diff->isAncestorOf(f.window->focusWidget()));

        // The history.
        reopen();
        QTest::mouseClick(f.bar()->historyTab(), Qt::LeftButton);
        settle();
        QVERIFY(!card->isVisible());
        QCOMPARE(f.window->mode(), MainWindow::HistoryMode);
        // Neither the slot nor the keys open it there.
        QVERIFY(QMetaObject::invokeMethod(f.window.get(), "showCommitPopover"));
        settle();
        QVERIFY(!card->isVisible());
        QTest::keyClick(list, Qt::Key_Return, Qt::ControlModifier);
        settle();
        QVERIFY(!card->isVisible());
        f.window->setMode(MainWindow::CommitMode);
        settle();

        // Ctrl+B: the Docked layout, where the keys press the page's button.
        reopen();
        QTest::keyClick(card->editor(), Qt::Key_B, Qt::ControlModifier);
        settle();
        QVERIFY(!card->isVisible());
        QCOMPARE(f.window->paneLayout(), PaneLayout::Docked);
        QVERIFY(QMetaObject::invokeMethod(f.window.get(), "showCommitPopover"));
        settle();
        QVERIFY(!card->isVisible());
        f.window->setPaneLayout(PaneLayout::Mini, false);
        settle();

        // The same repository again is no switch; one that is not a
        // repository is none either; another one closes the card.
        reopen();
        QVERIFY(f.window->openRepository(f.repo->root()));
        settle();
        QVERIFY(card->isVisible());
        QTemporaryDir plain;
        QVERIFY(plain.isValid());
        bool answered = false;
        clickNextMessageBox(&answered);
        QVERIFY(!f.window->openRepository(plain.path()));
        QVERIFY(answered);
        settle();
        QVERIFY(card->isVisible());
        QTemporaryDir other;
        QVERIFY(other.isValid());
        QVERIFY(git(other.path(), {"init", "-q", "-b", "main"}));
        QVERIFY(commit(other.path(), QStringLiteral("other"), 1));
        QVERIFY(f.window->openRepository(other.path()));
        settle();
        QVERIFY(!card->isVisible());
    }

    // Working the rail, the wheel over the diff, Refresh and another window
    // leave the card open.
    void theCommitPopoverStaysOpenWhileTheRailIsWorked()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(activate(f.window.get()));
        QVERIFY(f.openCard());
        CommitPopover *card = f.popover();
        QListView *list = f.rail()->list();
        QAbstractItemModel *model = list->model();
        QTest::keyClicks(card->editor(), QStringLiteral("Kept"));

        // Selecting another miniature shows its diff.
        const QModelIndex second = model->index(1, ChangesModel::Check);
        const QString path = second.data(ChangesModel::PathRole).toString();
        const QImage diffBefore = f.diff()->grab().toImage();
        QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, list->visualRect(second).center());
        settle();
        QVERIFY(card->isVisible());
        bool ok = false;
        QCOMPARE(f.page()->currentChange(&ok).path, path);
        QVERIFY(f.diff()->grab().toImage() != diffBefore); // another file's diff

        // Ticking a miniature updates the hint.
        const QString hint = card->hintText();
        QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::ControlModifier, list->visualRect(second).center());
        settle();
        QVERIFY(card->isVisible());
        QVERIFY(card->hintText() != hint);

        // The wheel over the diff.
        QWidget *viewport = f.diff()->viewport();
        const QPoint centre = viewport->rect().center();
        QWheelEvent wheel(centre, viewport->mapToGlobal(centre), QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
                          Qt::NoScrollPhase, false);
        QApplication::sendEvent(viewport, &wheel);
        settle();
        QVERIFY(card->isVisible());

        // The rail's Refresh.
        QTest::mouseClick(f.railRefresh(), Qt::LeftButton);
        settle();
        QVERIFY(card->isVisible());
        QCOMPARE(card->editor()->toPlainText(), QStringLiteral("Kept"));

        // Another window, and a dialog of this one: separate windows both.
        QWidget other;
        other.resize(200, 100);
        other.show();
        QVERIFY(QTest::qWaitForWindowExposed(&other));
        QTest::mouseClick(&other, Qt::LeftButton);
        settle();
        QVERIFY(card->isVisible());
        QDialog dialog(f.window.get());
        dialog.resize(200, 100);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        QTest::mouseClick(&dialog, Qt::LeftButton);
        settle();
        QVERIFY(card->isVisible());
        QCOMPARE(card->editor()->toPlainText(), QStringLiteral("Kept"));
    }

    // F5 with the card open, over the same files and over changed ones: the
    // card, its text, the keyboard, the current file, the rail's scroll and
    // the place in the diff all stay; unchanged files reset nothing.
    void theCommitPopoverSurvivesARefresh()
    {
        WindowFixture f = mainWindow(30);
        QVERIFY(f.window);
        f.window->resize(1200, 560);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(activate(f.window.get()));
        f.window->setPaneLayout(PaneLayout::Mini, false);
        settle();

        // A tracked file of 120 lines with three changes far apart: a diff
        // that scrolls and has more than one change to be at, so a position
        // reset to the start cannot pass for one that survived.
        const QString root = f.repo->root();
        QByteArray lines;
        for (int i = 1; i <= 120; ++i)
            lines += "line " + QByteArray::number(i) + '\n';
        QVERIFY(writeFixture(QDir(root).filePath(QStringLiteral("long.txt")), lines));
        QVERIFY(git(root, {"add", "long.txt"}));
        QVERIFY(git(root, {"commit", "-q", "-m", "long"}, 2));
        for (const int n : {10, 60, 110})
            lines.replace("line " + QByteArray::number(n) + '\n', "line " + QByteArray::number(n) + " changed\n");
        QVERIFY(writeFixture(QDir(root).filePath(QStringLiteral("long.txt")), lines));
        QListView *list = f.rail()->list();
        QTest::keyClick(list, Qt::Key_F5);
        settle();

        int longRow = -1;
        for (int row = 0; row < list->model()->rowCount(); ++row)
            if (list->model()->index(row, 0).data(ChangesModel::PathRole).toString() == QStringLiteral("long.txt"))
                longRow = row;
        QCOMPARE(longRow, 1); // the tracked files first, a.txt and then it
        list->setCurrentIndex(list->model()->index(longRow, ChangesModel::Check));
        settle();
        // Scrolled a little, the current miniature still in view.
        QScrollBar *scroll = list->verticalScrollBar();
        const int listScroll = 20;
        QVERIFY(scroll->maximum() > listScroll);
        scroll->setValue(listScroll);
        QVERIFY(list->viewport()->rect().contains(list->visualRect(list->currentIndex())));

        // A later change and a scrolled diff: none of it where the diff opens.
        const DiffView::ViewState initial = f.diff()->viewState();
        QVERIFY(f.diff()->verticalScrollBar()->maximum() > 0);
        DiffPane *pane = f.window->findChild<DiffPane *>();
        QVERIFY(pane);
        pane->nextChange();
        pane->nextChange();
        settle();
        const DiffView::ViewState moved = f.diff()->viewState();
        QVERIFY2(moved.block > 0 && moved.block != initial.block,
                 qPrintable(QStringLiteral("block %1 -> %2").arg(initial.block).arg(moved.block)));
        QVERIFY2(moved.row > 0 && moved.row != initial.row,
                 qPrintable(QStringLiteral("row %1 -> %2").arg(initial.row).arg(moved.row)));
        QVERIFY(f.openCard());
        CommitPopover *card = f.popover();
        QTest::keyClicks(card->editor(), QStringLiteral("Refreshed around"));
        const QString current = list->currentIndex().data(ChangesModel::PathRole).toString();
        QSignalSpy resets(f.page()->proxy()->sourceModel(), &QAbstractItemModel::modelReset);
        const auto unchanged = [&](const DiffView::ViewState &diff) {
            const DiffView::ViewState now = f.diff()->viewState();
            return card->isVisible() && card->editor()->toPlainText() == QStringLiteral("Refreshed around")
                && f.window->focusWidget() == card->editor()
                && list->currentIndex().data(ChangesModel::PathRole).toString() == current
                && scroll->value() == listScroll && now.row == diff.row && now.column == diff.column && now.block == diff.block;
        };

        DiffView::ViewState diff = f.diff()->viewState();
        QCOMPARE(diff.block, moved.block); // opening the card left the diff alone
        QCOMPARE(diff.row, moved.row);
        QTest::keyClick(card->editor(), Qt::Key_F5);
        settle();
        QVERIFY(unchanged(diff));
        QCOMPARE(resets.count(), 0);

        QVERIFY(writeFixture(QDir(f.repo->root()).filePath(QStringLiteral("zz-new.txt")), "new\n"));
        diff = f.diff()->viewState();
        QCOMPARE(diff.block, moved.block);
        QCOMPARE(diff.row, moved.row);
        QTest::keyClick(card->editor(), Qt::Key_F5);
        settle();
        QCOMPARE(resets.count(), 1);
        QVERIFY(unchanged(diff));
        QCOMPARE(card->hintText(), QStringLiteral("1 / 34 files selected · Space on a tile toggles it"));
    }
};

UI_TEST(MiniTest);

#include "mini_test.moc"
