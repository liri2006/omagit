QT       += core
QT       -= gui
CONFIG   += c++17 console
CONFIG   -= app_bundle
TARGET    = gitrepo_test
TEMPLATE  = app

include(../omagit.pri)

# Core only: linking the widget layer here would defeat the point of the
# QT -= gui build, which is to keep GitRepo and friends free of QtGui.
SOURCES  += gitrepo_test.cpp $$OMAGIT_CORE_SOURCES
HEADERS  += $$OMAGIT_CORE_HEADERS
OBJECTS_DIR = ../build/tests/obj
MOC_DIR     = ../build/tests/moc
DESTDIR     = ../build/tests
