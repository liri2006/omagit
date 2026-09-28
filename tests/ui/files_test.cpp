// The CHANGES list: the changes model's check marks and sort, the check-all
// box, hidden files, and the Tree, Compact and Table views of one list that
// share one current file, survive a refresh and remember the user's choice.
#include "UiTestCase.h"
#include "fixtures.h"

#include "../../src/HistoryModel.h"
#include "../../src/Settings.h"
#include "../../src/UiHelpers.h"

#include <QAbstractItemModelTester>
#include <QApplication>
#include <QFontMetrics>
#include <QItemSelectionModel>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalSpy>
#include <QStyleOptionViewItem>
#include <QTimer>

namespace {

FileChange change(const QString &path, FileChange::Kind kind)
{
    FileChange c;
    c.path = path;
    c.kind = kind;
    return c;
}

} // namespace

class FilesTest : public UiTestCase
{
    Q_OBJECT
private slots:
    void checkMarksSurviveARefresh()
    {
        ChangesModel model;
        model.setChanges({change("a.txt", FileChange::Modified), change("b.txt", FileChange::Untracked)});
        // A first load checks the versioned changes and leaves the rest alone.
        QCOMPARE(model.checkedCount(), 1);
        QCOMPARE(model.checkedPaths(), QStringList({"a.txt"}));

        model.setPathsChecked({QStringLiteral("b.txt")}, true);
        QCOMPARE(model.checkedCount(), 2);

        // b.txt is gone, c.txt is new: the choice for a.txt survives, the new
        // file starts unchecked and the vanished one leaves nothing behind.
        model.setChanges({change("a.txt", FileChange::Modified), change("c.txt", FileChange::Untracked)});
        QCOMPARE(model.checkedPaths(), QStringList({"a.txt"}));
        model.setChanges({change("a.txt", FileChange::Modified), change("b.txt", FileChange::Untracked),
                          change("c.txt", FileChange::Untracked)});
        QCOMPARE(model.checkedPaths(), QStringList({"a.txt"}));
    }

    void setAllCheckedOnAnEmptyModelNamesNoRow()
    {
        ChangesModel model;
        QSignalSpy checked(&model, &ChangesModel::checkedChanged);
        QSignalSpy changed(&model, &ChangesModel::dataChanged);
        model.setAllChecked(true);
        model.setUnversionedChecked(true);
        model.setPathsChecked({QStringLiteral("gone.txt")}, true);
        QCOMPARE(checked.count(), 3); // the tristate box still settles on 0 / 0
        QCOMPARE(changed.count(), 0); // but there is no row to name in dataChanged
        QCOMPARE(model.checkedCount(), 0);
        QCOMPARE(model.rowCount(), 0);
    }

    void statusSortsModifiedFirstAndUntrackedLast()
    {
        QCOMPARE(ChangesModel::statusRank(FileChange::Modified), 0);
        QVERIFY(ChangesModel::statusRank(FileChange::Modified) < ChangesModel::statusRank(FileChange::Added));
        QVERIFY(ChangesModel::statusRank(FileChange::Added) < ChangesModel::statusRank(FileChange::Deleted));
        for (int kind = FileChange::Modified; kind <= FileChange::Unknown; ++kind)
            if (kind != FileChange::Untracked)
                QVERIFY(ChangesModel::statusRank(FileChange::Kind(kind))
                        < ChangesModel::statusRank(FileChange::Untracked));
    }

    // The file lists' geometry (screens.js changesTable()): a 24 px header,
    // its hairline its last row and the table's top border its first, so it
    // is 23 inside the frame; 24 px rows; Name and Path titles read from the
    // left (the others centred); the checkbox a 16 px box centred in the
    // design's 32 px column, the header's too. Untracked files say so.
    void theTablesFollowTheDesignsGeometry()
    {
        CommitFixture f = commitFixture();
        QVERIFY(f.page);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        QTableView *table = f.page->table();
        const int frame = table->frameWidth();
        QCOMPARE(table->horizontalHeader()->height(), ui::space(ui::box::row) - frame);
        QCOMPARE(table->verticalHeader()->defaultSectionSize(), ui::space(ui::box::row));
        QCOMPARE(f.page->tree()->header()->height(), ui::space(ui::box::row) - frame);
        const auto alignment = [](const QAbstractItemModel *model, int section) {
            return Qt::Alignment(model->headerData(section, Qt::Horizontal, Qt::TextAlignmentRole).toInt());
        };
        QCOMPARE(alignment(table->model(), ChangesModel::Name) & Qt::AlignHorizontal_Mask, Qt::AlignLeft);
        QCOMPARE(alignment(table->model(), ChangesModel::Path) & Qt::AlignHorizontal_Mask, Qt::AlignLeft);
        QCOMPARE(alignment(table->model(), ChangesModel::Status) & Qt::AlignHorizontal_Mask, Qt::AlignHCenter);
        QCOMPARE(alignment(f.page->tree()->model(), ChangesTreeModel::Name) & Qt::AlignHorizontal_Mask, Qt::AlignLeft);
        QCOMPARE(change(QStringLiteral("u.txt"), FileChange::Untracked).statusText(), QStringLiteral("Untracked"));
        // The box of a row: 16 px, border included, centred in the design's
        // 32 px column, which starts with the frame's pixel.
        const int box = ui::space(ui::box::check);
        QCOMPARE(table->columnWidth(ChangesModel::Check), ui::space(32) - frame);
        const QRect cell(0, 0, table->columnWidth(ChangesModel::Check), ui::space(ui::box::row));
        QCOMPARE(checkBoxRect(cell, frame), QRect((ui::space(32) - box) / 2 - frame, (cell.height() - box) / 2, box, box));
        QStyleOptionViewItem item;
        item.initFrom(table);
        item.rect = QRect(0, 0, box, box);
        item.features |= QStyleOptionViewItem::HasCheckIndicator;
        QCOMPARE(table->style()->subElementRect(QStyle::SE_ItemViewItemCheckIndicator, &item, table).size(),
                 QSize(box, box));

        HistoryModel history(nullptr);
        for (int section : {int(HistoryModel::Message), int(HistoryModel::Author), int(HistoryModel::Date)})
            QCOMPARE(alignment(&history, section) & Qt::AlignHorizontal_Mask, Qt::AlignLeft);
    }

    // Nobody has picked a files view: the tree, the first of the three
    // buttons, at every width, and nothing of it saved. A choice — saved,
    // unreadable or on the command line — stays whatever the width.
    void theFilesViewIsTheTreeUntilChosen()
    {
        QSettings().remove(settings::kWindowFilesView);
        {
            CommitFixture f = commitFixture();
            QVERIFY(f.page);
            CommitPage *page = f.page.get();
            QCOMPARE(page->filesView(), CommitPage::FilesView::Tree);
            QVERIFY(page->treeButton()->isChecked());
            QCOMPARE(page->activeListView(), static_cast<QAbstractItemView *>(page->tree()));
            page->table()->selectRow(1);
            const QString current = page->table()->currentIndex().data(ChangesModel::PathRole).toString();
            QSignalSpy rows(page, &CommitPage::currentRowChanged);
            QSignalSpy resets(page->proxy(), &QAbstractItemModel::modelReset);
            page->setStacked(true);
            QCOMPARE(page->filesView(), CommitPage::FilesView::Tree);
            QVERIFY(page->treeButton()->isChecked());
            page->setStacked(false);
            QCOMPARE(page->filesView(), CommitPage::FilesView::Tree);
            QVERIFY(!QSettings().contains(settings::kWindowFilesView));
            QCOMPARE(rows.count(), 0);
            QCOMPARE(resets.count(), 0);
            QCOMPARE(page->table()->currentIndex().data(ChangesModel::PathRole).toString(), current);

            // The one already on is no choice at all; another one is, and
            // the width leaves it alone.
            page->treeButton()->click();
            QVERIFY(!QSettings().contains(settings::kWindowFilesView));
            page->compactButton()->click();
            QCOMPARE(QSettings().value(settings::kWindowFilesView).toString(), QStringLiteral("compact"));
            page->setStacked(true);
            page->setStacked(false);
            QCOMPARE(page->filesView(), CommitPage::FilesView::Compact);
        }
        // Saved, even unreadably: left alone and never rewritten.
        QSettings().setValue(settings::kWindowFilesView, QStringLiteral("sideways"));
        {
            CommitFixture f = commitFixture();
            QVERIFY(f.page);
            f.page->setStacked(true);
            QCOMPARE(f.page->filesView(), CommitPage::FilesView::Tree);
            QCOMPARE(QSettings().value(settings::kWindowFilesView).toString(), QStringLiteral("sideways"));
        }
        QSettings().remove(settings::kWindowFilesView);
        // --files-view: the run's own, and it saves nothing whatever is clicked.
        {
            CommitFixture f = commitFixture();
            QVERIFY(f.page);
            f.page->setFilesViewOverride(CommitPage::FilesView::Table);
            f.page->setStacked(true);
            QCOMPARE(f.page->filesView(), CommitPage::FilesView::Table);
            f.page->setStacked(false);
            QCOMPARE(f.page->filesView(), CommitPage::FilesView::Table);
            f.page->compactButton()->click();
            QCOMPARE(f.page->filesView(), CommitPage::FilesView::Compact);
            QVERIFY(!QSettings().contains(settings::kWindowFilesView));
        }
    }

    // Check-all is the box in the table's own header: it follows the files,
    // a click on it ticks or unticks them, and it sorts nothing.
    void theCheckAllBoxSitsInTheTableHeader()
    {
        CommitFixture f = commitFixture();
        QVERIFY(f.page);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        QVERIFY(f.header());
        QCOMPARE(f.checkAll(), int(Qt::PartiallyChecked));
        f.model()->setAllChecked(true);
        QCOMPARE(f.checkAll(), int(Qt::Checked));
        f.model()->setAllChecked(false);
        QCOMPARE(f.checkAll(), int(Qt::Unchecked));

        // Partial or none, a click checks them all; checked, it clears them.
        f.model()->setPathsChecked({QStringLiteral("a.txt")}, true);
        QCOMPARE(f.checkAll(), int(Qt::PartiallyChecked));
        const int sorted = f.header()->sortIndicatorSection();
        f.clickSection(ChangesModel::Check);
        QCOMPARE(f.model()->checkedCount(), f.model()->count());
        QCOMPARE(f.checkAll(), int(Qt::Checked));
        f.clickSection(ChangesModel::Check);
        QCOMPARE(f.model()->checkedCount(), 0);
        QCOMPARE(f.title(), QStringLiteral("CHANGES · 0/4"));
        // A column of checkboxes is nothing to sort by; the others still are.
        QCOMPARE(f.header()->sortIndicatorSection(), sorted);

        // Qt answers the second of two fast clicks with a double click, which
        // a checkbox has to take for a click of its own: the pair ticks and
        // unticks instead of the second one reaching the header.
        f.clickSection(ChangesModel::Check);
        QCOMPARE(f.model()->checkedCount(), f.model()->count());
        f.doubleClickSection(ChangesModel::Check);
        QCOMPARE(f.model()->checkedCount(), 0);
        QCOMPARE(f.header()->sortIndicatorSection(), sorted);

        f.clickSection(ChangesModel::Name);
        QCOMPARE(f.header()->sortIndicatorSection(), int(ChangesModel::Name));

        // Space on the current row checks that one file, as before.
        f.page->selectFirstRow();
        f.page->table()->setFocus();
        QTest::keyClick(f.page->table(), Qt::Key_Space);
        QCOMPARE(f.model()->checkedCount(), 1);
        QTest::keyClick(f.page->table(), Qt::Key_Space);
        QCOMPARE(f.model()->checkedCount(), 0);

        // ...whatever cell of the row is the current one: a click on a file's
        // name or its path leaves the current index in a column that carries
        // no checkbox of its own.
        for (const int column : {int(ChangesModel::Name), int(ChangesModel::Path)}) {
            f.clickCell(0, column);
            QCOMPARE(f.page->table()->currentIndex().column(), column);
            QTest::keyClick(f.page->table(), Qt::Key_Space);
            QCOMPARE(f.model()->checkedCount(), 1);
            QTest::keyClick(f.page->table(), Qt::Key_Space);
            QCOMPARE(f.model()->checkedCount(), 0);
        }

        // ...and the keybinding's check all / none is the same two states.
        f.page->toggleAllChecked();
        QCOMPARE(f.model()->checkedCount(), f.model()->count());
        f.page->toggleAllChecked();
        QCOMPARE(f.model()->checkedCount(), 0);

        // The history's files have no checkboxes, so that column is their
        // number, as it has always been.
        ChangesModel files;
        files.setCheckable(false);
        QVERIFY(!files.headerData(ChangesModel::Check, Qt::Horizontal, Qt::CheckStateRole).isValid());
        QCOMPARE(files.headerData(ChangesModel::Check, Qt::Horizontal, Qt::DisplayRole).toString(),
                 QStringLiteral("#"));
    }

    // Nothing the eye hides is ever committed: with the unversioned files out
    // of the list, none of them is checked, and the title, the button and the
    // check-all box all count the files on show.
    void hiddenFilesAreNeverChecked()
    {
        CommitFixture f = commitFixture();
        QVERIFY(f.page);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        QToolButton *eye = f.eye();
        QVERIFY(eye);
        const auto says = [&f](const QString &label) {
            return f.commitButton()->text().endsWith(label + QStringLiteral("  ⏎"));
        };
        // The header reads the box off the table's model, so both kinds of
        // change have to reach it: the source's own (forwarded by the proxy)
        // and rows coming and going (the proxy's own).
        QSignalSpy headerChanged(f.page->proxy(), &QAbstractItemModel::headerDataChanged);

        f.model()->setAllChecked(true);
        QCOMPARE(f.checkAll(), int(Qt::Checked));
        QVERIFY(headerChanged.count() > 0);
        headerChanged.clear();

        // Off: the two unversioned files leave the list and their marks with it.
        eye->click();
        QVERIFY(headerChanged.count() > 0);
        QCOMPARE(f.page->proxy()->rowCount(), 2);
        QCOMPARE(f.title(), QStringLiteral("CHANGES · 2/2"));
        QVERIFY(says(QStringLiteral("Commit 2 files")));
        QCOMPARE(f.checkAll(), int(Qt::Checked));
        QCOMPARE(f.checkedPaths(), QStringList({QStringLiteral("a.txt"), QStringLiteral("b.txt")}));

        // Check-all, by click and by keybinding, is over the shown rows only.
        f.clickSection(ChangesModel::Check);
        QCOMPARE(f.model()->checkedCount(), 0);
        QCOMPARE(f.title(), QStringLiteral("CHANGES · 0/2"));
        f.clickSection(ChangesModel::Check);
        QCOMPARE(f.checkedPaths(), QStringList({QStringLiteral("a.txt"), QStringLiteral("b.txt")}));
        QCOMPARE(f.checkAll(), int(Qt::Checked));
        f.page->toggleAllChecked();
        QCOMPARE(f.model()->checkedCount(), 0);
        f.page->toggleAllChecked();
        QCOMPARE(f.checkedPaths(), QStringList({QStringLiteral("a.txt"), QStringLiteral("b.txt")}));

        // Check marks survive a reload by path, so a mark that reached a
        // hidden file (the way amending ticks the files of the commit) is
        // gone again after the next one.
        f.model()->setPathsChecked({QStringLiteral("u1.txt")}, true);
        QCOMPARE(f.model()->checkedCount(), 3);
        f.page->reload();
        QCOMPARE(f.checkedPaths(), QStringList({QStringLiteral("a.txt"), QStringLiteral("b.txt")}));

        // A file that turns up unversioned arrives unchecked and out of sight.
        QVERIFY(writeFixture(QDir(f.dir->path()).filePath(QStringLiteral("u3.txt")), "u3\n"));
        f.page->reload();
        QCOMPARE(f.model()->count(), 5);
        QCOMPARE(f.page->proxy()->rowCount(), 2);
        QCOMPARE(f.title(), QStringLiteral("CHANGES · 2/2"));

        // On again: all five are listed, the three unversioned ones unticked,
        // and the box is partial.
        eye->click();
        QCOMPARE(f.page->proxy()->rowCount(), 5);
        QCOMPARE(f.title(), QStringLiteral("CHANGES · 2/5"));
        QCOMPARE(f.checkAll(), int(Qt::PartiallyChecked));
        QCOMPARE(f.checkedPaths(), QStringList({QStringLiteral("a.txt"), QStringLiteral("b.txt")}));
    }

    // Amending ticks the files of HEAD by path, after the reload. HEAD may
    // have deleted a file that is back as an unversioned one: behind the eye
    // it must not come out ticked, or the amend would take a file nobody saw.
    void amendingLeavesHiddenFilesUnticked()
    {
        CommitFixture f = commitFixture();
        QVERIFY(f.page);
        const QString path = f.dir->path();
        QVERIFY(git(path, {"rm", "-q", "--cached", "b.txt"}));
        QVERIFY(git(path, {"commit", "-q", "-m", "drop b"}, 1));
        // b.txt is still on disk: deleted by HEAD, unversioned now.
        f.page->reload();
        QVERIFY(f.repo->headPaths().contains(QStringLiteral("b.txt")));

        f.eye()->click(); // off
        f.model()->setAllChecked(false);
        // The order the window amends in: reload, then the paths of HEAD.
        f.page->reload();
        f.page->checkHeadPaths();
        QVERIFY(!f.checkedPaths().contains(QStringLiteral("b.txt")));
        QCOMPARE(f.model()->checkedCount(), 0);
        QCOMPARE(f.title(), QStringLiteral("CHANGES · 0/1"));

        // With the eye on the same two steps do tick it: it is there to see.
        f.eye()->click();
        f.page->reload();
        f.page->checkHeadPaths();
        QVERIFY(f.checkedPaths().contains(QStringLiteral("b.txt")));
    }

    // The check-all box lights up under the pointer, like the boxes of the
    // rows under it.
    void theCheckAllBoxLightsUpUnderThePointer()
    {
        CommitFixture f = commitFixture();
        QVERIFY(f.page);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        QHeaderView *h = f.page->table()->horizontalHeader();
        QVERIFY(h->viewport()->hasMouseTracking()); // moves arrive with no button held down
        f.model()->setAllChecked(false); // a ticked box is the accent either way
        const auto moveTo = [h](const QPoint &pos) {
            QMouseEvent move(QEvent::MouseMove, pos, h->viewport()->mapToGlobal(pos), Qt::NoButton, {}, {});
            QApplication::sendEvent(h->viewport(), &move);
        };
        const QImage plain = h->grab().toImage();

        moveTo(f.sectionCentre(ChangesModel::Check));
        const QImage hovered = h->grab().toImage();
        QVERIFY(hovered != plain);
        // The section around the box is not the box: only the indicator does.
        moveTo(QPoint(h->sectionViewportPosition(ChangesModel::Check), h->viewport()->height() - 1));
        QCOMPARE(h->grab().toImage(), plain);
        moveTo(f.sectionCentre(ChangesModel::Check));
        QCOMPARE(h->grab().toImage(), hovered);
        // The pointer leaving the header takes the hover with it.
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(h, &leave);
        QCOMPARE(h->grab().toImage(), plain);
    }

    // The tree is an index of the flat list, not a list of its own: the same
    // files, under their directories, in the order the proxy has them.
    void theFilesTreeMirrorsTheFlatList()
    {
        CommitFixture f = nestedFixture();
        QVERIFY(f.page);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        ChangesTreeModel *tree = f.treeModel();
        QVERIFY(tree);
        QAbstractItemModelTester tester(tree, QAbstractItemModelTester::FailureReportingMode::QtTest);

        // Directories first and alphabetically whatever their case, then the
        // files of that level in the flat order (Status: modified first,
        // untracked last). A chain of single children is not compressed.
        QCOMPARE(f.treeRows(),
                 QStringList({"0 alpha", "1 alpha/y.txt", "0 Beta", "1 Beta/x.txt", "0 single",
                              "1 single/one.txt", "0 src", "1 src/Deep", "2 src/Deep/c.txt", "1 src/deep",
                              "2 src/deep/b.txt", "1 src/a.txt", "0 tests", "1 tests/remove.txt",
                              "1 tests/new.txt", "0 root.txt", "0 top.txt"}));
        QCOMPARE(tree->columnCount(), int(ChangesTreeModel::ColumnCount));
        QCOMPARE(tree->headerData(ChangesTreeModel::Name, Qt::Horizontal, Qt::DisplayRole).toString(),
                 QStringLiteral("Name"));
        QCOMPARE(tree->headerData(ChangesTreeModel::Status, Qt::Horizontal, Qt::DisplayRole).toString(),
                 QStringLiteral("St"));

        // A file row is its flat row, both ways round; a directory is never
        // any file, however its path is spelled.
        for (const QString path : {"src/a.txt", "src/deep/b.txt", "root.txt", "tests/new.txt"}) {
            const QModelIndex index = tree->indexForPath(path);
            QVERIFY2(index.isValid(), qPrintable(path));
            QVERIFY(!tree->isDirectory(index));
            const QModelIndex flat = tree->mapToSource(index);
            QVERIFY(flat.isValid());
            QCOMPARE(flat.data(ChangesModel::PathRole).toString(), path);
            QCOMPARE(tree->mapFromSource(flat), index);
            QCOMPARE(index.data(ChangesModel::PathRole).toString(), path);
            QCOMPARE(index.siblingAtColumn(ChangesTreeModel::Name).data(Qt::DisplayRole).toString(),
                     QString(path).section(QLatin1Char('/'), -1));
        }
        const QModelIndex dir = tree->indexForDirectory(QStringLiteral("src"));
        QVERIFY(tree->isDirectory(dir));
        QVERIFY(!tree->indexForPath(QStringLiteral("src")).isValid()); // no file spells it
        QVERIFY(!tree->mapToSource(dir).isValid());
        QCOMPARE(tree->fileCount(dir), 3);
        QCOMPARE(tree->fileCount(tree->indexForDirectory(QStringLiteral("single"))), 1);
        // Status carries no text and no kind on a directory, so no pill.
        QVERIFY(!dir.siblingAtColumn(ChangesTreeModel::Status).data(ChangesModel::KindRole).isValid());
        QVERIFY(dir.siblingAtColumn(ChangesTreeModel::Status).data(Qt::DisplayRole).toString().isEmpty());
        QVERIFY(!dir.siblingAtColumn(ChangesTreeModel::Status).data(Qt::CheckStateRole).isValid());
        // The status letters both new presentations paint.
        QCOMPARE(ChangesModel::statusLetter(FileChange::Deleted), QChar('D'));
        QCOMPARE(ChangesModel::statusLetter(FileChange::Untracked), QChar('U'));
        QCOMPARE(ChangesModel::statusLetter(FileChange::Unknown), QChar('?'));
        // A file's tip opens with its status spelled out in its colour.
        {
            const QModelIndex file = tree->indexForPath(QStringLiteral("tests/new.txt"));
            const auto kind = FileChange::Kind(file.data(ChangesModel::KindRole).toInt());
            FileChange change;
            change.kind = kind;
            const QString tip = file.data(Qt::ToolTipRole).toString();
            QVERIFY2(tip.startsWith(QStringLiteral("<p style=\"white-space:pre\"><span style=\"color:%1\">%2</span><br>tests/new.txt")
                                        .arg(ChangesModel::statusColor(kind).name(), change.statusText())),
                     qPrintable(tip));
            QVERIFY(Qt::mightBeRichText(tip));
        }
        QCOMPARE(ChangesModel::statusLetter(FileChange::Unmerged), QChar('!'));

        // Sorting is the flat list's: entering the tree leaves the table's own
        // sort alone, even on a column the tree has not got.
        f.page->proxy()->sort(ChangesModel::Size, Qt::DescendingOrder);
        f.page->setFilesView(CommitPage::FilesView::Tree, false);
        settle();
        QCOMPARE(f.page->proxy()->sortColumn(), int(ChangesModel::Size));
        QCOMPARE(f.page->proxy()->sortOrder(), Qt::DescendingOrder);

        // The tree's own sections route to the flat columns behind them, and
        // clicking one again turns the order around.
        QHeaderView *header = f.treeHeader();
        QVERIFY(header);
        const auto clickTreeSection = [header](int section) {
            const QPoint centre(header->sectionViewportPosition(section) + header->sectionSize(section) / 2,
                                header->viewport()->height() / 2);
            QTest::mouseClick(header->viewport(), Qt::LeftButton, {}, centre);
        };
        clickTreeSection(ChangesTreeModel::Name);
        QCOMPARE(f.page->proxy()->sortColumn(), int(ChangesModel::Name));
        QCOMPARE(f.page->proxy()->sortOrder(), Qt::AscendingOrder);
        clickTreeSection(ChangesTreeModel::Name);
        QCOMPARE(f.page->proxy()->sortOrder(), Qt::DescendingOrder);
        clickTreeSection(ChangesTreeModel::Status);
        QCOMPARE(f.page->proxy()->sortColumn(), int(ChangesModel::Status));

        // The sort a tree section makes is the table's own, indicator and all:
        // back in the table, the same section goes on from there rather than
        // starting again at the top.
        clickTreeSection(ChangesTreeModel::Name);
        QCOMPARE(f.header()->sortIndicatorSection(), int(ChangesModel::Name));
        QCOMPARE(f.header()->sortIndicatorOrder(), Qt::AscendingOrder);
        f.page->setFilesView(CommitPage::FilesView::Table, false);
        settle();
        f.clickSection(ChangesModel::Name);
        QCOMPARE(f.page->proxy()->sortColumn(), int(ChangesModel::Name));
        QCOMPARE(f.page->proxy()->sortOrder(), Qt::DescendingOrder);
        f.page->setFilesView(CommitPage::FilesView::Tree, false);
        settle();

        // ...and the column of checkboxes sorts nothing; it checks everything.
        f.model()->setAllChecked(false);
        clickTreeSection(ChangesTreeModel::Check);
        QCOMPARE(f.page->proxy()->sortColumn(), int(ChangesModel::Name));
        QCOMPARE(f.model()->checkedCount(), f.page->proxy()->rowCount());
    }

    // A directory's box stands for every file under it, open or folded away,
    // and writing it goes through the one list of check marks there is.
    void theFilesTreeChecksWholeDirectories()
    {
        CommitFixture f = nestedFixture();
        QVERIFY(f.page);
        f.page->setFilesView(CommitPage::FilesView::Tree, false);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        ChangesTreeModel *tree = f.treeModel();

        // All three of src's files are modified, so they start checked; tests
        // has a deletion (checked) beside an untracked file (not).
        QCOMPARE(f.treeCheck("src"), int(Qt::Checked));
        QCOMPARE(f.treeCheck("tests"), int(Qt::PartiallyChecked));
        QCOMPARE(f.treeCheck("tests/new.txt"), int(Qt::Unchecked));

        // Partial checks everything under it; checked clears it again.
        QVERIFY(tree->setData(f.treeIndex("tests"), Qt::Checked, Qt::CheckStateRole));
        QCOMPARE(f.treeCheck("tests"), int(Qt::Checked));
        QVERIFY(f.checkedPaths().contains(QStringLiteral("tests/new.txt")));
        QVERIFY(tree->setData(f.treeIndex("tests"), Qt::Unchecked, Qt::CheckStateRole));
        QCOMPARE(f.treeCheck("tests"), int(Qt::Unchecked));
        QVERIFY(!f.checkedPaths().contains(QStringLiteral("tests/remove.txt")));

        // A directory deeper down takes its ancestors with it, as far as the
        // state goes: src becomes partial the moment one branch of it is.
        QVERIFY(tree->setData(f.treeIndex("src/deep"), Qt::Unchecked, Qt::CheckStateRole));
        QCOMPARE(f.treeCheck("src/deep"), int(Qt::Unchecked));
        QCOMPARE(f.treeCheck("src"), int(Qt::PartiallyChecked));
        QCOMPARE(f.treeCheck("src/Deep"), int(Qt::Checked));

        // Folded away is still under it: collapsing changes no count and no
        // check, and checking the parent reaches the hidden files all the same.
        const QString title = f.title();
        f.tree()->collapse(f.treeIndex("src"));
        QCOMPARE(f.title(), title);
        QVERIFY(tree->setData(f.treeIndex("src"), Qt::Checked, Qt::CheckStateRole));
        QCOMPARE(f.treeCheck("src/deep"), int(Qt::Checked));
        QVERIFY(f.checkedPaths().contains(QStringLiteral("src/deep/b.txt")));
        f.tree()->expand(f.treeIndex("src"));

        // A file goes through the flat proxy's own check index.
        QVERIFY(tree->setData(f.treeIndex("src/a.txt"), Qt::Unchecked, Qt::CheckStateRole));
        QVERIFY(!f.checkedPaths().contains(QStringLiteral("src/a.txt")));
        QCOMPARE(f.treeCheck("src"), int(Qt::PartiallyChecked));

        // Space acts on the row, from whichever column the keyboard is in.
        f.tree()->setFocus();
        for (const int column : {int(ChangesTreeModel::Name), int(ChangesTreeModel::Status)}) {
            f.tree()->setCurrentIndex(f.treeIndex("alpha", column));
            const int before = f.model()->checkedCount();
            QTest::keyClick(f.tree(), Qt::Key_Space);
            QCOMPARE(f.model()->checkedCount(), before - 1);
            QTest::keyClick(f.tree(), Qt::Key_Space);
            QCOMPARE(f.model()->checkedCount(), before);
        }

        // Check-all is the proxy's, over the files it is showing, and the
        // tree only puts the box in a section of its own.
        f.model()->setAllChecked(false);
        QCOMPARE(tree->headerData(ChangesTreeModel::Check, Qt::Horizontal, Qt::CheckStateRole).toInt(),
                 int(Qt::Unchecked));
        QVERIFY(tree->setHeaderData(ChangesTreeModel::Check, Qt::Horizontal, int(Qt::Checked), Qt::CheckStateRole));
        QCOMPARE(f.model()->checkedCount(), f.page->proxy()->rowCount());
        QVERIFY(!tree->headerData(ChangesTreeModel::Name, Qt::Horizontal, Qt::CheckStateRole).isValid());
        QVERIFY(tree->headerData(ChangesTreeModel::Check, Qt::Horizontal, Qt::DisplayRole).toString().isEmpty());

        // Nothing the eye hides is in the tree at all, so nothing a directory
        // of it checks can be hidden either.
        f.model()->setAllChecked(false);
        f.eye()->click();
        settle();
        QVERIFY(!tree->indexForPath(QStringLiteral("tests/new.txt")).isValid());
        QVERIFY(tree->setData(f.treeIndex("tests"), Qt::Checked, Qt::CheckStateRole));
        QCOMPARE(f.checkedPaths(), QStringList({QStringLiteral("tests/remove.txt")}));

        // An empty list is an empty tree, with nothing to check.
        f.model()->setChanges({});
        settle();
        QCOMPARE(tree->rowCount(), 0);
    }

    // Zero indentation and no root decoration do not stop Qt from walking the
    // tree: it works on the row, whichever column the keyboard is in.
    void theFilesTreeWalksWithTheKeyboard()
    {
        CommitFixture f = nestedFixture();
        QVERIFY(f.page);
        f.page->setFilesView(CommitPage::FilesView::Tree, false);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        QTreeView *tree = f.tree();
        QCOMPARE(tree->indentation(), 0);
        QVERIFY(!tree->rootIsDecorated());
        QCOMPARE(tree->objectName(), QStringLiteral("changesTree"));
        tree->setFocus();

        for (const int column : {int(ChangesTreeModel::Check), int(ChangesTreeModel::Status)}) {
            const QModelIndex src = f.treeIndex("src");
            tree->setCurrentIndex(f.treeIndex("src", column));
            QVERIFY(tree->isExpanded(src));
            QTest::keyClick(tree, Qt::Key_Left);
            QVERIFY(!tree->isExpanded(src));
            QTest::keyClick(tree, Qt::Key_Right);
            QVERIFY(tree->isExpanded(src));
        }

        // Down walks into the branch, Left from a leaf comes back to it.
        tree->setCurrentIndex(f.treeIndex("src"));
        QTest::keyClick(tree, Qt::Key_Down);
        QCOMPARE(f.treeModel()->path(tree->currentIndex()), QStringLiteral("src/Deep"));
        QTest::keyClick(tree, Qt::Key_Left); // an open directory folds first
        QTest::keyClick(tree, Qt::Key_Left); // ...and then hands over to its parent
        QCOMPARE(f.treeModel()->path(tree->currentIndex()), QStringLiteral("src"));

        // Space on a directory in the status column checks what is under it
        // and leaves the canonical current file where it was.
        QVERIFY(f.page->selectPath(QStringLiteral("root.txt")));
        const QString canonical = f.page->table()->currentIndex().data(ChangesModel::PathRole).toString();
        QSignalSpy current(f.page.get(), &CommitPage::currentRowChanged);
        tree->setCurrentIndex(f.treeIndex("alpha", ChangesTreeModel::Status));
        QTest::keyClick(tree, Qt::Key_Space);
        QVERIFY(!f.checkedPaths().contains(QStringLiteral("alpha/y.txt")));
        QCOMPARE(f.page->table()->currentIndex().data(ChangesModel::PathRole).toString(), canonical);
        QCOMPARE(current.count(), 0);
    }

    // The chevron and the folder open the branch; the box beside them makes
    // check marks. Neither ever does the other's job.
    void theFilesTreeOpensOnItsChevronAndChecksOnItsBox()
    {
        CommitFixture f = nestedFixture();
        QVERIFY(f.page);
        f.page->setFilesView(CommitPage::FilesView::Tree, false);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        const QModelIndex src = f.treeIndex("src");
        const int checked = f.model()->checkedCount();

        // 6..34 design px into the Name cell is the chevron and the folder.
        f.clickTreeCell(QStringLiteral("src"), ChangesTreeModel::Name,
                        QPoint(ui::space(12), f.tree()->visualRect(src).height() / 2));
        QVERIFY(!f.tree()->isExpanded(src));
        QCOMPARE(f.model()->checkedCount(), checked);
        f.clickTreeCell(QStringLiteral("src"), ChangesTreeModel::Name,
                        QPoint(ui::space(12), f.tree()->visualRect(src).height() / 2));
        QVERIFY(f.tree()->isExpanded(src));
        QCOMPARE(f.model()->checkedCount(), checked);

        // Only the left button works the chevron; the others are Qt's, and a
        // directory neither opens nor closes under them.
        for (const Qt::MouseButton button : {Qt::RightButton, Qt::MiddleButton}) {
            const QRect cell = f.tree()->visualRect(f.treeIndex("src", ChangesTreeModel::Name));
            const QPoint on(cell.left() + ui::space(12), cell.center().y());
            QTest::mousePress(f.tree()->viewport(), button, {}, on);
            QTest::mouseRelease(f.tree()->viewport(), button, {}, on);
            QVERIFY2(f.tree()->isExpanded(src), button == Qt::RightButton ? "right button" : "middle button");
            QCOMPARE(f.model()->checkedCount(), checked);
        }

        // The box: the marks move and the branch stays as it was.
        f.clickTreeCell(QStringLiteral("src"), ChangesTreeModel::Check);
        QVERIFY(f.tree()->isExpanded(src));
        QCOMPARE(f.model()->checkedCount(), checked - 3);
        f.clickTreeCell(QStringLiteral("src"), ChangesTreeModel::Check);
        QCOMPARE(f.model()->checkedCount(), checked);

        // A gesture that starts on the chevron opens the branch once, not
        // twice: the press has already done it, so the double click that
        // follows is eaten rather than closing it again.
        QSignalSpy opened(f.page.get(), &CommitPage::openRequested);
        QSignalSpy diff(f.page.get(), &CommitPage::showDiffPaneRequested);
        const QRect name = f.tree()->visualRect(f.treeIndex("src", ChangesTreeModel::Name));
        const QPoint chevron(name.left() + ui::space(12), name.center().y());
        QTest::mousePress(f.tree()->viewport(), Qt::LeftButton, {}, chevron);
        QTest::mouseRelease(f.tree()->viewport(), Qt::LeftButton, {}, chevron);
        QTest::mouseDClick(f.tree()->viewport(), Qt::LeftButton, {}, chevron);
        QVERIFY(!f.tree()->isExpanded(src));
        QCOMPARE(f.model()->checkedCount(), checked);

        for (const QPoint pos : {name.center(), chevron,
                                 QPoint(f.tree()->visualRect(src).right() - 2, name.center().y())}) {
            QTest::mouseDClick(f.tree()->viewport(), Qt::LeftButton, {}, pos);
            QCOMPARE(f.model()->checkedCount(), checked);
        }
        f.tree()->expand(src);

        // What a double-click means, taken at the signal: opening and closing
        // the branch is all a directory's ever does, while a file's is the
        // table's rule — open it, or show the diff where the pane is hidden.
        // (The synthetic events QtTest sends never reach QTreeView's own
        // double-click handling, in this tree or in a plain one.)
        const auto doubleClick = [&f](const QString &path) {
            QMetaObject::invokeMethod(f.tree(), "doubleClicked",
                                      Q_ARG(QModelIndex, f.treeIndex(path, ChangesTreeModel::Name)));
        };
        doubleClick(QStringLiteral("src"));
        QCOMPARE(opened.count(), 0);
        QCOMPARE(diff.count(), 0);
        doubleClick(QStringLiteral("root.txt"));
        QCOMPARE(opened.count(), 1);
        QCOMPARE(diff.count(), 0);
        f.page->setDiffPaneVisible(false);
        doubleClick(QStringLiteral("root.txt"));
        QCOMPARE(opened.count(), 1);
        QCOMPARE(diff.count(), 1);
        f.page->setDiffPaneVisible(true);
    }

    // A rename's source path may spell another file's current path: checking
    // a directory must take the files in it and nothing else.
    void checkingADirectoryLeavesARenamedFileAlone()
    {
        CommitFixture f = commitFixture();
        QVERIFY(f.page);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        // b/f.txt was renamed from a/f.txt, and an untracked file of its own
        // now stands at that very path. The overlap is what matters here, so
        // the list is given to the model outright.
        FileChange rename = change(QStringLiteral("b/f.txt"), FileChange::Renamed);
        rename.oldPath = QStringLiteral("a/f.txt");
        f.model()->setChanges({change(QStringLiteral("a/keep.txt"), FileChange::Modified),
                               change(QStringLiteral("a/f.txt"), FileChange::Untracked), rename});
        settle();
        QCOMPARE(f.page->proxy()->rowCount(), 3);
        QVERIFY(f.treeModel()->indexForPath(QStringLiteral("b/f.txt")).isValid());

        // The directory takes the two files in it, not the one whose old path
        // one of them happens to spell.
        f.model()->setAllChecked(false);
        QVERIFY(f.treeModel()->setData(f.treeIndex("a"), Qt::Checked, Qt::CheckStateRole));
        QCOMPARE(f.checkedPaths(), QStringList({QStringLiteral("a/f.txt"), QStringLiteral("a/keep.txt")}));
        QCOMPARE(f.treeCheck("b"), int(Qt::Unchecked));

        // The check-all box over the shown rows is exact in the same way: the
        // eye takes the untracked a/f.txt out, and nothing puts it back.
        f.model()->setAllChecked(false);
        f.eye()->click();
        settle();
        QCOMPARE(f.page->proxy()->rowCount(), 2);
        f.page->toggleAllChecked();
        QCOMPARE(f.model()->checkedCount(), 2);
        for (const FileChange &c : f.model()->checkedChanges())
            QVERIFY2(!c.isUntracked(), qPrintable(c.path));

        // Amending still matches the source of a rename on purpose: HEAD knows
        // the file by the path it had before, so both rows answer to it.
        f.eye()->click();
        settle();
        f.model()->setAllChecked(false);
        f.model()->setPathsChecked({QStringLiteral("a/f.txt")}, true);
        QCOMPARE(f.checkedPaths(), QStringList({QStringLiteral("a/f.txt"), QStringLiteral("a/f.txt"),
                                                QStringLiteral("b/f.txt")}));
    }

    // Check marks, colours and a theme change are repaints, not rebuilds: only
    // rows coming, going or being sorted make the tree again.
    void theFilesTreeIsRebuiltOnlyByTheShapeOfTheList()
    {
        CommitFixture f = nestedFixture();
        QVERIFY(f.page);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        QSignalSpy rebuilt(f.treeModel(), &QAbstractItemModel::modelReset);
        QSignalSpy changed(f.treeModel(), &QAbstractItemModel::dataChanged);

        f.model()->setAllChecked(true);
        QCOMPARE(rebuilt.count(), 0);
        QVERIFY(changed.count() > 0);
        f.model()->setPathsChecked({QStringLiteral("src/a.txt")}, false);
        QCOMPARE(rebuilt.count(), 0);

        // An unchanged reload changes nothing at all, eye or no eye.
        f.page->reload();
        QCOMPARE(rebuilt.count(), 0);
        f.eye()->click(); // off: the filter is a layout change, so one rebuild
        settle();
        QCOMPARE(rebuilt.count(), 1);
        f.page->reload();
        QCOMPARE(rebuilt.count(), 1);
        f.eye()->click();
        settle();
        QCOMPARE(rebuilt.count(), 2);

        // Sorting is a layout change of the proxy: the files come back in the
        // new order, under the same directories.
        f.clickSection(ChangesModel::Name);
        QVERIFY(rebuilt.count() > 2);
        QVERIFY(f.treeRows().contains(QStringLiteral("0 src")));

        // Rows really coming and going do rebuild it.
        const int before = rebuilt.count();
        QVERIFY(writeFixture(QDir(f.dir->path()).filePath(QStringLiteral("late.txt")), "late\n"));
        f.page->reload();
        QVERIFY(rebuilt.count() > before);
        QVERIFY(f.treeModel()->indexForPath(QStringLiteral("late.txt")).isValid());
    }

    // Which directories are folded away is this session's, by path: it
    // survives sorting, filtering and reloads, and counts nothing.
    void collapsedDirectoriesSurviveAndCountTheirFiles()
    {
        CommitFixture f = nestedFixture();
        QVERIFY(f.page);
        f.page->setFilesView(CommitPage::FilesView::Tree, false);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        // Everything starts open.
        for (const QString dir : {"alpha", "src", "src/deep", "tests"})
            QVERIFY2(f.tree()->isExpanded(f.treeIndex(dir)), qPrintable(dir));
        QCOMPARE(f.treeModel()->fileCount(f.treeIndex("single")), 1);
        QCOMPARE(f.treeModel()->fileCount(f.treeIndex("tests")), 2);

        const QString title = f.title();
        const QStringList checked = f.checkedPaths();
        f.tree()->collapse(f.treeIndex("src/deep"));
        f.tree()->collapse(f.treeIndex("tests"));
        QCOMPARE(f.title(), title);
        QCOMPARE(f.checkedPaths(), checked);
        QCOMPARE(f.page->proxy()->rowCount(), 10); // collapse hides nothing from the counts

        const auto stillFolded = [&f] {
            QVERIFY(!f.tree()->isExpanded(f.treeIndex("src/deep")));
            QVERIFY(!f.tree()->isExpanded(f.treeIndex("tests")));
            QVERIFY(f.tree()->isExpanded(f.treeIndex("src")));
        };
        f.page->proxy()->sort(ChangesModel::Name, Qt::AscendingOrder); // sorting
        stillFolded();
        f.eye()->click();                   // filtering
        settle();
        stillFolded();
        f.eye()->click();
        settle();
        stillFolded();
        f.page->reload();                   // a real reload
        stillFolded();
        // ...and a directory that leaves the list and comes back with it.
        QVERIFY(writeFixture(QDir(f.dir->path()).filePath(QStringLiteral("tests/remove.txt")), "one\n"));
        QVERIFY(writeFixture(QDir(f.dir->path()).filePath(QStringLiteral("tests/new.txt")), "u\n"));
        f.page->reload();
        QVERIFY(f.treeModel()->indexForDirectory(QStringLiteral("tests")).isValid());
        QVERIFY(!f.tree()->isExpanded(f.treeIndex("tests")));
        // ...and a switch away and back.
        f.page->setFilesView(CommitPage::FilesView::Table, false);
        f.page->setFilesView(CommitPage::FilesView::Tree, false);
        settle();
        stillFolded();
    }

    // Whichever list the user is looking at, the current file is one file:
    // the flat table's row, which is what the window reads the diff from.
    void theFilesViewsShareOneCurrentFile()
    {
        CommitFixture f = nestedFixture();
        QVERIFY(f.page);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        QVERIFY(f.page->selectPath(QStringLiteral("root.txt")));
        QSignalSpy current(f.page.get(), &CommitPage::currentRowChanged);

        // The canonical row reaches the tree, ancestors opened, whoever set it.
        f.tree()->collapse(f.treeIndex("src"));
        QVERIFY(f.page->selectPath(QStringLiteral("src/deep/b.txt")));
        QCOMPARE(current.count(), 1);
        QVERIFY(f.tree()->isExpanded(f.treeIndex("src")));
        QVERIFY(f.tree()->isExpanded(f.treeIndex("src/deep")));
        QCOMPARE(f.treeModel()->path(f.tree()->currentIndex()), QStringLiteral("src/deep/b.txt"));

        // Selecting the same file again is nobody's news.
        QVERIFY(f.page->selectPath(QStringLiteral("src/deep/b.txt")));
        QCOMPARE(current.count(), 1);

        // A file becoming current in the tree makes the flat row current, once.
        f.page->setFilesView(CommitPage::FilesView::Tree, false);
        settle();
        QCOMPARE(current.count(), 1); // switching reveals; it does not re-announce
        f.tree()->setCurrentIndex(f.treeIndex("alpha/y.txt", ChangesTreeModel::Name));
        QCOMPARE(current.count(), 2);
        bool ok = false;
        QCOMPARE(f.page->currentChange(&ok).path, QStringLiteral("alpha/y.txt"));
        QVERIFY(ok);
        // Another column of the same file is the same file.
        f.tree()->setCurrentIndex(f.treeIndex("alpha/y.txt", ChangesTreeModel::Status));
        QCOMPARE(current.count(), 2);

        // A directory is a place in the tree: the canonical file stays put.
        f.tree()->setCurrentIndex(f.treeIndex("tests"));
        QCOMPARE(current.count(), 2);
        QCOMPARE(f.page->currentChange(&ok).path, QStringLiteral("alpha/y.txt"));

        // Switching presentations reveals the canonical file and announces
        // nothing; the flat proxy and the selection model stay the ones they
        // have always been.
        QSortFilterProxyModel *const proxy = f.page->proxy();
        QItemSelectionModel *const selection = f.page->table()->selectionModel();
        for (const CommitPage::FilesView view : {CommitPage::FilesView::Table, CommitPage::FilesView::Compact,
                                                 CommitPage::FilesView::Tree}) {
            f.page->setFilesView(view, false);
            settle();
            QCOMPARE(f.page->activeListView(),
                     view == CommitPage::FilesView::Tree ? static_cast<QAbstractItemView *>(f.tree())
                                                         : f.page->table());
            QCOMPARE(f.page->proxy(), proxy);
            QCOMPARE(f.page->table()->selectionModel(), selection);
        }
        QCOMPARE(current.count(), 2);
        QCOMPARE(f.treeModel()->path(f.tree()->currentIndex()), QStringLiteral("alpha/y.txt"));

        // selectFirstRow() is still the flat list's first file, not the tree's
        // first directory.
        f.page->setFilesView(CommitPage::FilesView::Tree, false);
        QVERIFY(f.page->selectFirstRow());
        QCOMPARE(f.page->currentChange(&ok).path,
                 f.page->proxy()->index(0, 0).data(ChangesModel::PathRole).toString());
    }

    // The refresh sequence the window runs — remember, reload, select, scroll
    // back — has to land in the same place whichever list is on show.
    void theFilesViewsSurviveARefresh()
    {
        CommitFixture f = nestedFixture();
        QVERIFY(f.page);
        f.page->setFilesView(CommitPage::FilesView::Tree, false);
        f.page->resize(760, 300); // short enough that the list scrolls
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        QVERIFY(f.page->selectPath(QStringLiteral("top.txt")));
        f.page->tree()->scrollToBottom();
        settle();
        const QPoint offset = f.page->scrollOffset();
        QVERIFY(offset.y() > 0); // a tree only this tall needs its ancestors open

        // The window's own order: the offset first, then the reload, then the
        // selection, then the offset back.
        const QPoint before = f.page->scrollOffset();
        f.page->reload();
        QVERIFY(f.page->selectPath(QStringLiteral("top.txt")));
        f.page->setScrollOffset(before);
        QCOMPARE(f.page->scrollOffset(), before);
        bool ok = false;
        QCOMPARE(f.page->currentChange(&ok).path, QStringLiteral("top.txt"));

        // The same after the list really changes.
        QVERIFY(writeFixture(QDir(f.dir->path()).filePath(QStringLiteral("late.txt")), "late\n"));
        f.page->reload();
        QVERIFY(f.page->selectPath(QStringLiteral("top.txt")));
        f.page->setScrollOffset(before);
        QCOMPARE(f.page->scrollOffset(), before);

        // An unchanged refresh with a directory current leaves the keyboard on
        // that directory: nothing was rebuilt, so nothing moved.
        f.tree()->setCurrentIndex(f.treeIndex("src"));
        f.page->reload();
        QVERIFY(f.page->selectPath(QStringLiteral("top.txt")));
        QCOMPARE(f.treeModel()->path(f.tree()->currentIndex()), QStringLiteral("src"));

        // A refresh that really changes the list leaves it there too: the file
        // the window selects again is the file the tree was already showing,
        // so nothing drags the keyboard off the directory the user walked to.
        f.tree()->collapse(f.treeIndex("tests"));
        f.tree()->setCurrentIndex(f.treeIndex("src", ChangesTreeModel::Name));
        QVERIFY(writeFixture(QDir(f.dir->path()).filePath(QStringLiteral("later.txt")), "later\n"));
        f.page->reload();
        QVERIFY(f.page->selectPath(QStringLiteral("top.txt")));
        f.page->setScrollOffset(before);
        settle();
        QCOMPARE(f.treeModel()->path(f.tree()->currentIndex()), QStringLiteral("src"));
        QVERIFY(f.treeModel()->isDirectory(f.tree()->currentIndex()));
        QVERIFY(!f.tree()->isExpanded(f.treeIndex("tests")));
        QCOMPARE(f.page->currentChange(&ok).path, QStringLiteral("top.txt"));
        f.tree()->expand(f.treeIndex("tests"));

        // The table keeps its own offset the same way.
        f.page->setFilesView(CommitPage::FilesView::Table, false);
        settle();
        f.page->table()->scrollToBottom();
        const QPoint tableOffset = f.page->scrollOffset();
        f.page->reload();
        QVERIFY(f.page->selectPath(QStringLiteral("top.txt")));
        f.page->setScrollOffset(tableOffset);
        QCOMPARE(f.page->scrollOffset(), tableOffset);
    }

    // A file that leaves the list and comes back is a new file to the tree:
    // what it was synchronised to went with the file.
    void aFileThatComesBackIsRevealedAgain()
    {
        CommitFixture f = nestedFixture();
        QVERIFY(f.page);
        f.page->setFilesView(CommitPage::FilesView::Tree, false);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        const QString file = QDir(f.dir->path()).filePath(QStringLiteral("back.txt"));
        QVERIFY(writeFixture(file, "here\n"));
        f.page->reload();
        QVERIFY(f.page->selectPath(QStringLiteral("back.txt")));
        QCOMPARE(f.treeModel()->path(f.tree()->currentIndex()), QStringLiteral("back.txt"));

        QVERIFY(QFile::remove(file));
        f.page->reload();
        // Nothing else is selected in between, as in a list that went empty.
        QVERIFY(!f.page->selectPath(QStringLiteral("back.txt")));

        QVERIFY(writeFixture(file, "here again\n"));
        f.page->reload();
        QVERIFY(f.page->selectPath(QStringLiteral("back.txt")));
        QCOMPARE(f.treeModel()->path(f.tree()->currentIndex()), QStringLiteral("back.txt"));
    }

    // Putting the row the keyboard was on back must not scroll to it: a view
    // scrolling to a row opens every directory above it, and the one the user
    // had just folded away is one of them.
    void aFoldedAncestorSurvivesTheCurrentRowComingBack()
    {
        CommitFixture f = nestedFixture();
        QVERIFY(f.page);
        f.page->setFilesView(CommitPage::FilesView::Tree, false);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        QVERIFY(f.page->selectPath(QStringLiteral("src/deep/b.txt")));
        QCOMPARE(f.treeModel()->path(f.tree()->currentIndex()), QStringLiteral("src/deep/b.txt"));

        // Folded by its chevron, the way the user folds it: the current row is
        // now under a directory that is not showing it.
        const QModelIndex src = f.treeIndex("src");
        f.clickTreeCell(QStringLiteral("src"), ChangesTreeModel::Name,
                        QPoint(ui::space(12), f.tree()->visualRect(src).height() / 2));
        QVERIFY(!f.tree()->isExpanded(src));
        QCOMPARE(f.treeModel()->path(f.tree()->currentIndex()), QStringLiteral("src/deep/b.txt"));

        const auto stillFolded = [&f] {
            QVERIFY(!f.tree()->isExpanded(f.treeIndex("src")));
            QCOMPARE(f.treeModel()->path(f.tree()->currentIndex()), QStringLiteral("src/deep/b.txt"));
        };
        f.page->proxy()->sort(ChangesModel::Name, Qt::AscendingOrder); // one rebuild...
        settle();
        stillFolded();
        // ...and another, after a reload that really changes the list. The
        // second one proves the collapsed set itself came through the first:
        // an ancestor unfolded by a scroll would have dropped out of it.
        QVERIFY(writeFixture(QDir(f.dir->path()).filePath(QStringLiteral("late.txt")), "late\n"));
        f.page->reload();
        QVERIFY(f.page->selectPath(QStringLiteral("src/deep/b.txt")));
        settle();
        stillFolded();
    }

    // One name can be a file and a directory at once — a repository may list
    // `a` beside `a/b` — so the two are looked up apart, whichever order the
    // flat list is in.
    void aFileAndADirectoryOfOneNameStayApart()
    {
        CommitFixture f = commitFixture();
        QVERIFY(f.page);
        f.page->setFilesView(CommitPage::FilesView::Tree, false);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        // Given to the model outright: no working tree holds both at once.
        f.model()->setChanges({change(QStringLiteral("a"), FileChange::Modified),
                               change(QStringLiteral("a/b"), FileChange::Modified),
                               change(QStringLiteral("z.txt"), FileChange::Modified)});
        settle();

        for (const Qt::SortOrder order : {Qt::AscendingOrder, Qt::DescendingOrder}) {
            f.page->proxy()->sort(ChangesModel::Name, order);
            settle();
            ChangesTreeModel *tree = f.treeModel();
            const QModelIndex file = tree->indexForPath(QStringLiteral("a"));
            QVERIFY(file.isValid());
            QVERIFY(!tree->isDirectory(file));
            QCOMPARE(tree->mapToSource(file).data(ChangesModel::PathRole).toString(), QStringLiteral("a"));
            const QModelIndex dir = tree->indexForDirectory(QStringLiteral("a"));
            QVERIFY(dir.isValid());
            QVERIFY(tree->isDirectory(dir));
            QVERIFY(tree->indexForPath(QStringLiteral("a/b")).isValid());
            QVERIFY(!tree->indexForDirectory(QStringLiteral("a/b")).isValid());

            // Revealing the canonical file lands on the file...
            QVERIFY(f.page->selectPath(QStringLiteral("z.txt")));
            QVERIFY(f.page->selectPath(QStringLiteral("a")));
            QCOMPARE(tree->path(f.tree()->currentIndex()), QStringLiteral("a"));
            QVERIFY(!tree->isDirectory(f.tree()->currentIndex()));

            // ...while the directory the keyboard was on comes back as the
            // directory, over a rebuild of every node.
            f.tree()->setCurrentIndex(dir.siblingAtColumn(ChangesTreeModel::Name));
            QVERIFY(f.treeModel()->isDirectory(f.tree()->currentIndex()));
            f.page->proxy()->sort(ChangesModel::Status, order);
            settle();
            QCOMPARE(f.treeModel()->path(f.tree()->currentIndex()), QStringLiteral("a"));
            QVERIFY(f.treeModel()->isDirectory(f.tree()->currentIndex()));
        }
    }

    // Loading a choice is not making one, and neither is asking for the
    // presentation already on: only a real switch writes window/filesView.
    void readingAFilesViewNeverWritesIt()
    {
        QSettings().remove(settings::kWindowFilesView);
        CommitFixture f = commitFixture();
        QVERIFY(f.page);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        f.page->setFilesView(CommitPage::FilesView::Table, false);
        QCOMPARE(f.page->filesView(), CommitPage::FilesView::Table);
        QVERIFY(f.page->tableButton()->isChecked());
        QVERIFY(!QSettings().contains(settings::kWindowFilesView));
        // The one already on, this time asked to save: a complete no-op.
        f.page->setFilesView(CommitPage::FilesView::Table);
        QCOMPARE(f.page->filesView(), CommitPage::FilesView::Table);
        QVERIFY(!QSettings().contains(settings::kWindowFilesView));
        // A real switch is the user's choice and is written.
        f.page->setFilesView(CommitPage::FilesView::Compact);
        QCOMPARE(QSettings().value(settings::kWindowFilesView).toString(), QStringLiteral("compact"));
        QSettings().remove(settings::kWindowFilesView);
    }

    // The name delegates paint the row's own font: a conflicted file is bold
    // because the model says so, not because the option the view handed over
    // happened to be.
    void theNameDelegatesPaintTheRowsOwnFont()
    {
        CommitFixture f = commitFixture();
        QVERIFY(f.page);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        f.model()->setChanges({change(QStringLiteral("src/clash.txt"), FileChange::Unmerged),
                               change(QStringLiteral("src/plain.txt"), FileChange::Modified)});
        settle();
        const QFont plain = OmarchyTheme::instance()->uiFont();
        QFont bold = plain;
        bold.setBold(true);
        // One cell, painted by the delegate itself, with the font the view
        // would have handed it.
        const auto render = [](QAbstractItemDelegate *delegate, const QModelIndex &index, const QFont &font) {
            QStyleOptionViewItem option;
            option.rect = QRect(0, 0, 200, ui::rowHeight());
            option.font = font;
            option.fontMetrics = QFontMetrics(font);
            option.state = QStyle::State_Enabled;
            QImage image(option.rect.size(), QImage::Format_ARGB32);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            delegate->paint(&painter, option, index);
            return image;
        };

        // The tree's Name column: the conflicted row is the same picture
        // whichever font comes in, because the model's one wins...
        f.page->setFilesView(CommitPage::FilesView::Tree, false);
        settle();
        QAbstractItemDelegate *treeName = f.tree()->itemDelegateForColumn(ChangesTreeModel::Name);
        QVERIFY(treeName);
        const QModelIndex conflict = f.treeIndex("src/clash.txt", ChangesTreeModel::Name);
        QVERIFY(conflict.isValid());
        QCOMPARE(render(treeName, conflict, plain), render(treeName, conflict, bold));
        // ...while a row with no font of its own paints the one it is given,
        // so the two really are different pictures.
        const QModelIndex ordinary = f.treeIndex("src/plain.txt", ChangesTreeModel::Name);
        QVERIFY(render(treeName, ordinary, plain) != render(treeName, ordinary, bold));

        // The compact table's Name column, the same way.
        f.page->setFilesView(CommitPage::FilesView::Compact, false);
        settle();
        QAbstractItemDelegate *compactName = f.page->table()->itemDelegateForColumn(ChangesModel::Name);
        QVERIFY(compactName);
        const auto flatName = [&f](const QString &path) {
            QSortFilterProxyModel *proxy = f.page->proxy();
            for (int row = 0, rows = proxy->rowCount(); row < rows; ++row)
                if (proxy->index(row, ChangesModel::Check).data(ChangesModel::PathRole).toString() == path)
                    return proxy->index(row, ChangesModel::Name);
            return QModelIndex();
        };
        const QModelIndex flatConflict = flatName(QStringLiteral("src/clash.txt"));
        QVERIFY(flatConflict.isValid());
        QCOMPARE(render(compactName, flatConflict, plain), render(compactName, flatConflict, bold));
        const QModelIndex flatOrdinary = flatName(QStringLiteral("src/plain.txt"));
        QVERIFY(render(compactName, flatOrdinary, plain) != render(compactName, flatOrdinary, bold));
    }

    // Compact is the same table with its columns down to three, and going
    // back to the table gives the user's widths back.
    void theCompactPresentationIsTheTableWithThreeColumns()
    {
        CommitFixture f = nestedFixture();
        QVERIFY(f.page);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        QTableView *table = f.page->table();
        const int frame = table->frameWidth();
        const auto shownColumns = [table] {
            QList<int> shown;
            for (int c = 0; c < ChangesModel::ColumnCount; ++c)
                if (!table->isColumnHidden(c))
                    shown << c;
            return shown;
        };
        // The table: the design's columns for a wide page (screens.js
        // changesTable()), no Ext and one "+ −", Name taking the rest.
        const QList<int> tableColumns{int(ChangesModel::Check), int(ChangesModel::Name), int(ChangesModel::Path),
                                      int(ChangesModel::Size), int(ChangesModel::Status), int(ChangesModel::LinesAdded)};
        QCOMPARE(shownColumns(), tableColumns);
        // Name takes what the others leave, down to its floor.
        const auto nameFills = [table] {
            int others = 0;
            for (int c = 0; c < ChangesModel::ColumnCount; ++c)
                if (c != ChangesModel::Name && !table->isColumnHidden(c))
                    others += table->columnWidth(c);
            return table->columnWidth(ChangesModel::Name)
                == qMax(ui::space(ui::kMinStretchColumn), table->viewport()->width() - others);
        };
        QVERIFY(nameFills());
        table->setColumnWidth(ChangesModel::Path, 213); // a width the user chose
        const int sorted = f.header()->sortIndicatorSection();

        f.page->setFilesView(CommitPage::FilesView::Compact, false);
        settle();
        QCOMPARE(f.page->activeListView(), table); // compact is the table itself
        QCOMPARE(shownColumns(), QList<int>({int(ChangesModel::Check), int(ChangesModel::Name), int(ChangesModel::Status)}));
        // The design's 32 px from the table's outer edge, the frame inside them.
        QCOMPARE(table->columnWidth(ChangesModel::Check), ui::space(32) - frame);
        QCOMPARE(table->columnWidth(ChangesModel::Status), ui::space(32) - frame);
        QVERIFY(table->columnWidth(ChangesModel::Name) > 2 * ui::space(32)); // Name has the rest
        QVERIFY(nameFills());
        QCOMPARE(f.header()->sortIndicatorSection(), sorted);
        // The "St" heading is this presentation's alone: the shared model and
        // the proxy still call the column what they always did.
        QCOMPARE(f.page->proxy()->headerData(ChangesModel::Status, Qt::Horizontal, Qt::DisplayRole).toString(),
                 QStringLiteral("Status"));
        QCOMPARE(f.model()->headerData(ChangesModel::Status, Qt::Horizontal, Qt::DisplayRole).toString(),
                 QStringLiteral("Status"));

        // Back to the table: the width the user chose comes back.
        f.page->setFilesView(CommitPage::FilesView::Table, false);
        settle();
        QCOMPARE(shownColumns(), tableColumns);
        QCOMPARE(table->columnWidth(ChangesModel::Path), 213);
        QCOMPARE(table->columnWidth(ChangesModel::Check), ui::space(32) - frame); // the design's, not the user's
        QCOMPARE(f.header()->sortIndicatorSection(), sorted);
        QVERIFY(nameFills());

        // ...and again, and again: the widths do not drift.
        for (int round = 0; round < 3; ++round) {
            f.page->setFilesView(CommitPage::FilesView::Compact, false);
            f.page->setFilesView(CommitPage::FilesView::Table, false);
        }
        settle();
        QCOMPARE(table->columnWidth(ChangesModel::Path), 213);

        // A text size change while compact keeps the presentation; the class's
        // widths come back with it, at the new size.
        f.page->setFilesView(CommitPage::FilesView::Compact, false);
        f.page->applyTheme();
        settle();
        QCOMPARE(table->columnWidth(ChangesModel::Status), ui::space(32) - frame);
        QVERIFY(table->isColumnHidden(ChangesModel::Path));
        f.page->setFilesView(CommitPage::FilesView::Table, false);
        settle();
        QCOMPARE(table->columnWidth(ChangesModel::Path), ui::space(128));
        QVERIFY(nameFills());
    }

    // One of the three is always on, the choice is remembered, and a run
    // started with --files-view never writes it.
    void theFilesViewButtonsRememberTheChoice()
    {
        QSettings().remove(settings::kWindowFilesView);
        {
            CommitFixture f = commitFixture();
            QVERIFY(f.page);
            QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
            settle();
            const QList<QToolButton *> buttons{f.page->treeButton(), f.page->compactButton(), f.page->tableButton()};
            QStringList names;
            for (QToolButton *b : buttons) {
                names << b->accessibleName();
                QVERIFY(b->isCheckable());
                QCOMPARE(b->toolTip(), b->accessibleName());
                QCOMPARE(b->objectName(), QStringLiteral("iconButton"));
                QCOMPARE(b->size(), QSize(ui::space(24), ui::space(24)));
            }
            QCOMPARE(names, QStringList({"Tree", "Compact list", "Table"}));
            // Nothing saved is the tree, and reading a choice never writes one.
            QCOMPARE(f.page->filesView(), CommitPage::FilesView::Tree);
            QVERIFY(f.page->treeButton()->isChecked());
            QVERIFY(!QSettings().contains(settings::kWindowFilesView));

            // Exactly one at a time, and the saved choice follows the clicks.
            f.page->tableButton()->click();
            QCOMPARE(f.page->filesView(), CommitPage::FilesView::Table);
            QVERIFY(!f.page->treeButton()->isChecked() && !f.page->compactButton()->isChecked());
            QCOMPARE(QSettings().value(settings::kWindowFilesView).toString(), QStringLiteral("table"));
            f.page->compactButton()->click();
            QCOMPARE(QSettings().value(settings::kWindowFilesView).toString(), QStringLiteral("compact"));
            // Clicking the one already on changes nothing.
            f.page->compactButton()->click();
            QCOMPARE(f.page->filesView(), CommitPage::FilesView::Compact);
        }
        // A saved choice comes back; something unreadable is the tree.
        {
            CommitFixture f = commitFixture();
            QVERIFY(f.page);
            QCOMPARE(f.page->filesView(), CommitPage::FilesView::Compact);
            QVERIFY(f.page->compactButton()->isChecked());
        }
        QSettings().setValue(settings::kWindowFilesView, QStringLiteral("sideways"));
        {
            CommitFixture f = commitFixture();
            QVERIFY(f.page);
            QCOMPARE(f.page->filesView(), CommitPage::FilesView::Tree);
            QCOMPARE(QSettings().value(settings::kWindowFilesView).toString(), QStringLiteral("sideways"));
        }
        // The command line's spellings, and nothing else.
        bool ok = false;
        QCOMPARE(CommitPage::viewFromKey(QStringLiteral("tree"), &ok), CommitPage::FilesView::Tree);
        QVERIFY(ok);
        for (const QString bad : {"", "TREE", "list", "Table "}) {
            CommitPage::viewFromKey(bad, &ok);
            QVERIFY2(!ok, qPrintable(bad));
        }
        QCOMPARE(CommitPage::viewKey(CommitPage::FilesView::Compact), QStringLiteral("compact"));

        // An override run lists the files that way and saves nothing — not
        // even after the user clicks another view.
        QSettings().setValue(settings::kWindowFilesView, QStringLiteral("table"));
        {
            CommitFixture f = commitFixture();
            QVERIFY(f.page);
            f.page->setFilesViewOverride(CommitPage::FilesView::Tree);
            QCOMPARE(f.page->filesView(), CommitPage::FilesView::Tree);
            f.page->compactButton()->click();
            QCOMPARE(f.page->filesView(), CommitPage::FilesView::Compact);
            QCOMPARE(QSettings().value(settings::kWindowFilesView).toString(), QStringLiteral("table"));
        }
        QSettings().remove(settings::kWindowFilesView);
    }

    // File actions belong to the file that was clicked, whichever list it was
    // clicked in; a directory has no file menu at all.
    void theFileMenuFollowsTheClickedFile()
    {
        CommitFixture f = nestedFixture();
        QVERIFY(f.page);
        f.page->setFilesView(CommitPage::FilesView::Tree, false);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        const auto menuAt = [](QAbstractItemView *view, const QPoint &pos) {
            QStringList entries;
            QTimer::singleShot(50, [&entries] {
                auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
                if (!menu)
                    return;
                for (const QAction *a : menu->actions())
                    entries << (a->isSeparator() ? QStringLiteral("—")
                                                 : a->text() + (a->isEnabled() ? QString() : QStringLiteral(" (off)")));
                menu->close();
            });
            QContextMenuEvent event(QContextMenuEvent::Mouse, pos, view->viewport()->mapToGlobal(pos));
            QApplication::sendEvent(view->viewport(), &event);
            QTest::qWait(150);
            return entries;
        };

        // A deleted file cannot be opened, and the click makes it current.
        const QStringList deleted = menuAt(f.tree(), f.tree()->visualRect(f.treeIndex("tests/remove.txt")).center());
        QCOMPARE(deleted.size(), 3);
        QVERIFY2(deleted.first().endsWith(QStringLiteral("(off)")), qPrintable(deleted.first()));
        QCOMPARE(deleted.last(), QStringLiteral("Discard changes"));
        bool ok = false;
        QCOMPARE(f.page->currentChange(&ok).path, QStringLiteral("tests/remove.txt"));

        // A file that is there can be, and it becomes the current one too.
        const QStringList live = menuAt(f.tree(), f.tree()->visualRect(f.treeIndex("root.txt")).center());
        QCOMPARE(live.size(), 3);
        QVERIFY(!live.first().endsWith(QStringLiteral("(off)")));
        QCOMPARE(f.page->currentChange(&ok).path, QStringLiteral("root.txt"));

        // A directory and the empty space below the rows have none.
        QVERIFY(menuAt(f.tree(), f.tree()->visualRect(f.treeIndex("src")).center()).isEmpty());
        for (const QString dir : {"alpha", "Beta", "single", "src", "tests"})
            f.tree()->collapse(f.treeIndex(dir)); // leaves room under the rows
        settle();
        const QRect last = f.tree()->visualRect(f.treeIndex("top.txt"));
        QVERIFY(last.bottom() + 4 < f.tree()->viewport()->height());
        QVERIFY(!f.tree()->indexAt(QPoint(last.center().x(), last.bottom() + 4)).isValid());
        QVERIFY(menuAt(f.tree(), QPoint(last.center().x(), last.bottom() + 4)).isEmpty());
        QCOMPARE(f.page->currentChange(&ok).path, QStringLiteral("root.txt"));

        // The table's own menu is unchanged.
        f.page->setFilesView(CommitPage::FilesView::Table, false);
        settle();
        QVERIFY(f.page->selectPath(QStringLiteral("root.txt")));
        const QModelIndex row = f.page->table()->currentIndex();
        QCOMPARE(menuAt(f.page->table(), f.page->table()->visualRect(row).center()).size(), 3);
    }
};

UI_TEST(FilesTest);

#include "files_test.moc"
