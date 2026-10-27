// SPDX-License-Identifier: MIT
#include "tab_wheel_controller.hpp"

#include <limits>

namespace window_system {

int TabWheelController::laneAt(const TabLayout& layout, QPoint point, bool vertical)
{
    if (!vertical) {
        for (int lane = 0; lane < layout.laneList.size(); ++lane) {
            const auto& region = layout.laneList.at(lane);
            if (!region.vertical && region.viewport.contains(point)) return lane;
        }
    }
    int nearest = -1, distance = std::numeric_limits<int>::max();
    for (int lane = 0; lane < layout.laneList.size(); ++lane) {
        const auto viewport = layout.laneList.at(lane).viewport;
        if (viewport.contains(point)) return lane;
        const int gap = point.y() < viewport.top() ? viewport.top() - point.y()
            : point.y() > viewport.bottom() ? point.y() - viewport.bottom() : 0;
        if (gap < distance) { distance = gap; nearest = lane; }
    }
    return nearest;
}

TabWheelDecision TabWheelController::route(const TabLayout& layout, QPoint point, QPoint delta,
                                          const TabWheelPolicy& policy, qint64 timeMs)
{
    using Action = TabWheelDecision::Action;
    if (delta.isNull()) return {};
    const int directLane = laneAt(layout, point, policy.vertical);
    if (!policy.vertical && directLane >= 0 && !layout.laneList.at(directLane).vertical
        && layout.laneList.at(directLane).maximumOffset() > 0) {
        const int amount = delta.x() ? delta.x() : delta.y();
        return {Action::Scroll, directLane, -amount, true};
    }
    if (policy.collapsible && policy.wheelExpandRows && (policy.collapsed || policy.expanded)) {
        const int amount = (delta.y() ? delta.y() : delta.x()) * (policy.bottom ? -1 : 1);
        if (amount < 0 && policy.collapsed && layout.hidden > 0) return {Action::Expand};
        const auto lane = layout.laneList.value(layout.rowScrollLane);
        const bool atCollapseEdge = lane.offset == (policy.bottom ? lane.maximumOffset() : 0);
        if (amount > 0 && policy.expanded && atCollapseEdge) {
            return {rowBoundary_.allowsExit(lane.maximumOffset() > 0, timeMs)
                ? Action::Collapse : Action::HoldAtBoundary};
        }
    }
    const int lane = directLane;
    if (lane < 0) return {};
    const auto& info = layout.laneList.at(lane);
    const int amount = info.vertical ? (delta.y() ? delta.y() : delta.x())
        : (delta.x() ? delta.x() : delta.y());
    return {Action::Scroll, lane, -amount};
}

void TabWheelController::observeRowScroll(const TabLayout& layout, int lane, bool expanded, bool bottom, qint64 timeMs)
{
    if (!expanded || layout.rowScrollLane != lane) return;
    const auto info = layout.laneList.value(lane);
    rowBoundary_.observe(info.offset == (bottom ? info.maximumOffset() : 0), timeMs);
}

} // namespace window_system
