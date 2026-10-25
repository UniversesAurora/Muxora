// SPDX-License-Identifier: MIT
#pragma once

namespace window_system {

// Vertical tab strips: the default column width and the range a drag of the
// column's edge allows. The layout model (Qt Core only) checks saved widths
// against the same range as the appearance settings.
inline constexpr int minimumVerticalTabWidth = 140;
inline constexpr int maximumVerticalTabWidth = 480;
inline constexpr int defaultVerticalTabWidth = 220;

} // namespace window_system
