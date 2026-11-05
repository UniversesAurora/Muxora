// SPDX-License-Identifier: MIT
// Phase 10.4b: popup input and lifetime. Dimensions: disabled/hidden/separator
// rows, keyboard cascades, outside capture, screen edge, owner destruction.
#include "quick/popup_menu.hpp"
#include "quick/button_item.hpp"
#include "quick/dock_surface_item.hpp"
#include "quick/group_item.hpp"
#include "quick/peek_item.hpp"
#include "quick/rail_item.hpp"
#include "quick/rail_button_item.hpp"
#include "quick/tab_bar_item.hpp"
#include "quick/window_chrome_item.hpp"
#include "native_input.hpp"
#include "quick_menu_test_support.hpp"
#include <window_system/quick_workspace.hpp>
#include <window_system/window_manager.hpp>
#include <window_system/interface_appearance.hpp>
#include <QContextMenuEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QScreen>
#include <QSignalSpy>
#include <QTest>
#include <functional>

using namespace window_system;
using window_system::quick::PopupMenu;
using namespace quick_menu_test;

namespace {
MenuItemDescription action(QString id, bool enabled = true)
{
    MenuItemDescription result;
    result.id = result.commandId = id;
    result.action.title = std::move(id);
    result.action.enabled = enabled;
    return result;
}
class InputItem final : public QQuickItem {
public:
    explicit InputItem(QQuickItem* parent) : QQuickItem(parent) { setAcceptedMouseButtons(Qt::LeftButton); }
    int presses = 0;
protected:
    void mousePressEvent(QMouseEvent* event) override { ++presses; event->accept(); }
};
}

class QuickPopupMenuTest final : public QObject {
    Q_OBJECT
private slots:
    void init()
    {
        QTest::failOnWarning(QRegularExpression(".*(Unable to assign|TypeError|ReferenceError|Binding loop).*"));
    }

    void keyboardSkipsUnavailableRowsAndPreservesPayload_data()
    {
        QTest::addColumn<int>("key");
        QTest::newRow("down") << int(Qt::Key_Down);
        QTest::newRow("home") << int(Qt::Key_Home);
        QTest::newRow("end") << int(Qt::Key_End);
    }
    void keyboardSkipsUnavailableRowsAndPreservesPayload()
    {
        QFETCH(int, key);
        QQuickWindow window;
        window.resize(600, 400); window.show(); window.requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto disabled = action("disabled", false), hidden = action("hidden"), chosen = action("chosen");
        hidden.action.visible = false;
        chosen.action.checkable = true; chosen.action.checked = true;
        chosen.action.shortcut = "Ctrl+Shift+K"; chosen.arguments.insert("value", 42);
        MenuItemDescription separator; separator.kind = MenuItemDescription::Kind::Separator;
        PopupMenu menu(defaultTheme());
        QString command; QVariantMap arguments;
        connect(&menu, &PopupMenu::triggered, this, [&](const auto& item) { command = item.commandId; arguments = item.arguments; });
        QSignalSpy closed(&menu, &PopupMenu::closed);
        QVERIFY(menu.open(window.contentItem(), window.mapToGlobal(QPoint(30, 40)), {disabled, hidden, separator, chosen}));
        QTRY_VERIFY(popup(&window));
        auto* view = popup(&window);
        QTest::keyClick(view, Qt::Key(key));
        QTest::keyClick(view, Qt::Key_Return);
        QTRY_COMPARE(command, QString("chosen"));
        QCOMPARE(arguments.value("value").toInt(), 42);
        QTRY_COMPARE(closed.count(), 1);
        QVERIFY(!popup(&window));
    }

    void nestedKeyboardAndEscapeRestoreFocus_data()
    {
        QTest::addColumn<bool>("execute");
        QTest::newRow("escape") << false;
        QTest::newRow("execute") << true;
    }
    void nestedKeyboardAndEscapeRestoreFocus()
    {
        QFETCH(bool, execute);
        QQuickWindow window;
        window.resize(600, 400); window.show(); window.requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QQuickItem input(window.contentItem()); input.setSize({100, 30}); input.forceActiveFocus();
        auto branch = action("branch"); branch.kind = MenuItemDescription::Kind::Submenu;
        branch.children = {action("nested")};
        PopupMenu menu(defaultTheme());
        QSignalSpy triggered(&menu, &PopupMenu::triggered), closed(&menu, &PopupMenu::closed);
        QVERIFY(menu.open(window.contentItem(), window.mapToGlobal(QPoint(30, 40)), {branch, action("last")}));
        QTRY_VERIFY(popup(&window));
        auto* root = popup(&window);
        QTest::keyClick(root, Qt::Key_Down); QTest::keyClick(root, Qt::Key_Right);
        QTRY_VERIFY(popup(root));
        if (execute) QTest::keyClick(popup(root), Qt::Key_Return);
        else {
            QTest::keyClick(popup(root), Qt::Key_Escape);
            QTRY_VERIFY(!popup(root));
            QVERIFY(root->isVisible());
            QTest::keyClick(root, Qt::Key_Escape);
        }
        QTRY_COMPARE(closed.count(), 1);
        QCOMPARE(triggered.count(), execute ? 1 : 0);
        if (execute) QCOMPARE(triggered.first().first().value<MenuItemDescription>().commandId, QString("nested"));
        QTRY_VERIFY(input.hasActiveFocus());
    }

    void outsideClickOnlyDismissesAndReleasesCapture()
    {
        QQuickWindow window;
        window.resize(600, 400); window.show(); window.requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        InputItem input(window.contentItem()); input.setPosition({20, 300}); input.setSize({150, 60});
        PopupMenu menu(defaultTheme()); QSignalSpy closed(&menu, &PopupMenu::closed);
        QVERIFY(menu.open(window.contentItem(), window.mapToGlobal(QPoint(30, 40)), {action("item")}));
        QTRY_VERIFY(popup(&window));
        native_input::mouse(&window, {80, 330}, true);
        QTRY_COMPARE(closed.count(), 1);
        QCOMPARE(input.presses, 0);
        native_input::mouse(&window, {80, 330}, true);
        QTRY_COMPARE(input.presses, 1);
    }

    void ownerClosingReleasesPopupWindows()
    {
        auto window = std::make_unique<QQuickWindow>();
        window->resize(600, 400); window->show(); window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window.get()));
        auto* menu = new PopupMenu(defaultTheme(), window.get());
        QVERIFY(menu->open(window->contentItem(), window->mapToGlobal(QPoint(30, 40)), {action("item")}));
        QTRY_VERIFY(popup(window.get()));
        const QPointer<QQuickWindow> view(popup(window.get()));
        const QPointer<PopupMenu> session(menu);
        window.reset();
        QVERIFY(!session);
        QTRY_VERIFY(!view);
    }

    void screenEdgeAndScrollingKeepTheLastCommandReachable()
    {
        QQuickWindow window;
        window.resize(600, 400); window.show(); window.requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QVector<MenuItemDescription> items;
        for (int index = 0; index < 100; ++index) items.append(action(QString::number(index)));
        PopupMenu menu(defaultTheme());
        QString command;
        connect(&menu, &PopupMenu::triggered, this, [&](const auto& item) { command = item.commandId; });
        const auto available = window.screen()->availableGeometry();
        QVERIFY(menu.open(window.contentItem(), available.bottomRight() - QPoint(1, 1), items));
        QTRY_VERIFY(popup(&window));
        auto* view = popup(&window);
        QTRY_VERIFY(available.contains(view->geometry()));
        QTest::keyClick(view, Qt::Key_End); QTest::keyClick(view, Qt::Key_Return);
        QTRY_COMPARE(command, QString("99"));
    }

    void workspaceMenusKeepSelectionAndCloseTheCapturedTab_data()
    {
        QTest::addColumn<int>("source");
        QTest::newRow("tab-strip") << 0;
        QTest::newRow("sidebar") << 1;
        QTest::newRow("peek-header") << 2;
    }
    void workspaceMenusKeepSelectionAndCloseTheCapturedTab()
    {
        QFETCH(int, source);
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto window = manager.createWindow();
        const auto first = manager.addTab(window, {QStringLiteral("First")});
        const auto target = manager.addTab(window, {QStringLiteral("Target")});
        if (source) QVERIFY(manager.moveTabToEdgeBar(target, window, Edge::Left));
        QVERIFY(manager.activateTab(first));
        QTRY_VERIFY(workspace.window(window));
        auto* view = workspace.window(window);
        view->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(view));
        auto* dock = view->contentItem()->findChild<quick::DockSurfaceItem*>(); QVERIFY(dock);
        if (source == 2) {
            dock->showPeek(manager.model().groupForTab(target), target);
            QTRY_VERIFY(dock->peek());
            auto* panel = dock->peek();
            const QPoint point(30, 5);
            QContextMenuEvent event(QContextMenuEvent::Mouse, panel->mapToScene(point).toPoint(), panel->mapToGlobal(point).toPoint());
            QCoreApplication::sendEvent(view, &event);
            QTRY_VERIFY(popup(view));
            QTest::keyClick(popup(view), Qt::Key_Escape);
            QTRY_VERIFY(!popup(view));
            QVERIFY(dock->peek());
            QCoreApplication::sendEvent(view, &event);
        } else if (source == 1) {
            QTRY_VERIFY(dock->rail(Edge::Left));
            auto* button = dock->rail(Edge::Left)->buttons().first();
            QTest::mouseClick(view, Qt::RightButton, Qt::NoModifier,
                button->mapToScene(button->boundingRect().center()).toPoint());
        } else {
            auto* group = dock->group(manager.model().groupForTab(target)); QVERIFY(group);
            auto* bar = group->tabBar();
            const auto point = bar->controller().tabRect(bar->controller().indexOf(target)).center();
            QContextMenuEvent event(QContextMenuEvent::Mouse, bar->mapToScene(point).toPoint(), bar->mapToGlobal(point).toPoint());
            QCoreApplication::sendEvent(view, &event);
        }
        QTRY_VERIFY(popup(view));
        QCOMPARE(manager.context().tab, source == 2 ? target : first);
        auto* menu = popup(view);
        QVERIFY(QTest::qWaitForWindowExposed(menu));
        auto* close = namedItem(menu->contentItem(), QStringLiteral("workspace.closeTab")); QVERIFY(close);
        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
            close->mapToScene(close->boundingRect().center()).toPoint());
        QTRY_VERIFY(!manager.model().tab(target));
        QCOMPARE(manager.context().tab, first);
        QVERIFY(manager.model().tab(first));
        QTRY_VERIFY(!popup(view));
    }

    void paneMenuTargetsThePaneUnderThePointer()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto window = manager.createWindow();
        const auto tab = manager.addTab(window, {QStringLiteral("Panes")});
        const auto first = manager.model().tab(tab)->activePane;
        const auto target = manager.splitPane(tab, first, Placement::Right); QVERIFY(!target.isNull());
        QVERIFY(manager.focusPane(tab, first));
        QTRY_VERIFY(workspace.content(target) && workspace.content(target)->window());
        auto* view = workspace.window(window);
        view->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(view));
        auto* content = workspace.content(target);
        const auto point = content->mapToScene(content->boundingRect().center()).toPoint();
        QContextMenuEvent event(QContextMenuEvent::Mouse, point, view->mapToGlobal(point));
        QCoreApplication::sendEvent(view, &event);
        QTRY_VERIFY(popup(view));
        QCOMPARE(manager.context().pane, target);
        auto* menu = popup(view); QVERIFY(QTest::qWaitForWindowExposed(menu));
        auto* close = namedItem(menu->contentItem(), QStringLiteral("pane.close")); QVERIFY(close);
        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, close->mapToScene(close->boundingRect().center()).toPoint());
        QTRY_VERIFY(!manager.model().panes(tab).contains(target));
        QCOMPARE(manager.model().panes(tab), QVector<PaneId>{first});
    }

    void managerMenuRequestsUseTheQuickWindow()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto window = manager.createWindow();
        const auto first = manager.addTab(window, {QStringLiteral("First")});
        const auto second = manager.addTab(window, {QStringLiteral("Second")});
        const auto folder = manager.createTabFolder({first, second}, QStringLiteral("Folder")); QVERIFY(!folder.isNull());
        QTRY_VERIFY(workspace.window(window));
        auto* view = workspace.window(window);
        view->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(view));
        const auto global = view->mapToGlobal(QPoint(80, 80));
        manager.showTabFolderMenu(folder, global);
        QTRY_VERIFY(popup(view));
        auto* menu = popup(view); QVERIFY(QTest::qWaitForWindowExposed(menu));
        auto* ungroup = namedItem(menu->contentItem(), QStringLiteral("tabFolder.ungroup")); QVERIFY(ungroup);
        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, ungroup->mapToScene(ungroup->boundingRect().center()).toPoint());
        QTRY_VERIFY(!manager.model().folder(folder));
        QVERIFY(manager.model().tab(first)); QVERIFY(manager.model().tab(second));
        manager.showGroupMenu(manager.model().groupForTab(first), global);
        QTRY_VERIFY(popup(view));
        QTest::keyClick(popup(view), Qt::Key_Escape);
        QTRY_VERIFY(!popup(view));
    }

    void tabListActionsFollowTheTabAcrossReordering_data()
    {
        QTest::addColumn<QString>("operation");
        QTest::newRow("activate") << QString{};
        QTest::newRow("close") << QStringLiteral("close");
        QTest::newRow("unstick") << QStringLiteral("unstick");
    }
    void tabListActionsFollowTheTabAcrossReordering()
    {
        QFETCH(QString, operation);
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto window = manager.createWindow();
        const auto first = manager.addTab(window, {QStringLiteral("First")});
        const auto target = manager.addTab(window, {QStringLiteral("Target & title")});
        if (operation == "unstick") QVERIFY(manager.setTabSticky(target, true));
        QVERIFY(manager.activateTab(first));
        QTRY_VERIFY(workspace.window(window));
        auto* view = workspace.window(window); QVERIFY(view);
        view->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(view));
        auto* dock = view->contentItem()->findChild<quick::DockSurfaceItem*>(); QVERIFY(dock);
        auto* group = dock->group(manager.model().groupForTab(target)); QVERIFY(group);
        auto* button = group->findChild<quick::ButtonItem*>("TabListButton"); QVERIFY(button);
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier,
            button->mapToScene(button->boundingRect().center()).toPoint());
        QTRY_VERIFY(popup(view));
        auto* menu = popup(view); QVERIFY(QTest::qWaitForWindowExposed(menu));
        auto* row = namedItem(menu->contentItem(), target.toString(QUuid::WithoutBraces)); QVERIFY(row);
        if (operation != "unstick") QVERIFY(manager.reorderTab(target, false));
        if (!operation.isEmpty()) {
            native_input::mouse(menu, row->mapToScene(row->boundingRect().center()).toPoint());
            row = namedItem(row, operation); QVERIFY(row);
            QTRY_VERIFY(row->isVisible());
        }
        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier,
            row->mapToScene(row->boundingRect().center()).toPoint());
        QTRY_VERIFY(!popup(view));
        if (operation == "close") QTRY_VERIFY(!manager.model().tab(target));
        else if (operation == "unstick") QTRY_VERIFY(!manager.isTabSticky(target));
        else QTRY_COMPARE(manager.context().tab, target);
        QVERIFY(manager.model().tab(first));
    }

    void paneTextMenuPreservesEditingAndReadOnlyActions_data()
    {
        QTest::addColumn<bool>("readOnly");
        QTest::addColumn<QByteArray>("type");
        QTest::newRow("editor") << false << QByteArray("TextEdit");
        QTest::newRow("read-only-editor") << true << QByteArray("TextEdit");
        QTest::newRow("input") << false << QByteArray("TextInput");
        QTest::newRow("read-only-input") << true << QByteArray("TextInput");
    }
    void paneTextMenuPreservesEditingAndReadOnlyActions()
    {
        QFETCH(bool, readOnly);
        QFETCH(QByteArray, type);
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQuick; " + type + " { text: 'Alpha Beta'; selectByMouse: true }", QUrl());
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([&](const TabOptions&, PaneId) { return qobject_cast<QQuickItem*>(component.create()); });
        const auto window = manager.createWindow();
        const auto tab = manager.addTab(window, {QStringLiteral("Editor")});
        const auto pane = manager.model().tab(tab)->activePane;
        QTRY_VERIFY(workspace.content(pane) && workspace.content(pane)->window());
        auto* editor = workspace.content(pane);
        editor->setProperty("readOnly", readOnly);
        auto* view = workspace.window(window);
        view->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(view));
        editor->forceActiveFocus();
        QVERIFY(QMetaObject::invokeMethod(editor, "select", Q_ARG(int, 0), Q_ARG(int, 5)));
        const auto point = editor->mapToScene(QPointF(20, 5)).toPoint();
        QContextMenuEvent event(QContextMenuEvent::Mouse, point, view->mapToGlobal(point));
        QCoreApplication::sendEvent(view, &event);
        QTRY_VERIFY(popup(view));
        auto* menu = popup(view); QVERIFY(QTest::qWaitForWindowExposed(menu));
        QVERIFY(namedItem(menu->contentItem(), QStringLiteral("pane.close")));
        if (readOnly) {
            auto* remove = namedItem(menu->contentItem(), QStringLiteral("edit.delete")); QVERIFY(remove);
            QVERIFY(!remove->isVisible());
            editor->setProperty("readOnly", false);
            // Both Qt text controls move the cursor and clear the selection
            // when readOnly changes. Restore a selection before deleting.
            QVERIFY(QMetaObject::invokeMethod(editor, "select", Q_ARG(int, 0), Q_ARG(int, 5)));
            QTRY_VERIFY2(remove->isVisible() && remove->isEnabled(), qPrintable(QStringLiteral("visible=%1 enabled=%2 readOnly=%3 selection=%4")
                .arg(remove->isVisible()).arg(remove->isEnabled()).arg(editor->property("readOnly").toBool()).arg(editor->property("selectedText").toString())));
            editor->setProperty("readOnly", true);
            QTRY_VERIFY(!remove->isVisible() && !remove->isEnabled());
        }
        auto* command = namedItem(menu->contentItem(), readOnly ? QStringLiteral("edit.selectAll") : QStringLiteral("edit.delete"));
        QVERIFY(command);
        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, command->mapToScene(command->boundingRect().center()).toPoint());
        QTRY_VERIFY(!popup(view));
        if (readOnly) QTRY_COMPARE(editor->property("selectedText").toString(), QStringLiteral("Alpha Beta"));
        else {
            QTRY_COMPARE(editor->property("text").toString(), QStringLiteral(" Beta"));
            QContextMenuEvent keyboard(QContextMenuEvent::Keyboard, point, view->mapToGlobal(point));
            QCoreApplication::sendEvent(view, &keyboard);
            QTRY_VERIFY(popup(view));
            menu = popup(view); QVERIFY(QTest::qWaitForWindowExposed(menu));
            command = namedItem(menu->contentItem(), QStringLiteral("edit.undo")); QVERIFY(command);
            QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, command->mapToScene(command->boundingRect().center()).toPoint());
            QTRY_COMPARE(editor->property("text").toString(), QStringLiteral("Alpha Beta"));
        }
        QCOMPARE(manager.model().panes(tab), QVector<PaneId>{pane});
    }

    void aCommandQueryCanReleaseTheWorkspace_data()
    {
        QTest::addColumn<bool>("initial");
        QTest::newRow("initial-menu-bar") << true;
        QTest::newRow("context-menu") << false;
    }
    void aCommandQueryCanReleaseTheWorkspace()
    {
        QFETCH(bool, initial);
        WindowManager manager(WindowManager::Presentation::External);
        auto workspace = std::make_unique<QuickWorkspace>(manager);
        workspace->setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto window = manager.createWindow();
        const auto tab = manager.addTab(window, {QStringLiteral("Target")});
        Command command;
        command.id = "test.release"; command.title = "Release";
        command.contextMenus = {{ContextMenuTarget::Tab}};
        command.enabled = [&](const CommandContext&) { workspace.reset(); return true; };
        command.execute = [](const CommandContext&) { return true; };
        if (initial) {
            QVERIFY(manager.commands().add(std::move(command)));
            QTRY_VERIFY(!workspace);
            QVERIFY(manager.model().tab(tab));
            return;
        }
        QTRY_VERIFY(workspace->window(window));
        auto* view = workspace->window(window);
        view->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(view));
        auto* dock = view->contentItem()->findChild<quick::DockSurfaceItem*>(); QVERIFY(dock);
        auto* bar = dock->group(manager.model().groupForTab(tab))->tabBar();
        QVERIFY(manager.commands().add(std::move(command)));
        const auto point = bar->controller().tabRect(0).center();
        QContextMenuEvent event(QContextMenuEvent::Mouse, point, bar->mapToGlobal(point).toPoint());
        QCoreApplication::sendEvent(bar, &event);
        QVERIFY(!workspace);
        QVERIFY(manager.model().tab(tab));
    }

    void floatingTitleMenuTargetsItsOwnWindow()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto main = manager.createWindow();
        const auto first = manager.addTab(main, {QStringLiteral("Main")});
        const auto floating = manager.createWindow(WindowKind::Floating);
        const auto second = manager.addTab(floating, {QStringLiteral("Floating")});
        QTRY_VERIFY(workspace.window(floating));
        auto* view = workspace.window(floating);
        view->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(view));
        auto* chrome = view->contentItem()->findChild<quick::WindowChromeItem*>(); QVERIFY(chrome);
        const auto point = chrome->mapToScene(chrome->boundingRect().center()).toPoint();
        QContextMenuEvent event(QContextMenuEvent::Mouse, point, view->mapToGlobal(point));
        QCoreApplication::sendEvent(view, &event);
        QTRY_VERIFY(popup(view));
        auto* menu = popup(view); QVERIFY(QTest::qWaitForWindowExposed(menu));
        auto* kind = namedItem(menu->contentItem(), QStringLiteral("window.kind")); QVERIFY(kind);
        native_input::mouse(menu, kind->mapToScene(kind->boundingRect().center()).toPoint());
        QTRY_VERIFY(kind->property("highlighted").toBool());
        QTest::keyClick(menu, Qt::Key_Right);
        QTRY_VERIFY(popup(menu));
        menu = popup(menu); QVERIFY(QTest::qWaitForWindowExposed(menu));
        auto* promote = namedItem(menu->contentItem(), QStringLiteral("window.promote")); QVERIFY(promote);
        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, promote->mapToScene(promote->boundingRect().center()).toPoint());
        QTRY_COMPARE(manager.model().window(floating)->kind, WindowKind::Workbench);
        QCOMPARE(manager.model().group(manager.model().groupForTab(first))->windowId, main);
        QCOMPARE(manager.model().group(manager.model().groupForTab(second))->windowId, floating);
    }

    void submenuQueriesCurrentStateWithoutLosingItsTarget_data()
    {
        QTest::addColumn<bool>("release");
        QTest::newRow("execute") << false;
        QTest::newRow("query-releases-workspace") << true;
    }
    void submenuQueriesCurrentStateWithoutLosingItsTarget()
    {
        QFETCH(bool, release);
        WindowManager manager(WindowManager::Presentation::External);
        auto workspace = std::make_unique<QuickWorkspace>(manager);
        workspace->setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto window = manager.createWindow();
        const auto tab = manager.addTab(window, {QStringLiteral("Target")});
        bool enabled = false, destroy = false;
        TabId executed;
        Command command;
        command.id = "test.dynamic"; command.title = "Dynamic";
        command.contextMenus = {{ContextMenuTarget::Tab, {"Runtime"}}};
        command.enabled = [&](const CommandContext&) { if (destroy) workspace.reset(); return enabled; };
        command.execute = [&](const CommandContext& context) { executed = context.tab; return true; };
        QVERIFY(manager.commands().add(command));
        command.id = "test.other"; command.contextMenus = {{ContextMenuTarget::Tab, {"Other"}}};
        command.enabled = [](const CommandContext&) { return false; };
        QVERIFY(manager.commands().add(command));
        QTRY_VERIFY(workspace->window(window));
        const QPointer<QQuickWindow> view(workspace->window(window));
        view->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(view));
        auto* dock = view->contentItem()->findChild<quick::DockSurfaceItem*>(); QVERIFY(dock);
        dock->showContextMenu(CommandMenuSurface::Tab, manager.context(window), view->mapToGlobal(QPoint(80, 80)));
        QTRY_VERIFY(popup(view));
        auto* root = popup(view); QVERIFY(QTest::qWaitForWindowExposed(root));
        // Contributions use the same branch type ID; their paths distinguish
        // the two branches when live state is applied to retained rows.
        auto* branch = namedItem(root->contentItem(), QStringLiteral("ExtensionContextMenu")); QVERIFY(branch);
        QTest::keyClick(root, Qt::Key_End); QTest::keyClick(root, Qt::Key_Up);
        QTRY_VERIFY(branch->property("highlighted").toBool());
        QTest::keyClick(root, Qt::Key_Right);
        QTRY_VERIFY(popup(root));
        auto* child = popup(root); QVERIFY(QTest::qWaitForWindowExposed(child));
        auto* row = namedItem(child->contentItem(), QStringLiteral("test.dynamic")); QVERIFY(row);
        QVERIFY(!row->isEnabled());
        QTest::keyClick(child, Qt::Key_Escape); QTRY_VERIFY(!popup(root));
        enabled = true; destroy = release;
        QTest::keyClick(root, Qt::Key_Right);
        if (release) { QTRY_VERIFY(!workspace); QTRY_VERIFY(!view); return; }
        QTRY_VERIFY(popup(root));
        child = popup(root); QVERIFY(QTest::qWaitForWindowExposed(child));
        row = namedItem(child->contentItem(), QStringLiteral("test.dynamic")); QVERIFY(row);
        QTRY_VERIFY(row->isEnabled());
        QTest::mouseClick(child, Qt::LeftButton, Qt::NoModifier, row->mapToScene(row->boundingRect().center()).toPoint());
        QTRY_COMPARE(executed, tab);
        QVERIFY(manager.model().tab(tab));
    }

    void newTabTypesRefreshAndUseTheirButtonGroup()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto window = manager.createWindow();
        const auto first = manager.addTab(window, {QStringLiteral("First")});
        const auto target = manager.addTab(window, {QStringLiteral("Target")});
        QVERIFY(manager.moveTab(target, manager.model().groupForTab(first), Placement::Right));
        const auto targetGroup = manager.model().groupForTab(target);
        bool allowed = false;
        GroupId executed;
        Command command;
        command.id = "test.create"; command.title = "New content";
        command.enabled = [&](const CommandContext& context) { return allowed && context.group == targetGroup; };
        command.execute = [&](const CommandContext& context) { executed = context.group; return true; };
        QVERIFY(manager.commands().add(std::move(command)));
        QVERIFY(manager.setTabCreationCommands("test.create", {"test.create"}));
        QVERIFY(manager.activateTab(first));
        QTRY_VERIFY(workspace.window(window));
        auto* view = workspace.window(window);
        view->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(view));
        auto* dock = view->contentItem()->findChild<quick::DockSurfaceItem*>(); QVERIFY(dock);
        auto* group = dock->group(targetGroup); QVERIFY(group);
        auto* button = group->findChild<quick::ButtonItem*>("NewTabTypeButton"); QVERIFY(button);
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, button->mapToScene(button->boundingRect().center()).toPoint());
        QTRY_VERIFY(popup(view));
        auto* menu = popup(view); QVERIFY(QTest::qWaitForWindowExposed(menu));
        auto* create = namedItem(menu->contentItem(), QStringLiteral("test.create")); QVERIFY(create);
        QVERIFY(!create->isEnabled());
        allowed = true;
        QVERIFY(manager.commands().setShortcut("test.create", QKeySequence("Ctrl+Alt+N")));
        QTRY_VERIFY(create->isEnabled());
        QCOMPARE(manager.context().tab, first);
        QTest::keyClick(menu, Qt::Key_Home); QTest::keyClick(menu, Qt::Key_Return);
        QTRY_COMPARE(executed, targetGroup);
        QTRY_VERIFY(!popup(view));
        QVERIFY(manager.model().tab(first)); QVERIFY(manager.model().tab(target));
    }

    void structureUpdatesRetainInputAndReleaseRetiredRows_data()
    {
        QTest::addColumn<bool>("branch");
        QTest::newRow("action") << false;
        QTest::newRow("open-submenu") << true;
    }
    void structureUpdatesRetainInputAndReleaseRetiredRows()
    {
        QFETCH(bool, branch);
        QQuickWindow window;
        window.resize(600, 400); window.show(); window.requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto retired = action("retired");
        if (branch) { retired.kind = MenuItemDescription::Kind::Submenu; retired.children = {action("child")}; }
        QVector<MenuItemDescription> items{action("keep"), retired};
        PopupMenu menu(defaultTheme());
        menu.setRefreshHandler([&] { return items; });
        QString invoked;
        connect(&menu, &PopupMenu::triggered, this, [&](const auto& item) { invoked = item.commandId; });
        QVERIFY(menu.open(window.contentItem(), window.mapToGlobal(QPoint(30, 40)), items));
        QTRY_VERIFY(popup(&window));
        auto* root = popup(&window); QVERIFY(QTest::qWaitForWindowExposed(root));
        const QPointer<QQuickItem> keep(namedItem(root->contentItem(), QStringLiteral("keep")));
        const QPointer<QQuickItem> removed(namedItem(root->contentItem(), QStringLiteral("retired")));
        QVERIFY(keep); QVERIFY(removed);
        QPointer<quick::MenuEntry> entry;
        for (auto* candidate : menu.findChildren<quick::MenuEntry*>()) if (candidate->id() == "retired") entry = candidate;
        QVERIFY(entry);
        QTest::keyClick(root, branch ? Qt::Key_End : Qt::Key_Home);
        QPointer<QQuickWindow> child;
        if (branch) {
            QTest::keyClick(root, Qt::Key_Right);
            QTRY_VERIFY(popup(root));
            child = popup(root);
            QVERIFY(QTest::qWaitForWindowExposed(child));
        }
        items = {action("added"), action("keep")};
        menu.requestRefresh();
        QTRY_VERIFY(namedItem(root->contentItem(), QStringLiteral("added")));
        QCOMPARE(namedItem(root->contentItem(), QStringLiteral("keep")), keep.data());
        QTRY_VERIFY(!removed); QTRY_VERIFY(!entry); QTRY_VERIFY(!child);
        QVERIFY(root->isVisible());
        if (branch) QTest::keyClick(root, Qt::Key_End);
        // Inserting ahead of the selected action must not redirect Enter.
        QTest::keyClick(root, Qt::Key_Return);
        QTRY_COMPARE(invoked, QStringLiteral("keep"));
    }

    void repeatedContributionsKeepOneRowEachAcrossRefreshes()
    {
        QQuickWindow window;
        window.resize(600, 400); window.show(); window.requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        // A host contribution may repeat a built-in command in the same menu.
        QVector<MenuItemDescription> items{action("same"), action("same")};
        PopupMenu menu(defaultTheme());
        menu.setRefreshHandler([&] { return items; });
        QVERIFY(menu.open(window.contentItem(), window.mapToGlobal(QPoint(30, 40)), items));
        QTRY_VERIFY(popup(&window));
        auto* root = popup(&window); QVERIFY(QTest::qWaitForWindowExposed(root));
        const auto rows = [root] {
            int count = 0;
            const std::function<void(QQuickItem*)> visit = [&](QQuickItem* item) {
                if (item->objectName() == QStringLiteral("same") && item->isVisible()) ++count;
                for (auto* child : item->childItems()) visit(child);
            };
            visit(root->contentItem());
            return count;
        };
        QCOMPARE(rows(), 2);
        for (const auto* marker : {"first", "second"}) {
            items.append(action(marker));
            menu.requestRefresh();
            QTRY_VERIFY(namedItem(root->contentItem(), QString::fromLatin1(marker)));
            QCOMPARE(rows(), 2);
        }
    }

    // Hover tips, as QToolTip shows them: a button's after the wake-up
    // delay, a tab's title at once while tips are awake, and none once the
    // pointer leaves.
    void hoverTipsFollowThePointer()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto window = manager.createWindow();
        manager.addTab(window, {QStringLiteral("First tab")});
        manager.addTab(window, {QStringLiteral("Second tab")});
        QTRY_VERIFY(workspace.window(window));
        const QPointer<QQuickWindow> view(workspace.window(window));
        view->resize(900, 500);
        view->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(view));
        const auto shownTip = [&]() -> QString {
            for (auto* candidate : QGuiApplication::topLevelWindows()) {
                auto* tip = qobject_cast<QQuickWindow*>(candidate);
                if (!tip || tip == view || !tip->isVisible() || tip->transientParent() != view) continue;
                QList<QQuickItem*> items{tip->contentItem()};
                while (!items.isEmpty()) {
                    auto* item = items.takeFirst();
                    if (const auto text = item->property("text"); text.isValid() && !text.toString().isEmpty()) return text.toString();
                    items += item->childItems();
                }
            }
            return {};
        };
        auto* create = view->contentItem()->findChild<quick::ButtonItem*>(QStringLiteral("NewTabButton"));
        QVERIFY(create);
        native_input::mouse(view.data(), create->mapToScene(create->boundingRect().center()).toPoint());
        QTRY_COMPARE(shownTip(), QStringLiteral("New tab"));
        QPointer<quick::TabBarItem> bar;
        for (auto* item : view->contentItem()->findChildren<quick::TabBarItem*>()) if (item->isVisible()) bar = item;
        QVERIFY(bar);
        const auto title = bar->controller().tabRect(1).center();
        native_input::mouse(view.data(), bar->mapToScene(QPointF(title)).toPoint());
        QTRY_COMPARE(shownTip(), QStringLiteral("Second tab"));
        // The dock below the strips has no tip.
        native_input::mouse(view.data(), QPoint(view->width() / 2, view->height() - 40));
        QTRY_VERIFY(shownTip().isEmpty());
    }
    // The command palette and window selector in Quick windows, as in
    // Widgets: typing filters, Enter runs the selected row, Esc closes, and
    // the selector starts on its own window.
    void palettesRunTheirChoiceFromTheKeyboard()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        int runs = 0;
        Command command;
        command.id = "test.palette"; command.title = "Palette target";
        command.execute = [&](const CommandContext&) { ++runs; return true; };
        QVERIFY(manager.commands().add(std::move(command)));
        const auto main = manager.createWindow();
        manager.addTab(main, {QStringLiteral("Main")});
        const auto floating = manager.createWindow(WindowKind::Floating);
        manager.addTab(floating, {QStringLiteral("Floating")});
        QTRY_VERIFY(workspace.window(main) && workspace.window(floating));
        auto* view = workspace.window(main);
        view->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(view));
        const auto panel = [](const QString& name) -> QQuickWindow* {
            for (auto* window : QGuiApplication::topLevelWindows())
                if (window->objectName() == name && window->isVisible()) return qobject_cast<QQuickWindow*>(window);
            return nullptr;
        };
        manager.showCommandPalette(main);
        QTRY_VERIFY(panel("CommandPalette"));
        QPointer<QQuickWindow> palette(panel("CommandPalette"));
        QVERIFY(QTest::qWaitForWindowActive(palette));
        for (const char letter : QByteArrayLiteral("palette target")) QTest::keyClick(palette.data(), letter);
        QTRY_VERIFY(namedItem(palette->contentItem(), QStringLiteral("PanelRow/test.palette")));
        QTest::keyClick(palette.data(), Qt::Key_Return);
        QTRY_COMPARE(runs, 1);
        QTRY_VERIFY(!palette);
        manager.showCommandPalette(main);
        QTRY_VERIFY(panel("CommandPalette"));
        palette = panel("CommandPalette");
        QVERIFY(QTest::qWaitForWindowActive(palette));
        QTest::keyClick(palette.data(), Qt::Key_Escape);
        QTRY_VERIFY(!palette);
        QCOMPARE(runs, 1);
        manager.showWindowSelector(main);
        QTRY_VERIFY(panel("WindowSelector"));
        QPointer<QQuickWindow> selector(panel("WindowSelector"));
        QVERIFY(QTest::qWaitForWindowActive(selector));
        QVERIFY(namedItem(selector->contentItem(), QStringLiteral("PanelRow/") + floating.toString(QUuid::WithoutBraces)));
        QTest::keyClick(selector.data(), Qt::Key_Down);
        QTest::keyClick(selector.data(), Qt::Key_Return);
        QTRY_VERIFY(!selector);
        QTRY_COMPARE(manager.context().window, floating);
    }
    void menuBarUsesLateCommandsAndItsOwnWindow_data()
    {
        QTest::addColumn<bool>("floating");
        QTest::newRow("workbench") << false;
        QTest::newRow("floating") << true;
    }
    void menuBarUsesLateCommandsAndItsOwnWindow()
    {
        QFETCH(bool, floating);
        InterfaceAppearanceController appearance;
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto owner = manager.createWindow();
        manager.addTab(owner, {QStringLiteral("Owner")});
        const auto window = floating ? manager.createWindow(WindowKind::Floating, {}, owner) : owner;
        const auto tab = floating ? manager.addTab(window, {QStringLiteral("Floating")}) : manager.context(owner).tab;
        QTRY_VERIFY(workspace.window(window));
        auto* view = workspace.window(window);
        view->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(view));
        workspace.content(manager.model().tab(tab)->activePane)->forceActiveFocus();
        auto* bar = namedItem(view->contentItem(), QStringLiteral("MenuStrip")); QVERIFY(bar);
        bool enabled = false;
        CommandContext invoked;
        Command command;
        command.id = "test.menuBar"; command.title = "Run late command"; command.menuPath = {"tools"};
        command.enabled = [&](const CommandContext&) { return enabled; };
        command.execute = [&](const CommandContext& context) { invoked = context; return true; };
        QVERIFY(manager.commands().add(std::move(command)));
        auto changed = appearance.appearance(); changed.fontSize += 2; changed.zoomLevel = 0.5;
        QVERIFY(appearance.setAppearance(changed));
        QTest::keyClick(view, Qt::Key_T, Qt::AltModifier);
        QTRY_VERIFY(popup(view));
        auto* menu = popup(view); QVERIFY(QTest::qWaitForWindowExposed(menu));
        QTRY_VERIFY(namedItem(menu->contentItem(), QStringLiteral("test.menuBar")));
        QTest::keyClick(menu, Qt::Key_Left);
        QTRY_VERIFY(popup(view) && namedItem(popup(view)->contentItem(), QStringLiteral("window.select")));
        QTest::keyClick(popup(view), Qt::Key_Right);
        QTRY_VERIFY(popup(view) && namedItem(popup(view)->contentItem(), QStringLiteral("test.menuBar")));
        menu = popup(view); QVERIFY(QTest::qWaitForWindowExposed(menu));
        auto* action = namedItem(menu->contentItem(), QStringLiteral("test.menuBar"));
        QVERIFY(!action->isEnabled());
        enabled = true;
        QVERIFY(manager.commands().setShortcut("test.menuBar", QKeySequence("Ctrl+Alt+M")));
        QTRY_VERIFY(action->isEnabled());
        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, action->mapToScene(action->boundingRect().center()).toPoint());
        QTRY_COMPARE(invoked.window, window); QCOMPARE(invoked.tab, tab);
        QTRY_VERIFY(!popup(view));
        QTRY_VERIFY(workspace.content(manager.model().tab(tab)->activePane)->hasActiveFocus());
        const auto point = bar->mapToScene(bar->boundingRect().center()).toPoint();
        QContextMenuEvent context(QContextMenuEvent::Mouse, point, view->mapToGlobal(point));
        QCoreApplication::sendEvent(view, &context);
        QTRY_VERIFY(popup(view));
        menu = popup(view); QVERIFY(QTest::qWaitForWindowExposed(menu));
        action = namedItem(menu->contentItem(), QStringLiteral("presentation.toggleNumbers")); QVERIFY(action);
        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, action->mapToScene(action->boundingRect().center()).toPoint());
        QTRY_VERIFY(manager.tabAppearance().showNumbers);
    }

    void menuBarWindowListTracksWindowsCreatedAfterItsBar()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto first = manager.createWindow(); manager.addTab(first, {QStringLiteral("First")});
        QTRY_VERIFY(workspace.window(first));
        const auto retired = manager.createWindow(); manager.addTab(retired, {QStringLiteral("Retired")});
        const auto target = manager.createWindow(); manager.addTab(target, {QStringLiteral("Target")});
        QTRY_VERIFY(workspace.window(target));
        auto* view = workspace.window(first);
        view->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(view));
        auto* bar = namedItem(view->contentItem(), QStringLiteral("MenuStrip")); QVERIFY(bar);
        auto* button = namedItem(bar, QStringLiteral("window")); QVERIFY(button);
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, button->mapToScene(button->boundingRect().center()).toPoint());
        QTRY_VERIFY(popup(view));
        auto* menu = popup(view); QVERIFY(QTest::qWaitForWindowExposed(menu));
        QTRY_VERIFY(namedItem(menu->contentItem(), retired.toString(QUuid::WithoutBraces)));
        const QPointer<QQuickItem> removed(namedItem(menu->contentItem(), retired.toString(QUuid::WithoutBraces)));
        QVERIFY(manager.requestCloseWindow(retired));
        QTRY_VERIFY(!removed);
        QVERIFY(menu->isVisible());
        auto* activate = namedItem(menu->contentItem(), target.toString(QUuid::WithoutBraces)); QVERIFY(activate);
        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, activate->mapToScene(activate->boundingRect().center()).toPoint());
        QTRY_COMPARE(manager.context().window, target);
        QTRY_VERIFY(workspace.window(target)->isActive());
    }

    void menuBarReleasesRemovedRootContributions()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto window = manager.createWindow(); manager.addTab(window, {QStringLiteral("Content")});
        QTRY_VERIFY(workspace.window(window));
        auto* bar = namedItem(workspace.window(window)->contentItem(), QStringLiteral("MenuStrip")); QVERIFY(bar);
        Command command;
        command.id = "test.temporary"; command.title = "Temporary action"; command.menuPath = {"temporary"};
        command.execute = [](const CommandContext&) { return true; };
        QVERIFY(manager.commands().add(std::move(command)));
        QTRY_VERIFY(namedItem(bar, QStringLiteral("extension.temporary")));
        const QPointer<QQuickItem> row(namedItem(bar, QStringLiteral("extension.temporary")));
        const QPointer<QObject> menu(row->property("menu").value<QObject*>()); QVERIFY(menu);
        QVERIFY(manager.commands().remove("test.temporary"));
        QTRY_VERIFY(!row); QTRY_VERIFY(!menu);
        QVERIFY(manager.model().window(window));
    }

    void foldedMenusKeepMouseMnemonicAndOwnerLifetime_data()
    {
        QTest::addColumn<int>("mode");
        QTest::newRow("extension") << 0;
        QTest::newRow("hidden-mnemonic") << 1;
        QTest::newRow("owner-closes") << 2;
        QTest::newRow("arrows") << 3;
    }
    void foldedMenusKeepMouseMnemonicAndOwnerLifetime()
    {
        QFETCH(int, mode);
        WindowManager manager(WindowManager::Presentation::External);
        auto workspace = std::make_unique<QuickWorkspace>(manager);
        workspace->setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        TabId invoked;
        Command command;
        command.id = "test.folded"; command.title = "Folded action"; command.menuPath = {"tools"};
        command.execute = [&](const CommandContext& context) { invoked = context.tab; return true; };
        QVERIFY(manager.commands().add(std::move(command)));
        const auto window = manager.createWindow();
        const auto tab = manager.addTab(window, {QStringLiteral("Content")});
        QTRY_VERIFY(workspace->window(window));
        auto* view = workspace->window(window);
        view->resize(300, 420); view->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(view));
        auto* content = workspace->content(manager.model().tab(tab)->activePane); content->forceActiveFocus();
        auto* bar = namedItem(view->contentItem(), QStringLiteral("MenuStrip")); QVERIFY(bar);
        auto* more = namedItem(bar, QStringLiteral("MenuOverflowButton")); QVERIFY(more);
        auto* tools = namedItem(bar, QStringLiteral("tools")); QVERIFY(tools);
        QTRY_VERIFY(more->isVisible() && !tools->isVisible());
        if (mode == 1 || mode == 3) QTest::keyClick(view, Qt::Key_T, Qt::AltModifier);
        else native_input::mouse(view, more->mapToScene(more->boundingRect().center()).toPoint(), true);
        QTRY_VERIFY(popup(view));
        QPointer<QQuickWindow> menu(popup(view)); QVERIFY(QTest::qWaitForWindowExposed(menu));
        if (mode == 2) { workspace.reset(); QTRY_VERIFY(!menu); return; }
        if (mode == 0) {
            auto* branch = namedItem(menu->contentItem(), QStringLiteral("tools")); QVERIFY(branch);
            native_input::mouse(menu.data(), branch->mapToScene(branch->boundingRect().center()).toPoint());
            QTRY_VERIFY(branch->property("highlighted").toBool());
            QTest::keyClick(menu, Qt::Key_Right);
            QTRY_VERIFY(popup(menu));
            menu = popup(menu); QVERIFY(QTest::qWaitForWindowExposed(menu));
        }
        if (mode == 3) {
            QTest::keyClick(menu, Qt::Key_Left);
            QTRY_VERIFY(popup(view) && namedItem(popup(view)->contentItem(), QStringLiteral("window.select")));
            QTest::keyClick(popup(view), Qt::Key_Right);
            QTRY_VERIFY(popup(view) && namedItem(popup(view)->contentItem(), QStringLiteral("test.folded")));
            menu = popup(view); QVERIFY(QTest::qWaitForWindowExposed(menu));
        }
        auto* action = namedItem(menu->contentItem(), QStringLiteral("test.folded")); QVERIFY(action);
        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, action->mapToScene(action->boundingRect().center()).toPoint());
        QTRY_COMPARE(invoked, tab);
        QTRY_VERIFY(!popup(view));
        QTRY_VERIFY(content->hasActiveFocus());
        view->resize(1200, 700);
        QTRY_VERIFY(!more->isVisible() && tools->isVisible());
        QCOMPARE(namedItem(bar, QStringLiteral("tools")), tools);
        invoked = {};
        QTest::keyClick(view, Qt::Key_T, Qt::AltModifier);
        QTRY_VERIFY(popup(view));
        menu = popup(view); QVERIFY(QTest::qWaitForWindowExposed(menu));
        action = namedItem(menu->contentItem(), QStringLiteral("test.folded")); QVERIFY(action);
        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, action->mapToScene(action->boundingRect().center()).toPoint());
        QTRY_COMPARE(invoked, tab);
    }


};

QTEST_MAIN(QuickPopupMenuTest)
#include "quick_popup_menu_test.moc"
