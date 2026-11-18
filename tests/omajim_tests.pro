QT += core gui network xml sql quick quickcontrols2 testlib
CONFIG += c++20 testcase
CONFIG -= app_bundle
TARGET = omajim_tests
TEMPLATE = app

include(../qxmpp.pri)
INCLUDEPATH += ../src

HEADERS += \
    ../src/accounts.h \
    ../src/avatarstore.h \
    ../src/backend.h \
    ../src/composer.h \
    ../src/contactmodel.h \
    ../src/emojicatalog.h \
    ../src/filepicker.h \
    ../src/files.h \
    ../src/firewall.h \
    ../src/gajimimport.h \
    ../src/history.h \
    ../src/linkhints.h \
    ../src/messagemodel.h \
    ../src/notifier.h \
    ../src/richtext.h \
    ../src/rosterstore.h \
    ../src/secretstore.h \
    ../src/smileys.h \
    ../src/textformat.h \
    ../src/theme.h \
    ../src/translator.h \
    ../src/updater.h

SOURCES += \
    omajim_tests.cpp \
    ../src/accounts.cpp \
    ../src/avatarstore.cpp \
    ../src/backend.cpp \
    ../src/composer.cpp \
    ../src/contactmodel.cpp \
    ../src/emojicatalog.cpp \
    ../src/files.cpp \
    ../src/firewall.cpp \
    ../src/gajimimport.cpp \
    ../src/history.cpp \
    ../src/linkhints.cpp \
    ../src/messagemodel.cpp \
    ../src/richtext.cpp \
    ../src/rosterstore.cpp \
    ../src/smileys.cpp \
    ../src/textformat.cpp \
    ../src/theme.cpp \
    ../src/translator.cpp \
    ../src/updater.cpp

linux {
    QT += dbus
    HEADERS += ../src/tray.h
    SOURCES += ../src/notifier.cpp ../src/secretstore.cpp ../src/tray.cpp
}

macx {
    QT += widgets
    SOURCES += ../src/notifier_widgets.cpp
    OBJECTIVE_SOURCES += ../src/secretstore_mac.mm
    QMAKE_CXXFLAGS += -fobjc-arc
    LIBS += -framework Foundation -framework Security
}
