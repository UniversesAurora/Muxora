// SPDX-License-Identifier: MIT
#pragma once

#include <QList>
#include <QRect>
#include <QRegion>
#include <QSize>
#include <algorithm>
#include <limits>

namespace window_system {

// QWidget and QScreen geometries are already in logical coordinates. A union
// preserves placements spanning adjacent monitors, including negative origins.
inline QRect normalizeWindowPlacement(QRect desired, const QList<QRect>& available,
                                     QSize minimumSize)
{
    QRegion desktop;
    for (const auto& bounds : available) if (bounds.isValid()) desktop += bounds;
    if (desktop.isEmpty() || QRegion(desired).subtracted(desktop).isEmpty()) return desired;

    QRect target;
    qint64 bestOverlap = -1;
    qint64 bestDistance = std::numeric_limits<qint64>::max();
    for (const auto& bounds : available) {
        if (!bounds.isValid()) continue;
        const auto overlap = desired.intersected(bounds);
        const qint64 area = qint64(overlap.width()) * overlap.height();
        const qint64 dx = std::max({qint64(bounds.left()) - desired.right(),
                                   qint64(desired.left()) - bounds.right(), qint64(0)});
        const qint64 dy = std::max({qint64(bounds.top()) - desired.bottom(),
                                   qint64(desired.top()) - bounds.bottom(), qint64(0)});
        const qint64 distance = dx * dx + dy * dy;
        if (area > bestOverlap || (area == bestOverlap && distance < bestDistance)) {
            target = bounds; bestOverlap = area; bestDistance = distance;
        }
    }
    const QSize fittingSize(std::max(minimumSize.width(), std::min(desired.width(), target.width())),
                            std::max(minimumSize.height(), std::min(desired.height(), target.height())));
    desired.setSize(fittingSize);
    // When the desktop is smaller than the widget's minimum, its top-left and
    // caption remain reachable even though fitting every pixel is impossible.
    desired.moveLeft(std::clamp(desired.left(), target.left(),
                               std::max(target.left(), target.right() - desired.width() + 1)));
    desired.moveTop(std::clamp(desired.top(), target.top(),
                              std::max(target.top(), target.bottom() - desired.height() + 1)));
    return desired;
}

} // namespace window_system
