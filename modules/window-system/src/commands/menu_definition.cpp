// SPDX-License-Identifier: MIT
#include "menu_definition.hpp"
#include <QCoreApplication>

namespace window_system {
namespace {

using Item = CommandMenuItem;
using Items = QVector<Item>;
Item command(QString id, int order = 0) { return Item::action(std::move(id), order); }
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
void collectCommands(const Items& items, QStringList& result)
{
    for (const auto& item : items) {
        if (item.command) result.append(item.id);
        else collectCommands(item.children, result);
    }
}

} // namespace

const QVector<CommandMenuItem>& commandMenuDefinition(CommandMenuSurface surface)
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
                command("presentation.toggleAllRows"), command("presentation.toggleStickyMultiline"), command("presentation.toggleStickyAllRows"),
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

QStringList declaredMenuCommands(CommandMenuSurface surface)
{
    QStringList result; collectCommands(commandMenuDefinition(surface), result); return result;
}
QString applicationMenuExclusion(const QString& id)
{
    if (id == "runCommands") return QCoreApplication::translate("WindowSystem", "Parameterized composite; configured in the keyboard shortcuts editor");
    if (id == "presentation.setCentralPlacement" || id == "presentation.setPinnedPlacement")
        return QCoreApplication::translate("WindowSystem", "The tab position command requires top/bottom; central and pinned sidebar bars are configured independently through settings or the API");
    if (id == "presentation.setRowExpansion")
        return QCoreApplication::translate("WindowSystem", "The row expansion command requires a mode; choose it from the tab bar context menu or settings");
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

} // namespace window_system
