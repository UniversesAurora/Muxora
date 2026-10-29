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
        state.begin({first}, true, Motion::Presence, true, 0);
        state.advance(50'000'000);
        const auto firstDisplayed = state.pose(first, area).opacity;
        QVERIFY(firstDisplayed > 0 && firstDisplayed < 1);
        state.begin({second}, true, Motion::Presence, true, 50'000'000);
        QCOMPARE(state.pose(first, area).opacity, firstDisplayed);
        QCOMPARE(state.pose(second, area).opacity, 0.0);
        state.advance(110'000'000);
        const auto firstBeforeClosing = state.pose(first, area).opacity;
        const auto secondBeforeClosing = state.pose(second, area).opacity;
        state.begin({first}, false, Motion::Presence, true, 110'000'000);
        QCOMPARE(state.pose(first, area).opacity, firstBeforeClosing);
        QCOMPARE(state.pose(second, area).opacity, secondBeforeClosing);
        QCOMPARE(state.capturedVisibility(first), firstBeforeClosing);
        state.advance(1'000'000'000);
        QCOMPARE(state.pose(first, area).opacity, 0.0);
        state.finish();
        QVERIFY(!state.needsFrame());
        QVERIFY(state.pose(second, area).complete());
        QCOMPARE(state.pose(first, area).opacity, 0.0);
        state.remove(first); QVERIFY(state.pose(first, area).complete());
    }

    void planarEntriesCaptureWidthWithoutApplyingAnotherFade()
    {
        TabPresenceState state;
        const auto id = QUuid::createUuid(), absent = QUuid::createUuid();
        const QRect area(100, 200, 40, 32);
        state.begin({id}, true, Motion::Presence, true, 0); state.setPlanar({id, absent});
        QCOMPARE(state.pose(id, area).opacity, 0.0);
        state.advance(45'000'000);
        QVERIFY(state.pose(id, area).complete());
        QCOMPARE(state.capturedVisibility(id), 1.0);
        QVERIFY(state.pose(absent, area).complete());
        state.advance(1'000'000'000);
        state.finish(); QVERIFY(state.pose(id, area).complete());
    }

};

QTEST_APPLESS_MAIN(LabelPresenceTest)
#include "label_presence_test.moc"
