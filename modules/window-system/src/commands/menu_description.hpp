// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/command_registry.hpp>
#include "menu_definition.hpp"

namespace window_system {
class WindowManager;

struct CommandActionDescription {
    QString title;
    QString shortcut;
    bool visible = true;
    bool enabled = true;
    bool checkable = false;
    std::optional<bool> checked;
};

[[nodiscard]] std::optional<CommandActionDescription> describeCommandAction(
    WindowManager* manager, const QString& id, const CommandContext& context);

struct MenuItemDescription {
    enum class Kind { Command, Submenu, Separator, TabColor, Window, TabCreation };
    Kind kind = Kind::Command;
    QString id;
    QString commandId;
    QString menuId;
    QString menuTitle;
    CommandActionDescription action;
    QVector<MenuItemDescription> children;
    QVariantMap arguments;
    QStringList path;
    QString sortTitle;
    QString tooltip;
    QString iconName;
    int order = 0;
    int depth = 0;
    bool contribution = false;
};

[[nodiscard]] QVector<MenuItemDescription> describeContextMenu(
    CommandMenuSurface surface, WindowManager* manager, const CommandContext& context);
[[nodiscard]] QVector<MenuItemDescription> describeApplicationMenu(
    CommandMenuSurface surface, WindowManager* manager, WindowId window);
[[nodiscard]] QVector<MenuItemDescription> describeTabCreationMenu(
    WindowManager* manager, WindowId window, GroupId group);

} // namespace window_system
