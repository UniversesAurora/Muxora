// SPDX-License-Identifier: MIT
#pragma once

#include <QPainterPath>
#include <QRectF>

namespace window_system {

// The edge of the larger surface facing its anchor. Coordinates belong to
// the caller's paint device; there is no widget or tab dependency here.
enum class SurfaceEdge { Top, Bottom, Left, Right };

struct SurfaceGeometryInput {
    QRectF body;
    qreal bodyRadius = 8;
    QRectF anchor;
    QRectF visibleAnchor;
    SurfaceEdge edge = SurfaceEdge::Top;
    qreal anchorRadius = 6;
    qreal shoulderRadius = 4;
    // The anchor's inward edge moves to the body as this goes from 0 to 1.
    // Peek expansion supplies 1 - its own progress, without another clock.
    qreal joinProgress = 1;
    bool adjacent = true;
    bool overlay = false;
    bool covered = false;
    qreal coveredProgress = 1;
    bool compact = false;
};

struct SurfaceGeometry {
    QRectF body;
    qreal bodyRadius = 0;
    QRectF anchor;
    SurfaceEdge edge = SurfaceEdge::Top;
    QPainterPath bodyPath;
    QPainterPath anchorPath;
    QPainterPath mergedPath;
    qreal joinStart = 0;
    qreal joinEnd = 0;
    qreal joinProgress = 0;
    bool joined = false;
    bool covered = false;
    qreal coveredProgress = 0;
    bool compact = false;
};

[[nodiscard]] SurfaceGeometry surfaceGeometry(const SurfaceGeometryInput& input);
[[nodiscard]] SurfaceGeometry translatedSurfaceGeometry(const SurfaceGeometry& geometry, QPointF offset);
[[nodiscard]] QRectF surfaceShadowClip(const SurfaceGeometry& geometry, qreal extent);

} // namespace window_system
