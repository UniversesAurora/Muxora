// SPDX-License-Identifier: MIT
// A complete Quick workspace switching tabs: the manager, QuickWorkspace,
// menus, toolbars, status bar and tab surfaces, with plain host content.
// Measures submission cadence and the GUI thread's shadow rasterization;
// not display photons, GPU execution time or input latency.
#include "style/elevation.hpp"
#include <window_system/quick_workspace.hpp>
#include <window_system/window_manager.hpp>

#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSaveFile>
#include <QScreen>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace window_system;

namespace {
QJsonObject summary(QList<double> values)
{
    if (values.isEmpty()) return {{"count", 0}};
    std::sort(values.begin(), values.end());
    const auto rank = [&](double p) { return values.at(std::max(0, int(std::ceil(p * values.size())) - 1)); };
    double total = 0;
    for (const auto value : values) total += value;
    return {{"count", values.size()}, {"p50Ms", rank(.5)}, {"p95Ms", rank(.95)}, {"maxMs", values.last()}, {"totalMs", total}};
}
} // namespace

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"commit", "Measured source revision", "revision"});
    parser.addOption({"out", "Output JSON path", "file"});
    parser.addOption({"frames", "Measured submission intervals after 30 warm-up frames", "count", "240"});
    parser.addOption({"scene", "far: 30 tabs, first and last alternate (the strip scrolls); near: 6 visible tabs, first and third alternate", "name", "far"});
    parser.addOption({"placement", "Central tab bar placement: top or left", "name", "top"});
    parser.process(app);
    bool valid = false;
    const int count = parser.value("frames").toInt(&valid);
    const auto scene = parser.value("scene"), placement = parser.value("placement");
    if (!valid || count < 30 || count > 10000 || parser.value("commit").isEmpty()
        || (scene != "far" && scene != "near") || (placement != "top" && placement != "left")) return 2;

    WindowManager manager(WindowManager::Presentation::External);
    auto appearance = manager.tabAppearance();
    appearance.centralPlacement = placement == "top" ? TabBarPlacement::Top : TabBarPlacement::Left;
    manager.setTabAppearance(appearance);
    QuickWorkspace workspace(manager);
    workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
    const auto windowId = manager.createWindow(WindowKind::Workbench, {QStringLiteral("Benchmark"), QSize(1600, 900)});
    QVector<TabId> tabs;
    const int tabCount = scene == "far" ? 30 : 6;
    for (int index = 0; index < tabCount; ++index)
        tabs.append(manager.addTab(windowId, {QStringLiteral("Document %1").arg(index + 1), QStringLiteral("benchmark"), {}}));
    const TabId first = tabs.first(), second = scene == "far" ? tabs.last() : tabs.at(2);
    manager.activateTab(first);
    // The workspace builds and shows its windows from queued notifications.
    QElapsedTimer started; started.start();
    QQuickWindow* window = nullptr;
    while (!(window = workspace.window(windowId)) || !window->isExposed()) {
        if (started.elapsed() > 10000) return 3;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }

    QElapsedTimer clock; clock.start();
    QMutex mutex;
    QList<double> swaps;
    qint64 previousSwap = 0;
    int seen = 0;
    ElevationRenderer shadowCache; // Shares the GUI thread's elevation cache.
    ElevationCacheStats warm;
    bool warmed = false;
    QObject::connect(window, &QQuickWindow::frameSwapped, window, [&] {
        QMutexLocker lock(&mutex);
        const auto now = clock.nsecsElapsed();
        if (seen >= 30 && swaps.size() < count) swaps.append((now - previousSwap) / 1.e6);
        previousSwap = now;
        ++seen;
    }, Qt::DirectConnection);
    bool failed = false;
    int frame = 0;
    QObject::connect(window, &QQuickWindow::frameSwapped, window, [&] {
        bool complete;
        { QMutexLocker lock(&mutex); complete = swaps.size() >= count; if (seen >= 30 && !warmed) { warm = shadowCache.cacheStats(); warmed = true; } }
        // quit() asks the windows to close first; the workspace hands closing to
        // the manager and refuses, which would cancel quitting.
        if (complete) { app.exit(0); return; }
        if (++frame % 30 == 0) manager.activateTab(frame / 30 % 2 ? second : first);
        window->update();
    }, Qt::QueuedConnection);
    QTimer::singleShot(60000, &app, [&] { failed = true; app.exit(3); });
    window->requestActivate();
    // The first frames went out while waiting; start the continuous cadence.
    window->update();
    const int result = app.exec();
    QMutexLocker lock(&mutex);
    const bool complete = !failed && !result && swaps.size() == count;
    const qreal refresh = window->screen()->refreshRate();
    const double cycle = refresh > 0 ? 1000. / refresh : 0;
    qsizetype overTwo = 0;
    for (const auto interval : swaps) if (cycle > 0 && interval > 2 * cycle) ++overTwo;
    const QJsonObject report{
        {"commit", parser.value("commit")}, {"qt", qVersion()}, {"complete", complete}, {"exitCode", result}, {"timedOut", failed},
        {"scene", scene}, {"placement", placement}, {"tabs", tabCount},
        {"scope", "WindowManager and QuickWorkspace with menus, toolbars, status bar and plain QQuickItem content; tabs activated every 30 submissions while every frame is requested"},
        {"timing", "frameSwapped submission intervals and shadow image generation wall time on the GUI thread; not compositor presentation, GPU execution or input latency"},
        {"refreshHz", refresh}, {"dpr", window->effectiveDevicePixelRatio()},
        {"logicalSize", QJsonArray{window->width(), window->height()}},
        {"submissionInterval", summary(swaps)}, {"overTwoRefreshCycles", qint64(overTwo)},
        {"warmGeneratedShadowImages", qint64(shadowCache.cacheStats().generatedImages - warm.generatedImages)},
        {"warmShadowGenerationMs", (shadowCache.cacheStats().generationNs - warm.generationNs) / 1.e6}};
    const auto json = QJsonDocument(report).toJson();
    lock.unlock();
    if (parser.value("out").isEmpty()) std::fwrite(json.constData(), 1, json.size(), stdout);
    else {
        QSaveFile file(parser.value("out"));
        if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size() || !file.commit()) return 2;
    }
    return complete ? 0 : 3;
}
