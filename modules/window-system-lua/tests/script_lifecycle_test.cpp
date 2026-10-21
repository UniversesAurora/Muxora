// SPDX-License-Identifier: MIT
#include <window_system_lua/script_host.hpp>
#include <window_system/window_manager.hpp>
#include <QApplication>
#include <QEvent>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPointer>
#include <QSet>
#include <QSignalSpy>
#include <QTest>
#include <memory>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <psapi.h>
#endif

using window_system::WindowManager;
using window_system_lua::ScriptHost;
namespace {
void settle() {
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}
QHash<QString, int> ownedTypes(WindowManager &windows, ScriptHost *scripts = nullptr) {
    QSet<QObject *> visited;
    const auto visit = [&](auto &&recurse, QObject *object) -> void {
        if (!object || visited.contains(object))
            return;
        visited.insert(object);
        for (auto *child : object->children())
            recurse(recurse, child);
    };
    visit(visit, &windows);
    visit(visit, scripts);
    for (const auto id : windows.model().windowOrder())
        visit(visit, windows.windowWidget(id));
    for (auto *widget : QApplication::topLevelWidgets())
        if (widget->objectName() == "WindowSystemStorage")
            visit(visit, widget);
    QHash<QString, int> types;
    for (auto *object : visited)
        ++types[QString::fromLatin1(object->metaObject()->className())];
    return types;
}
bool load(ScriptHost &host, const QString &source) {
    QSignalSpy initialized(&host, &ScriptHost::initialized);
    host.initialize(source);
    if (initialized.isEmpty() && !initialized.wait(10000))
        return false;
    if (!initialized.first()[0].toBool())
        qWarning().noquote() << initialized.first()[1].toString();
    return initialized.first()[0].toBool();
}
void sample(ScriptHost &host, int generation) {
    QJsonObject values{{"generation", generation},
                       {"luaBytes", qint64(host.runtime()->memoryBytes())},
                       {"luaPeakBytes", qint64(host.runtime()->peakMemoryBytes())},
                       {"callbacks", host.runtime()->retainedCallbacks()},
                       {"pendingJobs", host.runtime()->pendingJobs()}};
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory),
                             sizeof(memory))) {
        values.insert("privateBytes", qint64(memory.PrivateUsage));
        values.insert("workingSetBytes", qint64(memory.WorkingSetSize));
    }
    DWORD handles = 0;
    if (GetProcessHandleCount(GetCurrentProcess(), &handles))
        values.insert("handles", qint64(handles));
    values.insert("gdiObjects", int(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS)));
    values.insert("userObjects", int(GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS)));
#endif
    qInfo().noquote() << "LUA_LIFECYCLE" << QJsonDocument(values).toJson(QJsonDocument::Compact);
}
} // namespace

class ScriptLifecycleTest final : public QObject {
    Q_OBJECT
  private slots:
    void reloadingLiveComponentsReclaimsVmAndPresentationResources();
};

void ScriptLifecycleTest::reloadingLiveComponentsReclaimsVmAndPresentationResources() {
    WindowManager windows;
    const auto root = windows.createWindow();
    auto *content = new QLabel("Retained content");
    const auto tab = windows.addTab(root, content, {"Retained", "fixture", {}});
    const auto floating = windows.createWindow(window_system::WindowKind::Floating, {}, root);
    windows.addTab(floating, new QLabel("Other content"), {"Other", "fixture", {}});
    settle();
    const auto originalTypes = ownedTypes(windows);
    const auto originalCommands = windows.commands().commands().size();
    auto scripts = std::make_unique<ScriptHost>(&windows);
    QSignalSpy errors(scripts.get(), &ScriptHost::scriptError);
    QHash<QString, int> activeTypes;
    const auto pane = windows.model().tab(tab)->activePane;
    const QString source = R"lua(
local retained=string.rep('x',128*1024)
muxora.commands.register({id='user.lifecycle',title='Lifecycle'},function()
  assert(#retained==128*1024)
end)
muxora.keymap.set({'ctrl+alt+f9 ctrl+alt+f10','ctrl+alt+f11'},'user.lifecycle')
assert(muxora.toolbars.register({id='user.lifecycle_tools',title='Lifecycle tools',commands={'user.lifecycle'}}))
assert(muxora.status.register({id='user.lifecycle_status',text='Ready'}))
muxora.timers.every(3600000,function()assert(#retained==128*1024)end)
local handled=0
muxora.events.on('fixture.output',function(_,event)
  handled=handled+1
  assert(event.data.index==handled and #retained==128*1024)
  assert(muxora.status.update('user.lifecycle_status',tostring(handled)))
end)
)lua";
    for (int generation = 1; generation <= 24; ++generation) {
        QVERIFY(load(*scripts, source));
        if (generation == 1) {
            QVERIFY(windows.setToolbarWidth(root, "user.lifecycle_tools", 137));
            QVERIFY(windows.setToolbarWidth(floating, "user.lifecycle_tools", 159));
        }
        QVERIFY(windows.execute("user.lifecycle", {}, root));
        for (int index = 1; index <= 32; ++index)
            scripts->publishEvent("fixture.output", QVariantMap{{"index", index}});
        QTRY_VERIFY(scripts->runtime()->pendingJobs() == 0 &&
                    scripts->resourceCounts()["deferred_events"].toInt() == 0);
        settle();
        QCOMPARE(errors.size(), 0);
        QCOMPARE(scripts->runtime()->retainedCallbacks(), 3);
        QCOMPARE(scripts->resourceCounts()["commands"].toInt(), 1);
        QCOMPARE(scripts->resourceCounts()["toolbars"].toInt(), 1);
        QCOMPARE(scripts->resourceCounts()["status_items"].toInt(), 1);
        QCOMPARE(scripts->resourceCounts()["timers"].toInt(), 1);
        QCOMPARE(scripts->resourceCounts()["subscriptions"].toInt(), 1);
        QCOMPARE(scripts->resourceCounts()["event_backlog_bytes"].toULongLong(), 0);
        QCOMPARE(windows.commands().commands().size(), originalCommands + 1);
        QCOMPARE(windows.toolbarState(root, "user.lifecycle_tools")->width, 137);
        QCOMPARE(windows.toolbarState(floating, "user.lifecycle_tools")->width, 159);
        QCOMPARE(windows.paneWidget(pane), content);
        QVERIFY(scripts->runtime()->memoryBytes() > 128 * 1024);
        QVERIFY(scripts->runtime()->peakMemoryBytes() >= scripts->runtime()->memoryBytes());
        if (generation == 4)
            activeTypes = ownedTypes(windows, scripts.get());
        else if (generation > 4)
            QCOMPARE(ownedTypes(windows, scripts.get()), activeTypes);
        if (generation == 4 || generation == 12 || generation == 24)
            sample(*scripts, generation);
    }
    QSignalSpy reset(scripts->runtime(), &lua_runtime::ScriptRuntime::resetFinished);
    scripts->unload();
    QTRY_COMPARE(reset.size(), 1);
    settle();
    QCOMPARE(scripts->runtime()->memoryBytes(), 0);
    QCOMPARE(scripts->runtime()->retainedCallbacks(), 0);
    QCOMPARE(scripts->runtime()->pendingJobs(), 0);
    for (const auto &count : scripts->resourceCounts())
        QCOMPARE(count.toLongLong(), 0);
    sample(*scripts, 0);
    scripts.reset();
    settle();
    QCOMPARE(ownedTypes(windows), originalTypes);
    QCOMPARE(windows.commands().commands().size(), originalCommands);
    QCOMPARE(windows.paneWidget(pane), content);
}
QTEST_MAIN(ScriptLifecycleTest)
#include "script_lifecycle_test.moc"
