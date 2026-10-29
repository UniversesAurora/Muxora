// SPDX-License-Identifier: MIT
// Quick presentation wiring: retained geometry/color updates, scene graph
// resource recreation, frame-paced motion, visibility and window ownership.
// This is not a visual baseline or a complete application performance test.
#include "quick/path_item.hpp"
#include "quick/body_shadow_item.hpp"
#include "quick/anchor_shadow_item.hpp"
#include "quick/group_surface_item.hpp"
#include "quick/tab_label_text_item.hpp"
#include "quick/window_motion.hpp"
#include "docking/surface_geometry.hpp"

#include <QGuiApplication>
#include <QPainter>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSignalSpy>
#include <QTest>
#include <limits>

using namespace window_system;
using namespace window_system::quick;

namespace {
QPainterPath rectangle(const QRectF& rect)
{
    QPainterPath path; path.addRect(rect); return path;
}

QColor pixelAt(const QImage& image, const QQuickWindow& window, QPointF point)
{
    return image.pixelColor(qRound(point.x() * image.width() / window.width()),
                            qRound(point.y() * image.height() / window.height()));
}

void prepareWindow(QQuickWindow& window)
{
    window.setTitle(QStringLiteral("WindowSystem Quick rendering test"));
    window.setColor(Qt::black);
    window.resize(360, 240);
    window.setPersistentGraphics(false);
    window.setPersistentSceneGraph(false);
}
} // namespace

class QuickRenderingTest final : public QObject {
    Q_OBJECT
private slots:
    void tabLabelTextKeepsShapingAcrossFadeAndRecreatesSceneResources()
    {
        QQuickWindow first, second;
        prepareWindow(first);
        prepareWindow(second);
        auto* item = new TabLabelTextItem(first.contentItem());

        TabLabelPresentation label;
        label.parts.tab = QRect(20, 20, 240, 28);
        label.parts.title = QRect(60, 20, 150, 28);
        label.parts.action = QRect(24, 24, 20, 20);
        label.state.shape = TabShape::Top;
        label.state.title = QStringLiteral("A long tab title that is clipped");
        label.state.actionShown = true;
        label.naturalWidth = 360;
        TabStripLabelStyle style;
        style.metrics.rowHeight = 28;
        style.palette.text = Qt::black;
        style.palette.mutedText = Qt::darkGray;

        QVERIFY(item->setLabel(label, style));
        QVERIFY(item->segmentCount() > 1);
        QVERIFY(item->segmentCount() <= 40);
        first.show();
        QVERIFY(QTest::qWaitForWindowExposed(&first));
        QVERIFY(!first.grabWindow().isNull());
        QTRY_VERIFY(item->renderStats().textLayoutBuilds > 0);
        const auto initialBuilds = item->renderStats().textLayoutBuilds;

        label.state.actionOpacity = 0.5;
        QVERIFY(item->setLabel(label, style));
        QVERIFY(!first.grabWindow().isNull());
        QCOMPARE(item->renderStats().textLayoutBuilds, initialBuilds);
        QVERIFY(item->renderStats().clipUpdates > 0);

        label.state.title = QStringLiteral("Changed source keeps the same bounds");
        QVERIFY(item->setLabel(label, style));
        QVERIFY(!first.grabWindow().isNull());
        QVERIFY(item->renderStats().textLayoutBuilds > initialBuilds);
        const auto changedBuilds = item->renderStats().textLayoutBuilds;

        item->setParentItem(second.contentItem());
        item->setParent(second.contentItem());
        second.show();
        QVERIFY(QTest::qWaitForWindowExposed(&second));
        QVERIFY(!second.grabWindow().isNull());
        QTRY_VERIFY(item->renderStats().textLayoutBuilds > changedBuilds);
        const auto movedBuilds = item->renderStats().textLayoutBuilds;

        second.hide();
        QTRY_VERIFY(!second.isExposed());
        second.releaseResources();
        second.show();
        QVERIFY(QTest::qWaitForWindowExposed(&second));
        QVERIFY(!second.grabWindow().isNull());
        QTRY_VERIFY(item->renderStats().textLayoutBuilds > movedBuilds);

        item->clearLabel();
        QCOMPARE(item->segmentCount(), 0);
        QVERIFY(!second.grabWindow().isNull());
    }

    void groupFramesKeepResourcesAcrossRejectedInputAndWindowChanges()
    {
        QQuickWindow first, second; prepareWindow(first); prepareWindow(second);
        auto* item = new GroupSurfaceItem(first.contentItem());
        item->setSize(first.size());
        const auto makeFrame = [&] {
            SurfaceGeometryInput input; input.body = {20, 60, 280, 140}; input.anchor = {80, 32, 120, 28};
            const auto geometry = surfaceGeometry(input);
            TabGroupSurfaceFrame frame;
            frame.groupBounds = QRect(QPoint(), first.size()); frame.body = geometry.body; frame.radius = geometry.bodyRadius;
            frame.elevation = elevationProfile(ElevationLevel::BodyRest);
            frame.anchors.append({geometry, elevationProfile(ElevationLevel::AnchorRaised)});
            prepareTabGroupSurfaceFrame(frame);
            return frame;
        };
        // The source frame dies before the first scene-graph synchronization.
        QVERIFY(item->setFrame(makeFrame()));
        first.show(); QVERIFY(QTest::qWaitForWindowExposed(&first));
        QTRY_VERIFY(item->renderStats().textureUploads > 0);
        const auto originalUploads = item->renderStats().textureUploads;
        auto invalid = makeFrame(); invalid.radius = std::numeric_limits<qreal>::infinity();
        ElevationRenderer resources;
        const auto images = resources.cacheStats().generatedImages;
        QVERIFY(!item->setFrame(invalid));
        QCOMPARE(resources.cacheStats().generatedImages, images);
        auto replacement = makeFrame(); replacement.style.shadow = QColor(Qt::blue);
        // No event processing between rejection and replacement: the old GPU
        // nodes still exist, so a changed resource must get a new revision.
        QVERIFY(item->setFrame(replacement));
        QTRY_VERIFY(item->renderStats().textureUploads > originalUploads);
        QVERIFY(!item->setFrame(invalid));
        const auto error = item->geometryError();
        QVERIFY(error != PathGeometryError::None);
        item->setParentItem(second.contentItem()); item->setParent(second.contentItem());
        QCOMPARE(item->geometryError(), error);
        const auto beforeMove = item->renderStats().textureUploads;
        QVERIFY(item->setFrame(makeFrame()));
        second.show(); QVERIFY(QTest::qWaitForWindowExposed(&second));
        QTRY_VERIFY(item->renderStats().textureUploads > beforeMove);
        const auto beforeRelease = item->renderStats().textureUploads;
        QSignalSpy released(&second, &QQuickWindow::sceneGraphInvalidated);
        second.hide(); QTRY_VERIFY(!second.isExposed()); second.releaseResources();
        QTRY_VERIFY(!released.isEmpty());
        second.show(); QVERIFY(QTest::qWaitForWindowExposed(&second));
        QTRY_VERIFY(item->renderStats().textureUploads > beforeRelease);
    }

    void joinedShadowsPreserveCompositionAndReuseTextures_data()
    {
        QTest::addColumn<int>("edge");
        QTest::addColumn<bool>("incremental");
        for (auto edge : {SurfaceEdge::Top, SurfaceEdge::Bottom, SurfaceEdge::Left, SurfaceEdge::Right})
            for (bool incremental : {false, true})
                QTest::newRow(qPrintable(QStringLiteral("edge-%1-incremental-%2").arg(int(edge)).arg(incremental)))
                    << int(edge) << incremental;
    }

    void joinedShadowsPreserveCompositionAndReuseTextures()
    {
        QFETCH(int, edge);
        QFETCH(bool, incremental);
        QTransform transform;
        switch (SurfaceEdge(edge)) {
        case SurfaceEdge::Top: break;
        case SurfaceEdge::Bottom: transform = QTransform(1, 0, 0, -1, 0, 0); break;
        case SurfaceEdge::Left: transform = QTransform(0, 1, 1, 0, 0, 0); break;
        case SurfaceEdge::Right: transform = QTransform(0, 1, -1, 0, 0, 0); break;
        }
        SurfaceGeometryInput input;
        input.body = transform.mapRect(QRectF(0, 40, 260, 130));
        input.anchor = transform.mapRect(QRectF(90, 12, 80, 28)); input.edge = SurfaceEdge(edge);
        auto geometry = surfaceGeometry(input);
        const auto delta = QPointF(48, 40) - geometry.mergedPath.boundingRect().topLeft();
        geometry = translatedSurfaceGeometry(geometry, delta);
        QQuickWindow window; prepareWindow(window); window.setColor(Qt::white);
        window.resize(440, 360);
        auto* body = new BodyShadowItem(window.contentItem());
        auto* anchor = new AnchorShadowItem(window.contentItem());
        const auto bodyProfile = elevationProfile(ElevationLevel::BodyRaised);
        const auto anchorProfile = elevationProfile(ElevationLevel::AnchorRaised);
        body->setPosition(geometry.body.topLeft()); body->setSize(geometry.body.size());
        body->setShadow(geometry.bodyRadius, bodyProfile, {});
        anchor->setShadow(geometry, bodyProfile, anchorProfile, {}, incremental);
        anchor->setOpacity(incremental ? 0.5 : 1);
        body->setExcluded(incremental ? QRectF{} : anchor->patchClip().translated(-geometry.body.topLeft()));
        window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto image = window.grabWindow(); QVERIFY(!image.isNull());
        QImage expected(image.size(), QImage::Format_RGB32); expected.fill(Qt::white);
        expected.setDevicePixelRatio(window.effectiveDevicePixelRatio());
        ElevationRenderer renderer; QPainter painter(&expected);
        renderer.paintBody(painter, geometry.body, geometry.bodyRadius, bodyProfile, {}, incremental ? QRectF{} : anchor->patchClip());
        painter.setOpacity(incremental ? 0.5 : 1);
        renderer.paintAnchor(painter, geometry, bodyProfile, anchorProfile, {}, incremental); painter.end();
        // Samples across the connection and both perimeters detect additive
        // darkening or a missing body segment, independently of fill AA.
        for (const auto point : {QPointF(83, 36), QPointF(20, 37), QPointF(130, 7),
                                 QPointF(265, 100), QPointF(130, 175), QPointF(130, 60)}) {
            const auto at = transform.map(point) + delta;
            QVERIFY2(qAbs(pixelAt(image, window, at).red() - pixelAt(expected, window, at).red()) <= 2,
                     qPrintable(QStringLiteral("shadow sample %1,%2").arg(at.x()).arg(at.y())));
        }
        const auto warmBody = body->renderStats(), warmAnchor = anchor->renderStats();
        const auto images = renderer.cacheStats().generatedImages;
        const auto shift = transform.map(QPointF(5, 0));
        input.anchor.translate(shift);
        geometry = translatedSurfaceGeometry(surfaceGeometry(input), delta);
        anchor->setShadow(geometry, bodyProfile, anchorProfile, {}, incremental);
        body->setExcluded(incremental ? QRectF{} : anchor->patchClip().translated(-geometry.body.topLeft()));
        QVERIFY(!window.grabWindow().isNull());
        QCOMPARE(anchor->renderStats().textureUploads, warmAnchor.textureUploads);
        QCOMPARE(body->renderStats().textureUploads, warmBody.textureUploads);
        QCOMPARE(renderer.cacheStats().generatedImages, images);
        if (!incremental) QVERIFY(body->renderStats().geometryUpdates > warmBody.geometryUpdates);
        QVERIFY(anchor->renderStats().geometryUpdates > warmAnchor.geometryUpdates);
    }

    void anchorTexturesRespectCoverageAndWindowOwnership()
    {
        QQuickWindow first, second; prepareWindow(first); prepareWindow(second);
        first.setColor(Qt::white); second.setColor(Qt::white);
        auto* item = new AnchorShadowItem(first.contentItem());
        SurfaceGeometryInput input; input.body = {20, 80, 300, 130}; input.anchor = {100, 52, 120, 28};
        input.covered = true;
        auto style = ElevationStyle{}; style.highContrast = true; style.contrastBorder = Qt::red;
        item->setShadow(surfaceGeometry(input), elevationProfile(ElevationLevel::BodyRest),
            elevationProfile(ElevationLevel::AnchorRaised), style);
        first.show(); QVERIFY(QTest::qWaitForWindowExposed(&first));
        const auto image = first.grabWindow();
        const auto border = pixelAt(image, first, {160, 52});
        QVERIFY(border.red() > border.green());
        QCOMPARE(pixelAt(image, first, {160, 82}), QColor(Qt::white));
        const auto uploads = item->renderStats().textureUploads;
        first.hide(); QTRY_VERIFY(!first.isExposed()); first.releaseResources();
        first.show(); QVERIFY(QTest::qWaitForWindowExposed(&first));
        QVERIFY(!first.grabWindow().isNull());
        QVERIFY(item->renderStats().textureUploads > uploads);
        const auto recreated = item->renderStats().textureUploads;
        item->setParentItem(second.contentItem()); item->setParent(second.contentItem());
        second.show(); QVERIFY(QTest::qWaitForWindowExposed(&second));
        const auto movedBorder = pixelAt(second.grabWindow(), second, {160, 52});
        QVERIFY(movedBorder.red() > movedBorder.green());
        QCOMPARE(item->renderStats().textureUploads, recreated + 1);
        QCOMPARE(pixelAt(first.grabWindow(), first, {160, 52}), QColor(Qt::white));
    }

    void bodyShadowsResizeWithoutNewImagesOrUploads()
    {
        QQuickWindow window;
        prepareWindow(window); window.setColor(Qt::white);
        auto* shadow = new BodyShadowItem(window.contentItem());
        shadow->setPosition({40, 40}); shadow->setSize({240, 140});
        const auto profile = elevationProfile(ElevationLevel::BodyRaised);
        shadow->setShadow(8, profile, {});
        window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.rendererInterface()->graphicsApi() != QSGRendererInterface::Software);
        auto image = window.grabWindow(); QVERIFY(!image.isNull());
        const auto below = pixelAt(image, window, {160, 182});
        QVERIFY(below.red() < 250); QCOMPARE(below.red(), below.green());
        QCOMPARE(pixelAt(image, window, {160, 100}), QColor(Qt::white));
        const auto warm = shadow->renderStats();
        QCOMPARE(warm.textureUploads, 1);
        QVERIFY(warm.imageBytesUploaded < 200 * 200 * 4);
        ElevationRenderer cache;
        const auto images = cache.cacheStats().generatedImages;
        for (int width : {180, 200, 260, 210}) {
            shadow->setWidth(width);
            QVERIFY(!window.grabWindow().isNull());
        }
        QCOMPARE(shadow->renderStats().textureUploads, warm.textureUploads);
        QCOMPARE(shadow->renderStats().imageBytesUploaded, warm.imageBytesUploaded);
        QVERIFY(shadow->renderStats().geometryUpdates > warm.geometryUpdates);
        QCOMPARE(cache.cacheStats().generatedImages, images);

        ElevationStyle contrast;
        contrast.highContrast = true; contrast.contrastBorder = Qt::red;
        shadow->setShadow(8, profile, contrast);
        image = window.grabWindow();
        const auto edge = pixelAt(image, window, {40, 100});
        QVERIFY(edge.red() > edge.green());
        QCOMPARE(shadow->renderStats().textureUploads, warm.textureUploads + 1);
        shadow->setShadow(8, elevationProfile(ElevationLevel::Flat), contrast);
        QCOMPARE(pixelAt(window.grabWindow(), window, {40, 100}), QColor(Qt::white));
    }

    void bodyShadowTexturesFollowSceneGraphOwnership()
    {
        QQuickWindow first, second;
        prepareWindow(first); prepareWindow(second);
        first.setColor(Qt::white); second.setColor(Qt::white);
        auto* shadow = new BodyShadowItem(first.contentItem());
        shadow->setPosition({40, 40}); shadow->setSize({240, 140});
        shadow->setShadow(8, elevationProfile(ElevationLevel::BodyRaised), {});
        first.show(); QVERIFY(QTest::qWaitForWindowExposed(&first));
        QVERIFY(!first.grabWindow().isNull());
        const auto initial = shadow->renderStats().textureUploads;
        first.hide(); QTRY_VERIFY(!first.isExposed()); first.releaseResources();
        first.show(); QVERIFY(QTest::qWaitForWindowExposed(&first));
        QVERIFY(!first.grabWindow().isNull());
        QVERIFY(shadow->renderStats().textureUploads > initial);
        const auto recreated = shadow->renderStats().textureUploads;
        shadow->setParentItem(second.contentItem()); shadow->setParent(second.contentItem());
        second.show(); QVERIFY(QTest::qWaitForWindowExposed(&second));
        QVERIFY(pixelAt(second.grabWindow(), second, {160, 182}).red() < 250);
        QCOMPARE(shadow->renderStats().textureUploads, recreated + 1);
        QCOMPARE(pixelAt(first.grabWindow(), first, {160, 182}), QColor(Qt::white));
    }

    void multisampleTarget_acceptsPartialJoinsAndShoulderTips()
    {
        QQuickWindow window;
        prepareWindow(window);
        auto format = window.format(); format.setSamples(4); window.setFormat(format);
        auto* item = new PathItem(window.contentItem(), PathAntialiasing::Multisample);
        item->setSize(QSizeF(360, 240)); item->setFill(Qt::red);
        SurfaceGeometryInput input;
        input.body = QRectF(20, 80, 220, 140); input.anchor = QRectF(65, 52, 100, 28);
        input.joinProgress = 0.5;
        const auto geometry = surfaceGeometry(input);
        QVERIFY(item->setPath(geometry.mergedPath));
        window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto image = window.grabWindow(); QVERIFY(!image.isNull());
        QCOMPARE(pixelAt(image, window, {100, 65}), QColor(Qt::red));
        QCOMPARE(pixelAt(image, window, {130, 150}), QColor(Qt::red));
        QVERIFY(item->setPath(geometry.anchorPath));
        image = window.grabWindow();
        QCOMPARE(pixelAt(image, window, {100, 65}), QColor(Qt::red));
        QCOMPARE(pixelAt(image, window, {130, 150}), QColor(Qt::black));
    }

    void geometryAndMaterialChanges_reachTheGpu()
    {
        QQuickWindow window;
        prepareWindow(window);
        auto* item = new PathItem(window.contentItem());
        item->setSize(QSizeF(360, 240));
        QVERIFY(item->setPath(rectangle({20, 20, 120, 80})));
        item->setFill(Qt::red);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.rendererInterface()->graphicsApi() != QSGRendererInterface::Software);
        auto image = window.grabWindow();
        QVERIFY(!image.isNull());
        QCOMPARE(pixelAt(image, window, {60, 60}), QColor(Qt::red));
        QCOMPARE(pixelAt(image, window, {200, 60}), QColor(Qt::black));

        item->setFill(Qt::green);
        QVERIFY(item->setPath(rectangle({160, 20, 120, 80})));
        image = window.grabWindow();
        QCOMPARE(pixelAt(image, window, {60, 60}), QColor(Qt::black));
        QCOMPARE(pixelAt(image, window, {200, 60}), QColor(Qt::green));

        item->setOpacity(0.5);
        image = window.grabWindow();
        const auto blended = pixelAt(image, window, {200, 60});
        QVERIFY(blended.green() >= 126 && blended.green() <= 129);
        QCOMPARE(blended.red(), 0);

        QVERIFY(item->setPath({}));
        image = window.grabWindow();
        QCOMPARE(pixelAt(image, window, {200, 60}), QColor(Qt::black));
    }

    void rejectedMesh_clearsPreviousGeometry()
    {
        QQuickWindow window;
        prepareWindow(window);
        auto* item = new PathItem(window.contentItem()); item->setSize(QSizeF(360, 240));
        item->setFill(Qt::red);
        QVERIFY(item->setPath(rectangle({20, 20, 200, 100})));
        window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
        QCOMPARE(pixelAt(window.grabWindow(), window, {60, 60}), QColor(Qt::red));
        QPainterPath hole; hole.addRect({20, 20, 200, 100}); hole.addRect({40, 40, 10, 10});
        QVERIFY(!item->setPath(hole));
        QCOMPARE(item->geometryError(), PathGeometryError::NestedContours);
        QCOMPARE(pixelAt(window.grabWindow(), window, {60, 60}), QColor(Qt::black));
    }

    void sceneGraphRecreation_restoresCurrentPresentation()
    {
        QQuickWindow first, second;
        prepareWindow(first); prepareWindow(second);
        auto* item = new PathItem(first.contentItem()); item->setSize(QSizeF(360, 240));
        item->setFill(Qt::blue);
        QVERIFY(item->setPath(rectangle({20, 20, 120, 80})));
        first.show(); QVERIFY(QTest::qWaitForWindowExposed(&first));
        QCOMPARE(pixelAt(first.grabWindow(), first, {60, 60}), QColor(Qt::blue));
        first.hide();
        QTRY_VERIFY(!first.isExposed());
        first.releaseResources();
        first.show(); QVERIFY(QTest::qWaitForWindowExposed(&first));
        QTRY_VERIFY(first.isSceneGraphInitialized());
        QCOMPARE(pixelAt(first.grabWindow(), first, {60, 60}), QColor(Qt::blue));

        item->setParentItem(second.contentItem()); item->setParent(second.contentItem());
        second.show(); QVERIFY(QTest::qWaitForWindowExposed(&second));
        QCOMPARE(pixelAt(second.grabWindow(), second, {60, 60}), QColor(Qt::blue));
        QCOMPARE(pixelAt(first.grabWindow(), first, {60, 60}), QColor(Qt::black));
    }

    void frameMotion_drivesItemsAndStopsAtRest()
    {
        QQuickWindow window;
        prepareWindow(window);
        auto* item = new PathItem(window.contentItem()); item->setSize(QSizeF(360, 240));
        item->setFill(Qt::red);
        QVERIFY(item->setPath(rectangle({20, 20, 30, 30})));
        WindowMotion motion;
        motion.setWindow(&window);
        connect(&motion, &WindowMotion::valueChanged, item, [item](qreal progress) { item->setX(100 * progress); });
        QSignalSpy values(&motion, &WindowMotion::valueChanged);
        QSignalSpy frames(&window, &QQuickWindow::frameSwapped);
        QSignalSpy animationFrames(&window, &QQuickWindow::afterAnimating);
        window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
        motion.animateTo(1, Motion::Reveal, true);
        QTRY_VERIFY2_WITH_TIMEOUT(!motion.running(), qPrintable(QStringLiteral("value=%1 values=%2 swaps=%3 animate=%4 visible=%5 exposed=%6 initialized=%7")
            .arg(motion.value()).arg(values.count()).arg(frames.count()).arg(animationFrames.count())
            .arg(window.isVisible()).arg(window.isExposed()).arg(window.isSceneGraphInitialized())), 3000);
        QCOMPARE(motion.value(), qreal(1));
        QVERIFY(values.count() > 2);
        QCOMPARE(pixelAt(window.grabWindow(), window, {130, 30}), QColor(Qt::red));
        QCOMPARE(pixelAt(window.grabWindow(), window, {30, 30}), QColor(Qt::black));
        // OS expose messages may still request one frame; an idle animation
        // must not leave a self-sustaining render loop behind.
        QTest::qWait(100);
        const auto atRest = frames.count();
        QTest::qWait(100);
        QVERIFY(frames.count() - atRest <= 1);

        window.hide();
        motion.animateTo(0, Motion::Reveal, true);
        QTest::qWait(motionDuration(Motion::Reveal) + 20);
        window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_VERIFY_WITH_TIMEOUT(!motion.running(), 3000);
        QCOMPARE(motion.value(), qreal(0));
    }
};

QTEST_MAIN(QuickRenderingTest)
#include "quick_rendering_test.moc"
