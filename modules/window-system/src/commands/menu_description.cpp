// SPDX-License-Identifier: MIT
#include "menu_description.hpp"
#include <window_system/window_manager.hpp>
#include <QPointer>
#include <QCoreApplication>
#include <QRegularExpression>
#include "localization/display_names.hpp"
#include "style/tab_appearance_p.hpp"
#include <algorithm>

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

namespace {

using Node = MenuItemDescription;
using Nodes = QVector<Node>;

Nodes describeItems(const QVector<CommandMenuItem>& items, WindowManager* manager,
                    const CommandContext& context, const QStringList& parentPath = {})
{
    const QPointer<WindowManager> owner(manager);
    Nodes result;
    int ordinal = 0;
    for (const auto& item : items) {
        Node node;
        node.id = item.id;
        node.order = item.order ? item.order : ++ordinal * 10;
        node.sortTitle = item.command ? item.id : item.title;
        if (item.command) {
            node.commandId = item.id;
            const auto action = describeCommandAction(manager, item.id, context);
            if (!owner) return {};
            if (action) node.action = *action;
            else { node.action.visible = false; node.action.enabled = false; }
        } else if (!item.id.isEmpty()) {
            node.kind = Node::Kind::Submenu;
            node.path = parentPath + QStringList{item.pathKey};
            node.action.title = QCoreApplication::translate("WindowSystem", item.title.toUtf8().constData());
            node.children = describeItems(item.children, manager, context, node.path);
            if (!owner) return {};
        } else node.kind = Node::Kind::Separator;
        result.append(std::move(node));
    }
    return result;
}

void tidy(Nodes& nodes)
{
    bool previous = false;
    Node* pending = nullptr;
    for (auto& node : nodes) {
        if (node.kind == Node::Kind::Submenu) {
            tidy(node.children);
            node.action.visible = std::any_of(node.children.cbegin(), node.children.cend(), [](const auto& child) {
                return child.kind != Node::Kind::Separator && child.action.visible;
            });
        }
        if (node.kind == Node::Kind::Separator) {
            node.action.visible = false;
            pending = &node;
        } else if (node.action.visible) {
            if (previous && pending) pending->action.visible = true;
            pending = nullptr;
            previous = true;
        }
    }
}

void appendContextContributions(Nodes& nodes, CommandMenuSurface surface,
                                WindowManager* manager, const CommandContext& context)
{
    struct Contribution { Command command; ContextMenuPlacement placement; };
    QVector<Contribution> contributions;
    const auto target = surface == CommandMenuSurface::Tab ? ContextMenuTarget::Tab
        : surface == CommandMenuSurface::Group ? ContextMenuTarget::Group
        : surface == CommandMenuSurface::Pane ? ContextMenuTarget::Pane : ContextMenuTarget::Window;
    const auto contentType = manager->tabOptions(context.tab).contentType;
    for (const auto& command : manager->commands().commands())
        for (const auto& placement : command.contextMenus)
            if (placement.target == target && (placement.contentType.isEmpty() || placement.contentType == contentType))
                contributions.append({command, placement});
    std::stable_sort(contributions.begin(), contributions.end(), [](const auto& left, const auto& right) {
        if (left.placement.order != right.placement.order) return left.placement.order < right.placement.order;
        return left.command.id < right.command.id;
    });
    if (!contributions.isEmpty()) {
        Node separator;
        separator.kind = Node::Kind::Separator;
        nodes.append(std::move(separator));
    }
    const QPointer<WindowManager> owner(manager);
    for (const auto& contribution : contributions) {
        auto resolved = describeItems({CommandMenuItem::action(contribution.command.id)}, manager, context);
        if (!owner) return;
        auto* destination = &nodes;
        QStringList path;
        for (const auto& title : contribution.placement.path) {
            path.append(title);
            auto child = std::find_if(destination->begin(), destination->end(), [&path](const auto& node) {
                return node.contribution && node.kind == Node::Kind::Submenu && node.path == path;
            });
            if (child == destination->end()) {
                Node menu;
                menu.kind = Node::Kind::Submenu;
                menu.id = QStringLiteral("ExtensionContextMenu");
                menu.action.title = title;
                menu.path = path;
                menu.contribution = true;
                destination->append(std::move(menu));
                child = destination->end() - 1;
            }
            destination = &child->children;
        }
        *destination += resolved;
    }
}

void appendTabColors(Nodes& nodes, WindowManager* manager, const CommandContext& context)
{
    for (auto& node : nodes) if (node.commandId == QStringLiteral("tab.color.set")) {
        node.kind = Node::Kind::Submenu;
        node.menuId = QStringLiteral("TabColorChoices");
        node.menuTitle = QCoreApplication::translate("WindowSystem", "Set tab color");
        const auto* tab = manager->model().tab(context.tab);
        const auto add = [&node, tab](QString title, QString id) {
            Node choice;
            choice.kind = Node::Kind::TabColor;
            choice.id = id;
            choice.commandId = QStringLiteral("tab.color.set");
            choice.arguments.insert(QStringLiteral("color"), id);
            choice.action.title = std::move(title);
            choice.action.checkable = true;
            choice.action.checked = tab && tab->colorId == id;
            node.children.append(std::move(choice));
        };
        add(QCoreApplication::translate("WindowSystem", "Default color"), {});
        for (const auto& preset : manager->tabAppearance().colors) add(tabColorDisplayName(preset), preset.id);
    }
}

bool orderedBefore(const Node& left, const Node& right)
{
    if (left.order != right.order) return left.order < right.order;
    const int titleOrder = QString::localeAwareCompare(left.sortTitle, right.sortTitle);
    return titleOrder ? titleOrder < 0 : left.id < right.id;
}
Node& insertOrdered(Nodes& nodes, Node node)
{
    const auto before = std::find_if(nodes.cbegin(), nodes.cend(), [&node](const auto& existing) {
        return orderedBefore(node, existing);
    });
    return *nodes.insert(before, std::move(node));
}
Nodes& applicationPath(Nodes& roots, const QStringList& path)
{
    auto* destination = &roots;
    QStringList partial;
    for (const auto& title : path) {
        partial.append(title);
        auto found = std::find_if(destination->begin(), destination->end(), [&partial](const auto& node) {
            return node.kind == Node::Kind::Submenu && node.path == partial;
        });
        if (found != destination->end()) destination = &found->children;
        else {
            Node menu;
            menu.kind = Node::Kind::Submenu;
            menu.id = QStringLiteral("extension.") + partial.join('.');
            menu.action.title = commandCategoryDisplayName(title);
            menu.sortTitle = QString(title).remove(QRegularExpression(QStringLiteral("\\(&[A-Za-z]\\)")));
            menu.path = partial;
            menu.order = 500;
            menu.contribution = true;
            destination = &insertOrdered(*destination, std::move(menu)).children;
        }
    }
    return *destination;
}
void appendApplicationContributions(Nodes& nodes, CommandMenuSurface surface,
                                    WindowManager* manager, const CommandContext& context)
{
    const QPointer<WindowManager> owner(manager);
    const auto declared = declaredMenuCommands(surface);
    const auto commands = manager->commands().commands();
    for (const auto& definition : commands) {
        const auto exclusion = surface == CommandMenuSurface::FloatingApplication
            ? floatingMenuExclusion(definition.id) : applicationMenuExclusion(definition.id);
        if (declared.contains(definition.id) || !exclusion.isEmpty()) continue;
        auto resolved = describeItems({CommandMenuItem::action(definition.id)}, manager, context);
        if (!owner) return;
        if (!manager->commands().find(definition.id)) continue;
        auto path = definition.menuPath;
        if (path.isEmpty()) {
            const auto category = definition.category.isEmpty() ? QStringLiteral("other") : definition.category;
            const QStringList core{QStringLiteral("file"), QStringLiteral("view"), QStringLiteral("layout"),
                QStringLiteral("window"), QStringLiteral("tools"), QStringLiteral("help")};
            path = core.contains(category) ? QStringList{category, QStringLiteral("extensions")}
                : QStringList{QStringLiteral("extensions"), category};
        }
        auto& node = resolved.first();
        node.order = definition.menuOrder;
        node.sortTitle = definition.title;
        node.contribution = true;
        insertOrdered(applicationPath(nodes, path), std::move(node));
    }
}
void appendWindowList(Nodes& nodes, WindowManager* manager)
{
    auto& menu = applicationPath(nodes, {QStringLiteral("window")});
    Node separator;
    separator.kind = Node::Kind::Separator;
    separator.id = QStringLiteral("WindowListSeparator");
    separator.order = 1000000;
    menu.append(std::move(separator));
    const auto order = manager->model().windowOrder();
    const auto active = manager->context().window;
    const auto append = [manager, active, &menu](WindowId id, const QString& prefix, bool floating) {
        Node node;
        node.kind = Node::Kind::Window;
        node.id = id.toString(QUuid::WithoutBraces);
        node.commandId = QStringLiteral("window.activate");
        node.arguments.insert(QStringLiteral("targetWindow"), id);
        node.tooltip = manager->windowCaption(id);
        node.action.title = prefix + node.tooltip;
        node.action.checkable = true;
        node.action.checked = active == id;
        node.iconName = floating ? QStringLiteral("detach") : QStringLiteral("window");
        node.depth = floating ? 1 : 0;
        node.order = 1000000;
        menu.append(std::move(node));
    };
    QHash<FamilyId, QVector<WindowId>> children;
    for (const auto id : order) {
        const auto* record = manager->model().window(id);
        if (record && record->kind == WindowKind::Floating) children[record->familyId].append(id);
    }
    int familyIndex = 0;
    for (const auto id : order) {
        const auto* record = manager->model().window(id);
        if (!record || record->kind != WindowKind::Workbench) continue;
        ++familyIndex;
        append(id, QStringLiteral("%1  ").arg(familyIndex), false);
        int childIndex = 0;
        for (const auto child : children.value(record->familyId))
            append(child, QStringLiteral("    %1.%2  ").arg(familyIndex).arg(++childIndex), true);
    }
}

} // namespace

QVector<MenuItemDescription> describeContextMenu(
    CommandMenuSurface surface, WindowManager* manager, const CommandContext& context)
{
    if (!manager) return {};
    const QPointer<WindowManager> owner(manager);
    auto result = describeItems(commandMenuDefinition(surface), manager, context);
    if (!owner) return {};
    appendContextContributions(result, surface, manager, context);
    if (!owner) return {};
    if (surface == CommandMenuSurface::Tab || surface == CommandMenuSurface::Group)
        appendTabColors(result, manager, context);
    tidy(result);
    return result;
}

QVector<MenuItemDescription> describeApplicationMenu(
    CommandMenuSurface surface, WindowManager* manager, WindowId window)
{
    if (!manager) return {};
    Q_ASSERT(surface == CommandMenuSurface::Application || surface == CommandMenuSurface::FloatingApplication);
    const QPointer<WindowManager> owner(manager);
    const auto context = manager->context(window);
    auto result = describeItems(commandMenuDefinition(surface), manager, context);
    if (!owner) return {};
    appendApplicationContributions(result, surface, manager, context);
    if (!owner) return {};
    appendWindowList(result, manager);
    tidy(result);
    return result;
}

QVector<MenuItemDescription> describeTabCreationMenu(WindowManager* manager, WindowId window, GroupId group)
{
    if (!manager) return {};
    const auto* target = manager->model().group(group);
    if (!target || target->windowId != window) return {};
    auto context = manager->context(window);
    context.group = group;
    context.tab = target->activeTab;
    const auto* tab = manager->model().tab(context.tab);
    context.pane = tab ? tab->activePane : PaneId{};
    const QPointer<WindowManager> owner(manager);
    const auto choices = manager->tabCreationCommands();
    QVector<MenuItemDescription> result;
    for (const auto& id : choices) {
        const auto* found = manager->commands().find(id);
        if (!found) continue;
        const Command definition = *found;
        const bool enabled = !definition.enabled || definition.enabled(context);
        if (!owner) return {};
        const auto* current = manager->model().group(group);
        if (!current || current->windowId != window) return {};
        MenuItemDescription item;
        item.kind = MenuItemDescription::Kind::TabCreation;
        item.id = id;
        item.commandId = id;
        item.action.title = definition.title;
        item.action.enabled = enabled;
        QStringList shortcuts;
        for (const auto& binding : definition.bindings)
            shortcuts.append(binding.sequence.toString(QKeySequence::NativeText));
        item.action.shortcut = shortcuts.join(QStringLiteral(" / "));
        result.append(std::move(item));
    }
    return result;
}

QVector<MenuItemDescription> describeTabPresentationMenu(
    WindowManager* manager, const CommandContext& context, bool autoHide)
{
    if (!manager) return {};
    const auto* group = manager->model().group(context.group);
    autoHide = autoHide || (group && group->location == GroupLocation::AutoHide);
    const bool pinned = group && group->location == GroupLocation::Pinned;
    const QPointer<WindowManager> owner(manager);
    QVector<MenuItemDescription> result;
    const auto addFlag = [manager, &result, &context, &owner](const QString& id) {
        const auto description = describeCommandAction(manager, id, context);
        if (!owner) return false;
        MenuItemDescription item;
        item.id = id;
        item.commandId = id;
        if (description) item.action = *description;
        else { item.action.visible = false; item.action.enabled = false; }
        result.append(std::move(item));
        return true;
    };
    if (autoHide) {
        for (const auto* id : {"presentation.toggleSidebarIcons", "presentation.toggleSidebarNumbers", "presentation.toggleSidebarTitleClipping"})
            if (!addFlag(QString::fromLatin1(id))) return {};
        return result;
    }
    for (const auto* id : {pinned ? "presentation.toggleSidebarIcons" : "presentation.toggleIcons", "presentation.toggleNewTabButton",
                           pinned ? "presentation.toggleSidebarNumbers" : "presentation.toggleNumbers", "presentation.toggleTitleClipping"})
        if (!addFlag(QString::fromLatin1(id))) return {};
    const auto appearance = manager->tabAppearance();
    const auto current = pinned ? appearance.pinnedPlacement : appearance.centralPlacement;
    const auto addChoices = [&result](QString title, QString id, const QString& command, const QString& argument,
                                      const QStringList& values, const QStringList& labels, int checked) {
        MenuItemDescription menu;
        menu.kind = MenuItemDescription::Kind::Submenu;
        menu.id = std::move(id);
        menu.action.title = std::move(title);
        for (int index = 0; index < values.size(); ++index) {
            MenuItemDescription choice;
            choice.id = command + '.' + values.at(index);
            choice.commandId = command;
            choice.arguments.insert(argument, values.at(index));
            choice.action.title = labels.at(index);
            choice.action.checkable = true;
            choice.action.checked = index == checked;
            menu.children.append(std::move(choice));
        }
        result.append(std::move(menu));
    };
    MenuItemDescription separator;
    separator.kind = MenuItemDescription::Kind::Separator;
    result.append(separator);
    if (current == TabBarPlacement::Top || current == TabBarPlacement::Bottom) {
        QStringList flags{QStringLiteral("presentation.toggleMultiline")};
        if (appearance.multiline) flags.append(QStringLiteral("presentation.toggleAllRows"));
        if (appearance.stickyTabsOnSeparateRow) {
            flags.append(QStringLiteral("presentation.toggleStickyMultiline"));
            if (appearance.stickyMultiline) flags.append(QStringLiteral("presentation.toggleStickyAllRows"));
        }
        for (const auto& id : flags) if (!addFlag(id)) return {};
        if (appearance.multiline || appearance.stickyMultiline) {
            addChoices(QCoreApplication::translate("WindowSystem", "Expanded tab rows"), QStringLiteral("TabRowExpansionMenu"),
                QStringLiteral("presentation.setRowExpansion"), QStringLiteral("mode"), tabRowExpansionNames(),
                {QCoreApplication::translate("WindowSystem", "Push the content"), QCoreApplication::translate("WindowSystem", "Overlay the content")},
                int(appearance.rowExpansion));
            if (!addFlag(QStringLiteral("presentation.toggleWheelExpandRows"))) return {};
        }
    }
    addChoices(QCoreApplication::translate("WindowSystem", "Tab bar position"), QStringLiteral("TabBarPlacementMenu"),
        pinned ? QStringLiteral("presentation.setPinnedPlacement") : QStringLiteral("presentation.setCentralPlacement"),
        QStringLiteral("position"), tabBarPlacementNames(),
        {QCoreApplication::translate("WindowSystem", "Top"), QCoreApplication::translate("WindowSystem", "Bottom"),
         QCoreApplication::translate("WindowSystem", "Left"), QCoreApplication::translate("WindowSystem", "Right")}, int(current));
    result.append(std::move(separator));
    return result;
}

} // namespace window_system
