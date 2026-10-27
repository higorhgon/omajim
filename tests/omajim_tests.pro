QT += core gui network xml sql dbus quick quickcontrols2 testlib
CONFIG += c++20 testcase
TARGET = omajim_tests
TEMPLATE = app

include(../qxmpp.pri)
INCLUDEPATH += ../src

HEADERS += \
    ../src/backend.h \
    ../src/contactmodel.h \
    ../src/filepicker.h \
    ../src/files.h \
    ../src/history.h \
    ../src/messagemodel.h \
    ../src/notifier.h \
    ../src/secretstore.h \
    ../src/theme.h \
    ../src/translator.h

SOURCES += \
    omajim_tests.cpp \
    ../src/backend.cpp \
    ../src/contactmodel.cpp \
    ../src/files.cpp \
    ../src/history.cpp \
    ../src/messagemodel.cpp \
    ../src/notifier.cpp \
    ../src/secretstore.cpp \
    ../src/theme.cpp \
    ../src/translator.cpp
