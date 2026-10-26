// SPDX-License-Identifier: MIT
#include <QCoreApplication>
#include "command_menus.hpp"
#include "popup_menu.hpp"
#include "chrome/command_strip.hpp"
#include "style/tab_appearance_p.hpp"
#include "localization/display_names.hpp"
#include "chrome/workspace_window.hpp"
#include <window_system/window_manager.hpp>

#include <QAction>
#include <QMenu>
#include <QMenuBar>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QTimer>
#include <QVariant>
#include <algorithm>
#include <optional>

namespace window_system {
namespace {

struct Item {
    QString id;
    QString title;
    QVector<Item> children;
    int order = 0;
    bool command = false;
    QString pathKey;
};
using Items = QVector<Item>;
Item command(QString id, int order = 0) { return {std::move(id), {}, {}, order, true}; }
Item command(const char* id, int order = 0) { return command(QString::fromUtf8(id), order); }
Item group(const char* id, const char* pathKey, const char* title, Items children, int order = 0)
{
    return {QString::fromUtf8(id), QString::fromUtf8(title), std::move(children), order, false, QString::fromUtf8(pathKey)};
}
Item separator(int order = 0) { return {{}, {}, {}, order, false}; }
Items directions(const char* prefix)
{
    Items result;
    for (const auto* direction : {"left", "right", "up", "down"})
        result.append(command(QString::fromLatin1(prefix) + QString::fromLatin1(direction)));
    return result;
}
Items tabMovement()
{
    return {group("tab.move.group", "moveToAdjacentGroup", QT_TRANSLATE_NOOP("WindowSystem", "Move into adjacent tab group"), directions("workspace.moveTabToGroup.")),
        group("tab.move.window", "moveToOtherWindow", QT_TRANSLATE_NOOP("WindowSystem", "Move to another window"), {command("workspace.moveTabToWindow.previous"),
            command("workspace.moveTabToWindow.next"), separator(), command("workspace.moveTabToWindow")}),
        group("tab.move.newWindow", "moveToNewWindow", QT_TRANSLATE_NOOP("WindowSystem", "Move to a new window"), {command("workspace.detachFloating"), command("workspace.detachWorkbench")})};
}
Items sorting()
{
    return {command("group.sort.alphabeticalAscending"), command("group.sort.alphabeticalDescending"), separator(),
        command("group.sort.openedAscending"), command("group.sort.openedDescending"), separator(),
        command("group.sort.visitedAscending"), command("group.sort.visitedDescending")};
}
Items groupMovement()
{
    return {group("group.move.direction", "placeBesideGroup", QT_TRANSLATE_NOOP("WindowSystem", "Place beside adjacent tab group"), directions("group.move.")),
        command("group.moveToGroup"),
        group("group.move.window", "moveToOtherWindow", QT_TRANSLATE_NOOP("WindowSystem", "Move to another window"), {command("group.moveWindow.previous"), command("group.moveWindow.next"),
            separator(), command("group.moveWindow")}),
        group("group.move.newWindow", "moveToNewWindow", QT_TRANSLATE_NOOP("WindowSystem", "Move to a new window"), {command("group.detachFloating"), command("group.detachWorkbench")}),
        group("group.move.edge", "pinToSidebar", QT_TRANSLATE_NOOP("WindowSystem", "Pin to sidebar"), directions("group.moveEdge.")),
        group("group.move.hide", "autoHideInSidebar", QT_TRANSLATE_NOOP("WindowSystem", "Auto-hide in sidebar"), directions("group.hideEdge."))};
}
Items groupExchange()
{
    auto result = directions("group.swap."); result += {separator(), command("group.swap")}; return result;
}
Items groupMerge()
{
    auto result = directions("group.merge."); result += {separator(), command("group.merge")}; return result;
}
Items paneMovement()
{
    return {command("pane.extractToTab"), separator(),
        group("pane.move.tab", "mergeIntoTab", QT_TRANSLATE_NOOP("WindowSystem", "Merge into another tab's pane layout"), {command("pane.moveToTab.previous"), command("pane.moveToTab.next"),
            separator(), command("pane.moveToTab")}),
        group("pane.move.group", "mergeIntoAdjacentGroup", QT_TRANSLATE_NOOP("WindowSystem", "Merge into active tab of adjacent group"), directions("pane.moveToGroup.")),
        group("pane.move.window", "moveToOtherWindow", QT_TRANSLATE_NOOP("WindowSystem", "Move to another window"), {command("pane.moveToWindow.previous"), command("pane.moveToWindow.next"),
            separator(), command("pane.moveToWindow")}),
        group("pane.move.newWindow", "moveToNewWindow", QT_TRANSLATE_NOOP("WindowSystem", "Move to a new window"), {command("pane.extractFloating"), command("pane.extractWorkbench")})};
}
Items tabEdgeOperations(bool includeCurrent = true)
{
    Items result{group("edge.collect", "autoHideTab", QT_TRANSLATE_NOOP("WindowSystem", "Auto-hide this tab in sidebar"), directions("workspace.hideToEdge.")), separator()};
    if (includeCurrent) result += {command("workspace.pinTab"), command("workspace.unpinTab")};
    result.append(group("edge.peek", "peekSize", QT_TRANSLATE_NOOP("WindowSystem", "This tab's peek size"), {command("tab.peek.setSize"), command("tab.peek.resetSize")}));
    return result;
}
Items groupOperations()
{
    return {group("group.create", "createGroup", QT_TRANSLATE_NOOP("WindowSystem", "Create tab group beside this group"), directions("workspace.splitGroup.")),
        group("group.resize", "resizeGroup", QT_TRANSLATE_NOOP("WindowSystem", "Resize this group"), directions("workspace.resizeGroup.")), separator(),
        group("tab.group.move", "moveGroup", QT_TRANSLATE_NOOP("WindowSystem", "Move this tab group"), groupMovement()),
        group("tab.group.swap", "swapGroup", QT_TRANSLATE_NOOP("WindowSystem", "Swap with another tab group"), groupExchange()),
        group("tab.group.merge", "mergeGroup", QT_TRANSLATE_NOOP("WindowSystem", "Merge this group's tabs into another group"), groupMerge()),
        group("group.convert", "convertGroup", QT_TRANSLATE_NOOP("WindowSystem", "Convert this group's contents"), {command("group.convertToTab"), command("group.moveIntoTab"), separator(),
            command("group.convertToFolder.left"), command("group.convertToFolder.right"), command("group.convertToFolder.up"), command("group.convertToFolder.down"),
            command("group.convertToFolder")}),
        command("tabFolder.collapseAll"), command("tabFolder.expandAll"),
        separator(), group("group.sort", "sortGroup", QT_TRANSLATE_NOOP("WindowSystem", "Sort tabs in this group"), sorting()),
        group("edge.group", "groupSidebar", QT_TRANSLATE_NOOP("WindowSystem", "Dock this group in sidebar"), {command("workspace.pinGroup"), command("workspace.unpinGroup"),
            command("workspace.restoreGroupToCenter"), separator(),
            group("edge.all", "sidebarTabs", QT_TRANSLATE_NOOP("WindowSystem", "All tabs in this sidebar"), {command("workspace.pinEdge"), command("workspace.unpinEdge")})}),
        separator(), command("group.rename"), command("group.close")};
}
Items folderOperations()
{
    return {command("tabFolder.rename"), command("tabFolder.setColor"), command("tabFolder.toggleCollapsed"), command("tabFolder.newTab"), separator(),
        group("tabFolder.order", "reorderFolder", QT_TRANSLATE_NOOP("WindowSystem", "Reorder this tab folder in its bar"), {command("tabFolder.moveBackward"), command("tabFolder.moveForward")}),
        group("tabFolder.move", "moveFolder", QT_TRANSLATE_NOOP("WindowSystem", "Move this tab folder"), {
            group("tabFolder.move.group", "moveToAdjacentGroup", QT_TRANSLATE_NOOP("WindowSystem", "Move into adjacent tab group"), directions("tabFolder.move.")),
            group("tabFolder.move.window", "moveToOtherWindow", QT_TRANSLATE_NOOP("WindowSystem", "Move to another window"), {command("tabFolder.moveWindow.previous"),
                command("tabFolder.moveWindow.next"), separator(), command("tabFolder.moveWindow")}),
            group("tabFolder.move.newWindow", "moveToNewWindow", QT_TRANSLATE_NOOP("WindowSystem", "Move to a new window"), {command("tabFolder.detachFloating"), command("tabFolder.detachWorkbench")})}),
        group("tabFolder.mergeMenu", "mergeFolder", QT_TRANSLATE_NOOP("WindowSystem", "Merge into another tab folder"), {command("tabFolder.merge.previous"), command("tabFolder.merge.next"),
            separator(), command("tabFolder.merge")}),
        group("tabFolder.convert", "convertFolder", QT_TRANSLATE_NOOP("WindowSystem", "Convert this tab folder to a tab group"), directions("tabFolder.convertToGroup.")),
        separator(), command("tabFolder.ungroup"), command("tabFolder.close")};
}
Items tabFolderMembership()
{
    return {command("tabFolder.new"), command("tabFolder.addTab"), command("tabFolder.removeTab")};
}
Items windowOperations()
{
    return {group("window.kind", "windowKind", QT_TRANSLATE_NOOP("WindowSystem", "Window type"), {command("window.promote"), command("window.demote"), command("window.reassignFamily")}),
        group("window.transfer", "moveWindowTabs", QT_TRANSLATE_NOOP("WindowSystem", "Move all tabs from this window"), {command("window.moveAllTabs"), command("window.redock")})};
}
Items floatingItems(Items items)
{
    for (auto iterator = items.begin(); iterator != items.end();) {
        if (iterator->command && !floatingMenuExclusion(iterator->id).isEmpty()) iterator = items.erase(iterator);
        else {
            iterator->children = floatingItems(std::move(iterator->children));
            ++iterator;
        }
    }
    return items;
}
const Items& layout(CommandMenuSurface surface)
{
    static const Items application = {
        group("file", "file", QT_TRANSLATE_NOOP("WindowSystem", "&File"), {command("window.new", 20), separator(30), separator(60),
            command("workspace.closeTab", 70), command("window.close", 80), separator(90)}, 10),
        group("view", "view", QT_TRANSLATE_NOOP("WindowSystem", "&View"), {command("workbench.showCommandPalette"), separator(),
            group("view.interface", "interface", QT_TRANSLATE_NOOP("WindowSystem", "Interface"), {
                command("workbench.action.zoomIn"), command("workbench.action.zoomOut"), command("workbench.action.zoomReset"), separator(),
                command("workbench.action.increaseInterfaceFontSize"), command("workbench.action.decreaseInterfaceFontSize"), command("workbench.action.resetInterfaceFontSize")}),
            group("view.visibility", "visibility", QT_TRANSLATE_NOOP("WindowSystem", "Visibility"), {command("window.toolbars.toggle"), command("window.statusBar.toggle"),
                command("window.tabBars.toggle"), command("window.sideBars.toggle")}),
            group("view.tabAppearance", "tabBar", QT_TRANSLATE_NOOP("WindowSystem", "Tab bar"), {command("presentation.toggleIcons"), command("presentation.toggleNewTabButton"),
                command("presentation.toggleNumbers"), command("presentation.toggleTitleClipping"), separator(), command("presentation.toggleMultiline"),
                command("presentation.toggleWheelExpandRows")}),
            group("view.sidebarAppearance", "sidebarAppearance", QT_TRANSLATE_NOOP("WindowSystem", "Sidebar tabs"), {command("presentation.toggleSidebarIcons"), command("presentation.toggleSidebarNumbers"),
                command("presentation.toggleSidebarTitleClipping")}),
            group("view.fullscreen", "fullscreen", QT_TRANSLATE_NOOP("WindowSystem", "Fullscreen"), {command("window.fullscreen.toggle"), command("window.fullscreen.group"),
                command("window.fullscreen.pane"), separator(), command("window.zen.toggle"), command("window.fullscreen.exit")}),
            group("view.tabs", "switchTab", QT_TRANSLATE_NOOP("WindowSystem", "Switch tabs"), {command("workspace.previousTab"), command("workspace.nextTab")}),
            group("view.groups", "focusGroup", QT_TRANSLATE_NOOP("WindowSystem", "Focus tab group"), directions("workspace.focusGroup.")),
            group("view.panes", "focusPane", QT_TRANSLATE_NOOP("WindowSystem", "Focus pane"), directions("pane.focus.")),
            group("view.toolbars", "toolbars", QT_TRANSLATE_NOOP("WindowSystem", "Toolbar strips"), {command("window.toggleStripsLock"), command("window.toolbar.move")})}, 20),
        group("layout", "layout", QT_TRANSLATE_NOOP("WindowSystem", "&Layout"), {
            group("layout.groups", "currentGroup", QT_TRANSLATE_NOOP("WindowSystem", "Current tab group"), groupOperations()),
            group("layout.tab", "currentTab", QT_TRANSLATE_NOOP("WindowSystem", "Current tab"), {command("tab.rename"), command("tab.stick"), command("tab.unstick"), command("tab.color.set"), separator(),
                group("tab.order", "reorderTab", QT_TRANSLATE_NOOP("WindowSystem", "Reorder this tab in its bar"), {command("workspace.reorderTab.previous"), command("workspace.reorderTab.next")}),
                group("tab.move", "moveTab", QT_TRANSLATE_NOOP("WindowSystem", "Move this tab"), tabMovement()),
                group("tab.convert", "convertTab", QT_TRANSLATE_NOOP("WindowSystem", "Convert this tab's contents"), {command("tab.convertToGroup"), command("tab.moveIntoTab")}),
                group("tab.edge", "tabSidebar", QT_TRANSLATE_NOOP("WindowSystem", "This tab's sidebar placement"), tabEdgeOperations())}),
            group("layout.folder", "currentFolder", QT_TRANSLATE_NOOP("WindowSystem", "Current tab folder"), [] {
                auto items = tabFolderMembership(); items += separator(); items += folderOperations();
                items += {separator(), command("tabFolder.focus.previous"), command("tabFolder.focus.next")}; return items;
            }()),
            group("layout.panes", "currentPane", QT_TRANSLATE_NOOP("WindowSystem", "Current pane"), {
                group("pane.split", "splitPane", QT_TRANSLATE_NOOP("WindowSystem", "Split this pane"), directions("pane.split.")),
                group("pane.resize", "resizePane", QT_TRANSLATE_NOOP("WindowSystem", "Resize this pane"), directions("pane.resize.")), separator(),
                group("pane.move", "movePane", QT_TRANSLATE_NOOP("WindowSystem", "Move this pane"), paneMovement()),
                group("pane.swap", "swapPane", QT_TRANSLATE_NOOP("WindowSystem", "Swap with another pane in this tab"), directions("pane.swap.")),
                separator(), command("pane.close")}), separator(900)}, 30),
        group("window", "window", QT_TRANSLATE_NOOP("WindowSystem", "&Window"), {command("window.select"), command("window.focusOwner"), separator(),
            group("window.cycle", "switchWindow", QT_TRANSLATE_NOOP("WindowSystem", "Switch window"), {
                group("window.cycle.family", "family", QT_TRANSLATE_NOOP("WindowSystem", "Window family"), {command("window.focusPrevious.family"), command("window.focusNext.family")}),
                group("window.cycle.all", "allWindows", QT_TRANSLATE_NOOP("WindowSystem", "All windows"), {command("window.focusPrevious.all"), command("window.focusNext.all")}),
                group("window.cycle.workbenches", "workbenches", QT_TRANSLATE_NOOP("WindowSystem", "Workbench windows"), {command("window.focusPrevious.workbenches"), command("window.focusNext.workbenches")})}),
            group("window.state", "windowState", QT_TRANSLATE_NOOP("WindowSystem", "Window state"), {command("window.minimize"), command("window.maximize"), command("window.restore")}),
            windowOperations()[0], windowOperations()[1],
            group("window.drag", "drag", QT_TRANSLATE_NOOP("WindowSystem", "Drag"), {command("window.drag.selectTarget"), command("window.drag.cancel")})}, 40),
        group("tools", "tools", QT_TRANSLATE_NOOP("WindowSystem", "&Tools"), {command("window.settings"), command("preferences.openKeyboardShortcuts")}, 50),
        group("help", "help", QT_TRANSLATE_NOOP("WindowSystem", "&Help"), {}, 60)};
    static const Items floating = floatingItems(application);
    static const Items tab = {command("workspace.closeTab"), command("tab.rename"), command("tab.stick"), command("tab.unstick"),
        command("workspace.pinTab"), command("workspace.unpinTab"), separator(), tabFolderMembership()[0], tabFolderMembership()[1], tabFolderMembership()[2],
        group("tab.closeRange", "closeTabs", QT_TRANSLATE_NOOP("WindowSystem", "Close multiple tabs"), {command("workspace.closeTabs.left"), command("workspace.closeTabs.right"),
            command("workspace.closeTabs.others"), command("workspace.closeTabs.all")}), separator(),
        group("tab.order", "reorderTab", QT_TRANSLATE_NOOP("WindowSystem", "Reorder this tab in its bar"), {command("workspace.reorderTab.previous"), command("workspace.reorderTab.next")}),
        group("tab.move", "moveTab", QT_TRANSLATE_NOOP("WindowSystem", "Move this tab"), tabMovement()),
        group("tab.convert", "convertTab", QT_TRANSLATE_NOOP("WindowSystem", "Convert this tab's contents"), {command("tab.convertToGroup"), command("tab.moveIntoTab")}),
        group("tab.edge", "tabSidebar", QT_TRANSLATE_NOOP("WindowSystem", "This tab's sidebar placement"), tabEdgeOperations(false)), separator(), command("tab.color.set"),
        group("tab.group", "containingGroup", QT_TRANSLATE_NOOP("WindowSystem", "Containing tab group"), groupOperations())};
    static const Items tabGroup = groupOperations();
    static const Items tabFolder = folderOperations();
    static const Items pane = {command("pane.close"), separator(),
        group("pane.split", "splitPane", QT_TRANSLATE_NOOP("WindowSystem", "Split this pane"), directions("pane.split.")),
        group("pane.move", "movePane", QT_TRANSLATE_NOOP("WindowSystem", "Move this pane"), paneMovement()),
        group("pane.swap", "swapPane", QT_TRANSLATE_NOOP("WindowSystem", "Swap with another pane in this tab"), directions("pane.swap.")),
        group("pane.resize", "resizePane", QT_TRANSLATE_NOOP("WindowSystem", "Resize this pane"), directions("pane.resize.")),
        group("pane.bars", "auxiliaryBars", QT_TRANSLATE_NOOP("WindowSystem", "This tab's auxiliary bars"), {command("tab.topBar.setSharing"), command("tab.bottomBar.setSharing")})};
    static const Items window = {command("window.select"), command("window.focusOwner"), separator(),
        windowOperations()[0], windowOperations()[1], separator(), command("window.settings"), command("window.close")};
    switch (surface) {
    case CommandMenuSurface::Application: return application;
    case CommandMenuSurface::FloatingApplication: return floating;
    case CommandMenuSurface::Tab: return tab;
    case CommandMenuSurface::Group: return tabGroup;
    case CommandMenuSurface::TabFolder: return tabFolder;
    case CommandMenuSurface::Pane: return pane;
    case CommandMenuSurface::Window: return window;
    }
    return application;
}
void collectCommands(const Items& items, QStringList& result)
{
    for (const auto& item : items) {
        if (item.command) result.append(item.id);
        else collectCommands(item.children, result);
    }
}
QString plainTitle(QString title) { return title.remove(QRegularExpression(QStringLiteral("\\(&[A-Za-z]\\)"))); }

void tidy(QMenu* menu)
{
    bool previous = false; QAction* pending = nullptr;
    QSet<QAction*> visibleSeparators;
    for (auto* action : menu->actions()) {
        if (auto* child = action->menu()) {
            tidy(child);
            const auto childActions = child->actions();
            action->setVisible(std::any_of(childActions.cbegin(), childActions.cend(),
                [](QAction* item) { return !item->isSeparator() && item->isVisible(); }));
        }
        if (action->isSeparator()) pending = action;
        else if (action->isVisible()) {
            if (previous && pending) visibleSeparators.insert(pending);
            pending = nullptr; previous = true;
        }
    }
    // Submenu opening refreshes the command tree while ancestors are visible.
    // Decide final separator visibility before mutation: hide-then-show makes
    // QMenu resize/reposition twice and flashes the parent under the cursor.
    for (auto* action : menu->actions()) if (action->isSeparator())
        action->setVisible(visibleSeparators.contains(action));
}
bool orderedBefore(const QAction* left, const QAction* right)
{
    const int a = left->property("menuOrder").toInt(), b = right->property("menuOrder").toInt();
    if (a != b) return a < b;
    const auto titleA = left->property("menuSortTitle").toString(), titleB = right->property("menuSortTitle").toString();
    const int titleOrder = QString::localeAwareCompare(titleA, titleB);
    return titleOrder ? titleOrder < 0 : left->objectName() < right->objectName();
}
void insertOrdered(QWidget* owner, QAction* added)
{
    const auto actions = owner->actions();
    const auto before = std::find_if(actions.cbegin(), actions.cend(), [added](QAction* action) {
        return action != added && orderedBefore(added, action);
    });
    if (before != actions.cend()) owner->insertAction(*before, added);
}
QMenu* makeMenu(QWidget* owner, const Item& item, const QStringList& path)
{
    const auto title = item.id.startsWith(QStringLiteral("extension."))
        ? commandCategoryDisplayName(item.title)
        : QCoreApplication::translate("WindowSystem", item.title.toUtf8().constData());
    auto* menu = new DismissOnlyMenu(title, owner);
    menu->setObjectName(item.id); menu->setProperty("commandMenuPath", path);
    menu->setStyleSheet(QStringLiteral("QMenu { menu-scrollable: 1; }"));
    menu->menuAction()->setObjectName(item.id); menu->menuAction()->setProperty("menuOrder", item.order);
    menu->menuAction()->setProperty("menuSortTitle", plainTitle(item.title));
    return menu;
}
using Context = std::function<CommandContext()>;
void addItems(QMenu* menu, const Items& items, WindowManager* manager, const Context& context,
              QList<QPointer<QAction>>& actions, QList<QPointer<QMenu>>* menus, QObject* actionOwner,
              bool contribution = false)
{
    const QPointer<QMenu> retainedMenu(menu);
    const QPointer<WindowManager> retainedManager(manager);
    const QPointer<QObject> retainedOwner(actionOwner);
    int ordinal = 0;
    for (const auto& item : items) {
        if (!retainedMenu || !retainedManager || (actionOwner && !retainedOwner)) return;
        QAction* action = nullptr;
        if (item.command) {
            action = menu->addAction(QString()); action->setData(item.id); action->setObjectName(item.id);
            action->setProperty("windowSystemCommand", item.id); actions.append(action);
            if (contribution) action->setProperty("commandMenuContribution", true);
            const QPointer<QAction> retainedAction(action);
            refreshCommandAction(manager, action, context());
            if (!retainedMenu || !retainedManager || !retainedAction || (actionOwner && !retainedOwner)) return;
            if (actionOwner) QObject::connect(action, &QAction::triggered, actionOwner,
                [manager = QPointer<WindowManager>(manager), receiver = QPointer<QObject>(actionOwner), id = item.id, context] {
                if (!manager || !receiver) return;
                const auto captured = context();
                QTimer::singleShot(0, receiver.data(), [manager, id, captured] {
                    if (!manager) return;
                    manager->executeInContext(id, captured);
                });
            });
        } else if (!item.id.isEmpty()) {
            const auto path = menu->property("commandMenuPath").toStringList() + QStringList{item.pathKey};
            auto* child = makeMenu(menu, item, path); menu->addMenu(child); action = child->menuAction();
            if (menus) menus->append(child);
            addItems(child, item.children, manager, context, actions, menus, actionOwner, contribution);
            if (!retainedMenu || !retainedManager || (actionOwner && !retainedOwner)) return;
        } else action = menu->addSeparator();
        action->setProperty("menuOrder", item.order ? item.order : ++ordinal * 10);
        action->setProperty("menuSortTitle", item.command ? item.id : item.title);
    }
}

} // namespace

QStringList declaredMenuCommands(CommandMenuSurface surface)
{
    QStringList result; collectCommands(layout(surface), result); return result;
}
QString applicationMenuExclusion(const QString& id)
{
    if (id == "runCommands") return QCoreApplication::translate("WindowSystem", "Parameterized composite; configured in the keyboard shortcuts editor");
    if (id == "presentation.setCentralPlacement" || id == "presentation.setPinnedPlacement")
        return QCoreApplication::translate("WindowSystem", "The tab position command requires top/bottom; central and pinned sidebar bars are configured independently through settings or the API");
    if (id == "presentation.setRowCollapse")
        return QCoreApplication::translate("WindowSystem", "The row collapse command requires a mode; choose it from the tab bar context menu or settings");
    if (id == "window.activate") return QCoreApplication::translate("WindowSystem", "Available in the Window menu family list and window switcher");
    if (id == "window.setTitle") return QCoreApplication::translate("WindowSystem", "The host, a script, or command arguments supply the window title");
    if (id == "window.statusItem.setVisible") return QCoreApplication::translate("WindowSystem", "Show or hide individual items from the status bar context menu");
    if (id == "window.statusItem.setText") return QCoreApplication::translate("WindowSystem", "The host, a script, or command arguments supply status item content");
    if (id == "layout.setRatio" || id == "layout.setEdgeExtent") return QCoreApplication::translate("WindowSystem", "Internal layout operation; requires an explicit node or region");
    if (id == "window.moveAllToEdge" || id == "window.moveAllToEdgeBar") return QCoreApplication::translate("WindowSystem", "Internal window drop; requires a target window and drop position");
    if (id == "workspace.moveTabToEdge" || id == "workspace.moveTabToEdgeBar") return QCoreApplication::translate("WindowSystem", "Internal tab drop; use directional sidebar commands or drag and drop");
    if (id == "window.toolbar.setSizing" || id == "window.toolbar.resetSize") return QCoreApplication::translate("WindowSystem", "Customize from the toolbar options menu or command palette");
    if (id == "window.toolbar.setVisible" || id == "window.toolbar.setWidth" || id == "window.toolbar.setItemVisible")
        return QCoreApplication::translate("WindowSystem", "Customize from the toolbar options menu or command palette");
    if (id == "tab.topBar.setSharing" || id == "tab.bottomBar.setSharing") return QCoreApplication::translate("WindowSystem", "Available from the content context menu and command palette");
    if (id == "tab.toggleSticky") return QCoreApplication::translate("WindowSystem", "Shortcut toggle; menus offer sticking and unsticking separately");
    const QStringList closeScopes{"workspace.closeTabs.left", "workspace.closeTabs.right", "workspace.closeTabs.others", "workspace.closeTabs.all"};
    if (closeScopes.contains(id)) return QCoreApplication::translate("WindowSystem", "Tab context menus provide bulk close options; the File menu closes the current tab");
    return {};
}
QString floatingMenuExclusion(const QString& id)
{
    if (const auto reason = applicationMenuExclusion(id); !reason.isEmpty()) return reason;
    if (id == "application.exit") return QCoreApplication::translate("WindowSystem", "Exit application appears only in workbench window menus");
    if (id == "layout.save" || id == "layout.reset") return QCoreApplication::translate("WindowSystem", "Save and reset workspace commands appear only in workbench window menus");
    if (id == "window.new") return QCoreApplication::translate("WindowSystem", "New workbench window appears only in workbench window menus");
    if (id == "window.statusBar.toggle") return QCoreApplication::translate("WindowSystem", "Floating windows do not have a global status bar");
    if (id == "window.demote") return QCoreApplication::translate("WindowSystem", "A floating window already uses this window type");
    return {};
}

void refreshCommandAction(WindowManager* manager, QAction* action, const CommandContext& context)
{
    if (!action) return;
    if (!manager) { action->setVisible(false); action->setEnabled(false); return; }
    const auto id = action->data().toString(); const auto* found = manager->commands().find(id);
    if (!found) { action->setVisible(false); action->setEnabled(false); return; }
    const Command definition = *found;
    const QPointer<QAction> retained(action); const QPointer<WindowManager> owner(manager);
    const bool enabled = !definition.enabled || definition.enabled(context);
    if (!retained || !owner) return;
    const auto* window = manager->model().window(context.window);
    const auto* group = manager->model().group(context.group);
    bool visible = true;
    if (id == "window.promote") visible = window && window->kind == WindowKind::Floating;
    if (id == "window.demote") visible = window && window->kind == WindowKind::Workbench && enabled;
    if (id == "window.redock" || id == "window.reassignFamily") visible = window && window->kind == WindowKind::Floating && enabled;
    if (id == "window.drag.selectTarget" || id == "window.drag.cancel") visible = enabled;
    if (id == "workspace.pinTab" || id == "workspace.pinGroup") visible = group && group->location == GroupLocation::AutoHide;
    if (id == "tab.stick" || id == "tab.unstick") visible = enabled;
    if (id == "tab.peek.setSize" || id == "tab.peek.resetSize") visible = group && group->location == GroupLocation::AutoHide;
    if (id == "workspace.unpinTab" || id == "workspace.unpinGroup") visible = group && group->location == GroupLocation::Pinned;
    if (id == "workspace.pinEdge" || id == "workspace.unpinEdge" || id == "workspace.restoreGroupToCenter")
        visible = group && group->location != GroupLocation::Central;
    std::optional<bool> appearanceChecked;
    if (id == "presentation.toggleIcons") appearanceChecked = manager->tabAppearance().showIcons;
    else if (id == "presentation.toggleSidebarIcons") appearanceChecked = manager->tabAppearance().showSidebarIcons;
    else if (id == "presentation.toggleSidebarNumbers") appearanceChecked = manager->tabAppearance().showSidebarNumbers;
    else if (id == "presentation.toggleNewTabButton") appearanceChecked = manager->tabAppearance().showNewTabButton;
    else if (id == "presentation.toggleNumbers") appearanceChecked = manager->tabAppearance().showNumbers;
    else if (id == "presentation.toggleTitleClipping") appearanceChecked = manager->tabAppearance().clipTitles;
    else if (id == "presentation.toggleSidebarTitleClipping") appearanceChecked = manager->tabAppearance().clipSidebarTitles;
    else if (id == "presentation.toggleMultiline") appearanceChecked = manager->tabAppearance().multiline;
    else if (id == "presentation.toggleAllRows") appearanceChecked = manager->tabAppearance().showAllRows;
    else if (id == "presentation.toggleStickyMultiline") appearanceChecked = manager->tabAppearance().stickyMultiline;
    else if (id == "presentation.toggleStickyAllRows") appearanceChecked = manager->tabAppearance().stickyShowAllRows;
    else if (id == "presentation.toggleWheelExpandRows") appearanceChecked = manager->tabAppearance().wheelExpandRows;
    const bool checkedCommand = appearanceChecked.has_value() || id == "window.tabBars.toggle" || id == "window.sideBars.toggle"
        || id == "window.toolbars.toggle" || id == "window.statusBar.toggle" || id == "window.zen.toggle"
        || (id.startsWith("window.fullscreen.") && id != "window.fullscreen.exit")
        || id == "window.toggleStripsLock" || id == "tab.topBar.setSharing" || id == "tab.bottomBar.setSharing";
    const QSignalBlocker blocker(action); action->setCheckable(checkedCommand);
    if (appearanceChecked) action->setChecked(*appearanceChecked);
    if (id == "window.tabBars.toggle") action->setChecked(manager->tabBarsVisible(context.window));
    if (id == "window.sideBars.toggle") action->setChecked(manager->sideBarsVisible(context.window));
    if (id == "window.toolbars.toggle") action->setChecked(manager->toolbarsVisible(context.window));
    if (id == "window.statusBar.toggle") action->setChecked(manager->statusBarVisible(context.window));
    if (id == "window.zen.toggle") action->setChecked(manager->zenMode(context.window));
    if (id == "window.fullscreen.toggle") action->setChecked(manager->viewMode(context.window) == ViewMode::WindowFullscreen);
    if (id == "window.fullscreen.group") action->setChecked(manager->viewMode(context.window) == ViewMode::GroupFullscreen);
    if (id == "window.fullscreen.pane") action->setChecked(manager->viewMode(context.window) == ViewMode::PaneFullscreen);
    if (id == "window.toggleStripsLock") {
        const auto* view = static_cast<WorkspaceWindow*>(manager->windowWidget(context.window));
        action->setChecked(view && view->strips() && view->strips()->isLocked());
    }
    if (id == "tab.topBar.setSharing" || id == "tab.bottomBar.setSharing") {
        const auto options = manager->tabOptions(context.tab);
        action->setChecked(manager->model().tab(context.tab) && (id == "tab.topBar.setSharing"
            ? options.topBarSharing : options.bottomBarSharing) == PaneBarSharing::Shared);
    }
    QStringList shortcuts;
    for (const auto& binding : definition.bindings)
        shortcuts.append(binding.sequence.toString(QKeySequence::NativeText));
    const auto shortcut = shortcuts.join(QStringLiteral(" / "));
    action->setText(definition.title + (!action->property("stripCommand").toBool() && !shortcut.isEmpty() ? "\t" + shortcut : QString()));
    action->setToolTip(definition.title + (shortcut.isEmpty() ? QString() : "  " + shortcut));
    action->setVisible(visible); action->setEnabled(enabled);
}

void populateContextMenu(QMenu* menu, CommandMenuSurface surface, WindowManager* manager, const CommandContext& captured)
{
    const QPointer<QMenu> retainedMenu(menu);
    const QPointer<WindowManager> retainedManager(manager);
    if (!menu->actions().isEmpty()) menu->addSeparator();
    QList<QPointer<QAction>> actions;
    addItems(menu, layout(surface), manager, [captured] { return captured; }, actions, nullptr, manager);
    if (!retainedMenu || !retainedManager) return;
    struct Contribution { Command command; ContextMenuPlacement placement; };
    QVector<Contribution> contributions;
    const auto target = surface == CommandMenuSurface::Tab ? ContextMenuTarget::Tab
        : surface == CommandMenuSurface::Group ? ContextMenuTarget::Group
        : surface == CommandMenuSurface::Pane ? ContextMenuTarget::Pane : ContextMenuTarget::Window;
    const auto contentType = manager->tabOptions(captured.tab).contentType;
    for (const auto& command : manager->commands().commands())
        for (const auto& placement : command.contextMenus)
            if (placement.target == target && (placement.contentType.isEmpty() || placement.contentType == contentType))
                contributions.append({command, placement});
    std::stable_sort(contributions.begin(), contributions.end(), [](const auto& left, const auto& right) {
        if (left.placement.order != right.placement.order) return left.placement.order < right.placement.order;
        return left.command.id < right.command.id;
    });
    QHash<QString, QPointer<QMenu>> contributedMenus;
    if (!contributions.isEmpty()) menu->addSeparator();
    for (const auto& contribution : contributions) {
        if (!retainedMenu || !retainedManager) return;
        QPointer<QMenu> destination(menu);
        QString prefix;
        for (const auto& title : contribution.placement.path) {
            prefix += QChar(0x1f) + title;
            auto child = contributedMenus.value(prefix);
            if (!child) {
                child = new DismissOnlyMenu(title, destination);
                child->setObjectName(QStringLiteral("ExtensionContextMenu"));
                destination->addMenu(child); contributedMenus.insert(prefix, child);
            }
            destination = child;
        }
        addItems(destination, {command(contribution.command.id)}, manager, [captured] { return captured; },
                 actions, nullptr, manager);
    }
    if (!retainedMenu || !retainedManager) return;
    if (surface == CommandMenuSurface::Tab || surface == CommandMenuSurface::Group) {
        for (auto* action : menu->actions()) if (action->objectName() == "tab.color.set") {
            auto* colors = new DismissOnlyMenu(QCoreApplication::translate("WindowSystem", "Set tab color"), menu);
            colors->setObjectName(QStringLiteral("TabColorChoices"));
            const auto addColor = [colors, owner = QPointer<WindowManager>(manager), captured](QString name, QString id) {
                auto* choice = colors->addAction(name); choice->setData(id); choice->setCheckable(true);
                if (owner && owner->model().tab(captured.tab)) choice->setChecked(owner->model().tab(captured.tab)->colorId == id);
                QObject::connect(choice, &QAction::triggered, colors, [owner, captured, id] {
                    if (!owner) return;
                    auto context = captured; context.args.insert("color", id);
                    owner->executeInContext(QStringLiteral("tab.color.set"), context);
                });
            };
            addColor(QCoreApplication::translate("WindowSystem", "Default color"), {});
            for (const auto& preset : manager->tabAppearance().colors) addColor(tabColorDisplayName(preset), preset.id);
            action->setMenu(colors);
        }
    }
    tidy(menu);
    for (auto* child : menu->findChildren<QMenu*>()) QObject::connect(child, &QMenu::aboutToShow, menu,
        [manager = QPointer<WindowManager>(manager), actions, captured, menu] {
        for (const auto& action : actions) if (action) refreshCommandAction(manager, action, captured);
        tidy(menu);
    });
}

ApplicationCommandMenus::ApplicationCommandMenus(QMenuBar* bar, WindowManager* manager, WindowId window,
                                               CommandMenuSurface surface)
    : QObject(bar), bar_(bar), manager_(manager), window_(window), surface_(surface)
{
    for (const auto& item : layout(surface_)) {
        auto* menu = makeMenu(bar, item, {item.id}); bar->addMenu(menu); menus_.append(menu);
        addItems(menu, item.children, manager_, [this] { return manager_ ? manager_->context(window_) : CommandContext{}; }, actions_, &menus_, this);
    }
    for (const auto& menu : menus_) connect(menu, &QMenu::aboutToShow, this, [this] { refresh(); });
    refresh();
}
QMenu* ApplicationCommandMenus::menuAtPath(const QStringList& path)
{
    if (path.isEmpty() || !bar_) return nullptr;
    QStringList partial;
    QMenu* parent = nullptr;
    for (const auto& title : path) {
        partial.append(title);
        auto existing = std::find_if(menus_.cbegin(), menus_.cend(), [&partial](const auto& menu) {
            return menu && menu->property("commandMenuPath").toStringList() == partial;
        });
        if (existing != menus_.cend()) parent = existing->data();
        else {
            Item item{QStringLiteral("extension.") + partial.join('.'), title, {}, 500, false};
            auto* child = makeMenu(parent ? static_cast<QWidget*>(parent) : bar_.data(), item, partial);
            child->setProperty("commandMenuContribution", true);
            if (parent) { parent->addMenu(child); insertOrdered(parent, child->menuAction()); }
            else { bar_->addMenu(child); insertOrdered(bar_, child->menuAction()); }
            menus_.append(child); connect(child, &QMenu::aboutToShow, this, [this] { refresh(); }); parent = child;
        }
    }
    return parent;
}
bool ApplicationCommandMenus::removeUnusedContributions()
{
    bool removed = false;
    const QPointer<ApplicationCommandMenus> retained(this);
    const auto actions = actions_;
    for (const auto& action : actions) {
        if (!retained || !manager_ || !bar_) return removed;
        // Built-in placeholders intentionally survive absent host commands so
        // their declared position is still available for a later registration.
        if (action && action->property("commandMenuContribution").toBool()
            && !manager_->commands().find(action->data().toString())) {
            delete action.data();
            removed = true;
        }
    }
    if (!retained) return removed;
    actions_.removeIf([](const auto& action) { return action.isNull(); });
    // Children are appended after their parent. Reclaim empty dynamic branches
    // from the leaves upward, preserving both core menus and shared branches.
    const auto menus = menus_;
    for (auto it = menus.crbegin(); it != menus.crend(); ++it) {
        if (!retained || !manager_ || !bar_) return removed;
        const auto menu = *it;
        if (!menu || !menu->property("commandMenuContribution").toBool()) continue;
        const auto items = menu->actions();
        if (std::none_of(items.cbegin(), items.cend(), [](QAction* action) { return !action->isSeparator(); })) {
            delete menu.data();
            removed = true;
        }
    }
    if (retained) menus_.removeIf([](const auto& menu) { return menu.isNull(); });
    return removed;
}
void ApplicationCommandMenus::addContributions()
{
    if (!manager_ || !bar_) return;
    const QPointer<ApplicationCommandMenus> retained(this);
    const auto declared = declaredMenuCommands(surface_);
    for (const auto& definition : manager_->commands().commands()) {
        const auto exclusion = surface_ == CommandMenuSurface::FloatingApplication
            ? floatingMenuExclusion(definition.id) : applicationMenuExclusion(definition.id);
        if (declared.contains(definition.id) || !exclusion.isEmpty()) continue;
        if (std::any_of(actions_.cbegin(), actions_.cend(), [&definition](const auto& action) {
            return action && action->data().toString() == definition.id;
        })) continue;
        auto path = definition.menuPath;
        if (path.isEmpty()) {
            const auto category = definition.category.isEmpty() ? QStringLiteral("other") : definition.category;
            const QStringList core{QStringLiteral("file"), QStringLiteral("view"), QStringLiteral("layout"),
                QStringLiteral("window"), QStringLiteral("tools"), QStringLiteral("help")};
            path = core.contains(category) ? QStringList{category, QStringLiteral("extensions")}
                : QStringList{QStringLiteral("extensions"), category};
        }
        const QPointer<QMenu> menu(menuAtPath(path)); if (!menu) continue;
        addItems(menu, {command(definition.id, definition.menuOrder)}, manager_,
            [this] { return manager_ ? manager_->context(window_) : CommandContext{}; }, actions_, nullptr, this, true);
        if (!retained || !manager_ || !bar_) return;
        // An enabled callback can unload its own command and synchronously
        // reclaim this branch. It can also register unrelated contributions;
        // the last action in the list is therefore not necessarily ours.
        if (!menu || !manager_->commands().find(definition.id)) continue;
        const auto found = std::find_if(actions_.cbegin(), actions_.cend(), [&definition](const auto& action) {
            return action && action->data().toString() == definition.id;
        });
        if (found == actions_.cend()) continue;
        const QPointer<QAction> action = *found;
        action->setProperty("menuOrder", definition.menuOrder);
        action->setProperty("menuSortTitle", definition.title);
        insertOrdered(menu, action);
    }
}
void ApplicationCommandMenus::refreshWindowList()
{
    if (!manager_ || !bar_) return;
    auto* menu = menuAtPath({QStringLiteral("window")});
    if (!menu) return;
    const auto order = manager_->model().windowOrder();
    const QSet<WindowId> present(order.cbegin(), order.cend());
    for (auto iterator = windowActions_.begin(); iterator != windowActions_.end();) {
        if (!present.contains(iterator.key()) || !iterator.value()) {
            if (iterator.value()) delete iterator.value().data();
            iterator = windowActions_.erase(iterator);
        } else ++iterator;
    }
    if (!windowListSeparator_) {
        windowListSeparator_ = menu->addSeparator();
        windowListSeparator_->setObjectName(QStringLiteral("WindowListSeparator"));
        windowListSeparator_->setProperty("menuOrder", 1000000);
    }
    QList<QAction*> desiredTail{windowListSeparator_};
    const auto append = [this, menu, &desiredTail](WindowId id, const QString& prefix, bool floating) {
        const auto* widget = manager_->windowWidget(id);
        if (!widget) return;
        auto retained = windowActions_.value(id);
        if (!retained) {
            auto* action = new QAction(menu);
            action->setObjectName(QStringLiteral("WindowListItem"));
            action->setProperty("windowSystemWindow", id);
            action->setProperty("menuOrder", 1000000);
            action->setCheckable(true);
            connect(action, &QAction::triggered, this,
                [manager = manager_, source = window_, target = id] {
                    if (!manager || !manager->model().window(target)) return;
                    QTimer::singleShot(0, manager, [manager, source, target] {
                        if (manager) manager->execute(QStringLiteral("window.activate"),
                            {{QStringLiteral("targetWindow"), target}}, source);
                    });
                });
            retained = action; windowActions_.insert(id, action);
        }
        retained->setText(prefix + widget->windowTitle());
        retained->setToolTip(widget->windowTitle());
        const auto theme = manager_->interfaceTheme();
        const QVariantList iconTokens{floating, theme.text, theme.mutedText, theme.selectionText, theme.accent};
        if (retained->property("windowListIconTokens").toList() != iconTokens) {
            retained->setIcon(iconFor(floating ? QStringLiteral("detach") : QStringLiteral("window"), theme));
            retained->setProperty("windowListIconTokens", iconTokens);
        }
        retained->setChecked(manager_->context().window == id);
        retained->setProperty("windowSystemWindowDepth", floating ? 1 : 0);
        desiredTail.append(retained);
    };
    QHash<FamilyId, QVector<WindowId>> children;
    for (const auto id : order) {
        const auto* record = manager_->model().window(id);
        if (record && record->kind == WindowKind::Floating) children[record->familyId].append(id);
    }
    int familyIndex = 0;
    for (const auto id : order) {
        const auto* record = manager_->model().window(id);
        if (!record || record->kind != WindowKind::Workbench) continue;
        const auto family = record->familyId;
        ++familyIndex;
        append(id, QStringLiteral("%1  ").arg(familyIndex), false);
        int childIndex = 0;
        for (const auto child : children.value(family))
            append(child, QStringLiteral("    %1.%2  ").arg(familyIndex).arg(++childIndex), true);
    }
    QList<QAction*> currentTail;
    const auto existing = menu->actions();
    for (auto* action : existing)
        if (action == windowListSeparator_ || action->objectName() == QStringLiteral("WindowListItem")) currentTail.append(action);
    // Moving the same actions on every nested aboutToShow invalidates native
    // parent-menu geometry. Only actual window/order changes touch the list.
    if (currentTail != desiredTail || existing.mid(existing.size() - desiredTail.size()) != desiredTail) {
        for (auto* action : currentTail) menu->removeAction(action);
        for (auto* action : desiredTail) menu->addAction(action);
    }
}
void ApplicationCommandMenus::refresh()
{
    if (!bar_) return;
    if (!manager_) {
        for (const auto& action : actions_) if (action) refreshCommandAction(nullptr, action, {});
        for (const auto& action : windowActions_) if (action) { action->setEnabled(false); action->setVisible(false); }
        if (windowListSeparator_) windowListSeparator_->setVisible(false);
        return;
    }
    const QPointer<ApplicationCommandMenus> retained(this);
    const auto previousCount = actions_.size();
    const bool removed = removeUnusedContributions();
    if (!retained || !manager_ || !bar_) return;
    addContributions(); if (!retained || !manager_ || !bar_) return;
    const auto context = manager_->context(window_);
    bool availabilityChanged = false;
    const auto actions = actions_;
    for (const auto& action : actions) {
        if (action) refreshCommandAction(manager_, action, context);
        if (!retained || !manager_ || !bar_) return;
    }
    refreshWindowList();
    for (auto* rootAction : bar_->actions()) if (auto* menu = rootAction->menu()) {
        tidy(menu); const auto rootActions = menu->actions();
        const bool available = std::any_of(rootActions.cbegin(), rootActions.cend(),
            [](QAction* action) { return !action->isSeparator() && action->isVisible(); });
        availabilityChanged |= rootAction->property("commandMenuAvailable").toBool() != available;
        rootAction->setProperty("commandMenuAvailable", available);
        if (!available) rootAction->setVisible(false);
    }
    if ((removed || previousCount != actions_.size() || availabilityChanged) && structureChanged) structureChanged();
}

} // namespace window_system
