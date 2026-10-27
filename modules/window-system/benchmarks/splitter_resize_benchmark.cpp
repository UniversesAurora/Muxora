// SPDX-License-Identifier: MIT
// Offscreen layout and complete paint cost; never exposes a desktop window.
#include <window_system/window_manager.hpp>
#include "docking/tab_bar.hpp"
#include "style/elevation.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QSaveFile>
#include <QSplitter>
#include <QTextEdit>
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace window_system;
namespace {
QJsonObject summary(QList<double> values)
{
    std::sort(values.begin(), values.end());
    const auto rank = [&](double p) { return values.at(std::max(0, int(std::ceil(p * values.size())) - 1)); };
    return {{"count", values.size()}, {"p50Ms", rank(.5)}, {"p95Ms", rank(.95)}, {"maxMs", values.last()}};
}
QWidget* content(bool text)
{
    if (!text) { auto* widget = new QWidget; widget->setAutoFillBackground(true); return widget; }
    auto* editor = new QTextEdit;
    editor->setHtml(QStringLiteral("<h3>Workspace notes</h3><p>Text stays in the same pane while its group is resized.</p>"
        "<p>窗口内容与标签一起调整。<b>Resize</b> and inspect the layout cost.</p>").repeated(5));
    return editor;
}
QJsonObject measure(bool text, TabBarPlacement placement, int iterations)
{
    WindowManager manager;
    auto policy = manager.tabAppearance(); policy.centralPlacement = placement; manager.setTabAppearance(policy);
    const auto window = manager.createWindow(WindowKind::Workbench, {"Resize benchmark", QSize(1600, 900)});
    const auto first = manager.addTab(window, content(text), {"First workspace", "benchmark", {}});
    manager.addTabToNewGroup(window, content(text), {"Second workspace", "benchmark", {}},
        manager.model().groupForTab(first), Placement::Right);
    auto* root = manager.windowWidget(window);
    auto* splitter = root->findChild<QSplitter*>("GroupSplitter");
    if (!splitter) qFatal("Missing benchmark splitter");
    root->ensurePolished();
    const qreal ratio = root->devicePixelRatioF();
    QImage canvas(QSize(qRound(root->width() * ratio), qRound(root->height() * ratio)), QImage::Format_RGB32);
    canvas.setDevicePixelRatio(ratio);
    QList<double> layout, paint, total;
    ElevationRenderer shadows;
    quint64 warmImages = 0;
    for (int frame = -30; frame < iterations; ++frame) {
        if (frame == 0) warmImages = shadows.cacheStats().generatedImages;
        const int step = (frame + 30) % 120;
        const int left = 400 + 10 * (step < 60 ? step : 120 - step);
        QElapsedTimer timer; timer.start();
        splitter->setSizes({left, 1600 - left});
        QCoreApplication::processEvents();
        const double layoutMs = timer.nsecsElapsed() / 1.e6;
        root->render(&canvas);
        const double totalMs = timer.nsecsElapsed() / 1.e6;
        if (root->isVisible()) qFatal("Benchmark must remain hidden");
        if (frame >= 0) { layout.append(layoutMs); paint.append(totalMs - layoutMs); total.append(totalMs); }
    }
    return {{"content", text ? "rich-text" : "opaque"}, {"placement", placement == TabBarPlacement::Top ? "top" : "left"},
        {"dpr", ratio}, {"logicalSize", QJsonArray{root->width(), root->height()}},
        {"layout", summary(layout)}, {"paintIncludingDeferredResize", summary(paint)}, {"total", summary(total)},
        {"warmShadowImages", qint64(shadows.cacheStats().generatedImages - warmImages)}};
}
}
int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QCommandLineParser parser; parser.addHelpOption();
    parser.addOption({"commit", "Measured source revision", "revision"});
    parser.addOption({"out", "Output JSON path", "file"});
    parser.addOption({"iterations", "Measured frames per case", "count", "180"});
    parser.process(app);
    bool ok = false; const int iterations = parser.value("iterations").toInt(&ok);
    if (!ok || iterations < 1 || parser.value("commit").isEmpty()) return 2;
    QJsonArray cases;
    for (const bool text : {false, true})
        for (const auto placement : {TabBarPlacement::Top, TabBarPlacement::Left})
            cases.append(measure(text, placement, iterations));
    const auto json = QJsonDocument(QJsonObject{{"commit", parser.value("commit")}, {"qt", qVersion()},
        {"scope", "hidden widgets; full render, not desktop frame timing"}, {"cases", cases}}).toJson();
    if (parser.value("out").isEmpty()) std::fwrite(json.constData(), 1, json.size(), stdout);
    else { QSaveFile file(parser.value("out")); if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size() || !file.commit()) return 2; }
    return 0;
}
