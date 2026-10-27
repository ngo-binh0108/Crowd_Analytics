#pragma once

#include <QtGlobal>

// Samples machine CPU load and this process's memory use (Windows).
class SystemMonitor {
public:
    void sample();
    double cpuPercent() const { return m_cpu; }
    double processMemoryMB() const { return m_procMemMB; }
    double systemMemoryPercent() const { return m_sysMemPct; }

private:
    quint64 m_prevIdle = 0;
    quint64 m_prevTotal = 0;
    double m_cpu = 0.0;
    double m_procMemMB = 0.0;
    double m_sysMemPct = 0.0;
};
