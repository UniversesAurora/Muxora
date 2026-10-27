// SPDX-License-Identifier: MIT
#include "tab_drag_geometry.hpp"
#include "tab_wheel_controller.hpp"

#include <algorithm>

namespace window_system {

int tabDragRowTop(const TabLayout& layout, const TabDragArea& area, QPoint pointer)
{
    if (layout.rowScrollLane >= 0) {
        return area.origin.y() + layout.laneList.at(layout.rowScrollLane)
            .rowTopAt(pointer.y() - area.origin.y(), area.rowHeight);
    }
    if (layout.laneList.size() > 1) {
        const int lane = std::max(0, TabWheelController::laneAt(layout, pointer - area.origin, false));
        return area.origin.y() + layout.laneList.at(lane).viewport.top();
    }
    return area.origin.y();
}

QRect tabDragRect(const TabLayout& layout, const TabDragArea& area,
                 QPoint pointer, QSize size, QPoint grab, TabDragConstraint constraint)
{
    QRect result(pointer - grab, size);
    if (constraint == TabDragConstraint::HorizontalRow) {
        result.moveTop(tabDragRowTop(layout, area, pointer));
        return result;
    }
    result.moveTop(std::clamp(result.top(), area.origin.y(),
        std::max(area.origin.y(), area.size.height() - size.height())));
    if (constraint == TabDragConstraint::CompactTile) {
        result.moveLeft(std::clamp(result.left(), area.verticalInset,
            std::max(area.verticalInset, area.size.width() - area.verticalInset - size.width())));
    } else result.moveLeft(area.verticalInset);
    return result;
}

} // namespace window_system
