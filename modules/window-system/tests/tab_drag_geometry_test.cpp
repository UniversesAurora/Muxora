// SPDX-License-Identifier: MIT
// Shared drag-placement contract without widgets or a graphics backend.
// Covers constraint axes, source anchors, compact tiles, oversized blocks,
// independently scrolled rows and top/bottom presentation origins.
#include "docking/tab_drag_geometry.hpp"
#include "docking/drag_start.hpp"
#include <QTest>

using namespace window_system;

class TabDragGeometryTest final : public QObject {
    Q_OBJECT
private slots:
    void verticalRows_keepTheColumnAndClampTheBlock()
    {
        const TabDragArea area{{180, 400}, {0, 6}, 28, 4};
        const QSize label(172, 28);
        const QPoint grab(100, 9);
        const auto middle = tabDragRect({}, area, {150, 120}, label, grab, TabDragConstraint::VerticalRow);
        QCOMPARE(middle.left(), area.verticalInset);
        QCOMPARE(middle.top() + grab.y(), 120);
        QCOMPARE(middle.size(), label);
        QCOMPARE(tabDragRect({}, area, {-500, -500}, label, grab, TabDragConstraint::VerticalRow).top(), area.origin.y());
        const auto last = tabDragRect({}, area, {900, 900}, label, grab, TabDragConstraint::VerticalRow);
        QCOMPARE(last.bottom(), area.size.height() - 1);
        QCOMPARE(last.left(), middle.left());
    }

    void compactTiles_keepTheGrabAndStayInsideBothAxes()
    {
        const TabDragArea area{{180, 400}, {0, 6}, 28, 4};
        const QSize tile(26, 26);
        const auto grab = QRect(QPoint(), tile).center();
        const auto middle = tabDragRect({}, area, {85, 120}, tile, grab, TabDragConstraint::CompactTile);
        QCOMPARE(middle.topLeft() + grab, QPoint(85, 120));
        const auto first = tabDragRect({}, area, {-50, -50}, tile, grab, TabDragConstraint::CompactTile);
        QCOMPARE(first.topLeft(), QPoint(area.verticalInset, area.origin.y()));
        const auto last = tabDragRect({}, area, {900, 900}, tile, grab, TabDragConstraint::CompactTile);
        QCOMPARE(last.right(), area.size.width() - area.verticalInset - 1);
        QCOMPARE(last.bottom(), area.size.height() - 1);
    }

    void horizontalRows_holdTheirLaneAndPreserveAlongAxisGrab()
    {
        const TabDragArea area{{200, 100}, {0, -12}, 28, 4};
        const QSize label(120, 28);
        const QPoint grab(87, 8);
        const auto rect = tabDragRect({}, area, {80, 99}, label, grab, TabDragConstraint::HorizontalRow);
        QCOMPARE(rect.top(), area.origin.y());
        QCOMPARE(rect.left() + grab.x(), 80);
        QCOMPARE(rect.width(), label.width());
        // Horizontal labels may extend outside the strip while the drag
        // controller decides whether the pointer has left that strip.
        const auto outside = tabDragRect({}, area, {-200, 0}, label, grab, TabDragConstraint::HorizontalRow);
        QVERIFY(outside.right() < 0);
        QCOMPARE(outside.top(), rect.top());
    }

    void scrolledRows_useTheDisplayedGrid_data()
    {
        QTest::addColumn<int>("origin");
        QTest::addColumn<int>("pointer");
        QTest::addColumn<int>("expected");
        for (const int origin : {3, -12}) {
            QTest::newRow(qPrintable(QString("%1-first").arg(origin))) << origin << origin + 5 << origin + 23;
            QTest::newRow(qPrintable(QString("%1-middle").arg(origin))) << origin << origin + 81 << origin + 79;
            QTest::newRow(qPrintable(QString("%1-end").arg(origin))) << origin << origin + 1000 << origin + 135;
        }
    }
    void scrolledRows_useTheDisplayedGrid()
    {
        QFETCH(int, origin); QFETCH(int, pointer); QFETCH(int, expected);
        TabLayout layout;
        layout.laneList.append({QRect(0, 30, 200, 70), 140, 7, true});
        layout.rowScrollLane = 0;
        const TabDragArea area{{200, 100}, {0, origin}, 28, 4};
        QCOMPARE(tabDragRowTop(layout, area, {40, pointer}), expected);
        QCOMPARE(tabDragRect(layout, area, {40, pointer}, {100, 28}, {30, 8},
            TabDragConstraint::HorizontalRow).top(), expected);
    }

    void separateSections_selectTheNearestDisplayedLane()
    {
        TabLayout layout;
        layout.laneList.append({QRect(0, 0, 200, 28), 400, 100, false});
        layout.laneList.append({QRect(0, 30, 200, 28), 500, 90, false});
        const TabDragArea area{{200, 70}, {0, 3}, 28, 4};
        QCOMPARE(tabDragRowTop(layout, area, {40, 8}), 3);
        QCOMPARE(tabDragRowTop(layout, area, {40, 38}), 33);
        QCOMPARE(tabDragRowTop(layout, area, {40, 300}), 33);
    }

    void oversizedFolderBlock_preservesSizeAndStartsAtTheOrigin()
    {
        const TabDragArea area{{80, 90}, {0, 4}, 28, 4};
        const QSize block(72, 178);
        const auto rect = tabDragRect({}, area, {30, 40}, block, {20, 10}, TabDragConstraint::VerticalRow);
        QCOMPARE(rect.top(), area.origin.y());
        QCOMPARE(rect.size(), block);
        QCOMPARE(rect.left(), area.verticalInset);
    }

    void localAndIncomingSources_shareNormalizedTargetPlacement_data()
    {
        QTest::addColumn<bool>("compact");
        QTest::newRow("row") << false;
        QTest::newRow("compact-tile") << true;
    }
    void localAndIncomingSources_shareNormalizedTargetPlacement()
    {
        QFETCH(bool, compact);
        const TabDragArea area{{180, 400}, {0, 6}, 28, 4};
        const QSize target = compact ? QSize(26, 26) : QSize(172, 28);
        const QRect source(10, 20, 400, 28);
        const auto anchor = DragAnchor::capture(source.topLeft() + QPoint(350, 12), source);
        const auto grab = anchor.forSize(target, compact);
        const auto mode = compact ? TabDragConstraint::CompactTile : TabDragConstraint::VerticalRow;
        const QPoint pointer(80, 120);
        const auto rect = tabDragRect({}, area, pointer, target, grab, mode);
        QVERIFY(rect.contains(pointer));
        QCOMPARE(rect.top() + grab.y(), pointer.y());
        if (compact) QCOMPARE(rect.left() + grab.x(), pointer.x());
    }
};

QTEST_APPLESS_MAIN(TabDragGeometryTest)
#include "tab_drag_geometry_test.moc"
