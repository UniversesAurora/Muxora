// SPDX-License-Identifier: MIT
// Phase 10.4b: popup input and lifetime. Dimensions: disabled/hidden/separator
// rows, keyboard cascades, outside capture, screen edge, owner destruction.
#include "quick/popup_menu.hpp"
#include "quick/dock_surface_item.hpp"
#include "quick/group_item.hpp"
#include "quick/peek_item.hpp"
#include "quick/rail_item.hpp"
#include "quick/rail_button_item.hpp"
#include "quick/tab_bar_item.hpp"
#include "native_input.hpp"
#include <window_system/quick_workspace.hpp>
#include <window_system/window_manager.hpp>
#include <QContextMenuEvent>
#include <QQuickWindow>
#include <QScreen>
#include <QSignalSpy>
#include <QTest>

using namespace window_system;
using window_system::quick::PopupMenu;

namespace {
MenuItemDescription action(QString id, bool enabled = true)
{
    MenuItemDescription result;
    result.id = result.commandId = id;
    result.action.title = std::move(id);
    result.action.enabled = enabled;
    return result;
}
QQuickWindow* popup(QQuickWindow* parent)
{
    for (auto* window : QGuiApplication::topLevelWindows())
        if (window->type() == Qt::Popup && window->isVisible() && window->transientParent() == parent)
            return qobject_cast<QQuickWindow*>(window);
    return nullptr;
}
QQuickItem* namedItem(QQuickItem* parent, const QString& name)
{
    if (parent->objectName() == name) return parent;
    for (auto* child : parent->childItems()) if (auto* found = namedItem(child, name)) return found;
    return nullptr;
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

    void aCommandQueryCanReleaseTheWorkspace()
    {
        WindowManager manager(WindowManager::Presentation::External);
        auto workspace = std::make_unique<QuickWorkspace>(manager);
        workspace->setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto window = manager.createWindow();
        const auto tab = manager.addTab(window, {QStringLiteral("Target")});
        QTRY_VERIFY(workspace->window(window));
        auto* view = workspace->window(window);
        view->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(view));
        auto* dock = view->contentItem()->findChild<quick::DockSurfaceItem*>(); QVERIFY(dock);
        auto* bar = dock->group(manager.model().groupForTab(tab))->tabBar();
        Command command;
        command.id = "test.release"; command.title = "Release";
        command.contextMenus = {{ContextMenuTarget::Tab}};
        command.enabled = [&](const CommandContext&) { workspace.reset(); return true; };
        command.execute = [](const CommandContext&) { return true; };
        QVERIFY(manager.commands().add(std::move(command)));
        const auto point = bar->controller().tabRect(0).center();
        QContextMenuEvent event(QContextMenuEvent::Mouse, point, bar->mapToGlobal(point).toPoint());
        QCoreApplication::sendEvent(bar, &event);
        QVERIFY(!workspace);
        QVERIFY(manager.model().tab(tab));
    }
};

QTEST_MAIN(QuickPopupMenuTest)
#include "quick_popup_menu_test.moc"
