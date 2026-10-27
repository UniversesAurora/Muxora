// SPDX-License-Identifier: MIT
#pragma once

#include <QPoint>
#include <QRect>
#include <QSize>
#include <algorithm>

namespace window_system {

// A point held inside a label, independent of its later presentation size.
// Source-less offsets support callers which explicitly provide local pixels.
struct DragAnchor {
    QPoint offset;
    QSize sourceSize;

    [[nodiscard]] static DragAnchor capture(QPoint point, QRect label)
    {
        const auto local = point - label.topLeft();
        return {{std::clamp(local.x(), 0, std::max(0, label.width() - 1)),
                 std::clamp(local.y(), 0, std::max(0, label.height() - 1))}, label.size()};
    }
    [[nodiscard]] QPoint forSize(QSize size, bool centered = false) const
    {
        const auto coordinate = [centered](int value, int source, int target) {
            if (target <= 1) return 0;
            if (centered) return (target - 1) / 2;
            if (source <= 1) return std::clamp(value, 0, target - 1);
            const qint64 bounded = std::clamp(value, 0, source - 1);
            return int((bounded * (target - 1) + (source - 1) / 2) / (source - 1));
        };
        return {coordinate(offset.x(), sourceSize.width(), size.width()),
                coordinate(offset.y(), sourceSize.height(), size.height())};
    }
};

// Press coordinates establish ownership and the grab. The latest event point
// positions the label; reaching the threshold must not replace the press.
struct DragStart {
    QPoint pressGlobal;
    QPoint currentGlobal;
    DragAnchor anchor;

    [[nodiscard]] static DragStart capture(QPoint global, QPoint local, QRect label)
    {
        return {global, global, DragAnchor::capture(local, label)};
    }
    [[nodiscard]] DragStart movedTo(QPoint global) const
    {
        auto result = *this; result.currentGlobal = global; return result;
    }
    [[nodiscard]] bool crossedThreshold(QPoint global, int distance) const
    {
        return (global - pressGlobal).manhattanLength() >= distance;
    }
};

} // namespace window_system
