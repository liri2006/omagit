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

class MergeDialogTest : public QObject
{
    Q_OBJECT
private slots:
    void swapAndPreview()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto git = [&](const QStringList &args) {
            QProcess process;
            process.setWorkingDirectory(directory.path());
            process.start("git", args);
            return process.waitForFinished() && process.exitCode() == 0;
        };
        QVERIFY(git({"init", "-q", "-b", "main"}));
        QVERIFY(git({"-c", "user.name=Test", "-c", "user.email=test@example.com",
                     "-c", "commit.gpgsign=false", "commit", "--allow-empty", "-m", "base"}));
        QVERIFY(git({"checkout", "-b", "feature"}));
        QVERIFY(git({"-c", "user.name=Test", "-c", "user.email=test@example.com",
                     "-c", "commit.gpgsign=false", "commit", "--allow-empty", "-m", "feature"}));
        QVERIFY(git({"checkout", "main"}));

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
            dialog.layout()->activate();
            // The window is exactly as tall as its content.
            QCOMPARE(dialog.height(), qMax(dialog.layout()->totalHeightForWidth(dialog.width()),
                                           dialog.layout()->totalMinimumSize().height()));
            QCOMPARE(swapButton->y(), headerTop);
            QCOMPARE(verdict->y(), verdictTop);
            for (auto *button : dialog.findChildren<QPushButton *>()) {
                if (!button->isVisible())
                    continue;
                QVERIFY(dialog.rect().contains(button->geometry()));
                QCOMPARE(dialog.height() - button->geometry().bottom() - 1, 16);
                QVERIFY(button->y() > verdict->geometry().bottom());
            }
        };
        checkLayout();
        auto contains = [&](const QString &text) {
            for (auto *label : dialog.findChildren<QLabel *>())
                if (label->isVisible() && label->text().contains(text))
                    return true;
            return false;
        };
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
