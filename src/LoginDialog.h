#pragma once

#include "AskPass.h"

#include <QDialog>
#include <QStringList>

class GitRepo;
class SecretEdit;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpacerItem;
class QUrl;
class QVBoxLayout;

// The credential helpers git would end up with for `target`, worked out the
// way credential.c works them out. `configEntries` holds every
// `credential.helper` and `credential.<url>.helper` of git's configuration in
// the order git reads it — system, global, local, worktree, the command line —
// one entry per string in the form `git config -z --get-regexp` writes them:
// the key, a newline, then the value. Git walks the lot in that order,
// appending the value of every entry that applies and emptying the list again
// whenever one is empty, so which of two entries wins is a matter of order and
// not of how well either matches. The unqualified key always applies; a
// `credential.<url>.helper` applies when its URL covers `target`. An empty
// `target` — a passphrase, or a question of git's own — is covered by nothing,
// so only the unqualified entries count towards it.
//
// A pure function: the matching is the part worth testing, and it has no
// business running git.
QStringList credentialHelpersFor(const QStringList &configEntries, const QUrl &target);

// Whether `remoteUrl` — the URL a remote of the repository is configured with
// — is the place `target`, the URL a prompt names, asks about. Git matches
// `credential.<url>.helper` against the remote's URL whole, path and all, and
// only drops the path afterwards, when credential.useHttpPath is off, which is
// how it prompts; so a helper set for `https://example.com/team` cannot be
// found again from the pathless `https://example.com` the prompt carries. The
// path has to be fetched back from the remote the prompt is about, and this is
// what recognizes it: the same scheme, the same host, the same port once the
// scheme's own is filled in on either side, and the same user where both name
// one — git asks for a username precisely when the URL carries none, so a
// remote naming nobody is still the remote a prompt for `alice` is about.
//
// A pure function, for the same reason as the one above.
bool remoteUrlMatchesTarget(const QString &remoteUrl, const QUrl &target);

// The sign-in the app puts on screen when git or ssh asks its askpass helper
// for something (AskPass): a username and a password for an https remote, the
// passphrase of an ssh key, or, for anything else, the question in git's own
// words with one field under it.
//
// Git asks for the username and the password one after the other, so the
// first prompt collects both here and AskPass answers the rest of the
// operation's prompts for that host with them — one dialog per sign-in,
// however many times git asks. Nothing is stored: the note at the foot says
// whether git's credential helper will remember it.
class LoginDialog : public QDialog
{
    Q_OBJECT
public:
    // `repo` is asked which credential helper would keep what is typed here —
    // the one configured for this very remote, if there is one (none: the note
    // says so). It may be null, and is only read while the dialog is built.
    explicit LoginDialog(const AskPassRequest &request, GitRepo *repo = nullptr, QWidget *parent = nullptr);

    const AskPassRequest &request() const { return m_request; }
    // What the user typed, once the dialog was accepted. In a passphrase (or
    // any other single-field) prompt the answer is the password.
    QString username() const;
    QString password() const;

protected:
    void showEvent(QShowEvent *event) override;

private:
    void buildUi();
    void applyTheme();
    void updateAcceptable();
    void fitToContent();
    QString headingText() const;
    QString hintText() const;
    // `forEveryRemote` is false when the repository has several remotes the
    // prompt could be about and they are not kept by the same helper, so the
    // note can hedge instead of promising one of the two answers.
    QString noteText(const QString &credentialHelper, bool forEveryRemote) const;
    static QString credentialHelperFor(GitRepo *repo, const AskPassRequest &request, bool *forEveryRemote);
    bool wantsUsername() const;

    AskPassRequest m_request;
    QString m_note;

    QLabel *m_heading;
    QLabel *m_hint;
    QLabel *m_userCaption;
    QLineEdit *m_userEdit;
    QLabel *m_secretCaption;
    SecretEdit *m_secretEdit;
    QLabel *m_noteLabel;
    QPushButton *m_cancelButton, *m_signInButton;
    QVBoxLayout *m_headLayout;   // the heading and its hint
    QVBoxLayout *m_fieldsLayout; // the captions and their fields
    QSpacerItem *m_fieldsGap;    // between the two captioned fields
    QHBoxLayout *m_buttonRow;
};
