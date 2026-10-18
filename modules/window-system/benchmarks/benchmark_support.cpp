// SPDX-License-Identifier: MIT
#include "benchmark_support.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QEvent>

#include <algorithm>
#include <numeric>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <psapi.h>
#endif

namespace window_system::benchmark {

namespace {

// Integer arithmetic keeps ceil(percent / 100 * n) exact for every n.
double nearestRank(const QVector<double>& sorted, int percent)
{
    const qsizetype rank = (qsizetype(percent) * sorted.size() + 99) / 100;
    return sorted.at(std::max<qsizetype>(rank, 1) - 1);
}

} // namespace

Summary summarize(QVector<double> values)
{
    Summary result;
    if (values.isEmpty()) return result;
    std::sort(values.begin(), values.end());
    result.count = int(values.size());
    result.p50 = nearestRank(values, 50);
    result.p95 = nearestRank(values, 95);
    result.max = values.last();
    result.mean = std::accumulate(values.cbegin(), values.cend(), 0.0) / double(values.size());
    return result;
}

quint64 privateBytes()
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    if (GetProcessMemoryInfo(GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)))
        return quint64(memory.PrivateUsage);
#endif
    return 0;
}

void drainDeferredDeletes()
{
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}

int liveWidgetCount()
{
    return int(QApplication::allWidgets().size());
}

} // namespace window_system::benchmark
