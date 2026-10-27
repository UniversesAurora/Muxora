// SPDX-License-Identifier: MIT
#include "quick/path_geometry.hpp"
#include "docking/surface_geometry.hpp"

#include <QTest>
#include <QTransform>
#include <array>
#include <cmath>
#include <limits>

using namespace window_system;
using namespace window_system::quick;

namespace {
qreal cross(QPointF a, QPointF b, QPointF c)
{
    const auto ab = b - a, ac = c - a;
    return ab.x() * ac.y() - ab.y() * ac.x();
}

QPointF point(const PathVertex& vertex) { return {vertex.x, vertex.y}; }

qreal meshArea(const PathMesh& mesh)
{
    qreal result = 0;
    for (int i = 0; i < mesh.indices.size(); i += 3)
        result += cross(point(mesh.vertices[mesh.indices[i]]), point(mesh.vertices[mesh.indices[i + 1]]),
                        point(mesh.vertices[mesh.indices[i + 2]])) / 2;
    return result;
}

bool meshContains(const PathMesh& mesh, QPointF sample)
{
    for (int i = 0; i < mesh.indices.size(); i += 3) {
        const auto a = point(mesh.vertices[mesh.indices[i]]);
        const auto b = point(mesh.vertices[mesh.indices[i + 1]]);
        const auto c = point(mesh.vertices[mesh.indices[i + 2]]);
        if (cross(a, b, sample) >= -0.000001 && cross(b, c, sample) >= -0.000001
            && cross(c, a, sample) >= -0.000001) return true;
    }
    return false;
}

QPainterPath polygon(std::initializer_list<QPointF> vertices)
{
    QPainterPath path;
    if (vertices.size() == 0) return path;
    auto iterator = vertices.begin(); path.moveTo(*iterator++);
    while (iterator != vertices.end()) path.lineTo(*iterator++);
    path.closeSubpath(); return path;
}

PathGeometryOptions solidOptions()
{
    PathGeometryOptions options; options.antialiasing = false; return options;
}

void verifyMesh(const PathMesh& mesh)
{
    QCOMPARE(mesh.indices.size() % 3, 0);
    for (const auto index : mesh.indices) QVERIFY(index < mesh.vertices.size());
    for (const auto& vertex : mesh.vertices) {
        QVERIFY(std::isfinite(vertex.x)); QVERIFY(std::isfinite(vertex.y));
        QVERIFY(vertex.coverage >= 0 && vertex.coverage <= 1);
    }
    for (int i = 0; i < mesh.indices.size(); i += 3)
        QVERIFY(cross(point(mesh.vertices[mesh.indices[i]]), point(mesh.vertices[mesh.indices[i + 1]]),
                      point(mesh.vertices[mesh.indices[i + 2]])) > 0);
}
} // namespace

class QuickPathTest final : public QObject {
    Q_OBJECT
private slots:
    void rectangleKeepsItsArea()
    {
        QPainterPath path; path.addRect({10, 20, 80, 30});
        const auto result = pathGeometry(path, solidOptions());
        QVERIFY(result.valid()); QCOMPARE(result.contourCount, 1);
        QCOMPARE(result.fill.vertices.size(), 4); QCOMPARE(result.fill.indices.size(), 6);
        QCOMPARE(result.area, 2400); QCOMPARE(meshArea(result.fill), 2400);
        QVERIFY(result.fringe.vertices.isEmpty()); verifyMesh(result.fill);
    }

    void concavePolygonDoesNotFillItsNotch()
    {
        const auto path = polygon({{0, 0}, {10, 0}, {10, 4}, {4, 4}, {4, 10}, {0, 10}});
        const auto result = pathGeometry(path, solidOptions());
        QVERIFY(result.valid()); QCOMPARE(result.area, 64); QCOMPARE(meshArea(result.fill), 64);
        for (qreal y = 0.5; y < 10; y += 1) for (qreal x = 0.5; x < 10; x += 1)
            QCOMPARE(meshContains(result.fill, {x, y}), path.contains({x, y}));
        verifyMesh(result.fill);
    }

    void duplicateAndCollinearPointsAreRemoved()
    {
        const auto path = polygon({{0, 0}, {10, 0}, {10, 0}, {20, 0}, {20, 10}, {10, 10}, {0, 10}});
        const auto result = pathGeometry(path, solidOptions());
        QVERIFY(result.valid()); QCOMPARE(result.flattenedVertexCount, 4);
        QCOMPARE(meshArea(result.fill), 200);
        const auto reversed = pathGeometry(path.toReversed(), solidOptions());
        QVERIFY(reversed.valid()); QCOMPARE(meshArea(reversed.fill), 200);
    }

    void separatedContoursRemainSeparated()
    {
        QPainterPath path; path.addRect({0, 0, 10, 10}); path.addRect({30, 0, 10, 10});
        const auto result = pathGeometry(path, solidOptions());
        QVERIFY(result.valid()); QCOMPARE(result.contourCount, 2);
        QCOMPARE(meshArea(result.fill), 200);
        QVERIFY(!meshContains(result.fill, {20, 5}));
        verifyMesh(result.fill);
    }

    void nestedContoursFailForBothFillRules_data()
    {
        QTest::addColumn<int>("rule");
        QTest::newRow("odd-even") << int(Qt::OddEvenFill);
        QTest::newRow("winding") << int(Qt::WindingFill);
    }

    void nestedContoursFailForBothFillRules()
    {
        QFETCH(int, rule);
        QPainterPath path; path.setFillRule(Qt::FillRule(rule));
        path.addRect({0, 0, 30, 30}); path.addRect({10, 10, 10, 10});
        const auto result = pathGeometry(path, solidOptions());
        QCOMPARE(result.error, PathGeometryError::NestedContours);
        QVERIFY(result.fill.vertices.isEmpty()); QVERIFY(result.fringe.vertices.isEmpty());
    }

    void crossingContoursFailAndSharedEdgesNormalize()
    {
        QPainterPath crossing; crossing.addRect({0, 0, 10, 10}); crossing.addRect({5, 5, 10, 10});
        const auto result = pathGeometry(crossing, solidOptions());
        QCOMPARE(result.error, PathGeometryError::IntersectingContours);
        QVERIFY(result.fill.vertices.isEmpty());
        QPainterPath touching; touching.addRect({0, 0, 10, 10}); touching.addRect({10, 0, 10, 10});
        const auto joined = pathGeometry(touching, solidOptions());
        QVERIFY(joined.valid()); QCOMPARE(meshArea(joined.fill), 200);
        const auto crossed = polygon({{0, 0}, {10, 10}, {0, 10}, {10, 0}});
        QCOMPARE(pathGeometry(crossed, solidOptions()).error, PathGeometryError::SelfIntersection);
    }

    void sharedEdgeNormalizationKeepsTheFillRule_data()
    {
        QTest::addColumn<int>("rule"); QTest::addColumn<qreal>("area");
        QTest::newRow("odd-even") << int(Qt::OddEvenFill) << qreal(100);
        QTest::newRow("winding") << int(Qt::WindingFill) << qreal(150);
    }

    void sharedEdgeNormalizationKeepsTheFillRule()
    {
        QFETCH(int, rule); QFETCH(qreal, area);
        QPainterPath path; path.setFillRule(Qt::FillRule(rule));
        path.addRect({0, 0, 10, 10}); path.addRect({5, 0, 10, 10});
        const auto result = pathGeometry(path, solidOptions());
        QVERIFY(result.valid()); QCOMPARE(meshArea(result.fill), area);
        for (qreal x = 0.5; x < 15; x += 1)
            QCOMPARE(meshContains(result.fill, {x, 5}), path.contains({x, 5}));
    }

    void antialiasingBandIsOnePhysicalPixel_data()
    {
        QTest::addColumn<qreal>("ratio");
        QTest::newRow("one") << qreal(1);
        QTest::newRow("fractional") << qreal(1.5);
        QTest::newRow("two") << qreal(2);
        QTest::newRow("four") << qreal(4);
    }

    void antialiasingBandIsOnePhysicalPixel()
    {
        QFETCH(qreal, ratio);
        QPainterPath path; path.addRect({0, 0, 40, 20});
        PathGeometryOptions options; options.devicePixelRatio = ratio;
        const auto result = pathGeometry(path, options);
        QVERIFY(result.valid()); verifyMesh(result.fill); verifyMesh(result.fringe);
        QCOMPARE(result.fringe.vertices.size(), 8); QCOMPARE(result.fringe.indices.size(), 24);
        const qreal half = 0.5 / ratio;
        for (int i = 0; i < result.fringe.vertices.size(); i += 2) {
            const auto& inside = result.fringe.vertices[i]; const auto& outside = result.fringe.vertices[i + 1];
            QCOMPARE(inside.coverage, 1); QCOMPARE(outside.coverage, 0);
            QVERIFY(std::abs(std::abs(inside.x - outside.x) - 2 * half) < 0.00001);
            QVERIFY(std::abs(std::abs(inside.y - outside.y) - 2 * half) < 0.00001);
        }
        QVERIFY(std::abs(meshArea(result.fill) - (40 - 2 * half) * (20 - 2 * half)) < 0.001);
        QVERIFY(std::abs(meshArea(result.fringe) - ((40 + 2 * half) * (20 + 2 * half)
            - (40 - 2 * half) * (20 - 2 * half))) < 0.001);
    }

    void antialiasingRejectsChangedTopology()
    {
        QPainterPath thin; thin.addRect({0, 0, 0.25, 10});
        QCOMPARE(pathGeometry(thin).error, PathGeometryError::AntialiasingTopology);
        QPainterPath close; close.addRect({0, 0, 10, 10}); close.addRect({10.5, 0, 10, 10});
        QCOMPARE(pathGeometry(close).error, PathGeometryError::AntialiasingTopology);
        QVERIFY(pathGeometry(close, solidOptions()).valid());
    }

    void joinedSurfacesCoverAllOrientations_data()
    {
        QTest::addColumn<int>("edge"); QTest::addColumn<qreal>("ratio");
        for (int edge = 0; edge < 4; ++edge) for (const qreal ratio : {1., 1.5, 2., 4.})
            QTest::newRow(qPrintable(QStringLiteral("%1-%2").arg(edge).arg(ratio))) << edge << ratio;
    }

    void joinedSurfacesCoverAllOrientations()
    {
        QFETCH(int, edge); QFETCH(qreal, ratio);
        const auto side = SurfaceEdge(edge);
        QTransform transform;
        switch (side) {
        case SurfaceEdge::Top: break;
        case SurfaceEdge::Bottom: transform = QTransform(1, 0, 0, -1, 0, 0); break;
        case SurfaceEdge::Left: transform = QTransform(0, 1, 1, 0, 0, 0); break;
        case SurfaceEdge::Right: transform = QTransform(0, 1, -1, 0, 0, 0); break;
        }
        SurfaceGeometryInput input;
        input.body = transform.mapRect(QRectF(20, 80, 220, 140));
        input.anchor = transform.mapRect(QRectF(65, 52, 100, 28)); input.edge = side;
        const auto geometry = surfaceGeometry(input);
        QVERIFY(geometry.joined);
        auto options = solidOptions(); options.devicePixelRatio = ratio;
        const auto result = pathGeometry(geometry.mergedPath, options);
        QVERIFY2(result.valid(), qPrintable(QString::number(int(result.error))));
        verifyMesh(result.fill);
        QVERIFY(result.area > input.body.width() * input.body.height());
        QVERIFY(std::abs(meshArea(result.fill) - result.area) < 0.05);
        for (const QPointF sample : {QPointF(130, 150), QPointF(100, 65), QPointF(10, 50), QPointF(30, 55)})
            QCOMPARE(meshContains(result.fill, transform.map(sample)), geometry.mergedPath.contains(transform.map(sample)));
    }

    void surfaceContoursAndOptionalCoverage_data()
    {
        QTest::addColumn<int>("edge"); QTest::addColumn<qreal>("ratio");
        QTest::addColumn<int>("state"); QTest::addColumn<bool>("anchorOnly");
        QTest::addColumn<bool>("coverage"); QTest::addColumn<bool>("coverageSupported");
        const std::array names{"joined", "flush-leading", "flush-trailing", "near-leading", "near-trailing",
            "partial-join", "covered", "partial-cover", "compact", "overlay", "clipped", "compact-overlay"};
        for (int edge = 0; edge < 4; ++edge) for (const qreal ratio : {1., 2.})
            for (int state = 0; state < names.size(); ++state) for (const bool anchorOnly : {false, true})
                for (const bool coverage : {false, true}) {
                    // Closing a flared shoulder as an isolated anchor makes
                    // a zero-angle tip. Its inward AA offset self-intersects;
                    // the complete merged surface has no such boundary tip.
                    const bool shoulderTip = anchorOnly && (state <= 4 || state == 7 || state == 10);
                    // A partially joined surface has a sharp concave corner.
                    // The matching inner/outer AA segments cross there; fill
                    // remains supported but this coverage mesh cannot be used.
                    const bool partialJoinCorner = !anchorOnly && state == 5;
                    QTest::newRow(qPrintable(QStringLiteral("%1-%2-%3-%4-%5").arg(edge).arg(ratio)
                        .arg(QLatin1String(names[state])).arg(anchorOnly ? "anchor" : "merged")
                        .arg(coverage ? "coverage" : "fill")))
                        << edge << ratio << state << anchorOnly << coverage << !(shoulderTip || partialJoinCorner);
                }
    }

    void surfaceContoursAndOptionalCoverage()
    {
        QFETCH(int, edge); QFETCH(qreal, ratio); QFETCH(int, state); QFETCH(bool, anchorOnly);
        QFETCH(bool, coverage); QFETCH(bool, coverageSupported);
        const auto side = SurfaceEdge(edge);
        QTransform transform;
        switch (side) {
        case SurfaceEdge::Top: break;
        case SurfaceEdge::Bottom: transform = QTransform(1, 0, 0, -1, 0, 0); break;
        case SurfaceEdge::Left: transform = QTransform(0, 1, 1, 0, 0, 0); break;
        case SurfaceEdge::Right: transform = QTransform(0, 1, -1, 0, 0, 0); break;
        }
        SurfaceGeometryInput input;
        input.body = QRectF(20, 80, 220, 140); input.anchor = QRectF(65, 52, 100, 28);
        switch (state) {
        case 1: input.anchor.moveLeft(20); break;
        case 2: input.anchor.moveRight(240); break;
        case 3: input.anchor.moveLeft(23); break;
        case 4: input.anchor.moveRight(237); break;
        case 5: input.joinProgress = 0.5; break;
        case 6: input.covered = true; input.adjacent = false; input.anchor.translate(0, -30); break;
        case 7: input.covered = true; input.coveredProgress = 0.5; input.anchor.translate(0, -15); break;
        case 8: input.compact = true; input.anchor = QRectF(65, 48, 26, 26); break;
        case 9: input.overlay = true; input.anchor.translate(0, -15); break;
        case 10: input.visibleAnchor = input.anchor.adjusted(30, 0, 0, 0); break;
        case 11: input.compact = true; input.overlay = true; input.anchor = QRectF(65, 48, 26, 26); break;
        }
        input.body = transform.mapRect(input.body); input.anchor = transform.mapRect(input.anchor);
        if (!input.visibleAnchor.isNull()) input.visibleAnchor = transform.mapRect(input.visibleAnchor);
        input.edge = side;
        const auto geometry = surfaceGeometry(input);
        const auto& path = anchorOnly ? geometry.anchorPath : geometry.mergedPath;
        PathGeometryOptions options; options.devicePixelRatio = ratio; options.antialiasing = coverage;
        const auto result = pathGeometry(path, options);
        if (coverage && !coverageSupported) {
            QCOMPARE(result.error, PathGeometryError::AntialiasingTopology);
            QVERIFY(result.fill.vertices.isEmpty()); QVERIFY(result.fill.indices.isEmpty());
            QVERIFY(result.fringe.vertices.isEmpty()); QVERIFY(result.fringe.indices.isEmpty());
            return;
        }
        QVERIFY2(result.valid(), qPrintable(QStringLiteral("error %1, vertices %2, work %3")
            .arg(int(result.error)).arg(result.flattenedVertexCount).arg(result.work)));
        QVERIFY(!result.fill.indices.isEmpty()); QCOMPARE(result.fringe.indices.isEmpty(), !coverage);
        verifyMesh(result.fill); verifyMesh(result.fringe);
        if (!coverage) QVERIFY(std::abs(meshArea(result.fill) - result.area) < 0.1);
        for (int index = 0; index < result.fill.indices.size(); index += 3) {
            const auto a = point(result.fill.vertices[result.fill.indices[index]]);
            const auto b = point(result.fill.vertices[result.fill.indices[index + 1]]);
            const auto c = point(result.fill.vertices[result.fill.indices[index + 2]]);
            QVERIFY(path.contains((a + b + c) / 3));
        }
    }

    void curvesRespectDeviceScale()
    {
        QPainterPath path; path.addRoundedRect({0, 0, 200, 100}, 12, 12);
        auto options = solidOptions();
        const auto baseline = pathGeometry(path, options);
        QVERIFY(baseline.valid());
        options.devicePixelRatio = 4;
        const auto dense = pathGeometry(path, options);
        QVERIFY(dense.valid()); QVERIFY(dense.flattenedVertexCount > baseline.flattenedVertexCount);
        QVERIFY(std::abs(dense.area - baseline.area) < 40);
        verifyMesh(dense.fill);
    }

    void budgetsFailWithoutPartialMeshes()
    {
        QPainterPath path; path.addRect({0, 0, 40, 20});
        auto options = solidOptions(); options.maximumElements = 2;
        QCOMPARE(pathGeometry(path, options).error, PathGeometryError::ElementBudgetExceeded);
        options = solidOptions(); options.maximumVertices = 3;
        QCOMPARE(pathGeometry(path, options).error, PathGeometryError::VertexBudgetExceeded);
        options = solidOptions(); options.maximumWork = 1;
        const auto result = pathGeometry(path, options);
        QCOMPARE(result.error, PathGeometryError::WorkBudgetExceeded);
        QVERIFY(result.fill.vertices.isEmpty()); QVERIFY(result.fill.indices.isEmpty());
        options = solidOptions(); options.maximumPhysicalExtent = 10;
        QCOMPARE(pathGeometry(path, options).error, PathGeometryError::CoordinateBudgetExceeded);
    }

    void invalidOptionsAndDegenerateContoursFail()
    {
        QPainterPath path; path.addRect({0, 0, 40, 20});
        for (const qreal ratio : {0., -1., 17., std::numeric_limits<qreal>::infinity(),
                                  std::numeric_limits<qreal>::quiet_NaN()}) {
            auto options = solidOptions(); options.devicePixelRatio = ratio;
            QCOMPARE(pathGeometry(path, options).error, PathGeometryError::InvalidOptions);
        }
        const auto line = polygon({{0, 0}, {5, 0}, {10, 0}});
        QCOMPARE(pathGeometry(line, solidOptions()).error, PathGeometryError::DegenerateContour);
        const auto empty = pathGeometry({});
        QVERIFY(empty.valid()); QVERIFY(empty.fill.vertices.isEmpty()); QCOMPARE(empty.contourCount, 0);
    }

    void nonFinitePathDataFailsBeforeFlattening()
    {
        QPainterPath path; path.addRect({0, 0, 40, 20});
        path.setElementPositionAt(1, std::numeric_limits<qreal>::infinity(), 0);
        const auto result = pathGeometry(path, solidOptions());
        QCOMPARE(result.error, PathGeometryError::NonFiniteCoordinate);
        QVERIFY(result.fill.vertices.isEmpty()); QVERIFY(result.fringe.vertices.isEmpty());
    }

    void retainedGenerationChangesOnlyWithInputs()
    {
        QPainterPath path; path.addRect({0, 0, 40, 20});
        PathGeometryCache cache;
        QVERIFY(cache.update(path)); QCOMPARE(cache.generation(), 1);
        QVERIFY(!cache.update(path)); QCOMPARE(cache.generation(), 1);
        auto options = PathGeometryOptions{}; options.devicePixelRatio = 2;
        QVERIFY(cache.update(path, options)); QCOMPARE(cache.generation(), 2);
        QVERIFY(!cache.update(path, options));
        path.translate(5, 3);
        QVERIFY(cache.update(path, options)); QCOMPARE(cache.generation(), 3);
        QVERIFY(cache.geometry().valid());
    }

    void boundedFailuresAreRetainedButOversizedInputIsNot()
    {
        QPainterPath nested; nested.addRect({0, 0, 40, 40}); nested.addRect({10, 10, 10, 10});
        PathGeometryCache cache;
        QVERIFY(cache.update(nested)); QCOMPARE(cache.geometry().error, PathGeometryError::NestedContours);
        QVERIFY(!cache.update(nested)); QCOMPARE(cache.generation(), 1);
        QPainterPath oversized; oversized.moveTo(0, 0);
        for (int i = 1; i <= 4096; ++i) oversized.lineTo(i, i % 7);
        QVERIFY(cache.update(oversized)); QCOMPARE(cache.geometry().error, PathGeometryError::ElementBudgetExceeded);
        QVERIFY(cache.update(oversized)); QCOMPARE(cache.generation(), 3);
    }
};

QTEST_GUILESS_MAIN(QuickPathTest)
#include "quick_path_test.moc"
