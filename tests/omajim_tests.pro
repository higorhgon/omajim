QT += core gui network xml sql dbus quick quickcontrols2 testlib
CONFIG += c++20 testcase
TARGET = omajim_tests
TEMPLATE = app

include(../qxmpp.pri)
INCLUDEPATH += ../src

HEADERS += \
    ../src/avatarstore.h \
    ../src/backend.h \
    ../src/contactmodel.h \
    ../src/filepicker.h \
    ../src/files.h \
    ../src/gajimimport.h \
    ../src/history.h \
    ../src/messagemodel.h \
    ../src/notifier.h \
    ../src/rosterstore.h \
    ../src/secretstore.h \
    ../src/textformat.h \
    ../src/theme.h \
    ../src/translator.h \
    ../src/tray.h

SOURCES += \
    omajim_tests.cpp \
    ../src/avatarstore.cpp \
    ../src/backend.cpp \
    ../src/contactmodel.cpp \
    ../src/files.cpp \
    ../src/gajimimport.cpp \
    ../src/history.cpp \
    ../src/messagemodel.cpp \
    ../src/notifier.cpp \
    ../src/rosterstore.cpp \
    ../src/secretstore.cpp \
    ../src/textformat.cpp \
    ../src/theme.cpp \
    ../src/translator.cpp \
    ../src/tray.cpp
