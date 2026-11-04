// SPDX-License-Identifier: MIT
#pragma once

#include "strip_constraints.hpp"
#include <QVector>
#include <window_system/toolbar_layout.hpp>
#include <array>
#include <optional>
#include <span>

namespace window_system {

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

struct StripAreaRowInput {
    ToolbarArea area = ToolbarArea::Top;
    int height = 0;
    bool occupied = false;
};
struct StripAreasLayout {
    std::array<int, 2> heights{};
    std::array<int, 2> next{};
    // Consume occupied rows in registry order. Each area's row zero starts
    // at its outer edge, so bottom rows advance toward the top.
    int takeRow(ToolbarArea area, int height);
};
[[nodiscard]] StripAreasLayout layoutStripAreas(std::span<const StripAreaRowInput> rows);

struct StripSlotInput {
    int width = 0;
    bool visible = true;
    bool elastic = false;
    bool fixedWidth = false;
};
struct StripVacancyAdjustment {
    int index = -1;
    int requestedWidth = -1;
};
// Grow the neighbour on the anchored side, or reset it to natural width
// when the vacancy opens onto the elastic strip or the free end of the row.
[[nodiscard]] std::optional<StripVacancyAdjustment> stripVacancyAdjustment(
    std::span<const StripSlotInput> strips, int removed, int spacing);

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
