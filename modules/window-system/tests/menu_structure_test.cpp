// SPDX-License-Identifier: MIT
#include "translation_checks.hpp"
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
#include <QSignalSpy>
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
        {"application.exit", "退出", "file", {}, {}, [](const CommandContext&) { return true; }, {"file"}, 100},
        {"help.about", "关于宿主", "help", {}, {}, [](const CommandContext&) { return true; }, {"help"}, 10},
        {"layout.reset", "恢复初始工作区布局", "view", {}, {}, [](const CommandContext&) { return true; }, {"layout"}, 1000},
        {"layout.save", "保存工作区布局", "file", QKeySequence("Ctrl+S"), {}, [](const CommandContext&) { return true; }, {"file"}, 40},
        {"host.newTab", "新建工作区标签", "file", {}, {}, [](const CommandContext&) { return true; }, {"file"}, 10}})
        QVERIFY(manager.commands().add(command));
}
} // namespace

class MenuStructureTest final : public QObject {
    Q_OBJECT
    translation_test::ChineseUiCatalog chineseUi_;
private slots:
    void initTestCase() { QVERIFY(chineseUi_.install()); qApp->setQuitOnLastWindowClosed(false); }
    void applicationTreeUsesSemanticGroupsAndCoversEveryCommand();
    void directionalFamiliesHaveAConsistentOrder();
    void layoutMenusIdentifyTheSourceObjectAndConversionTarget();
    void groupContextMenusDoNotPretendToOperateOnASingleTab();
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
    void floatingMenusAreLocalAndExcludeWholeApplicationOperations();
    void floatingMenuExecutionKeepsItsTabAndPaneContext();
    void floatingMenuContributionsStayIncrementalAndClassified();
    void windowListGroupsFamiliesAtTheEndAndRefreshesStableItems();
    void windowListActivatesAndRestoresTheChosenWindow();
    void tabAppearanceCommandsUseOneClassifiedCheckedAction_data();
    void tabAppearanceCommandsUseOneClassifiedCheckedAction();
    void wheelRowsRequireMultilineAndResetWithIt_data();
    void wheelRowsRequireMultilineAndResetWithIt();
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
    QCOMPARE(roots, (QStringList{"file", "view", "layout", "window", "tools", "help"}));
    QCOMPARE(ids(menuAt(bar, {"file"})), (QStringList{"host.newTab", "window.new", "layout.save", "workspace.closeTab", "window.close", "application.exit"}));
    QCOMPARE(ids(menuAt(bar, {"tools"})), (QStringList{"window.settings", "preferences.openKeyboardShortcuts"}));
    QCOMPARE(manager.commands().find("window.settings")->title, QStringLiteral("设置"));
    const auto layoutActions = menuAt(bar, {"layout"})->actions();
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
        {{"view", "focusGroup"}, "workspace.focusGroup."}, {{"view", "focusPane"}, "pane.focus."},
        {{"layout", "currentGroup", "createGroup"}, "workspace.splitGroup."},
        {{"layout", "currentGroup", "resizeGroup"}, "workspace.resizeGroup."},
        {{"layout", "currentGroup", "moveGroup", "placeBesideGroup"}, "group.move."},
        {{"layout", "currentPane", "splitPane"}, "pane.split."},
        {{"layout", "currentPane", "swapPane"}, "pane.swap."},
        {{"layout", "currentTab", "moveTab", "moveToAdjacentGroup"}, "workspace.moveTabToGroup."},
        {{"layout", "currentPane", "movePane", "mergeIntoAdjacentGroup"}, "pane.moveToGroup."}};
    for (const auto& [path, prefix] : families) {
        auto* menu = menuAt(bar, path); QVERIFY2(menu, qPrintable(path.join('/')));
        QCOMPARE(ids(menu, false), (QStringList{prefix + "left", prefix + "right", prefix + "up", prefix + "down"}));
    }
    QCOMPARE(ids(menuAt(bar, {"window", "switchWindow", "allWindows"})),
        (QStringList{"window.focusPrevious.all", "window.focusNext.all"}));
}

void MenuStructureTest::layoutMenusIdentifyTheSourceObjectAndConversionTarget()
{
    WindowManager manager; const auto window = manager.createWindow();
    manager.addTab(window, new QLabel("Content"), {"One", "label", {}});
    auto* bar = menuBar(manager, window); auto* layout = menuAt(bar, {"layout"}); QVERIFY(layout);
    QStringList objectNames, objectTitles;
    for (auto* action : layout->actions()) if (auto* child = action->menu()) {
        objectNames.append(child->objectName()); objectTitles.append(child->title());
    }
    QCOMPARE(objectNames, (QStringList{"layout.groups", "layout.tab", "layout.panes"}));
    QCOMPARE(objectTitles, (QStringList{"当前标签组", "当前标签", "当前窗格"}));
    QCOMPARE(ids(menuAt(bar, {"layout", "currentGroup", "convertGroup"}), false),
        (QStringList{"group.convertToTab", "group.moveIntoTab"}));
    QCOMPARE(ids(menuAt(bar, {"layout", "currentTab", "convertTab"}), false),
        (QStringList{"tab.convertToGroup", "tab.moveIntoTab"}));
    const auto paneMoves = ids(menuAt(bar, {"layout", "currentPane", "movePane"}), false);
    QCOMPARE(paneMoves, QStringList{"pane.extractToTab"});
    const auto withinTabBar = ids(menuAt(bar, {"layout", "currentTab", "reorderTab"}), false);
    QCOMPARE(withinTabBar, (QStringList{"workspace.reorderTab.previous", "workspace.reorderTab.next"}));
    QCOMPARE(ids(menuAt(bar, {"layout", "currentGroup", "sortGroup"}), false),
        (QStringList{"group.sort.alphabeticalAscending", "group.sort.alphabeticalDescending", "group.sort.openedAscending",
            "group.sort.openedDescending", "group.sort.visitedAscending", "group.sort.visitedDescending"}));
    const QList<QPair<QStringList, QStringList>> scoped{
        {{"layout", "currentGroup"}, {"group.convertToTab", "group.moveIntoTab", "group.moveToGroup", "group.swap", "group.merge", "group.close"}},
        {{"layout", "currentTab"}, {"tab.convertToGroup", "tab.moveIntoTab", "workspace.moveTabToWindow", "workspace.pinTab", "tab.peek.setSize"}},
        {{"layout", "currentPane"}, {"pane.extractToTab", "pane.moveToTab", "pane.moveToWindow", "pane.close"}}};
    QHash<QString, int> all;
    for (const auto& [path, commands] : scoped) {
        auto* source = menuAt(bar, path); QVERIFY(source);
        QHash<QString, int> occurrences; walk(source, occurrences);
        for (const auto& command : commands) {
            QVERIFY2(manager.commands().find(command), qPrintable(command));
            QCOMPARE(occurrences.value(command), 1); ++all[command];
        }
        verifySeparators(source);
    }
    for (auto count : all) QCOMPARE(count, 1);
    for (const auto* oldRoot : {"layout.moveGroup", "layout.swapGroup", "layout.mergeGroup", "layout.moveTab", "layout.movePane", "layout.edge"})
        QVERIFY(!layout->findChild<QMenu*>(QString::fromLatin1(oldRoot), Qt::FindDirectChildrenOnly));
}

void MenuStructureTest::groupContextMenusDoNotPretendToOperateOnASingleTab()
{
    WindowManager manager; const auto window = manager.createWindow();
    const auto first = manager.addTab(window, new QLabel("First"), {"First", "label", {}});
    manager.addTab(window, new QLabel("Second"), {"Second", "label", {}});
    QMenu menu; populateContextMenu(&menu, CommandMenuSurface::Group, &manager, manager.context(window));
    QHash<QString, int> occurrences; walk(&menu, occurrences);
    for (const auto* id : {"group.close", "group.convertToTab", "group.moveIntoTab", "group.moveToGroup", "group.swap", "group.merge",
                           "workspace.splitGroup.left", "workspace.splitGroup.right", "workspace.splitGroup.up", "workspace.splitGroup.down"})
        QCOMPARE(occurrences.value(QString::fromLatin1(id)), 1);
    for (const auto* id : {"workspace.closeTab", "tab.rename", "tab.color.set", "tab.convertToGroup", "workspace.pinTab", "pane.close"})
        QCOMPARE(occurrences.value(QString::fromLatin1(id)), 0);
    for (auto count : occurrences) QCOMPARE(count, 1);
    const auto declared = declaredMenuCommands(CommandMenuSurface::Group);
    QCOMPARE(QSet<QString>(declared.cbegin(), declared.cend()).size(), declared.size());
    verifySeparators(&menu); QVERIFY(manager.model().tab(first));
}

void MenuStructureTest::lateContributionsRespectMetadataAndPreserveExistingMenus()
{
    WindowManager manager; const auto window = manager.createWindow(); auto* bar = menuBar(manager, window);
    QPointer<QMenu> file = menuAt(bar, {"file"}); QVERIFY(file);
    auto* close = file->findChild<QAction*>("window.close"); QVERIFY(close);
    QVERIFY(manager.commands().add({"extension.last", "Late last", "file", {}, {}, [](const CommandContext&) { return true; }, {"view", "诊断"}, 30}));
    QVERIFY(manager.commands().add({"extension.first", "Late first", "tools", {}, {}, [](const CommandContext&) { return true; }, {"view", "诊断"}, 10}));
    QMetaObject::invokeMethod(file, "aboutToShow");
    auto* diagnostics = menuAt(bar, {"view", "诊断"}); QVERIFY(diagnostics);
    QCOMPARE(ids(diagnostics), (QStringList{"extension.first", "extension.last"}));
    QCOMPARE(menuAt(bar, {"file"}), file.data()); QCOMPARE(file->findChild<QAction*>("window.close"), close);
    QCOMPARE(diagnostics->findChildren<QAction*>("extension.first").size(), 1);
    QMetaObject::invokeMethod(file, "aboutToShow"); QCOMPARE(ids(diagnostics).size(), 2);
    QVERIFY(!menuAt(bar, {"file", "extensions"}));
    QVERIFY(diagnostics->style()->styleHint(QStyle::SH_Menu_Scrollable, nullptr, diagnostics));
}

void MenuStructureTest::unknownCategoriesStayVisibleAndSortIndependentlyOfRegistration()
{
    WindowManager manager; const auto window = manager.createWindow(); auto* bar = menuBar(manager, window);
    QVERIFY(manager.commands().add({"z.custom", "Zulu", "远程管理", {}, {}, [](const CommandContext&) { return true; }}));
    QVERIFY(manager.commands().add({"a.custom", "Alpha", "远程管理", {}, {}, [](const CommandContext&) { return true; }}));
    QVERIFY(manager.commands().add({"host.other", "Other view action", "view", {}, {}, [](const CommandContext&) { return true; }}));
    QMetaObject::invokeMethod(menuAt(bar, {"file"}), "aboutToShow");
    auto* custom = menuAt(bar, {"extensions", "远程管理"}); QVERIFY(custom);
    QCOMPARE(ids(custom), (QStringList{"a.custom", "z.custom"}));
    QCOMPARE(ids(menuAt(bar, {"view", "extensions"})), (QStringList{"host.other"}));
    QVERIFY(custom->menuAction()->isVisible()); QVERIFY(custom->style()->styleHint(QStyle::SH_Menu_Scrollable, nullptr, custom));
    for (auto* action : bar->actions()) if (action->menu()) verifySeparators(action->menu());
}

void MenuStructureTest::menuExecutionAndShortcutsUseTheRegistryExactlyOnce()
{
    WindowManager manager; bool allowed = true; int executions = 0; WindowId executedWindow;
    QVERIFY(manager.commands().add({"host.action", "Host action", "错误旧类别", QKeySequence("Ctrl+Alt+F8"),
        [&](const CommandContext&) { return allowed; }, [&](const CommandContext& c) { executedWindow = c.window; ++executions; return true; }, {"file"}, 40}));
    const auto window = manager.createWindow(); auto* content = new QLabel("Content");
    manager.addTab(window, content, {"One", "label", {}}); manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    auto* file = menuAt(menuBar(manager, window), {"file"}); auto* action = file->findChild<QAction*>("host.action"); QVERIFY(action);
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
    QMetaObject::invokeMethod(menuAt(bar, {"layout"}), "aboutToShow");
    QVERIFY(bar->findChild<QAction*>("workspace.pinTab")->isVisible());
    QVERIFY(!bar->findChild<QAction*>("workspace.unpinTab")->isVisible());
    QVERIFY(manager.pinTab(tab, true)); QMetaObject::invokeMethod(menuAt(bar, {"layout"}), "aboutToShow");
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
    // Classification is checked for every declared context operation, including
    // currently disabled items and the color action which owns a submenu.
    for (const auto surface : {CommandMenuSurface::Tab, CommandMenuSurface::Group, CommandMenuSurface::Pane, CommandMenuSurface::Window}) {
        QMenu current; populateContextMenu(&current, surface, &manager, captured);
        const auto declarations = declaredMenuCommands(surface);
        QCOMPARE(QSet<QString>(declarations.cbegin(), declarations.cend()).size(), declarations.size());
        for (const auto& id : declarations) {
            QVERIFY2(manager.commands().find(id), qPrintable(id));
            QCOMPARE(current.findChildren<QAction*>(id).size(), 1);
        }
        verifySeparators(&current);
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
    QVERIFY(view->strips()->setStripItemVisible("menu", "view", false));
    QVERIFY(manager.commands().add({"extension.top", "Inspect", "宿主", {}, {}, [](const CommandContext&) { return true; }, {"审计"}, 10}));
    QMetaObject::invokeMethod(menuAt(bar, {"file"}), "aboutToShow");
    auto* audit = menuAt(bar, {"审计"}); QVERIFY(audit); QVERIFY(audit->menuAction()->isVisible());
    QVERIFY(!view->strips()->isStripItemVisible("menu", "view")); QVERIFY(!menuAt(bar, {"view"})->menuAction()->isVisible());
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
        QVERIFY(!promotedBar); QVERIFY(!promotedAction);
        auto* lightweightBar = menuBar(manager, floating); QVERIFY(lightweightBar);
        QVERIFY(lightweightBar->findChild<QAction*>("window.tabBars.toggle"));
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
        [&](const CommandContext&) { ++executions; return true; }, {"tools", "检查"}, 10}));
    QPointer<ApplicationCommandMenus> presentation = new ApplicationCommandMenus(&externalBar, manager.get(), window);
    auto* tools = menuAt(&externalBar, {"tools", "检查"}); QVERIFY(tools);
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

void MenuStructureTest::floatingMenusAreLocalAndExcludeWholeApplicationOperations()
{
    WindowManager manager; contributeApplicationCommands(manager);
    const auto owner = manager.createWindow();
    manager.addTab(owner, new QLabel("Owner content"), {"owner", {}, {}});
    const auto floating = manager.createWindow(WindowKind::Floating, {}, owner);
    manager.addTab(floating, new QLabel("Floating content"), {"floating", {}, {}});
    auto* view = static_cast<WorkspaceWindow*>(manager.windowWidget(floating));
    auto* bar = menuBar(manager, floating); QVERIFY(bar);
    QCOMPARE(ids(menuAt(bar, {"file"})), (QStringList{"host.newTab", "workspace.closeTab", "window.close"}));
    QCOMPARE(ids(menuAt(bar, {"tools"})), (QStringList{"window.settings", "preferences.openKeyboardShortcuts"}));
    const QStringList excluded{"application.exit", "layout.save", "layout.reset", "window.new", "window.statusBar.toggle", "window.demote"};
    for (const auto& id : excluded) {
        QVERIFY2(!floatingMenuExclusion(id).isEmpty(), qPrintable(id));
        QVERIFY2(!bar->findChild<QAction*>(id), qPrintable(id));
    }
    QHash<QString, int> occurrences;
    for (auto* action : bar->actions()) if (action->menu()) {
        walk(action->menu(), occurrences); verifySeparators(action->menu());
    }
    for (const auto& definition : manager.commands().commands())
        QCOMPARE(occurrences.value(definition.id), floatingMenuExclusion(definition.id).isEmpty() ? 1 : 0);
    QCOMPARE(view->strips()->stripOrder(0), (QStringList{"floating-menu", "title", "caption-controls"}));
    QCOMPARE(view->strips()->rowCount(CommandStripHost::Area::Top), 1);
    QVERIFY(!view->strips()->stripWidget("workspace")); QVERIFY(!view->findChild<QWidget*>("StatusBar"));
    QCOMPARE(view->strips()->stripWidget("floating-menu")->parentWidget(), view->strips()->stripWidget("title")->parentWidget());
}

void MenuStructureTest::floatingMenuExecutionKeepsItsTabAndPaneContext()
{
    WindowManager manager; const auto owner = manager.createWindow();
    const auto ownerTab = manager.addTab(owner, new QLabel("Owner content"), {"owner", {}, {}});
    const auto floating = manager.createWindow(WindowKind::Floating, {}, owner);
    const auto localTab = manager.addTab(floating, new QLabel("Local first pane"), {"local", {}, {}});
    const auto firstPane = manager.model().tab(localTab)->activePane;
    const auto localPane = manager.splitPane(localTab, firstPane, new QLabel("Local second pane"), Placement::Right);
    QVERIFY(manager.focusPane(localTab, localPane));
    int executions = 0; CommandContext captured;
    QVERIFY(manager.commands().add({"host.inspectLocal", "检查当前窗格", "tools", {}, {},
        [&](const CommandContext& context) { captured = context; ++executions; return true; }, {"tools", "检查"}, 10}));
    manager.showWindow(owner); manager.showWindow(floating);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(floating)));
    QVERIFY(manager.focusPane(localTab, localPane));
    QVERIFY(manager.activateTab(ownerTab));
    QCOMPARE(manager.context(owner).tab, ownerTab);
    auto* bar = menuBar(manager, floating); QVERIFY(bar);
    QMetaObject::invokeMethod(menuAt(bar, {"tools"}), "aboutToShow");
    auto* action = bar->findChild<QAction*>("host.inspectLocal"); QVERIFY(action);
    QVERIFY(action->shortcut().isEmpty()); action->trigger();
    QTRY_COMPARE(executions, 1);
    QCOMPARE(captured.window, floating); QCOMPARE(captured.group, manager.model().groupForTab(localTab));
    QCOMPARE(captured.tab, localTab); QCOMPARE(captured.pane, localPane);
    QCOMPARE(manager.context(owner).tab, ownerTab);
    auto* sharing = bar->findChild<QAction*>("pane.split.left"); QVERIFY(sharing);
    QVERIFY(sharing->isEnabled());
    auto* localToggle = bar->findChild<QAction*>("window.tabBars.toggle"); QVERIFY(localToggle);
    localToggle->trigger(); QTRY_VERIFY(!manager.tabBarsVisible(floating)); QVERIFY(manager.tabBarsVisible(owner));
}

void MenuStructureTest::floatingMenuContributionsStayIncrementalAndClassified()
{
    WindowManager manager; const auto owner = manager.createWindow();
    const auto floating = manager.createWindow(WindowKind::Floating, {}, owner);
    manager.addTab(floating, new QLabel("Local"), {"local", {}, {}});
    auto* bar = menuBar(manager, floating); QVERIFY(bar);
    QPointer<QMenu> file = menuAt(bar, {"file"}); QVERIFY(file);
    QPointer<QAction> close = file->findChild<QAction*>("window.close"); QVERIFY(close);
    contributeApplicationCommands(manager);
    QVERIFY(manager.commands().add({"host.localLate", "本地工具", "tools", {}, {}, [](const CommandContext&) { return true; }, {"tools", "检查"}, 20}));
    QMetaObject::invokeMethod(file, "aboutToShow");
    QCOMPARE(menuAt(bar, {"file"}), file.data()); QCOMPARE(file->findChild<QAction*>("window.close"), close.data());
    QVERIFY(!bar->findChild<QAction*>("application.exit")); QVERIFY(!bar->findChild<QAction*>("layout.reset"));
    QCOMPARE(ids(menuAt(bar, {"tools", "检查"})), (QStringList{"host.localLate"}));
    QCOMPARE(ids(file), (QStringList{"host.newTab", "workspace.closeTab", "window.close"}));
    QMetaObject::invokeMethod(file, "aboutToShow"); QCOMPARE(bar->findChildren<QAction*>("host.localLate").size(), 1);
}

void MenuStructureTest::windowListGroupsFamiliesAtTheEndAndRefreshesStableItems()
{
    WindowManager manager; WindowOptions firstOptions; firstOptions.title = "First family";
    WindowOptions secondOptions; secondOptions.title = "Second family";
    const auto first = manager.createWindow(WindowKind::Workbench, firstOptions);
    const auto second = manager.createWindow(WindowKind::Workbench, secondOptions);
    manager.addTab(first, new QLabel("First"), {"First", {}, {}});
    manager.addTab(second, new QLabel("Second"), {"Second", {}, {}});
    const auto firstChild = manager.createWindow(WindowKind::Floating, {}, first);
    const auto secondChild = manager.createWindow(WindowKind::Floating, {}, second);
    const auto anotherFirstChild = manager.createWindow(WindowKind::Floating, {}, first);
    manager.addTab(firstChild, new QLabel("Child one"), {"Child one", {}, {}});
    manager.addTab(secondChild, new QLabel("Child two"), {"Child two", {}, {}});
    manager.addTab(anotherFirstChild, new QLabel("Child three"), {"Child three", {}, {}});
    auto* menu = menuAt(menuBar(manager, first), {"window"}); QVERIFY(menu);
    QMetaObject::invokeMethod(menu, "aboutToShow");
    QVector<WindowId> listed; QHash<WindowId, QPointer<QAction>> retained;
    for (auto* action : menu->actions()) if (const auto id = action->property("windowSystemWindow").toUuid(); !id.isNull()) {
        listed.append(id); retained.insert(id, action);
        QVERIFY(action->data().isNull()); QVERIFY(action->isCheckable());
        QCOMPARE(action->property("windowSystemWindowDepth").toInt(), manager.model().window(id)->kind == WindowKind::Floating ? 1 : 0);
    }
    QCOMPARE(listed, (QVector<WindowId>{first, firstChild, anotherFirstChild, second, secondChild}));
    QCOMPARE(menu->actions().last(), retained.value(secondChild).data());
    QVERIFY(manager.reassignFamily(firstChild, second));
    QVERIFY(manager.requestCloseWindow(anotherFirstChild));
    QMetaObject::invokeMethod(menu, "aboutToShow");
    QVERIFY(!retained.value(anotherFirstChild));
    listed.clear();
    for (auto* action : menu->actions()) if (const auto id = action->property("windowSystemWindow").toUuid(); !id.isNull()) {
        listed.append(id); QCOMPARE(action, retained.value(id).data());
        QVERIFY(action->text().contains(manager.windowWidget(id)->windowTitle()));
    }
    QCOMPARE(listed, (QVector<WindowId>{first, second, firstChild, secondChild}));
    QCOMPARE(menu->findChildren<QAction*>("WindowListSeparator").size(), 1);
    verifySeparators(menu);
}

void MenuStructureTest::windowListActivatesAndRestoresTheChosenWindow()
{
    WindowManager manager; const auto owner = manager.createWindow();
    manager.addTab(owner, new QLabel("Owner"), {"Owner", {}, {}});
    const auto floating = manager.createWindow(WindowKind::Floating, {}, owner);
    const auto tab = manager.addTab(floating, new QLabel("Target"), {"Target", {}, {}});
    manager.showWindow(owner); manager.showWindow(floating);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(floating)));
    manager.windowWidget(floating)->showMinimized();
    QVERIFY(manager.activateWindow(owner));
    auto* menu = menuAt(menuBar(manager, owner), {"window"}); QVERIFY(menu);
    QMetaObject::invokeMethod(menu, "aboutToShow"); QAction* target = nullptr;
    for (auto* action : menu->actions()) if (action->property("windowSystemWindow").toUuid() == floating) target = action;
    QVERIFY(target); target->trigger();
    QTRY_COMPARE(manager.context().window, floating);
    QVERIFY(!manager.windowWidget(floating)->isMinimized()); QCOMPARE(manager.context(floating).tab, tab);
    QMetaObject::invokeMethod(menu, "aboutToShow"); QVERIFY(target->isChecked());
    QVERIFY(!manager.execute("window.activate", {{"targetWindow", QUuid::createUuid()}}, owner));
}

void MenuStructureTest::tabAppearanceCommandsUseOneClassifiedCheckedAction_data()
{
    QTest::addColumn<bool>("floating"); QTest::addColumn<QString>("id");
    for (const bool floating : {false, true})
        for (const auto* id : {"presentation.toggleIcons", "presentation.toggleNewTabButton", "presentation.toggleNumbers",
                               "presentation.toggleTitleClipping", "presentation.toggleSidebarTitleClipping", "presentation.toggleMultiline", "presentation.toggleWheelExpandRows"})
            QTest::newRow(qPrintable(QStringLiteral("%1-%2").arg(floating ? "floating" : "workbench", QString::fromLatin1(id))))
                << floating << QString::fromLatin1(id);
}

void MenuStructureTest::tabAppearanceCommandsUseOneClassifiedCheckedAction()
{
    QFETCH(bool, floating); QFETCH(QString, id);
    WindowManager manager;
    if (id == "presentation.toggleWheelExpandRows") {
        auto appearance = manager.tabAppearance(); appearance.multiline = true;
        QVERIFY(manager.setTabAppearance(appearance));
    }
    const auto owner = manager.createWindow();
    const auto window = floating ? manager.createWindow(WindowKind::Floating, {}, owner) : owner;
    auto* content = new QLabel("Retained content");
    const auto tab = manager.addTab(window, content, {"Retained", "label", {}});
    const auto pane = manager.model().tab(tab)->activePane;
    auto* bar = menuBar(manager, window); QVERIFY(bar);
    const bool sidebar = id == "presentation.toggleSidebarTitleClipping";
    auto* menu = menuAt(bar, {"view", sidebar ? "sidebarAppearance" : "tabBar"}); QVERIFY(menu);
    const QStringList expected = sidebar ? QStringList{"presentation.toggleSidebarIcons", "presentation.toggleSidebarNumbers", "presentation.toggleSidebarTitleClipping"}
        : QStringList{"presentation.toggleIcons", "presentation.toggleNewTabButton", "presentation.toggleNumbers",
            "presentation.toggleTitleClipping", "presentation.toggleMultiline", "presentation.toggleWheelExpandRows"};
    QCOMPARE(ids(menu), expected);
    QHash<QString, int> occurrences;
    for (auto* root : bar->actions()) if (root->menu()) walk(root->menu(), occurrences);
    for (const auto& command : ids(menu)) {
        QCOMPARE(occurrences.value(command), 1);
        auto* action = menu->findChild<QAction*>(command); QVERIFY(action && action->isCheckable());
        QVERIFY(applicationMenuExclusion(command).isEmpty()); QVERIFY(floatingMenuExclusion(command).isEmpty());
    }
    for (const auto* placement : {"presentation.setCentralPlacement", "presentation.setPinnedPlacement"}) {
        QVERIFY(!applicationMenuExclusion(QString::fromLatin1(placement)).isEmpty());
        QVERIFY(!floatingMenuExclusion(QString::fromLatin1(placement)).isEmpty());
        QVERIFY(!bar->findChild<QAction*>(QString::fromLatin1(placement)));
    }
    const auto value = [&id](const TabAppearance& appearance) {
        if (id == "presentation.toggleIcons") return appearance.showIcons;
        if (id == "presentation.toggleNewTabButton") return appearance.showNewTabButton;
        if (id == "presentation.toggleNumbers") return appearance.showNumbers;
        if (id == "presentation.toggleTitleClipping") return appearance.clipTitles;
        if (id == "presentation.toggleSidebarTitleClipping") return appearance.clipSidebarTitles;
        if (id == "presentation.toggleMultiline") return appearance.multiline;
        return appearance.wheelExpandRows;
    };
    auto* action = menu->findChild<QAction*>(id); QVERIFY(action && action->isEnabled());
    const bool initial = value(manager.tabAppearance()); QCOMPARE(action->isChecked(), initial);
    QSignalSpy changes(&manager, &WindowManager::layoutChanged);
    action->trigger(); QTRY_COMPARE(value(manager.tabAppearance()), !initial);
    QCoreApplication::processEvents(); QCOMPARE(changes.count(), 1);
    QMetaObject::invokeMethod(menu, "aboutToShow"); QCOMPARE(action->isChecked(), !initial);
    QCOMPARE(bar->findChildren<QAction*>(id).size(), 1);
    changes.clear();
    QVERIFY(manager.executeInContext(id, manager.context(window)));
    QCOMPARE(value(manager.tabAppearance()), initial); QCOMPARE(changes.count(), 1);
    QMetaObject::invokeMethod(menu, "aboutToShow"); QCOMPARE(action->isChecked(), initial);
    QCOMPARE(manager.paneWidget(pane), content); QVERIFY(manager.model().validate().ok);
}

void MenuStructureTest::wheelRowsRequireMultilineAndResetWithIt_data()
{
    QTest::addColumn<bool>("floating");
    QTest::newRow("workbench") << false; QTest::newRow("floating") << true;
}

void MenuStructureTest::wheelRowsRequireMultilineAndResetWithIt()
{
    QFETCH(bool, floating);
    WindowManager manager; const auto owner = manager.createWindow();
    const auto window = floating ? manager.createWindow(WindowKind::Floating, {}, owner) : owner;
    const auto tab = manager.addTab(window, new QLabel("Retained"), {"Retained", "label", {}});
    auto* menu = menuAt(menuBar(manager, window), {"view", "tabBar"}); QVERIFY(menu);
    auto* multiline = menu->findChild<QAction*>("presentation.toggleMultiline");
    auto* wheel = menu->findChild<QAction*>("presentation.toggleWheelExpandRows");
    QVERIFY(multiline && wheel); QVERIFY(multiline->isCheckable() && wheel->isCheckable());
    QVERIFY(!multiline->isChecked() && !wheel->isChecked()); QVERIFY(wheel->isVisible() && !wheel->isEnabled());
    QSignalSpy changes(&manager, &WindowManager::layoutChanged);
    wheel->trigger(); QCoreApplication::processEvents(); QCOMPARE(changes.count(), 0);
    QVERIFY(!manager.executeInContext("presentation.toggleWheelExpandRows", manager.context(window)));
    QCOMPARE(changes.count(), 0); QVERIFY(!manager.tabAppearance().multiline && !manager.tabAppearance().wheelExpandRows);
    multiline->trigger(); QTRY_VERIFY(manager.tabAppearance().multiline);
    QMetaObject::invokeMethod(menu, "aboutToShow"); QVERIFY(multiline->isChecked()); QVERIFY(wheel->isEnabled());
    wheel->trigger(); QTRY_VERIFY(manager.tabAppearance().wheelExpandRows);
    QMetaObject::invokeMethod(menu, "aboutToShow"); QVERIFY(wheel->isChecked());
    multiline->trigger(); QTRY_VERIFY(!manager.tabAppearance().multiline);
    QMetaObject::invokeMethod(menu, "aboutToShow");
    // Wheel expansion is kept, shown checked but unavailable with one row.
    QVERIFY(manager.tabAppearance().wheelExpandRows && wheel->isChecked()); QVERIFY(wheel->isVisible() && !wheel->isEnabled());
    QCoreApplication::processEvents(); QCOMPARE(changes.count(), 3);
    QVERIFY(manager.model().tab(tab)); QVERIFY(manager.model().validate().ok);
}

QTEST_MAIN(MenuStructureTest)
#include "menu_structure_test.moc"
