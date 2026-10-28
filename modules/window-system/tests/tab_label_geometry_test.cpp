// SPDX-License-Identifier: MIT
// Slot geometry is a Qt Core contract, independent of font or paint devices.
#include "style/tab_label_geometry.hpp"
#include <QTest>

using namespace window_system;

class TabLabelGeometryTest final : public QObject {
    Q_OBJECT
private slots:
    void ordinaryPartsFollowTextDirectionAndReserveOnlyRequestedActions()
    {
        const QRect tab(10, 20, 180, 28);
        for (const auto shape : {TabShape::Top, TabShape::Bottom, TabShape::Row}) {
            const auto regular = layoutTabParts(tab, shape, {12, 16, 120}, false, true, false, Qt::LeftToRight);
            const auto reserved = layoutTabParts(tab, shape, {12, 16, 120}, false, true, true, Qt::LeftToRight);
            QCOMPARE(regular.number, QRect(20, 20, 12, 28));
            QCOMPARE(regular.icon, QRect(38, 26, 16, 16));
            QCOMPARE(regular.title, QRect(60, 20, 120, 28));
            QVERIFY(regular.title.intersects(regular.action));
            QVERIFY(reserved.title.right() < reserved.action.left());
            QCOMPARE(reserved.number, regular.number); QCOMPARE(reserved.icon, regular.icon);
            const auto mirrored = layoutTabParts(tab, shape, {12, 16, 120}, false, true, false, Qt::RightToLeft);
            for (const auto& pair : {qMakePair(regular.number, mirrored.number), qMakePair(regular.icon, mirrored.icon),
                                     qMakePair(regular.title, mirrored.title), qMakePair(regular.action, mirrored.action)}) {
                QCOMPARE(pair.first.size(), pair.second.size());
                QCOMPARE(pair.first.left() + pair.second.right(), tab.left() + tab.right());
            }
            const auto folded = layoutTabParts(tab, shape, {12, 16, 120}, true, true, true, Qt::LeftToRight);
            QCOMPARE(folded.icon.center(), tab.center()); QVERIFY(folded.action.isNull()); QVERIFY(folded.title.isNull());
        }
    }

    void columnRailsRemainFixedAndMirrorTheirActionsAndTitles()
    {
        const QRect tab(4, 50, 212, 28);
        const TabColumnLabelMetrics metrics{220, 40, 16, 28};
        const TabLabelParts label{14, 16, 100};
        const auto left = layoutColumnTabParts(tab, label, false, true, true, false, metrics);
        const auto right = layoutColumnTabParts(tab, label, false, true, true, true, metrics);
        QCOMPARE(left.icon.left(), 12); QCOMPARE(right.icon.left(), 192);
        QCOMPARE(left.icon.top(), 55); QCOMPARE(right.icon.top(), left.icon.top());
        QCOMPARE(left.title, QRect(54, 50, 133, 28)); QCOMPARE(right.title, QRect(33, 50, 133, 28));
        QCOMPARE(left.action.left() + right.action.right(), metrics.width - 1);
        QCOMPARE(left.number.left() + right.number.right(), metrics.width - 1);
        for (bool outerRight : {false, true}) {
            const auto folded = layoutColumnTabParts(tab, label, true, true, true, outerRight, metrics);
            QCOMPARE(folded.icon, outerRight ? right.icon : left.icon);
            QVERIFY(folded.action.isNull()); QVERIFY(folded.title.isNull());
            const auto narrow = layoutColumnTabParts({4, 50, 72, 28}, label, false, true, true, outerRight, {80, 40, 16, 28});
            QVERIFY(narrow.number.isNull()); QVERIFY(!narrow.icon.isEmpty());
        }
    }
};

QTEST_APPLESS_MAIN(TabLabelGeometryTest)
#include "tab_label_geometry_test.moc"
