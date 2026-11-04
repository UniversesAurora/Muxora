// SPDX-License-Identifier: MIT
#pragma once

#include <QVector>
#include <span>

namespace window_system {

inline constexpr int stripHandleWidth = 12;
inline constexpr int stripOptionsWidth = 12;

// Measured by the presentation in its current logical coordinates. A row's
// normalized sizing policy allows at most one owner of the spare space.
struct StripWidthInput {
    int minimumWidth = 0;
    int preferredWidth = 0;
    bool elastic = false;
    bool naturalWidthPriority = false;
};

[[nodiscard]] QVector<int> stripRowWidths(std::span<const StripWidthInput> strips, int available);

struct StripRowLayout {
    QVector<int> widths;
    QVector<int> positions;
};

[[nodiscard]] StripRowLayout layoutStripRow(std::span<const StripWidthInput> strips,
                                           int width, int margin, int spacing);

struct StripPolicyInput {
    bool elastic = false;
    bool elasticAllowed = true;
    bool elasticRequired = false;
    bool naturalWidthPriority = false;
    bool hasRequestedWidth = false;
};

// Preferred is either a row index or -1. Hidden strips remain part of the
// policy: hiding a host-required owner must not make a second one possible.
[[nodiscard]] int stripElasticOwner(std::span<const StripPolicyInput> strips, int preferred = -1);
[[nodiscard]] int stripWidthPriorityOwner(std::span<const StripPolicyInput> strips);

} // namespace window_system
