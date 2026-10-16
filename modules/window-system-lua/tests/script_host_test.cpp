// SPDX-License-Identifier: MIT
#include <window_system_lua/script_host.hpp>
#include <window_system/window_manager.hpp>
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <memory>

using namespace window_system;
using lua_runtime::ScriptResult;
using window_system_lua::ScriptHost;
namespace {
ScriptResult run(ScriptHost &host, QString source, CommandContext context = {}) {
    QSignalSpy finished(host.runtime(), &lua_runtime::ScriptRuntime::finished);
    const auto job = host.run(std::move(source), "@test", context);
    if (!job)
        return {false, {}, "Host was not initialized"};
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < 10000) {
        for (const auto &item : finished)
            if (item[0].toULongLong() == job)
                return qvariant_cast<ScriptResult>(item[1]);
        finished.wait(50);
    }
    return {false, {}, "Job timed out"};
}
bool initialize(ScriptHost &host, QString source = {}) {
    QSignalSpy initialized(&host, &ScriptHost::initialized);
    host.initialize(std::move(source));
    if (initialized.isEmpty() && !initialized.wait(10000))
        return false;
    if (!initialized[0][0].toBool())
        qWarning().noquote() << initialized[0][1].toString();
    return initialized[0][0].toBool();
}
TabId addEditor(WindowManager &manager, WindowId window, QString name = "Editor") {
    return manager.addTab(window, new QPlainTextEdit, {std::move(name), "test.editor", {}});
}
} // namespace
class ScriptHostTest final : public QObject {
    Q_OBJECT
  private slots:
    void stateAndLiveQueriesUseValuesOnly();
    void luaCommandsSupportMultipleChordBindings();
    void explicitCommandContextDoesNotFollowLaterFocus();
    void layoutOperationsKeepContentIdentity();
    void customChromeIsReleasedWithItsScriptGeneration();
    void initializationFailureReleasesPartialRegistrations();
    void reloadReplacesFunctionsAndRestoresUnderlyingBindings();
    void eventCallbacksAreBoundedAndCanUnsubscribe();
    void eventCallbacksPreserveOrderAndReleaseQueuedValues();
    void overloadedRuntimeDoesNotStrandEventResources();
    void timersAreCancelledOnUnloadAndOneShotsReleaseCallbacks();
    void pureLuaModulesAreCachedAndCannotEscapeTheirDirectory();
    void invalidArgumentsDoNotPartiallyReplaceBindings();
    void scriptEffectsRespectCloseGuard();
    void registrationObserversCanDestroyTheScriptHost();
    void layoutSnapshotsPreserveEmptyArrays();
    void appearanceAndComponentBarsUseTypedDescriptions();
    void componentServicesFollowProviderLifetime();
    void rejectedRegistrationsReleaseFunctionReferences();
    void reloadingToolbarsPreservesPerWindowAndKindLayouts();
    void failingPeriodicCallbacksStopAndReleaseTheirReferences();
};
void ScriptHostTest::stateAndLiveQueriesUseValuesOnly() {
    WindowManager manager;
    auto window = manager.createWindow();
    const auto tab = addEditor(manager, window);
    ScriptHost host(&manager);
    QVERIFY(initialize(host, "counter=40"));
    auto reply = run(
        host,
        "counter=counter+2; return counter,muxora.context(),muxora.windows.list(),muxora.commands.list()");
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QCOMPARE(reply.values[0].toLongLong(), 42);
    QCOMPARE(reply.values[1].toMap().value("tab").toString(), tab.toString(QUuid::WithoutBraces));
    QCOMPARE(reply.values[2].toList().size(), 1);
    QVERIFY(reply.values[3].toList().size() > 100);
    reply = run(
        host, "return muxora.settings.update({multiline=true,show_tab_numbers=true}),muxora.settings.get()");
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QVERIFY(manager.tabAppearance().multiline);
    QVERIFY(manager.tabAppearance().showNumbers);
}
void ScriptHostTest::luaCommandsSupportMultipleChordBindings() {
    WindowManager manager;
    auto window = manager.createWindow();
    const auto tab = addEditor(manager, window);
    ScriptHost host(&manager);
    QVERIFY(initialize(host, R"(
      count=0
      muxora.commands.register({id='user.increment',title='Increment'},function(ctx)
        assert(ctx.tab~='');count=count+1
      end)
      muxora.keymap.set({'ctrl+alt+f9 ctrl+alt+f10','ctrl+alt+f11',
        'ctrl+alt+f12 ctrl+alt+f10 ctrl+alt+f9',
        'ctrl+alt+f8 ctrl+alt+f10 ctrl+alt+f9 ctrl+alt+f12'},'user.increment')
      standalone=0
      muxora.keymap.set('ctrl+alt+f10',function()standalone=standalone+1 end,{id='user.standalone'})
    )"));
    QCOMPARE(manager.commands().bindings("user.increment").size(), 4);
    auto *editor = qobject_cast<QPlainTextEdit *>(manager.paneWidget(manager.model().tab(tab)->activePane));
    QVERIFY(editor);
    QTest::keyClick(editor, Qt::Key_F9, Qt::ControlModifier | Qt::AltModifier);
    QTest::keyClick(editor, Qt::Key_F10, Qt::ControlModifier | Qt::AltModifier);
    QTest::keyClick(editor, Qt::Key_F11, Qt::ControlModifier | Qt::AltModifier);
    for (const auto key :
         {Qt::Key_F12, Qt::Key_F10, Qt::Key_F9, Qt::Key_F8, Qt::Key_F10, Qt::Key_F9, Qt::Key_F12})
        QTest::keyClick(editor, key, Qt::ControlModifier | Qt::AltModifier);
    QTest::keyClick(editor, Qt::Key_F10, Qt::ControlModifier | Qt::AltModifier);
    const auto reply = run(host, "return count,standalone");
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QCOMPARE(reply.values[0].toLongLong(), 4);
    QCOMPARE(reply.values[1].toLongLong(), 1);
    QVERIFY(editor->toPlainText().isEmpty());
}
void ScriptHostTest::explicitCommandContextDoesNotFollowLaterFocus() {
    WindowManager manager;
    auto window = manager.createWindow();
    const auto original = addEditor(manager, window, "Original");
    const auto captured = manager.context(window);
    const auto other = addEditor(manager, window, "Other");
    ScriptHost host(&manager);
    QVERIFY(initialize(host));
    const auto source =
        QString("return muxora.commands.execute('workspace.closeTab',{}, "
                "{window='%1',group='%2',tab='%3',pane='%4'})")
            .arg(window.toString(QUuid::WithoutBraces), captured.group.toString(QUuid::WithoutBraces),
                 original.toString(QUuid::WithoutBraces), captured.pane.toString(QUuid::WithoutBraces));
    const auto reply = run(host, source);
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QVERIFY(reply.values[0].toBool());
    QVERIFY(!manager.model().tab(original));
    QVERIFY(manager.model().tab(other));
}
void ScriptHostTest::layoutOperationsKeepContentIdentity() {
    WindowManager manager;
    auto window = manager.createWindow();
    const auto tab = addEditor(manager, window);
    const auto pane = manager.model().tab(tab)->activePane;
    QPointer<QWidget> content = manager.paneWidget(pane);
    ScriptHost host(&manager);
    QVERIFY(initialize(host));
    auto reply = run(host, R"(
      local c=muxora.context()
      assert(muxora.tabs.rename(c.tab,'From Lua'))
      assert(muxora.sidebar.move(c.tab,'left',false))
      assert(muxora.sidebar.set_size(c.tab,'left',{width=360,height=220}))
      assert(muxora.sidebar.pin(c.tab,true))
      local windows=muxora.windows.list()
      return muxora.tabs.detach(c.tab,'floating')
    )");
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QVERIFY(!reply.values[0].toString().isEmpty());
    QCOMPARE(manager.model().tab(tab)->title, QString("From Lua"));
    QCOMPARE(manager.paneWidget(pane), content.data());
    QCOMPARE(manager.peekSize(tab, Edge::Left), QSize(360, 220));
    QVERIFY(manager.model().validate());
}
void ScriptHostTest::customChromeIsReleasedWithItsScriptGeneration() {
    WindowManager manager;
    auto window = manager.createWindow();
    addEditor(manager, window);
    ScriptHost host(&manager);
    QVERIFY(initialize(host, R"(
      muxora.commands.register({id='user.action',title='My action'},function() end)
      assert(muxora.toolbars.register({id='user.tools',title='My tools',commands={'user.action'}}))
      assert(muxora.status.register({id='user.counter',text='42'}))
    )"));
    QCOMPARE(manager.toolbars().size(), 1);
    QVERIFY(manager.statusItemIds().contains("user.counter"));
    QVERIFY(manager.toolbarState(window, "user.tools"));
    host.unload();
    QVERIFY(manager.toolbars().isEmpty());
    QVERIFY(!manager.statusItemIds().contains("user.counter"));
    QVERIFY(!manager.commands().find("user.action"));
    const auto counts = host.resourceCounts();
    for (const auto &count : counts)
        QCOMPARE(count.toInt(), 0);
}
void ScriptHostTest::initializationFailureReleasesPartialRegistrations() {
    WindowManager manager;
    auto window = manager.createWindow();
    addEditor(manager, window);
    ScriptHost host(&manager);
    QVERIFY(!initialize(host, R"(
      muxora.commands.register({id='user.partial',title='Partial'},function() end)
      muxora.status.register({id='user.partial_status',text='Partial'})
      error('intentional initialization failure')
    )"));
    QVERIFY(!manager.commands().find("user.partial"));
    QVERIFY(!manager.statusItemIds().contains("user.partial_status"));
    QVERIFY(!host.ready());
    QTRY_COMPARE(host.runtime()->retainedCallbacks(), 0);
    QVERIFY(initialize(host));
    QVERIFY(run(host, "return 2+2").ok);
}
void ScriptHostTest::reloadReplacesFunctionsAndRestoresUnderlyingBindings() {
    WindowManager manager;
    auto window = manager.createWindow();
    addEditor(manager, window);
    const auto original = manager.commands().bindings("workspace.closeTab");
    ScriptHost host(&manager);
    for (int generation = 1; generation <= 20; ++generation) {
        const auto source = QString(R"(
          value=%1
          muxora.commands.register({id='user.version',title='Version'},function() value=value+100 end)
          muxora.keymap.set('ctrl+alt+f12','workspace.closeTab')
        )")
                                .arg(generation);
        QVERIFY(initialize(host, source));
        QVERIFY(manager.execute("user.version", {}, window));
        const auto reply = run(host, "return value");
        QVERIFY2(reply.ok, qPrintable(reply.error));
        QCOMPARE(reply.values[0].toInt(), 100 + generation);
        QCOMPARE(manager.commands().bindings("workspace.closeTab").size(), original.size() + 1);
    }
    host.unload();
    QCOMPARE(manager.commands().bindings("workspace.closeTab"), original);
}
void ScriptHostTest::eventCallbacksAreBoundedAndCanUnsubscribe() {
    WindowManager manager;
    auto window = manager.createWindow();
    addEditor(manager, window);
    ScriptHost host(&manager);
    QSignalSpy errors(&host, &ScriptHost::scriptError);
    QVERIFY(initialize(host, R"(
      events=0
      token=muxora.events.on('terminal.output',function(ctx,event)
        assert(event.data.text=='ready');events=events+1;muxora.events.off(token)
      end)
    )"));
    for (int i = 0; i < 500; ++i)
        host.publishEvent("terminal.output", QVariantMap{{"text", "ready"}});
    auto reply = run(host, "return events");
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QCOMPARE(reply.values[0].toInt(), 1);
    QCOMPARE(host.resourceCounts()["subscriptions"].toInt(), 0);
    QCOMPARE(host.runtime()->pendingJobs(), 0);
    QCOMPARE(errors.size(), 1);
    QVERIFY(errors.first()[0].toString().contains("backlog limit exceeded"));
    QCOMPARE(host.resourceCounts()["deferred_events"].toInt(), 0);
    QTRY_COMPARE(host.runtime()->retainedCallbacks(), 0);
}
void ScriptHostTest::eventCallbacksPreserveOrderAndReleaseQueuedValues() {
    WindowManager manager;
    const auto window = manager.createWindow();
    addEditor(manager, window);
    ScriptHost host(&manager);
    QSignalSpy errors(&host, &ScriptHost::scriptError);
    QVERIFY(initialize(host, "received={};token=muxora.events.on('terminal.output',function(ctx,event) "
                             "table.insert(received,event.data.index) end)"));
    for (int i = 1; i <= 24; ++i)
        host.publishEvent("terminal.output", QVariantMap{{"index", i}});
    QTRY_VERIFY(host.runtime()->pendingJobs() == 0 && host.resourceCounts()["deferred_events"].toInt() == 0);
    auto reply = run(host, "return received");
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QVariantList expected;
    for (int i = 1; i <= 24; ++i)
        expected.append(i);
    QCOMPARE(reply.values[0].toList(), expected);
    QCOMPARE(errors.size(), 0);
    QCOMPARE(host.resourceCounts()["event_backlog_bytes"].toULongLong(), 0);
    reply = run(host, "assert(muxora.events.off(token))");
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QTRY_COMPARE(host.runtime()->retainedCallbacks(), 0);
}
void ScriptHostTest::overloadedRuntimeDoesNotStrandEventResources() {
    WindowManager manager;
    const auto window = manager.createWindow();
    addEditor(manager, window);
    ScriptHost host(&manager);
    QVERIFY(initialize(host, "muxora.events.on('terminal.output',function()end)"));
    for (int i = 0; i < host.runtime()->limits().maximumPendingJobs; ++i)
        QVERIFY(host.runtime()->execute("return 1") != 0);
    for (int i = 0; i < 10000; ++i)
        host.runtime()->invoke({});
    QCOMPARE(host.runtime()->pendingJobs(), host.runtime()->limits().maximumPendingJobs);
    QCOMPARE(host.runtime()->pendingRejections(), host.runtime()->limits().maximumPendingJobs);
    QSignalSpy errors(&host, &ScriptHost::scriptError);
    host.publishEvent("terminal.output", "ready");
    QCOMPARE(host.resourceCounts()["subscriptions"].toInt(), 0);
    QCOMPARE(errors.size(), 1);
    QVERIFY(errors.first()[0].toString().contains("overloaded"));
    QTRY_COMPARE(host.runtime()->retainedCallbacks(), 0);
    QCOMPARE(host.resourceCounts()["deferred_events"].toInt(), 0);
    QTRY_COMPARE(host.runtime()->pendingRejections(), 0);
}
void ScriptHostTest::timersAreCancelledOnUnloadAndOneShotsReleaseCallbacks() {
    WindowManager manager;
    auto window = manager.createWindow();
    addEditor(manager, window);
    ScriptHost host(&manager);
    QVERIFY(initialize(host, R"(
      ticks=0
      token=muxora.timers.every(10,function() ticks=ticks+1 end)
    )"));
    QTRY_VERIFY(host.runtime()->pendingJobs() == 0);
    QTest::qWait(50);
    auto reply = run(host, "assert(muxora.timers.cancel(token));return ticks");
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QVERIFY(reply.values[0].toInt() > 0);
    QCOMPARE(host.resourceCounts()["timers"].toInt(), 0);
    host.unload();
    QCOMPARE(host.resourceCounts()["timers"].toInt(), 0);
    QVERIFY(initialize(host, R"(fired=false;muxora.timers.after(10,function()fired=true end))"));
    QTest::qWait(50);
    reply = run(host, "return fired");
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QVERIFY(reply.values[0].toBool());
    QCOMPARE(host.resourceCounts()["timers"].toInt(), 0);
}
void ScriptHostTest::pureLuaModulesAreCachedAndCannotEscapeTheirDirectory() {
    WindowManager manager;
    auto window = manager.createWindow();
    addEditor(manager, window);
    ScriptHost host(&manager);
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QFile file(root.filePath("sample.lua"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write("loaded=(loaded or 0)+1;return {value=42}") > 0);
    file.close();
    host.setModuleDirectory(root.path());
    QVERIFY(initialize(host));
    auto reply = run(host, "return require('sample').value,require('sample').value,loaded");
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QCOMPARE(reply.values, (QVariantList{qlonglong(42), qlonglong(42), qlonglong(1)}));
    reply = run(host, "return require('../sample')");
    QVERIFY(!reply.ok);
    QVERIFY(!reply.error.isEmpty());
}
void ScriptHostTest::invalidArgumentsDoNotPartiallyReplaceBindings() {
    WindowManager manager;
    auto window = manager.createWindow();
    addEditor(manager, window);
    ScriptHost host(&manager);
    QVERIFY(initialize(host));
    const auto original = manager.commands().bindings("workspace.closeTab");
    auto reply = run(host, "return muxora.keymap.set({'ctrl+w','Ctrl'},'window.close',{replace=true})");
    QVERIFY(!reply.ok);
    QCOMPARE(manager.commands().bindings("workspace.closeTab"), original);
    reply = run(host, "return muxora.windows.set_visibility(nil,{tabs='yes'})");
    QVERIFY(!reply.ok);
    QVERIFY(manager.tabBarsVisible(window));
}
void ScriptHostTest::scriptEffectsRespectCloseGuard() {
    WindowManager manager;
    auto window = manager.createWindow();
    const auto tab = addEditor(manager, window);
    manager.setCloseGuard([](const QVector<TabId> &) { return false; });
    ScriptHost host(&manager);
    QVERIFY(initialize(host));
    const auto reply = run(host, "return muxora.tabs.close(),muxora.windows.close()");
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QCOMPARE(reply.values, (QVariantList{false, false}));
    QVERIFY(manager.model().tab(tab));
}
void ScriptHostTest::registrationObserversCanDestroyTheScriptHost() {
    WindowManager manager;
    auto window = manager.createWindow();
    addEditor(manager, window);
    auto host = std::make_unique<ScriptHost>(&manager);
    QPointer<ScriptHost> observed(host.get());
    QVERIFY(initialize(*host));
    connect(&manager, &WindowManager::commandsChanged, &manager, [&] {
        if (manager.commands().find("user.destroy"))
            host.reset();
    });
    host->run("muxora.commands.register({id='user.destroy',title='Destroy'},function()end)");
    QTRY_VERIFY(!observed);
    QVERIFY(!manager.commands().find("user.destroy"));
    QVERIFY(manager.model().validate());
}
void ScriptHostTest::layoutSnapshotsPreserveEmptyArrays() {
    WindowManager manager;
    const auto window = manager.createWindow();
    const auto tab = addEditor(manager, window);
    manager.setContentFactory([](const TabOptions &, PaneId) { return new QPlainTextEdit; });
    ScriptHost host(&manager);
    QVERIFY(initialize(host));
    const auto before = manager.model().toJson();
    const auto reply = run(host, "local saved=muxora.layout.snapshot();return muxora.layout.restore(saved)");
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QVERIFY(reply.values[0].toBool());
    QCOMPARE(manager.model().toJson(), before);
    QVERIFY(manager.model().tab(tab));
}
void ScriptHostTest::appearanceAndComponentBarsUseTypedDescriptions() {
    WindowManager manager;
    const auto window = manager.createWindow();
    const auto tab = addEditor(manager, window);
    const auto pane = manager.model().tab(tab)->activePane;
    QPointer<QWidget> originalContent = manager.paneWidget(pane);
    ScriptHost host(&manager);
    QVERIFY(initialize(host));
    const auto reply = run(host, R"(
      local c=muxora.context()
      assert(muxora.tabs.set_bar(c.tab,'top',{text='Tab status'}))
      assert(muxora.panes.set_bar(c.pane,'bottom','Pane status'))
      assert(muxora.tabs.set_bar_sharing(c.tab,'bottom',false))
      local appearance=muxora.tabs.appearance()
      appearance.colors[1].group_active.background='#fefefe'
      assert(muxora.tabs.set_appearance(appearance))
      assert(muxora.theme.update({name='Lua theme',metrics={spacing=3},accent='#663399'}))
      assert(muxora.settings.update({new_tab_position='end',drag_outside_kind='workbench'}))
      return muxora.theme.get().name,muxora.tabs.appearance().colors[1].group_active.background
    )");
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QCOMPARE(reply.values[0].toString(), QString("Lua theme"));
    QCOMPARE(QColor(reply.values[1].toString()), QColor("#fefefe"));
    QCOMPARE(manager.theme().metrics.spacing, 3);
    QCOMPARE(manager.behavior().newTabPosition, NewTabPosition::End);
    QCOMPARE(manager.behavior().dragOutsideKind, WindowKind::Workbench);
    QCOMPARE(manager.paneWidget(pane), originalContent.data());
}
void ScriptHostTest::componentServicesFollowProviderLifetime() {
    WindowManager manager;
    const auto window = manager.createWindow();
    addEditor(manager, window);
    ScriptHost host(&manager);
    QVERIFY(initialize(host));
    auto provider = std::make_unique<QObject>();
    bool correctThread = false;
    QVERIFY(host.registerService(
        "fixture.component_echo",
        [&](QString method, QVariantList args, QVariantMap) {
            correctThread = QThread::currentThread() == qApp->thread();
            return ScriptResult{true, {QVariantMap{{"method", method}, {"value", args.value(0)}}}, {}};
        },
        provider.get()));
    auto reply = run(host, "return muxora.call('fixture.component_echo','component payload')");
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QVERIFY(correctThread);
    QCOMPARE(reply.values[0].toMap()["value"].toString(), QString("component payload"));
    provider.reset();
    reply = run(host, "return muxora.call('fixture.component_echo','removed')");
    QVERIFY(!reply.ok);
}
void ScriptHostTest::rejectedRegistrationsReleaseFunctionReferences() {
    WindowManager manager;
    const auto window = manager.createWindow();
    addEditor(manager, window);
    ScriptHost host(&manager);
    QVERIFY(initialize(host));
    auto reply = run(host, R"(
      for i=1,50 do
        local cycle={};cycle.self=cycle
        local ok=pcall(function()
          muxora.commands.register({id='user.bad',title='Bad',extra=cycle},function()end)
        end)
        assert(not ok)
        ok=pcall(function()muxora.timers.after(-1,function()end)end)
        assert(not ok)
      end
      return true
    )");
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QTRY_COMPARE(host.runtime()->retainedCallbacks(), 0);
    QCOMPARE(host.resourceCounts()["commands"].toInt(), 0);
    QCOMPARE(host.resourceCounts()["timers"].toInt(), 0);
}
void ScriptHostTest::reloadingToolbarsPreservesPerWindowAndKindLayouts() {
    WindowManager manager;
    const auto main = manager.createWindow();
    addEditor(manager, main);
    const auto floating = manager.createWindow(WindowKind::Floating, {}, main);
    addEditor(manager, floating);
    ScriptHost host(&manager);
    const auto source = [](bool extra) {
        return QString(R"(
      muxora.commands.register({id='user.first',title='First'},function()end)
      muxora.commands.register({id='user.second',title='Second'},function()end)
      assert(muxora.toolbars.register({id='user.tools',title='User tools',commands=%1}))
      assert(muxora.status.register({id='user.status',text='Value'}))
    )")
            .arg(extra ? "{'user.first','user.second'}" : "{'user.first'}");
    };
    QVERIFY(initialize(host, source(false)));
    QVERIFY(manager.moveToolbar(main, "user.tools", ToolbarArea::Bottom, 0));
    QVERIFY(manager.setToolbarWidth(main, "user.tools", 100));
    QVERIFY(manager.setToolbarVisible(main, "user.tools", false));
    QVERIFY(manager.setStatusItemVisible(main, "user.status", false));
    QVERIFY(manager.setToolbarWidth(floating, "user.tools", 144));
    QVERIFY(manager.promoteWindow(floating));
    QVERIFY(manager.setToolbarWidth(floating, "user.tools", 180));
    QVERIFY(manager.demoteWindow(floating, main));
    QCOMPARE(manager.toolbarState(floating, "user.tools")->width, 144);
    QVERIFY(initialize(host, source(true)));
    auto state = manager.toolbarState(main, "user.tools");
    QVERIFY(state);
    QCOMPARE(state->area, ToolbarArea::Bottom);
    QCOMPARE(state->width, 100);
    QVERIFY(!state->visible);
    QVERIFY(!manager.statusItemVisible(main, "user.status"));
    QCOMPARE(manager.toolbarState(floating, "user.tools")->width, 144);
    QVERIFY(manager.promoteWindow(floating));
    QCOMPARE(manager.toolbarState(floating, "user.tools")->width, 180);
    QVERIFY(initialize(host));
    QVERIFY(manager.toolbars().isEmpty());
    QVERIFY(!manager.statusItemIds().contains("user.status"));
    QVERIFY(!QJsonDocument(manager.saveLayout()).toJson().contains("user.tools"));
}
void ScriptHostTest::failingPeriodicCallbacksStopAndReleaseTheirReferences() {
    WindowManager manager;
    const auto window = manager.createWindow();
    addEditor(manager, window);
    ScriptHost host(&manager);
    QVERIFY(initialize(host));
    QSignalSpy errors(&host, &ScriptHost::scriptError);
    auto reply = run(host, "muxora.timers.every(10,function()error('timer failure')end)");
    QVERIFY(reply.ok);
    QTRY_COMPARE(errors.size(), 1);
    QCOMPARE(host.resourceCounts()["timers"].toInt(), 0);
    QTRY_COMPARE(host.runtime()->retainedCallbacks(), 0);
    reply = run(host, "muxora.events.on('user.failure',function()error('event failure')end)");
    QVERIFY(reply.ok);
    host.publishEvent("user.failure");
    QTRY_COMPARE(errors.size(), 2);
    QCOMPARE(host.resourceCounts()["subscriptions"].toInt(), 0);
    QTRY_COMPARE(host.runtime()->retainedCallbacks(), 0);
}
QTEST_MAIN(ScriptHostTest)
#include "script_host_test.moc"
