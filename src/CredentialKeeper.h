#pragma once

#include "AskPass.h"

#include <QList>
#include <QObject>
#include <QStringList>

class QThread;

// Remembers the sign-ins the user asked to have remembered, the way git
// remembers them: through a credential helper. Omagit writes no password
// anywhere. It names the helper that is to keep the logins of that one
// server — a `credential.<scheme>://<host>[:<port>].helper` entry in git's
// global configuration — and hands the login to `git credential approve`,
// git's own way of saving a login that worked; the helper stores it, and git
// asks that helper first from then on, so the server stops asking.
//
// The helper is libsecret, the desktop's keyring, which every Omarchy machine
// has (git's libsecret helper, libsecret and gnome-keyring are all part of the
// base install). A login that could not be stored takes the configuration
// entry added for it back out, so git is not left asking a helper that has
// nothing for it.
//
// Core only: git runs off the UI thread, since a locked keyring may ask to be
// unlocked before it stores anything.
class CredentialKeeper : public QObject
{
    Q_OBJECT
public:
    explicit CredentialKeeper(QObject *parent = nullptr);
    // Waits for logins still being handed over: the process holding one is
    // not to be left behind.
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
