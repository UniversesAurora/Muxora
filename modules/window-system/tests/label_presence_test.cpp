// SPDX-License-Identifier: MIT
// Pure affine pose contract, with no window, paint device or clock.
#include "style/label_presence.hpp"
#include <QTest>

using namespace window_system;

class LabelPresenceTest final : public QObject {
    Q_OBJECT
private slots:
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
