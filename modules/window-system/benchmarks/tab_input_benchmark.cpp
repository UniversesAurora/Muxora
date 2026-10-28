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
#include <QMouseEvent>
#include <QSaveFile>
#include <algorithm>
#include <cstdio>

using namespace window_system;

namespace {
QJsonObject measure(auto send)
{
    QList<double> samples; samples.reserve(2000);
    for (int index = 0; index < 2200; ++index) {
        QElapsedTimer timer; timer.start();
        send(index);
        const double us = timer.nsecsElapsed() / 1000.0;
        if (index >= 200) samples.append(us);
    }
    std::sort(samples.begin(), samples.end());
    return {{"p50us", samples[999]}, {"p95us", samples[1899]}};
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
