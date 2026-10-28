// SPDX-License-Identifier: MIT
// Shared motion contract at window frame cadence, without a window or timer.
// Dimensions: every existing curve, 60/144/240 Hz samples, interruption,
// proportional surface duration, repeated/backward time, disabled and idle.
#include "style/frame_motion.hpp"
#include "docking/tab_rows_state.hpp"

#include <QTest>
#include <array>
#include <limits>

using namespace window_system;

namespace {
constexpr qint64 ms(int value) { return qint64(value) * 1'000'000; }

const std::array motions{
    Motion::Relocate, Motion::Reveal, Motion::Presence, Motion::Hover,
    Motion::SurfaceLift, Motion::SurfaceLower, Motion::SurfaceChangeLift, Motion::SurfaceChangeLower
};

void motionRows()
{
    QTest::addColumn<int>("motion");
    for (const auto motion : motions)
        QTest::newRow(qPrintable(QString::number(int(motion)))) << int(motion);
}

void compareValue(qreal actual, qreal expected)
{
    QVERIFY2(qAbs(actual - expected) <= 1e-12,
        qPrintable(QStringLiteral("actual %1, expected %2").arg(actual, 0, 'g', 16).arg(expected, 0, 'g', 16)));
}
} // namespace

class FrameMotionTest final : public QObject {
    Q_OBJECT
private slots:
    void rowSession_preservesTheSharedExpansionAcrossInputSources()
    {
        TabRowsState rows; const auto duration = ms(motionDuration(Motion::Reveal)), quarter = duration / 4;
        rows.setExpanded(true, true); QVERIFY(rows.collapseOnLeave());
        QVERIFY(rows.begin(true, 0)); rows.advance(quarter);
        const auto displayed = rows.value(); QVERIFY(displayed > 0 && displayed < 1);
        rows.setDragExpanded(true, true);
        QVERIFY(!rows.begin(true, quarter)); // The same open target keeps its clock.
        rows.setExpanded(false, false); QVERIFY(rows.open());
        QVERIFY(!rows.collapseOnLeave()); QVERIFY(!rows.begin(true, quarter));
        rows.advance(duration); rows.finish();
        QCOMPARE(rows.value(), qreal(1)); QVERIFY(!rows.needsFrame());
        rows.setDragExpanded(false, true); QVERIFY(rows.begin(true, duration + quarter));
        rows.advance(duration + 2 * quarter); const auto closing = rows.value();
        rows.setExpanded(true, false); QVERIFY(rows.begin(true, duration + 2 * quarter));
        QCOMPARE(rows.value(), closing); // Reversal starts at the last sample.
        rows.resetArrangement(); QVERIFY(!rows.open()); QVERIFY(!rows.needsFrame());
        rows.setDragExpanded(true, true); QVERIFY(rows.temporaryOverlay());
        QVERIFY(rows.watchesOutsideInput(true, false));
        QVERIFY(rows.begin(true, duration + 3 * quarter));
        rows.setDragExpanded(false, true); rows.begin(true, duration + 3 * quarter);
        QVERIFY(!rows.needsFrame()); QVERIFY(!rows.temporaryOverlay());
    }

    void sampling_usesTheExistingCurve_data() { motionRows(); }
    void sampling_usesTheExistingCurve()
    {
        QFETCH(int, motion);
        const auto kind = Motion(motion);
        FrameMotion value;
        value.animateTo(1, kind, true, ms(1000));
        QVERIFY(value.needsFrame());
        const auto curve = motionCurve(kind);
        const auto duration = ms(motionDuration(kind));
        for (int quarter = 0; quarter <= 4; ++quarter) {
            value.advance(ms(1000) + duration * quarter / 4);
            compareValue(value.value(), curve.valueForProgress(qreal(quarter) / 4));
        }
        QCOMPARE(value.value(), qreal(1));
        QVERIFY(!value.needsFrame());
    }

    void refreshCadence_preservesTiming_data()
    {
        QTest::addColumn<int>("motion");
        QTest::addColumn<int>("hz");
        for (const auto motion : motions)
            for (const int hz : {60, 144, 240})
                QTest::newRow(qPrintable(QStringLiteral("%1-%2hz").arg(int(motion)).arg(hz))) << int(motion) << hz;
    }
    void refreshCadence_preservesTiming()
    {
        QFETCH(int, motion);
        QFETCH(int, hz);
        const auto kind = Motion(motion);
        FrameMotion value;
        value.animateTo(1, kind, true, 0);
        const auto duration = ms(motionDuration(kind));
        qint64 frame = 1;
        for (int quarter = 1; quarter <= 4; ++quarter) {
            const auto checkpoint = duration * quarter / 4;
            while (frame * 1'000'000'000 / hz < checkpoint) {
                value.advance(frame * 1'000'000'000 / hz);
                ++frame;
            }
            value.advance(checkpoint);
            compareValue(value.value(), motionCurve(kind).valueForProgress(qreal(quarter) / 4));
        }
        QCOMPARE(value.value(), qreal(1));
        QVERIFY(!value.needsFrame());
    }

    void reverse_startsAtTheDisplayedValue()
    {
        FrameMotion value;
        value.animateTo(1, Motion::SurfaceLift, true, 0);
        value.advance(ms(60));
        const qreal displayed = value.value();
        QVERIFY(displayed > 0 && displayed < 1);
        value.animateTo(0, Motion::SurfaceLower, true, ms(80));
        QCOMPARE(value.value(), displayed);
        const auto duration = ms(qRound(motionDuration(Motion::SurfaceLower) * displayed));
        value.advance(ms(80) + duration / 2);
        compareValue(value.value(), displayed * (1 - motionCurve(Motion::SurfaceLower).valueForProgress(0.5)));
        value.advance(ms(80) + duration);
        QCOMPARE(value.value(), qreal(0));
        QVERIFY(!value.needsFrame());
    }

    void redirectedTarget_startsAtTheDisplayedValue()
    {
        FrameMotion value;
        value.animateTo(1, Motion::Reveal, true, 0);
        value.advance(ms(50));
        const qreal displayed = value.value();
        value.animateTo(2, Motion::Reveal, true, ms(75));
        QCOMPARE(value.value(), displayed);
        value.advance(ms(175));
        compareValue(value.value(), displayed + (2 - displayed) * motionCurve(Motion::Reveal).valueForProgress(0.5));
        value.advance(ms(275));
        QCOMPARE(value.value(), qreal(2));
        QVERIFY(!value.needsFrame());
    }

    void repeatedTarget_doesNotRestart()
    {
        FrameMotion value;
        value.animateTo(1, Motion::Reveal, true, 0);
        value.advance(ms(100));
        const auto displayed = value.value();
        value.animateTo(1, Motion::Reveal, true, ms(150));
        QCOMPARE(value.value(), displayed);
        value.advance(ms(200));
        QCOMPARE(value.value(), qreal(1));
        QVERIFY(!value.needsFrame());
    }

    void backwardTime_cannotReverseOrBackdate()
    {
        FrameMotion value;
        value.animateTo(1, Motion::Reveal, true, 0);
        value.advance(ms(100));
        const auto displayed = value.value();
        QVERIFY(!value.advance(ms(50)));
        QCOMPARE(value.value(), displayed);
        value.animateTo(0, Motion::Hover, true, ms(20));
        QVERIFY(!value.advance(ms(80)));
        QCOMPARE(value.value(), displayed);
        value.advance(ms(150));
        compareValue(value.value(), displayed * (1 - motionCurve(Motion::Hover).valueForProgress(0.5)));
        value.advance(ms(200));
        QCOMPARE(value.value(), qreal(0));
        QVERIFY(!value.needsFrame());
    }

    void disabledMotion_finishesEvenAnExistingTarget()
    {
        FrameMotion value;
        value.animateTo(1, Motion::SurfaceLift, true, 0);
        value.advance(ms(40));
        value.animateTo(1, Motion::SurfaceLift, false, ms(50));
        QCOMPARE(value.value(), qreal(1));
        QVERIFY(!value.needsFrame());
        QVERIFY(!value.advance(ms(100)));
        value.animateTo(-2, Motion::Presence, false, ms(110));
        QCOMPARE(value.value(), qreal(-2));
        QVERIFY(!value.needsFrame());
    }

    void idleAndJump_doNotRequestFrames()
    {
        FrameMotion value(0.5);
        QVERIFY(!value.needsFrame());
        QVERIFY(!value.advance(0));
        value.animateTo(0.5, Motion::Reveal, true, ms(1));
        QVERIFY(!value.needsFrame());
        value.animateTo(1, Motion::Reveal, true, ms(2));
        QVERIFY(value.needsFrame());
        value.jump(0.25);
        QCOMPARE(value.value(), qreal(0.25));
        QCOMPARE(value.target(), qreal(0.25));
        QVERIFY(!value.needsFrame());
        QVERIFY(!value.advance(ms(1000)));
    }

    void surfaceDuration_respectsDistanceAndScale()
    {
        FrameMotion value(0.25);
        value.animateTo(0.75, Motion::SurfaceLower, true, 0, 0.5);
        value.advance(ms(25));
        compareValue(value.value(), 0.25 + 0.5 * motionCurve(Motion::SurfaceLower).valueForProgress(0.5));
        value.advance(ms(50));
        QCOMPARE(value.value(), qreal(0.75));
        QVERIFY(!value.needsFrame());

        value.jump(-1);
        value.animateTo(1, Motion::SurfaceLift, true, ms(100));
        value.advance(ms(340));
        QCOMPARE(value.value(), qreal(1));
        QVERIFY(!value.needsFrame());

        value.animateTo(0, Motion::SurfaceLower, true, ms(400), 0);
        value.advance(ms(401));
        QCOMPARE(value.value(), qreal(0));
        QVERIFY(!value.needsFrame());
    }

    void ordinaryDuration_doesNotDependOnDistance()
    {
        FrameMotion value(0.99);
        value.animateTo(1, Motion::Reveal, true, 0, 0);
        value.advance(ms(100));
        compareValue(value.value(), 0.99 + 0.01 * motionCurve(Motion::Reveal).valueForProgress(0.5));
        QVERIFY(value.needsFrame());
        value.advance(ms(200));
        QCOMPARE(value.value(), qreal(1));
        QVERIFY(!value.needsFrame());
    }

    void largeClock_keepsNanosecondResolution()
    {
        FrameMotion value;
        constexpr qint64 start = std::numeric_limits<qint64>::max() - 2'000'000'000;
        value.animateTo(1, Motion::Hover, true, start);
        value.advance(start + 1);
        compareValue(value.value(), motionCurve(Motion::Hover).valueForProgress(1.0 / ms(100)));
        value.advance(start + ms(100));
        QCOMPARE(value.value(), qreal(1));
        QVERIFY(!value.needsFrame());
    }
};

QTEST_APPLESS_MAIN(FrameMotionTest)
#include "frame_motion_test.moc"
