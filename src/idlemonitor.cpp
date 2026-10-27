#include "idlemonitor.h"

#include <QGuiApplication>
#include <cstring>
#include <wayland-client.h>

#include "ext-idle-notify-v1-client-protocol.h"

// The protocol's callbacks, with the monitor and the notification's index
// as their data.
struct IdleCallbacks {
    struct Slot {
        IdleMonitor *monitor;
        int index;
    };

    static void global(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t) {
        auto *monitor = static_cast<IdleMonitor *>(data);
        if (std::strcmp(interface, ext_idle_notifier_v1_interface.name) == 0 && !monitor->m_notifier)
            monitor->m_notifier = static_cast<ext_idle_notifier_v1 *>(
                wl_registry_bind(registry, name, &ext_idle_notifier_v1_interface, 1));
    }
    static void globalRemove(void *, wl_registry *, uint32_t) {}

    static void idled(void *data, ext_idle_notification_v1 *) {
        auto *slot = static_cast<Slot *>(data);
        slot->monitor->setIdle(slot->index, true);
    }
    static void resumed(void *data, ext_idle_notification_v1 *) {
        auto *slot = static_cast<Slot *>(data);
        slot->monitor->setIdle(slot->index, false);
    }

    static constexpr wl_registry_listener registryListener{global, globalRemove};
    static constexpr ext_idle_notification_v1_listener notificationListener{idled, resumed};
};

IdleMonitor::IdleMonitor(const QList<int> &timeoutsMs, QObject *parent) : QObject(parent) {
    auto *wayland = qGuiApp ? qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>() : nullptr;
    if (!wayland || !wayland->display() || !wayland->seat())
        return;
    wl_display *display = wayland->display();

    // A queue of our own, dispatched here, so Qt's event handling is never
    // touched; idle events come once a minute at most, a second late is fine.
    m_queue = wl_display_create_queue(display);
    auto *wrapped = static_cast<wl_display *>(wl_proxy_create_wrapper(display));
    wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(wrapped), m_queue);
    m_registry = wl_display_get_registry(wrapped);
    wl_proxy_wrapper_destroy(wrapped);
    wl_registry_add_listener(m_registry, &IdleCallbacks::registryListener, this);
    wl_display_roundtrip_queue(display, m_queue);
    if (!m_notifier)
        return;

    for (int i = 0; i < timeoutsMs.size(); ++i) {
        if (timeoutsMs.at(i) <= 0)
            continue;
        auto *notification = ext_idle_notifier_v1_get_idle_notification(m_notifier, uint32_t(timeoutsMs.at(i)), wayland->seat());
        auto *slot = new IdleCallbacks::Slot{this, int(m_idle.size())};
        ext_idle_notification_v1_add_listener(notification, &IdleCallbacks::notificationListener, slot);
        m_notifications << notification;
        m_idle << false;
    }
    wl_display_flush(display);

    m_dispatch.setInterval(1000);
    connect(&m_dispatch, &QTimer::timeout, this, [display, this] {
        wl_display_dispatch_queue_pending(display, m_queue);
    });
    m_dispatch.start();
}

IdleMonitor::~IdleMonitor() {
    for (ext_idle_notification_v1 *notification : std::as_const(m_notifications)) {
        delete static_cast<IdleCallbacks::Slot *>(wl_proxy_get_user_data(reinterpret_cast<wl_proxy *>(notification)));
        ext_idle_notification_v1_destroy(notification);
    }
    if (m_notifier)
        ext_idle_notifier_v1_destroy(m_notifier);
    if (m_registry)
        wl_registry_destroy(m_registry);
    if (m_queue)
        wl_event_queue_destroy(m_queue);
}

void IdleMonitor::setIdle(int index, bool idle) {
    if (index < 0 || index >= m_idle.size())
        return;
    m_idle[index] = idle;
    int level = 0;
    for (bool passed : std::as_const(m_idle))
        level += passed ? 1 : 0;
    if (level != m_level) {
        m_level = level;
        emit levelChanged(level);
    }
}
