#pragma once

#include <QList>
#include <QString>

class GitRepo;

// The ssh key pairs of the user, and the git setting that picks one of them
// for a repository. ssh on its own offers the keys of its configuration, the
// agent's and its default files (id_ed25519 and the like), which does not
// reach a key kept under a name of its own. git has a place for the choice —
// `core.sshCommand` in the repository's configuration — and that is all
// Omagit writes: `ssh -i <key> -o IdentitiesOnly=yes`, so ssh offers that key
// and no other. An exported GIT_SSH_COMMAND outranks it; the windows that
// offer the choice say so. (GIT_SSH does not: git only goes by it when no
// command is configured.)
//
// Core only: the clone dialog and the key picker show what this finds.
namespace sshkeys {

struct Key {
    QString path;        // the private key file
    QString type;        // "ED25519", "RSA", "ECDSA", "ED25519-SK"…
    QString comment;     // what the public key file says after the key, often user@host
    QString fingerprint; // "SHA256:…", as ssh-keygen -l prints it
    QString name() const; // the file's name
};

// Where keys are looked for: ~/.ssh.
QString directory();
// The key pairs in `dir`, by name: every file with a public key beside it
// (the same name plus .pub) that reads as one.
QList<Key> find(const QString &dir = directory());
// A public key file's line — "ssh-ed25519 AAAA… comment" — taken apart.
// False when it is no public key.
bool readPublicKey(const QString &line, Key *key);
// Whether ssh offers the key at `path` by itself, being one of the default
// identity files (~/.ssh/id_ed25519 and its kin), with no configuration.
bool isDefaultIdentity(const QString &path);

// What core.sshCommand is set to so that ssh offers `keyPath` and nothing
// else, quoted for the shell git runs it with.
QString sshCommand(const QString &keyPath);
// The key a core.sshCommand hands ssh with -i; empty when it names none.
QString keyOf(const QString &command);
// Whether `url` is reached over ssh: ssh://, git+ssh://, or the scp form
// [user@]host:path.
bool isSshUrl(const QString &url);
// Whether an ssh-agent answers where ssh looks for one, SSH_AUTH_SOCK in the
// environment Omagit (and so the git and ssh it runs) was started with. A key
// added to it stays unlocked until the agent stops, with the session.
bool agentReachable();
// "GIT_SSH_COMMAND" when it is set in the environment (even to nothing),
// since git then leaves core.sshCommand alone; empty otherwise. GIT_SSH is
// not one: git consults it only when no core.sshCommand is set.
QString overridingVariable();

// The repository's core.sshCommand, as it stands; empty when there is none.
QString repoCommand(GitRepo *repo);
// Points the repository's core.sshCommand at `keyPath`, or removes it for an
// empty one (ssh's own choice again). False, with git's words in `error`,
// when git would not write it.
bool setRepoKey(GitRepo *repo, const QString &keyPath, QString *error = nullptr);

} // namespace sshkeys
