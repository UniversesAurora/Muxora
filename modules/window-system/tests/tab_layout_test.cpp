// SPDX-License-Identifier: MIT
// The tab strip layout (docs/design/tab-strip-design.md §3, §4, §6.1).
//
// State dimensions, all combined in everyStateKeepsTheInvariants:
//   orientation    horizontal, vertical
//   rows           single, multiple (horizontal)
//   sticky row     same row, separate row (horizontal)
//   frozen sticky  off, on (horizontal single row)
//   collapsed      off, on (horizontal multiple rows)
//   sticky tiles   off, on (vertical)
//   sticky tabs    0, 1, 3
//   tabs           0, 1, 5, 24
//   width          narrow, medium, wide
//   drag           none, a tab left its slot, that tab's gap, a carried
//                  block into the sticky or the regular section
//   scroll         start, far past the end
#include "docking/tab_layout.hpp"

#include <QRandomGenerator>
#include <QTest>

using namespace window_system;

namespace {
using Orientation = TabLayoutInput::Orientation;

QVector<int> widths(int count, int seed)
{
    QVector<int> result;
    QRandomGenerator random{quint32(seed)};
    for (int index = 0; index < count; ++index) result.append(60 + int(random.bounded(140)));
    return result;
}

TabLayoutInput horizontal(QVector<int> extents, int width, int sticky = 0)
{
    TabLayoutInput input;
    input.extents = std::move(extents); input.width = width; input.stickyCount = sticky;
    return input;
}

QString describe(const TabLayoutInput& input)
{
    return QStringLiteral("orientation=%1 multiline=%2 separate=%3 frozen=%4 collapsed=%5 tiles=%6 sticky=%7 tabs=%8 width=%9 "
                          "removed=%10 insertion=%11 insertionSticky=%12 block=%13 offsets=%14/%15 current=%16")
        .arg(int(input.orientation)).arg(input.multiline).arg(input.separateStickyRow).arg(input.freezeSticky)
        .arg(input.collapsed).arg(input.stickyTiles).arg(input.stickyCount).arg(input.extents.size()).arg(input.width)
        .arg(input.removed).arg(input.insertion).arg(input.insertionSticky).arg(input.block.size())
        .arg(input.stickyOffset).arg(input.regularOffset).arg(input.current);
}
} // namespace

class TabLayoutTest final : public QObject {
    Q_OBJECT
private slots:
    void everyStateKeepsTheInvariants();
    void randomStatesKeepTheInvariants();
    void singleRowScrollsOnlyWithinItsContent();
    void frozenStickyTabsStayAtTheStart();
    void separateStickyRowScrollsOnItsOwn();
    void separateStickyRowHoldsTrailingControlsWithoutOtherTabs();
    void multipleRowsScrollPastTheirHeightLimit();
    void collapsedRowsShowTheCurrentRowAndCountTheRest();
    void collapsedSeparateRowsShowOneRowEach();
    void columnKeepsStickyTabsWithinAThird();
    void stickyTilesWrapInTheColumn();
    void gapTakesTheBlockAsItWillBeShown();
    void insertionFollowsLabelCenters_data();
    void insertionFollowsLabelCenters();
    void dropPositionDecidesStickiness();
    void aLoneStickyTabStaysStickyNearTheStart();
    void blockIntoAnEmptyRegularSectionGetsItsOwnRow();
private:
    void verify(const TabLayoutInput& input);
};

void TabLayoutTest::verify(const TabLayoutInput& input)
{
    const auto context = describe(input).toUtf8();
    const auto layout = layoutTabs(input);
    const int count = int(input.extents.size());
    QCOMPARE(layout.tabs.size(), count);
    const bool vertical = input.orientation == Orientation::Vertical;
    int shown = 0;
    QVector<QRect> visible;
    for (int index = 0; index < count; ++index) {
        if (index == input.removed) { QVERIFY2(layout.lanes.at(index) < 0, context); continue; }
        if (layout.lanes.at(index) < 0) { QVERIFY2(input.collapsed && input.multiline && !vertical, context); continue; }
        ++shown;
        const auto& lane = layout.laneList.at(layout.lanes.at(index));
        QVERIFY2(lane.offset >= 0 && lane.offset <= lane.maximumOffset(), context);
        const auto area = layout.visibleRect(index);
        if (area.isEmpty()) continue;
        QVERIFY2(lane.viewport.contains(area), context);
        // Painting and hit testing agree on every visible pixel's owner.
        QVERIFY2(layout.tabAt(area.center()) == index, context);
        for (const auto& other : visible) QVERIFY2(!other.intersects(area), context);
        visible.append(area);
    }
    // Collapsing hides whole rows; nothing else disappears.
    QCOMPARE(shown + layout.hidden + (input.removed >= 0 && input.removed < count ? 1 : 0), count);
    if (!(input.collapsed && input.multiline && !vertical)) QCOMPARE(layout.hidden, 0);
    else if (input.current >= 0 && input.current < count && input.current != input.removed)
        QVERIFY2(layout.lanes.at(input.current) >= 0, context);
    // Order within each lane and row follows the tab order.
    for (int index = 0; index + 1 < count; ++index) {
        int next = index + 1;
        while (next < count && layout.lanes.at(next) < 0) ++next;
        if (layout.lanes.at(index) < 0 || next >= count || layout.lanes.at(index) != layout.lanes.at(next)) continue;
        const auto a = layout.tabs.at(index), b = layout.tabs.at(next);
        if (vertical) QVERIFY2(b.top() > a.top() || (b.top() == a.top() && b.left() > a.left()), context);
        else if (a.top() == b.top() && !(layout.laneList.at(layout.lanes.at(index)).frozen && index < input.stickyCount && next >= input.stickyCount))
            QVERIFY2(b.left() >= a.right() + 1, context);
        else if (a.top() != b.top()) QVERIFY2(b.top() > a.top(), context);
    }
    // The gap is the whole block, laid out like tabs, overlapping none of them.
    if (input.insertion >= 0 && !input.block.isEmpty() && !(input.collapsed && input.multiline)) {
        QCOMPARE(layout.gap.size(), input.block.size());
        for (int cell = 0; cell < layout.gap.size(); ++cell) {
            QVERIFY2(!layout.gap.at(cell).isNull(), context);
            for (int index = 0; index < count; ++index)
                if (layout.lanes.at(index) == layout.gapLane && !(layout.laneList.at(layout.gapLane).frozen && (index < input.stickyCount) != input.insertionSticky))
                    QVERIFY2(!layout.tabs.at(index).intersects(layout.gap.at(cell)), context);
        }
    }
    // Every insertion decision is a valid position, stable across the gap it opens.
    if (count > 0) {
        const int remaining = count - (input.removed >= 0 && input.removed < count ? 1 : 0);
        auto probing = input; probing.insertion = -1; probing.block.clear(); probing.collapsed = false;
        for (const QPoint probe : {QPoint(1, 1), QPoint(input.width / 2, 15), QPoint(input.width - 2, 40), QPoint(input.width / 3, 200)}) {
            const auto decision = tabInsertionAt(probing, probe, 20);
            QVERIFY2(decision.index >= 0 && decision.index <= remaining, context);
            int stickyRemaining = 0;
            for (int index = 0; index < std::min(input.stickyCount, count); ++index) stickyRemaining += index == input.removed ? 0 : 1;
            if (decision.sticky) QVERIFY2(decision.index <= stickyRemaining, context);
            else QVERIFY2(decision.index >= stickyRemaining || input.stickyCount == 0, context);
            auto opened = probing; opened.insertion = decision.index; opened.insertionSticky = decision.sticky; opened.block = {80};
            const auto again = tabInsertionAt(opened, probe, 20);
            QCOMPARE(again.index, decision.index); QCOMPARE(again.sticky, decision.sticky);
        }
    }
}

void TabLayoutTest::everyStateKeepsTheInvariants()
{
    int states = 0;
    for (const auto orientation : {Orientation::Horizontal, Orientation::Vertical})
    for (const bool multiline : {false, true})
    for (const bool separate : {false, true})
    for (const bool frozen : {false, true})
    for (const bool collapsed : {false, true})
    for (const int sticky : {0, 1, 3})
    for (const int count : {0, 1, 5, 24})
    for (const int width : {90, 420, 2400})
    for (const int drag : {0, 1, 2, 3, 4})
    for (const int offset : {0, 100000}) {
        const bool vertical = orientation == Orientation::Vertical;
        if (vertical && (multiline || collapsed || frozen)) continue; // Not settings of a column.
        if (!multiline && collapsed) continue;
        if (sticky > count) continue;
        TabLayoutInput input = horizontal(widths(count, count * 7 + width), width, sticky);
        input.orientation = orientation; input.multiline = multiline; input.separateStickyRow = separate;
        input.freezeSticky = frozen && !multiline; input.collapsed = collapsed; input.stickyTiles = vertical && separate;
        input.trailingExtent = vertical ? 0 : 66; input.height = vertical ? 600 : (multiline ? 96 : 0);
        input.current = count ? count / 2 : -1;
        input.stickyOffset = offset; input.regularOffset = offset;
        if (count && drag == 1) input.removed = count - 1;
        if (count && drag == 2) { input.removed = 0; input.insertion = count / 2; input.insertionSticky = sticky > 1 && count / 2 < sticky; input.block = {input.extents.at(0)}; }
        if (drag == 3) { input.insertion = 0; input.insertionSticky = sticky > 0; input.block = {70, 400, 3000}; }
        if (drag == 4) { input.insertion = count; input.insertionSticky = false; input.block = {120, 90}; }
        verify(input);
        if (QTest::currentTestFailed()) return;
        ++states;
    }
    QVERIFY(states > 3000);
}

void TabLayoutTest::randomStatesKeepTheInvariants()
{
    // Seeded, so a failure prints a state that replays exactly.
    QRandomGenerator random(20261025);
    for (int round = 0; round < 3000; ++round) {
        TabLayoutInput input;
        const int count = int(random.bounded(40));
        input.orientation = random.bounded(4) == 0 ? Orientation::Vertical : Orientation::Horizontal;
        input.extents = widths(count, int(random.bounded(100000)));
        input.width = 40 + int(random.bounded(2000));
        input.stickyCount = count ? int(random.bounded(count + 1)) : 0;
        const bool vertical = input.orientation == Orientation::Vertical;
        input.multiline = !vertical && random.bounded(2);
        input.separateStickyRow = random.bounded(2);
        input.freezeSticky = !input.multiline && !vertical && random.bounded(2);
        input.collapsed = input.multiline && random.bounded(2);
        input.stickyTiles = vertical && random.bounded(2);
        input.height = vertical ? 30 + int(random.bounded(900)) : input.multiline ? int(random.bounded(200)) : 0;
        input.trailingExtent = vertical ? 0 : int(random.bounded(120));
        input.current = count ? int(random.bounded(count)) : -1;
        input.collapsedStickyRow = int(random.bounded(5)); input.collapsedRegularRow = int(random.bounded(5));
        input.stickyOffset = int(random.bounded(5000)); input.regularOffset = int(random.bounded(5000));
        if (count && random.bounded(3) == 0) input.removed = int(random.bounded(count));
        if (random.bounded(2)) {
            input.insertion = int(random.bounded(count + 1));
            const int remainingSticky = input.stickyCount - (input.removed >= 0 && input.removed < input.stickyCount ? 1 : 0);
            input.insertionSticky = input.stickyCount > 0 && input.insertion <= remainingSticky && random.bounded(2);
            for (int cell = 0, cells = 1 + int(random.bounded(5)); cell < cells; ++cell) input.block.append(30 + int(random.bounded(400)));
        }
        verify(input);
        if (QTest::currentTestFailed()) return;
    }
}

void TabLayoutTest::singleRowScrollsOnlyWithinItsContent()
{
    auto input = horizontal({100, 100, 100}, 500); input.trailingExtent = 40; input.regularOffset = 80;
    auto layout = layoutTabs(input);
    QCOMPARE(layout.laneList.first().offset, 0); // Everything fits.
    QCOMPARE(layout.tabs.at(0).left(), 0); QCOMPARE(layout.trailing, QRect(300, 0, 40, 30));
    input.width = 250; layout = layoutTabs(input);
    QCOMPARE(layout.laneList.first().offset, 80); QCOMPARE(layout.tabs.at(1).left(), 20);
    input.regularOffset = 1000; layout = layoutTabs(input);
    // The last control ends exactly at the viewport's end; no blank extent.
    QCOMPARE(layout.laneList.first().offset, 90); QCOMPARE(layout.trailing.right(), 249);
}

void TabLayoutTest::frozenStickyTabsStayAtTheStart()
{
    auto input = horizontal({40, 40, 120, 120, 120}, 300, 2); input.freezeSticky = true; input.regularOffset = 60;
    const auto layout = layoutTabs(input);
    QCOMPARE(layout.tabs.at(0).left(), 0); QCOMPARE(layout.tabs.at(1).left(), 40);
    QCOMPARE(layout.tabs.at(2).left(), 80 - 60);
    QCOMPARE(layout.separator, QRect(79, 0, 1, 30));
    // The tab under the sticky run shows only its part past the run.
    QCOMPARE(layout.visibleRect(2).left(), 80);
    QCOMPARE(layout.tabAt(QPoint(50, 10)), 1);
}

void TabLayoutTest::separateStickyRowScrollsOnItsOwn()
{
    auto input = horizontal({200, 200, 200, 150, 150, 150}, 300, 3); input.separateStickyRow = true;
    input.stickyOffset = 100; input.regularOffset = 20; input.trailingExtent = 40;
    const auto layout = layoutTabs(input);
    QCOMPARE(layout.laneList.size(), 2);
    QCOMPARE(layout.laneList.at(0).offset, 100); QCOMPARE(layout.laneList.at(1).offset, 20);
    QCOMPARE(layout.tabs.at(0).left(), -100); QCOMPARE(layout.tabs.at(3).left(), -20);
    QCOMPARE(layout.tabs.at(3).top(), 32); QCOMPARE(layout.size, QSize(300, 62));
    QCOMPARE(layout.trailingLane, 1);
}

void TabLayoutTest::separateStickyRowHoldsTrailingControlsWithoutOtherTabs()
{
    auto input = horizontal({100, 100}, 600, 2); input.separateStickyRow = true; input.trailingExtent = 66;
    const auto layout = layoutTabs(input);
    QCOMPARE(layout.laneList.size(), 1); QCOMPARE(layout.size.height(), 30);
    QCOMPARE(layout.trailing, QRect(200, 0, 66, 30));
}

void TabLayoutTest::multipleRowsScrollPastTheirHeightLimit()
{
    auto input = horizontal(QVector<int>(20, 100), 300); input.multiline = true; input.height = 70; input.regularOffset = 33;
    const auto layout = layoutTabs(input);
    const auto& lane = layout.laneList.first();
    QVERIFY(lane.vertical);
    QCOMPARE(lane.viewport.height(), 70); // At least two rows: 30 + 2 + 30 = 62 < 70.
    QCOMPARE(lane.extent, 7 * 32 - 2);
    QCOMPARE(lane.offset, 33); QCOMPARE(layout.tabs.at(3).top(), 32 - 33);
}

void TabLayoutTest::collapsedRowsShowTheCurrentRowAndCountTheRest()
{
    auto input = horizontal(QVector<int>(10, 100), 330); input.multiline = true; input.collapsed = true;
    input.trailingExtent = 30; input.current = 7;
    const auto layout = layoutTabs(input);
    // 300 px wrap 3 tabs a row: rows 0-2, 3-5, 6-8, 9. Row 2 holds the current tab.
    QCOMPARE(layout.regularRow, 2); QCOMPARE(layout.hidden, 7);
    for (const int index : {6, 7, 8}) QCOMPARE(layout.tabs.at(index).top(), 0);
    QCOMPARE(layout.trailing, QRect(300, 0, 30, 30));
    QCOMPARE(layout.size.height(), 30);
}

void TabLayoutTest::collapsedSeparateRowsShowOneRowEach()
{
    auto input = horizontal(QVector<int>(12, 100), 330, 5); input.multiline = true; input.collapsed = true;
    input.separateStickyRow = true; input.trailingExtent = 30; input.current = 10; input.collapsedStickyRow = 1;
    const auto layout = layoutTabs(input);
    QCOMPARE(layout.stickyRow, 1); QCOMPARE(layout.tabs.at(3).top(), 0); QCOMPARE(layout.tabs.at(4).top(), 0);
    QCOMPARE(layout.regularRow, 1); QCOMPARE(layout.tabs.at(10).top(), 32);
    QCOMPARE(layout.hidden, 3 + 4); QCOMPARE(layout.stickyRows, 1);
}

void TabLayoutTest::columnKeepsStickyTabsWithinAThird()
{
    TabLayoutInput input; input.orientation = Orientation::Vertical; input.width = 220; input.height = 600;
    input.extents = QVector<int>(30, 0); input.stickyCount = 10;
    const auto layout = layoutTabs(input);
    QCOMPARE(layout.laneList.size(), 2);
    QCOMPARE(layout.laneList.at(0).viewport.height(), 200);
    QCOMPARE(layout.laneList.at(0).extent, 10 * 32 - 2);
    QCOMPARE(layout.separator, QRect(6, 202, 208, 1));
    QCOMPARE(layout.laneList.at(1).viewport.top(), 205);
    QCOMPARE(layout.tabs.at(10), QRect(6, 205, 208, 30));
}

void TabLayoutTest::stickyTilesWrapInTheColumn()
{
    TabLayoutInput input; input.orientation = Orientation::Vertical; input.width = 124; input.height = 600;
    input.extents = QVector<int>(8, 0); input.stickyCount = 5; input.stickyTiles = true;
    const auto layout = layoutTabs(input);
    // 112 px of row: three 36 px tiles with 2 px between them fit.
    QCOMPARE(layout.tabs.at(0), QRect(6, 0, 36, 32)); QCOMPARE(layout.tabs.at(2), QRect(82, 0, 36, 32));
    QCOMPARE(layout.tabs.at(3), QRect(6, 34, 36, 32));
}

void TabLayoutTest::gapTakesTheBlockAsItWillBeShown()
{
    auto input = horizontal({100, 100, 100}, 2000); input.insertion = 1; input.block = {50, 70};
    const auto layout = layoutTabs(input);
    QCOMPARE(layout.gap, (QVector<QRect>{QRect(100, 0, 50, 30), QRect(150, 0, 70, 30)}));
    QCOMPARE(layout.tabs.at(1).left(), 220);
    // Wrapped, the block wraps like tabs.
    input.multiline = true; input.width = 200;
    const auto wrapped = layoutTabs(input);
    QCOMPARE(wrapped.gap.at(1).top(), 32);
}

void TabLayoutTest::insertionFollowsLabelCenters_data()
{
    QTest::addColumn<int>("x"); QTest::addColumn<int>("index");
    QTest::newRow("before first center") << 49 << 0;
    QTest::newRow("past first center") << 51 << 1;
    QTest::newRow("past second center") << 151 << 2;
    QTest::newRow("past the end") << 900 << 3;
}

void TabLayoutTest::insertionFollowsLabelCenters()
{
    QFETCH(int, x); QFETCH(int, index);
    const auto input = horizontal({100, 100, 100}, 2000);
    QCOMPARE(tabInsertionAt(input, QPoint(x, 10)).index, index);
}

void TabLayoutTest::dropPositionDecidesStickiness()
{
    auto input = horizontal({60, 60, 100, 100}, 2000, 2);
    QVERIFY(tabInsertionAt(input, QPoint(10, 10)).sticky);
    QVERIFY(tabInsertionAt(input, QPoint(110, 10)).sticky); // Before the boundary.
    QVERIFY(!tabInsertionAt(input, QPoint(125, 10)).sticky); // Past the sticky run.
    QVERIFY(tabInsertionAt(input, QPoint(125, 10), 20).sticky); // A sticky block reaches a little further.
    QVERIFY(!tabInsertionAt(input, QPoint(260, 10)).sticky);
    input.separateStickyRow = true;
    const auto top = tabInsertionAt(input, QPoint(1000, 10));
    QVERIFY(top.sticky); QCOMPARE(top.index, 2);
    const auto bottom = tabInsertionAt(input, QPoint(5, 40));
    QVERIFY(!bottom.sticky); QCOMPARE(bottom.index, 2);
}

void TabLayoutTest::aLoneStickyTabStaysStickyNearTheStart()
{
    auto input = horizontal({60, 100, 100}, 2000, 1); input.removed = 0;
    QVERIFY(tabInsertionAt(input, QPoint(10, 10), 30).sticky);
    QVERIFY(!tabInsertionAt(input, QPoint(10, 10)).sticky);
    input.separateStickyRow = true;
    QVERIFY(tabInsertionAt(input, QPoint(10, 10)).sticky);
}

void TabLayoutTest::blockIntoAnEmptyRegularSectionGetsItsOwnRow()
{
    auto input = horizontal({100, 100}, 600, 2); input.separateStickyRow = true;
    input.insertion = 2; input.insertionSticky = false; input.block = {90};
    const auto layout = layoutTabs(input);
    QCOMPARE(layout.laneList.size(), 2); QCOMPARE(layout.gap.first(), QRect(0, 32, 90, 30));
}

QTEST_MAIN(TabLayoutTest)
#include "tab_layout_test.moc"
