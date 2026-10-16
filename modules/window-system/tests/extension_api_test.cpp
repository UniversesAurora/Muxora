// SPDX-License-Identifier: MIT
#include <window_system/window_manager.hpp>
#include "commands/command_menus.hpp"
#include "chrome/workspace_window.hpp"
#include "chrome/command_strip.hpp"
#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QMenu>
#include <QTest>

using namespace window_system;
class ExtensionApiTest final : public QObject {
    Q_OBJECT
  private slots:
    void componentCommandsContributeToMatchingContextMenus();
    void contextActionsExecuteOnceAfterTheirMenuIsDeleted();
    void closedContextTargetsCannotAffectAnotherTab();
    void bindingBatchesExposeOnlyTheCommittedState();
    void toolbarWindowMenuActionsExecuteOnlyOnce();
    void commandStateCallbacksMayDestroyTheWindowHost();
    void catalogRegistrationDefersEnabledCallbacksAndCoalescesRefresh();
};
void ExtensionApiTest::componentCommandsContributeToMatchingContextMenus() {
    WindowManager manager;
    const auto window = manager.createWindow();
    const auto tab = manager.addTab(window, new QLabel("Files"), {"Files", "test.files", {}});
    int calls = 0;
    Command action{"component.transfer", "Transfer", "Tools", {}, {}, [&](const CommandContext &) {
                       ++calls;
                       return true;
                   }};
    action.contextMenus = {{ContextMenuTarget::Pane, {"Transfer"}, "test.files", 10},
                           {ContextMenuTarget::Group, {"Component"}, {}, 20}};
    QVERIFY(manager.commands().add(action));
    const auto context = manager.context(window);
    QMenu pane;
    populateContextMenu(&pane, CommandMenuSurface::Pane, &manager, context);
    auto *contributed = pane.findChild<QAction *>("component.transfer");
    QVERIFY(contributed);
    QVERIFY(contributed->isVisible());
    contributed->trigger();
    QTRY_COMPARE(calls, 1);
    QMenu tabMenu;
    populateContextMenu(&tabMenu, CommandMenuSurface::Tab, &manager, context);
    QVERIFY(!tabMenu.findChild<QAction *>("component.transfer"));
    QMenu group;
    populateContextMenu(&group, CommandMenuSurface::Group, &manager, context);
    QVERIFY(group.findChild<QAction *>("component.transfer"));
    const auto other = manager.addTab(window, new QLabel("Notes"), {"Notes", "test.notes", {}});
    QMenu notes;
    populateContextMenu(&notes, CommandMenuSurface::Pane, &manager, manager.context(window));
    QVERIFY(!notes.findChild<QAction *>("component.transfer"));
    QVERIFY(manager.model().tab(tab));
    QVERIFY(manager.model().tab(other));
}
void ExtensionApiTest::contextActionsExecuteOnceAfterTheirMenuIsDeleted() {
    WindowManager manager;
    const auto window = manager.createWindow();
    manager.addTab(window, new QLabel("A"), {"A", "test", {}});
    int calls = 0;
    Command command{"component.once", "Once", "Tools", {}, {}, [&](const CommandContext &) {
                        ++calls;
                        return true;
                    }};
    command.contextMenus = {{ContextMenuTarget::Group, {}, {}, 0}};
    QVERIFY(manager.commands().add(command));
    auto *menu = new QMenu;
    populateContextMenu(menu, CommandMenuSurface::Group, &manager, manager.context(window));
    auto *action = menu->findChild<QAction *>("component.once");
    QVERIFY(action);
    action->trigger();
    delete menu;
    QTRY_COMPARE(calls, 1);
    QApplication::processEvents();
    QCOMPARE(calls, 1);
}
void ExtensionApiTest::closedContextTargetsCannotAffectAnotherTab() {
    WindowManager manager;
    const auto window = manager.createWindow();
    const auto first = manager.addTab(window, new QLabel("A"), {"A", "test", {}});
    const auto captured = manager.context(window);
    QMenu menu;
    populateContextMenu(&menu, CommandMenuSurface::Tab, &manager, captured);
    auto *close = menu.findChild<QAction *>("workspace.closeTab");
    QVERIFY(close);
    const auto second = manager.addTab(window, new QLabel("B"), {"B", "test", {}});
    QVERIFY(manager.closeTab(first));
    close->trigger();
    QApplication::processEvents();
    QVERIFY(manager.model().tab(second));
}
void ExtensionApiTest::bindingBatchesExposeOnlyTheCommittedState() {
    CommandRegistry registry;
    QVERIFY(registry.add(
        {"one", "One", "Test", QKeySequence("Ctrl+1"), {}, [](const CommandContext &) { return true; }}));
    QVERIFY(registry.add(
        {"two", "Two", "Test", QKeySequence("Ctrl+2"), {}, [](const CommandContext &) { return true; }}));
    int notifications = 0;
    bool complete = false;
    registry.setBindingsChangedHandler([&] {
        ++notifications;
        complete = registry.bindings("one")[0].sequence == QKeySequence("Ctrl+3") &&
                   registry.bindings("two")[0].sequence == QKeySequence("Ctrl+4");
    });
    QVERIFY(!registry.setBindingsBatch(
        {{"one", {{QKeySequence("Ctrl+3"), {}}}}, {"two", {{QKeySequence("Ctrl"), {}}}}}));
    QCOMPARE(notifications, 0);
    QCOMPARE(registry.bindings("one")[0].sequence, QKeySequence("Ctrl+1"));
    QVERIFY(registry.setBindingsBatch(
        {{"one", {{QKeySequence("Ctrl+3"), {}}}}, {"two", {{QKeySequence("Ctrl+4"), {}}}}}));
    QCOMPARE(notifications, 1);
    QVERIFY(complete);
}
void ExtensionApiTest::toolbarWindowMenuActionsExecuteOnlyOnce() {
    WindowManager manager;
    const auto window = manager.createWindow();
    manager.addTab(window, new QLabel("A"), {"A", "test", {}});
    int calls = 0;
    Command toggle{"component.windowToggle", "Toggle", "Tools", {}, {}, [&](const CommandContext &) {
                       ++calls;
                       return manager.setStatusBarVisible(window, !manager.statusBarVisible(window));
                   }};
    toggle.contextMenus = {{ContextMenuTarget::Window, {}, {}, 0}};
    QVERIFY(manager.commands().add(toggle));
    const auto view = static_cast<WorkspaceWindow *>(manager.windowWidget(window));
    QVERIFY(view);
    const std::unique_ptr<QMenu> menu(view->strips()->createContextMenu(nullptr));
    auto *action = menu->findChild<QAction *>("component.windowToggle");
    QVERIFY(action);
    const bool before = manager.statusBarVisible(window);
    action->trigger();
    QTRY_COMPARE(manager.statusBarVisible(window), !before);
    QApplication::processEvents();
    QCOMPARE(manager.statusBarVisible(window), !before);
    QCOMPARE(calls, 1);
}
void ExtensionApiTest::commandStateCallbacksMayDestroyTheWindowHost() {
    auto manager = std::make_unique<WindowManager>();
    const auto window = manager->createWindow();
    manager->addTab(window, new QLabel("A"), {"A", "test", {}});
    QPointer<WorkspaceWindow> view = static_cast<WorkspaceWindow *>(manager->windowWidget(window));
    bool destroy = false;
    QVERIFY(manager->commands().add({"component.destroy",
                                     "Destroy",
                                     "Test",
                                     {},
                                     [&](const CommandContext &) {
                                         if (destroy)
                                             manager.reset();
                                         return true;
                                     },
                                     [](const CommandContext &) { return true; },
                                     {"Component"}}));
    QVERIFY(view);
    destroy = true;
    emit manager->commandsChanged();
    QVERIFY(!manager);
    QVERIFY(view.isNull());
}
void ExtensionApiTest::catalogRegistrationDefersEnabledCallbacksAndCoalescesRefresh() {
    WindowManager manager;
    const auto window = manager.createWindow();
    manager.addTab(window, new QLabel("A"), {"A", "test", {}});
    QApplication::processEvents();
    int checks = 0, notifications = 0;
    QObject::connect(&manager, &WindowManager::commandsChanged, &manager, [&] { ++notifications; });
    QVERIFY(manager.commands().add({"component.deferred",
                                    "Deferred",
                                    "Tools",
                                    {},
                                    [&](const CommandContext &) {
                                        ++checks;
                                        return true;
                                    },
                                    [](const CommandContext &) { return true; },
                                    {"Component"}}));
    QVERIFY(manager.commands().add({"component.transient",
                                    "Transient",
                                    "Tools",
                                    {},
                                    {},
                                    [](const CommandContext &) { return true; },
                                    {"Temporary"}}));
    QVERIFY(manager.commands().remove("component.transient"));
    QCOMPARE(checks, 0);
    QCOMPARE(notifications, 0);
    QTRY_COMPARE(notifications, 1);
    QVERIFY(checks > 0);
    const auto view = manager.windowWidget(window);
    QVERIFY(view->findChild<QAction *>("component.deferred"));
    QVERIFY(!view->findChild<QAction *>("component.transient"));
}
QTEST_MAIN(ExtensionApiTest)
#include "extension_api_test.moc"
