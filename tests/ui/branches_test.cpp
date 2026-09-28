// Branches: the top bar's branch menu, the New branch card (the name, the
// start point, creating the branch) and the merge verdict's wording.
#include "UiTestCase.h"
#include "fixtures.h"

#include "../../src/BranchMenu.h"
#include "../../src/BranchPicker.h"
#include "../../src/HistoryView.h"
#include "../../src/KeybindingsPanel.h"
#include "../../src/MergeDialog.h"
#include "../../src/UiHelpers.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QListView>
#include <QSignalSpy>
#include <QStyleOptionButton>
#include <QTimer>
#include <QWidgetAction>

namespace {

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

// The rows of a menu that are on screen, the search prompt aside: "-" for a
// separator, a section's caption as it reads, an entry's text.
QStringList shownRows(const QMenu *menu)
{
    QStringList out;
    for (QAction *a : menu->actions()) {
        if (!a->isVisible())
            continue;
        if (a->isSeparator()) {
            out << QStringLiteral("-");
        } else if (auto *wa = qobject_cast<QWidgetAction *>(a)) {
            if (auto *label = qobject_cast<QLabel *>(wa->defaultWidget()))
                out << label->text();
        } else {
            out << a->text();
        }
    }
    return out;
}

// The branch menu's New branch row: the entry wearing the branch-plus glyph.
QAction *newBranchRow(const QMenu *menu)
{
    for (QAction *a : menu->actions())
        if (a->property("branchGlyph").toUInt() == ui::kBranchPlus)
            return a;
    return nullptr;
}

// Clicks `picker` and, once the branch menu it opens is up, hands the menu to
// `inMenu`; whatever it leaves open is closed after it, so the click returns.
void inPickerMenu(QAbstractButton *picker, const std::function<void(BranchMenu *)> &inMenu)
{
    QTimer::singleShot(0, picker, [inMenu] {
        QElapsedTimer clock;
        clock.start();
        BranchMenu *menu = nullptr;
        while (!(menu = qobject_cast<BranchMenu *>(QApplication::activePopupWidget())) && clock.elapsed() < 5000)
            QTest::qWait(10);
        if (menu)
            inMenu(menu);
        if (QWidget *popup = QApplication::activePopupWidget())
            popup->close();
    });
    picker->click();
}

// Picks the entry `name` of an open branch menu the way Return does: the menu
// closes, then the entry fires.
void pickIn(BranchMenu *menu, const QString &name)
{
    for (QAction *a : menu->actions()) {
        if (a->text() == name) {
            menu->close();
            a->trigger();
            return;
        }
    }
    menu->close();
}

// Whether `a` is `b` to a pixel on every edge.
bool withinAPixel(const QRect &a, const QRect &b)
{
    return qAbs(a.x() - b.x()) <= 1 && qAbs(a.y() - b.y()) <= 1 && qAbs(a.width() - b.width()) <= 1
        && qAbs(a.height() - b.height()) <= 1;
}

QString rectText(const QRect &r)
{
    return QStringLiteral("%1,%2 %3x%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
}

// The New branch card open the way Ctrl+N and the menus open it.
NewBranchCard *openNewBranchCard(const WindowFixture &f, const QString &start = {}, const QString &name = {})
{
    QMetaObject::invokeMethod(f.window.get(), "showNewBranchCard", Q_ARG(QString, start), Q_ARG(QString, name));
    settle();
    NewBranchCard *card = f.newBranchCard();
    return card && card->isVisible() ? card : nullptr;
}

// The footer's message of the moment.
QString footerStatus(const WindowFixture &f)
{
    auto *label = f.window->findChild<ui::ElidedLabel *>(QStringLiteral("footerStatus"));
    return label ? label->fullText() : QString();
}

} // namespace

class BranchesTest : public UiTestCase
{
    Q_OBJECT
private slots:
    // The branch menu: the design's 300 px where the window has the room, the
    // room from the button to the window's right margin where it has not; a
    // branch glyph on every local entry and a cloud on every remote one.
    void theBranchMenuWearsTheDesignsRows()
    {
        QWidget window;
        window.resize(ui::space(1000), 400);
        auto *anchor = new QToolButton(&window);
        anchor->resize(ui::space(80), ui::space(28));
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        BranchList branches;
        branches.local = {QStringLiteral("main"), QStringLiteral("feature/askpass")};
        branches.remote = {QStringLiteral("origin/main")};
        branches.current = QStringLiteral("main");
        const auto open = [&](BranchMenu &menu) {
            QTimer::singleShot(0, &menu, [&menu] {
                QTRY_VERIFY(menu.isVisible());
                menu.close();
            });
            menu.popupAt(anchor, false);
        };
        {
            BranchMenu menu;
            menu.setBranches(branches, branches.current, true, {});
            open(menu);
            QCOMPARE(menu.minimumWidth(), ui::space(300));
            QList<uint> glyphs;
            for (QAction *a : menu.actions())
                if (a->property("branchGlyph").isValid())
                    glyphs << a->property("branchGlyph").toUInt();
            QCOMPARE(glyphs, QList<uint>({ui::kBranch, ui::kBranch, ui::kCloudOutline}));
        }
        window.resize(ui::space(250), 400);
        settle();
        {
            BranchMenu menu;
            menu.setBranches(branches, branches.current, true, {});
            open(menu);
            // The button is at the window's left edge.
            QCOMPARE(menu.minimumWidth(), qMax(anchor->width(), window.width() - ui::windowMargin(&window)));
            QCOMPARE(menu.maximumWidth(), qMax(anchor->width(), window.width() - ui::windowMargin(&window)));
        }
    }

    // The top bar's branch menu ends with New branch… (Ctrl+N at the right,
    // behind a separator). The search doubles as the name: a text no branch
    // has leaves the row alone, current, with the name in it, and Return
    // takes it to the card; matches keep the first of them current and the
    // row under them; a local branch's own name hides it. Ctrl+N in the field
    // asks for the card at any time. Tags get a section of their own.
    void theBranchMenuOffersANewBranch()
    {
        BranchList branches;
        branches.local = {QStringLiteral("feature/askpass"), QStringLiteral("feature/tiling"), QStringLiteral("main")};
        branches.remote = {QStringLiteral("origin/feature/askpass"), QStringLiteral("origin/main")};
        branches.current = QStringLiteral("main");
        BranchMenu menu;
        menu.setNewBranchRow(true);
        menu.setBranches(branches, branches.current, true, {});
        QSignalSpy asked(&menu, &BranchMenu::newBranchRequested);
        auto *field = menu.findChild<QLineEdit *>(QStringLiteral("promptField"));
        QAction *row = newBranchRow(&menu);
        QVERIFY(field && row);

        // Last, after a separator; the keys after the tab, which the menu
        // paints at the right itself.
        QCOMPARE(menu.actions().last(), row);
        QCOMPARE(row->text(), QStringLiteral("New branch…\tCtrl+N"));
        QCOMPARE(row->text().section(QLatin1Char('\t'), 0, 0), QStringLiteral("New branch…"));
        QCOMPARE(row->text().section(QLatin1Char('\t'), 1), QStringLiteral("Ctrl+N"));
        QCOMPARE(shownRows(&menu),
                 QStringList({QStringLiteral("LOCAL"), QStringLiteral("feature/askpass"), QStringLiteral("feature/tiling"),
                              QStringLiteral("main"), QStringLiteral("-"), QStringLiteral("REMOTE"),
                              QStringLiteral("origin/feature/askpass"), QStringLiteral("origin/main"), QStringLiteral("-"),
                              QStringLiteral("New branch…\tCtrl+N")}));
        const QStringList initial = shownRows(&menu);
        QVERIFY(!menu.activeAction());

        // A name no branch has: the row alone, current, carrying the name.
        field->setText(QStringLiteral("feature/tile-rules"));
        QCOMPARE(shownRows(&menu), QStringList({QStringLiteral("New branch “feature/tile-rules”…")}));
        QCOMPARE(menu.activeAction(), row);
        QTest::keyClick(field, Qt::Key_Return);
        QCOMPARE(asked.size(), 1);
        QCOMPARE(asked.takeFirst().value(0).toString(), QStringLiteral("feature/tile-rules"));

        // Matches: the first is current, the row under them, Up from the
        // first wraps round to it.
        field->setText(QStringLiteral("fea"));
        QCOMPARE(shownRows(&menu),
                 QStringList({QStringLiteral("LOCAL"), QStringLiteral("feature/askpass"), QStringLiteral("feature/tiling"),
                              QStringLiteral("-"), QStringLiteral("REMOTE"), QStringLiteral("origin/feature/askpass"),
                              QStringLiteral("-"), QStringLiteral("New branch “fea”…")}));
        QCOMPARE(menu.activeAction()->text(), QStringLiteral("feature/askpass"));
        QTest::keyClick(field, Qt::Key_Up);
        QCOMPARE(menu.activeAction(), row);
        QTest::keyClick(field, Qt::Key_Down);
        QCOMPARE(menu.activeAction()->text(), QStringLiteral("feature/askpass"));
        QTest::keyClick(field, Qt::Key_Return); // the match, not the row
        QCOMPARE(asked.size(), 0);

        // A local branch's own name: no row (a remote one's leaves it).
        field->setText(QStringLiteral("main"));
        QVERIFY(!row->isVisible());
        QCOMPARE(shownRows(&menu), QStringList({QStringLiteral("LOCAL"), QStringLiteral("main"), QStringLiteral("-"),
                                                QStringLiteral("REMOTE"), QStringLiteral("origin/main")}));
        field->setText(QStringLiteral("origin/main"));
        QVERIFY(row->isVisible());
        // Ctrl+N asks whatever the row says, with the search's text trimmed.
        field->setText(QStringLiteral("  main "));
        QTest::keyClick(field, Qt::Key_N, Qt::ControlModifier);
        QCOMPARE(asked.size(), 1);
        QCOMPARE(asked.takeFirst().value(0).toString(), QStringLiteral("main"));
        field->clear();
        // A cleared search brings every entry back.
        QCOMPARE(shownRows(&menu), initial);
        QTest::keyClick(field, Qt::Key_N, Qt::ControlModifier);
        QCOMPARE(asked.size(), 1);
        QCOMPARE(asked.takeFirst().value(0).toString(), QString());
        QCOMPARE(row->text(), QStringLiteral("New branch…\tCtrl+N"));

        // Without the row (the merge view's and the From picker's menus): no
        // row, and Ctrl+N is no key of theirs; tags in a section of their own.
        BranchMenu plain;
        plain.setBranches(branches, branches.current, true, {}, QString(),
                          {QStringLiteral("v0.4"), QStringLiteral("v0.3")});
        QSignalSpy plainAsked(&plain, &BranchMenu::newBranchRequested);
        QVERIFY(!newBranchRow(&plain));
        const QStringList rows = shownRows(&plain);
        QCOMPARE(rows.mid(rows.indexOf(QStringLiteral("TAGS")) - 1),
                 QStringList({QStringLiteral("-"), QStringLiteral("TAGS"), QStringLiteral("v0.4"), QStringLiteral("v0.3")}));
        QList<uint> glyphs;
        for (QAction *a : plain.actions())
            if (a->text().startsWith(QLatin1Char('v')))
                glyphs << a->property("branchGlyph").toUInt();
        QCOMPARE(glyphs, QList<uint>({ui::kTagOutline, ui::kTagOutline}));
        auto *plainField = plain.findChild<QLineEdit *>(QStringLiteral("promptField"));
        QCOMPARE(plainField->placeholderText(), QStringLiteral("Search branches and tags…"));
        QSignalSpy picked(&plain, &BranchMenu::picked);
        plainField->setText(QStringLiteral("0.3"));
        QCOMPARE(shownRows(&plain), QStringList({QStringLiteral("TAGS"), QStringLiteral("v0.3")}));
        QTest::keyClick(plainField, Qt::Key_N, Qt::ControlModifier);
        QCOMPARE(plainAsked.size(), 0);
        QTest::keyClick(plainField, Qt::Key_Return);
        QCOMPARE(picked.size(), 1);
        QCOMPARE(picked.takeFirst().value(0).toString(), QStringLiteral("v0.3"));
        plainField->setText(QStringLiteral("nothing"));
        QCOMPARE(shownRows(&plain), QStringList({QStringLiteral("No matching branch")}));
    }

    // Neither side of the merge view offers a new branch: the row is the top
    // bar's menu's alone, and the pickers look as they did.
    void theMergePickersHaveNoNewBranchRow()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(git(f.repo->root(), {"branch", "feature"}));
        MergeDialog dialog(f.repo.get(), f.window.get());
        dialog.setAttribute(Qt::WA_DeleteOnClose, false);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        const QList<BranchPicker *> pickers = dialog.findChildren<BranchPicker *>();
        QCOMPARE(pickers.size(), 2);
        for (BranchPicker *picker : pickers) {
            QCOMPARE(picker->objectName(), QStringLiteral("branchPicker"));
            QCOMPARE(picker->height(), ui::space(36));
            QCOMPARE(picker->kind(), BranchPicker::Kind::Branch);
            bool seen = false, row = true;
            inPickerMenu(picker, [&seen, &row](BranchMenu *menu) {
                seen = true;
                row = newBranchRow(menu) != nullptr;
            });
            QVERIFY(seen);
            QVERIFY(!row);
        }
    }

    // The card at the design's three frames (out/manifest.json: New branch ·
    // 3 Card, · Eighth · Card, · Extra narrow · Card; screens.js
    // newBranchCard()) at a 12 px text: 360 wide at the branch chip, moved
    // left to keep the window's margin, 4 under the bar; the field, the
    // picker, Switch to it and Create branch where the frames have them.
    void theNewBranchCardFollowsTheDesign_data()
    {
        QTest::addColumn<int>("width");
        QTest::addColumn<QRect>("card");
        QTest::addColumn<QRect>("name");
        QTest::addColumn<QRect>("picker");
        QTest::addColumn<QRect>("create");
        QTest::addColumn<QPoint>("check");   // the Switch to it box, 16 px square
        QTest::addColumn<int>("designRows"); // the top bar's rows in the frame
        QTest::newRow("945") << 945 << QRect(111, 48, 360, 240) << QRect(123, 80, 336, 28) << QRect(123, 144, 336, 28)
                             << QRect(292, 248, 167, 28) << QPoint(123, 254) << 1;
        QTest::newRow("470") << 470 << QRect(44, 48, 360, 240) << QRect(56, 80, 336, 28) << QRect(56, 144, 336, 28)
                             << QRect(225, 248, 167, 28) << QPoint(56, 254) << 1;
        QTest::newRow("340") << 340 << QRect(8, 40, 324, 240) << QRect(20, 72, 300, 28) << QRect(20, 136, 300, 28)
                             << QRect(153, 240, 167, 28) << QPoint(20, 246) << 2;
    }

    void theNewBranchCardFollowsTheDesign()
    {
        QFETCH(int, width);
        QFETCH(QRect, card);
        QFETCH(QRect, name);
        QFETCH(QRect, picker);
        QFETCH(QRect, create);
        QFETCH(QPoint, check);
        QFETCH(int, designRows);
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

        WindowFixture f = mainWindow(0, false, [width](MainWindow *w) { w->resize(width, 612); }, {},
                                     QStringLiteral("omagit"));
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        settle();
        QWidget *host = f.host();
        if (host->width() != width) {
            // A top bar that cannot fold to the width holds the window wider.
            const QString why = QStringLiteral("the window cannot be %1 wide: its top bar asks for %2")
                                    .arg(width)
                                    .arg(f.window->minimumSizeHint().width());
            f.window.reset();
            QSKIP(qPrintable(why));
        }
        NewBranchCard *shown = openNewBranchCard(f, {}, QStringLiteral("feature/tile-rules"));
        QVERIFY(shown);
        const QRect cardRect = shown->geometry();
        // 4 under the bar, whatever its rows. The frame at 340 has the
        // extra-narrow two-row bar, under whose first row the card hangs;
        // under a one-row bar it is the bar's 8 lower. The frames' bar folds
        // the repository to a bare folder, so where the app's bar, wearing
        // the name, has two rows and the frame one, the card is 8 higher.
        QCOMPARE(cardRect.y(), host->mapFromGlobal(QPoint(0, ui::popupTop(f.bar()))).y());
        const int rows = f.bar()->height() > ui::space(44) ? 2 : 1;
        const int dy = cardRect.y() - card.y();
        QCOMPARE(dy, (designRows - rows) * ui::space(ui::kBar));
        // At the chip, or a margin inside the window's edge. The chip
        // stands where the repository's name puts it, which the frames'
        // stacked bar folds to a bare folder: the card's content is laid
        // out from wherever the card is.
        QCOMPARE(cardRect.x(), qMin(rectIn(f.bar()->branchButton(), host).x(),
                                    width - ui::windowMargin(f.window.get()) - cardRect.width()));
        const int dx = cardRect.x() - card.x();
        QCheckBox *box = shown->switchBox();
        QStyleOptionButton option;
        option.initFrom(box);
        const QRect indicator = box->style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, box);
        const QList<QPair<QRect, QRect>> pairs{
            {cardRect, card},
            {rectIn(shown->nameField(), host), name},
            {rectIn(shown->startPicker(), host), picker},
            {rectIn(shown->createButton(), host), create},
            {QRect(box->mapTo(host, indicator.topLeft()), indicator.size()), QRect(check, QSize(16, 16))},
        };
        for (const auto &pair : pairs) {
            const QRect want = pair.second.translated(dx, dy);
            QVERIFY2(withinAPixel(pair.first, want),
                     qPrintable(QStringLiteral("%1, the design %2").arg(rectText(pair.first), rectText(want))));
        }
        QCOMPARE(shown->startPicker()->height(), ui::space(ui::box::control));
        f.window.reset(); // before the theme it was built with
    }

    // The name as it is typed: a space goes in as a dash where it was typed;
    // Create is off for nothing, for a name git refuses (the red line says
    // so) and for a local branch's (the red line offers to switch to it).
    void theNewBranchCardChecksTheName()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(git(f.repo->root(), {"branch", "feature/askpass"}));
        NewBranchCard *card = openNewBranchCard(f);
        QVERIFY(card);
        QLineEdit *field = card->nameField();
        QCOMPARE(field->objectName(), QStringLiteral("newBranchName"));
        QCOMPARE(QApplication::focusWidget(), field);
        QVERIFY(field->text().isEmpty());
        QVERIFY(!card->createButton()->isEnabled());
        QVERIFY(card->errorText().isEmpty());
        QCOMPARE(card->createButton()->text(), ui::icon(ui::kBranchPlus, QStringLiteral("+ ")) + QStringLiteral("Create branch  ⏎"));
        QVERIFY(card->createButton()->isDefault());

        QTest::keyClicks(field, QStringLiteral("my feature"));
        QCOMPARE(field->text(), QStringLiteral("my-feature"));
        QVERIFY(card->createButton()->isEnabled());
        // Where the caret is, not at the end.
        QTest::keyClick(field, Qt::Key_Left);
        QTest::keyClick(field, Qt::Key_Left);
        QTest::keyClick(field, Qt::Key_Space);
        QCOMPARE(field->text(), QStringLiteral("my-featu-re"));
        QCOMPARE(field->cursorPosition(), 9);

        const int heightBefore = card->height();
        for (const QString &bad : {QStringLiteral("a..b"), QStringLiteral("HEAD"), QStringLiteral("-x"), QStringLiteral("x.lock")}) {
            field->setText(bad);
            QVERIFY2(!card->createButton()->isEnabled(), qPrintable(bad));
            QCOMPARE(card->errorText(), QStringLiteral("Not a valid branch name"));
            QVERIFY(!card->switchToExistingButton()->isVisible());
        }
        // The red line is a 24 px line 4 under the field: the card grows by it.
        QCOMPARE(card->height(), heightBefore + ui::space(ui::gap::caption + ui::box::row));

        field->setText(QStringLiteral("feature/askpass"));
        QVERIFY(!card->createButton()->isEnabled());
        QCOMPARE(card->errorText(), QStringLiteral("Already a branch"));
        QAbstractButton *existing = card->switchToExistingButton();
        QVERIFY(existing->isVisible());
        QCOMPARE(existing->height(), ui::space(ui::box::row));
        QCOMPARE(rectIn(existing, card).right(), card->width() - ui::space(ui::pad::popover) - 1);
        field->clear();
        QVERIFY(card->errorText().isEmpty());
        QVERIFY(!card->createButton()->isEnabled());
        QCOMPARE(card->height(), heightBefore);

        // The way out of a taken name: the branch it names, checked out.
        field->setText(QStringLiteral("feature/askpass"));
        existing->click();
        settle();
        QVERIFY(!card->isVisible());
        QCOMPARE(f.repo->branches().current, QStringLiteral("feature/askpass"));
        QCOMPARE(f.bar()->branchButton()->accessibleName(), QStringLiteral("feature/askpass"));
    }

    // Return makes the branch and switches to it, the footer says so, and the
    // top bar shows it — without the badges of the branch it left, having
    // no upstream. Switch to it off, the branch is made and the checkout
    // stays. Escape closes the card and gives the keyboard back.
    void theNewBranchCardCreatesTheBranch()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(activate(f.window.get()));
        const QString root = f.repo->root();
        // main is a commit behind an upstream of its own: a Pull badge.
        const QString ahead = QString::fromUtf8(f.repo->run({"commit-tree", "HEAD^{tree}", "-p", "HEAD", "-m", "theirs"})).trimmed();
        QVERIFY(!ahead.isEmpty());
        QVERIFY(git(root, {"remote", "add", "origin", QDir(root).filePath(QStringLiteral("no-such-remote.git"))}));
        QVERIFY(git(root, {"update-ref", "refs/remotes/origin/main", ahead}));
        QVERIFY(git(root, {"config", "branch.main.remote", "origin"}));
        QVERIFY(git(root, {"config", "branch.main.merge", "refs/heads/main"}));
        f.window->refresh();
        QTRY_COMPARE(f.bar()->pullButton()->count(), 1);

        // Escape: closed, the keyboard back on the list it came from.
        QAbstractItemView *list = f.page()->activeListView();
        list->setFocus();
        QTRY_COMPARE(QApplication::focusWidget(), list);
        QTest::keyClick(f.window.get(), Qt::Key_N, Qt::ControlModifier);
        NewBranchCard *card = f.newBranchCard();
        QTRY_VERIFY(card->isVisible());
        QCOMPARE(QApplication::focusWidget(), card->nameField());
        QCOMPARE(card->fromNote(), QStringLiteral("the current branch"));
        QCOMPARE(card->startPicker()->branch(), QStringLiteral("main"));
        QCOMPARE(card->startSha(), f.repo->headCommit().shortHash);
        QCOMPARE(card->startSubject(), QStringLiteral("first"));
        QCOMPARE(card->carryNote(), QStringLiteral("Your 2 changed files come along."));
        QVERIFY(card->switchBox()->isChecked() && card->switchBox()->isEnabled());
        QTest::keyClick(card->nameField(), Qt::Key_Escape);
        QVERIFY(!card->isVisible());
        QCOMPARE(QApplication::focusWidget(), list);

        // Return: made at HEAD and switched to, the changes along.
        QTest::keyClick(f.window.get(), Qt::Key_N, Qt::ControlModifier);
        QTRY_VERIFY(card->isVisible());
        QTest::keyClicks(card->nameField(), QStringLiteral("feature/tile-rules"));
        QTest::keyClick(card->nameField(), Qt::Key_Return);
        QTRY_VERIFY(!card->isVisible());
        const QString sha = f.repo->headCommit().shortHash;
        QCOMPARE(f.repo->branches().current, QStringLiteral("feature/tile-rules"));
        QCOMPARE(f.repo->status().size(), 2);
        QCOMPARE(footerStatus(f), QStringLiteral("Created feature/tile-rules at %1 and switched to it").arg(sha));
        QCOMPARE(f.bar()->branchButton()->accessibleName(), QStringLiteral("feature/tile-rules"));
        QTRY_COMPARE(f.bar()->pullButton()->count(), 0);
        QCOMPARE(f.bar()->pushButton()->count(), 0);

        // Switch to it off: made, and the checkout stays where it was.
        QTest::keyClick(f.window.get(), Qt::Key_N, Qt::ControlModifier);
        QTRY_VERIFY(card->isVisible());
        QVERIFY(card->switchBox()->isChecked()); // on each time the card opens
        QTest::keyClicks(card->nameField(), QStringLiteral("side"));
        card->switchBox()->click();
        QTest::keyClick(card->nameField(), Qt::Key_Return);
        QTRY_VERIFY(!card->isVisible());
        QVERIFY(f.repo->branches().local.contains(QStringLiteral("side")));
        QCOMPARE(f.repo->branches().current, QStringLiteral("feature/tile-rules"));
        QCOMPARE(footerStatus(f), QStringLiteral("Created side at %1 — still on feature/tile-rules").arg(sha));
        QTest::keyClick(f.window.get(), Qt::Key_N, Qt::ControlModifier);
        QTRY_VERIFY(card->isVisible());
        QVERIFY(card->switchBox()->isChecked());
        card->dismiss();
    }

    // Where it starts: a branch whose files differ from the changed ones
    // (Switch to it goes off, and Create still makes the branch there); a
    // remote branch, picked in the From menu, names an empty field after
    // itself; a tag. The From menu has the branches, the remote branches
    // and the tags, with the start ticked.
    void theNewBranchCardStartsWhereItIsAsked()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        const QString root = f.repo->root();
        // "other" has another a.txt, which the working tree has changed.
        QTemporaryDir side;
        QVERIFY(side.isValid());
        const QString other = QDir(side.path()).filePath(QStringLiteral("other"));
        QVERIFY(git(root, {"worktree", "add", "-q", "-b", "other", other, "HEAD"}));
        QVERIFY(writeFixture(QDir(other).filePath(QStringLiteral("a.txt")), "a on other\n"));
        QVERIFY(git(other, {"commit", "-q", "-am", "other's a"}, 2));
        QVERIFY(git(root, {"remote", "add", "origin", QDir(root).filePath(QStringLiteral("no-such-remote.git"))}));
        QVERIFY(git(root, {"update-ref", "refs/remotes/origin/feature/remote", "HEAD"}));
        QVERIFY(git(root, {"tag", "v1"}));

        NewBranchCard *card = openNewBranchCard(f, QStringLiteral("other"), QStringLiteral("from-other"));
        QVERIFY(card);
        QString otherSha;
        QVERIFY(f.repo->describeCommit(QStringLiteral("other"), &otherSha, nullptr));
        QCOMPARE(card->start(), QStringLiteral("other"));
        QVERIFY(card->fromNote().isEmpty());
        QCOMPARE(card->startSha(), otherSha);
        QCOMPARE(card->startSubject(), QStringLiteral("other's a"));
        QVERIFY(card->blockedNote()->isVisible());
        QCOMPARE(card->carryNote(), QStringLiteral("1 changed file differs at %1").arg(otherSha));
        QCOMPARE(card->blockedLines(), QStringList({QStringLiteral("a.txt"), QStringLiteral("Commit them first to switch to it.")}));
        QVERIFY(!card->switchBox()->isChecked());
        QVERIFY(!card->switchBox()->isEnabled());
        QVERIFY(card->createButton()->isEnabled());
        QTest::keyClick(card->nameField(), Qt::Key_Return);
        QTRY_VERIFY(!card->isVisible());
        QCOMPARE(f.repo->run({"rev-parse", "from-other"}), f.repo->run({"rev-parse", "other"}));
        QCOMPARE(f.repo->branches().current, QStringLiteral("main"));
        QCOMPARE(footerStatus(f), QStringLiteral("Created from-other at %1 — still on main").arg(otherSha));

        // The From menu: every branch, the remote ones and the tags, the
        // start ticked; a remote branch fills the empty field with its name.
        card = openNewBranchCard(f);
        QVERIFY(card);
        QVERIFY(!card->blockedNote()->isVisible());
        QVERIFY(card->switchBox()->isEnabled() && card->switchBox()->isChecked());
        QStringList rows;
        QString ticked;
        inPickerMenu(card->startPicker(), [&rows, &ticked](BranchMenu *menu) {
            rows = shownRows(menu);
            for (QAction *a : menu->actions())
                if (a->isChecked())
                    ticked = a->text();
            QVERIFY(!newBranchRow(menu));
            pickIn(menu, QStringLiteral("origin/feature/remote"));
        });
        QVERIFY2(rows.contains(QStringLiteral("REMOTE")) && rows.contains(QStringLiteral("TAGS")) && rows.contains(QStringLiteral("v1")),
                 qPrintable(rows.join(QLatin1Char(','))));
        QCOMPARE(ticked, QStringLiteral("main"));
        QCOMPARE(card->start(), QStringLiteral("origin/feature/remote"));
        QCOMPARE(card->startPicker()->branch(), QStringLiteral("origin/feature/remote"));
        QCOMPARE(card->startPicker()->kind(), BranchPicker::Kind::Branch);
        QCOMPARE(card->nameField()->text(), QStringLiteral("feature/remote"));
        QVERIFY(card->createButton()->isEnabled());
        // A name typed already stays; a tag is where the branch starts.
        card->nameField()->setText(QStringLiteral("mine"));
        inPickerMenu(card->startPicker(), [](BranchMenu *menu) { pickIn(menu, QStringLiteral("origin/main")); });
        inPickerMenu(card->startPicker(), [](BranchMenu *menu) { pickIn(menu, QStringLiteral("v1")); });
        QCOMPARE(card->nameField()->text(), QStringLiteral("mine"));
        QCOMPARE(card->start(), QStringLiteral("v1"));
        QCOMPARE(card->startPicker()->kind(), BranchPicker::Kind::Tag);
        QCOMPARE(card->startSha(), f.repo->headCommit().shortHash);
        // Back from the remote branch that tracks: made tracking it.
        inPickerMenu(card->startPicker(), [](BranchMenu *menu) { pickIn(menu, QStringLiteral("origin/feature/remote")); });
        QTest::keyClick(card->nameField(), Qt::Key_Return);
        QTRY_VERIFY(!card->isVisible());
        QCOMPARE(f.repo->branches().current, QStringLiteral("mine"));
        QCOMPARE(f.repo->upstreamState().upstream, QStringLiteral("origin/feature/remote"));

        // Detached: HEAD's commit, named by its hash, nothing beside FROM.
        QVERIFY(git(root, {"switch", "-q", "--detach", "main"}));
        card = openNewBranchCard(f);
        QVERIFY(card);
        QCOMPARE(card->start(), QStringLiteral("HEAD"));
        QCOMPARE(card->startPicker()->kind(), BranchPicker::Kind::Commit);
        QCOMPARE(card->startPicker()->branch(), f.repo->headCommit().shortHash);
        QVERIFY(card->fromNote().isEmpty());
        QVERIFY(card->startSha().isEmpty());
        QCOMPARE(card->startSubject(), QStringLiteral("first"));
        card->dismiss();

        // No commits yet on the branch checked out: switching (which renames
        // the branch to be) is all there is, and there is nowhere else to
        // start from.
        QVERIFY(git(root, {"checkout", "-q", "--orphan", "fresh"}));
        card = openNewBranchCard(f, {}, QStringLiteral("renamed"));
        QVERIFY(card);
        QVERIFY(card->switchBox()->isChecked() && !card->switchBox()->isEnabled());
        QVERIFY(!card->startPicker()->isEnabled());
        QCOMPARE(card->startPicker()->branch(), QStringLiteral("fresh"));
        QCOMPARE(card->fromNote(), QStringLiteral("the current branch"));
        QVERIFY(!card->blockedNote()->isVisible());
        QTest::keyClick(card->nameField(), Qt::Key_Return);
        QTRY_VERIFY(!card->isVisible());
        QCOMPARE(f.repo->branch(), QStringLiteral("renamed"));
        QCOMPARE(footerStatus(f), QStringLiteral("Created renamed and switched to it"));
    }

    // Ctrl+N is the window's (the keybindings list it as New branch, after
    // Branches): the card from the current branch, or, in the history, from
    // the selected commit — as the commit's menu's New branch from here… has
    // it too.
    void ctrlNOpensTheNewBranchCard()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(activate(f.window.get()));
        QVERIFY(QMetaObject::invokeMethod(f.window.get(), "showKeybindings"));
        auto *panel = f.window->findChild<KeybindingsPanel *>();
        QVERIFY(panel);
        auto *list = panel->findChild<QListView *>(QStringLiteral("keybindingsList"));
        QVERIFY(list);
        QStringList keys, actions;
        for (int row = 0; row < list->model()->rowCount(); ++row) {
            const QModelIndex index = list->model()->index(row, 0);
            keys << index.data(Qt::UserRole).toString();
            actions << index.data(Qt::DisplayRole).toString();
        }
        const qsizetype at = keys.indexOf(QStringLiteral("CTRL + N"));
        QVERIFY(at > 0);
        QCOMPARE(actions.at(at), QStringLiteral("New branch"));
        QCOMPARE(keys.at(at - 1), QStringLiteral("CTRL + 3"));
        QCOMPARE(actions.at(at - 1), QStringLiteral("Branches"));
        QVERIFY(list->model()->index(int(at), 0).data(Qt::UserRole + 2).toString().isEmpty());
        panel->close();
        settle();

        QVERIFY(activate(f.window.get()));
        QTest::keyClick(f.window.get(), Qt::Key_N, Qt::ControlModifier);
        NewBranchCard *card = f.newBranchCard();
        QTRY_VERIFY(card->isVisible());
        QVERIFY(card->start().isEmpty());
        QCOMPARE(card->fromNote(), QStringLiteral("the current branch"));
        // Open, Ctrl+N only puts the keyboard back in the name.
        QTest::keyClicks(card->nameField(), QStringLiteral("kept"));
        card->startPicker()->setFocus();
        QTest::keyClick(f.window.get(), Qt::Key_N, Qt::ControlModifier);
        QCOMPARE(QApplication::focusWidget(), card->nameField());
        QCOMPARE(card->nameField()->text(), QStringLiteral("kept"));
        card->dismiss();

        // The history: the selected commit's.
        f.window->setMode(MainWindow::HistoryMode);
        settle();
        auto *history = f.window->findChild<HistoryView *>();
        bool ok = false;
        QTRY_VERIFY((history->currentCommit(&ok), ok));
        const Commit commit = history->currentCommit(&ok);
        QTest::keyClick(f.window.get(), Qt::Key_N, Qt::ControlModifier);
        QTRY_VERIFY(card->isVisible());
        QCOMPARE(card->start(), commit.hash);
        QCOMPARE(card->fromNote(), QStringLiteral("the commit picked in History"));
        QCOMPARE(card->startPicker()->branch(), commit.shortHash);
        QCOMPARE(card->startPicker()->kind(), BranchPicker::Kind::Commit);
        QVERIFY(card->startSha().isEmpty()); // the picker names it already
        QCOMPARE(card->startSubject(), QStringLiteral("first"));
        card->dismiss();

        // The commit's menu (frame 75): the copies, then New branch from
        // here… after a separator, every row wearing its glyph.
        std::unique_ptr<QMenu> menu(history->commitMenu(commit));
        const QString copy = ui::icon(ui::kContentCopy);
        QCOMPARE(menuTexts(menu->actions()),
                 QStringList({copy + QStringLiteral("Copy SHA"), copy + QStringLiteral("Copy short SHA"),
                              copy + QStringLiteral("Copy message"), QStringLiteral("-"),
                              ui::icon(ui::kBranchPlus) + QStringLiteral("New branch from here…\tCtrl+N")}));
        QSignalSpy asked(history, &HistoryView::newBranchRequested);
        menu->actions().last()->trigger();
        QCOMPARE(asked.size(), 1);
        QCOMPARE(asked.first().value(0).toString(), commit.hash);
        QTRY_VERIFY(card->isVisible());
        QCOMPARE(card->start(), commit.hash);
        card->dismiss();
    }

    // One overlay at a time: Ctrl+N over the agent settings leaves the card
    // alone, the keyboard in its name; the agent settings opening close the
    // card; so does the branch menu (Ctrl+3).
    void theNewBranchCardIsTheOnlyOverlay()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(activate(f.window.get()));
        AgentPopover *agent = f.agentCard();
        NewBranchCard *card = f.newBranchCard();
        QTest::mouseClick(f.page()->agentButton(), Qt::LeftButton);
        settle();
        QVERIFY(agent->isVisible());
        QTest::keyClick(f.window.get(), Qt::Key_N, Qt::ControlModifier);
        QTRY_VERIFY(card->isVisible());
        QVERIFY(!agent->isVisible());
        QCOMPARE(QApplication::focusWidget(), card->nameField());

        agent->popup(f.page()->agentButton());
        settle();
        QVERIFY(agent->isVisible());
        QVERIFY(!card->isVisible());
        agent->dismiss();

        QTest::keyClick(f.window.get(), Qt::Key_N, Qt::ControlModifier);
        QTRY_VERIFY(card->isVisible());
        // The menu runs its own loop; once it is up, it is closed again.
        bool menuShown = false, cardShown = true;
        QTimer::singleShot(0, f.window.get(), [&menuShown, &cardShown, card] {
            QElapsedTimer clock;
            clock.start();
            BranchMenu *menu = nullptr;
            while (!(menu = qobject_cast<BranchMenu *>(QApplication::activePopupWidget())) && clock.elapsed() < 5000)
                QTest::qWait(10);
            menuShown = menu != nullptr;
            cardShown = card->isVisible();
            if (menu)
                menu->close();
        });
        QTest::keyClick(f.window.get(), Qt::Key_3, Qt::ControlModifier);
        QVERIFY(menuShown);
        QVERIFY(!cardShown);
        QVERIFY(!card->isVisible());
    }

    // The card open over a refresh reads the repository again, without being
    // opened again: a changed file coming to differ at the start brings the
    // warning and takes Switch to it away, reverted it gives them back; a
    // branch made meanwhile under the typed name is taken; a start that is
    // gone gives way to the current branch. The name, the caret, Switch to
    // it as it was left and the keyboard stay.
    void theNewBranchCardFollowsTheRepository()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(activate(f.window.get()));
        const QString root = f.repo->root();
        // b.txt is new in HEAD: at the first commit it is not there yet.
        const Commit first = f.repo->headCommit();
        QVERIFY(writeFixture(QDir(root).filePath(QStringLiteral("b.txt")), "b\n"));
        QVERIFY(git(root, {"add", "b.txt"}));
        QVERIFY(git(root, {"commit", "-q", "-m", "second"}, 2));
        f.window->refresh();
        settle();

        NewBranchCard *card = openNewBranchCard(f, first.hash);
        QVERIFY(card);
        QTest::keyClicks(card->nameField(), QStringLiteral("work"));
        QTest::keyClick(card->nameField(), Qt::Key_Left);
        QCOMPARE(card->fromNote(), QStringLiteral("the commit picked in History"));
        QCOMPARE(card->carryNote(), QStringLiteral("Your 2 changed files come along."));
        QVERIFY(!card->blockedNote()->isVisible());
        QVERIFY(card->switchBox()->isEnabled() && card->switchBox()->isChecked());
        const auto kept = [card] {
            return card->isVisible() && card->nameField()->text() == QLatin1String("work")
                && card->nameField()->cursorPosition() == 3 && QApplication::focusWidget() == card->nameField();
        };

        // b.txt changed: it would be overwritten at the first commit.
        QVERIFY(writeFixture(QDir(root).filePath(QStringLiteral("b.txt")), "b changed\n"));
        f.window->refresh();
        settle();
        QVERIFY(kept());
        QCOMPARE(card->start(), first.hash);
        QCOMPARE(card->fromNote(), QStringLiteral("the commit picked in History"));
        QVERIFY(card->blockedNote()->isVisible());
        QCOMPARE(card->carryNote(), QStringLiteral("1 changed file differs at %1").arg(first.shortHash));
        QVERIFY(!card->switchBox()->isEnabled() && !card->switchBox()->isChecked());
        // Reverted: nothing in the way, Switch to it back as it was.
        QVERIFY(git(root, {"checkout", "-q", "--", "b.txt"}));
        f.window->refresh();
        settle();
        QVERIFY(kept());
        QVERIFY(!card->blockedNote()->isVisible());
        QCOMPARE(card->carryNote(), QStringLiteral("Your 2 changed files come along."));
        QVERIFY(card->switchBox()->isEnabled() && card->switchBox()->isChecked());
        // Switch to it as the user leaves it survives a refresh.
        card->switchBox()->click();
        f.window->refresh();
        settle();
        QVERIFY(card->switchBox()->isEnabled() && !card->switchBox()->isChecked());

        // The typed name made a branch meanwhile.
        QVERIFY(card->createButton()->isEnabled());
        QVERIFY(git(root, {"branch", "work"}));
        f.window->refresh();
        settle();
        QVERIFY(kept());
        QCOMPARE(card->errorText(), QStringLiteral("Already a branch"));
        QVERIFY(!card->createButton()->isEnabled());
        QVERIFY(card->switchToExistingButton()->isVisible());
        card->dismiss();

        // A start that is gone: the current branch.
        QVERIFY(git(root, {"branch", "doomed"}));
        card = openNewBranchCard(f, QStringLiteral("doomed"));
        QVERIFY(card);
        QCOMPARE(card->start(), QStringLiteral("doomed"));
        QVERIFY(git(root, {"branch", "-D", "-q", "doomed"}));
        f.window->refresh();
        settle();
        QVERIFY(card->isVisible());
        QVERIFY(card->start().isEmpty());
        QCOMPARE(card->startPicker()->branch(), QStringLiteral("main"));
        QCOMPARE(card->fromNote(), QStringLiteral("the current branch"));
        card->dismiss();
    }

    // The From menu hangs 4 under the picker (frame 72: the picker's bottom
    // plus GAP.cluster), at a 12 px text.
    void theFromMenuHangsUnderThePicker()
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

        WindowFixture f = mainWindow(0, false, [](MainWindow *w) { w->resize(945, 612); });
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        settle();
        NewBranchCard *card = openNewBranchCard(f);
        QVERIFY(card);
        BranchPicker *picker = card->startPicker();
        int menuTop = -1;
        inPickerMenu(picker, [&menuTop](BranchMenu *menu) { menuTop = menu->geometry().top(); });
        QCOMPARE(menuTop, picker->mapToGlobal(QPoint(0, picker->height())).y() + 4);
        card->dismiss();
        f.window.reset(); // before the theme it was built with
    }

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
};

UI_TEST(BranchesTest);

#include "branches_test.moc"
