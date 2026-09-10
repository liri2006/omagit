#include "DesktopExec.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QFile>
#include <QHash>
#include <QMimeDatabase>
#include <QStandardPaths>

namespace {
// xdg-mime is a shell script that reads several config files; long enough
// for a cold cache, short enough not to hold the window up.
constexpr int kMimeQueryTimeoutMs = 1500;

// One "Key = Value" line of a desktop entry, both sides trimmed. The key is
// empty for a line that carries none (a comment, a blank line). Keys with a
// locale suffix ("Name[de]") stay whole and so match nothing we ask for.
struct EntryLine {
    QString key, value;
};

EntryLine entryLine(const QString &line)
{
    const qsizetype equals = line.indexOf(QLatin1Char('='));
    if (equals < 0)
        return EntryLine();
    return EntryLine{line.left(equals).trimmed(), line.mid(equals + 1).trimmed()};
}

// The [Desktop Entry] group of `file`; the groups after it describe actions
// ("Open in a new window") that are none of our business.
DefaultApp readDesktopEntry(const QString &file)
{
    DefaultApp app;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return app;
    app.desktopFile = file;
    bool inEntry = false;
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            if (inEntry)
                break;
            inEntry = line == QLatin1String("[Desktop Entry]");
            continue;
        }
        if (!inEntry)
            continue;
        const EntryLine entry = entryLine(line);
        if (entry.key == QLatin1String("Name"))
            app.name = entry.value;
        else if (entry.key == QLatin1String("Icon"))
            app.icon = entry.value;
        else if (entry.key == QLatin1String("Exec"))
            app.exec = entry.value;
        else if (entry.key == QLatin1String("Path"))
            app.workingDirectory = entry.value;
        else if (entry.key == QLatin1String("Terminal"))
            app.terminal = entry.value.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
    }
    return app;
}
} // namespace

DefaultApp defaultAppFor(const QString &filePath)
{
    static QHash<QString, DefaultApp> cache; // by MIME type; the query runs a shell script
    const QString mime = QMimeDatabase().mimeTypeForFile(filePath).name();
    const auto cached = cache.constFind(mime);
    if (cached != cache.constEnd())
        return *cached;
    QProcess query;
    query.start(QStringLiteral("xdg-mime"), {QStringLiteral("query"), QStringLiteral("default"), mime});
    // A failed query is remembered as "no application" too: the menu asks on
    // every right-click, and a script that hangs would hang it every time.
    if (!query.waitForFinished(kMimeQueryTimeoutMs) || query.exitCode() != 0) {
        cache.insert(mime, DefaultApp());
        return DefaultApp();
    }
    const QString desktopId = QString::fromUtf8(query.readAllStandardOutput()).trimmed();
    const QString file = desktopId.isEmpty() ? QString()
                                             : QStandardPaths::locate(QStandardPaths::ApplicationsLocation, desktopId);
    DefaultApp app = file.isEmpty() ? DefaultApp() : readDesktopEntry(file);
    if (app.name.isEmpty() && !desktopId.isEmpty()) // no desktop file found: show the id
        app.name = desktopId.endsWith(QLatin1String(".desktop")) ? desktopId.chopped(8) : desktopId;
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
            if (error)
                *error = QCoreApplication::translate("DesktopExec", "The application command for %1 is empty.").arg(app.name);
            return false;
        }
        QString program = args.takeFirst();
        if (app.terminal) {
            if (QStandardPaths::findExecutable(QStringLiteral("xdg-terminal-exec")).isEmpty()) {
                if (error)
                    *error = QCoreApplication::translate("DesktopExec", "Opening %1 requires xdg-terminal-exec.").arg(app.name);
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
            if (error)
                *error = process.errorString();
            return false;
        }
    } else if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
        if (error)
            *error = QCoreApplication::translate("DesktopExec", "Could not open %1 with its default application.")
                             .arg(shownPath.isEmpty() ? path : shownPath);
        return false;
    }
    return true;
}
