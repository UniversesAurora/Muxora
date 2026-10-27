// SPDX-License-Identifier: MIT
// Shared tab wheel contract without widgets, windows or elapsed-time waits.
// Dimensions: horizontal/vertical lanes, top/bottom, collapsed/expanded,
// independent sections, overflowing/nonoverflowing rows, boundary gestures.
#include "docking/tab_wheel_controller.hpp"

#include <QTest>

using namespace window_system;
using Action = TabWheelDecision::Action;

namespace {
TabLayout rows(int offset = 0, int extent = 240)
{
    TabLayout layout;
    layout.laneList.append({QRect(0, 0, 200, 80), extent, offset, true});
    layout.rowScrollLane = 0;
    layout.hidden = 6;
    return layout;
}

TabWheelPolicy expandingRows(bool bottom = false)
{
    TabWheelPolicy policy;
    policy.bottom = bottom;
    policy.collapsible = true;
    policy.wheelExpandRows = true;
    policy.expanded = true;
    return policy;
}

void compareScroll(const TabWheelDecision& decision, int lane, int delta, bool priority = false)
{
    QCOMPARE(decision.action, Action::Scroll);
    QCOMPARE(decision.lane, lane);
    QCOMPARE(decision.delta, delta);
    QCOMPARE(decision.horizontalPriority, priority);
}
} // namespace

class TabWheelControllerTest final : public QObject {
    Q_OBJECT
private slots:
    void laneSelection_preservesNestedPriorityAndNearestFallback()
    {
        auto layout = rows();
        layout.laneList.append({QRect(0, 20, 200, 20), 500, 0, false});
        QCOMPARE(TabWheelController::laneAt(layout, {20, 30}, false), 1);
        QCOMPARE(TabWheelController::laneAt(layout, {20, 30}, true), 0);
        QCOMPARE(TabWheelController::laneAt(layout, {-50, 30}, false), 0);
        QCOMPARE(TabWheelController::laneAt(layout, {20, -15}, false), 0);
        layout.laneList.append({QRect(0, 100, 200, 80), 240, 0, true});
        QCOMPARE(TabWheelController::laneAt(layout, {20, 98}, false), 2);
        QCOMPARE(TabWheelController::laneAt(layout, {20, 240}, true), 2);
        QCOMPARE(TabWheelController::laneAt({}, {20, 30}, false), -1);
    }

    void horizontalLane_ownsWheelEvenAtItsEnd_data()
    {
        QTest::addColumn<bool>("bottom");
        QTest::addColumn<bool>("expanded");
        QTest::addColumn<bool>("trailing");
        for (bool bottom : {false, true}) for (bool expanded : {false, true}) for (bool trailing : {false, true})
            QTest::newRow(qPrintable(QString("bottom-%1-expanded-%2-trailing-%3").arg(bottom).arg(expanded).arg(trailing)))
                << bottom << expanded << trailing;
    }
    void horizontalLane_ownsWheelEvenAtItsEnd()
    {
        QFETCH(bool, bottom); QFETCH(bool, expanded); QFETCH(bool, trailing);
        auto layout = rows();
        layout.laneList.append({QRect(0, 20, 200, 20), 500, trailing ? 300 : 0, false});
        auto policy = expandingRows(bottom);
        policy.expanded = expanded; policy.collapsed = !expanded;
        const int amount = trailing ? -40 : 40;
        TabWheelController controller;
        compareScroll(controller.route(layout, {20, 30}, {0, amount}, policy, 0), 1, -amount, true);
    }

    void laneAxis_prefersItsOwnWheelComponent_data()
    {
        QTest::addColumn<bool>("vertical");
        QTest::newRow("horizontal") << false;
        QTest::newRow("vertical") << true;
    }
    void laneAxis_prefersItsOwnWheelComponent()
    {
        QFETCH(bool, vertical);
        auto layout = rows(0, 80);
        layout.laneList[0].vertical = vertical;
        TabWheelPolicy policy; policy.vertical = vertical;
        TabWheelController controller;
        compareScroll(controller.route(layout, {20, 20}, {7, -30}, policy, 0), 0, vertical ? 30 : -7);
        compareScroll(controller.route(layout, {20, 20}, vertical ? QPoint(7, 0) : QPoint(0, -30), policy, 0),
            0, vertical ? -7 : 30);
    }

    void rowGestures_mirrorAtTheBottom_data()
    {
        QTest::addColumn<bool>("bottom");
        QTest::addColumn<bool>("horizontalDelta");
        for (bool bottom : {false, true}) for (bool horizontal : {false, true})
            QTest::newRow(qPrintable(QString("bottom-%1-horizontal-%2").arg(bottom).arg(horizontal))) << bottom << horizontal;
    }
    void rowGestures_mirrorAtTheBottom()
    {
        QFETCH(bool, bottom); QFETCH(bool, horizontalDelta);
        const auto layout = rows(0, 80);
        auto policy = expandingRows(bottom);
        policy.expanded = false; policy.collapsed = true;
        const int amount = bottom ? 40 : -40;
        const auto delta = horizontalDelta ? QPoint(amount, 0) : QPoint(0, amount);
        TabWheelController controller;
        QCOMPARE(controller.route(layout, {20, 20}, delta, policy, 0).action, Action::Expand);
        policy.expanded = true; policy.collapsed = false;
        QCOMPARE(controller.route(layout, {20, 20}, -delta, policy, 1).action, Action::Collapse);
    }

    void expansion_requiresHiddenRowsAndTheSetting()
    {
        auto layout = rows(0, 80);
        auto policy = expandingRows();
        policy.expanded = false; policy.collapsed = true;
        TabWheelController controller;
        policy.wheelExpandRows = false;
        compareScroll(controller.route(layout, {20, 20}, {0, -40}, policy, 0), 0, 40);
        policy.wheelExpandRows = true; policy.collapsible = false;
        compareScroll(controller.route(layout, {20, 20}, {0, -40}, policy, 1), 0, 40);
        policy.collapsible = true; layout.hidden = 0;
        compareScroll(controller.route(layout, {20, 20}, {0, -40}, policy, 2), 0, 40);
        layout.hidden = 6;
        QCOMPARE(controller.route(layout, {20, 20}, {0, -40}, policy, 3).action, Action::Expand);
    }

    void overflowingRows_requireAFreshGestureAtTheirCollapseEdge_data()
    {
        QTest::addColumn<bool>("bottom");
        QTest::newRow("top") << false;
        QTest::newRow("bottom") << true;
    }
    void overflowingRows_requireAFreshGestureAtTheirCollapseEdge()
    {
        QFETCH(bool, bottom);
        auto layout = rows();
        layout.laneList[0].offset = bottom ? layout.laneList[0].maximumOffset() : 0;
        const auto policy = expandingRows(bottom);
        const QPoint delta(0, bottom ? -40 : 40);
        TabWheelController controller;
        controller.observeRowScroll(layout, 0, true, bottom, 1000);
        for (qint64 time : {1080, 1160, 1240, 1320})
            QCOMPARE(controller.route(layout, {20, 20}, delta, policy, time).action, Action::HoldAtBoundary);
        QCOMPARE(controller.route(layout, {20, 20}, delta, policy, 1600).action, Action::Collapse);
        layout.laneList[0].offset = 20;
        controller.observeRowScroll(layout, 0, true, bottom, 1900);
        compareScroll(controller.route(layout, {20, 20}, delta, policy, 2300), 0, -delta.y());
        layout.laneList[0].offset = bottom ? layout.laneList[0].maximumOffset() : 0;
        QCOMPARE(controller.route(layout, {20, 20}, delta, policy, 2400).action, Action::HoldAtBoundary);
        QCOMPARE(controller.route(layout, {20, 20}, delta, policy, 2700).action, Action::Collapse);
    }

    void scrollFrames_rearmTheBoundaryAtTheirActualPosition()
    {
        auto layout = rows(50);
        const auto policy = expandingRows();
        TabWheelController controller;
        controller.observeRowScroll(layout, 0, true, false, 1000);
        layout.laneList[0].offset = 0;
        controller.observeRowScroll(layout, 0, true, false, 1200);
        QCOMPARE(controller.route(layout, {20, 20}, {0, 40}, policy, 1300).action, Action::HoldAtBoundary);
        QCOMPARE(controller.route(layout, {20, 20}, {0, 40}, policy, 1600).action, Action::Collapse);
    }

    void unrelatedScrolls_doNotChangeTheRowBoundary()
    {
        auto layout = rows();
        layout.laneList.append({QRect(0, 90, 200, 28), 500, 0, false});
        const auto policy = expandingRows();
        TabWheelController controller;
        controller.observeRowScroll(layout, 0, true, false, 1000);
        controller.observeRowScroll(layout, 1, true, false, 1200);
        controller.observeRowScroll(layout, 0, false, false, 1200);
        QCOMPARE(controller.route(layout, {20, 20}, {0, 40}, policy, 1250).action, Action::Collapse);
    }

    void reset_requiresAnotherBoundaryGestureOnlyForScrollableRows()
    {
        auto layout = rows();
        const auto policy = expandingRows();
        TabWheelController controller;
        controller.observeRowScroll(layout, 0, true, false, 1000);
        controller.reset();
        QCOMPARE(controller.route(layout, {20, 20}, {0, 40}, policy, 1300).action, Action::HoldAtBoundary);
        QCOMPARE(controller.route(layout, {20, 20}, {0, 40}, policy, 1600).action, Action::Collapse);
        controller.reset();
        layout.laneList[0].extent = layout.laneList[0].viewport.height();
        QCOMPARE(controller.route(layout, {20, 20}, {0, 40}, policy, 1700).action, Action::Collapse);
    }

    void emptyAndZeroInput_haveNoScrollAction()
    {
        TabWheelController controller;
        QCOMPARE(controller.route({}, {}, {0, 40}, {}, 0).action, Action::Ignore);
        auto layout = rows();
        const auto policy = expandingRows();
        controller.observeRowScroll(layout, 0, true, false, 1000);
        QCOMPARE(controller.route(layout, {20, 20}, {}, policy, 1200).action, Action::Ignore);
        QCOMPARE(controller.route(layout, {20, 20}, {0, 40}, policy, 1250).action, Action::Collapse);
    }
};

QTEST_APPLESS_MAIN(TabWheelControllerTest)
#include "tab_wheel_controller_test.moc"
