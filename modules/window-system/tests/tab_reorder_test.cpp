// SPDX-License-Identifier: MIT
// Stable-gap edge thresholds, without widgets, clocks or animated geometry.
#include "docking/tab_layout.hpp"
#include <QTest>

using namespace window_system;

namespace {
TabLayoutInput strip(bool vertical = false)
{
    TabLayoutInput input;
    input.orientation = vertical ? TabLayoutInput::Orientation::Vertical : TabLayoutInput::Orientation::Horizontal;
    input.width = vertical ? 180 : 800; input.height = 400;
    input.rowHeight = 30; input.rowGap = 2; input.inset = 4;
    input.extents = {100, 100, 100, 100};
    input.removed = 1; input.insertion = 1; input.block = {100};
    return input;
}
QRect gapBounds(const TabLayout& layout)
{
    QRect result;
    for (const auto& cell : layout.gap) result |= cell;
    return result;
}
void compareInsertion(TabInsertion actual, TabInsertion expected)
{
    QCOMPARE(actual.index, expected.index); QCOMPARE(actual.sticky, expected.sticky); QCOMPARE(actual.folder, expected.folder);
}
}

class TabReorderTest final : public QObject {
    Q_OBJECT
private slots:
    void symmetricEdges_data()
    {
        QTest::addColumn<bool>("vertical");
        QTest::addColumn<bool>("window");
        QTest::newRow("horizontal-tab") << false << false;
        QTest::newRow("vertical-tab") << true << false;
        QTest::newRow("horizontal-window") << false << true;
        QTest::newRow("vertical-window") << true << true;
    }
    void symmetricEdges()
    {
        QFETCH(bool, vertical); QFETCH(bool, window);
        auto input = strip(vertical);
        if (window) input.carried = TabLayoutInput::Carried::Tabs;
        const auto layout = layoutTabs(input);
        auto carried = gapBounds(layout);
        const auto neighbor = layout.tabs.at(2);
        if (vertical) carried.moveBottom(neighbor.center().y()); else carried.moveRight(neighbor.center().x());
        compareInsertion(tabReorderAt(input, layout, carried, QPoint(700, carried.center().y())), {1, false});
        carried.translate(vertical ? QPoint(0, 1) : QPoint(1, 0));
        compareInsertion(tabReorderAt(input, layout, carried, QPoint(-500, carried.center().y())), {2, false});
        carried = gapBounds(layout);
        const auto previous = layout.tabs.at(0);
        if (vertical) carried.moveTop(previous.center().y()); else carried.moveLeft(previous.center().x());
        compareInsertion(tabReorderAt(input, layout, carried, QPoint(-500, carried.center().y())), {1, false});
        carried.translate(vertical ? QPoint(0, -1) : QPoint(-1, 0));
        compareInsertion(tabReorderAt(input, layout, carried, QPoint(700, carried.center().y())), {0, false});
    }

    void acceptingANewGapDoesNotReverseTheDecision()
    {
        auto input = strip(); auto layout = layoutTabs(input);
        auto carried = gapBounds(layout); carried.moveRight(layout.tabs[2].center().x() + 1);
        const auto next = tabReorderAt(input, layout, carried, carried.center());
        QCOMPARE(next.index, 2);
        input.insertion = next.index; layout = layoutTabs(input);
        QCOMPARE(tabReorderAt(input, layout, carried, carried.center()).index, 2);
        carried.moveLeft(layout.tabs[2].center().x());
        QCOMPARE(tabReorderAt(input, layout, carried, carried.center()).index, 2);
        carried.translate(-1, 0);
        QCOMPARE(tabReorderAt(input, layout, carried, carried.center()).index, 1);
    }

    void firstEntryAndMissingRectKeepTheOriginalClassifier()
    {
        auto input = strip(); input.insertion = -1; input.block.clear();
        const auto layout = layoutTabs(input);
        for (const auto point : {QPoint(1, 10), QPoint(140, 10), QPoint(700, 10)})
            compareInsertion(tabReorderAt(input, layout, QRect(100, 0, 100, 30), point), tabInsertionAt(input, point));
        input.insertion = 1; input.block = {100};
        compareInsertion(tabReorderAt(input, layoutTabs(input), {}, QPoint(230, 10)), tabInsertionAt(input, QPoint(230, 10)));
    }

    void tileRowsWaitForTheirCenterAndColumnsUseTheSameEdgeRule()
    {
        auto input = strip(true);
        input.width = 100; input.extents = {26, 26, 26, 26, 26, 26};
        input.stickyCount = 6; input.stickyTiles = true; input.insertionSticky = true;
        input.tile = {26, 26}; input.minimumTileWidth = 26; input.block = {26};
        auto layout = layoutTabs(input);
        QCOMPARE(layout.tileColumns, 3);
        auto carried = gapBounds(layout);
        carried.moveRight(layout.tabs[2].center().x());
        QCOMPARE(tabReorderAt(input, layout, carried, carried.center(), 0, true).index, 1);
        carried.translate(1, 0);
        QCOMPARE(tabReorderAt(input, layout, carried, carried.center(), 0, true).index, 2);
        carried = gapBounds(layout); carried.moveBottom(layout.tabs[3].center().y());
        QCOMPARE(tabReorderAt(input, layout, carried, carried.center(), 0, true).index, 1);
        carried.translate(0, 1);
        QCOMPARE(tabReorderAt(input, layout, carried, carried.center(), 0, true).index, 4);
        input.insertion = 4; layout = layoutTabs(input);
        QCOMPARE(tabReorderAt(input, layout, carried, carried.center(), 0, true).index, 4);
        carried = gapBounds(layout); carried.moveTop(layout.tabs[0].center().y());
        QCOMPARE(tabReorderAt(input, layout, carried, carried.center(), 0, true).index, 4);
        carried.translate(0, -1);
        QCOMPARE(tabReorderAt(input, layout, carried, carried.center(), 0, true).index, 1);
        input.insertion = 1; layout = layoutTabs(input);
        QCOMPARE(tabReorderAt(input, layout, carried, carried.center(), 0, true).index, 1);
    }

    void wrappedRowsMoveTheGapWithTheCoveredLabelAndRemainStable()
    {
        auto input = strip(); input.width = 250; input.multiline = true;
        const auto layout = layoutTabs(input);
        auto carried = gapBounds(layout); carried.moveTop(layout.tabs[2].top()); carried.moveLeft(layout.tabs[2].left());
        // Covering tab 2 pushes it before the gap, so the gap really enters
        // the next row rather than repacking back to the previous one.
        QCOMPARE(tabReorderAt(input, layout, carried, carried.center()).index, 2);
        input.insertion = 2;
        auto moved = layoutTabs(input);
        QCOMPARE(gapBounds(moved), carried);
        QCOMPARE(tabReorderAt(input, moved, carried, carried.center()).index, 2);
        // The leading edge must pass the previous row's center as well as
        // its neighbor's horizontal center. Equality does not reverse it.
        carried.moveLeft(moved.tabs[2].left());
        carried.moveTop(moved.tabs[2].center().y());
        QCOMPARE(tabReorderAt(input, moved, carried, carried.center()).index, 2);
        carried.translate(0, -1);
        QCOMPARE(tabReorderAt(input, moved, carried, carried.center()).index, 1);
        input.insertion = 1; moved = layoutTabs(input);
        QCOMPARE(tabReorderAt(input, moved, carried, carried.center()).index, 1);
    }

    void explicitSectionChangesStillControlStickiness()
    {
        auto input = strip(true); input.extents = {100, 100, 100, 100, 100};
        input.stickyCount = 2; input.removed = 4; input.insertion = 2;
        const auto layout = layoutTabs(input);
        const auto carried = gapBounds(layout).translated(0, -80);
        QVERIFY(tabReorderAt(input, layout, carried, carried.center(), 0, true).sticky);
        QVERIFY(!tabReorderAt(input, layout, carried, carried.center(), 0, false).sticky);
        input.carried = TabLayoutInput::Carried::Folder;
        const auto folder = tabReorderAt(input, layout, carried, carried.center(), 0, true);
        QVERIFY(!folder.sticky); QVERIFY(folder.index >= 2);
    }

    void frozenAndScrolledLabelsWaitForTheirRealCenterToBeVisible()
    {
        auto input = strip(); input.width = 300;
        input.extents = {40, 40, 100, 100, 100, 100}; input.stickyCount = 2;
        input.freezeSticky = true; input.removed = 3; input.insertion = 3;
        auto layout = layoutTabs(input);
        auto carried = gapBounds(layout); carried.moveRight(layout.tabs[4].center().x() + 1);
        QVERIFY(!layout.laneList.first().viewport.contains(layout.tabs[4].center()));
        // The pointer is still inside, but a long label can extend beyond
        // the viewport. That must not move an entirely hidden neighbor.
        QVERIFY(layout.laneList.first().viewport.contains(carried.center()));
        compareInsertion(tabReorderAt(input, layout, carried, carried.center()), {3, false});

        input.regularOffset = 60; layout = layoutTabs(input);
        carried = gapBounds(layout); carried.moveRight(layout.tabs[4].center().x());
        compareInsertion(tabReorderAt(input, layout, carried, carried.center()), {3, false});
        carried.translate(1, 0);
        compareInsertion(tabReorderAt(input, layout, carried, carried.center()), {4, false});
        input.insertion = 4; layout = layoutTabs(input);
        compareInsertion(tabReorderAt(input, layout, carried, carried.center()), {4, false});

        // A wide carried tab can cover a regular center hidden under the
        // frozen run while its own center remains in the regular section.
        input.block = {180}; layout = layoutTabs(input);
        carried = gapBounds(layout); carried.moveLeft(50);
        QVERIFY(layout.tabs[2].center().x() < layout.laneList.first().frozen);
        QVERIFY(carried.center().x() >= layout.laneList.first().frozen);
        compareInsertion(tabReorderAt(input, layout, carried, carried.center()), {3, false});
    }

    void collapsedRowsDoNotReorderThroughTheirHiddenNeighbors()
    {
        auto input = strip(); input.width = 330; input.extents.fill(100, 10);
        input.multiline = true; input.collapsed = true; input.current = 4;
        input.insertion = 5;
        auto layout = layoutTabs(input);
        QVERIFY(!layout.gap.isEmpty()); QVERIFY(layout.tabs[6].isEmpty());
        auto carried = gapBounds(layout).translated(10, 0);
        compareInsertion(tabReorderAt(input, layout, carried, carried.center()), {5, false});
        carried = gapBounds(layout); carried.moveLeft(layout.tabs[5].center().x());
        compareInsertion(tabReorderAt(input, layout, carried, carried.center()), {5, false});
        carried.translate(-1, 0);
        compareInsertion(tabReorderAt(input, layout, carried, carried.center()), {4, false});
        input.insertion = 4; layout = layoutTabs(input);
        compareInsertion(tabReorderAt(input, layout, carried, carried.center()), {4, false});
    }

    void wrappedBlocksKeepTheOriginalClassifier_data()
    {
        QTest::addColumn<TabLayoutInput::Carried>("kind");
        QTest::newRow("window-tabs") << TabLayoutInput::Carried::Tabs;
        QTest::newRow("folder") << TabLayoutInput::Carried::Folder;
    }
    void wrappedBlocksKeepTheOriginalClassifier()
    {
        QFETCH(TabLayoutInput::Carried, kind);
        auto input = strip(); input.width = 250; input.multiline = true;
        input.extents.fill(100, 6); input.block = {100, 100}; input.carried = kind;
        auto layout = layoutTabs(input);
        QVERIFY(layout.gap[0].top() != layout.gap[1].top());
        const auto carried = gapBounds(layout).translated(1, 0);
        const auto expected = tabInsertionAt(input, carried.center());
        compareInsertion(tabReorderAt(input, layout, carried, carried.center()), expected);
        input.insertion = expected.index; input.insertionSticky = expected.sticky; input.insertionFolder = expected.folder;
        layout = layoutTabs(input);
        compareInsertion(tabReorderAt(input, layout, carried, carried.center()), expected);
    }

    void folderMembershipDoesNotChangeWhileTheGapIsStationary()
    {
        auto input = strip(); input.width = 1000; input.extents = {100, 100, 100, 100, 100};
        input.removed = 4; input.insertion = 2; input.insertionFolder = 0;
        input.folders = {{1, 3, 30, false}};
        const auto layout = layoutTabs(input);
        const auto carried = gapBounds(layout);
        compareInsertion(tabReorderAt(input, layout, carried, carried.center()), {2, false, 0});
    }

    void aCarriedBlockNeverSplitsAnExistingFolder_data()
    {
        QTest::addColumn<bool>("collapsed");
        QTest::newRow("expanded") << false; QTest::newRow("collapsed") << true;
    }
    void aCarriedBlockNeverSplitsAnExistingFolder()
    {
        QFETCH(bool, collapsed);
        auto input = strip(); input.width = 1400; input.extents = {100, 100, 100, 100, 100};
        input.removed = 4; input.insertion = 0; input.block = {60, 60}; input.carried = TabLayoutInput::Carried::Tabs;
        input.folders = {{1, 3, 30, collapsed}};
        const auto layout = layoutTabs(input);
        for (int x = 0; x <= 700; x += 7) {
            auto carried = gapBounds(layout); carried.moveLeft(x);
            const auto result = tabReorderAt(input, layout, carried, carried.center());
            QVERIFY(result.index == 0 || result.index == 1 || result.index == 4);
            QCOMPARE(result.folder, -1); QVERIFY(!result.sticky);
        }
    }
};

QTEST_APPLESS_MAIN(TabReorderTest)
#include "tab_reorder_test.moc"
