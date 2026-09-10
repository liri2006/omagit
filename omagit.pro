QT       += core gui gui-private widgets
CONFIG   += c++17 release
TARGET    = omagit
TEMPLATE  = app

DEFINES += QT_DEPRECATED_WARNINGS OMAGIT_VERSION=\\\"0.2.0\\\"

SOURCES += \
    src/main.cpp \
    src/OmarchyTheme.cpp \
    src/GitRepo.cpp \
    src/DiffModel.cpp \
    src/DiffView.cpp \
    src/ChangesModel.cpp \
    src/HistoryModel.cpp \
    src/HistoryView.cpp \
    src/MiniRail.cpp \
    src/RemoteSync.cpp \
    src/BadgeButton.cpp \
    src/Toolbar.cpp \
    src/TickMenu.cpp \
    src/BranchMenu.cpp \
    src/MergeDialog.cpp \
    src/CommitMessageAgent.cpp \
    src/MessageEdit.cpp \
    src/KeybindingsPanel.cpp \
    src/MainWindow.cpp

HEADERS += \
    src/DesktopExec.h \
    src/OmarchyTheme.h \
    src/GitRepo.h \
    src/DiffModel.h \
    src/DiffView.h \
    src/ChangesModel.h \
    src/HistoryModel.h \
    src/HistoryView.h \
    src/MiniRail.h \
    src/PaneLayout.h \
    src/RemoteSync.h \
    src/BadgeButton.h \
    src/Toolbar.h \
    src/TickMenu.h \
    src/BranchMenu.h \
    src/MergeDialog.h \
    src/CommitMessageAgent.h \
    src/MessageEdit.h \
    src/KeybindingsPanel.h \
    src/MainWindow.h

RESOURCES += data/omagit.qrc

OBJECTS_DIR = build/obj
MOC_DIR     = build/moc
RCC_DIR     = build/rcc
