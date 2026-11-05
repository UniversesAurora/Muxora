// SPDX-License-Identifier: MIT
#include "toolbar_description.hpp"
#include <window_system/window_manager.hpp>
#include <QCoreApplication>
#include <QPointer>

namespace window_system {

const QVector<QPair<QString, QString>>& workspaceToolbarCommands()
{
    static const QVector<QPair<QString, QString>> commands{
        {"host.newTab", "add"}, {"workspace.splitGroup.right", "split-horizontal"},
        {"pane.split.right", "split-vertical"}, {"workspace.detachFloating", "detach"},
        {"workbench.showCommandPalette", "command"}, {"window.settings", "settings"}, {"layout.save", "save"}};
    return commands;
}

QVector<CommandToolbarDescription> describeCommandToolbars(WindowManager* manager, WindowId window)
{
    const auto* record = manager->model().window(window);
    if (!record) return {};
    const auto context = manager->context(window);
    auto definitions = manager->toolbars();
    if (record->kind == WindowKind::Workbench) {
        CommandToolbar workspace{"workspace", QCoreApplication::translate("WindowSystem", "Workspace"), {}};
        for (const auto& command : workspaceToolbarCommands()) workspace.commands.append(command.first);
        definitions.prepend(std::move(workspace));
    }
    const QPointer<WindowManager> owner(manager);
    QVector<CommandToolbarDescription> result;
    for (auto& definition : definitions) {
        CommandToolbarDescription strip{std::move(definition), {}};
        for (const auto& id : strip.toolbar.commands) {
            const auto state = describeCommandAction(manager, id, context);
            if (!owner) return {};
            if (!state) continue;
            MenuItemDescription item;
            item.id = item.commandId = id;
            item.action = *state;
            item.iconName = QStringLiteral("command");
            if (strip.toolbar.id == QStringLiteral("workspace"))
                for (const auto& command : workspaceToolbarCommands()) if (command.first == id) { item.iconName = command.second; break; }
            item.tooltip = item.action.title + (item.action.shortcut.isEmpty() ? QString() : QStringLiteral("  ") + item.action.shortcut);
            strip.actions.append(std::move(item));
        }
        result.append(std::move(strip));
    }
    return result;
}

} // namespace window_system
