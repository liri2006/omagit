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
    parser.setApplicationDescription(QStringLiteral("Pending changes and diff viewer for git, themed by Omarchy."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("path"), QStringLiteral("Directory or file inside a git repository (default: cwd)."));
    QCommandLineOption screenshotOpt(QStringLiteral("screenshot"), QStringLiteral("Render the window to <file> and exit (for testing)."), QStringLiteral("file"));
    QCommandLineOption selectOpt(QStringLiteral("select"), QStringLiteral("Pre-select the given repo-relative path."), QStringLiteral("path"));
    parser.addOption(screenshotOpt);
    parser.addOption(selectOpt);
    parser.process(app);

    const QStringList args = parser.positionalArguments();
    const QString start = args.isEmpty() ? QDir::currentPath() : args.first();

    OmarchyTheme theme;
    theme.apply(app);

    QString error;
    const QString root = GitRepo::findRoot(start, &error);
    if (root.isEmpty()) {
        QMessageBox::critical(nullptr, QStringLiteral("Omagit"),
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
    window.show();
    if (parser.isSet(screenshotOpt)) {
        const QString file = parser.value(screenshotOpt);
        QTimer::singleShot(800, &window, [&window, file] {
            window.grab().save(file);
            QCoreApplication::exit(0);
        });
    }
    return app.exec();
}
