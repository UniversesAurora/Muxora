// SPDX-License-Identifier: MIT
// Descriptive input cost. No visible windows, native input or CTest threshold.
#include "docking/tab_bar.hpp"
#include <QApplication>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QImage>
#include <QMouseEvent>
#include <QSaveFile>
#include <QWheelEvent>
#include <algorithm>
#include <cstdio>

using namespace window_system;

namespace {
class PaintCounter final : public QObject {
public:
    int count = 0;
protected:
    bool eventFilter(QObject*, QEvent* event) override
    {
        if (event->type() == QEvent::Paint) ++count;
        return false;
    }
};

QJsonObject measure(auto send, int count = 2000, int warmup = 200)
{
    QList<double> samples; samples.reserve(count);
    for (int index = 0; index < warmup + count; ++index) {
        QElapsedTimer timer; timer.start();
        send(index);
        const double us = timer.nsecsElapsed() / 1000.0;
        if (index >= warmup) samples.append(us);
    }
    std::sort(samples.begin(), samples.end());
    return {{"p50us", samples[qCeil(count * 0.5) - 1]}, {"p95us", samples[qCeil(count * 0.95) - 1]}};
}
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"commit", "Source revision label", "revision"});
    parser.addOption({"out", "Output JSON file", "file"});
    parser.process(app);
    if (parser.value("commit").isEmpty()) parser.showHelp(2);
    TabBar::setAnimationsEnabled(false);
    QJsonArray cases;
    for (const auto placement : {TabBarPlacement::Top, TabBarPlacement::Bottom, TabBarPlacement::Left, TabBarPlacement::Right}) {
        TabBar bar(defaultTheme(), nullptr);
        bar.resize(placement == TabBarPlacement::Left || placement == TabBarPlacement::Right ? QSize(220, 600) : QSize(1000, 32));
        bar.setAvailableSize(bar.size());
        TabAppearance policy; policy.showNewTabButton = false;
        bar.setAppearance(policy, placement);
        for (int index = 0; index < 12; ++index) bar.addTab({}, QStringLiteral("Workspace %1").arg(index));
        const auto point = bar.tabRect(0).center();
        const auto send = [&](QEvent::Type type, QPoint local, Qt::MouseButton button, Qt::MouseButtons buttons) {
            QMouseEvent event(type, local, bar.mapToGlobal(local), button, buttons, Qt::NoModifier);
            QApplication::sendEvent(&bar, &event);
        };
        for (const bool held : {false, true}) {
            if (held) send(QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
            auto result = measure([&](int index) {
                send(QEvent::MouseMove, point + QPoint(index % 3, 0), Qt::NoButton, held ? Qt::LeftButton : Qt::NoButton);
            });
            if (held) send(QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
            result.insert("placement", int(placement)); result.insert("held", held);
            cases.append(result);
        }
        auto keys = measure([&](int index) {
            QKeyEvent event(QEvent::KeyPress, index % 2 ? Qt::Key_Home : Qt::Key_End, Qt::NoModifier);
            QApplication::sendEvent(&bar, &event);
        });
        keys.insert("placement", int(placement)); keys.insert("keyboard", true);
        cases.append(keys);
        if (bar.vertical()) { bar.resize(bar.width(), 240); bar.setAvailableSize(bar.size()); }
        const auto wheelPoint = bar.scrollViewport().center();
        auto wheel = measure([&](int index) {
            QWheelEvent event(wheelPoint, bar.mapToGlobal(wheelPoint), QPoint(0, index % 2 ? 8 : -8), {},
                              Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(&bar, &event);
        });
        wheel.insert("placement", int(placement)); wheel.insert("pixelWheel", true);
        cases.append(wheel);
        if (bar.vertical()) {
            bar.setIconMode(true);
            auto column = measure([&](int index) { bar.setColumnHovered(index % 2 == 0); });
            bar.setColumnHovered(false); bar.setIconMode(false);
            column.insert("placement", int(placement)); column.insert("columnHover", true);
            cases.append(column);
        }
        const auto dragged = QUuid::createUuid(); bar.setTabData(0, dragged); bar.ensureVisible(0);
        const auto label = bar.tabRect(0); bar.setDraggedTab(dragged);
        if (label.isEmpty()) return 3;
        bar.setLifted(label.center(), label.center() - label.topLeft());
        auto* layer = bar.findChild<QWidget*>("LiftedTabLayer");
        if (bar.liftedRect().isEmpty() || !layer || layer->isHidden()) return 3;
        PaintCounter paints; layer->installEventFilter(&paints);
        const qreal ratio = bar.devicePixelRatioF();
        QImage canvas(QSize(qCeil(bar.width() * ratio), qCeil(bar.height() * ratio)), QImage::Format_RGB32);
        canvas.setDevicePixelRatio(ratio);
        auto lifted = measure([&](int) { canvas.fill(Qt::white); bar.render(&canvas); }, 180, 30);
        if (paints.count < 210) return 3;
        layer->removeEventFilter(&paints);
        lifted.insert("placement", int(placement)); lifted.insert("liftedPaint", true);
        lifted.insert("samples", 180); lifted.insert("warmup", 30); lifted.insert("dpr", ratio);
        lifted.insert("layerPaints", paints.count); lifted.insert("logicalSize", QJsonArray{bar.width(), bar.height()});
        cases.append(lifted);
        bar.setLifted({}); bar.setDraggedTab({});
        policy.showNumbers = true;
        bar.setAppearance(policy, placement);
        for (int index = bar.count(); index < 100; ++index) bar.addTab({}, QStringLiteral("Workspace %1").arg(index));
        auto layout = measure([&](int index) {
            const auto size = QSize((bar.vertical() ? 220 : 1000) + index % 2, bar.height());
            bar.resize(size); bar.setAvailableSize(size);
        });
        layout.insert("placement", int(placement)); layout.insert("numberedLayout", true);
        layout.insert("labels", bar.count()); cases.append(layout);
        if (bar.isVisible()) return 3;
    }
    const auto bytes = QJsonDocument(QJsonObject{{"commit", parser.value("commit")},
        {"warmup", 200}, {"samples", 2000}, {"cases", cases}}).toJson();
    if (parser.value("out").isEmpty()) std::fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout);
    else {
        QSaveFile file(parser.value("out"));
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) return 2;
    }
}
