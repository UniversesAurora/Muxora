// SPDX-License-Identifier: MIT
#pragma once
#include <QString>
#include <optional>
class QMenu;
namespace window_system {
class WindowManager;
struct CommandContext;
void registerTabPresentationCommands(WindowManager* manager);
// A tab color preset chosen from a menu at the cursor; `reset` names the
// empty choice. Nothing when dismissed or when the manager went away.
std::optional<QString> pickTabColor(WindowManager* manager, const QString& reset);
void appendTabPresentationActions(QMenu* menu, WindowManager* manager, const CommandContext& captured,
                                  bool autoHide = false);
}
