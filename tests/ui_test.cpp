// Build in tests: qmake6 ui.pro -o Makefile.ui && make -f Makefile.ui
// Run: QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME= QT_STYLE_OVERRIDE=Fusion ../build/tests/ui_test
// Exercises the pure logic behind the widgets: the history graph layout, the
// changes model's check marks, the toolbar's overflow, the keybindings filter,
// the theme's colors.toml parsing and the merge verdict's wording.
#include "../src/ChangesModel.h"
#include "../src/CommitPage.h"
#include "../src/HistoryModel.h"
#include "../src/KeybindingsPanel.h"
#include "../src/MergeDialog.h"
#include "../src/MessageEdit.h"
#include "../src/OmarchyTheme.h"
#include "../src/Settings.h"
#include "../src/Toolbar.h"
#include "../src/UiHelpers.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QListView>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalSpy>
#include <QSplitter>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

#include <memory>

// The theme the whole run shares; the theme test puts a fresh one here after
// it has pointed OmarchyTheme at a scratch directory of its own.
static std::unique_ptr<OmarchyTheme> g_theme;

namespace {

// Committer dates decide the order `git log --date-order` returns, so every
// commit gets one of its own: 2024-01-01 01:00, 02:00, ...
QString stamp(int hour)
{
    return QStringLiteral("2024-01-01T%1:00:00+00:00").arg(hour, 2, 10, QLatin1Char('0'));
}

bool git(const QString &dir, const QStringList &args, int hour = 0)
{
    QProcess p;
    p.setWorkingDirectory(dir);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("GIT_AUTHOR_NAME"), QStringLiteral("Test"));
    env.insert(QStringLiteral("GIT_AUTHOR_EMAIL"), QStringLiteral("test@example.com"));
    env.insert(QStringLiteral("GIT_COMMITTER_NAME"), QStringLiteral("Test"));
    env.insert(QStringLiteral("GIT_COMMITTER_EMAIL"), QStringLiteral("test@example.com"));
    if (hour > 0) {
        env.insert(QStringLiteral("GIT_AUTHOR_DATE"), stamp(hour));
        env.insert(QStringLiteral("GIT_COMMITTER_DATE"), stamp(hour));
    }
    p.setProcessEnvironment(env);
    p.start(QStringLiteral("git"), QStringList{QStringLiteral("-c"), QStringLiteral("commit.gpgsign=false")} + args);
    if (!p.waitForFinished(15000) || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        qWarning("git %s failed: %s", qPrintable(args.join(' ')), p.readAllStandardError().constData());
        return false;
    }
    return true;
}

bool commit(const QString &dir, const QString &message, int hour)
{
    return git(dir, {QStringLiteral("commit"), QStringLiteral("--allow-empty"), QStringLiteral("-q"),
                     QStringLiteral("-m"), message}, hour);
}

FileChange change(const QString &path, FileChange::Kind kind)
{
    FileChange c;
    c.path = path;
    c.kind = kind;
    return c;
}

MergePreview preview(MergePreview::Outcome outcome)
{
    MergePreview p;
    p.outcome = outcome;
    p.source = QStringLiteral("feature");
    p.destination = QStringLiteral("main");
    p.commits = 2;
    p.diverged = 1;
    p.files = 3;
    p.added = 10;
    p.removed = 4;
    return p;
}

// The queued height check of the message box runs from the event loop, so
// the assertions have to let it.
void settle()
{
    QTest::qWait(30);
}

// The buttons of a toolbar that are on screen, in the order they were added.
QList<bool> visible(const QList<QToolButton *> &buttons)
{
    QList<bool> out;
    for (const QToolButton *b : buttons)
        out << b->isVisible();
    return out;
}

} // namespace

class UiTest : public QObject
{
    Q_OBJECT
private slots:
    // --- HistoryModel -------------------------------------------------------

    void graphOfLinearHistory()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(git(dir.path(), {"init", "-q", "-b", "main"}));
        QVERIFY(commit(dir.path(), QStringLiteral("A"), 1));
        QVERIFY(commit(dir.path(), QStringLiteral("B"), 2));
        QVERIFY(commit(dir.path(), QStringLiteral("C"), 3));

        GitRepo repo(dir.path());
        HistoryModel model(&repo);
        model.reload();
        QCOMPARE(model.rowCount(), 3);
        QCOMPARE(model.laneCount(), 1);
        for (int row = 0; row < 3; ++row)
            QCOMPARE(model.graph(row).lane, 0);
        // The tip starts its line, the root ends it.
        QCOMPARE(model.graph(0).edges.size(), 1);
        QCOMPARE(model.graph(0).edges.first().from, -1);
        QCOMPARE(model.graph(0).edges.first().to, 0);
        QCOMPARE(model.graph(2).edges.size(), 1);
        QCOMPARE(model.graph(2).edges.first().from, 0);
        QCOMPARE(model.graph(2).edges.first().to, -1);
        QVERIFY(model.isHead(0));
    }

    // Two branches that are merged one after the other: the second one must
    // take the lane the first gave back instead of opening a third.
    void graphOfBranchesAndMerges()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path();
        QVERIFY(git(path, {"init", "-q", "-b", "main"}));
        QVERIFY(commit(path, QStringLiteral("A"), 1));
        QVERIFY(git(path, {"branch", "f1"}));
        QVERIFY(commit(path, QStringLiteral("B"), 2));
        QVERIFY(git(path, {"checkout", "-q", "f1"}));
        QVERIFY(commit(path, QStringLiteral("C"), 3));
        QVERIFY(git(path, {"checkout", "-q", "main"}));
        QVERIFY(git(path, {"merge", "--no-ff", "--no-edit", "-q", "-m", "M1", "f1"}, 4));
        QVERIFY(git(path, {"branch", "f2"}));
        QVERIFY(git(path, {"checkout", "-q", "f2"}));
        QVERIFY(commit(path, QStringLiteral("D"), 5));
        QVERIFY(git(path, {"checkout", "-q", "main"}));
        QVERIFY(git(path, {"merge", "--no-ff", "--no-edit", "-q", "-m", "M2", "f2"}, 6));

        GitRepo repo(dir.path());
        HistoryModel model(&repo);
        model.reload();
        QCOMPARE(model.rowCount(), 6);
        QStringList subjects;
        for (int row = 0; row < model.rowCount(); ++row)
            subjects << model.commit(row).subject;
        QCOMPARE(subjects, QStringList({"M2", "D", "M1", "C", "B", "A"}));
        // The second branch reuses lane 1, so two lanes hold the whole graph.
        QCOMPARE(model.laneCount(), 2);
        QList<int> lanes;
        for (int row = 0; row < model.rowCount(); ++row)
            lanes << model.graph(row).lane;
        QCOMPARE(lanes, QList<int>({0, 1, 0, 1, 0, 1}));
        // The merge branches out of its node into the second parent's lane.
        const GraphRow &merge = model.graph(0);
        QCOMPARE(merge.edges.size(), 2);
        QCOMPARE(merge.edges.last().from, -1);
        QCOMPARE(merge.edges.last().to, 1);
    }

    // --- ChangesModel -------------------------------------------------------

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

    // --- Toolbar ------------------------------------------------------------

    void buttonsFoldIntoTheMoreMenuFromTheRight()
    {
        Toolbar bar;
        auto *leading = ui::toolButton(QStringLiteral("L"));
        bar.setLeading(leading);
        QList<QToolButton *> buttons;
        QWidget *separator = nullptr;
        const QStringList names{QStringLiteral("Fetch"), QStringLiteral("Pull"), QStringLiteral("Push"),
                                QStringLiteral("Merge")};
        for (int i = 0; i < names.size(); ++i) {
            auto *button = ui::toolButton(names.at(i));
            buttons << button;
            bar.addButton(button, names.at(i), names.at(i).left(1), names.at(i));
            if (i != 1)
                continue;
            // The separator between Pull and Push: whatever addSeparator() adds.
            const QList<QWidget *> before = bar.findChildren<QWidget *>();
            bar.addSeparator();
            for (QWidget *child : bar.findChildren<QWidget *>())
                if (!before.contains(child))
                    separator = child;
        }
        QVERIFY(separator);
        bar.resize(bar.sizeHint());
        bar.show();
        QVERIFY(QTest::qWaitForWindowExposed(&bar));

        const int wide = bar.sizeHint().width();
        bar.resize(wide + 40, bar.sizeHint().height());
        QCoreApplication::processEvents();
        QCOMPARE(visible(buttons), QList<bool>({true, true, true, true}));
        for (int i = 0; i < buttons.size(); ++i)
            QCOMPARE(buttons.at(i)->text(), names.at(i)); // full labels while there is room
        QVERIFY(separator->isVisible());

        // Shrinking hides the buttons from the right, never from the middle.
        int hiddenAt = -1;
        for (int width = wide + 40; width >= bar.minimumSizeHint().width(); width -= 8) {
            bar.resize(width, bar.sizeHint().height());
            QCoreApplication::processEvents();
            const QList<bool> shown = visible(buttons);
            bool seenHidden = false;
            for (int i = 0; i < shown.size(); ++i) {
                if (!shown.at(i))
                    seenHidden = true;
                else
                    QVERIFY2(!seenHidden, "a button folded away while a later one stayed");
            }
            if (seenHidden && hiddenAt < 0)
                hiddenAt = width;
            if (seenHidden) {
                QVERIFY(bar.findChild<QToolButton *>()); // the more button takes over
                if (!shown.at(2)) // Push and Merge gone: the separator has nothing to separate
                    QVERIFY(!separator->isVisible());
            }
            QVERIFY(leading->isVisible()); // the layout switcher never folds away
        }
        QVERIFY2(hiddenAt > 0, "nothing ever folded away");

        // At its narrowest only the leading button and the more menu are left.
        bar.resize(bar.minimumSizeHint().width(), bar.sizeHint().height());
        QCoreApplication::processEvents();
        QCOMPARE(visible(buttons), QList<bool>({false, false, false, false}));

        // And they all come back when the room does.
        bar.resize(wide + 40, bar.sizeHint().height());
        QCoreApplication::processEvents();
        QCOMPARE(visible(buttons), QList<bool>({true, true, true, true}));
        QVERIFY(separator->isVisible());
    }

    // --- KeybindingsPanel ---------------------------------------------------

    void keybindingsFilterMatchesTheSpelledOutKeys()
    {
        QWidget host;
        host.resize(800, 600);
        auto *panel = new KeybindingsPanel(&host);
        panel->setAttribute(Qt::WA_DeleteOnClose, false);
        panel->add(QStringLiteral("CTRL + F"), QStringLiteral("Fetch"));
        panel->add(QStringLiteral("CTRL SHIFT + P"), QStringLiteral("Push"), QStringLiteral("everywhere"));
        panel->add(QStringLiteral("F5 / CTRL SHIFT + R"), QStringLiteral("Refresh"));

        auto *search = panel->findChild<QLineEdit *>(QStringLiteral("keybindingsSearch"));
        auto *list = panel->findChild<QListView *>(QStringLiteral("keybindingsList"));
        QVERIFY(search);
        QVERIFY(list);
        QAbstractItemModel *model = list->model();
        QCOMPARE(model->rowCount(), 3);

        // "ctrl+f" finds the row the menu spells "CTRL + F".
        search->setText(QStringLiteral("ctrl+f"));
        QCOMPARE(model->rowCount(), 1);
        QCOMPARE(model->index(0, 0).data(Qt::DisplayRole).toString(), QStringLiteral("Fetch"));
        QCOMPARE(model->index(0, 0).data(Qt::UserRole).toString(), QStringLiteral("CTRL + F"));

        // "ctrl f" finds it too; the terms match one by one, so the rows
        // whose SHIFT carries an f are along for the ride.
        search->setText(QStringLiteral("ctrl f"));
        QVERIFY(model->rowCount() >= 1);
        bool foundFetch = false;
        for (int row = 0; row < model->rowCount(); ++row)
            foundFetch |= model->index(row, 0).data(Qt::UserRole).toString() == QStringLiteral("CTRL + F");
        QVERIFY(foundFetch);
        search->setText(QStringLiteral("ctrl shift"));
        QCOMPARE(model->rowCount(), 2);
        search->setText(QStringLiteral("push"));
        QCOMPARE(model->rowCount(), 1);
        search->setText(QStringLiteral("everywhere")); // the context counts too
        QCOMPARE(model->rowCount(), 1);
        search->setText(QStringLiteral("nothing here"));
        QCOMPARE(model->rowCount(), 0);
        search->clear();
        QCOMPARE(model->rowCount(), 3);
        panel->close();
    }

    // --- MessageEdit / CommitPage: the message box grows to fit -------------

    void messageHeightCountsWrappedLinesAndIsMeasuredOncePerBurst()
    {
        MessageEdit edit;
        edit.resize(300, 100);
        edit.show();
        QVERIFY(QTest::qWaitForWindowExposed(&edit));
        const int spacing = edit.fontMetrics().lineSpacing();

        const int oneLine = edit.contentHeight();
        QVERIFY(oneLine > spacing);
        QVERIFY(oneLine < edit.height()); // an empty box is taller than its text
        edit.setPlainText(QStringLiteral("one\ntwo\nthree"));
        QCOMPARE(edit.contentHeight(), oneLine + 2 * spacing);

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
        const auto pane = [splitter] { return splitter->sizes().at(0); };
        const int initial = pane();
        const int total = splitter->sizes().at(0) + splitter->sizes().at(1);
        QVERIFY(initial > 0);

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
        QVERIFY(grown <= splitter->height() / 2);
        // The room came out of the changes list, not out of thin air.
        QCOMPARE(splitter->sizes().at(0) + splitter->sizes().at(1), total);
        QCOMPARE(splitter->sizes().at(1), total - grown);

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
        QVERIFY(!QSettings().contains(settings::kWindowCommitMessageSplitter));

        // Once the user has dragged the handle, typing leaves the size alone
        // (a box can be made smaller than its text)...
        const auto dragTo = [splitter, total](int height) {
            splitter->setSizes({height, total - height});
            emit splitter->splitterMoved(height, 1);
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

    // --- mergeVerdict() -----------------------------------------------------

    void verdictForEveryOutcome()
    {
        const QString main = QStringLiteral("main");

        MergeVerdict same = mergeVerdict(preview(MergePreview::Same), false, main);
        QCOMPARE(same.kind, MergeVerdict::Info);
        QCOMPARE(same.headline, QStringLiteral("Pick two different branches."));
        QCOMPARE(same.detail, QStringList({QStringLiteral("The same branch is on both sides.")}));
        QVERIFY(!same.canMerge);

        MergeVerdict upToDate = mergeVerdict(preview(MergePreview::UpToDate), false, main);
        QCOMPARE(upToDate.kind, MergeVerdict::Info);
        QCOMPARE(upToDate.headline, QStringLiteral("Nothing to merge."));
        QCOMPARE(upToDate.detail, QStringList({QStringLiteral("main already has every commit of feature.")}));
        QVERIFY(!upToDate.canMerge);

        MergeVerdict fastForward = mergeVerdict(preview(MergePreview::FastForward), false, main);
        QCOMPARE(fastForward.kind, MergeVerdict::Good);
        QVERIFY(fastForward.headline.startsWith(QStringLiteral("Fast-forward")));
        QVERIFY(fastForward.detail.first().contains(QStringLiteral("main simply moves up 2 commits")));
        QCOMPARE(fastForward.detail.size(), 2); // the move, then the file statistics
        QVERIFY(fastForward.detail.last().contains(QStringLiteral("3 files changed")));
        QVERIFY(fastForward.canMerge);
        QVERIFY(fastForward.buttonTip.contains(QStringLiteral("Fast-forward")));

        // The box below the card asks for a merge commit: no fast-forward then.
        MergeVerdict noFf = mergeVerdict(preview(MergePreview::FastForward), true, main);
        QCOMPARE(noFf.kind, MergeVerdict::Good);
        QVERIFY(noFf.headline.contains(QStringLiteral("no conflicts")));
        QVERIFY(noFf.detail.first().contains(QStringLiteral("could simply move up")));
        QVERIFY(noFf.canMerge);
        QVERIFY(noFf.buttonTip.contains(QStringLiteral("merge commit")));

        MergeVerdict clean = mergeVerdict(preview(MergePreview::Clean), false, main);
        QCOMPARE(clean.kind, MergeVerdict::Good);
        QVERIFY(clean.headline.contains(QStringLiteral("no conflicts")));
        QCOMPARE(clean.detail.size(), 2);
        QVERIFY(clean.detail.first().contains(QStringLiteral("2 commits from feature")));
        QVERIFY(clean.canMerge);
        QVERIFY(clean.files.isEmpty());

        MergePreview conflicting = preview(MergePreview::Conflicts);
        conflicting.conflicts = QStringList({QStringLiteral("a.txt"), QStringLiteral("b.txt")});
        MergeVerdict conflicts = mergeVerdict(conflicting, false, main);
        QCOMPARE(conflicts.kind, MergeVerdict::Bad);
        QCOMPARE(conflicts.headline, QStringLiteral("2 files would conflict."));
        QCOMPARE(conflicts.files, conflicting.conflicts);
        QVERIFY(conflicts.canMerge); // the merge may still be started and resolved
        QVERIFY(conflicts.buttonTip.contains(QStringLiteral("Start the merge")));
        conflicting.conflicts = QStringList({QStringLiteral("a.txt")});
        QCOMPARE(mergeVerdict(conflicting, false, main).headline, QStringLiteral("1 file would conflict."));

        MergePreview broken = preview(MergePreview::Failed);
        broken.error = QStringLiteral("unknown revision");
        MergeVerdict failed = mergeVerdict(broken, false, main);
        QCOMPARE(failed.kind, MergeVerdict::Bad);
        QCOMPARE(failed.headline, QStringLiteral("Could not check the merge."));
        QCOMPARE(failed.detail, QStringList({broken.error}));
        QVERIFY(!failed.canMerge);
    }

    void verdictWarnsAboutBlockedPathsAndACheckout()
    {
        MergePreview blocked = preview(MergePreview::Clean);
        blocked.blocked = QStringList({QStringLiteral("a.txt")});
        MergeVerdict one = mergeVerdict(blocked, false, QStringLiteral("main"));
        QVERIFY(one.warning.contains(QStringLiteral("Local changes to a.txt")));
        QVERIFY(!one.canMerge);
        QCOMPARE(one.buttonTip, QStringLiteral("Blocked by local changes — see above"));

        blocked.blocked = QStringList({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"),
                                       QStringLiteral("d"), QStringLiteral("e"), QStringLiteral("f")});
        const MergeVerdict many = mergeVerdict(blocked, false, QStringLiteral("main"));
        QVERIFY(many.warning.contains(QStringLiteral("6 files")));
        QVERIFY(many.warning.contains(QStringLiteral("a, b, c, d and 2 more")));

        // Merging into a branch that is not checked out says so, right under
        // the headline, and only when there is something to merge.
        const MergeVerdict elsewhere = mergeVerdict(preview(MergePreview::Clean), false, QStringLiteral("other"));
        QCOMPARE(elsewhere.detail.size(), 3);
        QCOMPARE(elsewhere.detail.at(1), QStringLiteral("main is checked out first"));
        const MergeVerdict same = mergeVerdict(preview(MergePreview::Same), false, QStringLiteral("other"));
        QCOMPARE(same.detail.size(), 1);
    }

    // --- OmarchyTheme -------------------------------------------------------
    // Last: it points OmarchyTheme::instance() at a theme of its own.

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

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setOrganizationName("omagit-tests");
    app.setApplicationName("ui-test");
    g_theme = std::make_unique<OmarchyTheme>();
    g_theme->apply(app);
    UiTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "ui_test.moc"
