QXMPP_ROOT = $$(QXMPP_ROOT)
INCLUDEPATH += $$QXMPP_ROOT/usr/include/QXmppQt6
# Onde ela fica quando vem junto com o Qt (MSYS2 no Windows: /ucrt64/include).
isEmpty(QXMPP_ROOT): INCLUDEPATH += $$[QT_INSTALL_PREFIX]/include/QXmppQt6
!isEmpty(QXMPP_ROOT): LIBS += -L$$QXMPP_ROOT/usr/lib
macx {
    QXMPP_PREFIX = $$(QXMPP_PREFIX)
    isEmpty(QXMPP_PREFIX): QXMPP_PREFIX = $$system(brew --prefix qxmpp 2>/dev/null)
    !isEmpty(QXMPP_PREFIX) {
        INCLUDEPATH += "$$QXMPP_PREFIX/include/QXmppQt6"
        LIBS += -L"$$QXMPP_PREFIX/lib"
        QMAKE_RPATHDIR += "$$QXMPP_PREFIX/lib"
        PRE_TARGETDEPS += "$$QXMPP_PREFIX/lib/libQXmppQt6.dylib"
    }
}
LIBS += -lQXmppQt6
