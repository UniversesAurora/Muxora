// SPDX-License-Identifier: MIT
#include <window_system_lua/script_host.hpp>
#include <window_system/window_manager.hpp>
#include <QApplication>
#include <QLabel>
#include <QJsonArray>
#include <QTimer>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    window_system::WindowManager windows;
    const auto window = windows.createWindow();
    auto *content = new QLabel("External content");
    const auto tab = windows.addTab(window, content, {"External", "contract", {}});
    if (tab.isNull())
        return 1;
    window_system_lua::ScriptHost host(&windows);
    QObject provider;
    if (!host.registerService(
            "contract.echo",
            [](QString, QVariantList args, QVariantMap) {
                return lua_runtime::ScriptResult{true, {args.value(0)}, {}};
            },
            &provider))
        return 2;
    quint64 checkJob = 0;
    QObject::connect(&host, &window_system_lua::ScriptHost::initialized, &app, [&](bool ok, QString error) {
        if (!ok) {
            qWarning().noquote() << error;
            app.exit(3);
            return;
        }
        checkJob = host.run("assert(muxora.windows.set_title(nil,'External Lua'));"
                            "assert(muxora.tabs.rename(nil,'Retained content'));return muxora.context().tab");
        if (!checkJob)
            app.exit(4);
    });
    QObject::connect(host.runtime(), &lua_runtime::ScriptRuntime::finished, &app,
                     [&](quint64 job, lua_runtime::ScriptResult result) {
                         if (!checkJob || checkJob != job)
                             return;
                         const bool valid =
                             result.ok &&
                             result.values.value(0).toString() == tab.toString(QUuid::WithoutBraces) &&
            windows.windowWidget(window)->windowTitle() == QStringLiteral("Retained content — External Lua") &&
            windows.saveLayout().value("windows").toArray().first().toObject().value("title").toString() == "External Lua" &&
                             windows.model().tab(tab)->title == "Retained content" &&
                             windows.paneWidget(windows.model().tab(tab)->activePane) == content &&
                             windows.toolbars().size() == 1 &&
                             windows.statusItemIds().contains("user.status");
                         if (!valid)
                             qWarning().noquote() << result.error;
                         app.exit(valid ? 0 : 5);
                     });
    QTimer::singleShot(10000, &app, [&] { app.exit(6); });
    host.initialize(
        "assert(muxora.call('contract.echo','Ready')=='Ready');"
        "muxora.commands.register({id='user.action',title='Action'},function()end);"
        "assert(muxora.toolbars.register({id='user.tools',title='Tools',commands={'user.action'}}));"
        "assert(muxora.status.register({id='user.status',text='Ready'}))");
    return app.exec();
}
