QXMPP_ROOT = $$(QXMPP_ROOT)
INCLUDEPATH += $$QXMPP_ROOT/usr/include/QXmppQt6
!isEmpty(QXMPP_ROOT): LIBS += -L$$QXMPP_ROOT/usr/lib
LIBS += -lQXmppQt6
