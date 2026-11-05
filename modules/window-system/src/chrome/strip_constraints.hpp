// SPDX-License-Identifier: MIT
#pragma once

namespace window_system {

inline constexpr int stripHandleWidth = 12;
inline constexpr int stripOptionsWidth = 12;
inline constexpr int stripDefaultMinimumContentWidth = 12;
// Between strips in a row, and the least height of a row.
inline constexpr int stripSpacing = 4;
inline constexpr int compactRowHeight = 28;

enum class StripSizing { Content, Elastic };
struct StripConstraints {
    bool compressible = true;
    bool elasticAllowed = true;
    int minimumContentWidth = stripDefaultMinimumContentWidth;
    // A required elastic strip owns its row's spare space. Moving an
    // ordinary elastic strip into that row makes the latter content-sized.
    bool elasticRequired = false;
    // Always exactly its natural width: it never absorbs a neighbour's
    // width, takes a dragged or saved width, or leaves blank space.
    bool fixedWidth = false;
};

} // namespace window_system
