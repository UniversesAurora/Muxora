#pragma once
#include <window_system/window_manager.hpp>

namespace muxora {
QWidget* createPlaceholder(const window_system::TabOptions& options,
                           window_system::WindowManager* windows);
void populateInitialWorkspace(window_system::WindowManager& windows,
                              window_system::WindowId window);
} // namespace muxora
