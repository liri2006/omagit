// Build in tests: qmake6 mergedialog.pro -o Makefile.mergedialog && make -f Makefile.mergedialog
// Run: QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME= QT_STYLE_OVERRIDE=Fusion ../build/tests/mergedialog_test
#include "../src/MergeDialog.h"
#include "../src/OmarchyTheme.h"

#include <QApplication>
#include <QLabel>
#include <QFrame>
#include <QLayout>
#include <QProcess>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

namespace {

bool git(const QString &directory, const QStringList &args)
{
    QProcess process;
    process.setWorkingDirectory(directory);
    process.start("git", args);
    return process.waitForFinished() && process.exitCode() == 0;
}

// A repository with main and a feature branch one commit ahead of it, main
// checked out.
bool makeFixture(const QString &directory)
{
    const QStringList commit{"-c", "user.name=Test", "-c", "user.email=test@example.com",
                             "-c", "commit.gpgsign=false", "commit", "--allow-empty", "-m"};
    return git(directory, {"init", "-q", "-b", "main"}) && git(directory, commit + QStringList{"base"})
        && git(directory, {"checkout", "-b", "feature"}) && git(directory, commit + QStringList{"feature"})
        && git(directory, {"checkout", "main"});
}

// Whether a visible label of the dialog says `text`.
bool shows(const QDialog &dialog, const QString &text)
{
    for (auto *label : dialog.findChildren<QLabel *>())
        if (label->isVisible() && label->text().contains(text))
            return true;
    return false;
}

// The window is exactly as tall as its content, and the buttons sit inside
// it, 16 px above its bottom edge and under the verdict.
void checkFitsContent(QDialog &dialog, const QWidget *verdict)
{
    dialog.layout()->activate();
    QCOMPARE(dialog.height(), qMax(dialog.layout()->totalHeightForWidth(dialog.width()),
                                   dialog.layout()->totalMinimumSize().height()));
    for (auto *button : dialog.findChildren<QPushButton *>()) {
        if (!button->isVisible())
            continue;
        QVERIFY(dialog.rect().contains(button->geometry()));
        QCOMPARE(dialog.height() - button->geometry().bottom() - 1, 16);
        QVERIFY(button->y() > verdict->geometry().bottom());
    }
}

} // namespace

class MergeDialogTest : public QObject
{
    Q_OBJECT
private slots:
    void swapAndPreview()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(makeFixture(directory.path()));
        auto git = [&](const QStringList &args) { return ::git(directory.path(), args); };

        GitRepo repo(directory.path());
        MergeDialog dialog(&repo);
        dialog.setAttribute(Qt::WA_DeleteOnClose, false);
        dialog.setBranches("feature", "main");
        dialog.show();
        QApplication::processEvents();
        auto *swapButton = dialog.findChild<QToolButton *>("swapButton");
        auto *verdict = dialog.findChild<QFrame *>("mergeVerdict");
        QVERIFY(swapButton);
        QVERIFY(verdict);
        const int headerTop = swapButton->y();
        const int verdictTop = verdict->y();
        const int checkingHeight = dialog.height();
        auto checkLayout = [&] {
            checkFitsContent(dialog, verdict);
            QVERIFY(!QTest::currentTestFailed());
            QCOMPARE(swapButton->y(), headerTop);
            QCOMPARE(verdict->y(), verdictTop);
        };
        checkLayout();
        auto contains = [&](const QString &text) { return shows(dialog, text); };
        QTRY_VERIFY(contains("no conflicts"));
        // The checking message reserved the room a two-line verdict takes: no jump.
        QCOMPARE(dialog.height(), checkingHeight);
        checkLayout();
        const int originalHeight = dialog.height();
        QTest::mouseClick(swapButton, Qt::LeftButton);
        QCOMPARE(dialog.source(), QString("main"));
        QCOMPARE(dialog.destination(), QString("feature"));
        // While the other direction is checked, the card holds its height.
        QVERIFY(contains("Checking"));
        QCOMPARE(dialog.height(), originalHeight);
        checkLayout();
        QTRY_VERIFY(contains("Nothing to merge."));
        // Two lines of detail again (feature is checked out first): the same height.
        QCOMPARE(dialog.height(), originalHeight);
        checkLayout();

        QTest::keyClick(&dialog, Qt::Key_S, Qt::ControlModifier);
        QCOMPARE(dialog.source(), QString("feature"));
        QCOMPARE(dialog.destination(), QString("main"));
        QTRY_VERIFY(contains("no conflicts"));
        for (int i = 0; i < 5; ++i)
            QTest::mouseClick(swapButton, Qt::LeftButton);
        QCOMPARE(dialog.source(), QString("main"));
        QCOMPARE(dialog.destination(), QString("feature"));
        QTRY_VERIFY(contains("Nothing to merge."));
        QVERIFY(!contains("The same branch is on both sides."));
        checkLayout();

        const QString longBranch = QString("topic/") + QString("long-description/").repeated(18) + "work";
        QVERIFY(git({"branch", longBranch, "feature"}));
        dialog.setBranches(longBranch, "main");
        QTRY_VERIFY(contains("no conflicts"));
        // The long name wraps the headline: the window grows to show all of it,
        // and shrinks back for a short verdict.
        QVERIFY(dialog.height() > originalHeight);
        checkLayout();
        dialog.setBranches("feature", "main");
        QCOMPARE(dialog.height(), dialog.minimumHeight()); // held while checking
        QTRY_VERIFY(contains("no conflicts"));
        QCOMPARE(dialog.height(), originalHeight);
        checkLayout();
    }

    // Over a narrow window the dialog is the window's width less 24 px, and
    // under 520 px the pickers stack: MERGE, the source across the width, the
    // swap button with INTO beside it, then the destination across the width.
    void narrowWindowsStackThePickers()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(makeFixture(directory.path()));
        GitRepo repo(directory.path());

        QWidget host;
        host.resize(470, 612);
        host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        MergeDialog dialog(&repo, &host);
        dialog.setAttribute(Qt::WA_DeleteOnClose, false);
        dialog.setBranches("feature", "main");
        dialog.show();
        QApplication::processEvents();

        auto *swapButton = dialog.findChild<QToolButton *>("swapButton");
        auto *verdict = dialog.findChild<QFrame *>("mergeVerdict");
        QToolButton *source = nullptr, *destination = nullptr;
        for (auto *picker : dialog.findChildren<QToolButton *>("branchPicker"))
            (picker->accessibleName() == QLatin1String("feature") ? source : destination) = picker;
        QLabel *mergeCaption = nullptr, *intoCaption = nullptr;
        for (auto *label : dialog.findChildren<QLabel *>("sectionLabel")) {
            if (label->text() == QLatin1String("MERGE"))
                mergeCaption = label;
            else if (label->text() == QLatin1String("INTO"))
                intoCaption = label;
        }
        QVERIFY(swapButton && verdict && source && destination && mergeCaption && intoCaption);

        QCOMPARE(dialog.width(), 446);
        QVERIFY(source->y() < swapButton->y());
        QVERIFY(swapButton->y() < destination->y());
        QCOMPARE(source->width(), dialog.width() - 40);
        QCOMPARE(destination->width(), dialog.width() - 40);
        QVERIFY(intoCaption->x() > swapButton->geometry().right());
        QVERIFY(mergeCaption->geometry().bottom() < source->y());
        QTRY_VERIFY(shows(dialog, "no conflicts"));
        checkFitsContent(dialog, verdict);
        QVERIFY(!QTest::currentTestFailed());

        // A wider window: side by side again, at the window's width less 24.
        dialog.hide();
        host.resize(627, 612);
        QApplication::processEvents();
        dialog.show();
        QApplication::processEvents();
        QCOMPARE(dialog.width(), 603);
        QCOMPARE(swapButton->y(), source->y());
        QCOMPARE(swapButton->y(), destination->y());
        QVERIFY(source->x() < swapButton->x());
        QVERIFY(swapButton->x() < destination->x());
        QTRY_VERIFY(shows(dialog, "no conflicts"));
        checkFitsContent(dialog, verdict);
        QVERIFY(!QTest::currentTestFailed());

        // With no window under it, the design's 640.
        MergeDialog bare(&repo);
        bare.setAttribute(Qt::WA_DeleteOnClose, false);
        bare.show();
        QApplication::processEvents();
        QCOMPARE(bare.width(), 640);
    }
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setOrganizationName("omagit-tests");
    app.setApplicationName("mergedialog-test");
    OmarchyTheme theme;
    theme.apply(app);
    MergeDialogTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "mergedialog_test.moc"
