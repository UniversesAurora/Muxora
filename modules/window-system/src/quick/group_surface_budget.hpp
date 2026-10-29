// SPDX-License-Identifier: MIT
#pragma once

#include "docking/tab_group_surface.hpp"
#include "path_geometry.hpp"
#include <algorithm>
#include <cmath>

namespace window_system::quick {
namespace group_surface_budget_detail {

// One preparation's geometry and retained-image costs. Temporary coverage
// masks share the existing image limit but do not consume retained bytes.
struct Budget {
    PathGeometryOptions limits;
    qreal ratio;
    int elements = limits.maximumElements;
    qsizetype vertices = limits.maximumVertices;
    qsizetype imageBytes = elevationImageBudgetBytes;
    PathGeometryError error = PathGeometryError::None;

    bool fail(PathGeometryError reason) { error = reason; return false; }
    bool finite(qreal value)
    {
        return std::isfinite(value) || fail(PathGeometryError::NonFiniteCoordinate);
    }
    bool coordinate(qreal value)
    {
        if (!finite(value)) return false;
        return (std::abs(value) <= limits.maximumPhysicalExtent
            && std::abs(value) * ratio <= limits.maximumPhysicalExtent) || fail(PathGeometryError::CoordinateBudgetExceeded);
    }
    bool rect(const QRectF& value)
    {
        return coordinate(value.x()) && coordinate(value.y()) && coordinate(value.width()) && coordinate(value.height())
            && coordinate(value.right()) && coordinate(value.bottom());
    }
    bool profile(const ElevationProfile& value)
    {
        for (const auto* layer : {&value.key, &value.ambient})
            if (!coordinate(layer->offset) || !coordinate(layer->blur) || !finite(layer->opacity)) return false;
        return finite(value.edgeOpacity);
    }
    bool path(const QPainterPath& value)
    {
        if (value.elementCount() > elements) return fail(PathGeometryError::ElementBudgetExceeded);
        elements -= value.elementCount();
        for (int index = 0; index < value.elementCount(); ++index) {
            const auto point = value.elementAt(index);
            if (!coordinate(point.x) || !coordinate(point.y)) return false;
        }
        return true;
    }
    bool region(const QRegion& value, bool consume = true)
    {
        if (value.rectCount() > vertices / 4) return fail(PathGeometryError::VertexBudgetExceeded);
        for (const auto& area : value) if (!rect(QRectF(area))) return false;
        if (consume) vertices -= qsizetype(value.rectCount()) * 4;
        return true;
    }
    bool pixels(qreal width, qreal height, bool retained)
    {
        if (!finite(width) || !finite(height)) return false;
        if (width <= 0 || height <= 0) return true;
        if (width > limits.maximumPhysicalExtent || height > limits.maximumPhysicalExtent
            || width / ratio > limits.maximumPhysicalExtent || height / ratio > limits.maximumPhysicalExtent)
            return fail(PathGeometryError::CoordinateBudgetExceeded);
        const qreal bytes = std::ceil(width) * std::ceil(height) * 4;
        if (bytes > elevationImageBudgetBytes || (retained && bytes > imageBytes))
            return fail(PathGeometryError::WorkBudgetExceeded);
        if (retained) imageBytes -= qsizetype(bytes);
        return true;
    }
    bool image(qreal width, qreal height, bool retained)
    {
        return coordinate(width) && coordinate(height) && pixels(width * ratio, height * ratio, retained);
    }
    bool coverage(qreal width, qreal height, const ElevationProfile& value)
    {
        // coverage() prepares padding even for a layer whose opacity is zero.
        const qreal influence = std::max({qreal(0), value.key.blur + std::abs(value.key.offset),
                                          value.ambient.blur + std::abs(value.ambient.offset)});
        if (!coordinate(influence)) return false;
        const qreal padding = std::ceil(influence * ratio) + 1;
        return image((std::ceil(width * ratio) + 2 * padding) / ratio,
                     (std::ceil(height * ratio) + 2 * padding) / ratio, false);
    }
    bool body(QRectF area, qreal radius, const ElevationProfile& value, bool highContrast)
    {
        if (value.edgeOpacity <= 0 && value.key.opacity <= 0 && value.ambient.opacity <= 0) return true;
        radius = std::max(qreal(0), std::min({radius, area.width() / 2, area.height() / 2}));
        const auto margins = elevationMargins(value);
        const qreal margin = std::max({margins.left(), margins.top(), margins.bottom()});
        if (!coordinate(margin)) return false;
        const qreal spread = std::ceil(margin) + 1;
        const qreal side = 2 * std::ceil(radius + spread) + 1 + 2 * spread;
        return image(side, side, true) && (highContrast || coverage(side, side, value));
    }
};

} // namespace group_surface_budget_detail

// Check only the rendering boundary: cost, numeric safety and supported path
// size. Shadow paths may contain holes and are deliberately not triangulated.
[[nodiscard]] inline PathGeometryError validateGroupSurfaceFrame(const TabGroupSurfaceFrame& frame, qreal ratio)
{
    using Error = PathGeometryError;
    if (!std::isfinite(ratio) || ratio <= 0 || ratio > 16) return Error::InvalidOptions;
    // An empty body is an explicit request to discard the entire frame.
    if (frame.body.isEmpty()) return Error::None;
    group_surface_budget_detail::Budget budget{{}, ratio};
    if (frame.anchors.size() > budget.vertices / 4) return Error::VertexBudgetExceeded;
    budget.vertices -= frame.anchors.size() * 4;
    if (!budget.rect(frame.body) || !budget.rect(QRectF(frame.groupBounds)) || !budget.rect(QRectF(frame.bounds))
        || !budget.coordinate(frame.radius) || !budget.profile(frame.elevation) || !budget.finite(frame.style.shadowMultiplier)
        || !budget.finite(frame.extension.opacity) || !budget.path(frame.extension.path) || !budget.region(frame.paintRegion))
        return budget.error;
    for (const auto& anchor : frame.anchors) {
        const auto& geometry = anchor.geometry;
        if (!budget.rect(geometry.body) || !budget.rect(geometry.anchor) || !budget.coordinate(geometry.bodyRadius)
            || !budget.coordinate(geometry.joinStart) || !budget.coordinate(geometry.joinEnd)
            || !budget.finite(geometry.joinProgress) || !budget.finite(geometry.coveredProgress)
            || !budget.path(geometry.bodyPath) || !budget.path(geometry.anchorPath) || !budget.path(geometry.mergedPath)
            || !budget.profile(anchor.elevation) || !budget.finite(anchor.presence.opacity) || !budget.finite(anchor.presence.scale)
            || !budget.coordinate(anchor.presence.offset.x()) || !budget.coordinate(anchor.presence.offset.y())) return budget.error;
    }
    if (!budget.body(frame.body, frame.radius, frame.elevation, frame.style.highContrast)) return budget.error;
    const auto bodyMargins = elevationMargins(frame.elevation);
    QRegion completeRegions;
    qsizetype completeCount = 0;
    for (const auto& anchor : frame.anchors) {
        if (anchor.lifted || anchor.foreground || anchor.geometry.compact) continue;
        const bool complete = anchor.presence.complete();
        if (!complete && anchor.presence.opacity <= 0) continue;
        const auto margins = elevationMargins(anchor.elevation);
        const qreal margin = std::max({margins.left(), margins.top(), margins.bottom(),
                                       bodyMargins.left(), bodyMargins.top(), bodyMargins.bottom()});
        if (!budget.coordinate(margin)) return budget.error;
        const qreal extent = std::ceil(margin) + 1;
        const auto bounds = anchor.geometry.anchorPath.boundingRect().adjusted(-extent, -extent, extent, extent);
        if (!budget.rect(bounds) || !budget.rect(anchor.presence.mapRect(bounds))) return budget.error;
        if (complete) {
            const auto clip = surfaceShadowClip(anchor.geometry, extent);
            if (!budget.rect(clip)) return budget.error;
            completeRegions |= clip.toAlignedRect();
            if (!budget.region(completeRegions, false)) return budget.error;
            ++completeCount;
        }
        if (anchor.geometry.anchorPath.isEmpty()) continue;
        if (!budget.image(bounds.width(), bounds.height(), true)) return budget.error;
        if (!frame.style.highContrast) {
            if ((!anchor.geometry.bodyPath.isEmpty() && !budget.coverage(bounds.width(), bounds.height(), frame.elevation))
                || !budget.coverage(bounds.width(), bounds.height(), anchor.elevation)) return budget.error;
        }
    }
    if (completeCount > 1) {
        if (!budget.region(completeRegions)) return budget.error;
        for (const auto& band : completeRegions) {
            // Count both the final merged band and its same-sized layer.
            if (!budget.image(band.width(), band.height(), true) || !budget.image(band.width(), band.height(), true)) return budget.error;
        }
    }
    if (frame.column) {
        const auto& column = *frame.column;
        if (!budget.rect(QRectF(column.bounds)) || !budget.rect(column.body) || !budget.coordinate(column.radius)
            || !budget.finite(column.opacity) || !budget.finite(column.elevation.shadowMultiplier)) return budget.error;
        if (column.opacity <= 0) return Error::None;
        const auto shadow = elevationProfile(ElevationLevel::ColumnOverlay);
        if (!budget.profile(shadow) || !budget.region(QRegion(frame.bounds) - frame.groupBounds)
            || !budget.body(column.body, column.radius, shadow, column.elevation.highContrast)
            || !budget.body(column.body.adjusted(0.5, 0.5, -0.5, -0.5), column.radius, {{}, {}, 1}, true)) return budget.error;
    }
    return Error::None;
}

} // namespace window_system::quick
