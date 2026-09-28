// The keyboard: the keybindings panel's filter and list, and Return against
// Ctrl+Return in the docked message box.
#include "UiTestCase.h"
#include "fixtures.h"

#include "../../src/KeybindingsPanel.h"

#include <QLineEdit>
#include <QListView>

class KeysTest : public UiTestCase
{
    Q_OBJECT
private slots:
    // Plain Return is a new line in either box; Ctrl+Return in the Docked
    // layout still commits, from the page's own message box.
    void returnIsANewLineAndCtrlReturnCommitsInTheDockedLayout()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(activate(f.window.get()));
        const QString head = f.head();

        QVERIFY(f.openCard());
        MessageEdit *card = f.popover()->editor();
        QTest::keyClicks(card, QStringLiteral("Subject"));
        QTest::keyClick(card, Qt::Key_Return);
        QTest::keyClicks(card, QStringLiteral("Body"));
        QCOMPARE(card->toPlainText(), QStringLiteral("Subject\nBody"));
        QVERIFY(f.popover()->isVisible());
        QCOMPARE(f.head(), head);

        f.window->setPaneLayout(PaneLayout::Docked, false);
        settle();
        MessageEdit *page = f.pageEditor();
        page->setFocus();
        QTRY_COMPARE(f.window->focusWidget(), static_cast<QWidget *>(page));
        QTest::keyClick(page, Qt::Key_End, Qt::ControlModifier);
        QTest::keyClick(page, Qt::Key_Return);
        QTest::keyClicks(page, QStringLiteral("More"));
        QCOMPARE(page->toPlainText(), QStringLiteral("Subject\nBody\nMore"));
        QCOMPARE(f.head(), head);
        QTest::keyClick(page, Qt::Key_Return, Qt::ControlModifier);
        settle();
        QVERIFY(f.head() != head);
        QCOMPARE(f.repo->headMessage().trimmed(), QStringLiteral("Subject\nBody\nMore"));
        QVERIFY(f.page()->messageDocument()->isEmpty());
    }

    // One row for Ctrl+Return, saying where it works; the keypad's Enter is
    // the same binding, unlisted.
    void theKeybindingsListCtrlReturnOnce()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(QMetaObject::invokeMethod(f.window.get(), "showKeybindings"));
        auto *panel = f.window->findChild<KeybindingsPanel *>();
        QVERIFY(panel);
        auto *list = panel->findChild<QListView *>(QStringLiteral("keybindingsList"));
        QVERIFY(list);
        QAbstractItemModel *model = list->model();
        int rows = 0, commits = 0;
        for (int row = 0; row < model->rowCount(); ++row) {
            const QModelIndex index = model->index(row, 0);
            const QString keys = index.data(Qt::UserRole).toString();
            QVERIFY(keys != QStringLiteral("CTRL + ENTER"));
            if (index.data(Qt::DisplayRole).toString() == QStringLiteral("Commit checked files"))
                ++commits;
            if (keys != QStringLiteral("CTRL + RETURN"))
                continue;
            ++rows;
            QCOMPARE(index.data(Qt::UserRole + 2).toString(), QStringLiteral("Commit view, Mini rail"));
        }
        QCOMPARE(rows, 1);
        QCOMPARE(commits, 1);
        panel->close();
    }

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
};

UI_TEST(KeysTest);

#include "keys_test.moc"
