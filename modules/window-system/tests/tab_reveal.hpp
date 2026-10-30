// SPDX-License-Identifier: MIT
#pragma once

// Where a strip settles when it starts from `from` and must show tab `index`
// in full: `from` itself, or the least scrolling that keeps the tab clear of
// the edge fades (tab strip design 3.5). Uses the lane's own rule, so tests
// state the expected position rather than copying the arithmetic.

#include "docking/tab_bar.hpp"

#include <algorithm>

namespace tab_reveal {

[[nodiscard]] inline int offsetFrom(const window_system::TabBar& bar, int index, int from)
{
    const auto& layout = bar.tabLayout();
    const int laneIndex = layout.lanes.value(index, -1);
    if (laneIndex < 0) return std::clamp(from, 0, bar.maximumScrollOffset());
    auto lane = layout.laneList.at(laneIndex);
    const int shift = lane.offset - std::clamp(from, 0, lane.maximumOffset());
    auto tab = layout.tabs.at(index);
    if (lane.vertical) tab.translate(0, shift); else tab.translate(shift, 0);
    lane.offset -= shift;
    return lane.offsetToReveal(tab, bar.metrics().edgeFade);
}

} // namespace tab_reveal
