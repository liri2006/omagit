// The commit page around its file list: the message box that grows with its
// text and keeps its height, the options menus that mirror the page, and the
// title, Commit and Amend labels that count the checked files.
#include "UiTestCase.h"
#include "fixtures.h"

#include "../../src/Settings.h"
#include "../../src/TickMenu.h"
#include "../../src/UiHelpers.h"

#include <QApplication>
#include <QClipboard>
#include <QLayout>
#include <QSignalSpy>

namespace {

// Whether each of those entries is checked.
QList<bool> checkedStates(const QList<QAction *> &actions)
{
    QList<bool> out;
    for (const QAction *a : actions)
        out << a->isChecked();
    return out;
}

} // namespace

class CommitPageTest : public UiTestCase
{
    Q_OBJECT
private slots:
    // A handle dragged with the header rows folded away is saved as the
    // box's height: a page opened with the rows gives the box that height,
    // and keeps it when they fold away again — down to the box's own
    // minimum, and between it and the MESSAGE row plus that minimum, which
    // the rows would clamp it to were they there when the state comes back.
    // A drag with the rows saves an ordinary state. The flag with no state,
    // or with one that does not read, moves nothing and goes.
    void theMessageHeightSavedFoldedComesBackTheSame()
    {
        QSettings().remove(settings::kWindowCommitSplitter);
        QSettings().remove(settings::kWindowCommitSplitterFolded);
        const auto open = [](CommitFixture &f) {
            f.page->resize(500, 800);
            f.page->show();
            return QTest::qWaitForWindowExposed(f.page.get());
        };
        // The box's height with nothing saved.
        int resting = 0;
        {
            CommitFixture f = commitFixture();
            QVERIFY(f.page);
            QVERIFY(open(f));
            settle();
            resting = f.page->findChild<MessageEdit *>()->height();
        }
        for (const int design : {200, 28, 44}) {
            const int dragged = ui::space(design);
            const auto where = [dragged](int box) {
                return QStringLiteral("dragged to %1, the box %2").arg(dragged).arg(box).toUtf8();
            };
            {
                CommitFixture f = commitFixture();
                QVERIFY(f.page);
                QVERIFY(open(f));
                settle();
                f.page->setHeaderRowsHidden(true);
                settle();
                auto *splitter = f.page->findChild<QSplitter *>(QStringLiteral("commitMessageSplitter"));
                QVERIFY(splitter);
                const int total = splitter->sizes().at(0) + splitter->sizes().at(1);
                splitter->setSizes({total - dragged, dragged});
                emit splitter->splitterMoved(total - dragged, 1);
                settle();
                QVERIFY(QSettings().value(settings::kWindowCommitSplitterFolded).toBool());
                const int box = f.page->findChild<MessageEdit *>()->height();
                QVERIFY2(box == dragged, where(box).constData());
            }
            {
                CommitFixture f = commitFixture();
                QVERIFY(f.page);
                QVERIFY(open(f));
                settle();
                MessageEdit *message = f.page->findChild<MessageEdit *>();
                QVERIFY(!f.page->headerRowsHidden());
                QVERIFY2(message->height() == dragged, where(message->height()).constData());
                f.page->setHeaderRowsHidden(true);
                settle();
                QVERIFY2(message->height() == dragged, where(message->height()).constData());
                f.page->setHeaderRowsHidden(false);
                settle();
                QVERIFY2(message->height() == dragged, where(message->height()).constData());
                auto *splitter = f.page->findChild<QSplitter *>(QStringLiteral("commitMessageSplitter"));
                emit splitter->splitterMoved(splitter->sizes().at(0), 1);
                QVERIFY(QSettings().contains(settings::kWindowCommitSplitter));
                QVERIFY(!QSettings().contains(settings::kWindowCommitSplitterFolded));
            }
        }

        // The flag alone, and the flag over a state that does not read.
        for (const QByteArray &state : {QByteArray(), QByteArrayLiteral("garbage")}) {
            QSettings conf;
            conf.remove(settings::kWindowCommitSplitter);
            if (!state.isEmpty())
                conf.setValue(settings::kWindowCommitSplitter, state);
            conf.setValue(settings::kWindowCommitSplitterFolded, true);
            conf.sync();
            CommitFixture f = commitFixture();
            QVERIFY(f.page);
            QVERIFY(!QSettings().contains(settings::kWindowCommitSplitterFolded));
            QVERIFY(open(f));
            settle();
            QVERIFY(!f.page->headerRowsHidden());
            QCOMPARE(f.page->findChild<MessageEdit *>()->height(), resting);
        }
        QSettings().remove(settings::kWindowCommitSplitter);
    }

    // The options menu says what Amend says at the moment it opens, and
    // nothing else: the eye stays in the CHANGES row (or, with the rows
    // folded away, in the top bar's More menu).
    void theOptionsMenuMirrorsThePage()
    {
        CommitFixture f = commitFixture();
        QVERIFY(f.page);
        CommitPage *page = f.page.get();
        page->setStacked(true);
        QMenu *menu = page->optionsButton()->menu();

        QList<QAction *> actions = filledMenu(menu);
        QCOMPARE(menuTexts(actions),
                 QStringList({ui::icon(ui::kUndo, QStringLiteral("A  ")) + QStringLiteral("Amend last commit")}));
        QVERIFY(actions.at(0)->isCheckable() && !actions.at(0)->isChecked());
        QVERIFY(actions.at(0)->isEnabled());

        // A merge in progress: no amending.
        MergeState merge;
        merge.inProgress = true;
        merge.source = QStringLiteral("feature");
        page->setMergeState(merge, f.repo->headCommit());
        actions = filledMenu(menu);
        QVERIFY(!actions.at(0)->isEnabled());
        QCOMPARE(actions.at(0)->toolTip(), QStringLiteral("Not while a merge is in progress"));
        QCOMPARE(actions.at(0)->toolTip(), f.amend()->toolTip());
    }

    // Each entry takes the path its control takes.
    void theOptionsMenuActsThroughThePage()
    {
        CommitFixture f = commitFixture();
        QVERIFY(f.page);
        CommitPage *page = f.page.get();
        page->setStacked(true);
        QMenu *menu = page->optionsButton()->menu();

        QSignalSpy amended(page, &CommitPage::amendToggled);
        filledMenu(menu).at(0)->trigger();
        QVERIFY(f.amend()->isChecked());
        QCOMPARE(amended.count(), 1);
        QVERIFY(filledMenu(menu).at(0)->isChecked());
        filledMenu(menu).at(0)->trigger();
        QVERIFY(!f.amend()->isChecked());
    }

    // The header rows' controls as menu entries: a Files view submenu with
    // the three presentations, the current one ticked and wearing its glyph,
    // and the eye, each read as the buttons are and taking their path.
    void theHeaderOptionsMirrorAndActThroughThePage()
    {
        QSettings().remove(settings::kWindowFilesView);
        CommitFixture f = commitFixture();
        QVERIFY(f.page);
        CommitPage *page = f.page.get();
        page->setFilesView(CommitPage::FilesView::Table, false);
        QMenu menu;
        page->addHeaderOptions(&menu);
        QList<QAction *> actions = menu.actions();
        QCOMPARE(menuTexts(actions), QStringList({ui::icon(ui::kTable) + QStringLiteral("Files view"),
                                                  ui::icon(ui::kEye) + QStringLiteral("Show unversioned files")}));
        QMenu *views = actions.at(0)->menu();
        QVERIFY(views);
        QVERIFY(qobject_cast<TickMenu *>(views));
        QCOMPARE(menuTexts(views->actions()),
                 QStringList({ui::icon(ui::kFileTree) + QStringLiteral("Tree"),
                              ui::icon(ui::kFormatListBulleted) + QStringLiteral("Compact list"),
                              ui::icon(ui::kTable) + QStringLiteral("Table")}));
        QCOMPARE(checkedStates(views->actions()), QList<bool>({false, false, true}));
        QVERIFY(actions.at(1)->isCheckable() && actions.at(1)->isChecked());
        QCOMPARE(actions.at(1)->toolTip(), f.eye()->toolTip());

        // Tree, through its button: the choice is saved as a click saves it.
        views->actions().at(0)->trigger();
        QCOMPARE(page->filesView(), CommitPage::FilesView::Tree);
        QVERIFY(page->treeButton()->isChecked());
        QCOMPARE(QSettings().value(settings::kWindowFilesView).toString(), QStringLiteral("tree"));
        // The eye, through its button: the unversioned files go.
        actions.at(1)->trigger();
        QVERIFY(!f.eye()->isChecked());
        QCOMPARE(page->proxy()->rowCount(), 2);

        // Filled again, it says so.
        QMenu again;
        page->addHeaderOptions(&again);
        QCOMPARE(again.actions().at(0)->text(), ui::icon(ui::kFileTree) + QStringLiteral("Files view"));
        QCOMPARE(checkedStates(again.actions().at(0)->menu()->actions()), QList<bool>({true, false, false}));
        QVERIFY(!again.actions().at(1)->isChecked());
        QSettings().remove(settings::kWindowFilesView);
    }

    void messageHeightCountsWrappedLinesAndIsMeasuredOncePerBurst()
    {
        MessageEdit edit;
        edit.resize(300, 100);
        edit.show();
        QVERIFY(QTest::qWaitForWindowExposed(&edit));
        const int spacing = edit.fontMetrics().lineSpacing();

        // One line: a box as low as a field holds it (centred).
        const int oneLine = edit.contentHeight();
        QCOMPARE(oneLine, ui::space(ui::box::control));
        QVERIFY(oneLine < edit.height()); // an empty box is taller than its text
        // More: the lines 8 from the box's edges (its frame, its margins and
        // the document's together), one line spacing a line.
        edit.setPlainText(QStringLiteral("one\ntwo"));
        const int twoLines = edit.contentHeight();
        QCOMPARE(twoLines, 2 * spacing + 2 * ui::space(ui::pad::control));
        edit.setPlainText(QStringLiteral("one\ntwo\nthree"));
        QCOMPARE(edit.contentHeight(), twoLines + spacing);

        // One paragraph the box has to wrap counts as the lines it takes on
        // screen, not as the single block it is.
        QSignalSpy spy(&edit, &MessageEdit::contentHeightChanged);
        edit.setPlainText(QString(QStringLiteral("word ")).repeated(120));
        QVERIFY(edit.contentHeight() > edit.height());
        QVERIFY(edit.contentHeight() > oneLine + 8 * spacing);

        // A narrower box wraps the same text into more lines, and says so:
        // the window sizes its panes after the text has arrived (--amend).
        settle();
        const int wide = edit.contentHeight();
        spy.clear();
        edit.resize(150, 100);
        settle();
        QVERIFY(edit.contentHeight() > wide);
        QCOMPARE(spy.count(), 1);
        const auto edited = [&spy] { return spy.at(0).at(0).value<MessageEdit::Edit>(); };
        QCOMPARE(edited(), MessageEdit::Edit::Typed); // re-wrapped, not pasted

        // The agent streams a message in many partials: one measurement, and
        // a pasted one. (From a short text, so no scrollbar comes or goes:
        // that resizes the viewport, which is a measurement of its own.)
        edit.setPlainText(QStringLiteral("short"));
        settle();
        spy.clear();
        for (int i = 0; i < 5; ++i)
            edit.replaceText(QStringLiteral("partial %1").arg(i), i > 0);
        settle();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(edited(), MessageEdit::Edit::Pasted);

        // Typed text is not pasted; text from the clipboard is; taking text
        // out is a deletion.
        spy.clear();
        QTest::keyClicks(&edit, QStringLiteral("typed"));
        settle();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(edited(), MessageEdit::Edit::Typed);
        spy.clear();
        QApplication::clipboard()->setText(QStringLiteral("clip"));
        edit.paste();
        settle();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(edited(), MessageEdit::Edit::Pasted);
        spy.clear();
        QTest::keyClick(&edit, Qt::Key_Backspace);
        settle();
        QCOMPARE(spy.count(), 1);
        QCOMPARE(edited(), MessageEdit::Edit::Deleted);
    }

    void commitMessagePaneFollowsItsText()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(git(dir.path(), {"init", "-q", "-b", "main"}));
        QVERIFY(commit(dir.path(), QStringLiteral("A"), 1));

        GitRepo repo(dir.path());
        // The page asks the agent CLIs on PATH for their models as it is
        // built; an empty PATH keeps those processes out of this test.
        const QByteArray path = qgetenv("PATH");
        qputenv("PATH", dir.path().toUtf8());
        CommitPage page(&repo);
        qputenv("PATH", path);
        page.resize(700, 800);
        page.show();
        QVERIFY(QTest::qWaitForWindowExposed(&page));
        settle();

        auto *splitter = page.findChild<QSplitter *>(QStringLiteral("commitMessageSplitter"));
        auto *message = page.findChild<MessageEdit *>();
        QVERIFY(splitter);
        QVERIFY(message);
        // The section grid: the handle between the changes and the message is
        // the block gap the design puts between the parts of a pane (the
        // regular 8 of a page no window has classified; the stylesheet's
        // handle must not win), and the header rows carry 24 px squares.
        const int block = ui::space(ui::kRegularDensity.block);
        QCOMPARE(splitter->handleWidth(), block);
        QCOMPARE(splitter->handle(1)->height(), block);
        // ...and the action bar hangs the same block gap under the message.
        QCOMPARE(page.layout()->spacing(), block);
        QList<QToolButton *> squares = page.findChildren<QToolButton *>(QStringLiteral("iconButton"));
        // the agent cog, the three files-view buttons, the unversioned eye and
        // Refresh; the stacked action bar's options button is a toolbar one
        squares.removeOne(page.optionsButton());
        QCOMPARE(squares.size(), 6);
        for (const QToolButton *square : squares)
            QCOMPARE(square->size(), QSize(ui::space(ui::box::row), ui::space(ui::box::row)));

        // The message's pane is the MESSAGE row and its gap over the box;
        // what follows measures the box.
        const int header = ui::space(ui::box::row) + ui::space(ui::gap::header);
        const auto pane = [splitter, header] { return splitter->sizes().at(1) - header; };
        const int initial = pane();
        const int total = splitter->sizes().at(0) + splitter->sizes().at(1);
        QVERIFY(initial > 0);
        QCOMPARE(message->height(), initial);

        // A message that fits is left alone.
        message->setPlainText(QStringLiteral("a one line subject"));
        settle();
        QCOMPARE(pane(), initial);

        // One that does not gets exactly the height it needs...
        message->setPlainText(QStringLiteral("a line of the message\n").repeated(12));
        settle();
        const int grown = pane();
        QVERIFY2(grown > initial, qPrintable(QStringLiteral("pane stayed at %1").arg(grown)));
        QCOMPARE(grown, message->contentHeight());
        QCOMPARE(message->height(), grown);
        QVERIFY(grown <= splitter->height() / 2);
        // The room came out of the changes list, not out of thin air.
        QCOMPARE(splitter->sizes().at(0) + splitter->sizes().at(1), total);
        QCOMPARE(splitter->sizes().at(0), total - grown - header);

        // ...and gives it back when the message is cut short (to the height
        // the box had at the start, not to the one line the text needs).
        message->setPlainText(QStringLiteral("short again"));
        settle();
        QCOMPARE(pane(), initial);

        // Half of the splitter is the ceiling, however long the message.
        message->setPlainText(QStringLiteral("a line of the message\n").repeated(200));
        settle();
        QCOMPARE(pane(), splitter->height() / 2);
        QVERIFY(message->contentHeight() > pane()); // it really was capped

        // Growing is not the user's choice, so it is not remembered.
        QVERIFY(!QSettings().contains(settings::kWindowCommitSplitter));

        // Once the user has dragged the handle, typing leaves the size alone
        // (a box can be made smaller than its text)...
        const auto dragTo = [splitter, total, header](int height) {
            splitter->setSizes({total - height - header, height + header});
            emit splitter->splitterMoved(total - height - header, 1);
        };
        message->setPlainText(QStringLiteral("short"));
        settle(); // the cut lands before the drag, as it would for a person
        dragTo(initial);
        settle();
        QCOMPARE(pane(), initial);
        message->setFocus();
        for (int i = 0; i < 30; ++i)
            QTest::keyClick(message, Qt::Key_Return);
        settle();
        QCOMPARE(pane(), initial);
        QVERIFY(message->contentHeight() > initial);

        // ...but a message from the agent grows it again...
        message->replaceText(QStringLiteral("a line from the agent\n").repeated(12));
        settle();
        QCOMPARE(pane(), qMin(message->contentHeight(), splitter->height() / 2));
        QVERIFY(pane() > initial);

        // ...and so does a paste.
        dragTo(initial);
        settle();
        QCOMPARE(pane(), initial);
        QApplication::clipboard()->setText(QStringLiteral("a pasted line\n").repeated(12));
        message->paste();
        settle();
        QVERIFY(pane() > initial);
        QCOMPARE(pane(), qMin(message->contentHeight(), splitter->height() / 2));

        // Deleting lines shrinks the box back to the text, and from then on
        // typing grows it again (the dragged size is forgotten)...
        const int twelve = pane();
        QTest::keyClick(message, Qt::Key_End, Qt::ControlModifier);
        for (int i = 0; i < 4; ++i)
            QTest::keyClick(message, Qt::Key_Up, Qt::ShiftModifier);
        QTest::keyClick(message, Qt::Key_Backspace);
        settle();
        QVERIFY(pane() < twelve);
        QCOMPARE(pane(), message->contentHeight());
        const int eight = pane();
        for (int i = 0; i < 4; ++i)
            QTest::keyClick(message, Qt::Key_Return);
        settle();
        QVERIFY(pane() > eight);

        // ...and clearing it out goes back to the resting height, not lower.
        QTest::keyClick(message, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(message, Qt::Key_Backspace);
        settle();
        QCOMPARE(pane(), initial);
        QVERIFY(message->contentHeight() < initial);
    }

    // The count is part of the section's title, and the Commit button says
    // how many files it would take, with the key that presses it.
    void theTitleAndTheCommitButtonCountTheCheckedFiles()
    {
        CommitFixture f = commitFixture();
        QVERIFY(f.page);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        // The button's text carries the glyph and the key that presses it;
        // its accessible name is the wording alone, and follows it.
        const auto says = [&f](const QString &label) {
            return f.commitButton()->text().endsWith(label + QStringLiteral("  ⏎"))
                && f.commitButton()->accessibleName() == label;
        };

        QCOMPARE(f.title(), QStringLiteral("CHANGES · 2/4"));
        QVERIFY(says(QStringLiteral("Commit 2 files")));
        QVERIFY(f.commitButton()->isEnabled());

        f.model()->setAllChecked(true);
        QCOMPARE(f.title(), QStringLiteral("CHANGES · 4/4"));
        QVERIFY(says(QStringLiteral("Commit 4 files")));

        // Nothing checked: no count, and nothing to press either.
        f.model()->setAllChecked(false);
        QCOMPARE(f.title(), QStringLiteral("CHANGES · 0/4"));
        QVERIFY(says(QStringLiteral("Commit")));
        QVERIFY(!f.commitButton()->isEnabled());

        // One file is a file, not "1 files".
        f.model()->setPathsChecked({QStringLiteral("a.txt")}, true);
        QCOMPARE(f.title(), QStringLiteral("CHANGES · 1/4"));
        QVERIFY(says(QStringLiteral("Commit 1 file")));

        // Amending and a merge in progress keep their own wording, count or
        // no count. (Amend first: a merge rules the checkbox out.)
        f.page->setAmendChecked(true);
        QVERIFY(says(QStringLiteral("Amend")));
        f.page->setAmendChecked(false);
        MergeState merge;
        merge.inProgress = true;
        f.page->setMergeState(merge, Commit());
        QVERIFY(says(QStringLiteral("Commit merge")));
        merge.inProgress = false;
        f.page->setMergeState(merge, Commit());
        QVERIFY(says(QStringLiteral("Commit 1 file")));

        // An empty list is the section's name on its own.
        f.model()->setChanges({});
        QCOMPARE(f.title(), QStringLiteral("CHANGES"));
        QVERIFY(says(QStringLiteral("Commit")));
    }

    // The action bar holds the amend checkbox and the Commit button on one
    // line; too narrow for both, and the checkbox goes by its short name.
    void theAmendLabelShortensOnANarrowPage()
    {
        CommitFixture f = commitFixture();
        QVERIFY(f.page);
        QVERIFY(QTest::qWaitForWindowExposed(f.page.get()));
        settle();
        QCOMPARE(f.amend()->text(), QStringLiteral("Amend last commit"));
        // The empty middle of the row is nobody's: the checkbox is as wide as
        // its label, not as wide as the space the button leaves.
        QCOMPARE(f.amend()->width(), f.amend()->sizeHint().width());
        f.page->resize(260, 600);
        settle();
        QCOMPARE(f.amend()->text(), QStringLiteral("Amend"));
        f.page->resize(760, 600);
        settle();
        QCOMPARE(f.amend()->text(), QStringLiteral("Amend last commit"));
    }
};

UI_TEST(CommitPageTest);

#include "commitpage_test.moc"
