// SPDX-License-Identifier: MIT
#include "docking/tab_strip_arrangement.hpp"
#include <QFontMetrics>
#include <QGuiApplication>
#include <QTest>

using namespace window_system;

namespace {
TabStripArrangementInput strip()
{
    TabStripArrangementInput input;
    input.font = QGuiApplication::font(); input.group = QSize(400, 500);
    input.geometry.outerMargin = 4; input.geometry.shadowBlur = 12;
    input.geometry.rowHeight = 30;
    input.geometry.iconColumnWidth = 40; input.geometry.minimumColumnWidth = 64; input.geometry.maximumColumnWidth = 320;
    input.labels.extents.fill(76, 120);
    input.controls.trailingWidth = 42; input.controls.trailingPresent = true;
    input.controls.endWidth = 30; input.controls.endPresent = true;
    input.controls.countPresent = true; input.controls.ownerVisible = true;
    input.controls.countSize = QSize(200, 30); input.controls.countMinimumWidth = 24; input.controls.countPadding = 14;
    return input;
}
}

class TabStripArrangementTest final : public QObject {
    Q_OBJECT
private slots:
    void countConvergenceAndVisibilityKeepThePublishedLayoutConsistent()
    {
        auto input = strip(); input.geometry.rows.multiline = true; input.geometry.rows.showAll = false;
        for (const int width : {150, 200, 400, 750}) {
            const auto result = arrangeTabStrip(input, QSize(width, 34));
            QVERIFY(result.controls.countShown); QVERIFY(result.countMeasured);
            QCOMPARE(result.countText, QStringLiteral("+%1").arg(result.layout.hidden));
            QVERIFY(result.controls.countSize.width() < input.controls.countSize.width());
            QVERIFY(result.controls.countSize.width() >= QFontMetrics(input.font).horizontalAdvance(result.countText) + input.controls.countPadding);
            QCOMPARE(result.input.endExtent, result.controls.endWidth + result.controls.countSize.width());
            QCOMPARE(layoutTabs(result.input).tabs, result.layout.tabs);
            QCOMPARE(result.controlRects.count.right() + 1, result.controlRects.end.left());
            input.controls = result.controls;
            const auto repeated = arrangeTabStrip(input, QSize(width, 34));
            QCOMPARE(repeated.layout.tabs, result.layout.tabs);
            QCOMPARE(repeated.controls.countSize, result.controls.countSize);
            input.controls.countSize.setWidth(200);
        }
        input.controls.ownerVisible = false;
        const auto hidden = arrangeTabStrip(input, QSize(400, 34));
        QVERIFY(hidden.controls.countShown); QVERIFY(!hidden.countMeasured); QVERIFY(hidden.countText.isEmpty());
        QCOMPARE(hidden.controls.countSize, input.controls.countSize); QVERIFY(hidden.controlRects.count.isNull());
        input.geometry.rows.dragExpanded = true;
        QVERIFY(!arrangeTabStrip(input, QSize(400, 200)).controls.countShown);
    }

    void fourEdgesKeepControlsAndClampedOffsetsInTheirOwnCoordinates()
    {
        auto input = strip(); input.labels.stickyCount = 3; input.geometry.rows.hasSticky = true;
        input.labels.stickyOffset = 100000; input.labels.regularOffset = 100000;
        for (const auto edge : {TabBarPlacement::Top, TabBarPlacement::Bottom, TabBarPlacement::Left, TabBarPlacement::Right}) {
            input.geometry.placement = edge;
            const auto size = input.geometry.vertical() ? QSize(180, 250) : QSize(400, 34);
            const auto result = arrangeTabStrip(input, size);
            for (const auto& lane : result.layout.laneList) {
                QCOMPARE(lane.sticky ? result.stickyOffset : result.regularOffset, lane.offset);
                QVERIFY(lane.offset <= lane.maximumOffset());
            }
            for (int index = 0; index < result.layout.tabs.size(); ++index) {
                const auto visible = result.layout.visibleRect(index);
                if (!visible.isEmpty()) QCOMPARE(result.layout.tabAt(visible.center()), index);
            }
            if (input.geometry.vertical()) {
                QVERIFY(result.controlRects.end.isNull()); QVERIFY(result.controlRects.trailing.isNull());
                QCOMPARE(input.geometry.origin(), QPoint{});
            } else {
                QCOMPARE(result.controlRects.end.right(), size.width() - 1);
                QCOMPARE(result.controlRects.end.top(), edge == TabBarPlacement::Top ? 4 : 0);
            }
        }
    }

    void rowSizingSharesExpansionAndKeepsReservationIndependentOfTheDragGap()
    {
        auto input = strip(); input.geometry.rows.multiline = true; input.geometry.rows.showAll = false;
        auto rows = measureTabStripRows(input, input.group);
        input.labels.insertion = 0; input.labels.block.fill(76, 60);
        const auto dragging = measureTabStripRows(input, input.group);
        QCOMPARE(dragging.closed, rows.closed); QCOMPARE(dragging.push, rows.push);
        input.geometry.rows.expanded = true; input.geometry.rows.reveal = 0.5;
        for (const auto edge : {TabBarPlacement::Top, TabBarPlacement::Bottom}) {
            input.geometry.placement = edge;
            QCOMPARE(tabStripReservedSize(input.geometry, input.group, rows).height(), rows.closed);
            QCOMPARE(tabStripDisplaySize(input.geometry, input.group, rows).height(), qRound(rows.closed + (rows.overlay - rows.closed) * 0.5));
            QCOMPARE(input.geometry.origin().y(), edge == TabBarPlacement::Top ? 4 : 6);
        }
        input.geometry.rows.expansion = TabRowExpansion::Push;
        QCOMPARE(tabStripReservedSize(input.geometry, input.group, rows).height(), qRound(rows.closed + (rows.push - rows.closed) * 0.5));
        input.geometry.placement = TabBarPlacement::Right; input.geometry.iconsOnly = true;
        QCOMPARE(tabStripReservedSize(input.geometry, input.group), QSize(40, input.group.height()));
    }
};

QTEST_MAIN(TabStripArrangementTest)
#include "tab_strip_arrangement_test.moc"
