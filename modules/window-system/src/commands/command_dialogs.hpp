// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/command_registry.hpp>

class QWidget;

namespace window_system {

class WindowManager;

void openCommandPalette(WindowManager* manager, const CommandContext& context, QWidget* parent);
void openWindowSelector(WindowManager* manager, WindowId source, QWidget* parent);
void openBehaviorSettings(WindowManager* manager, QWidget* parent);
void openShortcutEditor(WindowManager* manager, QWidget* parent);

} // namespace window_system
