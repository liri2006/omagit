#include "NautilusMenu.h"
#include "ProcessUtil.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

#include <time.h>
#include <unistd.h>

namespace {
// The extension as the build embedded it (data/omagit.qrc).
const QLatin1String kResource(":/nautilus/omagit.py");
// nautilus-python's loader, where distributions put Nautilus 4's extensions.
const char *const kPythonLoaders[] = {
    "/usr/lib/nautilus/extensions-4/libnautilus-python.so",
    "/usr/lib64/nautilus/extensions-4/libnautilus-python.so",
    "/usr/lib/x86_64-linux-gnu/nautilus/extensions-4/libnautilus-python.so",
    "/usr/lib/aarch64-linux-gnu/nautilus/extensions-4/libnautilus-python.so",
};
// How often restart() looks whether the quitting Nautilus is gone.
constexpr int kQuitPollMs = 100;
// In /proc/<pid>/stat, counted from the state (field 3) after the command
// name: the start time, field 22.
constexpr int kStartTimeField = 19;

// One restart, from `nautilus -q` to the new instance. It belongs to the
// application rather than to whoever asked: a dialog closed half way must
// not leave Nautilus quit and never started again. One deadline covers the
// whole of it, the quit and the wait for the old instance to be gone.
class RestartJob : public QObject
{
public:
    RestartJob(QObject *context, std::function<void(const QString &)> done, int deadlineMs)
        : QObject(QCoreApplication::instance())
        , m_context(context)
        , m_done(std::move(done))
        , m_quit(new QProcess(this))
        , m_poll(new QTimer(this))
        , m_deadline(new QTimer(this))
    {
        m_poll->setInterval(kQuitPollMs);
        connect(m_poll, &QTimer::timeout, this, [this] { startAgainOnceGone(); });
        m_deadline->setSingleShot(true);
        connect(m_deadline, &QTimer::timeout, this, [this] {
            finish(QCoreApplication::translate("nautilusmenu",
                                               "Nautilus did not quit — close its windows and start it again."));
        });
        connect(m_quit, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            // A crash still ends in finished(), which says so.
            if (error == QProcess::FailedToStart)
                finish(m_quit->errorString());
        });
        connect(m_quit, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
            if (status != QProcess::NormalExit || exitCode != 0) {
                finish(QCoreApplication::translate("nautilusmenu", "nautilus -q failed (exit %1)").arg(exitCode));
                return;
            }
            // Asked to quit is not gone yet: a new one started now would hand
            // its window to the old instance over D-Bus and quit with it.
            m_poll->start();
        });
        m_deadline->start(deadlineMs);
        m_quit->start(QStringLiteral("nautilus"), {QStringLiteral("-q")});
    }

private:
    void startAgainOnceGone()
    {
        if (nautilusmenu::nautilusRunning())
            return;
        m_poll->stop();
        QProcess nautilus;
        nautilus.setProgram(QStringLiteral("nautilus"));
        nautilus.setArguments({QStringLiteral("--new-window")});
        if (!nautilus.startDetached()) {
            finish(QCoreApplication::translate("nautilusmenu", "Could not start Nautilus: %1").arg(nautilus.errorString()));
            return;
        }
        finish(QString());
    }

    // Once, whichever way it ends: a quit still running is let go of, and
    // the caller hears of it only if it is still there to listen.
    void finish(const QString &error)
    {
        if (m_finished)
            return;
        m_finished = true;
        m_poll->stop();
        m_deadline->stop();
        if (m_quit->state() != QProcess::NotRunning)
            abandonProcess(m_quit, this);
        if (m_context)
            m_done(error);
        deleteLater();
    }

    QPointer<QObject> m_context;
    std::function<void(const QString &)> m_done;
    QProcess *m_quit;
    QTimer *m_poll;
    QTimer *m_deadline;
    bool m_finished = false;
};
} // namespace

namespace nautilusmenu {

QString extensionPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QLatin1String("/nautilus-python/extensions/omagit.py");
}

bool isInstalled()
{
    return QFileInfo::exists(extensionPath());
}

bool isCurrent()
{
    QFile installed(extensionPath()), embedded(kResource);
    return installed.open(QIODevice::ReadOnly) && embedded.open(QIODevice::ReadOnly)
        && installed.readAll() == embedded.readAll();
}

bool install(QString *error)
{
    QFile source(kResource);
    if (!source.open(QIODevice::ReadOnly)) {
        if (error)
            *error = source.errorString();
        return false;
    }
    const QString path = extensionPath();
    if (!QDir().mkpath(QFileInfo(path).path())) {
        if (error)
            *error = QCoreApplication::translate("nautilusmenu", "Could not create %1").arg(QFileInfo(path).path());
        return false;
    }
    // A short write is a failed one: commit() would put half a script in place.
    const QByteArray bytes = source.readAll();
    QSaveFile target(path);
    if (!target.open(QIODevice::WriteOnly) || target.write(bytes) != bytes.size() || !target.commit()) {
        if (error)
            *error = QCoreApplication::translate("nautilusmenu", "Could not write %1: %2").arg(path, target.errorString());
        return false;
    }
    return true;
}

bool remove(QString *error)
{
    QFile file(extensionPath());
    if (!file.exists() || file.remove())
        return true;
    if (error)
        *error = QCoreApplication::translate("nautilusmenu", "Could not remove %1: %2").arg(file.fileName(), file.errorString());
    return false;
}

bool refreshIfInstalled(QString *error)
{
    if (!isInstalled() || isCurrent())
        return true;
    return install(error);
}

bool nautilusFound()
{
    return !QStandardPaths::findExecutable(QStringLiteral("nautilus")).isEmpty();
}

bool pythonSupportFound()
{
    for (const char *path : kPythonLoaders) {
        if (QFileInfo::exists(QString::fromLatin1(path)))
            return true;
    }
    return false;
}

// The same places, in the same order, and the same access(X_OK) test as
// nautilus/omagit.py's _find_omagit().
bool omagitFound()
{
    if (!QStandardPaths::findExecutable(QStringLiteral("omagit")).isEmpty())
        return true;
    const QString candidates[] = {
        QDir::homePath() + QLatin1String("/.local/bin/omagit"),
        QStringLiteral("/usr/local/bin/omagit"),
        QStringLiteral("/usr/bin/omagit"),
    };
    for (const QString &path : candidates) {
        if (access(QFile::encodeName(path).constData(), X_OK) == 0)
            return true;
    }
    return false;
}

// What `pgrep -x -u $UID nautilus` finds, read off /proc without a process,
// with each one's start time. The kernel counts that in clock ticks since
// boot, so the age is measured against the boot clock — /proc/stat's btime
// only has whole seconds.
qint64 nautilusAgeMs()
{
    const uint uid = getuid();
    const long ticksPerSecond = sysconf(_SC_CLK_TCK);
    timespec boot{};
    if (ticksPerSecond <= 0 || clock_gettime(CLOCK_BOOTTIME, &boot) != 0)
        return -1;
    const qint64 nowMs = qint64(boot.tv_sec) * 1000 + boot.tv_nsec / 1000000;
    qint64 oldest = -1;
    const QDir proc(QStringLiteral("/proc"));
    for (const QFileInfo &entry : proc.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool isPid = false;
        entry.fileName().toUInt(&isPid);
        if (!isPid || entry.ownerId() != uid)
            continue;
        QFile comm(entry.filePath() + QLatin1String("/comm"));
        if (!comm.open(QIODevice::ReadOnly) || comm.readAll().trimmed() != "nautilus")
            continue;
        QFile stat(entry.filePath() + QLatin1String("/stat"));
        if (!stat.open(QIODevice::ReadOnly))
            continue;
        // The command name sits in parentheses and may hold spaces and
        // parentheses of its own: the fields start after the last ')'.
        const QByteArray line = stat.readAll();
        const int nameEnd = line.lastIndexOf(')');
        if (nameEnd < 0)
            continue;
        const QList<QByteArray> fields = line.mid(nameEnd + 1).simplified().split(' ');
        // A zombie has quit already; only its parent has not noticed yet.
        if (fields.size() <= kStartTimeField || fields.first() == "Z")
            continue;
        bool valid = false;
        const qulonglong startTicks = fields.at(kStartTimeField).toULongLong(&valid);
        if (!valid)
            continue;
        const qint64 startMs = qint64(startTicks * 1000 / qulonglong(ticksPerSecond));
        oldest = qMax(oldest, qMax<qint64>(0, nowMs - startMs));
    }
    return oldest;
}

bool nautilusRunning()
{
    return nautilusAgeMs() >= 0;
}

bool restartNeeded()
{
    const QFileInfo file(extensionPath());
    if (!file.exists())
        return false;
    const qint64 age = nautilusAgeMs();
    return age >= 0 && age > file.lastModified().msecsTo(QDateTime::currentDateTime());
}

void restart(QObject *context, std::function<void(const QString &error)> done, int deadlineMs)
{
    new RestartJob(context, std::move(done), deadlineMs);
}

} // namespace nautilusmenu
