// SPDX-License-Identifier: MIT
#include "commands/command_menus.hpp"
#include "chrome/command_strip.hpp"
#include "chrome/workspace_window.hpp"
#include <window_system/window_manager.hpp>
#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QHash>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QSet>
#include <QStyle>
#include <QTabBar>
#include <QTest>
#include <QTimer>
#include <memory>

using namespace window_system;

namespace {
QMenuBar* menuBar(WindowManager& manager, WindowId window)
{
    return manager.windowWidget(window)->findChild<QMenuBar*>("MenuStrip");
}
QMenu* menuAt(QMenuBar* bar, const QStringList& path)
{
    for (auto* menu : bar->findChildren<QMenu*>())
        if (menu->property("commandMenuPath").toStringList() == path) return menu;
    return nullptr;
}
QStringList ids(QMenu* menu, bool visibleOnly = true)
{
    QStringList result;
    for (auto* action : menu->actions()) if ((!visibleOnly || action->isVisible()) && !action->data().toString().isEmpty())
        result.append(action->data().toString());
    return result;
}
void walk(QMenu* menu, QHash<QString, int>& occurrences)
{
    for (auto* action : menu->actions()) {
        if (auto* child = action->menu()) walk(child, occurrences);
        else if (!action->data().toString().isEmpty()) ++occurrences[action->data().toString()];
    }
}
void verifySeparators(QMenu* menu)
{
    bool previousSeparator = true;
    for (auto* action : menu->actions()) if (action->isVisible()) {
        QVERIFY(!action->isSeparator() || !previousSeparator);
        previousSeparator = action->isSeparator();
        if (auto* child = action->menu()) verifySeparators(child);
    }
    QVERIFY(!previousSeparator || menu->actions().isEmpty() || !menu->menuAction()->isVisible());
}
void contributeApplicationCommands(WindowManager& manager)
{
    for (const auto& command : QVector<Command>{
        {"application.exit", "退出", "文件", {}, {}, [](const CommandContext&) { return true; }, {"文件"}, 100},
        {"help.about", "关于宿主", "帮助", {}, {}, [](const CommandContext&) { return true; }, {"帮助"}, 10},
        {"layout.reset", "恢复初始工作区布局", "视图", {}, {}, [](const CommandContext&) { return true; }, {"布局"}, 1000},
        {"layout.save", "保存工作区布局", "文件", QKeySequence("Ctrl+S"), {}, [](const CommandContext&) { return true; }, {"文件"}, 40},
        {"host.newTab", "新建工作区标签", "文件", {}, {}, [](const CommandContext&) { return true; }, {"文件"}, 10}})
        QVERIFY(manager.commands().add(command));
}
} // namespace

class MenuStructureTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); }
    void applicationTreeUsesSemanticGroupsAndCoversEveryCommand();
    void directionalFamiliesHaveAConsistentOrder();
    void lateContributionsRespectMetadataAndPreserveExistingMenus();
    void unknownCategoriesStayVisibleAndSortIndependentlyOfRegistration();
    void menuExecutionAndShortcutsUseTheRegistryExactlyOnce();
    void stateRefreshKeepsChecksVisibilityAndIndependentWindows();
    void tabAndPaneContextMenusRetainScopeAndUsefulGroups();
    void nestedContextCommandsOperateOnTheClickedContent_data();
    void nestedContextCommandsOperateOnTheClickedContent();
    void lateTopLevelMenusJoinToolbarCustomizationWithoutResettingChoices();
    void windowCloseAndKindChangesReleaseTheirMenuObjects();
    void externalMenuHostCanOutliveItsManager();
};

void MenuStructureTest::applicationTreeUsesSemanticGroupsAndCoversEveryCommand()
{
    WindowManager manager;
    const auto builtins = manager.commands().commands();
    contributeApplicationCommands(manager);
    const auto window = manager.createWindow(); manager.addTab(window, new QLabel("Content"), {"One", "label", {}});
    auto* bar = menuBar(manager, window); QVERIFY(bar);
    QStringList roots;
    for (auto* action : bar->actions()) if (action->isVisible()) roots.append(action->objectName());
    QCOMPARE(roots, (QStringList{"文件", "视图", "布局", "窗口", "工具", "帮助"}));
    QCOMPARE(ids(menuAt(bar, {"文件"})), (QStringList{"host.newTab", "window.new", "layout.save", "workspace.closeTab", "window.close", "application.exit"}));
    QCOMPARE(ids(menuAt(bar, {"工具"})), (QStringList{"window.settings", "preferences.openKeyboardShortcuts"}));
    QCOMPARE(manager.commands().find("window.settings")->title, QStringLiteral("设置"));
    const auto layoutActions = menuAt(bar, {"布局"})->actions();
    QCOMPARE(layoutActions.last()->data().toString(), QStringLiteral("layout.reset"));
    QHash<QString, int> occurrences;
    for (auto* action : bar->actions()) if (action->menu()) { walk(action->menu(), occurrences); verifySeparators(action->menu()); }
    const auto declarations = declaredMenuCommands(CommandMenuSurface::Application);
    QCOMPARE(QSet<QString>(declarations.cbegin(), declarations.cend()).size(), declarations.size());
    for (const auto& definition : builtins) {
        const auto reason = applicationMenuExclusion(definition.id);
        QVERIFY2(declarations.contains(definition.id) || !reason.isEmpty(), qPrintable("Unclassified module command: " + definition.id));
        QCOMPARE(occurrences.value(definition.id), reason.isEmpty() ? 1 : 0);
    }
    for (auto count : occurrences) QCOMPARE(count, 1);
    for (const auto& definition : manager.commands().commands())
        if (applicationMenuExclusion(definition.id).isEmpty()) QCOMPARE(occurrences.value(definition.id), 1);
}

void MenuStructureTest::directionalFamiliesHaveAConsistentOrder()
{
    WindowManager manager; const auto window = manager.createWindow();
    manager.addTab(window, new QLabel("Content"), {"One", "label", {}});
    auto* bar = menuBar(manager, window);
    const QList<QPair<QStringList, QString>> families{
        {{"视图", "聚焦标签组"}, "workspace.focusGroup."}, {{"视图", "聚焦窗格"}, "pane.focus."},
        {{"布局", "标签组", "新建标签组"}, "workspace.splitGroup."}, {{"布局", "窗格", "拆分"}, "pane.split."},
        {{"布局", "窗格", "交换位置"}, "pane.swap."}, {{"布局", "移动标签", "到标签组"}, "workspace.moveTabToGroup."},
        {{"布局", "移动窗格", "到标签组"}, "pane.moveToGroup."}};
    for (const auto& [path, prefix] : families) {
        auto* menu = menuAt(bar, path); QVERIFY2(menu, qPrintable(path.join('/')));
        QCOMPARE(ids(menu, false), (QStringList{prefix + "left", prefix + "right", prefix + "up", prefix + "down"}));
    }
    QCOMPARE(ids(menuAt(bar, {"窗口", "切换窗口", "全部窗口"})),
        (QStringList{"window.focusPrevious.all", "window.focusNext.all"}));
}

void MenuStructureTest::lateContributionsRespectMetadataAndPreserveExistingMenus()
{
    WindowManager manager; const auto window = manager.createWindow(); auto* bar = menuBar(manager, window);
    QPointer<QMenu> file = menuAt(bar, {"文件"}); QVERIFY(file);
    auto* close = file->findChild<QAction*>("window.close"); QVERIFY(close);
    QVERIFY(manager.commands().add({"extension.last", "Late last", "文件", {}, {}, [](const CommandContext&) { return true; }, {"视图", "诊断"}, 30}));
    QVERIFY(manager.commands().add({"extension.first", "Late first", "工具", {}, {}, [](const CommandContext&) { return true; }, {"视图", "诊断"}, 10}));
    QMetaObject::invokeMethod(file, "aboutToShow");
    auto* diagnostics = menuAt(bar, {"视图", "诊断"}); QVERIFY(diagnostics);
    QCOMPARE(ids(diagnostics), (QStringList{"extension.first", "extension.last"}));
    QCOMPARE(menuAt(bar, {"文件"}), file.data()); QCOMPARE(file->findChild<QAction*>("window.close"), close);
    QCOMPARE(diagnostics->findChildren<QAction*>("extension.first").size(), 1);
    QMetaObject::invokeMethod(file, "aboutToShow"); QCOMPARE(ids(diagnostics).size(), 2);
    QVERIFY(!menuAt(bar, {"文件", "扩展"}));
    QVERIFY(diagnostics->style()->styleHint(QStyle::SH_Menu_Scrollable, nullptr, diagnostics));
}

void MenuStructureTest::unknownCategoriesStayVisibleAndSortIndependentlyOfRegistration()
{
    WindowManager manager; const auto window = manager.createWindow(); auto* bar = menuBar(manager, window);
    QVERIFY(manager.commands().add({"z.custom", "Zulu", "远程管理", {}, {}, [](const CommandContext&) { return true; }}));
    QVERIFY(manager.commands().add({"a.custom", "Alpha", "远程管理", {}, {}, [](const CommandContext&) { return true; }}));
    QVERIFY(manager.commands().add({"host.other", "Other view action", "视图", {}, {}, [](const CommandContext&) { return true; }}));
    QMetaObject::invokeMethod(menuAt(bar, {"文件"}), "aboutToShow");
    auto* custom = menuAt(bar, {"扩展", "远程管理"}); QVERIFY(custom);
    QCOMPARE(ids(custom), (QStringList{"a.custom", "z.custom"}));
    QCOMPARE(ids(menuAt(bar, {"视图", "扩展"})), (QStringList{"host.other"}));
    QVERIFY(custom->menuAction()->isVisible()); QVERIFY(custom->style()->styleHint(QStyle::SH_Menu_Scrollable, nullptr, custom));
    for (auto* action : bar->actions()) if (action->menu()) verifySeparators(action->menu());
}

void MenuStructureTest::menuExecutionAndShortcutsUseTheRegistryExactlyOnce()
{
    WindowManager manager; bool allowed = true; int executions = 0; WindowId executedWindow;
    QVERIFY(manager.commands().add({"host.action", "Host action", "错误旧类别", QKeySequence("Ctrl+Alt+F8"),
        [&](const CommandContext&) { return allowed; }, [&](const CommandContext& c) { executedWindow = c.window; ++executions; return true; }, {"文件"}, 40}));
    const auto window = manager.createWindow(); auto* content = new QLabel("Content");
    manager.addTab(window, content, {"One", "label", {}}); manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    auto* file = menuAt(menuBar(manager, window), {"文件"}); auto* action = file->findChild<QAction*>("host.action"); QVERIFY(action);
    QVERIFY(action->shortcut().isEmpty()); action->trigger(); QTRY_COMPARE(executions, 1); QCOMPARE(executedWindow, window);
    QTest::keyClick(content, Qt::Key_F8, Qt::ControlModifier | Qt::AltModifier); QCOMPARE(executions, 2);
    QVERIFY(manager.commands().setShortcut("host.action", QKeySequence("Ctrl+Alt+F9")));
    QMetaObject::invokeMethod(file, "aboutToShow");
    QVERIFY(action->text().endsWith(QKeySequence("Ctrl+Alt+F9").toString(QKeySequence::NativeText))); QVERIFY(action->shortcut().isEmpty());
    QTest::keyClick(content, Qt::Key_F9, Qt::ControlModifier | Qt::AltModifier); QCOMPARE(executions, 3);
    allowed = false; QMetaObject::invokeMethod(file, "aboutToShow"); QVERIFY(!action->isEnabled()); QVERIFY(action->isVisible());
    action->trigger(); QCoreApplication::processEvents(); QCOMPARE(executions, 3);
}

void MenuStructureTest::stateRefreshKeepsChecksVisibilityAndIndependentWindows()
{
    WindowManager manager; const auto first = manager.createWindow(); const auto second = manager.createWindow();
    const auto tab = manager.addTab(first, new QLabel("Content"), {"One", "label", {}});
    auto* bar = menuBar(manager, first); auto* toggle = bar->findChild<QAction*>("window.tabBars.toggle"); QVERIFY(toggle);
    QVERIFY(toggle->isCheckable()); QVERIFY(toggle->isChecked());
    toggle->trigger(); QTRY_VERIFY(!manager.tabBarsVisible(first)); QVERIFY(!toggle->isChecked()); QVERIFY(manager.tabBarsVisible(second));
    QVERIFY(!bar->findChild<QAction*>("window.promote")->isVisible());
    QVERIFY(!bar->findChild<QAction*>("workspace.pinTab")->isVisible());
    QVERIFY(manager.moveTabToEdge(tab, Edge::Left));
    QMetaObject::invokeMethod(menuAt(bar, {"布局"}), "aboutToShow");
    QVERIFY(bar->findChild<QAction*>("workspace.pinTab")->isVisible());
    QVERIFY(!bar->findChild<QAction*>("workspace.unpinTab")->isVisible());
    QVERIFY(manager.pinTab(tab, true)); QMetaObject::invokeMethod(menuAt(bar, {"布局"}), "aboutToShow");
    QVERIFY(!bar->findChild<QAction*>("workspace.pinTab")->isVisible()); QVERIFY(bar->findChild<QAction*>("workspace.unpinTab")->isVisible());
}

void MenuStructureTest::tabAndPaneContextMenusRetainScopeAndUsefulGroups()
{
    WindowManager manager; const auto window = manager.createWindow();
    const auto tab = manager.addTab(window, new QLabel("Content"), {"One", "label", {}});
    const auto pane = manager.model().tab(tab)->activePane; const auto captured = manager.context(window);
    QMenu tabMenu; populateContextMenu(&tabMenu, CommandMenuSurface::Tab, &manager, captured);
    QMenu paneMenu; auto* copy = paneMenu.addAction("Copy");
    populateContextMenu(&paneMenu, CommandMenuSurface::Pane, &manager, captured);
    QVERIFY(paneMenu.actions().contains(copy)); QCOMPARE(paneMenu.actions().first(), copy);
    auto* closeRange = tabMenu.findChild<QMenu*>("tab.closeRange"); QVERIFY(closeRange);
    QCOMPARE(ids(closeRange), (QStringList{"workspace.closeTabs.left", "workspace.closeTabs.right", "workspace.closeTabs.others", "workspace.closeTabs.all"}));
    QVERIFY(tabMenu.findChild<QMenu*>("tab.move")); QVERIFY(paneMenu.findChild<QMenu*>("pane.move"));
    QVERIFY(!tabMenu.findChild<QAction*>("pane.extractFloating")); QVERIFY(!paneMenu.findChild<QAction*>("workspace.closeTab"));
    auto* sharing = paneMenu.findChild<QAction*>("tab.topBar.setSharing"); QVERIFY(sharing); QVERIFY(sharing->isChecked());
    QVERIFY(manager.commands().execute("tab.topBar.setSharing", captured));
    refreshCommandAction(&manager, sharing, captured); QVERIFY(!sharing->isChecked()); QCOMPARE(manager.model().tab(tab)->activePane, pane);
    for (auto* menu : {&tabMenu, &paneMenu}) {
        QHash<QString, int> occurrences; walk(menu, occurrences); for (auto count : occurrences) QCOMPARE(count, 1); verifySeparators(menu);
    }
}

void MenuStructureTest::nestedContextCommandsOperateOnTheClickedContent_data()
{
    QTest::addColumn<bool>("paneMenu");
    QTest::newRow("tab-to-next-window") << false;
    QTest::newRow("pane-to-next-tab") << true;
}

void MenuStructureTest::nestedContextCommandsOperateOnTheClickedContent()
{
    QFETCH(bool, paneMenu);
    WindowManager manager; const auto sourceWindow = manager.createWindow();
    auto* clickedContent = new QLabel("Clicked content");
    const auto clickedTab = manager.addTab(sourceWindow, clickedContent, {"Clicked", "label", {}});
    const auto clickedPane = manager.model().tab(clickedTab)->activePane;
    PaneId retained;
    if (paneMenu) retained = manager.splitPane(clickedTab, clickedPane, new QLabel("Retained"), Placement::Right);
    const auto otherTab = manager.addTab(sourceWindow, new QLabel("Other content"), {"Other", "label", {}});
    const auto targetWindow = paneMenu ? sourceWindow : manager.createWindow();
    manager.showWindow(sourceWindow); QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(sourceWindow)));
    QVERIFY(manager.activateTab(otherTab));
    QCOMPARE(manager.context(sourceWindow).tab, otherTab);
    const QStringList submenuIds = paneMenu ? QStringList{"pane.move", "pane.move.tab"}
        : QStringList{"tab.move", "tab.move.window"};
    const QString selectedId = paneMenu ? "pane.moveToTab.next" : "workspace.moveTabToWindow.next";
    QPointer<QMenu> rootPopup; int stage = 0; bool chosen = false, timedOut = false;
    QTimer navigation, watchdog; navigation.setInterval(20); watchdog.setSingleShot(true);
    connect(&navigation, &QTimer::timeout, &manager, [&] {
        auto* popup = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!popup) return;
        if (!rootPopup) rootPopup = popup;
        if (stage > 0 && popup->objectName() != submenuIds[stage - 1]) return;
        if (stage < submenuIds.size()) {
            auto* child = popup->findChild<QMenu*>(submenuIds[stage], Qt::FindDirectChildrenOnly);
            if (!child || !child->menuAction()->isVisible() || !child->menuAction()->isEnabled()) return;
            popup->setActiveAction(child->menuAction()); ++stage;
            QTest::keyClick(popup, Qt::Key_Right);
        } else {
            for (auto* action : popup->actions()) if (action->data().toString() == selectedId) {
                if (!action->isVisible() || !action->isEnabled()) return;
                chosen = true; navigation.stop(); popup->setActiveAction(action);
                QTest::keyClick(popup, Qt::Key_Return); break;
            }
        }
    });
    connect(&watchdog, &QTimer::timeout, &manager, [&] {
        timedOut = true; navigation.stop(); if (rootPopup) rootPopup->close();
    });
    navigation.start(); watchdog.start(2000);
    QWidget* receiver = clickedContent; QPoint point = clickedContent->rect().center();
    if (!paneMenu) {
        auto* tabs = manager.windowWidget(sourceWindow)->findChild<QTabBar*>("TabBar"); QVERIFY(tabs);
        int clickedIndex = -1;
        for (int index = 0; index < tabs->count(); ++index) if (tabs->tabData(index).toUuid() == clickedTab) clickedIndex = index;
        QVERIFY(clickedIndex >= 0); receiver = tabs; point = tabs->tabRect(clickedIndex).center();
    }
    QContextMenuEvent event(QContextMenuEvent::Keyboard, point, receiver->mapToGlobal(point));
    QApplication::sendEvent(receiver, &event); navigation.stop(); watchdog.stop();
    QVERIFY(!timedOut); QVERIFY(chosen);
    if (paneMenu) {
        QTRY_COMPARE(manager.model().tabForPane(clickedPane), otherTab);
        QCOMPARE(manager.model().panes(clickedTab), QVector<PaneId>{retained});
        QCOMPARE(manager.model().tab(otherTab)->activePane, clickedPane);
    } else {
        QTRY_COMPARE(manager.model().group(manager.model().groupForTab(clickedTab))->windowId, targetWindow);
        QCOMPARE(manager.model().group(manager.model().groupForTab(otherTab))->windowId, sourceWindow);
    }
    QCOMPARE(manager.paneWidget(clickedPane), clickedContent); QVERIFY(manager.model().validate());
}

void MenuStructureTest::lateTopLevelMenusJoinToolbarCustomizationWithoutResettingChoices()
{
    WindowManager manager; const auto window = manager.createWindow();
    auto* view = static_cast<WorkspaceWindow*>(manager.windowWidget(window)); auto* bar = menuBar(manager, window);
    QVERIFY(view->strips()->setStripItemVisible("menu", "视图", false));
    QVERIFY(manager.commands().add({"extension.top", "Inspect", "宿主", {}, {}, [](const CommandContext&) { return true; }, {"审计"}, 10}));
    QMetaObject::invokeMethod(menuAt(bar, {"文件"}), "aboutToShow");
    auto* audit = menuAt(bar, {"审计"}); QVERIFY(audit); QVERIFY(audit->menuAction()->isVisible());
    QVERIFY(!view->strips()->isStripItemVisible("menu", "视图")); QVERIFY(!menuAt(bar, {"视图"})->menuAction()->isVisible());
    const auto saved = view->strips()->saveLayout(); QVERIFY(!saved.isEmpty());
    std::unique_ptr<QMenu> options(view->strips()->createOverflowMenu("menu", view));
    auto* customize = options->findChild<QMenu*>("StripCustomizeMenu"); QVERIFY(customize);
    bool listed = false;
    for (auto* action : customize->actions()) if (action->data().toString() == audit->menuAction()->objectName()) listed = true;
    QVERIFY(listed);
}

void MenuStructureTest::windowCloseAndKindChangesReleaseTheirMenuObjects()
{
    WindowManager manager; const auto root = manager.createWindow();
    manager.addTab(root, new QLabel("Root content"), {"Root", "label", {}});
    const auto closed = manager.createWindow();
    manager.addTab(closed, new QLabel("Closed content"), {"Closed", "label", {}});
    QPointer<QMenuBar> closedBar = menuBar(manager, closed);
    QPointer<QAction> closeAction = closedBar->findChild<QAction*>("window.close"); QVERIFY(closeAction);
    closeAction->trigger(); QTRY_VERIFY(!manager.model().window(closed));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(!closedBar); QVERIFY(!closeAction); QVERIFY(manager.model().window(root));

    auto* content = new QLabel("Retained across window kinds");
    const auto tab = manager.addTab(root, content, {"Movable", "label", {}});
    const auto pane = manager.model().tab(tab)->activePane;
    const auto floating = manager.detachTab(tab); QVERIFY(!floating.isNull());
    for (int cycle = 0; cycle < 3; ++cycle) {
        QVERIFY(manager.promoteWindow(floating));
        QPointer<QMenuBar> promotedBar = menuBar(manager, floating); QVERIFY(promotedBar);
        QPointer<QAction> promotedAction = promotedBar->findChild<QAction*>("window.tabBars.toggle"); QVERIFY(promotedAction);
        QVERIFY(manager.demoteWindow(floating, root));
        QVERIFY(!promotedBar); QVERIFY(!promotedAction); QVERIFY(!menuBar(manager, floating));
        QCOMPARE(manager.paneWidget(pane), content);
        QCOMPARE(manager.model().tab(tab)->activePane, pane);
        QCOMPARE(manager.model().window(floating)->kind, WindowKind::Floating);
    }
    QVERIFY(manager.promoteWindow(floating));
    QPointer<QMenuBar> lastBar = menuBar(manager, floating); QVERIFY(lastBar);
    QVERIFY(manager.requestCloseWindow(floating));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(!lastBar); QVERIFY(manager.model().window(root)); QVERIFY(manager.model().validate());
}

void MenuStructureTest::externalMenuHostCanOutliveItsManager()
{
    QMenuBar externalBar; auto manager = std::make_unique<WindowManager>();
    const auto window = manager->createWindow(); int executions = 0;
    QVERIFY(manager->commands().add({"宿主.检查", "检查宿主", "宿主", {}, {},
        [&](const CommandContext&) { ++executions; return true; }, {"工具", "检查"}, 10}));
    QPointer<ApplicationCommandMenus> presentation = new ApplicationCommandMenus(&externalBar, manager.get(), window);
    auto* tools = menuAt(&externalBar, {"工具", "检查"}); QVERIFY(tools);
    QPointer<QAction> action = tools->findChild<QAction*>("宿主.检查"); QVERIFY(action);
    QCOMPARE(action->data().toString(), QStringLiteral("宿主.检查")); QVERIFY(action->isEnabled());
    action->trigger(); manager.reset(); QCoreApplication::processEvents();
    QCOMPARE(executions, 0); QVERIFY(presentation); QVERIFY(action);
    presentation->refresh(); QVERIFY(!action->isEnabled()); QVERIFY(!action->isVisible());
    // The observation guard also protects an already retained action, and Qt
    // disconnects its context provider when the presentation owner is gone.
    action->setEnabled(true); action->trigger(); QCoreApplication::processEvents(); QCOMPARE(executions, 0);
    delete presentation.data(); QVERIFY(!presentation);
    action->trigger(); QCoreApplication::processEvents(); QCOMPARE(executions, 0);
}

QTEST_MAIN(MenuStructureTest)
#include "menu_structure_test.moc"
