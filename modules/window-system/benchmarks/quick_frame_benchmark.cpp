// SPDX-License-Identifier: MIT
// A scene-graph primitive benchmark, not a replacement Muxora application.
// Measures submission cadence and GUI mesh preparation, not display photons.
#include "quick/path_item.hpp"
#include "quick/window_motion.hpp"
#include "quick/body_shadow_item.hpp"
#include "quick/anchor_shadow_item.hpp"
#include "quick/group_surface_item.hpp"
#include "docking/tab_group_surface.hpp"
#include "docking/surface_geometry.hpp"

#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QScreen>
#include <QSGRendererInterface>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>

using namespace window_system;
using namespace window_system::quick;

namespace {
QJsonObject summary(QList<double> values)
{
    if (values.isEmpty()) return {{"count", 0}};
    std::sort(values.begin(), values.end());
    const auto rank = [&](double p) { return values.at(std::max(0, int(std::ceil(p * values.size())) - 1)); };
    return {{"count", values.size()}, {"p50Ms", rank(.5)}, {"p95Ms", rank(.95)}, {"maxMs", values.last()}};
}

struct Samples {
    QMutex mutex;
    QList<double> swaps;
    QList<double> render;
    qint64 previousSwap = 0;
    qint64 renderStart = 0;
    int seen = 0;
};
} // namespace

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"commit", "Measured source revision", "revision"});
    parser.addOption({"out", "Output JSON path", "file"});
    parser.addOption({"frames", "Measured submission intervals after 30 warm-up frames", "count", "240"});
    parser.addOption({"scene", "Primitive scene: joined-fills, body-resize, joined-resize, group-resize or group-peek", "name", "joined-fills"});
    parser.process(app);
    bool valid = false;
    const int count = parser.value("frames").toInt(&valid);
    if (!valid || count < 30 || count > 10000 || parser.value("commit").isEmpty()) return 2;
    const auto scene = parser.value("scene");
    if (scene != "joined-fills" && scene != "body-resize" && scene != "joined-resize" && scene != "group-resize" && scene != "group-peek") return 2;
    const bool bodies = scene == "body-resize";
    const bool shadows = scene != "joined-fills";
    const bool anchors = scene == "joined-resize";
    const bool peeking = scene == "group-peek";
    const bool groups = scene == "group-resize" || peeking;

    // Render-thread callbacks outlive the GUI event loop, so their captured
    // state must also outlive destruction of the QQuickWindow and its graph.
    QElapsedTimer clock; clock.start();
    Samples samples;
    samples.swaps.reserve(count); samples.render.reserve(count);
    std::atomic<bool> threaded = false;
    std::atomic<bool> rhiBackend = false;
    std::atomic<int> api = 0;
    auto* guiThread = app.thread();
    QQuickWindow window;
    window.setTitle(QStringLiteral("WindowSystem Quick frame benchmark"));
    window.resize(1600, 900);
    window.setColor(QColor("#ded7eb"));
    auto* leftShadow = shadows && !groups ? new BodyShadowItem(window.contentItem()) : nullptr;
    auto* rightShadow = shadows && !groups ? new BodyShadowItem(window.contentItem()) : nullptr;
    auto* leftAnchor = anchors ? new AnchorShadowItem(window.contentItem()) : nullptr;
    auto* rightAnchor = anchors ? new AnchorShadowItem(window.contentItem()) : nullptr;
    auto* leftGroup = groups ? new GroupSurfaceItem(window.contentItem()) : nullptr;
    auto* rightGroup = groups ? new GroupSurfaceItem(window.contentItem()) : nullptr;
    if (groups) { leftGroup->setSize(QSizeF(1600, 900)); rightGroup->setSize(QSizeF(1600, 900)); }
    auto* left = new PathItem(window.contentItem());
    auto* right = new PathItem(window.contentItem());
    left->setSize(QSizeF(1600, 900)); right->setSize(QSizeF(1600, 900));
    WindowMotion motion;
    motion.setWindow(&window);
    QList<double> preparation; preparation.reserve(count + 60);
    QObject::connect(&window, &QQuickWindow::sceneGraphInitialized, &window, [&] {
        threaded = QThread::currentThread() != guiThread;
        const auto graphics = window.rendererInterface()->graphicsApi();
        rhiBackend = graphics != QSGRendererInterface::Software && graphics != QSGRendererInterface::Unknown;
        api = int(graphics);
    }, Qt::DirectConnection);
    QObject::connect(&window, &QQuickWindow::beforeRendering, &window, [&] {
        QMutexLocker lock(&samples.mutex); samples.renderStart = clock.nsecsElapsed();
    }, Qt::DirectConnection);
    QObject::connect(&window, &QQuickWindow::afterFrameEnd, &window, [&] {
        QMutexLocker lock(&samples.mutex);
        if (samples.seen >= 30 && samples.render.size() < count)
            samples.render.append((clock.nsecsElapsed() - samples.renderStart) / 1.e6);
    }, Qt::DirectConnection);
    QObject::connect(&window, &QQuickWindow::frameSwapped, &window, [&] {
        QMutexLocker lock(&samples.mutex);
        const auto now = clock.nsecsElapsed();
        if (samples.seen >= 30 && samples.swaps.size() < count)
            samples.swaps.append((now - samples.previousSwap) / 1.e6);
        samples.previousSwap = now;
        ++samples.seen;
    }, Qt::DirectConnection);
    bool failed = false;
    ElevationRenderer shadowResources;
    quint64 warmImages = 0;
    ShadowRenderStats warmLeft, warmRight, warmLeftAnchor, warmRightAnchor;
    bool warmed = false;
    const auto leftResources = [&] { return groups ? leftGroup->renderStats() : leftShadow->renderStats(); };
    const auto rightResources = [&] { return groups ? rightGroup->renderStats() : rightShadow->renderStats(); };
    const auto prepareGroup = [&](GroupSurfaceItem* item, const SurfaceGeometry& geometry, QRect group, qreal value) {
        if (!item) return true;
        TabGroupSurfaceFrame frame;
        frame.groupBounds = group; frame.body = geometry.body; frame.radius = geometry.bodyRadius;
        frame.elevation = elevationProfile(ElevationLevel::BodyRest);
        frame.anchors.append({geometry, elevationProfile(ElevationLevel::AnchorRaised)});
        if (peeking) {
            frame.anchors.first().presence = labelPresencePose(geometry.anchor, .15 + .7 * value);
            TabColumnSurface column;
            column.bounds = group.adjusted(-96, 0, 96, 0); column.body = {qreal(group.left() - 60), 8, 130, 240};
            column.radius = 8; column.opacity = value; column.fill = window.color();
            column.line = QColor("#8a6fbd"); column.line.setAlphaF(value * .5);
            frame.column = column;
        }
        prepareTabGroupSurfaceFrame(frame);
        const bool accepted = item->setFrame(frame);
        if (!accepted) qWarning("Group frame preparation rejected input (%d)", int(item->geometryError()));
        return accepted;
    };
    const auto prepare = [&](qreal value) {
        QElapsedTimer cost; cost.start();
        const qreal split = 400 + 600 * value;
        SurfaceGeometryInput input;
        input.body = QRectF(4, 40, split - 8, 856);
        if (!bodies) input.anchor = QRectF(28, 12, 140, 28);
        const auto firstGeometry = surfaceGeometry(input);
        if (leftShadow) {
            leftShadow->setPosition(input.body.topLeft()); leftShadow->setSize(input.body.size());
            leftShadow->setShadow(input.bodyRadius, elevationProfile(ElevationLevel::BodyRest), {});
        }
        if (anchors) {
            leftAnchor->setShadow(firstGeometry, elevationProfile(ElevationLevel::BodyRest),
                elevationProfile(ElevationLevel::AnchorRaised), {});
            leftShadow->setExcluded(leftAnchor->patchClip().translated(-input.body.topLeft()));
        }
        const bool first = left->setPath(bodies ? firstGeometry.bodyPath : firstGeometry.mergedPath);
        const bool firstGroup = prepareGroup(leftGroup, firstGeometry, QRect(0, 0, qRound(split), 900), value);
        input.body = QRectF(split + 4, 40, 1596 - split, 856);
        if (!bodies) input.anchor = QRectF(split + 28, 12, 140, 28);
        const auto secondGeometry = surfaceGeometry(input);
        if (rightShadow) {
            rightShadow->setPosition(input.body.topLeft()); rightShadow->setSize(input.body.size());
            rightShadow->setShadow(input.bodyRadius, elevationProfile(ElevationLevel::BodyRest), {});
        }
        if (anchors) {
            rightAnchor->setShadow(secondGeometry, elevationProfile(ElevationLevel::BodyRest),
                elevationProfile(ElevationLevel::AnchorRaised), {});
            rightShadow->setExcluded(rightAnchor->patchClip().translated(-input.body.topLeft()));
        }
        const bool second = right->setPath(bodies ? secondGeometry.bodyPath : secondGeometry.mergedPath);
        const bool secondGroup = prepareGroup(rightGroup, secondGeometry, QRect(qRound(split), 0, 1600 - qRound(split), 900), value);
        if (!first || !second || !firstGroup || !secondGroup) { failed = true; app.exit(3); return; }
        if (preparation.size() < count + 60) preparation.append(cost.nsecsElapsed() / 1.e6);
    };
    prepare(0);
    QObject::connect(&motion, &WindowMotion::valueChanged, &window, prepare);
    QObject::connect(&window, &QQuickWindow::frameSwapped, &window, [&] {
        bool complete, warm;
        { QMutexLocker lock(&samples.mutex); complete = samples.swaps.size() >= count; warm = samples.seen >= 30; }
        if (shadows && warm && !warmed) {
            warmLeft = leftResources(); warmRight = rightResources();
            if (anchors) { warmLeftAnchor = leftAnchor->renderStats(); warmRightAnchor = rightAnchor->renderStats(); }
            warmImages = shadowResources.cacheStats().generatedImages; warmed = true;
        }
        // quit() can synchronously close windows and wait for the render
        // thread. Never call it while holding a lock used by that thread.
        if (complete) { app.quit(); return; }
        if (!motion.running()) motion.animateTo(motion.value() < .5 ? 1 : 0, Motion::Reveal, true);
    }, Qt::QueuedConnection);
    QObject::connect(&window, &QQuickWindow::sceneGraphError, &app, [&](auto, const QString&) {
        failed = true; app.exit(3);
    });
    QTimer::singleShot(30000, &app, [&] { failed = true; app.exit(3); });
    if (failed) return 3;
    window.show();
    motion.animateTo(1, Motion::Reveal, true);
    const int result = app.exec();
    window.hide();
    motion.setWindow(nullptr);
    QMutexLocker lock(&samples.mutex);
    const bool complete = !failed && !result && rhiBackend && samples.swaps.size() == count;
    const qreal refresh = window.screen()->refreshRate();
    const double cycle = refresh > 0 ? 1000. / refresh : 0;
    qsizetype overTwo = 0;
    for (const auto interval : samples.swaps) if (cycle > 0 && interval > 2 * cycle) ++overTwo;
    QJsonObject report{
        {"commit", parser.value("commit")}, {"qt", qVersion()},
        {"complete", complete},
        {"scene", scene},
        {"scope", peeking ? "two group backplate frames with presence and column opacity; no text, host content or input"
                         : groups ? "two group backplate frames with retained shadows and clipping; no text, host content or input"
                         : anchors ? "two joined surface fills and retained composed shadows; no text, host content or input"
                         : bodies ? "two body fills and retained perimeter shadows; no joined anchors, text, host content or input"
                         : "two joined surface fills only; no text, shadows, host content or input"},
        {"timing", "frameSwapped submission intervals; not compositor presentation or GPU execution time"},
        {"graphicsApi", api.load()}, {"threadedSceneGraph", threaded.load()},
        {"refreshHz", refresh}, {"dpr", window.effectiveDevicePixelRatio()},
        {"logicalSize", QJsonArray{window.width(), window.height()}},
        {"submissionInterval", summary(samples.swaps)},
        {"renderToFrameEndCpuWall", summary(samples.render)},
        {"meshPreparation", summary(preparation)}, {"overTwoRefreshCycles", qint64(overTwo)}
    };
    if (shadows) {
        const auto a = leftResources(), b = rightResources();
        report.insert("shadowResources", QJsonObject{
            {"textureUploads", qint64(a.textureUploads + b.textureUploads)},
            {"imageBytesUploaded", qint64(a.imageBytesUploaded + b.imageBytesUploaded)},
            {"geometryUpdates", qint64(a.geometryUpdates + b.geometryUpdates)},
            {"warmTextureUploads", qint64(a.textureUploads + b.textureUploads - warmLeft.textureUploads - warmRight.textureUploads)},
            {"warmGeneratedImages", qint64(shadowResources.cacheStats().generatedImages - warmImages)}});
    }
    if (anchors) {
        const auto a = leftAnchor->renderStats(), b = rightAnchor->renderStats();
        report.insert("anchorShadowResources", QJsonObject{
            {"textureUploads", qint64(a.textureUploads + b.textureUploads)},
            {"imageBytesUploaded", qint64(a.imageBytesUploaded + b.imageBytesUploaded)},
            {"geometryUpdates", qint64(a.geometryUpdates + b.geometryUpdates)},
            {"warmTextureUploads", qint64(a.textureUploads + b.textureUploads - warmLeftAnchor.textureUploads - warmRightAnchor.textureUploads)}});
    }
    const auto json = QJsonDocument(report).toJson();
    lock.unlock();
    if (parser.value("out").isEmpty()) std::fwrite(json.constData(), 1, json.size(), stdout);
    else {
        QSaveFile file(parser.value("out"));
        if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size() || !file.commit()) return 2;
    }
    return complete ? 0 : 3;
}
