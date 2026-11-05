// SPDX-License-Identifier: MIT
#pragma once

namespace window_system {
class WindowManager;
struct CommandContext;
void registerToolbarMoveCommand(WindowManager* manager);
bool offerToolbarMoveChoices(WindowManager* manager, const CommandContext& context);
} // namespace window_system
