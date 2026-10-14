// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/layout_model.hpp>
#include <QRect>

namespace window_system {

// Coordinates belong to the containing top-level client, so moving that
// window on the desktop never changes an otherwise identical strip anchor.
struct TabViewportState {
    QRect bounds;
    QRect windowClientBounds;
    int offset = 0;
    TabId activeTab;
    bool activeFullyVisible = false;
};

} // namespace window_system
