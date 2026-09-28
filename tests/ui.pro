TARGET = ui_test
# The test classes run in the order their files are listed here.
SOURCES += \
    ui/main.cpp \
    ui/UiTestCase.cpp \
    ui/fixtures.cpp \
    ui/clone_test.cpp \
    ui/history_test.cpp \
    ui/files_test.cpp \
    ui/commitpage_test.cpp \
    ui/keys_test.cpp \
    ui/topbar_test.cpp \
    ui/stacked_test.cpp \
    ui/window_sizes_test.cpp \
    ui/diffpane_test.cpp \
    ui/mini_test.cpp \
    ui/agent_test.cpp \
    ui/branches_test.cpp \
    ui/signin_test.cpp \
    ui/sync_test.cpp \
    ui/settings_test.cpp \
    ui/theme_test.cpp
HEADERS += \
    ui/UiTestCase.h \
    ui/fixtures.h
include(widgets.pri)
