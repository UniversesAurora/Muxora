// SPDX-License-Identifier: MIT
#include "docking/tab_position_motion.hpp"
#include <QTest>

using namespace window_system;

class TabPositionMotionTest final : public QObject {
    Q_OBJECT
private slots:
    void frameSamplesAndRetargetingPreserveTheDisplayedRectangle()
    {
        TabPositionMotion motion; const auto id = QUuid::createUuid();
        const QRect from(0, 0, 100, 28), target(10000, 100, 200, 28);
        motion.slide(id, from, true); motion.setMotion(Motion::Relocate);
        QCOMPARE(motion.rect(id, target, {}), target);
        const qint64 start = 10'000'000;
        motion.start(start); QVERIFY(motion.needsFrame());
        QCOMPARE(motion.rect(id, target, {}), from);
        motion.advance(start + 250'000); const auto first = motion.rect(id, target, {});
        motion.advance(start + 500'000); const auto second = motion.rect(id, target, {});
        QVERIFY(first.left() > from.left()); QVERIFY(second.left() > first.left());
        motion.setFrom(id, second); motion.start(start + 500'000);
        const QRect redirected(-100, 40, 150, 40);
        QCOMPARE(motion.rect(id, redirected, {}), second);
        motion.advance(start + 500'000 + qint64(motionDuration(Motion::Relocate)) * 1'000'000);
        QCOMPARE(motion.rect(id, redirected, {}), redirected); QVERIFY(!motion.needsFrame());
        motion.clear(); QVERIFY(motion.isEmpty()); QVERIFY(motion.settling().isEmpty());
    }

    void settlingAndPlanarReflowKeepTheirOwnWidthRules()
    {
        TabPositionMotion motion;
        const auto dropped = QUuid::createUuid(), row = QUuid::createUuid(), tile = QUuid::createUuid();
        const QRect from(0, 0, 100, 28), target(100, 50, 200, 28);
        motion.settle(dropped, 3, from); motion.slide(row, from, false); motion.slide(tile, from, true);
        motion.setMotion(Motion::SurfaceLower); motion.start(0);
        QCOMPARE(motion.settling().value(dropped), 3); QVERIFY(motion.resizesWidth(dropped));
        QCOMPARE(motion.rect(dropped, target, {true, true, false}, 0.5), QRect(50, 25, 150, 28));
        QCOMPARE(motion.rect(row, target, {true, false, false}, 0.5), QRect(50, 25, 200, 28));
        QCOMPARE(motion.rect(tile, target, {true, true, true}, 0.5), QRect(50, 25, 150, 28));
        motion.advance(qint64(motionDuration(Motion::SurfaceLower)) * 1'000'000);
        QVERIFY(!motion.needsFrame()); QCOMPARE(motion.rect(dropped, target, {}), target);
    }
};

QTEST_APPLESS_MAIN(TabPositionMotionTest)
#include "tab_position_motion_test.moc"
