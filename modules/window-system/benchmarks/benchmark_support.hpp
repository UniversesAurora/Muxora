// SPDX-License-Identifier: MIT
#pragma once

#include <QVector>
#include <QtGlobal>

namespace window_system::benchmark {

struct Samples {
    QVector<double> ms;
};

struct Summary {
    double p50 = 0;
    double p95 = 0;
    double max = 0;
    double mean = 0;
    int count = 0;
};

// Percentiles use the nearest-rank method on the sorted samples:
// index = ceil(p * n) - 1, without interpolation. Empty input yields count 0.
Summary summarize(QVector<double> values);

// Process private commit (PROCESS_MEMORY_COUNTERS_EX::PrivateUsage) on
// Windows; other platforms return 0.
quint64 privateBytes();

// Deliver pending deleteLater() requests, then let the event loop settle.
void drainDeferredDeletes();

// Number of QWidget instances alive in the application.
int liveWidgetCount();

} // namespace window_system::benchmark
