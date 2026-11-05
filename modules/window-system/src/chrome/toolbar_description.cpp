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

namespace {
// Each choice runs a toolbar command with its arguments, as in Widgets.
MenuItemDescription toolbarCommand(QString id, QString title, QString command, QVariantMap arguments,
                                   std::optional<bool> checked = std::nullopt)
{
    MenuItemDescription item;
    item.id = std::move(id);
    item.commandId = std::move(command);
    item.action.title = std::move(title);
    item.action.checkable = checked.has_value();
    item.action.checked = checked;
    item.arguments = std::move(arguments);
    return item;
}
MenuItemDescription separator()
{
    MenuItemDescription item;
    item.kind = MenuItemDescription::Kind::Separator;
    return item;
}
} // namespace

QVector<MenuItemDescription> describeToolbarAreaMenu(const ToolbarEditor& editor)
{
    const bool locked = editor.toolbarsLocked();
    QVector<MenuItemDescription> result{toolbarCommand(QStringLiteral("LockStripsAction"),
        QCoreApplication::translate("WindowSystem", "Lock toolbars"), QStringLiteral("window.toggleStripsLock"),
        {{"locked", !locked}}, locked), separator()};
    for (const auto& id : editor.toolbarIds()) {
        const auto state = editor.toolbarState(id);
        const bool visible = state && state->visible;
        result.append(toolbarCommand(QStringLiteral("StripVisibility/") + id, editor.toolbarTitle(id),
            QStringLiteral("window.toolbar.setVisible"), {{"strip", id}, {"visible", !visible}}, visible));
    }
    return result;
}

QVector<MenuItemDescription> describeToolbarOptions(
    const ToolbarEditor& editor, const QString& strip, const QVector<MenuItemDescription>& buttons)
{
    const auto state = editor.toolbarState(strip);
    if (!state) return {};
    QVector<MenuItemDescription> result{toolbarCommand(QStringLiteral("StripResetWidthAction"),
        QCoreApplication::translate("WindowSystem", "Reset size"), QStringLiteral("window.toolbar.resetSize"), {{"strip", strip}})};
    if (editor.isToolbarSizingEditable(strip))
        result.append(toolbarCommand(QStringLiteral("StripElasticAction"), QCoreApplication::translate("WindowSystem", "Flexible space"),
            QStringLiteral("window.toolbar.setSizing"), {{"strip", strip}, {"sizing", state->elastic ? "content" : "elastic"}}, state->elastic));
    MenuItemDescription customize;
    customize.kind = MenuItemDescription::Kind::Submenu;
    customize.id = QStringLiteral("StripCustomizeMenu");
    customize.action.title = QCoreApplication::translate("WindowSystem", "Add or remove buttons");
    if (buttons.isEmpty()) {
        const bool shown = !state->hiddenItems.contains(QStringLiteral("content"));
        customize.children.append(toolbarCommand(QStringLiteral("content"), editor.toolbarTitle(strip),
            QStringLiteral("window.toolbar.setItemVisible"), {{"strip", strip}, {"item", "content"}, {"visible", !shown}}, shown));
    }
    for (const auto& button : buttons) {
        const bool shown = !state->hiddenItems.contains(button.id);
        auto item = toolbarCommand(button.id, button.action.title, QStringLiteral("window.toolbar.setItemVisible"),
            {{"strip", strip}, {"item", button.id}, {"visible", !shown}}, shown);
        item.iconName = button.iconName;
        customize.children.append(std::move(item));
    }
    result.append(std::move(customize));
    return result;
}

} // namespace window_system
