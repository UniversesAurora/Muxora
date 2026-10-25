// SPDX-License-Identifier: MIT
#include "docking/surface_geometry.hpp"
#include "style/elevation.hpp"

#include <QImage>
#include <QPainter>
#include <QSet>
#include <QTest>
#include <QTransform>
#include <array>

using namespace window_system;

namespace {
SurfaceGeometryInput topGeometry()
{
    SurfaceGeometryInput input;
    input.body = {20, 80, 500, 300};
    input.anchor = {100, 52, 120, 28};
    return input;
}

QTransform orientation(SurfaceEdge edge)
{
    switch (edge) {
    case SurfaceEdge::Top: return {};
    case SurfaceEdge::Bottom: return QTransform(1, 0, 0, -1, 0, 0);
    case SurfaceEdge::Left: return QTransform(0, 1, 1, 0, 0, 0);
    case SurfaceEdge::Right: return QTransform(0, 1, -1, 0, 0, 0);
    }
    return {};
}
} // namespace

class SurfaceElevationTest final : public QObject {
    Q_OBJECT
private slots:
    void levelProfilesMatchTheirExtents()
    {
        const std::array<std::pair<ElevationLevel, QMarginsF>, 6> expected{{
            {ElevationLevel::Flat, {}}, {ElevationLevel::AnchorRest, {3, 2, 3, 4}},
            {ElevationLevel::AnchorRaised, {5, 3, 5, 7}}, {ElevationLevel::BodyRest, {8, 6, 8, 10}},
            {ElevationLevel::BodyRaised, {12, 8, 12, 16}}, {ElevationLevel::Lifted, {6, 3, 6, 9}},
        }};
        for (const auto& [level, margins] : expected) QCOMPARE(elevationMargins(elevationProfile(level)), margins);
    }

    void interpolationUsesOneBoundedProgress()
    {
        const auto rest = elevationProfile(ElevationLevel::BodyRest);
        const auto raised = elevationProfile(ElevationLevel::BodyRaised);
        QSet<int> levels;
        qreal previous = rest.ambient.blur;
        for (int sample = 0; sample <= 1000; ++sample) {
            const qreal progress = sample / 1000.0;
            levels.insert(elevationStep(progress));
            const auto actual = interpolateElevation(rest, raised, progress);
            QVERIFY(actual.ambient.blur >= previous);
            QCOMPARE(actual.ambient.blur, 8 + 4 * quantizedElevation(progress));
            QCOMPARE(actual.key.offset, 1 + quantizedElevation(progress));
            previous = actual.ambient.blur;
        }
        QCOMPARE(levels.size(), 32);
        QCOMPARE(quantizedElevation(-100), 0);
        QCOMPARE(quantizedElevation(100), 1);
    }

    void attachmentStates_data()
    {
        QTest::addColumn<bool>("adjacent"); QTest::addColumn<bool>("overlay");
        QTest::addColumn<bool>("covered"); QTest::addColumn<bool>("compact");
        QTest::addColumn<bool>("expected");
        QTest::newRow("horizontal-single") << true << false << false << false << true;
        QTest::newRow("horizontal-inner-row") << true << false << false << false << true;
        QTest::newRow("horizontal-outer-row") << false << false << true << false << false;
        QTest::newRow("horizontal-expanded-overlay") << true << true << false << false << false;
        QTest::newRow("sticky-separated-row") << false << false << true << false << false;
        QTest::newRow("vertical-normal-row") << true << false << false << false << true;
        QTest::newRow("vertical-icon-row") << true << false << false << false << true;
        QTest::newRow("vertical-sticky-normal-row") << true << false << false << false << true;
        QTest::newRow("vertical-compact-tile") << true << false << false << true << false;
        QTest::newRow("vertical-peek") << true << true << false << false << false;
    }

    void attachmentStates()
    {
        QFETCH(bool, adjacent); QFETCH(bool, overlay); QFETCH(bool, covered); QFETCH(bool, compact); QFETCH(bool, expected);
        auto input = topGeometry();
        input.adjacent = adjacent; input.overlay = overlay; input.covered = covered; input.compact = compact;
        QCOMPARE(surfaceGeometry(input).joined, expected);
    }

    void orientationsKeepTheSameJoint()
    {
        for (auto edge : {SurfaceEdge::Top, SurfaceEdge::Bottom, SurfaceEdge::Left, SurfaceEdge::Right}) {
            auto input = topGeometry(); const auto transform = orientation(edge);
            input.body = transform.mapRect(input.body); input.anchor = transform.mapRect(input.anchor); input.edge = edge;
            const auto actual = surfaceGeometry(input);
            QVERIFY(actual.joined);
            QCOMPARE(actual.joinStart, 100);
            QCOMPARE(actual.joinEnd, 220);
            QVERIFY(actual.mergedPath.contains(transform.map(QPointF(160, 80))));
            QCOMPARE(actual.anchorPath.boundingRect(), transform.mapRect(QRectF(96, 52, 128, 28)));
        }
    }

    void clippedEndsHaveNoShoulder()
    {
        auto input = topGeometry();
        input.visibleAnchor = {140, 52, 80, 28};
        const auto actual = surfaceGeometry(input);
        QVERIFY(actual.joined);
        QCOMPARE(actual.joinStart, 140);
        QCOMPARE(actual.anchorPath.boundingRect().left(), 140);
        QCOMPARE(actual.anchorPath.boundingRect().right(), 224);
        input.visibleAnchor = {600, 52, 20, 28};
        const auto hidden = surfaceGeometry(input);
        QVERIFY(!hidden.joined);
        QVERIFY(hidden.anchorPath.isEmpty());
    }

    void translatingKeepsAllPartsInTheSameSpace()
    {
        for (auto edge : {SurfaceEdge::Top, SurfaceEdge::Bottom, SurfaceEdge::Left, SurfaceEdge::Right}) {
            auto input = topGeometry(); const auto transform = orientation(edge);
            input.body = transform.mapRect(input.body); input.anchor = transform.mapRect(input.anchor); input.edge = edge;
            const auto original = surfaceGeometry(input);
            const QPointF delta(37, 81);
            const auto moved = translatedSurfaceGeometry(original, delta);
            QCOMPARE(moved.body, original.body.translated(delta));
            QCOMPARE(moved.anchor, original.anchor.translated(delta));
            QCOMPARE(moved.anchorPath, original.anchorPath.translated(delta));
            QCOMPARE(moved.bodyPath, original.bodyPath.translated(delta));
            QCOMPARE(moved.mergedPath, original.mergedPath.translated(delta));
            const qreal axis = edge == SurfaceEdge::Top || edge == SurfaceEdge::Bottom ? delta.x() : delta.y();
            QCOMPARE(moved.joinStart, original.joinStart + axis);
            QCOMPARE(moved.joinEnd, original.joinEnd + axis);
        }
    }

    void bodyCornersLimitTheJoint()
    {
        auto input = topGeometry(); input.anchor.moveLeft(22);
        const auto actual = surfaceGeometry(input);
        QCOMPARE(actual.joinStart, 28);
        QCOMPARE(actual.anchorPath.boundingRect().left(), 22);
    }

    void cornerLimitAlsoClipsTheActualExtension()
    {
        for (auto edge : {SurfaceEdge::Top, SurfaceEdge::Bottom, SurfaceEdge::Left, SurfaceEdge::Right}) {
            auto input = topGeometry();
            input.anchor = {22, 47, 120, 28};
            const auto transform = orientation(edge);
            input.body = transform.mapRect(input.body); input.anchor = transform.mapRect(input.anchor); input.edge = edge;
            const auto actual = surfaceGeometry(input);
            QVERIFY(actual.joined);
            // Existing label area is retained; only its extension is
            // restricted to the body's straight edge.
            QVERIFY(actual.anchorPath.contains(transform.map(QPointF(23, 70))));
            QVERIFY(!actual.anchorPath.contains(transform.map(QPointF(23, 78))));
            QVERIFY(actual.anchorPath.contains(transform.map(QPointF(30, 78))));
            QVERIFY(!actual.anchorPath.contains(transform.map(QPointF(22, 81))));
            QVERIFY(!actual.mergedPath.contains(transform.map(QPointF(22, 81))));
        }
    }

    void verticalPeekInterpolatesTheExistingGeometry()
    {
        auto input = topGeometry();
        input.body.moveTop(85);
        input.edge = SurfaceEdge::Left;
        const auto rotate = orientation(input.edge);
        input.body = rotate.mapRect(input.body); input.anchor = rotate.mapRect(input.anchor);
        qreal previous = -1;
        for (int step = 0; step <= 10; ++step) {
            input.joinProgress = step / 10.0;
            const auto actual = surfaceGeometry(input);
            QCOMPARE(actual.anchor.right(), 80 + 5 * input.joinProgress);
            QVERIFY(actual.anchor.right() >= previous);
            previous = actual.anchor.right();
        }
    }

    void coveredSideMorphsWithTheSameProgress()
    {
        auto input = topGeometry();
        input.covered = true;
        input.adjacent = false;
        for (int step = 0; step <= 10; ++step) {
            input.coveredProgress = step / 10.0;
            const auto actual = surfaceGeometry(input);
            QCOMPARE(actual.coveredProgress, input.coveredProgress);
            QCOMPARE(surfaceShadowClip(actual, 9).bottom(), actual.anchor.bottom() + 9 * (1 - input.coveredProgress));
        }
        const auto covered = surfaceGeometry(input);
        QVERIFY(covered.anchorPath.contains(QPointF(100.1, 79.9)));
        QVERIFY(!covered.anchorPath.contains(QPointF(100.1, 52.1)));
    }

    void cachedShadowsDoNotGrowWithTheBodyOrPointerMoves()
    {
        QImage target(800, 500, QImage::Format_ARGB32_Premultiplied);
        target.fill(Qt::transparent);
        QPainter painter(&target);
        ElevationRenderer renderer;
        const auto bodyLevel = elevationProfile(ElevationLevel::BodyRaised);
        const auto anchorLevel = elevationProfile(ElevationLevel::AnchorRaised);
        const ElevationStyle style;
        renderer.paintBody(painter, {20, 80, 400, 300}, 8, bodyLevel, style);
        const auto first = renderer.cacheStats();
        renderer.paintBody(painter, {20, 80, 40000, 30000}, 8, bodyLevel, style);
        QCOMPARE(renderer.cacheStats().generatedImages, first.generatedImages);
        QVERIFY(first.largestImageBytes < 100 * 100 * 4);

        auto input = topGeometry();
        renderer.paintAnchor(painter, surfaceGeometry(input), bodyLevel, anchorLevel, style);
        const auto beforeMove = renderer.cacheStats();
        for (int offset = 1; offset <= 100; ++offset) {
            input.anchor.moveLeft(100 + offset);
            renderer.paintAnchor(painter, surfaceGeometry(input), bodyLevel, anchorLevel, style);
        }
        QCOMPARE(renderer.cacheStats().generatedImages, beforeMove.generatedImages);
        QVERIFY(renderer.cacheStats().bytes <= 16 * 1024 * 1024);
    }
};

QTEST_GUILESS_MAIN(SurfaceElevationTest)
#include "surface_elevation_test.moc"
