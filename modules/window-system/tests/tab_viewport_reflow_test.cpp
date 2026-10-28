// SPDX-License-Identifier: MIT
// Tab strip design 3.1: retain displayed anchors through geometry changes.
// Dimensions: window/group edges, active visibility, frozen prefix,
// unshown/hidden geometry, changed active ID, horizontal/independent lanes.
#include "docking/tab_viewport_state.hpp"
#include <QTest>

using namespace window_system;

namespace {
TabViewportState shownState()
{
    return {{100, 0, 400, 30}, {100, 100, 1000, 700}, 200, 31, 47, TabId::createUuid()};
}
}

class TabViewportReflowTest final : public QObject {
    Q_OBJECT
private slots:
    void geometryChangesPreserveTheDisplayedAnchor_data()
    {
        QTest::addColumn<QRect>("window"); QTest::addColumn<QRect>("bounds");
        QTest::addColumn<bool>("activeVisible"); QTest::addColumn<QRect>("activeArea");
        QTest::addColumn<int>("inset"); QTest::addColumn<int>("expected");
        const QRect window(100, 100, 1000, 700), active(400, 0, 100, 30);
        QTest::newRow("desktop-move") << QRect(180, 130, 1000, 700) << QRect(100, 0, 400, 30) << false << QRect{} << 0 << 200;
        QTest::newRow("height-only") << QRect(100, 100, 1000, 500) << QRect(100, 0, 400, 30) << false << QRect{} << 0 << 200;
        QTest::newRow("right-window-shrink") << QRect(100, 100, 900, 700) << QRect(100, 0, 300, 30) << false << QRect{} << 0 << 200;
        QTest::newRow("left-window-shrink") << QRect(180, 100, 920, 700) << QRect(100, 0, 320, 30) << false << QRect{} << 0 << 280;
        QTest::newRow("window-and-group-move") << QRect(180, 100, 920, 700) << QRect(130, 0, 280, 30) << false << QRect{} << 0 << 310;
        QTest::newRow("expansion-defers-content-clamping") << window << QRect(-160, 0, 660, 30) << false << QRect{} << 0 << -60;
        QTest::newRow("active-before-right-edge") << window << QRect(100, 0, 340, 30) << true << active << 0 << 200;
        QTest::newRow("active-touches-right-edge") << window << QRect(100, 0, 300, 30) << true << active << 0 << 200;
        QTest::newRow("active-reaches-right-edge") << window << QRect(100, 0, 280, 30) << true << active << 0 << 220;
        QTest::newRow("active-reaches-left-edge") << window << QRect(330, 0, 170, 30) << true << active << 0 << 400;
        QTest::newRow("both-edges-shrink") << window << QRect(140, 0, 240, 30) << true << active << 0 << 260;
        QTest::newRow("reverse-expansion") << window << QRect(60, 0, 450, 30) << true << active << 0 << 160;
        QTest::newRow("oversized-active-shows-start") << window << QRect(100, 0, 80, 30) << true << active << 0 << 400;
        QTest::newRow("frozen-prefix-shows-active-start") << window << QRect(100, 0, 160, 30) << true << active << 80 << 320;
    }
    void geometryChangesPreserveTheDisplayedAnchor()
    {
        QFETCH(QRect, window); QFETCH(QRect, bounds); QFETCH(bool, activeVisible);
        QFETCH(QRect, activeArea); QFETCH(int, inset); QFETCH(int, expected);
        auto old = shownState(); old.activeFullyVisible = activeVisible;
        auto current = old; current.bounds = bounds; current.windowClientBounds = window;
        const auto result = tabViewportReflow(old, current, true, true, activeArea, inset);
        QVERIFY(result.ready); QVERIFY(result.restore);
        QCOMPARE(result.offset, expected);
        QCOMPARE(result.stickyOffset, old.stickyOffset); QCOMPARE(result.rowsOffset, old.rowsOffset);
        QCOMPARE(result.ensureActive, activeVisible);
    }

    void unshownGeometryWaitsWithoutInventingAnAnchor()
    {
        auto old = shownState(), current = old;
        current.bounds = {};
        auto result = tabViewportReflow(old, current, true, true, {}, 0);
        QVERIFY(!result.ready); QVERIFY(!result.restore); QVERIFY(!result.ensureActive);
        result = tabViewportReflow(old, current, true, false, {}, 0);
        QVERIFY(result.ready); QVERIFY(!result.restore);
        old.bounds = {}; current = shownState(); current.activeTab = old.activeTab;
        result = tabViewportReflow(old, current, true, true, {}, 0);
        QVERIFY(result.ready); QVERIFY(result.restore); QVERIFY(!result.ensureActive);
        QCOMPARE(result.offset, old.offset);
        current.activeTab = TabId::createUuid();
        QVERIFY(tabViewportReflow(old, current, true, true, {}, 0).ensureActive);
        old.activeTab = {};
        QVERIFY(!tabViewportReflow(old, current, true, true, {}, 0).ensureActive);
    }

    void independentLanesRestoreAllOffsetsBeforeRevealingTheActiveTab()
    {
        // Vertical sections and multiline rows share the nonhorizontal path.
        for (int activeState : {0, 1, 2}) {
            auto old = shownState(); old.activeFullyVisible = activeState == 1;
            auto current = old;
            current.bounds = {160, 0, 200, 150}; current.windowClientBounds = {180, 100, 600, 700};
            current.offset = current.stickyOffset = current.rowsOffset = 0;
            if (activeState == 2) current.activeTab = TabId::createUuid();
            const auto result = tabViewportReflow(old, current, false, true, {400, 0, 100, 30}, 80);
            QVERIFY(result.ready); QVERIFY(result.restore);
            QCOMPARE(result.offset, 200); QCOMPARE(result.stickyOffset, 31); QCOMPARE(result.rowsOffset, 47);
            QCOMPARE(result.ensureActive, activeState != 0);
        }
    }
};

QTEST_APPLESS_MAIN(TabViewportReflowTest)
#include "tab_viewport_reflow_test.moc"
