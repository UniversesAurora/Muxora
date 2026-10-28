// SPDX-License-Identifier: MIT
#include "tab_incoming_state.hpp"
#include <utility>

namespace window_system {

TabIncomingState::Change TabIncomingState::replace(TabDragVisual visual, QPoint pointer, const TabLayout& layout,
                                                  const TabDragArea& area, TabDragConstraint constraint)
{
    if (visual_.empty() && visual.empty()) return Change::None;
    const auto change = visual_.empty() ? Change::Enter : visual.empty() ? Change::Leave : Change::Update;
    visual_ = std::move(visual); pointer_ = pointer;
    reorderOffset_.reset();
    if (!visual_.empty() && !layout.gap.isEmpty()) {
        QRect gap;
        for (const auto& cell : layout.gap) gap |= cell;
        reorderOffset_ = rect(layout, area, constraint).center() - (gap.center() + area.origin);
    }
    return change;
}

QRect TabIncomingState::rect(const TabLayout& layout, const TabDragArea& area, TabDragConstraint constraint) const
{
    return rectAt(pointer_, layout, area, constraint);
}

QRect TabIncomingState::rectAt(QPoint pointer, const TabLayout& layout,
                              const TabDragArea& area, TabDragConstraint constraint) const
{
    if (visual_.empty() || visual_.size.isEmpty()) return {};
    auto grab = visual_.anchor;
    if (visual_.sourceAnchor) {
        const auto reference = visual_.active.isEmpty() ? QRect(QPoint(), visual_.size) : visual_.active;
        grab = reference.topLeft() + visual_.sourceAnchor->forSize(reference.size(), constraint == TabDragConstraint::CompactTile);
    }
    return tabDragRect(layout, area, pointer, visual_.size, grab, constraint);
}

} // namespace window_system
