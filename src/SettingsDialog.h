#pragma once

#include <QDialog>

class QCheckBox;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QVBoxLayout;

// The application's settings, behind the footer's cog, More's Settings… and
// Ctrl+,. Every change applies at once; Close only closes. For now it holds
// the file manager: whether Nautilus offers "Open in Omagit" on the folders
// and files of a repository (nautilusmenu:: puts the extension in place).
class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void buildUi();
    void applyTheme();
    void setNautilusMenu(bool on);
    void restartNautilus();
    // The note under the checkbox and whether the restart button shows, from
    // what is installed and running now, and m_error.
    void updateNautilusNote();
    // Red while the note reports an error, in the red of the current theme.
    void applyNoteColor();
    void fitWidth();
    void fitToContent();

    QLabel *m_fileManagerCaption;
    QCheckBox *m_nautilusBox;
    QLabel *m_nautilusNote;
    QPushButton *m_restartButton;
    QPushButton *m_closeButton;
    QVBoxLayout *m_fileManagerLayout; // the caption, the checkbox and its detail
    QVBoxLayout *m_nautilusDetail;    // the note and the restart button, under the checkbox's text
    QHBoxLayout *m_buttonRow;
    bool m_changed = false; // the extension was added or removed while this dialog was open
    bool m_restarting = false;
    QString m_error;     // the last write or restart that failed, until one works
    QString m_noteSheet; // the stylesheet applyNoteColor() last gave the note
};
