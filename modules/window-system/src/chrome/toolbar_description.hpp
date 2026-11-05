// SPDX-License-Identifier: MIT
#pragma once

#include "commands/menu_description.hpp"
#include <window_system/custom_chrome.hpp>

namespace window_system {

struct CommandToolbarDescription {
    CommandToolbar toolbar;
    QVector<MenuItemDescription> actions;
};

[[nodiscard]] const QVector<QPair<QString, QString>>& workspaceToolbarCommands();
[[nodiscard]] QVector<CommandToolbarDescription> describeCommandToolbars(WindowManager* manager, WindowId window);

} // namespace window_system
