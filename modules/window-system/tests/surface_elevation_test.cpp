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

    void bodyResourcesOutliveTheirCacheWithoutCopyingPixels()
    {
        for (qreal ratio : {1.0, 1.25, 2.0}) {
            ElevationBodyPatch retained;
            {
                ElevationRenderer renderer;
                retained = renderer.bodyPatch(8, elevationProfile(ElevationLevel::BodyRaised), {}, ratio);
                const auto reused = renderer.bodyPatch(8, elevationProfile(ElevationLevel::BodyRaised), {}, ratio);
                QCOMPARE(retained.image.constBits(), reused.image.constBits());
                renderer.clear();
            }
            QCOMPARE(retained.devicePixelRatio, ratio);
            QVERIFY(!retained.image.isNull());
            QVERIFY(retained.image.sizeInBytes() < 200 * 200 * 4);
            const QRectF body(20, 80, 40000, 30000);
            QRectF extent;
            for (const auto& quad : retained.quads(body, 8)) {
                extent |= quad.target;
                QVERIFY(QRectF(retained.image.rect()).contains(quad.sourcePixels));
                QVERIFY(!quad.target.intersects(body.adjusted(80, 80, -80, -80)));
            }
            QCOMPARE(extent, body.adjusted(-retained.bodyInset, -retained.bodyInset,
                                           retained.bodyInset, retained.bodyInset));
        }
    }

    void anchorResourcesRetainPixelsAcrossMovementAndCacheRelease()
    {
        const auto body = elevationProfile(ElevationLevel::BodyRaised);
        const auto anchor = elevationProfile(ElevationLevel::AnchorRaised);
        for (auto edge : {SurfaceEdge::Top, SurfaceEdge::Bottom, SurfaceEdge::Left, SurfaceEdge::Right}) {
            for (qreal ratio : {1.0, 1.25, 2.0}) {
                auto input = topGeometry(); const auto transform = orientation(edge);
                input.body = transform.mapRect(input.body); input.anchor = transform.mapRect(input.anchor); input.edge = edge;
                ElevationRenderer renderer;
                const auto geometry = surfaceGeometry(input);
                const auto first = renderer.anchorPatch(geometry, body, anchor, {}, ratio);
                QVERIFY(!first.image.isNull());
                QVERIFY(first.image.sizeInBytes() < 400 * 200 * 4);
                const auto images = renderer.cacheStats().generatedImages;
                const auto movedGeometry = translatedSurfaceGeometry(geometry, {37, 81});
                const auto moved = renderer.anchorPatch(movedGeometry, body, anchor, {}, ratio);
                QCOMPARE(moved.image.constBits(), first.image.constBits());
                QCOMPARE(moved.bounds, first.bounds.translated(37, 81));
                QCOMPARE(moved.clip, first.clip.translated(37, 81));
                QCOMPARE(renderer.cacheStats().generatedImages, images);
                // A distant body edge does not enlarge this local resource.
                input.body.setSize(input.body.size() * 10);
                if (edge == SurfaceEdge::Top || edge == SurfaceEdge::Left) {
                    const auto enlarged = renderer.anchorPatch(surfaceGeometry(input), body, anchor, {}, ratio);
                    QCOMPARE(enlarged.image.constBits(), first.image.constBits());
                }
                renderer.clear();
                QVERIFY(!first.image.isNull());
                QCOMPARE(first.image.constBits(), moved.image.constBits());
            }
        }
    }
};

QTEST_GUILESS_MAIN(SurfaceElevationTest)
#include "surface_elevation_test.moc"
