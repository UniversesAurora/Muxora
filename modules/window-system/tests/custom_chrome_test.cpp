// SPDX-License-Identifier: MIT
#include <window_system/window_manager.hpp>
#include "chrome/command_strip.hpp"
#include "chrome/status_bar.hpp"
#include "chrome/workspace_window.hpp"

#include <QAction>
#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>
#include <memory>

using namespace window_system;

namespace {
WorkspaceWindow *view(WindowManager &manager, WindowId window) {
    return static_cast<WorkspaceWindow *>(manager.windowWidget(window));
}
QList<QAction *> toolbarActions(WindowManager &manager, WindowId window, const QString &id) {
    QList<QAction *> result;
    for (auto *action : manager.windowWidget(window)->findChildren<QAction *>())
        if (action->property("customToolbar").toString() == id)
            result.append(action);
    return result;
}
QLabel *statusItem(WindowManager &manager, WindowId window, const QString &id) {
    for (auto *label : manager.windowWidget(window)->findChildren<QLabel *>("CustomStatusItem"))
        if (label->property("statusItemId").toString() == id)
            return label;
    return nullptr;
}
void addCommand(WindowManager &manager, const QString &id, std::function<bool(const CommandContext &)> run) {
    QVERIFY(manager.commands().add({id,
                                    QStringLiteral("Custom action"),
                                    QStringLiteral("tools"),
                                    {},
                                    {},
                                    std::move(run),
                                    {QStringLiteral("tools"), QStringLiteral("Extensions")}}));
}
} // namespace

class CustomChromeTest final : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); }
    void cleanup() { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }
    void registryNotificationsAndRemovalKeepRunningCallbacksAlive();
    void registryCanBeDestroyedByTheRegistrationObserver();
    void toolbarsUseCommandsAndRetainIndependentWindowLayouts();
    void lockedOptionsPreferenceUpdatesEveryWindowAndRestoresStrictly();
    void toolbarsSurviveKindChangesAndReleaseEveryPresentation();
    void dynamicDefinitionsRejectInvalidAndReservedIds();
    void removedProviderIdsDoNotAccumulateInSavedProfiles();
    void statusItemsUpdateInPlaceAndParticipateInVisibilityAndRestore();
    void renamingPreservesContentAndTheFactoryDescriptor();
};

void CustomChromeTest::lockedOptionsPreferenceUpdatesEveryWindowAndRestoresStrictly() {
    WindowManager manager;
    const auto first = manager.createWindow();
    const auto second = manager.createWindow(WindowKind::Floating, {}, first);
    manager.addTab(first, new QLabel("Main"), {"Main", "label", {}});
    manager.addTab(second, new QLabel("Float"), {"Float", "label", {}});
    QVERIFY(!manager.behavior().hideToolbarOptionsWhenLocked);
    auto behavior = manager.behavior(); behavior.hideToolbarOptionsWhenLocked = true;
    manager.setBehavior(behavior);
    for (auto id : {first, second}) QVERIFY(view(manager, id)->strips()->hideOptionsWhenLocked());
    const auto third = manager.createWindow(); QVERIFY(view(manager, third)->strips()->hideOptionsWhenLocked());
    const auto snapshot = manager.saveLayout();
    QVERIFY(snapshot.value("behavior").toObject().value("hideToolbarOptionsWhenLocked").toBool());
    WindowManager restored;
    restored.setContentFactory([](const TabOptions& options, PaneId) { return new QLabel(options.title); });
    QVERIFY(restored.restoreLayout(snapshot)); QVERIFY(restored.behavior().hideToolbarOptionsWhenLocked);
    for (auto id : restored.model().windowOrder()) QVERIFY(view(restored, id)->strips()->hideOptionsWhenLocked());
    auto invalid = snapshot; auto settings = invalid.value("behavior").toObject();
    settings.insert("hideToolbarOptionsWhenLocked", "true"); invalid.insert("behavior", settings);
    const auto before = restored.saveLayout(); QVERIFY(!restored.restoreLayout(invalid));
    QCOMPARE(restored.saveLayout(), before);
}

void CustomChromeTest::registryNotificationsAndRemovalKeepRunningCallbacksAlive() {
    CommandRegistry registry;
    int changes = 0, calls = 0;
    int bindingChanges = 0;
    registry.setCatalogChangedHandler([&] { ++changes; });
    registry.setBindingsChangedHandler([&] { ++bindingChanges; });
    QVERIFY(registry.add({"custom.selfRemove", "Remove", {}, {}, {}, [&](const CommandContext &) {
                              ++calls;
                              return registry.remove("custom.selfRemove");
                          }}));
    QCOMPARE(changes, 1);
    QVERIFY(registry.execute("custom.selfRemove", {}));
    QCOMPARE(calls, 1);
    QCOMPARE(changes, 2);
    QVERIFY(!registry.find("custom.selfRemove"));
    QVERIFY(!registry.execute("custom.selfRemove", {}));
    QVERIFY(!registry.remove("custom.selfRemove"));
    QVERIFY(!registry.remove("runCommands"));
    QCOMPARE(changes, 2);
    QVERIFY(registry.find("runCommands"));
    QCOMPARE(bindingChanges, 0);
}

void CustomChromeTest::registryCanBeDestroyedByTheRegistrationObserver() {
    auto registry = std::make_unique<CommandRegistry>();
    registry->setCatalogChangedHandler([&] { registry.reset(); });
    QVERIFY(!registry->add(
        {"destroy.owner", "Destroy", {}, {}, {}, [](const CommandContext &) { return true; }}));
    QVERIFY(!registry);
    registry = std::make_unique<CommandRegistry>();
    QVERIFY(
        registry->add({"destroy.owner", "Destroy", {}, {}, {}, [](const CommandContext &) { return true; }}));
    registry->setCatalogChangedHandler([&] { registry.reset(); });
    QVERIFY(!registry->remove("destroy.owner"));
    QVERIFY(!registry);
}

void CustomChromeTest::toolbarsUseCommandsAndRetainIndependentWindowLayouts() {
    WindowManager manager;
    WindowId invokedWindow;
    addCommand(manager, "custom.execute", [&](const CommandContext &ctx) {
        invokedWindow = ctx.window;
        return true;
    });
    const auto first = manager.createWindow();
    QVERIFY(manager.registerToolbar({"custom.tools", "Tools", {"custom.execute"}}));
    const auto second = manager.createWindow();
    QCOMPARE(manager.toolbars().size(), 1);
    QVERIFY(manager.toolbarState(first, "custom.tools"));
    QVERIFY(manager.toolbarState(second, "custom.tools"));
    auto actions = toolbarActions(manager, second, "custom.tools");
    QCOMPARE(actions.size(), 1);
    actions.first()->trigger();
    QTRY_COMPARE(invokedWindow, second);
    QVERIFY(
        manager.execute("window.toolbar.setVisible", {{"strip", "custom.tools"}, {"visible", false}}, first));
    QVERIFY(!manager.toolbarState(first, "custom.tools")->visible);
    QVERIFY(manager.toolbarState(second, "custom.tools")->visible);
    QVERIFY(manager.execute("window.toolbar.setWidth", {{"strip", "custom.tools"}, {"width", 190}}, first));
    QCOMPARE(manager.toolbarState(first, "custom.tools")->width, 190);
    QVERIFY(manager.execute("window.toolbar.setItemVisible",
                            {{"strip", "custom.tools"}, {"item", "custom.execute"}, {"visible", false}},
                            first));
    QVERIFY(manager.toolbarState(first, "custom.tools")->hiddenItems.contains("custom.execute"));
    QVERIFY(manager.toolbarState(second, "custom.tools")->hiddenItems.isEmpty());
    QVERIFY(manager.moveToolbar(first, "custom.tools", ToolbarArea::Bottom, 0));
    QCOMPARE(manager.toolbarState(first, "custom.tools")->area, ToolbarArea::Bottom);
    QCOMPARE(manager.toolbarState(second, "custom.tools")->area, ToolbarArea::Top);
    QVERIFY(manager.setToolbarElastic(first, "custom.tools", true));
    QVERIFY(manager.toolbarState(first, "custom.tools")->elastic);
    QVERIFY(manager.execute("window.toggleStripsLock", {{"locked", false}}, first));
    QVERIFY(!manager.toolbarState(first, "custom.tools")->locked);
    QVERIFY(manager.toolbarState(second, "custom.tools")->locked);
    QVERIFY(!manager.setToolbarElastic(first, "title", false));
    QVERIFY(!manager.setToolbarElastic(first, "caption-controls", true));
}

void CustomChromeTest::toolbarsSurviveKindChangesAndReleaseEveryPresentation() {
    WindowManager manager;
    addCommand(manager, "custom.execute", [](const CommandContext &) { return true; });
    const auto owner = manager.createWindow();
    manager.addTab(owner, new QLabel("Owner"), {"Owner", "label", {}});
    const auto floating = manager.createWindow(WindowKind::Floating, {}, owner);
    manager.addTab(floating, new QLabel("Content"), {"Content", "label", {}});
    const auto actionCount = manager.windowWidget(floating)->findChildren<QAction *>().size();
    for (int iteration = 0; iteration < 50; ++iteration) {
        QVERIFY(
            manager.registerToolbar({"custom.tools", "Tools", {"custom.execute"}, ToolbarArea::Bottom, 0}));
        QCOMPARE(toolbarActions(manager, floating, "custom.tools").size(), 1);
        const QPointer<QAction> previous(toolbarActions(manager, floating, "custom.tools").first());
        QVERIFY(manager.promoteWindow(floating));
        QVERIFY(previous.isNull());
        QCOMPARE(toolbarActions(manager, floating, "custom.tools").size(), 1);
        QVERIFY(manager.demoteWindow(floating, owner));
        QCOMPARE(toolbarActions(manager, floating, "custom.tools").size(), 1);
        const QPointer<QAction> removed(toolbarActions(manager, floating, "custom.tools").first());
        QVERIFY(manager.removeToolbar("custom.tools"));
        QVERIFY(removed.isNull());
        QVERIFY(!manager.toolbarState(floating, "custom.tools"));
        QCOMPARE(toolbarActions(manager, floating, "custom.tools").size(), 0);
        QCOMPARE(manager.windowWidget(floating)->findChildren<QAction *>().size(), actionCount);
    }
    QVERIFY(manager.toolbars().isEmpty());
}

void CustomChromeTest::dynamicDefinitionsRejectInvalidAndReservedIds() {
    WindowManager manager;
    addCommand(manager, "custom.execute", [](const CommandContext &) { return true; });
    const auto window = manager.createWindow();
    const auto before = manager.saveLayout();
    QVERIFY(!manager.registerToolbar({"title", "Reserved", {"custom.execute"}}));
    QVERIFY(!manager.registerToolbar({"bad id", "Invalid", {"custom.execute"}}));
    QVERIFY(!manager.registerToolbar({"missing", "Missing", {"not.registered"}}));
    QVERIFY(!manager.registerToolbar({"duplicates", "Duplicates", {"custom.execute", "custom.execute"}}));
    QVERIFY(!manager.registerToolbar({"huge.row", "Huge", {"custom.execute"}, ToolbarArea::Top, 100000}));
    QVERIFY(!manager.registerStatusItem({"message", "Reserved", {}}));
    QVERIFY(!manager.setToolbarWidth(window, "title", -2));
    QVERIFY(!manager.setToolbarVisible(window, "unknown", true));
    QVERIFY(!manager.removeToolbar("title"));
    QVERIFY(!manager.removeStatusItem("workspace"));
    QCOMPARE(manager.saveLayout(), before);
}

void CustomChromeTest::statusItemsUpdateInPlaceAndParticipateInVisibilityAndRestore() {
    WindowManager manager;
    const auto first = manager.createWindow();
    manager.addTab(first, new QLabel("Content"), {"Content", "label", {}});
    QVERIFY(manager.registerStatusItem({"custom.state", "Initial", "Description"}));
    const auto second = manager.createWindow();
    manager.addTab(second, new QLabel("Second"), {"Second", "label", {}});
    const QPointer<QLabel> label(statusItem(manager, first, "custom.state"));
    QVERIFY(label);
    QCOMPARE(label->text(), QStringLiteral("Initial"));
    QCOMPARE(label->toolTip(), QStringLiteral("Description"));
    QVERIFY(manager.execute("window.statusItem.setText",
                            {{"item", "custom.state"}, {"text", "Updated"}, {"tooltip", "New tooltip"}},
                            first));
    QCOMPARE(statusItem(manager, first, "custom.state"), label.data());
    QCOMPARE(label->text(), QStringLiteral("Updated"));
    QCOMPARE(statusItem(manager, second, "custom.state")->text(), QStringLiteral("Updated"));
    QVERIFY(manager.execute("window.statusItem.setVisible", {{"item", "custom.state"}, {"visible", false}},
                            first));
    QVERIFY(label->isHidden());
    QVERIFY(manager.statusItemVisible(second, "custom.state"));
    auto *bar = manager.windowWidget(first)->findChild<WorkspaceStatusBar *>("StatusBar");
    QVERIFY(bar);
    std::unique_ptr<QMenu> menu(bar->createContextMenu());
    bool found = false;
    for (auto *action : menu->actions())
        if (action->data().toString() == "custom.state") {
            found = true;
            QVERIFY(!action->isChecked());
        }
    QVERIFY(found);
    const auto snapshot = manager.saveLayout();
    WindowManager withoutProvider;
    withoutProvider.setContentFactory(
        [](const TabOptions &options, PaneId) { return new QLabel(options.title); });
    QVERIFY(withoutProvider.restoreLayout(snapshot));
    QVERIFY(!statusItem(withoutProvider, first, "custom.state"));
    QVERIFY(withoutProvider.registerStatusItem({"custom.state", "Late provider", {}}));
    QVERIFY(statusItem(withoutProvider, first, "custom.state")->isHidden());
    WindowManager restored;
    QVERIFY(restored.registerStatusItem({"custom.state", "Restored", {}}));
    restored.setContentFactory([](const TabOptions &options, PaneId) { return new QLabel(options.title); });
    QVERIFY(restored.restoreLayout(snapshot));
    QVERIFY(!restored.statusItemVisible(first, "custom.state"));
    QVERIFY(statusItem(restored, first, "custom.state")->isHidden());
    QVERIFY(manager.removeStatusItem("custom.state"));
    QVERIFY(label.isNull());
    QVERIFY(!statusItem(manager, second, "custom.state"));
    QVERIFY(!manager.statusItemIds().contains("custom.state"));
}

void CustomChromeTest::renamingPreservesContentAndTheFactoryDescriptor() {
    WindowManager manager;
    const auto window = manager.createWindow();
    auto *content = new QLabel("Same content");
    const auto tab = manager.addTab(window, content, {"Old", "label", {{"value", 42}}});
    const auto pane = manager.model().tab(tab)->activePane;
    QVERIFY(manager.execute("tab.rename", {{"title", "Renamed"}}, window));
    QCOMPARE(manager.model().tab(tab)->title, QStringLiteral("Renamed"));
    QCOMPARE(manager.paneWidget(pane), content);
    QCOMPARE(manager.tabOptions(tab).title, QStringLiteral("Renamed"));
    QCOMPARE(manager.tabOptions(tab).state.value("value").toInt(), 42);
    QVERIFY(manager.execute("window.setTitle", {{"title", "Custom window"}}, window));
    QVERIFY(manager.windowWidget(window)->windowTitle().contains("Custom window"));
    QCOMPARE(manager.saveLayout().value("windows").toArray().first().toObject().value("title").toString(),
             QStringLiteral("Custom window"));
    const auto before = manager.saveLayout();
    QVERIFY(!manager.renameTab(QUuid::createUuid(), QStringLiteral("Missing")));
    QCOMPARE(manager.saveLayout(), before);
}

void CustomChromeTest::removedProviderIdsDoNotAccumulateInSavedProfiles() {
    WindowManager manager;
    addCommand(manager, "custom.execute", [](const CommandContext &) { return true; });
    const auto window = manager.createWindow();
    manager.addTab(window, new QLabel("Content"), {"Content", "label", {}});
    for (int iteration = 0; iteration < 50; ++iteration) {
        const auto id = QStringLiteral("temporary.toolbar.%1.end").arg(iteration);
        QVERIFY(manager.registerToolbar({id, "Temporary", {"custom.execute"}, ToolbarArea::Bottom, 0}));
        QVERIFY(manager.setToolbarWidth(window, id, 180));
        QVERIFY(manager.removeToolbar(id));
        const auto json = QJsonDocument(manager.saveLayout()).toJson(QJsonDocument::Compact);
        QVERIFY2(!json.contains(id.toUtf8()), qPrintable(id));
        QVERIFY(manager.toolbars().isEmpty());
        QVERIFY(toolbarActions(manager, window, id).isEmpty());
    }
}

QTEST_MAIN(CustomChromeTest)
#include "custom_chrome_test.moc"
