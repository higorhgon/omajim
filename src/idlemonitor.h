#pragma once

#include <QList>
#include <QObject>
#include <QTimer>

struct ext_idle_notification_v1;
struct ext_idle_notifier_v1;
struct wl_event_queue;
struct wl_registry;

// The idle level counts how many enabled timeouts have passed. On Linux
// it stays at 0 if the compositor doesn't support ext-idle-notify-v1.
class IdleMonitor : public QObject {
    Q_OBJECT

public:
    explicit IdleMonitor(const QList<int> &timeoutsMs, QObject *parent = nullptr);
    ~IdleMonitor() override;

    bool available() const {
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
        return m_dispatch.isActive();
#else
        return m_notifier != nullptr;
#endif
    }
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
