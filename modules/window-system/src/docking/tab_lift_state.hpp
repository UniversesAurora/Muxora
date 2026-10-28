// SPDX-License-Identifier: MIT
#pragma once

#include "drag_start.hpp"
#include "tab_drag_geometry.hpp"
#include <optional>

namespace window_system {

struct TabLiftTarget {
    QSize size;
    // A folder is grabbed by its header, even when it carries many rows.
    QSize anchorSize;
    TabDragConstraint constraint;
    bool centered = false;
};

class TabLiftState final {
public:
    enum class Change { None, Lift, Move, Lower };
    [[nodiscard]] Change update(std::optional<QPoint> pointer, DragAnchor anchor)
    {
        if (!pointer) {
            if (!pointer_) return Change::None;
            clear(); return Change::Lower;
        }
        const auto change = pointer_ ? Change::Move : Change::Lift;
        pointer_ = pointer; anchor_ = anchor; return change;
    }
    void clear() { pointer_.reset(); }
    [[nodiscard]] bool active() const { return pointer_.has_value(); }
    [[nodiscard]] QRect rect(const TabLayout& layout, const TabDragArea& area, const TabLiftTarget& target) const
    {
        return pointer_ ? rectAt(*pointer_, layout, area, target) : QRect{};
    }
    [[nodiscard]] QRect rectAt(QPoint pointer, const TabLayout& layout,
                               const TabDragArea& area, const TabLiftTarget& target) const
    {
        return pointer_ ? tabDragRect(layout, area, pointer, target.size,
            anchor_.forSize(target.anchorSize, target.centered), target.constraint) : QRect{};
    }
private:
    std::optional<QPoint> pointer_;
    DragAnchor anchor_;
};

} // namespace window_system
