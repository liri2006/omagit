#pragma once

#include "AskPass.h"

#include <QList>
#include <QObject>
#include <QStringList>

class QThread;

// Remembers the sign-ins the user asked to have remembered, the way git
// remembers them: through a credential helper. Omagit writes no password
// anywhere. It hands the login to `git credential approve`, git's own way of
// saving a login that worked, with that one helper configured for the call;
// the helper stores it. Then it names the helper that is to keep the logins
// of that server — a `credential.<scheme>://<host>[:<port>].helper` entry in
// git's global configuration — so git asks that helper from then on, and the
// server stops asking.
//
// The helper is libsecret, the desktop's keyring, which every Omarchy machine
// has (git's libsecret helper, libsecret and gnome-keyring are all part of the
// base install). The store comes first so that no entry ever points at a
// helper with nothing for it: a login that could not be stored leaves the
// configuration alone, and an entry that could not be written takes the
// stored login back out with `git credential reject`.
//
// Core only: git runs off the UI thread, since a locked keyring may ask to be
// unlocked before it stores anything.
class CredentialKeeper : public QObject
{
    Q_OBJECT
public:
    explicit CredentialKeeper(QObject *parent = nullptr);
    // Waits a few seconds for logins still being handed over, so a store
    // about to finish is not cut short. One still going after that (a keyring
    // waiting to be unlocked) is left to finish on its own: its thread
    // deletes itself, and finished() is not emitted for it.
    ~CredentialKeeper() override;

    // The helper logins go to: "libsecret", unless a test names one of its own.
    static QString helper();
    static void setHelper(const QString &name);
    // Whether git has that helper: git-credential-<name> in git's exec path,
    // or on PATH, where git looks for helpers too.
    static bool helperAvailable();
    // The key that names the helper for the server `context` is on (the
    // context AskPass keys logins by): its scheme, host and port, so every
    // repository on the server is covered. Empty for anything but a URL.
    static QString configKey(const QString &context);

    // Hands `logins` to git, in the background; finished() says how it went.
    void keep(const QList<KeptLogin> &logins);
    // The same, at once — what keep() runs off the UI thread. Returns what
    // went wrong with each login that could not be kept.
    static QStringList keepNow(const QList<KeptLogin> &logins);

signals:
    // `errors` is empty when every login was kept.
    void finished(const QStringList &errors);

private:
    static QStringList keepAll(const QList<KeptLogin> &logins, const QString &helper);

    QList<QThread *> m_threads;
};
