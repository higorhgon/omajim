QXMPP_ROOT = $$(QXMPP_ROOT)
INCLUDEPATH += $$QXMPP_ROOT/usr/include/QXmppQt6
# Onde ela fica quando vem junto com o Qt (MSYS2 no Windows: /ucrt64/include).
isEmpty(QXMPP_ROOT): INCLUDEPATH += $$[QT_INSTALL_PREFIX]/include/QXmppQt6
!isEmpty(QXMPP_ROOT): LIBS += -L$$QXMPP_ROOT/usr/lib
LIBS += -lQXmppQt6
