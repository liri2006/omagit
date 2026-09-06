QT       += core gui widgets
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
    src/MainWindow.cpp

HEADERS += \
    src/OmarchyTheme.h \
    src/GitRepo.h \
    src/DiffModel.h \
    src/DiffView.h \
    src/ChangesModel.h \
    src/HistoryModel.h \
    src/HistoryView.h \
    src/MainWindow.h

RESOURCES += data/omagit.qrc

OBJECTS_DIR = build/obj
MOC_DIR     = build/moc
RCC_DIR     = build/rcc
