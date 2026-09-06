#include "GitRepo.h"
#include "MainWindow.h"
#include "OmarchyTheme.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QMessageBox>
#include <QSettings>
#include <QTimer>

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
    QCommandLineOption fullOpt(QStringLiteral("full"), QStringLiteral("Start with the left section filling the window (no diff pane)."));
    QCommandLineOption amendOpt(QStringLiteral("amend"), QStringLiteral("Open the commit dialog with \"Amend last commit\" ticked."));
    parser.addOption(screenshotOpt);
    parser.addOption(selectOpt);
    parser.addOption(historyOpt);
    parser.addOption(fullOpt);
    parser.addOption(amendOpt);
    parser.process(app);

    const QStringList args = parser.positionalArguments();
    const QString start = args.isEmpty() ? QDir::currentPath() : args.first();

    OmarchyTheme theme;
    theme.apply(app);

    QString error;
    const QString root = GitRepo::findRoot(start, &error);
    if (root.isEmpty()) {
        QMessageBox::critical(nullptr, QStringLiteral("OmaGit"),
                              QStringLiteral("%1 is not inside a git repository.\n\n%2").arg(start, error));
        return 1;
    }

    GitRepo repo(root);
    MainWindow window(&repo);
    QObject::connect(&app, &QApplication::aboutToQuit, [&window] {
        QSettings().setValue(QStringLiteral("window/geometry"), window.saveGeometry());
    });
    if (parser.isSet(selectOpt))
        window.setInitialSelection(parser.value(selectOpt));
    if (parser.isSet(fullOpt))
        window.setLeftFull(true, false);
    if (parser.isSet(historyOpt))
        window.setMode(MainWindow::HistoryMode);
    window.show();
    if (parser.isSet(amendOpt))
        QTimer::singleShot(0, &window, [&window] { window.setAmend(true); });
    if (parser.isSet(screenshotOpt)) {
        const QString file = parser.value(screenshotOpt);
        QTimer::singleShot(800, &window, [&window, file] {
            window.grab().save(file);
            QCoreApplication::exit(0);
        });
    }
    return app.exec();
}
