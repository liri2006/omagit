# The source lists of Omagit, kept here so a new file is registered once and
# the app and the test projects all see it. Paths go through $$PWD, so an
# including .pro may sit in any directory.

# What builds without QtGui: the git wrapper and the processes around it.
# tests/tests.pro builds exactly these with `QT -= gui`, so nothing listed
# here may reach for QtGui or QtWidgets.
OMAGIT_CORE_SOURCES = \
    $$PWD/src/AskPass.cpp \
    $$PWD/src/GitRepo.cpp \
    $$PWD/src/RemoteSync.cpp \
    $$PWD/src/CommitMessageAgent.cpp

OMAGIT_CORE_HEADERS = \
    $$PWD/src/AskPass.h \
    $$PWD/src/GitRepo.h \
    $$PWD/src/RemoteSync.h \
    $$PWD/src/CommitMessageAgent.h \
    $$PWD/src/DesktopExec.h \
    $$PWD/src/ProcessUtil.h

# The widget layer: everything else the application is made of, except
# src/main.cpp, which the test programs replace with a main() of their own.
OMAGIT_WIDGET_SOURCES = \
    $$PWD/src/OmarchyTheme.cpp \
    $$PWD/src/DiffModel.cpp \
    $$PWD/src/DiffView.cpp \
    $$PWD/src/SyntaxHighlighter.cpp \
    $$PWD/src/ChangesModel.cpp \
    $$PWD/src/ChangesTreeModel.cpp \
    $$PWD/src/HistoryModel.cpp \
    $$PWD/src/HistoryView.cpp \
    $$PWD/src/MiniRail.cpp \
    $$PWD/src/BadgeButton.cpp \
    $$PWD/src/TopBar.cpp \
    $$PWD/src/TickMenu.cpp \
    $$PWD/src/BranchMenu.cpp \
    $$PWD/src/MergeDialog.cpp \
    $$PWD/src/LoginDialog.cpp \
    $$PWD/src/CloneDialog.cpp \
    $$PWD/src/MessageEdit.cpp \
    $$PWD/src/KeybindingsPanel.cpp \
    $$PWD/src/UiHelpers.cpp \
    $$PWD/src/DesktopExec.cpp \
    $$PWD/src/DiffPane.cpp \
    $$PWD/src/CommitPage.cpp \
    $$PWD/src/Footer.cpp \
    $$PWD/src/MainWindow.cpp

OMAGIT_WIDGET_HEADERS = \
    $$PWD/src/OmarchyTheme.h \
    $$PWD/src/DiffModel.h \
    $$PWD/src/DiffView.h \
    $$PWD/src/SyntaxHighlighter.h \
    $$PWD/src/ChangesModel.h \
    $$PWD/src/ChangesTreeModel.h \
    $$PWD/src/HistoryModel.h \
    $$PWD/src/HistoryView.h \
    $$PWD/src/MiniRail.h \
    $$PWD/src/PaneLayout.h \
    $$PWD/src/BadgeButton.h \
    $$PWD/src/TopBar.h \
    $$PWD/src/TickMenu.h \
    $$PWD/src/BranchMenu.h \
    $$PWD/src/MergeDialog.h \
    $$PWD/src/LoginDialog.h \
    $$PWD/src/CloneDialog.h \
    $$PWD/src/MessageEdit.h \
    $$PWD/src/KeybindingsPanel.h \
    $$PWD/src/UiHelpers.h \
    $$PWD/src/DiffPane.h \
    $$PWD/src/CommitPage.h \
    $$PWD/src/Footer.h \
    $$PWD/src/MainWindow.h \
    $$PWD/src/Settings.h

OMAGIT_RESOURCES = $$PWD/data/omagit.qrc
