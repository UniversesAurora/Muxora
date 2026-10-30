#pragma once
#include <window_system/quick_workspace.hpp>

class QQmlEngine;

namespace muxora {
// The preview's placeholder pages for the Quick workspace, from one compiled
// QML component. Buttons on the pages run manager commands.
window_system::QuickWorkspace::ContentFactory quickPlaceholderFactory(window_system::WindowManager& windows,
                                                                      QQmlEngine& engine);
} // namespace muxora
