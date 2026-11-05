// SPDX-License-Identifier: MIT
#pragma once

#include "commands/menu_description.hpp"
#include "toolbar_editor.hpp"
#include <window_system/custom_chrome.hpp>

namespace window_system {

struct CommandToolbarDescription {
    CommandToolbar toolbar;
    QVector<MenuItemDescription> actions;
};

[[nodiscard]] const QVector<QPair<QString, QString>>& workspaceToolbarCommands();
[[nodiscard]] QVector<CommandToolbarDescription> describeCommandToolbars(WindowManager* manager, WindowId window);
// The toolbar area's menu: lock the toolbars, then show or hide each strip.
[[nodiscard]] QVector<MenuItemDescription> describeToolbarAreaMenu(const ToolbarEditor& editor);
// A strip's options after its overflowing buttons: reset its size, flexible
// space where the row allows it, and its buttons (or its content) to show.
[[nodiscard]] QVector<MenuItemDescription> describeToolbarOptions(
    const ToolbarEditor& editor, const QString& strip, const QVector<MenuItemDescription>& buttons);

} // namespace window_system
