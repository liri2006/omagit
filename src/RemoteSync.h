#pragma once

#include "GitRepo.h"

#include <QDateTime>
#include <QFileSystemWatcher>
#include <QObject>
#include <QPointer>
#include <QTimer>

class QProcess;

// Fetch, pull and push for the repository, plus the numbers behind the Pull
// and Push buttons: how far the branch is behind and ahead of its upstream.
//
// The counts come from the local remote-tracking ref (cheap, no network),
// so they only move when something fetches. To keep the pull count live the
// object fetches on its own: shortly after start, then every few minutes
// while the window is on screen, and when the window comes back to the front
// after a while. Failures back off exponentially so an offline machine is
// not hammered. Anything that touches the refs from outside (a fetch or
// push in a terminal) is picked up through a watch on the git directory.
class RemoteSync : public QObject
{
    Q_OBJECT
public:
    enum Op { None, Fetch, Pull, Push };
    Q_ENUM(Op)

    explicit RemoteSync(GitRepo *repo, QObject *parent = nullptr);
    ~RemoteSync() override;

    const UpstreamState &state() const { return m_state; }
    Op runningOp() const { return m_op; }
    bool busy() const { return m_op != None; }
    QDateTime lastFetch() const { return m_lastFetch; }
    bool lastFetchOk() const { return m_lastFetchOk; }
    QString lastFetchError() const { return m_lastFetchError; }

    bool canFetch() const { return !busy() && !m_state.remotes.isEmpty(); }
    bool canPull() const { return !busy() && m_state.hasUpstream(); }
    bool canPush() const { return !busy() && !m_state.branch.isEmpty() && !m_state.remote.isEmpty(); }
    // True when a push would create the upstream branch (`push -u`).
    bool pushPublishes() const { return !m_state.hasUpstream(); }

    // Seconds between automatic fetches; 0 turns them off.
    int autoFetchInterval() const { return m_interval; }
    void setAutoFetchInterval(int seconds);
    // Whether the window is on screen. Automatic fetches pause while it is not.
    void setActive(bool active);
    // The window came to the front: fetch now unless one happened recently.
    void nudge();

    // Which git command an operation runs, for tooltips.
    QStringList fetchArgs() const;
    QStringList pullArgs() const;
    QStringList pushArgs() const;

public slots:
    // Re-reads the upstream and the ahead/behind counts (local, quick).
    void refreshState();
    void fetch();
    void pull();
    void push();

signals:
    // The counts, the upstream, or the busy state changed.
    void stateChanged();
    // An operation finished; `message` is a one-line summary or the error
    // text. `automatic` marks a fetch the object started on its own.
    void finished(RemoteSync::Op op, bool ok, bool automatic, const QString &message);
    // Refs or the git directory changed from outside (debounced).
    void repositoryChanged();

private:
    void start(Op op, const QStringList &args);
    void onFinished(Op op, int code, const QByteArray &out, const QByteArray &err);
    void scheduleAutoFetch();
    void autoFetch();
    void watchGitDir();

    GitRepo *m_repo;
    UpstreamState m_state;
    Op m_op = None;
    QPointer<QProcess> m_process;
    bool m_autoOp = false; // the running fetch was started automatically
    int m_behindBefore = 0, m_aheadBefore = 0;
    QDateTime m_lastFetch;
    bool m_lastFetchOk = true;
    QString m_lastFetchError;
    int m_interval = 180;
    int m_failures = 0;
    bool m_active = false;
    QTimer m_autoTimer;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QString m_gitDir;
};
