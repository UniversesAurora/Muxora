// SPDX-License-Identifier: MIT
#include "benchmark_support.hpp"

#include <QTest>

using namespace window_system::benchmark;

class BenchmarkSupportTest final : public QObject {
    Q_OBJECT
private slots:
    void nearestRankPercentiles()
    {
        QVector<double> values;
        for (int value = 100; value >= 1; --value) values.append(value);
        const auto summary = summarize(values);
        QCOMPARE(summary.count, 100);
        QCOMPARE(summary.p50, 50.0);
        QCOMPARE(summary.p95, 95.0);
        QCOMPARE(summary.max, 100.0);
        QCOMPARE(summary.mean, 50.5);
    }

    void emptyInput()
    {
        const auto summary = summarize({});
        QCOMPARE(summary.count, 0);
        QCOMPARE(summary.p50, 0.0);
        QCOMPARE(summary.p95, 0.0);
        QCOMPARE(summary.max, 0.0);
        QCOMPARE(summary.mean, 0.0);
    }
};

QTEST_APPLESS_MAIN(BenchmarkSupportTest)
#include "benchmark_support_test.moc"
