#pragma once

#include "AskPass.h"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QStringList>

class QProcess;

// Keeps ssh keys unlocked until the user logs out, the way a terminal would:
// `ssh-add <key>` hands each key to the ssh-agent the session runs (the one
// SSH_AUTH_SOCK names), and ssh takes it from there instead of asking for the
// passphrase again. ssh-add asks for the passphrase through an askpass helper
// like any ssh, so it runs with Omagit's own (an AskPass of its own) and gets
// the passphrase the sign-in dialog was given — from memory, never from a
// file. Only keys whose passphrase just worked come here (AskPass::keysToUnlock()).
//
// Core only; ssh-add runs one key at a time, in the background.
class AgentKeeper : public QObject
{
    Q_OBJECT
public:
    explicit AgentKeeper(QObject *parent = nullptr);
    ~AgentKeeper() override;

    // Adds `keys` to the agent; finished() says how it went once all are done.
    void add(const QList<AgentKey> &keys);
    // The askpass helper ssh-add is pointed at: the running program, unless a
    // test (whose own binary is no helper) points it at the built app.
    void setHelperPath(const QString &path);

signals:
    // `errors` is empty when every key was added.
    void finished(const QStringList &errors);

private:
    void next();

    AskPass *m_askPass;
    QList<AgentKey> m_queue;
    QStringList m_errors;
    QPointer<QProcess> m_process;
    QString m_passphrase; // the key being added
    bool m_asked = false; // ssh-add asked once: a second time means it was turned down
};
