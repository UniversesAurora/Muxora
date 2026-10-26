// SPDX-License-Identifier: MIT
// Tab strip design: independent sections in one expansion surface.
// In-memory widgets only; no show(), native input or visual assertions.
#include "docking/tab_bar.hpp"
#include "style/tab_appearance_p.hpp"
#include <QTest>

using namespace window_system;

class MixedTabRowsTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { TabBar::setAnimationsEnabled(false); }
    void horizontalScrollingWinsOverSharedExpansion_data();
    void horizontalScrollingWinsOverSharedExpansion();
    void oneExpansionOpensBothCollapsingSections_data();
    void oneExpansionOpensBothCollapsingSections();
    void policiesRoundTripIndependently();
    void overflowingRowsNeedAFreshWheelGestureAtTheBoundary();
};

void MixedTabRowsTest::horizontalScrollingWinsOverSharedExpansion_data()
{
    QTest::addColumn<bool>("bottom"); QTest::addColumn<bool>("stickyWraps");
    for (bool bottom : {false, true}) for (bool stickyWraps : {false, true})
        QTest::newRow(qPrintable(QString("bottom-%1-sticky-wraps-%2").arg(bottom).arg(stickyWraps))) << bottom << stickyWraps;
}
void MixedTabRowsTest::horizontalScrollingWinsOverSharedExpansion()
{
    QFETCH(bool, bottom); QFETCH(bool, stickyWraps);
    TabBar bar(defaultTheme(), nullptr);
    bar.setAvailableSize({340, 500}); bar.resize(340, 500);
    for (int i = 0; i < 36; ++i) { const int n = bar.addTab({}, QString("Workspace %1").arg(i)); bar.setTabData(n, QUuid::createUuid()); }
    bar.setStickyCount(9);
    TabAppearance policy;
    policy.stickyTabsOnSeparateRow = true; policy.multiline = !stickyWraps; policy.stickyMultiline = stickyWraps;
    policy.showAllRows = policy.stickyShowAllRows = false; policy.wheelExpandRows = true;
    bar.setAppearance(policy, bottom ? TabBarPlacement::Bottom : TabBarPlacement::Top);
    int horizontal = -1;
    for (int i = 0; i < bar.tabLayout().laneList.size(); ++i) if (!bar.tabLayout().laneList[i].vertical) horizontal = i;
    QVERIFY(horizontal >= 0);
    const auto lane = bar.tabLayout().laneList[horizontal]; QVERIFY(lane.maximumOffset() > 0);
    QVERIFY(bar.scrollAt(lane.viewport.center() + bar.tabOrigin(), {0, -120}));
    QVERIFY(!bar.expanded()); QVERIFY(bar.tabLayout().laneList[horizontal].offset > lane.offset);
    const int preserved = bar.tabLayout().laneList[horizontal].offset;
    const int outer = bar.tabLayout().rowScrollLane; QVERIFY(outer >= 0);
    const auto bounds = bar.tabLayout().laneList[outer].viewport;
    const int y = stickyWraps ? bounds.top() + 2 : bounds.bottom() - 2;
    QVERIFY(bar.scrollAt(QPoint(bounds.left() + 3, y) + bar.tabOrigin(), {0, bottom ? 120 : -120}));
    QVERIFY(bar.expanded());
    QCOMPARE(bar.tabLayout().laneList[horizontal].offset, preserved);
}

void MixedTabRowsTest::oneExpansionOpensBothCollapsingSections_data()
{
    QTest::addColumn<bool>("overlay");
    QTest::newRow("push") << false; QTest::newRow("overlay") << true;
}
void MixedTabRowsTest::oneExpansionOpensBothCollapsingSections()
{
    QFETCH(bool, overlay);
    TabBar bar(defaultTheme(), nullptr);
    bar.setAvailableSize({340, 600}); bar.resize(340, 600);
    for (int i = 0; i < 36; ++i) { const int n = bar.addTab({}, QString("Workspace %1").arg(i)); bar.setTabData(n, QUuid::createUuid()); }
    bar.setStickyCount(9);
    TabAppearance policy;
    policy.stickyTabsOnSeparateRow = true; policy.multiline = policy.stickyMultiline = true;
    policy.showAllRows = policy.stickyShowAllRows = false;
    policy.rowExpansion = overlay ? TabRowExpansion::Overlay : TabRowExpansion::Push;
    bar.setAppearance(policy, TabBarPlacement::Top);
    const int closed = bar.reservedSize({340, 600}).height(); QVERIFY(bar.hiddenTabs() > 0);
    bar.setExpanded(true, false, false);
    QCOMPARE(bar.hiddenTabs(), 0);
    if (overlay) QCOMPARE(bar.reservedSize({340, 600}).height(), closed);
    else QVERIFY(bar.reservedSize({340, 600}).height() > closed);
    QVERIFY(bar.displaySize({340, 600}).height() > closed);
    bar.setExpanded(false, false, false);
    QVERIFY(bar.hiddenTabs() > 0); QCOMPARE(bar.reservedSize({340, 600}).height(), closed);
    bar.setDragExpanded(true);
    QCOMPARE(bar.hiddenTabs(), 0); QCOMPARE(bar.reservedSize({340, 600}).height(), closed);
    QVERIFY(bar.displaySize({340, 600}).height() > closed);
    bar.setDragExpanded(false); QCOMPARE(bar.reservedSize({340, 600}).height(), closed);
}

void MixedTabRowsTest::policiesRoundTripIndependently()
{
    TabAppearance policy;
    policy.multiline = true; policy.stickyMultiline = false;
    policy.showAllRows = false; policy.stickyShowAllRows = true;
    policy.rowExpansion = TabRowExpansion::Push;
    TabAppearance restored;
    QVERIFY(parseTabAppearance(tabAppearanceToJson(policy), restored));
    QVERIFY(restored == policy);
}

void MixedTabRowsTest::overflowingRowsNeedAFreshWheelGestureAtTheBoundary()
{
    ScrollBoundaryGuard guard(250);
    guard.observe(true, 1000);
    for (qint64 time : {1080, 1160, 1240, 1320}) QVERIFY(!guard.allowsExit(true, time));
    QVERIFY(guard.allowsExit(true, 1600));
    guard.observe(false, 1900); // Scrolling away disarms the boundary.
    QVERIFY(!guard.allowsExit(true, 2300));
    QVERIFY(guard.allowsExit(true, 2600));
    guard.reset();
    QVERIFY(guard.allowsExit(false, 0)); // No scroll range means no delay.
    guard.reset();
    QVERIFY(!guard.allowsExit(true, 0));
}

QTEST_MAIN(MixedTabRowsTest)
#include "mixed_tab_rows_test.moc"
