#pragma once

#include <QProcess>
#include <QStringList>
#include <QUrl>

// Expand desktop-entry field codes into arguments, never into a shell command.
inline QStringList desktopExecArguments(const QString &exec, const QString &path,
                                        const QString &name, const QString &icon,
                                        const QString &desktopFile)
{
    QStringList result;
    for (QString arg : QProcess::splitCommand(exec)) {
        if (arg == QLatin1String("%f") || arg == QLatin1String("%F"))
            result << path;
        else if (arg == QLatin1String("%u") || arg == QLatin1String("%U"))
            result << QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
        else if (arg == QLatin1String("%c"))
            result << name;
        else if (arg == QLatin1String("%k"))
            result << desktopFile;
        else if (arg == QLatin1String("%i")) {
            if (!icon.isEmpty())
                result << QStringLiteral("--icon") << icon;
        } else if (arg != QLatin1String("%d") && arg != QLatin1String("%D")
                   && arg != QLatin1String("%n") && arg != QLatin1String("%N")
                   && arg != QLatin1String("%v") && arg != QLatin1String("%m")) {
            arg.replace(QStringLiteral("%%"), QStringLiteral("%"));
            result << arg;
        }
    }
    return result;
}
