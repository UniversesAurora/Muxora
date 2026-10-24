// SPDX-License-Identifier: MIT
#include <window_system_lua/script_host.hpp>
#include <window_system/window_manager.hpp>
#include <QApplication>
#include <QDir>
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
    void sameCommandPrefixBindingsAreAtomicAndRestoreOnUnload_data();
    void sameCommandPrefixBindingsAreAtomicAndRestoreOnUnload();
    void internalShortcutPrefixAliasesAreRejected_data();
    void internalShortcutPrefixAliasesAreRejected();
    void exactShortcutAliasesUpdateArgumentsWithoutDuplicates();
    void sharedShortcutContinuationsRemainReachable();
    void explicitCommandContextDoesNotFollowLaterFocus();
    void layoutOperationsKeepContentIdentity();
    void stickyTabsAreScriptable();
    void hierarchyConversionsKeepTheSamePaneObjects();
    void customChromeIsReleasedWithItsScriptGeneration();
    void initializationFailureReleasesPartialRegistrations();
    void reloadReplacesFunctionsAndRestoresUnderlyingBindings();
    void eventCallbacksAreBoundedAndCanUnsubscribe();
    void eventCallbacksPreserveOrderAndReleaseQueuedValues();
    void overloadedRuntimeDoesNotStrandEventResources();
    void timersAreCancelledOnUnloadAndOneShotsReleaseCallbacks();
    void standardModulesCanCallTheHostAtTopLevel_data();
    void standardModulesCanCallTheHostAtTopLevel();
    void standardModuleLoadingPreservesLuaSemantics_data();
    void standardModuleLoadingPreservesLuaSemantics();
    void nestedModuleLoadingCanYieldThroughUserCoroutines();
    void cancellingModuleLoadingDoesNotCacheOrContinueTheModule();
    void nonyieldableModuleLoadingHasNoHostSideEffects_data();
    void nonyieldableModuleLoadingHasNoHostSideEffects();
    void standardModulesUseUserPathsAndPackageLoaded();
    void standardModulesAllowUserSearchersAndBinaryChunks();
    void standardModulesReportMissingFilesAndUnavailableCLibraries();
    void nestedCoroutinesCanCallTheWindowHost();
    void cancellingANestedHostCallRejectsStaleReplies();
    void osExitUsesTheHostAndDoesNotTerminateAnExternalHost();
    void invalidArgumentsDoNotPartiallyReplaceBindings();
    void scriptEffectsRespectCloseGuard();
    void registrationObserversCanDestroyTheScriptHost();
    void layoutSnapshotsPreserveEmptyArrays();
    void appearanceAndComponentBarsUseTypedDescriptions();
    void titleClippingSettingsAreTypedAtomicAndPersistent();
    void stickyTabSettingsAreTypedAndAtomic();
    void componentServicesFollowProviderLifetime();
    void rejectedRegistrationsReleaseFunctionReferences();
    void reloadingToolbarsPreservesPerWindowAndKindLayouts();
    void failingPeriodicCallbacksStopAndReleaseTheirReferences();
    void windowDisplayStateUsesSharedCommands_data();
    void windowDisplayStateUsesSharedCommands();
    void panelPoliciesCanBeConfiguredWithoutNestedExecution();
};
void ScriptHostTest::hierarchyConversionsKeepTheSamePaneObjects() {
    WindowManager manager; const auto window = manager.createWindow();
    const auto first = addEditor(manager, window, "First");
    const auto firstPane = manager.model().tab(first)->activePane; const auto firstWidget = manager.paneWidget(firstPane);
    const auto second = addEditor(manager, window, "Second");
    const auto group = manager.model().groupForTab(second);
    ScriptHost host(&manager); QVERIFY(initialize(host));
    auto reply = run(host, QString("return muxora.groups.to_tab('%1')").arg(group.toString(QUuid::WithoutBraces)));
    QVERIFY2(reply.ok, qPrintable(reply.error)); QCOMPARE(reply.values[0].toString(), second.toString(QUuid::WithoutBraces));
    QCOMPARE(manager.paneWidget(firstPane), firstWidget); QCOMPARE(manager.model().panes(second).size(), 2);
    reply = run(host, QString("return muxora.tabs.to_group('%1','top')").arg(second.toString(QUuid::WithoutBraces)));
    QVERIFY2(reply.ok, qPrintable(reply.error)); QCOMPARE(manager.model().group(QUuid(reply.values[0].toString()))->tabs.size(), 2);
    const auto newPane = manager.splitPane(second, manager.model().tab(second)->activePane, new QLabel("Extra"), Placement::Right);
    const auto extraWidget = manager.paneWidget(newPane);
    reply = run(host, QString("return muxora.panes.to_tab('%1')").arg(newPane.toString(QUuid::WithoutBraces)));
    QVERIFY2(reply.ok, qPrintable(reply.error)); const QUuid extracted(reply.values[0].toString()); QVERIFY(!extracted.isNull());
    reply = run(host, QString("return muxora.tabs.into_tab('%1','%2',nil,'bottom')")
        .arg(extracted.toString(QUuid::WithoutBraces), second.toString(QUuid::WithoutBraces)));
    QVERIFY2(reply.ok, qPrintable(reply.error)); QVERIFY(reply.values[0].toBool()); QCOMPARE(manager.paneWidget(newPane), extraWidget);
    const auto otherWindow = manager.createWindow(); const auto targetTab = addEditor(manager, otherWindow, "Target");
    reply = run(host, QString("return muxora.groups.into_tab('%1','%2',nil,'left')")
        .arg(manager.model().groupForTab(second).toString(QUuid::WithoutBraces), targetTab.toString(QUuid::WithoutBraces)));
    QVERIFY2(reply.ok, qPrintable(reply.error)); QVERIFY(reply.values[0].toBool());
    QCOMPARE(manager.paneWidget(firstPane), firstWidget); QCOMPARE(manager.paneWidget(newPane), extraWidget);
    QVERIFY(manager.model().validate());
}
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
void ScriptHostTest::sameCommandPrefixBindingsAreAtomicAndRestoreOnUnload_data() {
    QTest::addColumn<bool>("oldIsShort");
    QTest::addColumn<bool>("replace");
    QTest::newRow("reject-existing-short") << true << false;
    QTest::newRow("reject-existing-long") << false << false;
    QTest::newRow("replace-existing-short") << true << true;
    QTest::newRow("replace-existing-long") << false << true;
}
void ScriptHostTest::sameCommandPrefixBindingsAreAtomicAndRestoreOnUnload() {
    QFETCH(bool, oldIsShort);
    QFETCH(bool, replace);
    WindowManager manager;
    const auto window = manager.createWindow();
    const auto tab = addEditor(manager, window);
    const QString shortKey = "Ctrl+Alt+F9";
    const QString longKey = "Ctrl+Alt+F9, X";
    const QString requested = oldIsShort ? longKey : shortKey;
    const QVector<CommandBinding> original{
        {QKeySequence(oldIsShort ? shortKey : longKey), {{"value", "original"}}},
        {QKeySequence("Ctrl+Alt+F8"), {{"value", "retained"}}}};
    QVariantMap invokedArguments;
    Command command;
    command.id = "fixture.prefix";
    command.title = "Prefix fixture";
    command.bindings = original;
    command.execute = [&](const CommandContext &context) {
        invokedArguments = context.args;
        return true;
    };
    QVERIFY(manager.commands().add(std::move(command)));
    ScriptHost host(&manager);
    QVERIFY(initialize(host));
    const auto reply = run(host, QString(R"lua(
        return muxora.keymap.set({'ctrl+alt+f10','%1'},'fixture.prefix',
                                {replace=%2,args={value='replacement'}})
    )lua")
                                     .arg(requested, replace ? "true" : "false"));
    if (replace) {
        QVERIFY2(reply.ok, qPrintable(reply.error));
        QCOMPARE(reply.values, QVariantList{true});
        const QVector<CommandBinding> expected{original[1],
                                               {QKeySequence("Ctrl+Alt+F10"), {{"value", "replacement"}}},
                                               {QKeySequence(requested), {{"value", "replacement"}}}};
        QCOMPARE(manager.commands().bindings("fixture.prefix"), expected);
        auto *editor = manager.paneWidget(manager.model().tab(tab)->activePane);
        const QKeySequence sequence(requested);
        for (int index = 0; index < sequence.count(); ++index)
            QTest::keyClick(editor, sequence[index].key(), sequence[index].keyboardModifiers());
        QCOMPARE(invokedArguments, QVariantMap({{"value", "replacement"}}));
        QCOMPARE(host.resourceCounts()["bindings"].toInt(), 1);
    } else {
        QVERIFY(!reply.ok);
        QCOMPARE(manager.commands().bindings("fixture.prefix"), original);
        QCOMPARE(host.resourceCounts()["bindings"].toInt(), 0);
    }
    host.unload();
    QCOMPARE(manager.commands().bindings("fixture.prefix"), original);
    QCOMPARE(host.resourceCounts()["bindings"].toInt(), 0);
}
void ScriptHostTest::internalShortcutPrefixAliasesAreRejected_data() {
    QTest::addColumn<bool>("shortFirst");
    QTest::addColumn<bool>("replace");
    QTest::newRow("short-first-default") << true << false;
    QTest::newRow("long-first-default") << false << false;
    QTest::newRow("short-first-replace") << true << true;
    QTest::newRow("long-first-replace") << false << true;
}
void ScriptHostTest::internalShortcutPrefixAliasesAreRejected() {
    QFETCH(bool, shortFirst);
    QFETCH(bool, replace);
    WindowManager manager;
    const auto window = manager.createWindow();
    addEditor(manager, window);
    Command target;
    target.id = "fixture.target";
    target.title = "Target";
    target.bindings = {{QKeySequence("Ctrl+Alt+F8"), {}}};
    target.execute = [](const CommandContext &) { return true; };
    const auto targetBindings = target.bindings;
    QVERIFY(manager.commands().add(std::move(target)));
    Command other;
    other.id = "fixture.other";
    other.title = "Other";
    other.bindings = {{QKeySequence("Ctrl+Alt+F10"), {}}};
    other.execute = [](const CommandContext &) { return true; };
    const auto otherBindings = other.bindings;
    QVERIFY(manager.commands().add(std::move(other)));
    ScriptHost host(&manager);
    QVERIFY(initialize(host));
    // replace=true would also remove the other command's F10 binding if the
    // descriptor were applied before validating all newly supplied aliases.
    const auto aliases = shortFirst ? "'ctrl+alt+f9','ctrl+alt+f9 x'" : "'ctrl+alt+f9 x','ctrl+alt+f9'";
    const auto reply = run(host, QString(R"lua(
        return muxora.keymap.set({%1,'ctrl+alt+f10'},'fixture.target',{replace=%2})
    )lua")
                                     .arg(aliases, replace ? "true" : "false"));
    QVERIFY(!reply.ok);
    QVERIFY(reply.error.contains("prefix", Qt::CaseInsensitive));
    QCOMPARE(manager.commands().bindings("fixture.target"), targetBindings);
    QCOMPARE(manager.commands().bindings("fixture.other"), otherBindings);
    QCOMPARE(host.resourceCounts()["bindings"].toInt(), 0);
}
void ScriptHostTest::exactShortcutAliasesUpdateArgumentsWithoutDuplicates() {
    WindowManager manager;
    const auto window = manager.createWindow();
    const auto tab = addEditor(manager, window);
    Command command;
    command.id = "fixture.arguments";
    command.title = "Arguments";
    command.bindings = {{QKeySequence("Ctrl+Alt+F8"), {}}};
    const auto original = command.bindings;
    QVariantMap invokedArguments;
    command.execute = [&](const CommandContext &context) {
        invokedArguments = context.args;
        return true;
    };
    QVERIFY(manager.commands().add(std::move(command)));
    ScriptHost host(&manager);
    QVERIFY(initialize(host));
    const auto reply = run(host, R"lua(
        assert(muxora.keymap.set({'ctrl+alt+f9','ctrl+alt+f9'},'fixture.arguments',{args={value='first'}}))
        return muxora.keymap.set('ctrl+alt+f9','fixture.arguments',{args={value='updated'}})
    )lua");
    QVERIFY2(reply.ok, qPrintable(reply.error));
    const QVector<CommandBinding> expected{original[0],
                                           {QKeySequence("Ctrl+Alt+F9"), {{"value", "updated"}}}};
    QCOMPARE(manager.commands().bindings("fixture.arguments"), expected);
    auto *editor = manager.paneWidget(manager.model().tab(tab)->activePane);
    QTest::keyClick(editor, Qt::Key_F9, Qt::ControlModifier | Qt::AltModifier);
    QCOMPARE(invokedArguments, QVariantMap({{"value", "updated"}}));
    host.unload();
    QCOMPARE(manager.commands().bindings("fixture.arguments"), original);
}
void ScriptHostTest::sharedShortcutContinuationsRemainReachable() {
    WindowManager manager;
    const auto window = manager.createWindow();
    const auto tab = addEditor(manager, window);
    ScriptHost host(&manager);
    QVERIFY(initialize(host, R"lua(
        seen={0,0,0,0}
        local function bind(key,index)
            muxora.keymap.set(key,function()seen[index]=seen[index]+1 end,
                              {id='user.shared_'..index})
        end
        bind('ctrl+alt+f9 x',1)
        bind('ctrl+alt+f8 x',2)
        bind('ctrl+alt+f9 y',3)
        bind('x',4)
    )lua"));
    auto *editor = qobject_cast<QPlainTextEdit *>(manager.paneWidget(manager.model().tab(tab)->activePane));
    QVERIFY(editor);
    for (const auto first : {Qt::Key_F9, Qt::Key_F8}) {
        QTest::keyClick(editor, first, Qt::ControlModifier | Qt::AltModifier);
        QTest::keyClick(editor, Qt::Key_X);
    }
    QTest::keyClick(editor, Qt::Key_F9, Qt::ControlModifier | Qt::AltModifier);
    QTest::keyClick(editor, Qt::Key_Y);
    QTest::keyClick(editor, Qt::Key_X);
    const auto reply = run(host, "return seen");
    QVERIFY2(reply.ok, qPrintable(reply.error));
    // One returned value, the table {1, 1, 1, 1}. Spelled out: QVariantList({list})
    // picks the copy constructor on MSVC and the initializer list on GCC.
    QCOMPARE(reply.values, QVariantList{QVariant(QVariantList{1, 1, 1, 1})});
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
void ScriptHostTest::stickyTabsAreScriptable() {
    WindowManager manager;
    auto window = manager.createWindow();
    addEditor(manager, window, "First");
    const auto second = addEditor(manager, window, "Second");
    ScriptHost host(&manager);
    QVERIFY(initialize(host));
    auto reply = run(host, R"(
      local c=muxora.context()
      assert(muxora.tabs.sticky())
      local group=muxora.groups.list()[1]
      assert(group.sticky_tabs==1 and group.tabs[1]==c.tab)
      assert(muxora.tabs.sticky(nil,false))
      return muxora.groups.list()[1].sticky_tabs
    )");
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QCOMPARE(reply.values[0].toInt(), 0);
    QVERIFY(!manager.isTabSticky(second));
    QVERIFY(manager.moveTabToEdge(second, Edge::Left));
    reply = run(host, "return muxora.tabs.sticky(muxora.context().tab)");
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QVERIFY(!reply.values[0].toBool());
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
void ScriptHostTest::standardModulesCanCallTheHostAtTopLevel_data() {
    QTest::addColumn<bool>("duringInitialization");
    QTest::newRow("initialization") << true;
    QTest::newRow("ordinary-task") << false;
}
void ScriptHostTest::standardModulesCanCallTheHostAtTopLevel() {
    QFETCH(bool, duringInitialization);
    WindowManager manager;
    const auto window = manager.createWindow();
    const auto tab = addEditor(manager, window, "Before loading");
    ScriptHost host(&manager);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile module(directory.filePath("host_module.lua"));
    QVERIFY(module.open(QIODevice::WriteOnly));
    const QByteArray source = R"lua(
      moduleLoads=(moduleLoads or 0)+1
      assert(muxora.tabs.rename(nil,'Loaded from module'))
      return {tab=muxora.context().tab}
    )lua";
    QCOMPARE(module.write(source), source.size());
    module.close();
    host.setModuleDirectory(directory.path());
    const QString load = QStringLiteral(R"lua(
      local value=require('host_module')
      assert(value.tab==muxora.context().tab)
      assert(value==require('host_module') and moduleLoads==1)
    )lua");
    if (duringInitialization) {
        QVERIFY(initialize(host, load));
    } else {
        QVERIFY(initialize(host));
        const auto reply = run(host, load);
        QVERIFY2(reply.ok, qPrintable(reply.error));
    }
    QCOMPARE(manager.model().tab(tab)->title, QStringLiteral("Loaded from module"));
}
void ScriptHostTest::standardModuleLoadingPreservesLuaSemantics_data() {
    QTest::addColumn<QString>("source");
    QTest::newRow("preload-arguments-and-cache-results") << QStringLiteral(R"lua(
      local loads=0
      package.preload.fixture=function(name,data)
        assert(name=='fixture' and data==':preload:')
        loads=loads+1
        return {value=42},'discarded'
      end
      local first=table.pack(require('fixture'))
      assert(first.n==2 and first[1].value==42 and first[2]==':preload:')
      local cached=table.pack(require('fixture'))
      assert(cached.n==1 and cached[1]==first[1] and loads==1)
    )lua");
    QTest::newRow("custom-searcher-and-loader-data") << QStringLiteral(R"lua(
      local data={source='custom'}
      table.insert(package.searchers,1,function(name)
        assert(name=='fixture')
        assert(muxora.context().window~='')
        return function(module,extra)
          assert(module==name and extra==data)
          return {value=73}
        end,data
      end)
      local value,extra=require('fixture')
      assert(value.value==73 and extra==data and package.loaded.fixture==value)
    )lua");
    QTest::newRow("nil-loader-result-and-nil-loader-data") << QStringLiteral(R"lua(
      local loads=0
      package.searchers={function()
        return function() loads=loads+1 end
      end}
      local first=table.pack(require('fixture'))
      assert(first.n==2 and first[1]==true and first[2]==nil)
      local cached=table.pack(require('fixture'))
      assert(cached.n==1 and cached[1]==true and loads==1)
    )lua");
    QTest::newRow("explicit-loaded-value-with-nil-result") << QStringLiteral(R"lua(
      local value={value=91}
      package.preload.fixture=function(name) package.loaded[name]=value end
      assert(require('fixture')==value and require('fixture')==value)
      package.loaded.fixture=nil
      package.preload.fixture=function(name) package.loaded[name]=false end
      assert(require('fixture')==false and package.loaded.fixture==false)
    )lua");
    QTest::newRow("returned-false-overrides-cache-and-reloads") << QStringLiteral(R"lua(
      local loads=0
      package.preload.fixture=function(name)
        loads=loads+1
        package.loaded[name]={discarded=true}
        return false,'ignored'
      end
      assert(require('fixture')==false and require('fixture')==false)
      assert(loads==2 and package.loaded.fixture==false)
    )lua");
    QTest::newRow("missing-module-messages") << QStringLiteral(R"lua(
      package.searchers={
        function() return 'first' end,
        function() return nil,'ignored' end,
        function() return 42 end,
        function() return false end,
        function() return 'last' end
      }
      expectError(function()require('missing')end,
        "module 'missing' not found:\n\tfirst\n\t42\n\tlast")
    )lua");
    QTest::newRow("missing-module-message-with-embedded-zero") << QStringLiteral(R"lua(
      package.searchers={
        function() return 'first\0discarded' end,
        function() return 'last' end
      }
      expectError(function()require('missing')end,"module 'missing' not found:\n\tfirst")
    )lua");
    QTest::newRow("searchers-stop-at-first-raw-hole") << QStringLiteral(R"lua(
      package.searchers=setmetatable({
        [1]=function() return 'first' end,
        [3]=function() error('search after hole') end
      },{__index=function()error('searcher metatable must not run')end})
      expectError(function()require('missing')end,"module 'missing' not found:\n\tfirst")
    )lua");
    QTest::newRow("callable-searcher-but-function-loader-only") << QStringLiteral(R"lua(
      local rejected=setmetatable({},{__call=function()error('not a function loader')end})
      package.searchers={
        function()return rejected end,
        setmetatable({},{__call=function(_,name)
          return function(module,data)
            assert(module==name and data=='callable searcher')
            return 57
          end,'callable searcher'
        end})
      }
      local value,data=require('fixture')
      assert(value==57 and data=='callable searcher')
    )lua");
    QTest::newRow("invalid-searchers") << QStringLiteral(R"lua(
      package.searchers=false
      expectError(function()require('missing')end,"'package.searchers' must be a table")
    )lua");
    QTest::newRow("failed-loader-can-be-retried") << QStringLiteral(R"lua(
      local loads=0
      package.preload.fixture=function()
        loads=loads+1
        if loads==1 then error('loader failure',0) end
        return 81
      end
      expectError(function()require('fixture')end,'loader failure')
      assert(package.loaded.fixture==nil and require('fixture')==81 and loads==2)
    )lua");
    QTest::newRow("recursive-loading-is-not-rejected") << QStringLiteral(R"lua(
      local loads=0
      package.preload.fixture=function(name)
        loads=loads+1
        if loads<3 then return require(name) end
        return {depth=loads}
      end
      local value=require('fixture')
      assert(value.depth==3 and loads==3 and require('fixture')==value)
    )lua");
    QTest::newRow("original-package-and-registry-cache") << QStringLiteral(R"lua(
      local original,loaded=package,package.loaded
      original.preload.fixture=function()return {value=64}end
      original.loaded={}
      package={}
      local value=require('fixture')
      assert(value.value==64 and loaded.fixture==value and original.loaded.fixture==nil)
      assert(require('fixture')==value)
    )lua");
    QTest::newRow("registry-cache-replacement") << QStringLiteral(R"lua(
      local registry=debug.getregistry()
      local old,loaded=registry._LOADED,{}
      registry._LOADED=loaded
      package.preload.fixture=function()return {value=65}end
      local value=require('fixture')
      assert(value.value==65 and loaded.fixture==value and old.fixture==nil)
      registry._LOADED=old
    )lua");
    QTest::newRow("loading-does-not-depend-on-mutable-globals") << QStringLiteral(R"lua(
      local assert,pcall,kind,sub=assert,pcall,type,string.sub
      local loads=0
      package.preload.fixture=function()loads=loads+1;return 66 end
      local function broken()assert(false,'overridden global must not run')end
      type,rawget,error,select=broken,broken,broken,broken
      table.concat,string.find,string.sub=broken,broken,broken
      debug.getregistry=broken
      assert(require('fixture')==66 and require('fixture')==66 and loads==1)
      package.searchers={function()return 'missing detail'end}
      local ok,message=pcall(require,'missing')
      local expected="module 'missing' not found:\n\tmissing detail"
      assert(not ok and kind(message)=='string' and sub(message,-#expected)==expected)
    )lua");
    QTest::newRow("numeric-module-name") << QStringLiteral(R"lua(
      package.preload['73']=function(name,data)
        assert(name=='73' and data==':preload:')
        return 74
      end
      assert(require(73)==74 and package.loaded['73']==74)
      expectError(function()require()end,
        "bad argument #1 to 'require' (string expected, got no value)")
      expectError(function()require(false)end,
        "bad argument #1 to 'require' (string expected, got boolean)")
    )lua");
    QTest::newRow("named-argument-types-use-raw-real-metatable") << QStringLiteral(R"lua(
      local named=setmetatable({},{__name='Fixture',__metatable='hidden'})
      expectError(function()require(named)end,
        "bad argument #1 to 'require' (string expected, got Fixture)")
      local unnamed=setmetatable({},{__index=function()error('type-name metatable must not run')end})
      expectError(function()require(unnamed)end,
        "bad argument #1 to 'require' (string expected, got table)")
      local numeric=setmetatable({},{__name=42})
      expectError(function()require(numeric)end,
        "bad argument #1 to 'require' (string expected, got table)")
    )lua");
    QTest::newRow("light-userdata-argument-type") << QStringLiteral(R"lua(
      local value=42
      local function closure()return value end
      local pointer=debug.upvalueid(closure,1)
      expectError(function()require(pointer)end,
        "bad argument #1 to 'require' (string expected, got light userdata)")
    )lua");
    QTest::newRow("embedded-zero-keeps-standard-key-and-loader-name") << QStringLiteral(R"lua(
      local name='fixture\0suffix'
      package.preload.fixture=function(module,data)
        assert(module==name and data==':preload:')
        return {value=75}
      end
      local value=require(name)
      assert(value.value==75 and package.loaded.fixture==value and require('fixture')==value)
    )lua");
}
void ScriptHostTest::standardModuleLoadingPreservesLuaSemantics() {
    QFETCH(QString, source);
    WindowManager manager;
    const auto window = manager.createWindow();
    addEditor(manager, window);
    ScriptHost host(&manager);
    QVERIFY(initialize(host));
    const auto reply = run(host, QStringLiteral(R"lua(
      local function expectError(action,expected)
        local ok,message=pcall(action)
        assert(not ok and type(message)=='string')
        assert(message:sub(-#expected)==expected,message)
      end
    )lua") + source + QStringLiteral("\nreturn true"));
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QCOMPARE(reply.values, QVariantList{true});
}
void ScriptHostTest::nestedModuleLoadingCanYieldThroughUserCoroutines() {
    WindowManager manager;
    const auto window = manager.createWindow();
    const auto tab = addEditor(manager, window);
    ScriptHost host(&manager);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto write = [&directory](const QString &name, const QByteArray &source) {
        QFile file(directory.filePath(name));
        return file.open(QIODevice::WriteOnly) && file.write(source) == source.size();
    };
    QVERIFY(write("outer.lua", R"lua(
      local inner=require('inner')
      assert(muxora.tabs.rename(nil,'Nested module'))
      return {tab=muxora.context().tab,inner=inner}
    )lua"));
    QVERIFY(write("inner.lua", R"lua(
      return coroutine.wrap(function()return muxora.context().tab end)()
    )lua"));
    host.setModuleDirectory(directory.path());
    QVERIFY(initialize(host));
    const auto reply = run(host, R"lua(
      local outer=coroutine.create(function()
        return coroutine.wrap(function()
          local ok,value=pcall(require,'outer')
          assert(ok,value)
          assert(value.tab==value.inner)
          return value.tab
        end)()
      end)
      local ok,value=coroutine.resume(outer)
      assert(ok,value)
      assert(coroutine.status(outer)=='dead')
      return value
    )lua");
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QCOMPARE(reply.values, QVariantList{tab.toString(QUuid::WithoutBraces)});
    QCOMPARE(manager.model().tab(tab)->title, QStringLiteral("Nested module"));
}
void ScriptHostTest::cancellingModuleLoadingDoesNotCacheOrContinueTheModule() {
    WindowManager manager;
    const auto window = manager.createWindow();
    const auto tab = addEditor(manager, window, "Unchanged");
    ScriptHost host(&manager);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile module(directory.filePath("cancelled_module.lua"));
    QVERIFY(module.open(QIODevice::WriteOnly));
    const QByteArray source = R"lua(
      muxora.call('fixture.cancel')
      muxora.tabs.rename(nil,'Must not execute')
      return {loaded=true}
    )lua";
    QCOMPARE(module.write(source), source.size());
    module.close();
    int calls = 0;
    host.setServiceHandler([&](QString method, QVariantList, QVariantMap) {
        if (method != "fixture.cancel")
            return ScriptResult{false, {}, "Unexpected host call"};
        ++calls;
        host.runtime()->cancelAll();
        return ScriptResult{true, {true}, {}};
    });
    host.setModuleDirectory(directory.path());
    QVERIFY(initialize(host));
    const auto reply = run(host, R"lua(
      pcall(require,'cancelled_module')
      muxora.tabs.rename(nil,'Must not continue')
    )lua");
    QVERIFY(!reply.ok);
    QVERIFY2(reply.error.contains("cancel", Qt::CaseInsensitive), qPrintable(reply.error));
    QCOMPARE(calls, 1);
    QCOMPARE(manager.model().tab(tab)->title, QStringLiteral("Unchanged"));
    const auto recovered = run(host, R"lua(
      assert(package.loaded.cancelled_module==nil)
      return muxora.context().tab
    )lua");
    QVERIFY2(recovered.ok, qPrintable(recovered.error));
    QCOMPARE(recovered.values, QVariantList{tab.toString(QUuid::WithoutBraces)});
}
void ScriptHostTest::nonyieldableModuleLoadingHasNoHostSideEffects_data() {
    QTest::addColumn<QString>("source");
    QTest::newRow("tostring-metamethod") << QStringLiteral(R"lua(
      tostring(setmetatable({},{__tostring=function()return require('effect')end}))
    )lua");
    QTest::newRow("sort-comparator") << QStringLiteral(R"lua(
      table.sort({2,1},function()require('effect');return false end)
    )lua");
}
void ScriptHostTest::nonyieldableModuleLoadingHasNoHostSideEffects() {
    QFETCH(QString, source);
    WindowManager manager;
    const auto window = manager.createWindow();
    addEditor(manager, window);
    ScriptHost host(&manager);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile module(directory.filePath("effect.lua"));
    QVERIFY(module.open(QIODevice::WriteOnly));
    const QByteArray moduleSource = "return muxora.call('fixture.effect')";
    QCOMPARE(module.write(moduleSource), moduleSource.size());
    module.close();
    int calls = 0;
    host.setServiceHandler([&](QString method, QVariantList, QVariantMap) {
        if (method != "fixture.effect")
            return ScriptResult{false, {}, "Unexpected host call"};
        ++calls;
        return ScriptResult{true, {QStringLiteral("loaded")}, {}};
    });
    host.setModuleDirectory(directory.path());
    QVERIFY(initialize(host));
    const auto reply = run(host, QStringLiteral("local ok,message=pcall(function()\n") + source +
                                     QStringLiteral(R"lua(
      end)
      assert(not ok and message:find('yieldable Lua task',1,true),message)
      assert(package.loaded.effect==nil)
      return true
    )lua"));
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QCOMPARE(reply.values, QVariantList{true});
    QCOMPARE(calls, 0);
    const auto recovered = run(host, "return require('effect')");
    QVERIFY2(recovered.ok, qPrintable(recovered.error));
    QCOMPARE(recovered.values[0].toString(), QStringLiteral("loaded"));
    QCOMPARE(calls, 1);
}
void ScriptHostTest::standardModulesUseUserPathsAndPackageLoaded() {
    WindowManager manager;
    auto window = manager.createWindow();
    addEditor(manager, window);
    ScriptHost host(&manager);
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVERIFY(QDir(root.path()).mkpath("a"));
    QVERIFY(QDir(root.path()).mkpath("folder"));
    const auto write = [&root](const QString &name, const QByteArray &source) {
        QFile file(root.filePath(name));
        return file.open(QIODevice::WriteOnly) && file.write(source) == source.size();
    };
    QVERIFY(write("a.lua", "loaded=(loaded or 0)+1;return {value=42}"));
    QVERIFY(write("a/b.lua", "return {nested=71}"));
    QVERIFY(write("folder/init.lua", "return {directory=93}"));
    QVERIFY(write("empty.lua", "emptyLoads=(emptyLoads or 0)+1"));
    QVERIFY(write("false_value.lua", "falseLoads=(falseLoads or 0)+1;return false"));
    host.setModuleDirectory(root.path());
    QVERIFY(initialize(host));
    auto reply = run(host, R"lua(
      local first,filename=require('a')
      assert(filename:sub(-6)=='/a.lua')
      assert(first==require('a') and package.loaded.a==first and loaded==1)
      assert(require('a.b').nested==71 and require('folder').directory==93)
      assert(require('empty')==true and require('empty')==true and emptyLoads==1)
      assert(require('false_value')==false and require('false_value')==false and falseLoads==2)
      package.loaded.a={value=52}
      assert(require('a').value==52)
      package.loaded.a=nil
      assert(require('a').value==42 and loaded==2)
      return package.path
    )lua");
    QVERIFY2(reply.ok, qPrintable(reply.error));
    const auto prefix = root.path() + "/?.lua;" + root.path() + "/?/init.lua;";
    QVERIFY(reply.values[0].toString().startsWith(prefix));
    QVERIFY(reply.values[0].toString().size() > prefix.size());
    QVERIFY(initialize(host));
    reply = run(host, "assert(package.loaded.a==nil);return require('a').value,loaded");
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QCOMPARE(reply.values, (QVariantList{qlonglong(42), qlonglong(1)}));
}
void ScriptHostTest::standardModulesAllowUserSearchersAndBinaryChunks() {
    WindowManager manager;
    const auto window = manager.createWindow();
    addEditor(manager, window);
    ScriptHost host(&manager);
    QTemporaryDir root;
    QTemporaryDir outside;
    QVERIFY(root.isValid());
    QVERIFY(outside.isValid());
    QFile file(outside.filePath("outside-name.lua"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    const auto largeSource = QByteArray("--") + QByteArray(1024 * 1024 + 1, 'x') + "\nreturn 73";
    QCOMPARE(file.write(largeSource), largeSource.size());
    file.close();
    QFile byteSource(root.filePath("bytes.lua"));
    QVERIFY(byteSource.open(QIODevice::WriteOnly));
    const QByteArray bytes("return '\xff'");
    QCOMPARE(byteSource.write(bytes), bytes.size());
    byteSource.close();
    host.setModuleDirectory(root.path());
    host.setServiceHandler([&](QString method, QVariantList, QVariantMap) {
        return method == "fixture.paths"
                   ? ScriptResult{true, {QVariantMap{{"outside", outside.path()}, {"root", root.path()}}}, {}}
                   : ScriptResult{false, {}, "Unexpected host call"};
    });
    QVERIFY(initialize(host));
    const auto reply = run(host, R"lua(
      local paths=muxora.call('fixture.paths')
      package.path=paths.outside..'/?.lua;'..package.path
      assert(require('outside-name')==73)
      assert(require('bytes'):byte()==255)
      package.preload.fixture=function(name) return {name=name} end
      assert(require('fixture').name=='fixture')
      local binary=assert(io.open(paths.root..'/binary.lua','wb'))
      assert(binary:write(string.dump(function()return {binary=94}end)))
      assert(binary:close())
      assert(require('binary').binary==94)
      return true
    )lua");
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QCOMPARE(reply.values, QVariantList{true});
}
void ScriptHostTest::standardModulesReportMissingFilesAndUnavailableCLibraries() {
    WindowManager manager;
    const auto window = manager.createWindow();
    addEditor(manager, window);
    ScriptHost host(&manager);
    QVERIFY(initialize(host));
    const auto reply = run(host, R"lua(
      assert(package.cpath=='')
      local ok,message=pcall(require,'muxora_fixture_does_not_exist')
      assert(not ok and message:find("module 'muxora_fixture_does_not_exist' not found",1,true))
      assert(message:find('no file',1,true))
      local module,problem=package.loadlib('muxora_fixture_missing.dll','luaopen_fixture')
      assert(module==nil and type(problem)=='string' and #problem>0)
      local legacy,legacyError=pcall(muxora.call,'module.read','a')
      assert(not legacy and legacyError:find('Unknown Lua host method: module.read',1,true))
      return true
    )lua");
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QCOMPARE(reply.values, QVariantList{true});
}
void ScriptHostTest::nestedCoroutinesCanCallTheWindowHost() {
    WindowManager manager;
    const auto window = manager.createWindow();
    const auto tab = addEditor(manager, window);
    ScriptHost host(&manager);
    QVERIFY(initialize(host));
    const auto reply = run(host, R"lua(
      local outer=coroutine.create(function()
        local inner=coroutine.create(function()
          local ok,changed=pcall(muxora.tabs.rename,nil,'Nested rename')
          assert(ok and changed)
          return muxora.context().tab
        end)
        local ok,value=coroutine.resume(inner)
        assert(ok and coroutine.status(inner)=='dead')
        return value
      end)
      local ok,tab=coroutine.resume(outer)
      assert(ok and coroutine.status(outer)=='dead')
      local wrapped=coroutine.wrap(function()
        coroutine.yield('visible',nil,8)
        return muxora.context().tab
      end)
      local first=table.pack(wrapped())
      assert(first.n==3 and first[1]=='visible' and first[2]==nil and first[3]==8)
      assert(wrapped()==tab)
      return tab
    )lua");
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QCOMPARE(reply.values, QVariantList{tab.toString(QUuid::WithoutBraces)});
    QCOMPARE(manager.model().tab(tab)->title, QStringLiteral("Nested rename"));
}
void ScriptHostTest::cancellingANestedHostCallRejectsStaleReplies() {
    WindowManager manager;
    const auto window = manager.createWindow();
    const auto tab = addEditor(manager, window, "Unchanged");
    ScriptHost host(&manager);
    QVERIFY(initialize(host));
    int calls = 0;
    host.setServiceHandler([&](QString method, QVariantList, QVariantMap) {
        if (method != "fixture.cancel")
            return ScriptResult{false, {}, "Unexpected host call"};
        ++calls;
        host.runtime()->cancelAll();
        return ScriptResult{true, {true}, {}};
    });
    const auto reply = run(host, R"lua(
      local nested=coroutine.create(function()
        return pcall(muxora.call,'fixture.cancel')
      end)
      pcall(coroutine.resume,nested)
      muxora.tabs.rename(nil,'Must not execute')
    )lua");
    QVERIFY(!reply.ok);
    QVERIFY(reply.error.contains("cancel", Qt::CaseInsensitive));
    QCOMPARE(calls, 1);
    QCOMPARE(manager.model().tab(tab)->title, QStringLiteral("Unchanged"));
    const auto recovered = run(host, "return muxora.context().tab");
    QVERIFY2(recovered.ok, qPrintable(recovered.error));
    QCOMPARE(recovered.values, QVariantList{tab.toString(QUuid::WithoutBraces)});
}
void ScriptHostTest::osExitUsesTheHostAndDoesNotTerminateAnExternalHost() {
    WindowManager manager;
    const auto window = manager.createWindow();
    const auto tab = addEditor(manager, window);
    ScriptHost host(&manager);
    QVERIFY(initialize(host));
    auto reply = run(host, R"lua(
      local ok,message=pcall(os.exit,17,true)
      assert(not ok and message:find('Unknown Lua host method: app.exit',1,true))
      return muxora.context().tab
    )lua");
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QCOMPARE(reply.values, QVariantList{tab.toString(QUuid::WithoutBraces)});
    int calls = 0;
    host.setServiceHandler([&](QString method, QVariantList args, QVariantMap) {
        if (method != "app.exit" || !args.isEmpty())
            return ScriptResult{false, {}, "Unexpected host call"};
        ++calls;
        return ScriptResult{true, {false}, {}};
    });
    reply = run(host, "assert(os.exit(3,true)==false);assert(muxora.app.exit()==false)");
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QCOMPARE(calls, 2);
    QVERIFY(manager.model().window(window));
    QVERIFY(manager.model().tab(tab));
}
void ScriptHostTest::windowDisplayStateUsesSharedCommands_data() {
    QTest::addColumn<bool>("floating");
    QTest::newRow("workbench") << false;
    QTest::newRow("floating") << true;
}
void ScriptHostTest::windowDisplayStateUsesSharedCommands() {
    QFETCH(bool, floating);
    WindowManager manager;
    const auto root = manager.createWindow();
    const auto window = floating ? manager.createWindow(WindowKind::Floating, {}, root) : root;
    const auto tab = addEditor(manager, window);
    const auto pane = manager.model().tab(tab)->activePane;
    const auto *content = manager.paneWidget(pane);
    ScriptHost host(&manager);
    QVERIFY(initialize(host));
    const auto reply = run(host, R"lua(
        assert(muxora.windows.set_state(nil,'maximized'))
        assert(muxora.windows.state().maximized)
        assert(muxora.commands.execute('window.minimize'))
        assert(muxora.windows.state().minimized)
        assert(muxora.windows.set_state(nil,'normal'))
        local state=muxora.windows.state()
        assert(not state.maximized and not state.minimized and state.mode=='normal')
        assert(muxora.windows.set_mode(nil,'group'))
        assert(muxora.commands.execute('window.restore'))
        assert(muxora.windows.state().mode=='normal')
        assert(not pcall(muxora.windows.set_state,nil,'unknown'))
    )lua",
                           manager.context(window));
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QCOMPARE(manager.paneWidget(pane), content);
    QCOMPARE(manager.model().tab(tab)->activePane, pane);
}
void ScriptHostTest::panelPoliciesCanBeConfiguredWithoutNestedExecution() {
    WindowManager manager;
    const auto window = manager.createWindow();
    addEditor(manager, window);
    ScriptHost host(&manager);
    QVERIFY(initialize(host));
    auto reply = run(host, "return muxora.ui.command_palette(nil,{modal=true,dismiss_on_outside=false})");
    QVERIFY2(reply.ok, qPrintable(reply.error));
    auto panels = manager.windowWidget(window)->findChildren<FloatingPanel *>();
    QCOMPARE(panels.size(), 1);
    QVERIFY(panels[0]->isVisible());
    QVERIFY(panels[0]->isModal());
    QVERIFY(!panels[0]->options().dismissOnOutsidePress);
    panels[0]->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    reply = run(host, "return muxora.ui.window_selector(nil,{modal=false,dismiss_on_outside=true})");
    QVERIFY2(reply.ok, qPrintable(reply.error));
    panels = manager.windowWidget(window)->findChildren<FloatingPanel *>();
    QCOMPARE(panels.size(), 1);
    QVERIFY(panels[0]->isVisible());
    QVERIFY(!panels[0]->isModal());
    QVERIFY(panels[0]->options().dismissOnOutsidePress);
    panels[0]->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    reply = run(host, "return muxora.ui.command_palette(nil,{modal='yes'})");
    QVERIFY(!reply.ok);
    QVERIFY(manager.windowWidget(window)->findChildren<FloatingPanel *>().isEmpty());
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
      assert(muxora.settings.get().caption_button_order=='windows')
      assert(muxora.settings.update({caption_button_order='macos'}))
      assert(muxora.settings.get().caption_button_order=='macos')
      assert(not pcall(muxora.settings.update,{caption_button_order='mac'}))
      return muxora.theme.get().name,muxora.tabs.appearance().colors[1].group_active.background
    )");
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QCOMPARE(reply.values[0].toString(), QString("Lua theme"));
    QCOMPARE(QColor(reply.values[1].toString()), QColor("#fefefe"));
    QCOMPARE(manager.theme().metrics.spacing, 3);
    QCOMPARE(manager.behavior().newTabPosition, NewTabPosition::End);
    QCOMPARE(manager.behavior().dragOutsideKind, WindowKind::Workbench);
    QCOMPARE(manager.behavior().captionButtonOrder, CaptionButtonOrder::MacOS);
    QCOMPARE(manager.paneWidget(pane), originalContent.data());
}
void ScriptHostTest::titleClippingSettingsAreTypedAtomicAndPersistent() {
    WindowManager manager;
    ScriptHost host(&manager);
    QVERIFY(initialize(host));
    auto reply = run(host, R"(
      local initial=muxora.settings.get()
      assert(initial.clip_tab_titles==false and initial.maximum_tab_title_width==160)
      assert(initial.clip_sidebar_titles==false and initial.maximum_sidebar_title_width==160)
      assert(muxora.settings.update({clip_tab_titles=true,maximum_tab_title_width=137}))
      local appearance=muxora.tabs.appearance()
      assert(appearance.clip_tab_titles and appearance.maximum_tab_title_width==137)
      assert(not appearance.clip_sidebar_titles and appearance.maximum_sidebar_title_width==160)
      assert(muxora.tabs.set_appearance({clip_sidebar_titles=true,maximum_sidebar_title_width=113}))
      assert(muxora.settings.get().clip_tab_titles and muxora.settings.get().maximum_tab_title_width==137)
      assert(muxora.tabs.set_appearance({clip_tab_titles=false}))
      local disabled=muxora.settings.get()
      assert(not disabled.clip_tab_titles and disabled.maximum_tab_title_width==137)
      assert(disabled.clip_sidebar_titles and disabled.maximum_sidebar_title_width==113)
      for _,invalid in ipairs({-1,1000001,160.5,'160',false,math.huge,0/0}) do
        assert(not pcall(muxora.settings.update,{clip_tab_titles=true,maximum_tab_title_width=invalid}))
        assert(not pcall(muxora.tabs.set_appearance,{clip_tab_titles=true,maximum_tab_title_width=invalid}))
        local unchanged=muxora.settings.get()
        assert(not unchanged.clip_tab_titles and unchanged.maximum_tab_title_width==137)
        assert(unchanged.clip_sidebar_titles and unchanged.maximum_sidebar_title_width==113)
        assert(not pcall(muxora.settings.update,{maximum_tab_title_width=200,maximum_sidebar_title_width=invalid}))
        assert(not pcall(muxora.tabs.set_appearance,{clip_sidebar_titles=false,maximum_sidebar_title_width=invalid}))
        unchanged=muxora.settings.get()
        assert(not unchanged.clip_tab_titles and unchanged.maximum_tab_title_width==137)
        assert(unchanged.clip_sidebar_titles and unchanged.maximum_sidebar_title_width==113)
      end
      assert(not pcall(muxora.settings.update,{clip_tab_titles='true'}))
      assert(not pcall(muxora.tabs.set_appearance,{clip_tab_titles=1}))
      assert(not pcall(muxora.settings.update,{clip_sidebar_titles='true'}))
      assert(not pcall(muxora.tabs.set_appearance,{clip_sidebar_titles=1}))
      for _,boundary in ipairs({0,1000000}) do
        local sidebar_before=muxora.settings.get().maximum_sidebar_title_width
        assert(muxora.tabs.set_appearance({maximum_tab_title_width=boundary}))
        assert(muxora.settings.get().maximum_tab_title_width==boundary)
        assert(muxora.settings.get().maximum_sidebar_title_width==sidebar_before)
        assert(muxora.settings.update({maximum_sidebar_title_width=boundary}))
        assert(muxora.tabs.appearance().maximum_sidebar_title_width==boundary)
        assert(muxora.settings.get().maximum_tab_title_width==boundary)
      end
      assert(muxora.settings.update({clip_tab_titles=true,maximum_tab_title_width=137,
                                     clip_sidebar_titles=true,maximum_sidebar_title_width=113}))
      local snapshot=muxora.layout.snapshot()
      assert(muxora.settings.update({clip_tab_titles=false,maximum_tab_title_width=200,
                                     clip_sidebar_titles=false,maximum_sidebar_title_width=300}))
      assert(muxora.layout.restore(snapshot))
      return muxora.settings.get()
    )");
    QVERIFY2(reply.ok, qPrintable(reply.error));
    const auto restored = reply.values[0].toMap();
    QVERIFY(restored.value("clip_tab_titles").toBool());
    QCOMPARE(restored.value("maximum_tab_title_width").toInt(), 137);
    QVERIFY(restored.value("clip_sidebar_titles").toBool());
    QCOMPARE(restored.value("maximum_sidebar_title_width").toInt(), 113);
    QVERIFY(manager.tabAppearance().clipTitles); QCOMPARE(manager.tabAppearance().maximumTitleWidth, 137);
    QVERIFY(manager.tabAppearance().clipSidebarTitles); QCOMPARE(manager.tabAppearance().maximumSidebarTitleWidth, 113);
}
void ScriptHostTest::stickyTabSettingsAreTypedAndAtomic() {
    WindowManager manager;
    ScriptHost host(&manager);
    QVERIFY(initialize(host));
    auto reply = run(host, R"(
      assert(muxora.settings.get().sticky_tab_sizing=='normal')
      assert(muxora.tabs.appearance().sticky_tab_sizing=='normal')
      assert(muxora.settings.update({sticky_tab_sizing='compact'}))
      assert(muxora.tabs.appearance().sticky_tab_sizing=='compact')
      assert(muxora.tabs.set_appearance({sticky_tab_sizing='shrink'}))
      assert(muxora.settings.get().sticky_tabs_on_separate_row==false)
      assert(muxora.settings.update({sticky_tabs_on_separate_row=true}))
      assert(muxora.tabs.appearance().sticky_tabs_on_separate_row==true)
      assert(not pcall(muxora.tabs.set_appearance,{sticky_tabs_on_separate_row='false'}))
      assert(muxora.tabs.appearance().sticky_tabs_on_separate_row==true)
      assert(muxora.settings.get().prevent_sticky_close=='keyboard_and_mouse')
      for _,guard in ipairs({'keyboard','mouse','never','keyboard_and_mouse'}) do
        assert(muxora.tabs.set_appearance({prevent_sticky_close=guard}))
        assert(muxora.settings.get().prevent_sticky_close==guard)
      end
      for _,invalid in ipairs({'keyboardAndMouse','always',false}) do
        assert(not pcall(muxora.settings.update,{prevent_sticky_close=invalid}))
        assert(muxora.tabs.appearance().prevent_sticky_close=='keyboard_and_mouse')
      end
      for _,invalid in ipairs({'tiny','Compact',1,true}) do
        assert(not pcall(muxora.settings.update,{sticky_tab_sizing=invalid,multiline=true}))
        assert(not pcall(muxora.tabs.set_appearance,{sticky_tab_sizing=invalid}))
        assert(muxora.settings.get().sticky_tab_sizing=='shrink' and not muxora.settings.get().multiline)
      end
      assert(muxora.tabs.appearance().shrink_sticky_tab_width==80)
      assert(muxora.tabs.set_appearance({shrink_sticky_tab_width=120}))
      for _,invalid in ipairs({37,1001,'120',true}) do
        assert(not pcall(muxora.tabs.set_appearance,{shrink_sticky_tab_width=invalid}))
        assert(muxora.settings.get().shrink_sticky_tab_width==120)
      end
      return muxora.settings.get().sticky_tab_sizing
    )");
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QCOMPARE(reply.values[0].toString(), QStringLiteral("shrink"));
    QCOMPARE(manager.tabAppearance().stickySizing, StickyTabSizing::Shrink);
    QCOMPARE(manager.tabAppearance().shrinkStickyTabWidth, 120);
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
