// SPDX-License-Identifier: MIT
#include "tab_group_surface.hpp"

#include <QPainter>
#include <QPen>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace window_system {

void prepareTabGroupSurfaceFrame(TabGroupSurfaceFrame& frame)
{
    frame.bounds = {};
    frame.paintRegion = {};
    if (frame.body.isEmpty()) return;
    QRectF bounds = frame.body.marginsAdded(elevationMargins(elevationProfile(ElevationLevel::BodyRaised)));
    for (const auto& anchor : frame.anchors)
        bounds |= anchor.presence.mapRect(anchor.geometry.anchorPath.boundingRect().marginsAdded(elevationMargins(anchor.elevation)));
    if (frame.column)
        bounds |= frame.column->body.marginsAdded(elevationMargins(elevationProfile(ElevationLevel::ColumnOverlay)));
    frame.bounds = bounds.toAlignedRect();
    const int inset = qCeil(frame.radius) + 1;
    frame.paintRegion = QRegion(frame.bounds) - frame.body.toRect().adjusted(inset, inset, -inset, -inset);
}

void paintTabColumnSurface(QPainter& painter, ElevationRenderer& renderer,
                           const TabColumnSurface& column, QPoint offset)
{
    if (column.opacity <= 0) return;
    painter.save(); painter.translate(offset); painter.setRenderHint(QPainter::Antialiasing);
    QPainterPath outline; outline.addRoundedRect(column.body, column.radius, column.radius);
    QPainterPath outside; outside.addRect(column.bounds);
    painter.save(); painter.setClipPath(outside.subtracted(outline), Qt::IntersectClip);
    painter.setOpacity(column.opacity);
    renderer.paintBody(painter, column.body, column.radius, elevationProfile(ElevationLevel::ColumnOverlay), column.elevation);
    painter.restore();
    painter.setBrush(column.fill);
    painter.setPen(QPen(column.line, 1));
    painter.drawRoundedRect(column.body.adjusted(0.5, 0.5, -0.5, -0.5), column.radius, column.radius);
    painter.restore();
}

void paintTabGroupSurface(QPainter& painter, ElevationRenderer& renderer,
                         const TabGroupSurfaceFrame& frame, QPoint offset, const QRegion& exposed)
{
    if (frame.body.isEmpty() || !exposed.intersects(frame.bounds.translated(offset))) return;
    painter.save(); painter.translate(offset);
    painter.setClipRegion(frame.paintRegion, Qt::IntersectClip);
    const auto resting = [](const TabSurfaceAnchor& anchor) {
        return !anchor.lifted && !anchor.foreground && !anchor.geometry.compact;
    };
    painter.save();
    QPainterPath bodyRegion; bodyRegion.addRect(frame.bounds);
    const auto bodyMargins = elevationMargins(frame.elevation);
    for (const auto& anchor : frame.anchors) if (resting(anchor) && anchor.presence.complete()) {
        const auto margins = elevationMargins(anchor.elevation);
        const qreal extent = std::ceil(std::max({margins.left(), margins.top(), margins.bottom(),
            bodyMargins.left(), bodyMargins.top(), bodyMargins.bottom()})) + 1;
        QPainterPath local; local.addRect(surfaceShadowClip(anchor.geometry, extent));
        bodyRegion = bodyRegion.subtracted(local);
    }
    painter.setClipPath(bodyRegion, Qt::IntersectClip);
    renderer.paintBody(painter, frame.body, frame.radius, frame.elevation, frame.style);
    painter.restore();
    QVector<ElevatedAnchor> shadows;
    for (const auto& anchor : frame.anchors) if (resting(anchor) && anchor.presence.complete())
        shadows.append({anchor.geometry, anchor.elevation});
    renderer.paintAnchors(painter, shadows, frame.elevation, frame.style);
    for (const auto& anchor : frame.anchors) if (resting(anchor) && !anchor.presence.complete() && anchor.presence.opacity > 0) {
        // Transform only the additional anchor contribution; the body stays
        // in place and reuses its existing bounded shadow resource.
        painter.save();
        painter.setOpacity(painter.opacity() * anchor.presence.opacity);
        painter.translate(anchor.presence.offset); painter.scale(anchor.presence.scale, anchor.presence.scale);
        renderer.paintAnchor(painter, anchor.geometry, frame.elevation, anchor.elevation, frame.style, true);
        painter.restore();
    }
    if (frame.extension.opacity > 0) {
        painter.save(); painter.setRenderHint(QPainter::Antialiasing);
        painter.setOpacity(painter.opacity() * frame.extension.opacity);
        painter.fillPath(frame.extension.path, frame.extension.fill);
        painter.restore();
    }
    if (frame.column) {
        // The live column paints its interior. The shared plane contributes
        // only the shell that extends beyond this group's bounds.
        QPainterPath outside, group;
        outside.addRect(frame.bounds); group.addRect(frame.groupBounds);
        painter.save(); painter.setClipPath(outside.subtracted(group), Qt::IntersectClip);
        paintTabColumnSurface(painter, renderer, *frame.column);
        painter.restore();
    }
    painter.restore();
}

} // namespace window_system
