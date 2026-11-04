// SPDX-License-Identifier: MIT
#include "menu_description.hpp"
#include <window_system/window_manager.hpp>
#include <QPointer>

namespace window_system {

std::optional<CommandActionDescription> describeCommandAction(
    WindowManager* manager, const QString& id, const CommandContext& context)
{
    if (!manager) return std::nullopt;
    const auto* found = manager->commands().find(id);
    if (!found) return std::nullopt;
    // Host predicates may unregister commands or destroy their manager.
    const Command definition = *found;
    const QPointer<WindowManager> owner(manager);
    const bool enabled = !definition.enabled || definition.enabled(context);
    if (!owner) return std::nullopt;
    CommandActionDescription result;
    result.title = definition.title;
    result.enabled = enabled;
    const auto* window = manager->model().window(context.window);
    const auto* group = manager->model().group(context.group);

    if (id == "window.promote") result.visible = window && window->kind == WindowKind::Floating;
    if (id == "window.demote") result.visible = window && window->kind == WindowKind::Workbench && enabled;
    if (id == "window.redock" || id == "window.reassignFamily") result.visible = window && window->kind == WindowKind::Floating && enabled;
    if (id == "window.drag.selectTarget" || id == "window.drag.cancel") result.visible = enabled;
    if (id == "workspace.pinTab" || id == "workspace.pinGroup") result.visible = group && group->location == GroupLocation::AutoHide;
    if (id == "tab.stick" || id == "tab.unstick") result.visible = enabled;
    if (id == "tab.peek.setSize" || id == "tab.peek.resetSize") result.visible = group && group->location == GroupLocation::AutoHide;
    if (id == "workspace.unpinTab" || id == "workspace.unpinGroup") result.visible = group && group->location == GroupLocation::Pinned;
    if (id == "workspace.pinEdge" || id == "workspace.unpinEdge" || id == "workspace.restoreGroupToCenter")
        result.visible = group && group->location != GroupLocation::Central;
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
    result.checkable = appearanceChecked.has_value() || id == "window.tabBars.toggle" || id == "window.sideBars.toggle"
        || id == "window.toolbars.toggle" || id == "window.statusBar.toggle" || id == "window.zen.toggle"
        || (id.startsWith("window.fullscreen.") && id != "window.fullscreen.exit")
        || id == "window.toggleStripsLock" || id == "tab.topBar.setSharing" || id == "tab.bottomBar.setSharing";
    if (appearanceChecked) result.checked = *appearanceChecked;
    if (id == "window.tabBars.toggle") result.checked = manager->tabBarsVisible(context.window);
    if (id == "window.sideBars.toggle") result.checked = manager->sideBarsVisible(context.window);
    if (id == "window.toolbars.toggle") result.checked = manager->toolbarsVisible(context.window);
    if (id == "window.statusBar.toggle") result.checked = manager->statusBarVisible(context.window);
    if (id == "window.zen.toggle") result.checked = manager->zenMode(context.window);
    if (id == "window.fullscreen.toggle") result.checked = manager->viewMode(context.window) == ViewMode::WindowFullscreen;
    if (id == "window.fullscreen.group") result.checked = manager->viewMode(context.window) == ViewMode::GroupFullscreen;
    if (id == "window.fullscreen.pane") result.checked = manager->viewMode(context.window) == ViewMode::PaneFullscreen;
    if (id == "window.toggleStripsLock") result.checked = manager->toolbarsLocked(context.window);
    if (id == "tab.topBar.setSharing" || id == "tab.bottomBar.setSharing") {
        const auto options = manager->tabOptions(context.tab);
        result.checked = manager->model().tab(context.tab) && (id == "tab.topBar.setSharing"
            ? options.topBarSharing : options.bottomBarSharing) == PaneBarSharing::Shared;
    }
    QStringList shortcuts;
    for (const auto& binding : definition.bindings)
        shortcuts.append(binding.sequence.toString(QKeySequence::NativeText));
    result.shortcut = shortcuts.join(QStringLiteral(" / "));
    return result;
}

} // namespace window_system
