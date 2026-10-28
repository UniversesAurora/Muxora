// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/layout_model.hpp>
#include <QRect>
#include <optional>

namespace window_system {

// Coordinates belong to the containing top-level client, so moving that
// window on the desktop never changes an otherwise identical strip anchor.
struct TabViewportState {
    QRect bounds;
    QRect windowClientBounds;
    int offset = 0;
    int stickyOffset = 0;
    int rowsOffset = 0;
    TabId activeTab;
    bool activeFullyVisible = false;
    std::optional<bool> rowsExpanded;
};

struct TabViewportReflow {
    bool ready = false;
    bool restore = false;
    bool ensureActive = false;
    int offset = 0;
    int stickyOffset = 0;
    int rowsOffset = 0;
};

// The host restores these offsets before revealing the active tab against
// the resulting layout. Content limits and fade safety belong to TabLayout.
[[nodiscard]] inline TabViewportReflow tabViewportReflow(
    const TabViewportState& old, const TabViewportState& current, bool horizontal,
    bool barVisible, QRect activeContentRect, int stickyInset)
{
    if (current.bounds.isEmpty()) return {.ready = !barVisible};
    const bool newActive = !old.activeTab.isNull() && current.activeTab != old.activeTab;
    TabViewportReflow result{true, true, newActive || old.activeFullyVisible,
                            old.offset, old.stickyOffset, old.rowsOffset};
    if (!horizontal) return result;
    if (old.bounds.isEmpty()) {
        // Unshown geometry never establishes a displayed anchor.
        result.ensureActive = newActive;
        return result;
    }
    // Desktop translation alone is unrelated to strip scrolling.
    const int windowLeftShift = current.windowClientBounds.width() != old.windowClientBounds.width()
        ? current.windowClientBounds.left() - old.windowClientBounds.left() : 0;
    result.offset += current.bounds.left() - old.bounds.left() + windowLeftShift;
    if (old.activeFullyVisible && old.activeTab == current.activeTab && !activeContentRect.isEmpty()) {
        const int rightLimit = activeContentRect.right() - current.bounds.width() + 1;
        result.offset = current.bounds.width() - stickyInset >= activeContentRect.width()
            ? qBound(rightLimit, result.offset, activeContentRect.left() - stickyInset)
            : activeContentRect.left() - stickyInset;
    }
    return result;
}

} // namespace window_system
