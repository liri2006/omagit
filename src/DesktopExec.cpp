#include "DesktopExec.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QFile>
#include <QHash>
#include <QMimeDatabase>
#include <QStandardPaths>

DefaultApp defaultAppFor(const QString &filePath)
{
    static QHash<QString, DefaultApp> cache; // by MIME type; the query runs a shell script
    const QString mime = QMimeDatabase().mimeTypeForFile(filePath).name();
    const auto cached = cache.constFind(mime);
    if (cached != cache.constEnd())
        return *cached;
    DefaultApp app;
    QProcess query;
    query.start(QStringLiteral("xdg-mime"), {QStringLiteral("query"), QStringLiteral("default"), mime});
    if (query.waitForFinished(1500) && query.exitCode() == 0) {
        const QString desktopId = QString::fromUtf8(query.readAllStandardOutput()).trimmed();
        const QString file = desktopId.isEmpty() ? QString()
                                                 : QStandardPaths::locate(QStandardPaths::ApplicationsLocation, desktopId);
        QFile f(file);
        if (!file.isEmpty() && f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            app.desktopFile = file;
            bool inEntry = false;
            while (!f.atEnd()) {
                const QString line = QString::fromUtf8(f.readLine()).trimmed();
                if (line.startsWith(QLatin1Char('['))) {
                    inEntry = line == QLatin1String("[Desktop Entry]");
                    continue;
                }
                if (!inEntry)
                    continue;
                if (line.startsWith(QLatin1String("Name=")))
                    app.name = line.mid(5);
                else if (line.startsWith(QLatin1String("Icon=")))
                    app.icon = line.mid(5);
                else if (line.startsWith(QLatin1String("Exec=")))
                    app.exec = line.mid(5);
                else if (line.startsWith(QLatin1String("Path=")))
                    app.workingDirectory = line.mid(5);
                else if (line == QLatin1String("Terminal=true"))
                    app.terminal = true;
            }
        }
        if (app.name.isEmpty() && !desktopId.isEmpty()) // no desktop file found: show the id
            app.name = desktopId.endsWith(QLatin1String(".desktop")) ? desktopId.chopped(8) : desktopId;
    }
    cache.insert(mime, app);
    return app;
}

bool openWithDefaultApp(const QString &path, const QString &fallbackWorkingDirectory, QString *error,
                        const QString &shownPath)
{
    const DefaultApp app = defaultAppFor(path);
    if (!app.exec.isEmpty()) {
        QStringList args = desktopExecArguments(app.exec, path, app.name, app.icon, app.desktopFile);
        if (args.isEmpty()) {
            *error = QCoreApplication::translate("MainWindow", "The application command for %1 is empty.").arg(app.name);
            return false;
        }
        QString program = args.takeFirst();
        if (app.terminal) {
            if (QStandardPaths::findExecutable(QStringLiteral("xdg-terminal-exec")).isEmpty()) {
                *error = QCoreApplication::translate("MainWindow", "Opening %1 requires xdg-terminal-exec.").arg(app.name);
                return false;
            }
            args.prepend(program);
            args.prepend(QStringLiteral("--"));
            program = QStringLiteral("xdg-terminal-exec");
        }
        // Keep the editor alive when Omagit closes.
        QProcess process;
        process.setWorkingDirectory(app.workingDirectory.isEmpty() ? fallbackWorkingDirectory : app.workingDirectory);
        process.setProgram(program);
        process.setArguments(args);
        if (!process.startDetached()) {
            *error = process.errorString();
            return false;
        }
    } else if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
        *error = QCoreApplication::translate("MainWindow", "Could not open %1 with its default application.")
                     .arg(shownPath.isEmpty() ? path : shownPath);
        return false;
    }
    return true;
}
