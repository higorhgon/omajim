#include "instance.h"

#include <QDBusConnection>
#include <QDBusMessage>

// Not the Python Omajim's application id, so both can run side by side.
static const char *Service = "io.github.higorhgon.Omajim.Instance";

bool Instance::claim() {
    QDBusConnection bus = QDBusConnection::sessionBus();
    // Without a session bus there's no one to hand over to.
    if (!bus.isConnected())
        return true;
    if (bus.registerService(Service)) {
        bus.registerObject("/Instance", this, QDBusConnection::ExportScriptableSlots);
        return true;
    }
    bus.call(QDBusMessage::createMethodCall(Service, "/Instance", Service, "Show"), QDBus::Block, 2000);
    return false;
}
