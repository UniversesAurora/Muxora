// SPDX-License-Identifier: MIT
#pragma once

#include "tab_layout.hpp"

namespace window_system {

enum class TabDragConstraint { HorizontalRow, VerticalRow, CompactTile };

struct TabDragArea {
    QSize size;
    QPoint origin;
    int rowHeight = 0;
    int verticalInset = 0;
};

// The pointer and grab are already in the target strip's logical coordinates.
// Local labels, incoming windows and folder blocks use the same constraints;
// source ownership and normalized anchor selection stay with their controller.
[[nodiscard]] int tabDragRowTop(const TabLayout& layout, const TabDragArea& area, QPoint pointer);
[[nodiscard]] QRect tabDragRect(const TabLayout& layout, const TabDragArea& area,
                               QPoint pointer, QSize size, QPoint grab, TabDragConstraint constraint);

} // namespace window_system
