// SPDX-License-Identifier: MIT
#pragma once
class QMenu;
namespace window_system {
class WindowManager;
struct CommandContext;
void registerTabPresentationCommands(WindowManager* manager);
void appendTabPresentationActions(QMenu* menu, WindowManager* manager, const CommandContext& captured,
                                  bool autoHide = false);
}
