// IdleMonitor no Windows: o tempo desde a última entrada (teclado, mouse) vem
// do GetLastInputInfo, consultado de tempos em tempos. Mesma interface da
// versão Wayland (idlemonitor.cpp), que não existe aqui.
#include "idlemonitor.h"

#include <windows.h>

IdleMonitor::IdleMonitor(const QList<int> &timeoutsMs, QObject *parent) : QObject(parent) {
    QList<int> timeouts;
    for (int timeout : timeoutsMs) {
        // 0 desliga o degrau, como na versão Wayland.
        if (timeout <= 0)
            continue;
        timeouts << timeout;
        m_idle << false;
    }
    if (timeouts.isEmpty())
        return;

    // Os tempos são de minutos; uns segundos de atraso não importam.
    m_dispatch.setInterval(5000);
    connect(&m_dispatch, &QTimer::timeout, this, [this, timeouts] {
        LASTINPUTINFO info{sizeof(LASTINPUTINFO), 0};
        if (!GetLastInputInfo(&info))
            return;
        const DWORD idleMs = GetTickCount() - info.dwTime;
        for (int i = 0; i < timeouts.size(); ++i)
            setIdle(i, idleMs >= DWORD(timeouts.at(i)));
    });
    m_dispatch.start();
}

IdleMonitor::~IdleMonitor() = default;

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
