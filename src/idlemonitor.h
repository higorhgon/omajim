#pragma once

#include <QList>
#include <QObject>
#include <QTimer>

struct ext_idle_notification_v1;
struct ext_idle_notifier_v1;
struct wl_event_queue;
struct wl_registry;

// Tells how long the user has been away from the computer, from the
// compositor (ext-idle-notify-v1, which Hyprland implements and which honors
// idle inhibitors such as a playing video). Given timeouts in increasing
// order, the level is how many have passed: 0 while in use. Outside Wayland,
// or with a compositor without the protocol, it stays at 0.
class IdleMonitor : public QObject {
    Q_OBJECT

public:
    explicit IdleMonitor(const QList<int> &timeoutsMs, QObject *parent = nullptr);
    ~IdleMonitor() override;

    bool available() const { return m_notifier != nullptr; }
    int level() const { return m_level; }

signals:
    void levelChanged(int level);

private:
    friend struct IdleCallbacks;
    void setIdle(int index, bool idle);

    wl_event_queue *m_queue = nullptr;
    wl_registry *m_registry = nullptr;
    ext_idle_notifier_v1 *m_notifier = nullptr;
    QList<ext_idle_notification_v1 *> m_notifications;
    QList<bool> m_idle;
    QTimer m_dispatch;
    int m_level = 0;
};
