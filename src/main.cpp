#include "GitRepo.h"
#include "MainWindow.h"
#include "OmarchyTheme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QMessageBox>
#include <QKeyEvent>
#include <QKeySequence>
#include <QPainter>
#include <QSettings>
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
    QCommandLineOption screenshotMenuOpt(QStringLiteral("screenshot-menu"), QStringLiteral("Open the branch, repo or agent dropdown before taking the --screenshot (for testing)."), QStringLiteral("branch|repo|agent"));
    parser.addOption(screenshotOpt);
    parser.addOption(screenshotAfterOpt);
    parser.addOption(selectOpt);
    parser.addOption(historyOpt);
    parser.addOption(fullOpt);
    parser.addOption(miniOpt);
    parser.addOption(amendOpt);
    parser.addOption(noFetchOpt);
    parser.addOption(screenshotMenuOpt);
    QCommandLineOption screenshotKeysOpt(QStringLiteral("screenshot-keys"), QStringLiteral("Comma-separated keys (m,a,Down,Return) sent to the focused widget once the --screenshot-menu dropdown is open, or to the window (for testing)."), QStringLiteral("keys"));
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
        QMessageBox::critical(nullptr, QStringLiteral("OmaGit"),
                              QStringLiteral("%1 is not inside a git repository.\n\n%2").arg(start, error));
        return 1;
    }

    GitRepo repo(root);
    MainWindow window(&repo);
    // A --screenshot run is a test: it must not leave its offscreen geometry behind.
    if (!parser.isSet(screenshotOpt)) {
        QObject::connect(&app, &QApplication::aboutToQuit, [&window] {
            QSettings().setValue(QStringLiteral("window/geometry"), window.saveGeometry());
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
        const int after = parser.value(screenshotAfterOpt).toInt();
        const QString menu = parser.value(screenshotMenuOpt);
        if (!menu.isEmpty()) {
            // The dropdown runs its own event loop; the grab below happens inside it.
            QTimer::singleShot(after, &window, [&window, menu] {
                const char *slot = menu == QLatin1String("repo") ? "showRepoMenu"
                    : menu == QLatin1String("agent")             ? "showAgentMenu"
                                                                 : "showBranchMenu";
                QMetaObject::invokeMethod(&window, slot);
            });
        }
        const QStringList keys = parser.value(screenshotKeysOpt).split(QLatin1Char(','), Qt::SkipEmptyParts);
        if (!keys.isEmpty()) {
            // Inside a dropdown the keys go to its focused field; otherwise
            // to the window itself, where the shortcuts (Ctrl+G, …) live,
            // early enough for a long --screenshot-after to show their effect.
            QTimer::singleShot(menu.isEmpty() ? qMin(after, 800) : after + 200, &window, [&window, keys, menu] {
                QObject *target = menu.isEmpty() ? static_cast<QObject *>(window.windowHandle())
                                                 : static_cast<QObject *>(QApplication::focusWidget());
                if (!target)
                    return;
                for (const QString &name : keys) {
                    const QKeyCombination combo = QKeySequence::fromString(name)[0];
                    const QString text = name.size() == 1 ? name : QString();
                    QApplication::postEvent(target, new QKeyEvent(QEvent::KeyPress, combo.key(), combo.keyboardModifiers(), text));
                    QApplication::postEvent(target, new QKeyEvent(QEvent::KeyRelease, combo.key(), combo.keyboardModifiers(), text));
                }
            });
        }
        QTimer::singleShot(after + (menu.isEmpty() ? 0 : 500), &window, [&window, file] {
            QPixmap shot = window.grab();
            if (QWidget *popup = QApplication::activePopupWidget()) {
                QPainter p(&shot);
                p.drawPixmap(popup->mapToGlobal(QPoint(0, 0)) - window.mapToGlobal(QPoint(0, 0)), popup->grab());
            }
            shot.save(file);
            QCoreApplication::exit(0);
        });
    }
    return app.exec();
}
