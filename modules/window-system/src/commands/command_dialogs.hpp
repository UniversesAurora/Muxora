// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/command_registry.hpp>
#include <window_system/floating_panel.hpp>

class QWidget;

namespace window_system {

class WindowManager;
struct TabOptions;

void openCommandPalette(WindowManager* manager, const CommandContext& context, QWidget* parent,
                        const FloatingPanelOptions& options = {});
void openWindowSelector(WindowManager* manager, WindowId source, QWidget* parent,
                        const FloatingPanelOptions& options = {});
void openBehaviorSettings(WindowManager* manager, QWidget* parent);
void openShortcutEditor(WindowManager* manager, QWidget* parent);
[[nodiscard]] bool isInternalSettingsContentType(const QString& type);
[[nodiscard]] QWidget* createInternalSettingsContent(WindowManager* manager, const TabOptions& options);

} // namespace window_system
