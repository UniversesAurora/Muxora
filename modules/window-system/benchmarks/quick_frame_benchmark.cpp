// SPDX-License-Identifier: MIT
// A scene-graph primitive benchmark, not a replacement Muxora application.
// Measures submission cadence and GUI mesh preparation, not display photons.
#include "quick/path_item.hpp"
#include "quick/window_motion.hpp"
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
    parser.process(app);
    bool valid = false;
    const int count = parser.value("frames").toInt(&valid);
    if (!valid || count < 30 || count > 10000 || parser.value("commit").isEmpty()) return 2;

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
    const auto prepare = [&](qreal value) {
        QElapsedTimer cost; cost.start();
        const qreal split = 400 + 600 * value;
        SurfaceGeometryInput input;
        input.body = QRectF(4, 40, split - 8, 856);
        input.anchor = QRectF(28, 12, 140, 28);
        const bool first = left->setPath(surfaceGeometry(input).mergedPath);
        input.body = QRectF(split + 4, 40, 1596 - split, 856);
        input.anchor = QRectF(split + 28, 12, 140, 28);
        const bool second = right->setPath(surfaceGeometry(input).mergedPath);
        if (!first || !second) { failed = true; app.exit(3); return; }
        if (preparation.size() < count + 60) preparation.append(cost.nsecsElapsed() / 1.e6);
    };
    prepare(0);
    QObject::connect(&motion, &WindowMotion::valueChanged, &window, prepare);
    QObject::connect(&window, &QQuickWindow::frameSwapped, &window, [&] {
        bool complete;
        { QMutexLocker lock(&samples.mutex); complete = samples.swaps.size() >= count; }
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
    const auto json = QJsonDocument(QJsonObject{
        {"commit", parser.value("commit")}, {"qt", qVersion()},
        {"complete", complete},
        {"scope", "two joined surface fills only; no text, shadows, host content or input"},
        {"timing", "frameSwapped submission intervals; not compositor presentation or GPU execution time"},
        {"graphicsApi", api.load()}, {"threadedSceneGraph", threaded.load()},
        {"refreshHz", refresh}, {"dpr", window.effectiveDevicePixelRatio()},
        {"logicalSize", QJsonArray{window.width(), window.height()}},
        {"submissionInterval", summary(samples.swaps)},
        {"renderToFrameEndCpuWall", summary(samples.render)},
        {"meshPreparation", summary(preparation)}, {"overTwoRefreshCycles", qint64(overTwo)}
    }).toJson();
    lock.unlock();
    if (parser.value("out").isEmpty()) std::fwrite(json.constData(), 1, json.size(), stdout);
    else {
        QSaveFile file(parser.value("out"));
        if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size() || !file.commit()) return 2;
    }
    return complete ? 0 : 3;
}
