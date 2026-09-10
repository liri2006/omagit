QT       += core gui gui-private network widgets
CONFIG   += c++17
# An optimised build unless one is asked for on the command line:
# `qmake6 CONFIG+=debug omagit.pro` gives -g without editing this file.
!CONFIG(debug, debug|release): CONFIG += release
TARGET    = omagit
TEMPLATE  = app

DEFINES += QT_DEPRECATED_WARNINGS OMAGIT_VERSION=\\\"0.2.0\\\"

include(omagit.pri)

SOURCES += $$OMAGIT_CORE_SOURCES $$OMAGIT_WIDGET_SOURCES src/main.cpp
HEADERS += $$OMAGIT_CORE_HEADERS $$OMAGIT_WIDGET_HEADERS

RESOURCES += $$OMAGIT_RESOURCES

OBJECTS_DIR = build/obj
MOC_DIR     = build/moc
RCC_DIR     = build/rcc
