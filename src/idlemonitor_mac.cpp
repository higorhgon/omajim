#include "idlemonitor.h"

#include <CoreGraphics/CoreGraphics.h>

IdleMonitor::IdleMonitor(const QList<int> &timeoutsMs, QObject *parent) : QObject(parent) {
    QList<int> timeouts;
    for (int timeout : timeoutsMs) {
        if (timeout > 0)
            timeouts << timeout;
    }
    if (timeouts.isEmpty())
        return;

    m_dispatch.setInterval(5000);
    connect(&m_dispatch, &QTimer::timeout, this, [this, timeouts] {
        const double idleMs = 1000 * CGEventSourceSecondsSinceLastEventType(
            kCGEventSourceStateCombinedSessionState, kCGAnyInputEventType);
        int level = 0;
        for (int timeout : timeouts)
            level += idleMs >= timeout;
        if (level != m_level) {
            m_level = level;
            emit levelChanged(level);
        }
    });
    m_dispatch.start();
}

IdleMonitor::~IdleMonitor() = default;
