// The footer's status and the Settings dialog's Nautilus menu: adding and
// removing the extension and restarting Nautilus, against a fake nautilus in
// temporary folders.
#include "UiTestCase.h"
#include "fixtures.h"

#include "../../src/Footer.h"
#include "../../src/NautilusMenu.h"
#include "../../src/SettingsDialog.h"
#include "../../src/UiHelpers.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>

#include <signal.h>

namespace {

// For the length of a test of the Nautilus menu: the extension folder (under
// XDG_DATA_HOME), HOME and PATH are temporary directories, so nothing of the
// user's is read or written. PATH holds a fake `nautilus` unless the test asks
// for none, and a fake `omagit` when it asks for one. The fake is a script:
// `--serve` stands in for a running Nautilus (its pid goes to serve.pid), `-q`
// quits it as `quit` says, and anything else goes to `log`, which is how a
// start shows. Whatever it left running is killed by pid when the scope
// ends, however the test ended — never by name.
class NautilusScope
{
public:
    enum Tool { NoTools = 0, FakeNautilus = 1, FakeOmagit = 2 };
    // What the fake's `nautilus -q` does: kill the serving one and exit 255,
    // as Nautilus 50's does after a quit that worked; sleep until it is
    // killed (its pid in quit.pid); exit 3 and quit nothing; or not start at
    // all — the whole fake then names an interpreter that is not there, so
    // it is set only once a serving one is running.
    enum class Quit { Kill, Hang, Fail, Unstartable };

    explicit NautilusScope(int tools = FakeNautilus, Quit quit = Quit::Kill)
        : m_sleep(QStandardPaths::findExecutable(QStringLiteral("sleep")))
        , m_dataHome("XDG_DATA_HOME", m_data.path().toUtf8())
        , m_homeEnv("HOME", m_home.path().toUtf8())
        , m_path("PATH", m_bin.path().toUtf8())
    {
        m_valid = m_data.isValid() && m_home.isValid() && m_bin.isValid() && !m_sleep.isEmpty()
            && (!(tools & FakeNautilus) || setQuit(quit))
            && (!(tools & FakeOmagit) || writeFixture(m_bin.filePath(QStringLiteral("omagit")), "#!/bin/sh\nexit 0\n", true));
    }
    ~NautilusScope() { killFakes(); }

    bool isValid() const { return m_valid; }
    QString dataPath() const { return m_data.path(); }
    QString homePath() const { return m_home.path(); }
    QString log() const
    {
        QFile file(m_bin.filePath(QStringLiteral("log")));
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
    }
    // The pid a fake wrote to `file` in the bin folder, 0 when none did.
    qint64 pid(const QString &file) const
    {
        QFile in(m_bin.filePath(file));
        return in.open(QIODevice::ReadOnly) ? in.readAll().trimmed().toLongLong() : 0;
    }

    bool setQuit(Quit quit)
    {
        // A new file, not the one a serving fake is still reading.
        const QString path = m_bin.filePath(QStringLiteral("nautilus"));
        QFile::remove(path);
        if (quit == Quit::Unstartable)
            return writeFixture(path, "#!" + m_bin.filePath(QStringLiteral("missing")).toUtf8() + "\n", true);
        const QByteArray sleep = m_sleep.toUtf8();
        const QByteArray quitting = quit == Quit::Kill
            ? "[ -f \"$dir/serve.pid\" ] && read pid < \"$dir/serve.pid\" && kill \"$pid\"\n    exit 255"
            : quit == Quit::Hang ? "echo $$ > \"$dir/quit.pid\"\n    exec '" + sleep + "' 1000"
                                 : QByteArray("exit 3");
        return writeFixture(path,
                            "#!/bin/sh\n"
                            "dir='" + m_bin.path().toUtf8() + "'\n"
                            "case \"$1\" in\n"
                            "--serve)\n"
                            "    echo $$ > \"$dir/serve.pid\"\n"
                            "    trap 'exit 0' TERM\n"
                            "    while :; do '" + sleep + "' 0.1; done\n"
                            "    ;;\n"
                            "-q)\n"
                            "    " + quitting + "\n"
                            "    ;;\n"
                            "*)\n"
                            "    echo \"$*\" >> \"$dir/log\"\n"
                            "    ;;\n"
                            "esac\n",
                            true);
    }

    // A fake Nautilus running, the way the desktop starts one: detached.
    bool serve()
    {
        if (!QProcess::startDetached(m_bin.filePath(QStringLiteral("nautilus")), {QStringLiteral("--serve")}))
            return false;
        QElapsedTimer waited;
        waited.start();
        while (waited.elapsed() < 5000) {
            if (pid(QStringLiteral("serve.pid")) > 0 && nautilusmenu::nautilusRunning())
                return true;
            QTest::qWait(20);
        }
        return false;
    }

    // Alive, and not a zombie its parent has yet to collect.
    static bool alive(qint64 pid)
    {
        QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
        if (!stat.open(QIODevice::ReadOnly))
            return false;
        const QByteArray line = stat.readAll();
        const int nameEnd = line.lastIndexOf(')');
        return nameEnd >= 0 && line.mid(nameEnd + 1).trimmed().left(1) != "Z";
    }

private:
    // Only a process with a fake's name: a pid read back late may have
    // been handed to something else since.
    void killFakes()
    {
        for (const QString &file : {QStringLiteral("serve.pid"), QStringLiteral("quit.pid")}) {
            const qint64 fake = pid(file);
            if (fake <= 1 || !alive(fake))
                continue;
            QFile comm(QStringLiteral("/proc/%1/comm").arg(fake));
            const QByteArray name = comm.open(QIODevice::ReadOnly) ? comm.readAll().trimmed() : QByteArray();
            if (name != "nautilus" && name != "sleep")
                continue;
            ::kill(pid_t(fake), SIGKILL);
            // Gone before the next test looks for a running Nautilus.
            QElapsedTimer waited;
            waited.start();
            while (alive(fake) && waited.elapsed() < 2000)
                QTest::qWait(10);
        }
    }

    QTemporaryDir m_data, m_home, m_bin;
    QString m_sleep; // looked up on the PATH of before
    ScopedEnv m_dataHome, m_homeEnv, m_path;
    bool m_valid = false;
};

// The note under the Nautilus box.
QLabel *nautilusNote(const SettingsDialog &dialog)
{
    for (QLabel *label : dialog.findChildren<QLabel *>())
        if (label->accessibleName() == QStringLiteral("Nautilus menu state"))
            return label;
    return nullptr;
}

// A note as this host shows it: without nautilus-python, the hint to add it
// comes before every quieter one.
QString quietNote(const QString &note)
{
    return nautilusmenu::pythonSupportFound()
        ? note
        : QStringLiteral("Nautilus needs nautilus-python to load it: omarchy pkg add nautilus-python");
}

// An omagit the extension would find in the system's own folders, which no
// temporary HOME or PATH hides.
bool systemOmagit()
{
    return QFileInfo(QStringLiteral("/usr/local/bin/omagit")).isExecutable()
        || QFileInfo(QStringLiteral("/usr/bin/omagit")).isExecutable();
}

} // namespace

class SettingsTest : public UiTestCase
{
    Q_OBJECT
private slots:
    // A status too long for the footer ends in an ellipsis rather than a
    // character cut in half, and its tooltip spells it out; with the room for
    // it, the whole text and no tooltip. A message and the idle text that
    // comes back after it go the same way.
    void theFooterElidesItsStatus()
    {
        QWidget host;
        host.resize(1000, 100);
        auto *footer = new Footer(&host);
        const QString path = QStringLiteral("~/Projects/") + QStringLiteral("a-rather-long-directory-name/").repeated(2)
            + QStringLiteral("omagit");
        footer->setIdleText(path);
        const int height = footer->sizeHint().height();
        footer->setGeometry(0, 0, 260, height);
        host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        settle();

        auto *label = footer->findChild<ui::ElidedLabel *>();
        QVERIFY(label);
        // The design's small regular dim text, 11 px at base 12.
        QCOMPARE(label->objectName(), QStringLiteral("footerStatus"));
        QCOMPARE(label->font().pixelSize(), qRound(OmarchyTheme::instance()->fontBase() * 11 / 12.0));
        QVERIFY(!label->font().bold());
        QCOMPARE(label->palette().color(QPalette::WindowText).rgba(), OmarchyTheme::instance()->mutedText().rgba());
        QCOMPARE(label->fullText(), path);
        const auto elided = [label, &path] {
            QVERIFY2(label->text().endsWith(QChar(0x2026)), qPrintable(label->text()));
            QVERIFY(label->fontMetrics().horizontalAdvance(label->text()) <= label->width());
            QCOMPARE(label->toolTip(), path);
        };
        elided();
        QVERIFY(label->sizeHint().width() > label->width()); // it still asks for the whole path

        footer->resize(900, height);
        settle();
        QCOMPARE(label->text(), path);
        QVERIFY(label->toolTip().isEmpty());

        footer->resize(260, height);
        settle();
        elided();
        footer->showStatus(QStringLiteral("short"), 50);
        QCOMPARE(label->text(), QStringLiteral("short"));
        QVERIFY(label->toolTip().isEmpty());
        QTRY_COMPARE(label->fullText(), path);
        elided();
    }

    // The Nautilus box writes the extension the app carries into the user's
    // extension folder and deletes it again, and says what is on disk: a
    // folder that cannot be made leaves it unticked with the reason, in red,
    // and a file that cannot be written or removed names its path.
    void theSettingsAddAndRemoveTheNautilusMenu()
    {
        NautilusScope scope(NautilusScope::FakeNautilus | NautilusScope::FakeOmagit);
        QVERIFY(scope.isValid());
        const QString folder = scope.dataPath() + QStringLiteral("/nautilus-python/extensions");
        const QString path = folder + QStringLiteral("/omagit.py");
        QCOMPARE(nautilusmenu::extensionPath(), path);
        QVERIFY(nautilusmenu::nautilusFound());
        QVERIFY(nautilusmenu::omagitFound());
        // A Nautilus of the user's own would ask for a restart after each change.
        const bool noNautilus = !nautilusmenu::nautilusRunning();
        const QString adds = quietNote(QStringLiteral(
            "Adds “Open in Omagit” to Nautilus’s right-click menu for folders and files inside a repository."));
        const QString usage = quietNote(QStringLiteral(
            "Right-click a folder or file inside a repository in Nautilus to open it here."));

        SettingsDialog dialog;
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        settle();
        auto *box = dialog.findChild<QCheckBox *>(QStringLiteral("nautilusMenuBox"));
        QVERIFY(box);
        QVERIFY(box->isEnabled());
        QVERIFY(!box->isChecked());
        QLabel *note = nautilusNote(dialog);
        QVERIFY(note);
        QVERIFY(note->styleSheet().isEmpty());
        QCOMPARE(note->text(), adds);
        // The design's small regular dim text, 11 px at base 12.
        QCOMPARE(note->objectName(), QStringLiteral("settingsNote"));
        QCOMPARE(note->font().pixelSize(), qRound(OmarchyTheme::instance()->fontBase() * 11 / 12.0));
        QVERIFY(!note->font().bold());
        QCOMPARE(note->palette().color(QPalette::WindowText).rgba(), OmarchyTheme::instance()->mutedText().rgba());

        box->click();
        QVERIFY(box->isChecked());
        QFile installed(path), embedded(QStringLiteral(":/nautilus/omagit.py"));
        QVERIFY(installed.open(QIODevice::ReadOnly));
        QVERIFY(embedded.open(QIODevice::ReadOnly));
        const QByteArray script = embedded.readAll();
        QVERIFY(script.contains("class OpenInOmagitAction"));
        QCOMPARE(installed.readAll(), script);
        installed.close();
        QVERIFY(nautilusmenu::isCurrent());
        if (noNautilus)
            QCOMPARE(note->text(), usage);

        box->click();
        QVERIFY(!box->isChecked());
        QVERIFY(!QFileInfo::exists(path));
        if (noNautilus)
            QCOMPARE(note->text(), adds);

        // A file where the folder should go: nothing is written.
        QVERIFY(QDir(scope.dataPath() + QStringLiteral("/nautilus-python")).removeRecursively());
        QFile blocker(scope.dataPath() + QStringLiteral("/nautilus-python"));
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        box->click();
        QVERIFY(!box->isChecked());
        QCOMPARE(note->text(), QStringLiteral("Could not create %1").arg(folder));
        QVERIFY(note->styleSheet().contains(OmarchyTheme::instance()->color(QStringLiteral("red")).name()));
        // Red over the note's own rule, which still sets its size.
        QCOMPARE(note->palette().color(QPalette::WindowText).rgba(),
                 OmarchyTheme::instance()->color(QStringLiteral("red")).rgba());
        QCOMPARE(note->font().pixelSize(), qRound(OmarchyTheme::instance()->fontBase() * 11 / 12.0));
        // With the way clear it works again, and the note is dim once more.
        QVERIFY(blocker.remove());
        box->click();
        QVERIFY(box->isChecked());
        QVERIFY(QFileInfo::exists(path));
        QVERIFY(note->styleSheet().isEmpty());
        QCOMPARE(note->palette().color(QPalette::WindowText).rgba(), OmarchyTheme::instance()->mutedText().rgba());

        // A folder where the file should go can be neither written over nor
        // removed, and the reason says which path.
        QVERIFY(QFile::remove(path));
        QVERIFY(QDir().mkdir(path));
        QString error;
        QVERIFY(!nautilusmenu::install(&error));
        QVERIFY2(error.startsWith(QStringLiteral("Could not write %1: ").arg(path)), qPrintable(error));
        QVERIFY(!nautilusmenu::remove(&error));
        QVERIFY2(error.startsWith(QStringLiteral("Could not remove %1: ").arg(path)), qPrintable(error));
        box->click();
        QVERIFY(box->isChecked()); // it is still there
        QVERIFY2(note->text().startsWith(QStringLiteral("Could not remove %1: ").arg(path)), qPrintable(note->text()));
        QVERIFY(note->styleSheet().contains(OmarchyTheme::instance()->color(QStringLiteral("red")).name()));
    }

    // Without Nautilus there is nothing to add the entry to, so the box is
    // off; a copy already there can still be taken away.
    void theSettingsNeedNautilusToAddTheMenu()
    {
        NautilusScope scope(NautilusScope::NoTools);
        QVERIFY(scope.isValid());
        QVERIFY(!nautilusmenu::nautilusFound());
        const QString missing = QStringLiteral("Nautilus is not installed.");
        {
            SettingsDialog dialog;
            dialog.show();
            QVERIFY(QTest::qWaitForWindowExposed(&dialog));
            settle();
            auto *box = dialog.findChild<QCheckBox *>(QStringLiteral("nautilusMenuBox"));
            QVERIFY(box);
            QVERIFY(!box->isEnabled());
            QVERIFY(!box->isChecked());
            QCOMPARE(nautilusNote(dialog)->text(), missing);
        }

        QVERIFY(nautilusmenu::install(nullptr));
        SettingsDialog dialog;
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        settle();
        auto *box = dialog.findChild<QCheckBox *>(QStringLiteral("nautilusMenuBox"));
        QVERIFY(box);
        QVERIFY(box->isEnabled());
        QVERIFY(box->isChecked());
        QCOMPARE(nautilusNote(dialog)->text(), missing);
        box->click();
        QVERIFY(!box->isChecked());
        QVERIFY(!nautilusmenu::isInstalled());
        QVERIFY(!box->isEnabled());
        QCOMPARE(nautilusNote(dialog)->text(), missing);
    }

    // The entry starts an installed omagit: with none where the extension
    // looks, the note says how to get one — and the box still works. One in
    // ~/.local/bin is found, as the extension finds it.
    void theSettingsSayWhenNoOmagitIsInstalled()
    {
        NautilusScope scope;
        QVERIFY(scope.isValid());
        if (!nautilusmenu::pythonSupportFound())
            QSKIP("nautilus-python is not installed: the hint to add it comes first");
        if (systemOmagit())
            QSKIP("An omagit is installed in /usr: the extension finds it there");
        QVERIFY(nautilusmenu::nautilusFound());
        QVERIFY(!nautilusmenu::omagitFound());
        const QString noOmagit = QStringLiteral(
            "The entry starts an installed omagit, and none is on PATH or in ~/.local/bin — run ./install.sh.");
        {
            SettingsDialog dialog;
            dialog.show();
            QVERIFY(QTest::qWaitForWindowExposed(&dialog));
            settle();
            auto *box = dialog.findChild<QCheckBox *>(QStringLiteral("nautilusMenuBox"));
            QVERIFY(box);
            QVERIFY(box->isEnabled());
            QCOMPARE(nautilusNote(dialog)->text(), noOmagit);
            QVERIFY(nautilusNote(dialog)->styleSheet().isEmpty());
            box->click();
            QVERIFY(box->isChecked());
            QVERIFY(nautilusmenu::isInstalled());
            if (!nautilusmenu::nautilusRunning())
                QCOMPARE(nautilusNote(dialog)->text(), noOmagit);
            box->click();
            QVERIFY(!box->isChecked());
            QVERIFY(!nautilusmenu::isInstalled());
        }

        const QString localBin = scope.homePath() + QStringLiteral("/.local/bin");
        QVERIFY(QDir().mkpath(localBin));
        QVERIFY(writeFixture(localBin + QStringLiteral("/omagit"), "#!/bin/sh\nexit 0\n", true));
        QVERIFY(nautilusmenu::omagitFound());
        SettingsDialog dialog;
        QCOMPARE(nautilusNote(dialog)->text(),
                 QStringLiteral("Adds “Open in Omagit” to Nautilus’s right-click menu for folders and files inside a repository."));
    }

    // A copy an older build wrote is replaced with this build's, by the
    // start-up call and by the settings; a current one and none at all are
    // left as they are.
    void theNautilusMenuRefreshesAnOutdatedCopy()
    {
        NautilusScope scope;
        QVERIFY(scope.isValid());
        const QString path = nautilusmenu::extensionPath();
        QFile embedded(QStringLiteral(":/nautilus/omagit.py"));
        QVERIFY(embedded.open(QIODevice::ReadOnly));
        const QByteArray script = embedded.readAll();
        const auto contents = [&path] {
            QFile file(path);
            return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
        };

        QString error;
        QVERIFY(nautilusmenu::refreshIfInstalled(&error));
        QVERIFY(!nautilusmenu::isInstalled());
        QVERIFY(!nautilusmenu::isCurrent());

        const QByteArray older = "# the extension of an older build\n";
        QVERIFY(QDir().mkpath(QFileInfo(path).path()));
        QVERIFY(writeFixture(path, older));
        QVERIFY(nautilusmenu::isInstalled());
        QVERIFY(!nautilusmenu::isCurrent());
        QVERIFY(nautilusmenu::refreshIfInstalled(&error));
        QVERIFY(nautilusmenu::isCurrent());
        QCOMPARE(contents(), script);

        // An hour old and current: not written again.
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadWrite));
            QVERIFY(file.setFileTime(QDateTime::currentDateTime().addSecs(-3600), QFileDevice::FileModificationTime));
        }
        const QDateTime written = QFileInfo(path).lastModified();
        QVERIFY(nautilusmenu::refreshIfInstalled(&error));
        QCOMPARE(QFileInfo(path).lastModified(), written);
        QCOMPARE(contents(), script);

        // The settings bring an older copy up to date as they open.
        QVERIFY(writeFixture(path, older));
        SettingsDialog dialog;
        QVERIFY(nautilusmenu::isCurrent());
        QVERIFY(nautilusNote(dialog)->styleSheet().isEmpty());
    }

    // Ticked while Nautilus runs, the box offers the restart that loads the
    // extension; the restart quits the running one, starts a new one once it
    // is gone, and the offer goes away. The quit's exit 255 is no failure.
    void theSettingsRestartNautilus()
    {
        NautilusScope scope(NautilusScope::FakeNautilus | NautilusScope::FakeOmagit);
        QVERIFY(scope.isValid());
        if (nautilusmenu::nautilusRunning())
            QSKIP("A Nautilus of this user is running: the restart would quit it");
        QVERIFY(scope.serve());
        const qint64 served = scope.pid(QStringLiteral("serve.pid"));

        SettingsDialog dialog;
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        settle();
        auto *box = dialog.findChild<QCheckBox *>(QStringLiteral("nautilusMenuBox"));
        auto *restart = dialog.findChild<QPushButton *>(QStringLiteral("restartNautilusButton"));
        QVERIFY(box && restart);
        QLabel *note = nautilusNote(dialog);
        QVERIFY(!restart->isVisible());

        box->click();
        QVERIFY(box->isChecked());
        QVERIFY(restart->isVisible());
        QVERIFY(restart->isEnabled());
        QCOMPARE(note->text(), quietNote(QStringLiteral("Nautilus picks it up once it restarts.")));

        restart->click();
        QCOMPARE(note->text(), QStringLiteral("Restarting Nautilus…"));
        QVERIFY(!restart->isEnabled());
        QTRY_VERIFY(scope.log().contains(QStringLiteral("--new-window")));
        QVERIFY(!NautilusScope::alive(served)); // gone before the new one started
        QTRY_VERIFY(!restart->isVisible());
        QTRY_COMPARE(note->text(),
                     quietNote(QStringLiteral("Right-click a folder or file inside a repository in Nautilus to open it here.")));
        QVERIFY(note->styleSheet().isEmpty());
    }

    // A restart that failed says why, in red, and offers itself again; the
    // next try drops the old reason as it starts rather than leave it up
    // while that one runs.
    void theSettingsForgetARestartErrorOnTheNextTry()
    {
        NautilusScope scope(NautilusScope::FakeNautilus);
        QVERIFY(scope.isValid());
        if (nautilusmenu::nautilusRunning())
            QSKIP("A Nautilus of this user is running: the restart would quit it");
        QVERIFY(scope.serve());
        // Fails at once, where a quit that quits nothing waits out the deadline.
        QVERIFY(scope.setQuit(NautilusScope::Quit::Unstartable));

        SettingsDialog dialog;
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        settle();
        auto *box = dialog.findChild<QCheckBox *>(QStringLiteral("nautilusMenuBox"));
        auto *restart = dialog.findChild<QPushButton *>(QStringLiteral("restartNautilusButton"));
        QVERIFY(box && restart);
        QLabel *note = nautilusNote(dialog);
        const QString red = OmarchyTheme::instance()->color(QStringLiteral("red")).name();

        box->click();
        QVERIFY(box->isChecked());
        QVERIFY(restart->isVisible());
        restart->click();
        QTRY_VERIFY(restart->isEnabled());
        const QString error = note->text();
        QVERIFY2(!error.isEmpty() && error != QStringLiteral("Restarting Nautilus…"), qPrintable(error));
        QVERIFY(note->styleSheet().contains(red));
        QVERIFY(restart->isVisible());

        // Read before the event loop runs again: the quit cannot have failed yet.
        restart->click();
        QCOMPARE(note->text(), QStringLiteral("Restarting Nautilus…"));
        QVERIFY(note->styleSheet().isEmpty());
        QVERIFY(!restart->isEnabled());
        // It fails the same way.
        QTRY_VERIFY(restart->isEnabled());
        QCOMPARE(note->text(), error);
        QVERIFY(note->styleSheet().contains(red));
    }

    // A Nautilus started before the extension was written has not loaded it:
    // settings opened later still offer the restart. One started after it has.
    void theSettingsOfferARestartToAnOlderNautilus()
    {
        NautilusScope scope(NautilusScope::FakeNautilus | NautilusScope::FakeOmagit);
        QVERIFY(scope.isValid());
        if (nautilusmenu::nautilusRunning())
            QSKIP("A Nautilus of this user is running: it would count as the older one");
        QVERIFY(scope.serve());
        QVERIFY(!nautilusmenu::restartNeeded()); // nothing to load yet
        QTest::qWait(100);
        const qint64 age = nautilusmenu::nautilusAgeMs();
        QVERIFY2(age >= 90 && age < 10000, qPrintable(QString::number(age)));
        QVERIFY(nautilusmenu::install(nullptr));
        QVERIFY(nautilusmenu::restartNeeded());
        {
            SettingsDialog dialog;
            dialog.show();
            QVERIFY(QTest::qWaitForWindowExposed(&dialog));
            settle();
            auto *restart = dialog.findChild<QPushButton *>(QStringLiteral("restartNautilusButton"));
            QVERIFY(restart);
            QVERIFY(restart->isVisible());
            QCOMPARE(nautilusNote(dialog)->text(), quietNote(QStringLiteral("Nautilus picks it up once it restarts.")));
        }

        // Written an hour before this Nautilus started: loaded already.
        {
            QFile file(nautilusmenu::extensionPath());
            QVERIFY(file.open(QIODevice::ReadWrite));
            QVERIFY(file.setFileTime(QDateTime::currentDateTime().addSecs(-3600), QFileDevice::FileModificationTime));
        }
        QVERIFY(!nautilusmenu::restartNeeded());
        SettingsDialog dialog;
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        settle();
        QVERIFY(!dialog.findChild<QPushButton *>(QStringLiteral("restartNautilusButton"))->isVisible());
        QCOMPARE(nautilusNote(dialog)->text(),
                 quietNote(QStringLiteral("Right-click a folder or file inside a repository in Nautilus to open it here.")));
    }

    // A restart that cannot finish says why and starts nothing: a Nautilus
    // that does not quit before the deadline, whether its quit hangs (and is
    // killed) or exits without quitting it. One whose asker went away still
    // finishes, without a word.
    void theNautilusRestartStopsOnFailure()
    {
        NautilusScope scope(NautilusScope::FakeNautilus, NautilusScope::Quit::Hang);
        QVERIFY(scope.isValid());
        if (nautilusmenu::nautilusRunning())
            QSKIP("A Nautilus of this user is running: the restart would wait for it");
        QString error;
        bool called = false;
        QObject context;
        const auto done = [&error, &called](const QString &e) {
            error = e;
            called = true;
        };

        QElapsedTimer took;
        took.start();
        nautilusmenu::restart(&context, done, 500);
        QTRY_VERIFY(called);
        QVERIFY(took.elapsed() >= 450);
        QVERIFY2(error.contains(QStringLiteral("did not quit")), qPrintable(error));
        const qint64 quitting = scope.pid(QStringLiteral("quit.pid"));
        QVERIFY(quitting > 0);
        QVERIFY(!QFileInfo::exists(QStringLiteral("/proc/%1").arg(quitting)));
        settle();
        QVERIFY(!scope.log().contains(QStringLiteral("--new-window")));

        QVERIFY(scope.serve());
        QVERIFY(scope.setQuit(NautilusScope::Quit::Fail));
        called = false;
        error.clear();
        took.restart();
        nautilusmenu::restart(&context, done, 500);
        QTRY_VERIFY(called);
        QVERIFY(took.elapsed() >= 450);
        QVERIFY2(error.contains(QStringLiteral("did not quit")), qPrintable(error));
        settle();
        QVERIFY(!scope.log().contains(QStringLiteral("--new-window")));

        // The one still serving is the one this quit takes away.
        QVERIFY(scope.setQuit(NautilusScope::Quit::Kill));
        called = false;
        auto *gone = new QObject;
        nautilusmenu::restart(gone, done, 2000);
        delete gone;
        QTRY_VERIFY(scope.log().contains(QStringLiteral("--new-window")));
        settle();
        QVERIFY(!called);
    }
};

UI_TEST(SettingsTest);

#include "settings_test.moc"
