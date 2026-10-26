// SPDX-License-Identifier: MIT
#include "interface_appearance_commands.hpp"
#include <window_system/interface_appearance.hpp>
#include <window_system/window_manager.hpp>
#include <QCoreApplication>
#include <algorithm>

namespace window_system {
void registerInterfaceAppearanceCommands(WindowManager* manager)
{
    const auto add = [manager](const char* id, const char* title, QKeySequence shortcut, int step, bool zoom) {
        manager->commands().add({id, QCoreApplication::translate("WindowSystem", title), "view", shortcut,
            [](const CommandContext&) { return InterfaceAppearanceController::applicationInstance() != nullptr; },
            [step, zoom](const CommandContext&) {
                auto* controller = InterfaceAppearanceController::applicationInstance();
                if (!controller) return false;
                auto value = controller->appearance();
                if (zoom) value.zoomLevel = step ? std::clamp(value.zoomLevel + step, InterfaceAppearance::minimumZoomLevel, InterfaceAppearance::maximumZoomLevel) : 0;
                else value.fontSize = step ? std::clamp(value.fontSize + step, InterfaceAppearance::minimumFontSize, InterfaceAppearance::maximumFontSize) : InterfaceAppearance{}.fontSize;
                return controller->setAppearance(value);
            }});
    };
    add("workbench.action.zoomIn", QT_TRANSLATE_NOOP("WindowSystem", "Zoom in"), QKeySequence("Ctrl+="), 1, true);
    manager->commands().addBinding("workbench.action.zoomIn", {QKeySequence("Ctrl++"), {}});
    add("workbench.action.zoomOut", QT_TRANSLATE_NOOP("WindowSystem", "Zoom out"), QKeySequence("Ctrl+-"), -1, true);
    add("workbench.action.zoomReset", QT_TRANSLATE_NOOP("WindowSystem", "Reset zoom"), QKeySequence("Ctrl+0"), 0, true);
    add("workbench.action.increaseInterfaceFontSize", QT_TRANSLATE_NOOP("WindowSystem", "Increase interface font size"), {}, 1, false);
    add("workbench.action.decreaseInterfaceFontSize", QT_TRANSLATE_NOOP("WindowSystem", "Decrease interface font size"), {}, -1, false);
    add("workbench.action.resetInterfaceFontSize", QT_TRANSLATE_NOOP("WindowSystem", "Reset interface font size"), {}, 0, false);
}
} // namespace window_system
