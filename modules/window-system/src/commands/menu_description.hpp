// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/command_registry.hpp>

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

} // namespace window_system
