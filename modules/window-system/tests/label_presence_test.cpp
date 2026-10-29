// SPDX-License-Identifier: MIT
// Shared label redirection and planar presence, without a window.
#include "docking/tab_presence_state.hpp"
#include <QTest>

using namespace window_system;

class LabelPresenceTest final : public QObject {
    Q_OBJECT
private slots:
    void redirectsTheSharedClockWithoutJumpingOtherLabels()
    {
        TabPresenceState state;
        const auto first = QUuid::createUuid(), second = QUuid::createUuid();
        const QRect area(0, 0, 200, 32);
        state.retarget({first}, true, 1);
        QCOMPARE(state.pose(first, area, 0.4).opacity, 0.4);
        state.retarget({second}, true, 0.4);
        QCOMPARE(state.pose(first, area, 0).opacity, 0.4);
        QCOMPARE(state.pose(second, area, 0).opacity, 0.0);
        QCOMPARE(state.pose(first, area, 0.5).opacity, 0.7);
        state.retarget({first}, false, 0.5);
        QCOMPARE(state.pose(first, area, 0).opacity, 0.7);
        QCOMPARE(state.pose(second, area, 0).opacity, 0.5);
        QCOMPARE(state.capturedVisibility(first, 0), 0.7);
        QCOMPARE(state.pose(first, area, 1).opacity, 0.0);
        state.finish();
        QVERIFY(state.pose(second, area, 0).complete());
        QCOMPARE(state.pose(first, area, 1).opacity, 0.0);
        state.remove(first); QVERIFY(state.pose(first, area, 0).complete());
    }

    void planarEntriesCaptureWidthWithoutApplyingAnotherFade()
    {
        TabPresenceState state;
        const auto id = QUuid::createUuid(), absent = QUuid::createUuid();
        const QRect area(100, 200, 40, 32);
        state.retarget({id}, true, 1); state.setPlanar({id, absent});
        QCOMPARE(state.pose(id, area, 0).opacity, 0.0);
        const auto shown = state.pose(id, area, 0.25);
        QVERIFY(shown.complete()); QCOMPARE(shown.mapRect(area), QRectF(area));
        QCOMPARE(state.capturedVisibility(id, 0.25), 1.0);
        QVERIFY(state.pose(absent, area, 0).complete());
        state.finish(); QVERIFY(state.pose(id, area, 0).complete());
    }

};

QTEST_APPLESS_MAIN(LabelPresenceTest)
#include "label_presence_test.moc"
