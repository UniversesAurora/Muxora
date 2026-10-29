// SPDX-License-Identifier: MIT
#pragma once

#include "surface_geometry.hpp"
#include "style/elevation.hpp"
#include "style/label_presence.hpp"
#include <QColor>
#include <QPainterPath>
#include <QRect>
#include <QRectF>
#include <QRegion>
#include <QVector>
#include <optional>

class QPainter;

namespace window_system {

struct TabSurfaceAnchor {
    SurfaceGeometry geometry;
    ElevationProfile elevation;
    bool lifted = false;
    bool foreground = false;
    LabelPresencePose presence;
};

struct TabSurfaceExtension {
    QPainterPath path;
    QColor fill;
    qreal opacity = 0;
};

// Bounds and body belong to the same drawing plane. The line already includes
// the overlay alpha; opacity affects only the shadow, not the panel fill.
struct TabColumnSurface {
    QRect bounds;
    QRectF body;
    qreal radius = 0;
    qreal opacity = 0;
    QColor fill;
    QColor line;
    ElevationStyle elevation;
};

struct TabGroupSurfaceFrame {
    QRect groupBounds;
    QRectF body;
    qreal radius = 0;
    ElevationProfile elevation;
    ElevationStyle style;
    QVector<TabSurfaceAnchor> anchors;
    std::optional<TabColumnSurface> column;
    TabSurfaceExtension extension;
    QRect bounds;
    QRegion paintRegion;
};

void prepareTabGroupSurfaceFrame(TabGroupSurfaceFrame& frame);
void paintTabColumnSurface(QPainter& painter, ElevationRenderer& renderer,
                           const TabColumnSurface& column, QPoint offset = {});
void paintTabGroupSurface(QPainter& painter, ElevationRenderer& renderer,
                         const TabGroupSurfaceFrame& frame, QPoint offset, const QRegion& exposed);

} // namespace window_system
