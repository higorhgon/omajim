QT += core gui network xml sql qml quick quickcontrols2
# No Windows não há D-Bus: bandeja e diálogo de arquivos vêm do QtWidgets, e
# o ícone (SVG) do QtSvg.
unix: QT += dbus
win32: QT += widgets svg

CONFIG += c++20 release
TARGET = omajim
TEMPLATE = app

# QXMPP_ROOT points at an unpacked qxmpp package when the library isn't
# installed yet; empty means the system copy in /usr.
include(qxmpp.pri)

HEADERS += \
    src/accounts.h \
    src/avatarstore.h \
    src/backend.h \
    src/composer.h \
    src/contactmodel.h \
    src/emojicatalog.h \
    src/filepicker.h \
    src/files.h \
    src/firewall.h \
    src/gajimimport.h \
    src/history.h \
    src/linkhints.h \
    src/messagemodel.h \
    src/richtext.h \
    src/rosterstore.h \
    src/smileys.h \
    src/textformat.h \
    src/theme.h \
    src/translator.h \
    src/updater.h

SOURCES += \
    src/main.cpp \
    src/accounts.cpp \
    src/avatarstore.cpp \
    src/backend.cpp \
    src/composer.cpp \
    src/contactmodel.cpp \
    src/emojicatalog.cpp \
    src/files.cpp \
    src/firewall.cpp \
    src/gajimimport.cpp \
    src/history.cpp \
    src/linkhints.cpp \
    src/messagemodel.cpp \
    src/richtext.cpp \
    src/rosterstore.cpp \
    src/smileys.cpp \
    src/textformat.cpp \
    src/theme.cpp \
    src/translator.cpp \
    src/updater.cpp

RESOURCES += src/resources.qrc

# A mesma interface em cada sistema: no Linux sobre D-Bus e Wayland, no
# Windows sobre as APIs dele (os arquivos *_win.cpp e win*.cpp).
HEADERS += src/idlemonitor.h src/instance.h src/notifier.h src/secretstore.h

unix {
HEADERS += src/portalfilepicker.h src/tray.h
SOURCES += src/instance.cpp src/notifier.cpp src/portalfilepicker.cpp src/secretstore.cpp src/tray.cpp

# Idle time from the compositor (ext-idle-notify-v1): the protocol's C code
# is generated at build time by wayland-scanner, never committed.
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
}

win32 {
HEADERS += src/winfilepicker.h src/wintray.h
SOURCES += src/idlemonitor_win.cpp src/instance_win.cpp src/notifier_win.cpp src/secretstore_win.cpp \
    src/winfilepicker.cpp src/wintray.cpp
RESOURCES += src/windows.qrc
# Gerenciador de Credenciais e GetLastInputInfo.
LIBS += -ladvapi32 -luser32 -lshell32
# Ícone do .exe; sem console atrás da janela.
RC_ICONS = packaging/windows/omajim.ico
CONFIG += windows
CONFIG -= console
}
