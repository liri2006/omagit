// The agent settings card: no agent installed, the models and effort levels
// of fake claude and codex CLIs, each choice saved, and where the card opens
// and how it takes the keyboard and gives it back.
#include "UiTestCase.h"
#include "fixtures.h"

#include "../../src/Segmented.h"
#include "../../src/UiHelpers.h"

#include <QApplication>
#include <QClipboard>
#include <QLineEdit>
#include <QSignalSpy>

namespace {

// The parseCodexModels sample: GPT-6-Astra with six levels, GPT-5.5 with two.
const char kCodexModels[] =
    "{\"models\":[{\"slug\":\"gpt-5.5\",\"display_name\":\"GPT-5.5\",\"visibility\":\"list\",\"priority\":12,"
    "\"default_reasoning_level\":\"medium\",\"supported_reasoning_levels\":[{\"effort\":\"low\"},{\"effort\":\"high\"}]},"
    "{\"slug\":\"gpt-reserve\",\"display_name\":\"GPT-Reserve\",\"visibility\":\"hide\",\"priority\":3},"
    "{\"slug\":\"gpt-6-astra\",\"display_name\":\"GPT-6-Astra\",\"visibility\":\"list\",\"priority\":1,"
    "\"default_reasoning_level\":\"medium\",\"supported_reasoning_levels\":[{\"effort\":\"low\"},{\"effort\":\"medium\"},"
    "{\"effort\":\"high\"},{\"effort\":\"xhigh\"},{\"effort\":\"max\"},{\"effort\":\"ultra\"}]}]}";

// A fake `codex`: `debug models` prints the catalog sample.
bool writeFakeCodex(const QString &dir)
{
    return writeFakeAgent(dir, QStringLiteral("codex"), "debug", kCodexModels);
}

// The model rows as the card shows them: name and id, the chosen one marked.
QStringList rowTexts(const AgentPopover *card)
{
    QStringList out;
    for (const QAbstractButton *row : card->modelRows())
        out << row->text() + QLatin1Char('|') + row->accessibleDescription() + (row->isChecked() ? QStringLiteral("|*") : QString());
    return out;
}

// The model row named `name`, or null.
QAbstractButton *modelRow(const AgentPopover *card, const QString &name)
{
    for (QAbstractButton *row : card->modelRows())
        if (row->text() == name)
            return row;
    return nullptr;
}

QString savedAgent(const char *key)
{
    return QSettings().value(QLatin1String(key)).toString();
}

// The visible labels of the card, their texts.
QStringList cardLabels(const AgentPopover *card)
{
    QStringList out;
    for (const QLabel *l : card->findChildren<QLabel *>())
        if (l->isVisible())
            out << l->text();
    return out;
}

} // namespace

class AgentTest : public UiTestCase
{
    Q_OBJECT
private slots:
    // Nothing on PATH but git: the card says so, offers the two commands and
    // copies them; none of the settings' parts is there.
    void theAgentCardSaysWhenNoAgentIsInstalled()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(activate(f.window.get()));
        AgentScope scope(f.tools->path());
        AgentPopover *card = f.agentCard();
        QVERIFY(card);
        QVERIFY(!card->isVisible());
        QCOMPARE(card->parentWidget(), f.host());

        QTest::mouseClick(f.page()->agentButton(), Qt::LeftButton);
        settle();
        QVERIFY(card->isVisible());
        const QStringList labels = cardLabels(card);
        for (const QString &text : {QStringLiteral("No coding agent installed"), QStringLiteral("Claude Code or Codex writes it for you."),
                                    QStringLiteral("INSTALL ONE"), QStringLiteral("$ omarchy default agent claude"),
                                    QStringLiteral("$ omarchy default agent codex"),
                                    QStringLiteral("Reopen this menu once one is installed.")})
            QVERIFY2(labels.contains(text), qPrintable(text + QStringLiteral(" in ") + labels.join(QLatin1Char('/'))));
        QVERIFY(!card->agentPicker());
        QVERIFY(card->modelRows().isEmpty());
        QVERIFY(!card->otherModelButton() && !card->otherModelField());
        QVERIFY(!card->levelTrack());
        QVERIFY(!card->generateButton());
        // The robot: muted ink at the top left, in the design's 20 px glyph.
        const QImage image = card->grab().toImage();
        bool robot = false;
        const QColor muted = OmarchyTheme::instance()->mutedText();
        for (int x = ui::space(10); x < ui::space(10) + ui::space(32); ++x)
            for (int y = ui::space(10); y < ui::space(10) + ui::space(24); ++y)
                robot = robot || closeTo(image.pixelColor(x, y), muted);
        QVERIFY(robot);

        QCOMPARE(card->copyButtons().size(), 2);
        QSignalSpy status(f.page(), &CommitPage::statusMessage);
        QApplication::clipboard()->clear();
        QTest::mouseClick(card->copyButtons().first(), Qt::LeftButton);
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("omarchy default agent claude"));
        QCOMPARE(status.count(), 1);
        QCOMPARE(status.first().first().toString(), QStringLiteral("Copied"));
        QTest::mouseClick(card->copyButtons().last(), Qt::LeftButton);
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("omarchy default agent codex"));
        QVERIFY(card->isVisible()); // copying is no reason to close
        QVERIFY(QSettings().childKeys().filter(QStringLiteral("agent")).isEmpty());
        QVERIFY(!QSettings().childGroups().contains(QStringLiteral("agent")));
    }

    // A fake claude on PATH: its name on the picker, the models its --help
    // names, and its levels on the track; Default chosen while nothing is saved.
    void theAgentCardListsTheAgentsModelsAndLevels()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(writeFakeClaude(f.tools->path()));
        AgentScope scope(f.tools->path(), QStringLiteral("claude"));
        AgentPopover *card = f.agentCard();
        QTest::mouseClick(f.page()->agentButton(), Qt::LeftButton);
        settle();
        QVERIFY(card->isVisible());

        SegmentStrip *picker = card->agentPicker();
        QVERIFY(picker);
        QVERIFY(picker->isStretch());
        QCOMPARE(picker->segments().size(), 1);
        QCOMPARE(picker->segments().first()->text(), QStringLiteral("Claude Code"));
        QVERIFY(picker->segments().first()->isChecked());
        QCOMPARE(picker->segments().first()->width(), picker->width() - 2); // one agent, the whole width
        QCOMPARE(picker->height(), ui::space(28));

        QCOMPARE(rowTexts(card), QStringList({QStringLiteral("Default|whatever claude uses|*"), QStringLiteral("Fable|fable"),
                                              QStringLiteral("Opus|opus"), QStringLiteral("Sonnet|sonnet")}));
        QVERIFY(card->levelTrack());
        QCOMPARE(card->levelTrack()->labels(), QStringList({QStringLiteral("Default"), QStringLiteral("Low"), QStringLiteral("Medium"),
                                                            QStringLiteral("High"), QStringLiteral("Xhigh"), QStringLiteral("Max")}));
        QCOMPARE(card->levelTrack()->selected(), 0);
        const QStringList labels = cardLabels(card);
        for (const QString &text : {QStringLiteral("AGENT"), QStringLiteral("claude is the Omarchy default"), QStringLiteral("MODEL"),
                                    QStringLiteral("from claude --help"), QStringLiteral("REASONING"),
                                    QStringLiteral("more thinking, slower answer")})
            QVERIFY2(labels.contains(text), qPrintable(text + QStringLiteral(" in ") + labels.join(QLatin1Char('/'))));
        QVERIFY(card->otherModelButton()->isVisible());
        QVERIFY(!card->otherModelField()->isVisible());
        QVERIFY(card->generateButton()->isVisible());
        QVERIFY(card->generateButton()->text().endsWith(QStringLiteral("Generate now  Ctrl+G")));
        // Opening it saves nothing.
        QVERIFY(!QSettings().childGroups().contains(QStringLiteral("agent")));
    }

    // Every choice is saved the moment it is made, the page's tooltip follows,
    // and a level the new model does not have goes.
    void theAgentCardSavesEachChoice()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(activate(f.window.get()));
        QVERIFY(writeFakeClaude(f.tools->path()));
        AgentScope scope(f.tools->path());
        AgentPopover *card = f.agentCard();
        QTest::mouseClick(f.page()->agentButton(), Qt::LeftButton);
        settle();
        QVERIFY(card->isVisible());

        const QRect before = card->geometry();
        QTest::mouseClick(modelRow(card, QStringLiteral("Opus")), Qt::LeftButton);
        settle();
        QCOMPARE(savedAgent("agent/name"), QStringLiteral("claude"));
        QCOMPARE(savedAgent("agent/model"), QStringLiteral("opus"));
        QVERIFY(card->isVisible()); // a choice is no reason to close
        QCOMPARE(card->geometry(), before);
        QAbstractButton *opus = modelRow(card, QStringLiteral("Opus"));
        QVERIFY(opus->isChecked());
        QVERIFY(opus->font().bold());
        QVERIFY(!modelRow(card, QStringLiteral("Default"))->isChecked());
        QVERIFY(!modelRow(card, QStringLiteral("Default"))->font().bold());
        // The name in the accent: some of its ink is the accent itself.
        {
            const QImage image = opus->grab().toImage();
            bool accent = false;
            for (int x = ui::space(10); x < ui::space(60); ++x)
                for (int y = 0; y < image.height(); ++y)
                    accent = accent || closeTo(image.pixelColor(x, y), OmarchyTheme::instance()->accent());
            QVERIFY(accent);
        }
        QVERIFY(f.page()->commitControls().generateTip.contains(QStringLiteral("(opus)")));
        QVERIFY(f.pageEditor()->cornerButton()->toolTip().contains(QStringLiteral("(opus)")));
        // A second click on the chosen row changes nothing.
        QTest::mouseClick(modelRow(card, QStringLiteral("Opus")), Qt::LeftButton);
        settle();
        QVERIFY(modelRow(card, QStringLiteral("Opus"))->isChecked());

        // The track: a click on High, then the keys.
        LevelTrack *track = card->levelTrack();
        QTest::mouseClick(track, Qt::LeftButton, {}, track->stopCentre(3).toPoint());
        settle();
        QCOMPARE(savedAgent("agent/effort"), QStringLiteral("high"));
        track = card->levelTrack();
        QCOMPARE(track->selected(), 3);
        track->setFocus();
        QTRY_VERIFY(track->hasFocus());
        QTest::keyClick(track, Qt::Key_Left);
        settle();
        QCOMPARE(savedAgent("agent/effort"), QStringLiteral("medium"));
        QTRY_VERIFY(card->levelTrack()->hasFocus()); // the keyboard stays on the (new) track
        QTest::keyClick(card->levelTrack(), Qt::Key_Right);
        QTest::keyClick(card->levelTrack(), Qt::Key_Right);
        settle();
        QCOMPARE(savedAgent("agent/effort"), QStringLiteral("xhigh"));
        QCOMPARE(card->levelTrack()->selected(), 4);
        QTest::keyClick(card->levelTrack(), Qt::Key_Left);
        settle();
        QCOMPARE(savedAgent("agent/effort"), QStringLiteral("high"));

        // A level Sonnet has stays; one it has not goes.
        QTest::mouseClick(modelRow(card, QStringLiteral("Sonnet")), Qt::LeftButton);
        settle();
        QCOMPARE(savedAgent("agent/model"), QStringLiteral("sonnet"));
        QCOMPARE(savedAgent("agent/effort"), QStringLiteral("high"));

        // A saved level the track does not have shows as Default, and a click
        // on Default, the stop already lit, clears it.
        CommitMessageAgent::saveChoice(AgentChoice{QStringLiteral("claude"), QStringLiteral("opus"), QStringLiteral("ultra")});
        card->dismiss();
        QTest::mouseClick(f.page()->agentButton(), Qt::LeftButton);
        settle();
        QVERIFY(card->isVisible());
        QCOMPARE(card->levelTrack()->selected(), 0);
        QTest::mouseClick(card->levelTrack(), Qt::LeftButton, {}, card->levelTrack()->stopCentre(0).toPoint());
        settle();
        QCOMPARE(savedAgent("agent/effort"), QString());
        QCOMPARE(savedAgent("agent/model"), QStringLiteral("opus"));
        QCOMPARE(CommitMessageAgent::savedChoice().effort, QString());
        QCOMPARE(card->levelTrack()->selected(), 0);
        QVERIFY(f.page()->commitControls().generateTip.contains(QStringLiteral("(opus)")));

        CommitMessageAgent::saveChoice(AgentChoice{QStringLiteral("claude"), QStringLiteral("opus"), QStringLiteral("ultra")});
        card->dismiss();
        QTest::mouseClick(f.page()->agentButton(), Qt::LeftButton);
        settle();
        QCOMPARE(card->levelTrack()->selected(), 0); // a level the track does not have is its Default
        QTest::mouseClick(modelRow(card, QStringLiteral("Sonnet")), Qt::LeftButton);
        settle();
        QCOMPARE(savedAgent("agent/model"), QStringLiteral("sonnet"));
        QCOMPARE(savedAgent("agent/effort"), QString());
        // Default is a model like the others.
        QTest::mouseClick(modelRow(card, QStringLiteral("Default")), Qt::LeftButton);
        settle();
        QCOMPARE(savedAgent("agent/model"), QString());
        QVERIFY(f.page()->commitControls().generateTip.contains(QStringLiteral("(default model)")));
    }

    // A model by name: the field opens on demand, Return saves and shows the
    // name as a row, Escape closes the field before the card.
    void theAgentCardTakesAModelByName()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(activate(f.window.get()));
        QVERIFY(writeFakeClaude(f.tools->path()));
        AgentScope scope(f.tools->path());
        AgentPopover *card = f.agentCard();
        QTest::mouseClick(f.page()->agentButton(), Qt::LeftButton);
        settle();
        QVERIFY(card->isVisible());
        const int height = card->height();

        QTest::mouseClick(card->otherModelButton(), Qt::LeftButton);
        settle();
        QLineEdit *field = card->otherModelField();
        QVERIFY(field->isVisible());
        QVERIFY(!card->otherModelButton()->isVisible());
        QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget *>(field));
        QCOMPARE(field->text(), QString());
        QCOMPARE(field->placeholderText(), QStringLiteral("Model name, as claude --model takes it"));
        QCOMPARE(field->width(), card->generateButton()->width()); // the inner width
        QCOMPARE(card->height(), height); // the field takes the button's row
        QTest::keyClicks(field, QStringLiteral("claude-x"));
        QTest::keyClick(field, Qt::Key_Return);
        settle();
        QCOMPARE(savedAgent("agent/model"), QStringLiteral("claude-x"));
        QVERIFY(card->isVisible());
        QCOMPARE(rowTexts(card), QStringList({QStringLiteral("Default|whatever claude uses"), QStringLiteral("Claude-x|claude-x|*"),
                                              QStringLiteral("Fable|fable"), QStringLiteral("Opus|opus"),
                                              QStringLiteral("Sonnet|sonnet")}));
        QVERIFY(!card->otherModelField()->isVisible());
        QVERIFY(card->otherModelButton()->isVisible());
        QTRY_VERIFY(card->hasFocus());

        // Open again: the name is there to edit. Escape: back, nothing changed.
        QTest::mouseClick(card->otherModelButton(), Qt::LeftButton);
        settle();
        field = card->otherModelField();
        QCOMPARE(field->text(), QStringLiteral("claude-x"));
        QCOMPARE(field->selectedText(), QStringLiteral("claude-x"));
        QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget *>(field));
        QTest::keyClicks(field, QStringLiteral("other"));
        QTest::keyClick(field, Qt::Key_Escape);
        settle();
        QVERIFY(card->isVisible());
        QVERIFY(!field->isVisible());
        QVERIFY(card->otherModelButton()->isVisible());
        QCOMPARE(savedAgent("agent/model"), QStringLiteral("claude-x"));
        QTRY_VERIFY(card->hasFocus());
        QTest::keyClick(card, Qt::Key_Escape);
        settle();
        QVERIFY(!card->isVisible());

        // An empty name is Default.
        QTest::mouseClick(f.page()->agentButton(), Qt::LeftButton);
        settle();
        QTest::mouseClick(card->otherModelButton(), Qt::LeftButton);
        settle();
        card->otherModelField()->clear();
        QTest::keyClick(card->otherModelField(), Qt::Key_Return);
        settle();
        QCOMPARE(savedAgent("agent/model"), QString());
        QVERIFY(modelRow(card, QStringLiteral("Default"))->isChecked());
        QVERIFY(!modelRow(card, QStringLiteral("Claude-x")));
    }

    // Generate now closes the card and asks the agent, as the page's own
    // button does.
    void theAgentCardGeneratesNow()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(activate(f.window.get()));
        QVERIFY(writeFakeClaude(f.tools->path()));
        AgentScope scope(f.tools->path());
        AgentPopover *card = f.agentCard();
        QTest::mouseClick(f.page()->agentButton(), Qt::LeftButton);
        settle();
        QVERIFY(card->isVisible());
        QVERIFY(card->generateButton()->isDefault());
        QTest::mouseClick(card->generateButton(), Qt::LeftButton);
        QVERIFY(!card->isVisible());
        QTRY_COMPARE_WITH_TIMEOUT(f.pageEditor()->toPlainText(), QStringLiteral("Fake subject"), 10000);
    }

    // While a run is going, Generate now is off, saying why, and the rest of
    // the card, an open other-model field with it, stays as it was; once the
    // run is over it is on again.
    void theAgentCardWaitsForARunningAgent()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(activate(f.window.get()));
        QVERIFY(writeFakeClaude(f.tools->path()));
        AgentScope scope(f.tools->path());
        AgentPopover *card = f.agentCard();
        QTest::mouseClick(f.page()->agentButton(), Qt::LeftButton);
        settle();
        QVERIFY(card->isVisible());
        QVERIFY(!f.page()->commitControls().generating);
        QPushButton *generate = card->generateButton();
        QVERIFY(generate->isEnabled());
        const QString tip = generate->toolTip();
        QTest::mouseClick(card->otherModelButton(), Qt::LeftButton);
        settle();
        QLineEdit *field = card->otherModelField();
        QTest::keyClicks(field, QStringLiteral("claude-x"));

        // Checked before the event loop runs again, so before the fake can
        // have answered.
        f.page()->generateMessage();
        QVERIFY(f.page()->commitControls().generating);
        QVERIFY(card->isVisible());
        QCOMPARE(card->generateButton(), generate); // followed, not rebuilt
        QVERIFY(!generate->isEnabled());
        QCOMPARE(generate->toolTip(), QStringLiteral("The agent is writing the message — the sparkle stops it"));
        QCOMPARE(card->otherModelField(), field);
        QVERIFY(field->isVisible());
        QCOMPARE(field->text(), QStringLiteral("claude-x"));

        QTRY_COMPARE_WITH_TIMEOUT(f.pageEditor()->toPlainText(), QStringLiteral("Fake subject"), 10000);
        QVERIFY(!f.page()->commitControls().generating);
        QVERIFY(generate->isEnabled());
        QCOMPARE(generate->toolTip(), tip);
        QCOMPARE(card->otherModelField(), field);
        QCOMPARE(field->text(), QStringLiteral("claude-x"));
        QVERIFY(QSettings().value(QStringLiteral("agent/model")).toString().isEmpty());
    }

    // The cogs open it — the page's in Docked, the commit card's in Mini —
    // and close it again; the slot main() calls opens the one of the layout
    // of the moment, and none in the history.
    void theAgentCardOpensFromTheCogs()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(activate(f.window.get()));
        AgentScope scope(f.tools->path());
        AgentPopover *card = f.agentCard();
        QSignalSpy opened(card, &AgentPopover::opened);
        QToolButton *pageCog = f.page()->agentButton();

        QTest::mouseClick(pageCog, Qt::LeftButton);
        settle();
        QVERIFY(card->isVisible());
        QCOMPARE(card->anchor(), static_cast<QWidget *>(pageCog));
        QCOMPARE(opened.count(), 1);
        QTest::mouseClick(pageCog, Qt::LeftButton); // the cog toggles
        settle();
        QVERIFY(!card->isVisible());

        QVERIFY(QMetaObject::invokeMethod(f.window.get(), "showAgentMenu"));
        settle();
        QVERIFY(card->isVisible());
        QCOMPARE(card->anchor(), static_cast<QWidget *>(pageCog));
        const QRect geometry = card->geometry();
        QVERIFY(QMetaObject::invokeMethod(f.window.get(), "showAgentMenu")); // again: stays, same place
        settle();
        QVERIFY(card->isVisible());
        QCOMPARE(card->geometry(), geometry);
        QCOMPARE(f.window->findChildren<AgentPopover *>().size(), 1);
        card->dismiss();

        // Mini: from the commit card's cog.
        QVERIFY(f.openCard());
        QToolButton *cardCog = f.popover()->agentButton();
        QTest::mouseClick(cardCog, Qt::LeftButton);
        settle();
        QVERIFY(card->isVisible());
        QVERIFY(f.popover()->isVisible());
        QCOMPARE(card->anchor(), static_cast<QWidget *>(cardCog));
        QTest::mouseClick(cardCog, Qt::LeftButton);
        settle();
        QVERIFY(!card->isVisible());
        QVERIFY(f.popover()->isVisible());
        // The slot in Mini opens the commit card too.
        f.popover()->dismiss();
        settle();
        QVERIFY(QMetaObject::invokeMethod(f.window.get(), "showAgentMenu"));
        settle();
        QVERIFY(f.popover()->isVisible());
        QVERIFY(card->isVisible());
        QCOMPARE(card->anchor(), static_cast<QWidget *>(cardCog));
        QCOMPARE(card->x(), f.popover()->geometry().right() + 1 + ui::space(8)); // beside the commit card

        // The history has no cog.
        f.window->setMode(MainWindow::HistoryMode);
        settle();
        QVERIFY(!card->isVisible());
        QVERIFY(QMetaObject::invokeMethod(f.window.get(), "showAgentMenu"));
        settle();
        QVERIFY(!card->isVisible());
        QVERIFY(!f.popover()->isVisible());
    }

    // Over the cog, which stands low on the commit page (right over the
    // message box), its right edge on the cog's, 360 wide where there is
    // room, clamped by the window's margins where there is not, moved up in a
    // short window (hanging from More there, the cog's row folded away), and
    // placed again when its height changes. Under a cog with room below it:
    // theAgentCardSitsBesideTheCommitCard().
    void theAgentCardStandsOverItsCog()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(writeFakeClaude(f.tools->path()));
        QVERIFY(writeFakeCodex(f.tools->path()));
        AgentScope scope(f.tools->path(), QStringLiteral("claude"));
        AgentPopover *card = f.agentCard();
        QWidget *host = f.host();
        // The window's margin all round, which the card stays inside.
        const auto margins = [&] {
            const int m = ui::windowMargin(f.window.get());
            return QMargins(m, m, m, m);
        };
        QToolButton *cog = f.page()->agentButton();
        const auto cogRect = [&] { return rectIn(cog, host); };

        // Wide enough for a left section (the design's 400 of the large class)
        // that the card can hang from the cog's right edge.
        f.window->resize(1200, 1234);
        settle();
        QTest::mouseClick(cog, Qt::LeftButton);
        settle();
        QVERIFY(card->isVisible());
        QCOMPARE(card->width(), ui::space(360));
        QCOMPARE(card->geometry().right(), cogRect().right() + ui::space(ui::gap::icon));
        QCOMPARE(card->geometry().bottom() + 1 + ui::space(ui::gap::cluster), cogRect().y());
        QCOMPARE(card->height(), card->sizeHint().height());

        // Another agent, another height; the bottom stays over the cog.
        const int claudeHeight = card->height();
        QVERIFY(card->agentPicker());
        QCOMPARE(card->agentPicker()->segments().size(), 2);
        QTest::mouseClick(card->agentPicker()->segments().last(), Qt::LeftButton);
        settle();
        QCOMPARE(savedAgent("agent/name"), QStringLiteral("codex"));
        QCOMPARE(rowTexts(card), QStringList({QStringLiteral("Default|whatever codex uses|*"), QStringLiteral("GPT-6-Astra|gpt-6-astra"),
                                              QStringLiteral("GPT-5.5|gpt-5.5")}));
        QCOMPARE(card->levelTrack()->labels().size(), 7);
        QVERIFY(card->height() != claudeHeight);
        QCOMPARE(card->height(), card->sizeHint().height());
        QCOMPARE(card->geometry().bottom() + 1 + ui::space(ui::gap::cluster), cogRect().y());
        QCOMPARE(card->geometry().right(), cogRect().right() + ui::space(ui::gap::icon));
        // A model with fewer levels, a shorter track; none, no track at all.
        QTest::mouseClick(modelRow(card, QStringLiteral("GPT-5.5")), Qt::LeftButton);
        settle();
        QCOMPARE(card->levelTrack()->labels(), QStringList({QStringLiteral("Default"), QStringLiteral("Low"), QStringLiteral("High")}));

        // Narrow: as wide as the margins allow, from the left margin. The top
        // bar keeps the window wider than that; a minimum of the test's own
        // lets it be squeezed anyway. So narrow a window has its bar on two
        // rows and the page's header rows folded away: the card went with the
        // cog, and opens again from More, which carries the cog's entry.
        f.window->setMinimumSize(1, 1);
        f.window->resize(300, 1234);
        settle();
        QVERIFY(f.page()->headerRowsHidden());
        QVERIFY(!card->isVisible());
        QVERIFY(QMetaObject::invokeMethod(f.window.get(), "showAgentMenu"));
        settle();
        QVERIFY(card->isVisible());
        QToolButton *more = f.bar()->moreButton();
        QCOMPARE(card->anchor(), more);
        QTRY_COMPARE(card->width(), qMin(ui::space(360), host->width() - margins().left() - margins().right()));
        QCOMPARE(card->x(), qMax(margins().left(), qMin(rectIn(more, host).right() + 1 + ui::space(ui::gap::icon),
                                                        host->width() - margins().right())
                                                    - card->width()));
        QVERIFY(host->width() < 360 + margins().left() + margins().right());
        QCOMPARE(card->x(), margins().left());

        // Short: moved up to fit, never above the top margin. Widening out of
        // the stacked width closes the card, so it is opened again first —
        // from More again: a shallow window folds the cog's row away too.
        f.window->resize(945, 360);
        settle();
        QVERIFY(!card->isVisible());
        QVERIFY(f.page()->headerRowsHidden());
        QVERIFY(!cog->isVisible());
        QVERIFY(QMetaObject::invokeMethod(f.window.get(), "showAgentMenu"));
        settle();
        QVERIFY(card->isVisible());
        QCOMPARE(card->anchor(), more);
        QTRY_VERIFY(card->geometry().bottom() + 1 <= host->height() - margins().bottom()
                    || card->y() == margins().top());
        QVERIFY(card->y() < rectIn(more, host).bottom() + 1 + ui::space(ui::gap::cluster));
        QVERIFY(card->y() >= margins().top());
    }

    // Mini, from the commit card's cog: beside that card, 8 to its right and
    // level with it, never over it; where the room right of it is under 240,
    // under the cog again, or over it where the window has no room under it.
    void theAgentCardSitsBesideTheCommitCard()
    {
        QTemporaryDir dir, home;
        QVERIFY(dir.isValid() && home.isValid());
        QVERIFY(writeFixture(QDir(dir.path()).filePath(QStringLiteral("shell.toml")), "[font]\nbase-size = 12\n"));
        {
            ScopedEnv themeDir("OMAGIT_THEME_DIR", dir.path().toUtf8());
            ScopedEnv scratchHome("HOME", home.path().toUtf8());
            OmarchyTheme theme;
            QCOMPARE(theme.fontBase(), 12);
            theme.apply(*qApp);
            {
                WindowFixture f = mainWindow();
                QVERIFY(f.window);
                QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
                QVERIFY(writeFakeClaude(f.tools->path()));
                AgentScope scope(f.tools->path(), QStringLiteral("claude"));
                f.window->resize(945, 1234);
                settle();
                QVERIFY(f.openCard());
                CommitPopover *commitCard = f.popover();
                AgentPopover *card = f.agentCard();
                QWidget *host = f.host();
                // The window's margin all round, which the card stays inside.
                const auto margins = [&] {
                    const int m = ui::windowMargin(f.window.get());
                    return QMargins(m, m, m, m);
                };
                QToolButton *cog = commitCard->agentButton();
                QTest::mouseClick(cog, Qt::LeftButton);
                settle();
                QVERIFY(card->isVisible());
                QVERIFY(commitCard->isVisible());
                const QRect commitRect = commitCard->geometry();
                QCOMPARE(card->x(), commitRect.right() + 1 + ui::space(8));
                QVERIFY(card->y() <= commitRect.y());
                QVERIFY2(!card->geometry().intersects(commitRect),
                         qPrintable(QStringLiteral("%1,%2 %3x%4 / %5,%6 %7x%8")
                                        .arg(card->x()).arg(card->y()).arg(card->width()).arg(card->height())
                                        .arg(commitRect.x()).arg(commitRect.y()).arg(commitRect.width()).arg(commitRect.height())));
                QCOMPARE(card->width(), ui::space(360));
                QCOMPARE(card->height(), card->sizeHint().height());

                // Narrower, until the room right of the commit card is under
                // 240: under the cog where it fits, over it where that does,
                // its right edge on the cog's, and otherwise moved up as far
                // as the window's bottom margin asks.
                f.window->setMinimumSize(1, 1);
                const auto room = [&] {
                    return host->width() - margins().right() - (commitCard->geometry().right() + 1 + ui::space(8));
                };
                for (int width = 945; width > 300 && room() >= ui::space(240); width -= 10) {
                    f.window->resize(width, 1234);
                    settle();
                }
                QVERIFY(room() < ui::space(240));
                QVERIFY(commitCard->isVisible());
                QVERIFY(card->isVisible());
                const QRect cogRect = rectIn(cog, host);
                const int under = cogRect.bottom() + 1 + ui::space(ui::gap::cluster);
                const int over = cogRect.y() - ui::space(ui::gap::cluster) - card->height();
                const int bottom = host->height() - margins().bottom();
                QTRY_COMPARE(card->y(), under + card->height() <= bottom ? under
                                        : over >= margins().top()           ? over
                                                                          : qMax(margins().top(), bottom - card->height()));
                // The card's right edge on the edge of the column the cog stands in, 4 past the cog's.
                const int column = qMin(cogRect.right() + 1 + ui::space(ui::gap::icon), host->width() - margins().right());
                QCOMPARE(card->x(), qMax(margins().left(), column - card->width()));
                QCOMPARE(card->width(), qMin(ui::space(360), host->width() - margins().left() - margins().right()));
            }
        }
        g_theme.reset(new OmarchyTheme);
        g_theme->apply(*qApp);
        QVERIFY(OmarchyTheme::instance() == g_theme.get());
    }

    // The two cards together: a press on the agent card leaves the commit
    // card open; whatever closes the commit card, the history, the layout
    // switch and another repository close the agent card; a press on the
    // message box closes it and goes on to the box.
    void theAgentCardLivesWithTheCommitCard()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(activate(f.window.get()));
        AgentScope scope(f.tools->path());
        AgentPopover *card = f.agentCard();
        CommitPopover *commitCard = f.popover();
        const auto openBoth = [&] {
            QVERIFY(f.openCard());
            QTest::mouseClick(commitCard->agentButton(), Qt::LeftButton);
            settle();
            QVERIFY(card->isVisible());
        };

        openBoth();
        // A press on the agent card, where nothing but the card is.
        const QPoint inside = card->mapTo(f.window.get(), QPoint(card->width() - 4, card->height() - 4));
        clickAt(f.window.get(), inside);
        settle();
        QVERIFY(card->isVisible());
        QVERIFY(commitCard->isVisible());
        QTest::mouseClick(card->copyButtons().first(), Qt::LeftButton);
        settle();
        QVERIFY(commitCard->isVisible());

        // Escape from the commit card's editor closes both.
        commitCard->editor()->setFocus();
        QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget *>(commitCard->editor()));
        QVERIFY(card->isVisible());
        QTest::keyClick(commitCard->editor(), Qt::Key_Escape);
        settle();
        QVERIFY(!commitCard->isVisible());
        QVERIFY(!card->isVisible());
        QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget *>(f.rail()->list()));

        // The tile.
        openBoth();
        QTest::mouseClick(f.tile(), Qt::LeftButton);
        settle();
        QVERIFY(!commitCard->isVisible());
        QVERIFY(!card->isVisible());

        // The history.
        openBoth();
        QTest::mouseClick(f.bar()->historyTab(), Qt::LeftButton);
        settle();
        QVERIFY(!card->isVisible());
        f.window->setMode(MainWindow::CommitMode);
        settle();

        // Ctrl+B, from Mini to Docked and back.
        openBoth();
        QTest::keyClick(commitCard->editor(), Qt::Key_B, Qt::ControlModifier);
        settle();
        QCOMPARE(f.window->paneLayout(), PaneLayout::Docked);
        QVERIFY(!card->isVisible());
        QTest::mouseClick(f.page()->agentButton(), Qt::LeftButton);
        settle();
        QVERIFY(card->isVisible());
        QTest::keyClick(card, Qt::Key_B, Qt::ControlModifier);
        settle();
        QCOMPARE(f.window->paneLayout(), PaneLayout::Mini);
        QVERIFY(!card->isVisible());
        // Saved, as the keys save it, so the windows of the tests after this
        // one start Docked again.
        f.window->setPaneLayout(PaneLayout::Docked);
        settle();

        // A press on the message box: closed, and the box has the keyboard —
        // not the list, which had it when the card opened.
        f.page()->table()->setFocus();
        QTest::mouseClick(f.page()->agentButton(), Qt::LeftButton);
        settle();
        QVERIFY(card->isVisible());
        MessageEdit *box = f.pageEditor();
        clickAt(f.window.get(), box->mapTo(f.window.get(), QPoint(ui::space(20), box->height() - ui::space(10))));
        settle();
        QVERIFY(!card->isVisible());
        QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget *>(box));

        // Another repository.
        QTest::mouseClick(f.page()->agentButton(), Qt::LeftButton);
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

    // A refresh under an open agent card — F5, and the file watcher after an
    // edit — in Docked and in Mini: the card (and the commit card) stay, the
    // other-model field keeps its unsaved text and the keyboard, nothing is
    // saved, and the list did refresh.
    void theAgentCardSurvivesARefresh()
    {
        for (const bool mini : {false, true}) {
            WindowFixture f = mainWindow();
            QVERIFY(f.window);
            QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
            QVERIFY(activate(f.window.get()));
            QVERIFY(writeFakeClaude(f.tools->path()));
            AgentScope scope(f.tools->path());
            AgentPopover *card = f.agentCard();
            if (mini) {
                QVERIFY(f.openCard());
                QTest::mouseClick(f.popover()->agentButton(), Qt::LeftButton);
            } else {
                QTest::mouseClick(f.page()->agentButton(), Qt::LeftButton);
            }
            settle();
            QVERIFY(card->isVisible());
            QTest::mouseClick(card->otherModelButton(), Qt::LeftButton);
            settle();
            QLineEdit *field = card->otherModelField();
            QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget *>(field));
            QTest::keyClicks(field, QStringLiteral("claude-x"));
            const auto unchanged = [&] {
                return card->isVisible() && (!mini || f.popover()->isVisible()) && card->otherModelField() == field
                    && field->isVisible() && field->text() == QStringLiteral("claude-x")
                    && QApplication::focusWidget() == field && !QSettings().childGroups().contains(QStringLiteral("agent"));
            };
            QVERIFY(unchanged());
            QAbstractItemModel *rows = f.page()->proxy();
            const int before = rows->rowCount();

            // F5, over a file that was not there.
            QVERIFY(writeFixture(QDir(f.repo->root()).filePath(QStringLiteral("zz-new.txt")), "new\n"));
            QTest::keyClick(field, Qt::Key_F5);
            settle();
            QCOMPARE(rows->rowCount(), before + 1);
            QVERIFY2(unchanged(), mini ? "Mini, F5" : "Docked, F5");

            // The watcher: a.txt written back as committed drops out of the
            // list once the debounce is over.
            QVERIFY(writeFixture(QDir(f.repo->root()).filePath(QStringLiteral("a.txt")), "a\n"));
            QTRY_COMPARE_WITH_TIMEOUT(rows->rowCount(), before, 10000);
            settle();
            QVERIFY2(unchanged(), mini ? "Mini, watcher" : "Docked, watcher");
            if (mini) {
                // Saved as Docked again for the tests after this one.
                f.window->setPaneLayout(PaneLayout::Docked);
                settle();
            }
        }
    }

    // The keyboard is on the card the moment it opens, so Escape closes it at
    // once; closing gives the keyboard back to where it was.
    void theAgentCardTakesAndReturnsTheKeyboard()
    {
        WindowFixture f = mainWindow();
        QVERIFY(f.window);
        QVERIFY(QTest::qWaitForWindowExposed(f.window.get()));
        QVERIFY(activate(f.window.get()));
        AgentScope scope(f.tools->path());
        AgentPopover *card = f.agentCard();
        QCOMPARE(f.window->paneLayout(), PaneLayout::Docked);

        f.pageEditor()->setFocus();
        QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget *>(f.pageEditor()));
        QTest::mouseClick(f.page()->agentButton(), Qt::LeftButton);
        settle();
        QVERIFY(card->isVisible());
        QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget *>(card));
        QTest::keyClick(card, Qt::Key_Escape);
        settle();
        QVERIFY(!card->isVisible());
        QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget *>(f.pageEditor()));

        QVERIFY(f.openCard());
        QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget *>(f.popover()->editor()));
        QTest::mouseClick(f.popover()->agentButton(), Qt::LeftButton);
        settle();
        QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget *>(card));
        QTest::keyClick(card, Qt::Key_Escape);
        settle();
        QVERIFY(!card->isVisible());
        QVERIFY(f.popover()->isVisible()); // one Escape, one card
        QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget *>(f.popover()->editor()));
    }
};

UI_TEST(AgentTest);

#include "agent_test.moc"
