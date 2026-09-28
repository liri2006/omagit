QT       += core gui network widgets
CONFIG   += c++17
# An optimised build unless one is asked for on the command line:
# `qmake6 CONFIG+=debug omagit.pro` gives -g without editing this file.
!CONFIG(debug, debug|release): CONFIG += release
TARGET    = omagit
TEMPLATE  = app

DEFINES += QT_DEPRECATED_WARNINGS OMAGIT_VERSION=\\\"0.9.0\\\"

# --screenshot-keys types into the window the way the window system would,
# which takes Qt's private QPA API. That API is tied to the exact Qt release
# it was built against, so distribution packages build with
# `CONFIG+=no_screenshot_keys` and leave the test-only option out.
CONFIG(no_screenshot_keys): DEFINES += OMAGIT_NO_SCREENSHOT_KEYS
else: QT += gui-private

include(omagit.pri)

SOURCES += $$OMAGIT_CORE_SOURCES $$OMAGIT_WIDGET_SOURCES src/main.cpp
HEADERS += $$OMAGIT_CORE_HEADERS $$OMAGIT_WIDGET_HEADERS

RESOURCES += $$OMAGIT_RESOURCES

OBJECTS_DIR = build/obj
MOC_DIR     = build/moc
RCC_DIR     = build/rcc

# `make install` puts the app under PREFIX (default /usr/local); packages use
# `qmake6 PREFIX=/usr` and `make INSTALL_ROOT="$pkgdir" install`. Stripping is
# left to the packager, so makepkg can split out debug symbols.
isEmpty(PREFIX): PREFIX = /usr/local
CONFIG += nostrip
target.path = $$PREFIX/bin
desktop.files = data/omagit.desktop
desktop.path = $$PREFIX/share/applications
icon.files = data/omagit.svg
icon.path = $$PREFIX/share/icons/hicolor/scalable/apps
INSTALLS += target desktop icon
