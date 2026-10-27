#include "SystemMonitor.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>

static quint64 toU64(const FILETIME& ft)
{
    return (quint64(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}
#endif

void SystemMonitor::sample()
{
#ifdef Q_OS_WIN
    FILETIME idle, kernel, user;
    if (GetSystemTimes(&idle, &kernel, &user)) {
        const quint64 i = toU64(idle);
        const quint64 total = toU64(kernel) + toU64(user);   // kernel time includes idle time
        if (m_prevTotal > 0 && total > m_prevTotal) {
            const double dTotal = double(total - m_prevTotal);
            const double dIdle = double(i - m_prevIdle);
            m_cpu = qBound(0.0, 100.0 * (1.0 - dIdle / dTotal), 100.0);
        }
        m_prevIdle = i;
        m_prevTotal = total;
    }

    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        m_procMemMB = pmc.WorkingSetSize / (1024.0 * 1024.0);

    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms))
        m_sysMemPct = ms.dwMemoryLoad;
#endif
}
