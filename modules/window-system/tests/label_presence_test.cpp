// SPDX-License-Identifier: MIT
// Pure affine pose contract, with no window, paint device or clock.
#include "style/label_presence.hpp"
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

    void coordinates_data()
    {
        QTest::addColumn<QRectF>("rect");
        QTest::newRow("top") << QRectF(20, 0, 130, 28);
        QTest::newRow("bottom") << QRectF(20, 400, 130, 28);
        QTest::newRow("left") << QRectF(0, 40, 180, 28);
        QTest::newRow("right") << QRectF(600, 40, 180, 28);
        QTest::newRow("compact") << QRectF(4, 4, 26, 26);
    }

    void coordinates()
    {
        QFETCH(QRectF, rect);
        const auto hidden = labelPresencePose(rect, -1);
        QCOMPARE(hidden.opacity, qreal(0));
        QCOMPARE(hidden.scale, qreal(0.88));
        const auto shown = labelPresencePose(rect, 2);
        QVERIFY(shown.complete()); QCOMPARE(shown.mapRect(rect), rect);
        for (int step = 0; step <= 100; ++step) {
            const qreal progress = qreal(step) / 100;
            const auto pose = labelPresencePose(rect, progress);
            QCOMPARE(pose.opacity, progress);
            QCOMPARE(pose.mapRect(rect).center(), rect.center() + QPointF(0, 3 * (1 - progress)));
            const QPointF origin(12.5, -34.75);
            const auto moved = pose.translated(origin).mapRect(rect.translated(origin));
            const auto expected = pose.mapRect(rect).translated(origin);
            QVERIFY(qAbs(moved.x() - expected.x()) < 1e-10);
            QVERIFY(qAbs(moved.y() - expected.y()) < 1e-10);
            QCOMPARE(moved.size(), expected.size());
        }
        const auto planar = labelPresencePose(rect, 0.5, LabelPresenceKind::Planar);
        QVERIFY(planar.complete()); QCOMPARE(planar.mapRect(rect), rect);
        QCOMPARE(labelPresencePose(rect, 0, LabelPresenceKind::Planar).opacity, qreal(0));
    }
};

QTEST_APPLESS_MAIN(LabelPresenceTest)
#include "label_presence_test.moc"
