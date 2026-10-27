// SPDX-License-Identifier: MIT
#pragma once

#include "tab_layout.hpp"
#include "style/scroll_boundary_guard.hpp"

namespace window_system {

struct TabWheelPolicy {
    bool vertical = false;
    bool bottom = false;
    bool collapsible = false;
    bool wheelExpandRows = false;
    bool collapsed = false;
    bool expanded = false;
};

struct TabWheelDecision {
    enum class Action { Ignore, Scroll, Expand, Collapse, HoldAtBoundary };
    Action action = Action::Ignore;
    int lane = -1;
    int delta = 0;
    // A horizontal lane owns the wheel even at its end, so it must not
    // fall through to the shared row-expansion gesture.
    bool horizontalPriority = false;
};

// Routes wheel input without applying it or retaining a layout. The host owns
// scrolling, animation, expansion and callback lifetime checks. Coordinates
// are in the tab area's space, matching TabLayout rather than a widget.
class TabWheelController final {
public:
    [[nodiscard]] static int laneAt(const TabLayout& layout, QPoint point, bool vertical);
    [[nodiscard]] TabWheelDecision route(const TabLayout& layout, QPoint point, QPoint delta,
                                        const TabWheelPolicy& policy, qint64 timeMs);
    // Called after the host applies a scroll, including a smooth-scroll frame.
    void observeRowScroll(const TabLayout& layout, int lane, bool expanded, bool bottom, qint64 timeMs);
    void reset() { rowBoundary_.reset(); }

private:
    ScrollBoundaryGuard rowBoundary_{250};
};

} // namespace window_system
