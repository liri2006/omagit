#pragma once

#include <QObject>
#include <QProcess>

// Let go of a process whose answer is of no interest any more — the object
// that asked has moved on, or is half gone already. The callbacks come off
// first, because the wait below delivers finished(); the wait itself is
// what makes the kill stick, and QProcess's destructor would wait anyway,
// only without a limit. Deleting the process (or not) is the caller's
// business: a destructor has nowhere to post a deleteLater to.
inline void abandonProcess(QProcess *process, QObject *listener)
{
    constexpr int kKillWaitMs = 2000;
    if (!process)
        return;
    if (listener)
        QObject::disconnect(process, nullptr, listener, nullptr);
    process->kill();
    process->waitForFinished(kKillWaitMs);
}
