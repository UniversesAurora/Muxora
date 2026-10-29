// SPDX-License-Identifier: MIT
#pragma once

#include "tab_foreground_frame.hpp"

namespace window_system {

// Both planes share one GUI-state capture. Coordinates remain local to the
// strip; a renderer places its foreground in the window's stacking plane.
struct TabPresentationFrame {
    QSize size;
    std::shared_ptr<const TabStripFrame> base;
    std::shared_ptr<const TabForegroundFrame> foreground;
};

} // namespace window_system
