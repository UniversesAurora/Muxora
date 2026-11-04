// SPDX-License-Identifier: MIT
#pragma once

#include <QVector>
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

} // namespace window_system
