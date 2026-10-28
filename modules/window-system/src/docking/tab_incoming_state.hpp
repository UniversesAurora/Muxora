// SPDX-License-Identifier: MIT
#pragma once

#include "tab_drag_geometry.hpp"
#include "tab_drag_visual.hpp"

namespace window_system {

// Incoming data owns its carrier descriptors. The adapter supplies its current
// layout and constraint, so geometry never queries a source or destination UI.
class TabIncomingState final {
public:
    enum class Change { None, Enter, Update, Leave };
    [[nodiscard]] Change replace(TabDragVisual visual, QPoint pointer, const TabLayout& layout,
                                 const TabDragArea& area, TabDragConstraint constraint);
    [[nodiscard]] const TabDragVisual& visual() const { return visual_; }
    [[nodiscard]] QRect rect(const TabLayout& layout, const TabDragArea& area, TabDragConstraint constraint) const;
    [[nodiscard]] QRect rectAt(QPoint pointer, const TabLayout& layout,
                               const TabDragArea& area, TabDragConstraint constraint) const;
    [[nodiscard]] std::optional<QPoint> reorderOffset() const { return reorderOffset_; }
    void resetReorderOffset() { reorderOffset_.reset(); }
private:
    TabDragVisual visual_;
    QPoint pointer_;
    // Relative to the accepted gap at the last publication. Off-center entry
    // must not reorder again before the carrier moves relative to that gap.
    std::optional<QPoint> reorderOffset_;
};

} // namespace window_system
