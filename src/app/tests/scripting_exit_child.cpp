// SPDX-License-Identifier: MIT
#include "scripting_setup.hpp"
#include "user_preferences.hpp"
#include <window_system/window_manager.hpp>
#include <window_system_lua/script_host.hpp>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    const auto arguments = app.arguments();
    if (arguments.size() != 2)
        return 2;
    const auto directory = arguments[1];
    const QDir state(directory);
    window_system::WindowManager manager;
    manager.createWindow();
    muxora::UserPreferences preferences(directory);
    QElapsedTimer shutdown;
    {
        muxora::AppScriptingSetup scripting(&manager, preferences);
        QObject::connect(&scripting, &muxora::AppScriptingSetup::initialized, &app,
                         [&](bool ok, const QString &) {
            if (!ok) {
                app.exit(3);
                return;
            }
            const auto quote = [](QString text) {
                return QStringLiteral("[=[%1]=]").arg(text);
            };
            const auto helper = QDir(QCoreApplication::applicationDirPath())
                                    .filePath(QStringLiteral("muxora-script-child.exe"));
            const auto command = QStringLiteral("\"\"%1\" --wait-for-release \"%2\" \"%3\"\"")
                                     .arg(QDir::toNativeSeparators(helper),
                                          QDir::toNativeSeparators(state.filePath("blocked-ready")),
                                          QDir::toNativeSeparators(state.filePath("release")));
            if (!scripting.host()->run(QStringLiteral("os.execute(%1)").arg(quote(command))))
                app.exit(4);
        });
        QTimer ready;
        ready.setInterval(10);
        QObject::connect(&ready, &QTimer::timeout, &app, [&] {
            if (QFile::exists(state.filePath("blocked-ready"))) {
                ready.stop();
                shutdown.start();
                QFile started(state.filePath("shutdown-started"));
                const auto timestamp = QByteArray::number(shutdown.msecsSinceReference());
                if (!started.open(QIODevice::WriteOnly) || started.write(timestamp) != timestamp.size()) {
                    app.exit(7);
                    return;
                }
                started.close();
                app.quit();
            }
        });
        ready.start();
        QTimer::singleShot(10000, &app, [&] { app.exit(5); });
        scripting.start();
        const auto result = app.exec();
        if (result)
            return result;
    }
    QFile report(state.filePath("shutdown.json"));
    if (!report.open(QIODevice::WriteOnly))
        return 6;
    report.write(QJsonDocument(QJsonObject{{"elapsed_ms", shutdown.elapsed()}}).toJson());
    return 0;
}
