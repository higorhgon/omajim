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
    src/filepicker.h \
    src/files.h \
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
    src/files.cpp \
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
