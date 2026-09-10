#include "GitRepo.h"
#include "MainWindow.h"
#include "OmarchyTheme.h"
#include "Settings.h"

#include <QAbstractScrollArea>
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QMessageBox>
#include <QKeySequence>
#include <qpa/qwindowsysteminterface.h>
#include <QPainter>
#include <QSettings>
#include <QScrollBar>
#include <QTimer>
#include <QWindow>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("omagit"));
    QCoreApplication::setApplicationName(QStringLiteral("omagit"));
    QCoreApplication::setApplicationVersion(QStringLiteral(OMAGIT_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral("omagit"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Pending changes, history and diff viewer for git, themed by Omarchy."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("path"), QStringLiteral("Directory or file inside a git repository (default: cwd)."));
    QCommandLineOption screenshotOpt(QStringLiteral("screenshot"), QStringLiteral("Render the window to <file> and exit (for testing)."), QStringLiteral("file"));
    QCommandLineOption selectOpt(QStringLiteral("select"), QStringLiteral("Pre-select the given repo-relative path."), QStringLiteral("path"));
    QCommandLineOption historyOpt(QStringLiteral("history"), QStringLiteral("Open the history view instead of the commit dialog."));
    QCommandLineOption fullOpt(QStringLiteral("full"), QStringLiteral("Start with the diff pane hidden: the left section fills the window."));
    QCommandLineOption miniOpt(QStringLiteral("mini"), QStringLiteral("Start in the Mini layout: a rail of file miniatures next to the diff pane."));
    QCommandLineOption amendOpt(QStringLiteral("amend"), QStringLiteral("Open the commit dialog with \"Amend last commit\" ticked."));
    QCommandLineOption screenshotAfterOpt(QStringLiteral("screenshot-after"), QStringLiteral("Milliseconds to wait before taking the --screenshot (default 800)."), QStringLiteral("ms"), QStringLiteral("800"));
    QCommandLineOption noFetchOpt(QStringLiteral("no-fetch"), QStringLiteral("Do not fetch by itself to keep the Pull count current."));
    QCommandLineOption screenshotMenuOpt(QStringLiteral("screenshot-menu"), QStringLiteral("Open the branch, repo, agent, keybindings or merge panel before taking the --screenshot (for testing)."), QStringLiteral("branch|repo|agent|keybindings|merge"));
    parser.addOption(screenshotOpt);
    parser.addOption(screenshotAfterOpt);
    parser.addOption(selectOpt);
    parser.addOption(historyOpt);
    parser.addOption(fullOpt);
    parser.addOption(miniOpt);
    parser.addOption(amendOpt);
    parser.addOption(noFetchOpt);
    parser.addOption(screenshotMenuOpt);
    QCommandLineOption screenshotKeysOpt(QStringLiteral("screenshot-keys"), QStringLiteral("Comma-separated keys (m,a,Down,Return) sent to the focused widget once the --screenshot-menu dropdown is open, or to the window; @objectName[:vbar] or @ClassName[:vbar] focuses that (first visible) widget or its vertical scrollbar first (for testing)."), QStringLiteral("keys"));
    parser.addOption(screenshotKeysOpt);
    parser.process(app);

    const QStringList args = parser.positionalArguments();
    const QString start = args.isEmpty() ? QDir::currentPath() : args.first();

    OmarchyTheme theme;
    theme.apply(app);

    QString error;
    QString root = GitRepo::findRoot(start, &error);
    // Started from somewhere outside a repository (no path given): show the
    // repository opened last instead, the way an editor reopens its files.
    if (root.isEmpty() && args.isEmpty()) {
        const QStringList recent = MainWindow::recentRepositories();
        if (!recent.isEmpty())
            root = recent.first();
    }
    if (root.isEmpty()) {
        QMessageBox::critical(nullptr, QStringLiteral("Omagit"),
                              QStringLiteral("%1 is not inside a git repository.\n\n%2").arg(start, error));
        return 1;
    }

    GitRepo repo(root);
    MainWindow window(&repo);
    // A --screenshot run is a test: it must not leave its offscreen geometry behind.
    if (!parser.isSet(screenshotOpt)) {
        QObject::connect(&app, &QApplication::aboutToQuit, [&window] {
            QSettings().setValue(settings::kWindowGeometry, window.saveGeometry());
        });
    }
    if (parser.isSet(selectOpt))
        window.setInitialSelection(parser.value(selectOpt));
    if (parser.isSet(miniOpt))
        window.setPaneLayout(PaneLayout::Mini, false);
    if (parser.isSet(fullOpt))
        window.setDiffPaneVisible(false, false);
    if (parser.isSet(historyOpt))
        window.setMode(MainWindow::HistoryMode);
    if (parser.isSet(noFetchOpt))
        window.setAutoFetchEnabled(false);
    window.show();
    if (parser.isSet(amendOpt))
        QTimer::singleShot(0, &window, [&window] { window.setAmend(true); });
    if (parser.isSet(screenshotOpt)) {
        const QString file = parser.value(screenshotOpt);
        bool validAfter = false;
        const int given = parser.value(screenshotAfterOpt).toInt(&validAfter);
        // A misspelt --screenshot-after would otherwise grab at once.
        const int after = validAfter ? given : screenshotAfterOpt.defaultValues().first().toInt();
        const QString menu = parser.value(screenshotMenuOpt);
        if (!menu.isEmpty()) {
            // The dropdown runs its own event loop; the grab below happens inside it.
            QTimer::singleShot(after, &window, [&window, menu] {
                const char *slot = menu == QLatin1String("repo") ? "showRepoMenu"
                    : menu == QLatin1String("keybindings")       ? "showKeybindings"
                    : menu == QLatin1String("agent")             ? "showAgentMenu"
                    : menu == QLatin1String("merge")             ? "showMergeDialog"
                                                                 : "showBranchMenu";
                QMetaObject::invokeMethod(&window, slot);
            });
        }
        const QStringList keys = parser.value(screenshotKeysOpt).split(QLatin1Char(','), Qt::SkipEmptyParts);
        // The merge view works out its verdict first; keys and the grab wait for it.
        const int settle = menu == QLatin1String("merge") ? 1200 : 0;
        if (!keys.isEmpty()) {
            // Inside a dropdown the keys go to its focused field; otherwise
            // to the window itself, where the shortcuts (Ctrl+G, …) live,
            // early enough for a long --screenshot-after to show their effect.
            QTimer::singleShot(menu.isEmpty() ? qMin(after, 800) : after + 200 + settle, &window, [&window, keys] {
                if (!window.windowHandle())
                    return;
                for (const QString &name : keys) {
                    if (name.startsWith(QLatin1Char('@'))) {
                        // Deliver the keys so far, then move the focus for the rest.
                        QWindowSystemInterface::flushWindowSystemEvents();
                        QCoreApplication::sendPostedEvents();
                        const QString wanted = name.mid(1).section(QLatin1Char(':'), 0, 0);
                        const bool vbar = name.endsWith(QLatin1String(":vbar"));
                        for (QWidget *w : window.findChildren<QWidget *>()) {
                            if (!w->isVisible() || (w->objectName() != wanted && QLatin1String(w->metaObject()->className()) != wanted))
                                continue;
                            auto *area = qobject_cast<QAbstractScrollArea *>(w);
                            (vbar && area ? static_cast<QWidget *>(area->verticalScrollBar()) : w)->setFocus();
                            break;
                        }
                        continue;
                    }
                    const QKeyCombination combo = QKeySequence::fromString(name)[0];
                    const QString text = name.size() == 1 ? name : QString();
                    // As the window system would report the key, so that the
                    // shortcuts see it first; a QKeyEvent posted by hand goes
                    // straight to the focused widget and never reaches them.
                    QWindow *win = QGuiApplication::focusWindow() ? QGuiApplication::focusWindow() : window.windowHandle();
                    QWindowSystemInterface::handleKeyEvent(win, QEvent::KeyPress, combo.key(), combo.keyboardModifiers(), text);
                    QWindowSystemInterface::handleKeyEvent(win, QEvent::KeyRelease, combo.key(), combo.keyboardModifiers(), text);
                }
                // Before the grab, which may be due in this same event loop turn.
                QWindowSystemInterface::flushWindowSystemEvents();
            });
        }
        QTimer::singleShot(after + (menu.isEmpty() ? 0 : 500 + settle), &window, [&window, file] {
            QPixmap shot = window.grab();
            // A dialog (the merge view) and a dropdown are windows of their own: paint them on top.
            QPainter p(&shot);
            for (QWidget *w : {QApplication::activeModalWidget(), QApplication::activePopupWidget()})
                if (w && w != &window)
                    p.drawPixmap(w->mapToGlobal(QPoint(0, 0)) - window.mapToGlobal(QPoint(0, 0)), w->grab());
            shot.save(file);
            QCoreApplication::exit(0);
        });
    }
    return app.exec();
}
