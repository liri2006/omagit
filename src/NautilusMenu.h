#pragma once

#include <QString>

#include <functional>

class QObject;

// Nautilus's "Open in Omagit" entry: a nautilus-python extension the app
// carries in its resources (nautilus/omagit.py) and copies into the user's
// extension folder when the settings ask for it. Nautilus reads that folder
// when it starts, so a running one only shows a change after a restart.
namespace nautilusmenu {

// How long restart() gives Nautilus to quit and come back before it gives up.
constexpr int kRestartDeadlineMs = 5000;

// $XDG_DATA_HOME/nautilus-python/extensions/omagit.py, where nautilus-python
// looks for the user's own extensions.
QString extensionPath();
bool isInstalled();
// Installed, and byte for byte the extension this build carries.
bool isCurrent();
// Writes the extension (replacing an older copy) or deletes it; false with
// the reason in `error` when the file system says no.
bool install(QString *error);
bool remove(QString *error);
// An installed copy an older build wrote is replaced with this build's; true
// when there was nothing to do or the new copy is in place.
bool refreshIfInstalled(QString *error);

// Whether Nautilus is there at all, and nautilus-python, which loads the
// extension into it — without it the file is there and nothing shows.
bool nautilusFound();
bool pythonSupportFound();
// Whether the entry finds an omagit to start: it looks where the extension's
// _find_omagit() does, PATH first, then the usual install folders.
bool omagitFound();
// How long the oldest Nautilus of this user has been running, in
// milliseconds; -1 when none is. A running one keeps its extensions until it
// quits.
qint64 nautilusAgeMs();
bool nautilusRunning();
// The installed extension is newer than the running Nautilus, which has not
// loaded it yet.
bool restartNeeded();

// Quits the running Nautilus (`nautilus -q`), waits for it to be gone and
// starts a new one, which loads the extensions as they are now. The work
// belongs to the application, so it finishes even when `context` goes away
// first; `done` runs once at the end, with an empty error on success, and
// only while `context` is still there. After a failure nothing is started.
void restart(QObject *context, std::function<void(const QString &error)> done,
             int deadlineMs = kRestartDeadlineMs);

} // namespace nautilusmenu
