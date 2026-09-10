# What every widget test suite is built from: the whole application except
# src/main.cpp, which each suite replaces with a main() of its own. Linking
# the lot keeps omagit.pri the only place a new source file is registered.
# Include this *after* setting TARGET and the suite's own SOURCES.

QT += core gui network widgets testlib
CONFIG += c++17 console
CONFIG -= app_bundle
TEMPLATE = app

include(../omagit.pri)

SOURCES += $$OMAGIT_CORE_SOURCES $$OMAGIT_WIDGET_SOURCES
HEADERS += $$OMAGIT_CORE_HEADERS $$OMAGIT_WIDGET_HEADERS
RESOURCES += $$OMAGIT_RESOURCES

OBJECTS_DIR = ../build/$${TARGET}/obj
MOC_DIR     = ../build/$${TARGET}/moc
RCC_DIR     = ../build/$${TARGET}/rcc
DESTDIR     = ../build/tests
