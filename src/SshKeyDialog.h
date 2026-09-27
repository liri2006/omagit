#pragma once

#include <QDialog>

class QHBoxLayout;
class QLabel;
class QListWidget;
class QPushButton;
class QVBoxLayout;

// Which ssh key a repository signs in with, chosen from the key pairs in
// ~/.ssh (or any key file) — offered when the server turned down the key ssh
// picked by itself. The choice is the repository's core.sshCommand
// (sshkeys::setRepoKey()), which the window writes once this is accepted;
// "ssh's choice" takes it out again. Built like the other dialogs: fixed
// width, fitted to a narrower window, as tall as its content.
class SshKeyDialog : public QDialog
{
    Q_OBJECT
public:
    // `repoName` is what the heading names; `currentCommand` the repository's
    // core.sshCommand as it stands, whose key comes selected (none: ssh's
    // choice). A command naming no key is one the choice would replace, and
    // the dialog says so.
    SshKeyDialog(const QString &repoName, const QString &currentCommand, QWidget *parent = nullptr);

    // The key file chosen; empty for ssh's own choice.
    QString chosenKey() const;
    // Selects the row of `path` (adding one for a key found elsewhere).
    void selectKey(const QString &path);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void applyTheme();
    void fitToContent();
    void browse();
    int addKeyRow(const QString &path, const QString &text, const QString &tip);

    QVBoxLayout *m_headLayout;
    QLabel *m_heading, *m_hint, *m_warning;
    QListWidget *m_list;
    QHBoxLayout *m_buttonRow;
    QPushButton *m_browse, *m_cancel, *m_use;
    QString m_warningColor;
};
