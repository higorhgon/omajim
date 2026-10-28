QT += core gui network xml sql dbus qml quick quickcontrols2

CONFIG += c++20 release
TARGET = omajim
TEMPLATE = app

# QXMPP_ROOT points at an unpacked qxmpp package when the library isn't
# installed yet; empty means the system copy in /usr.
include(qxmpp.pri)

HEADERS += \
    src/avatarstore.h \
    src/backend.h \
    src/contactmodel.h \
    src/emojicatalog.h \
    src/filepicker.h \
    src/files.h \
    src/firewall.h \
    src/gajimimport.h \
    src/history.h \
    src/instance.h \
    src/messagemodel.h \
    src/notifier.h \
    src/portalfilepicker.h \
    src/rosterstore.h \
    src/secretstore.h \
    src/textformat.h \
    src/theme.h \
    src/translator.h \
    src/tray.h

SOURCES += \
    src/main.cpp \
    src/avatarstore.cpp \
    src/backend.cpp \
    src/contactmodel.cpp \
    src/emojicatalog.cpp \
    src/files.cpp \
    src/firewall.cpp \
    src/gajimimport.cpp \
    src/history.cpp \
    src/instance.cpp \
    src/messagemodel.cpp \
    src/notifier.cpp \
    src/portalfilepicker.cpp \
    src/rosterstore.cpp \
    src/secretstore.cpp \
    src/textformat.cpp \
    src/theme.cpp \
    src/translator.cpp \
    src/tray.cpp

RESOURCES += src/resources.qrc

# Idle time from the compositor (ext-idle-notify-v1): the protocol's C code
# is generated at build time by wayland-scanner, never committed.
HEADERS += src/idlemonitor.h
SOURCES += src/idlemonitor.cpp
CONFIG += link_pkgconfig
PKGCONFIG += wayland-client
WAYLAND_XML = $$system(pkg-config --variable=pkgdatadir wayland-protocols)/staging/ext-idle-notify/ext-idle-notify-v1.xml
wayland_header.input = WAYLAND_XML
wayland_header.output = ${QMAKE_FILE_BASE}-client-protocol.h
wayland_header.commands = wayland-scanner client-header ${QMAKE_FILE_IN} ${QMAKE_FILE_OUT}
wayland_header.variable_out = HEADERS
wayland_header.CONFIG += target_predeps no_link
wayland_code.input = WAYLAND_XML
wayland_code.output = ${QMAKE_FILE_BASE}-protocol.c
wayland_code.commands = wayland-scanner private-code ${QMAKE_FILE_IN} ${QMAKE_FILE_OUT}
wayland_code.variable_out = SOURCES
wayland_code.depends = ${QMAKE_FILE_BASE}-client-protocol.h
QMAKE_EXTRA_COMPILERS += wayland_header wayland_code
