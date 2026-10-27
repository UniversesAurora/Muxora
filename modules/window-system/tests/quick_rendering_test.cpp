// SPDX-License-Identifier: MIT
// Quick presentation wiring: retained geometry/color updates, scene graph
// resource recreation, frame-paced motion, visibility and window ownership.
// This is not a visual baseline or a complete application performance test.
#include "quick/path_item.hpp"
#include "quick/window_motion.hpp"
#include "docking/surface_geometry.hpp"

#include <QGuiApplication>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSignalSpy>
#include <QTest>

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
