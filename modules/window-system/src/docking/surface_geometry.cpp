// SPDX-License-Identifier: MIT
#include "surface_geometry.hpp"

#include <QTransform>
#include <algorithm>

namespace window_system {
namespace {
QTransform toTop(SurfaceEdge edge)
{
    switch (edge) {
    case SurfaceEdge::Top: return {};
    case SurfaceEdge::Bottom: return QTransform(1, 0, 0, -1, 0, 0);
    case SurfaceEdge::Left: return QTransform(0, 1, 1, 0, 0, 0);
    case SurfaceEdge::Right: return QTransform(0, -1, 1, 0, 0, 0);
    }
    return {};
}

QPainterPath anchorOutline(const QRectF& rect, qreal outerRadius, qreal inwardRadius,
                           qreal leftShoulder, qreal rightShoulder,
                           bool clippedLeft, bool clippedRight)
{
    const qreal left = rect.left(), right = rect.right();
    const qreal top = rect.top(), bottom = rect.bottom();
    const qreal radius = std::min({outerRadius, rect.width() / 2, rect.height() / 2});
    const qreal topLeft = clippedLeft ? 0 : radius;
    const qreal topRight = clippedRight ? 0 : radius;
    const qreal bottomLeft = clippedLeft ? 0 : std::min(inwardRadius, radius);
    const qreal bottomRight = clippedRight ? 0 : std::min(inwardRadius, radius);
    QPainterPath path;
    path.moveTo(left + bottomLeft - leftShoulder, bottom);
    path.quadTo(left, bottom, left, bottom - bottomLeft - leftShoulder);
    path.lineTo(left, top + topLeft);
    path.quadTo(left, top, left + topLeft, top);
    path.lineTo(right - topRight, top);
    path.quadTo(right, top, right, top + topRight);
    path.lineTo(right, bottom - bottomRight - rightShoulder);
    path.quadTo(right, bottom, right - bottomRight + rightShoulder, bottom);
    path.closeSubpath();
    return path;
}
} // namespace

SurfaceGeometry surfaceGeometry(const SurfaceGeometryInput& input)
{
    SurfaceGeometry result;
    result.body = input.body;
    result.bodyRadius = std::max(qreal(0), std::min({input.bodyRadius, input.body.width() / 2, input.body.height() / 2}));
    result.edge = input.edge;
    result.covered = input.covered && !input.overlay;
    result.coveredProgress = result.covered ? std::clamp(input.coveredProgress, qreal(0), qreal(1)) : 0;
    result.compact = input.compact;
    if (!input.body.isEmpty()) result.bodyPath.addRoundedRect(input.body, result.bodyRadius, result.bodyRadius);
    result.mergedPath = result.bodyPath;
    if (input.anchor.isEmpty()) return result;

    const auto transform = toTop(input.edge);
    const auto inverse = transform.inverted();
    const QRectF body = transform.mapRect(input.body);
    const QRectF original = transform.mapRect(input.anchor);
    const QRectF visible = input.visibleAnchor.isNull() ? original : original.intersected(transform.mapRect(input.visibleAnchor));
    if (visible.isEmpty()) return result;
    QRectF anchor = visible;
    const qreal start = std::max(visible.left(), body.left() + result.bodyRadius);
    const qreal end = std::min(visible.right(), body.right() - result.bodyRadius);
    const bool eligible = input.adjacent && !input.overlay && result.coveredProgress < 1 && !input.compact
        && !body.isEmpty() && end > start;
    result.joinProgress = eligible ? std::clamp(input.joinProgress, qreal(0), qreal(1)) : 0;
    result.joined = result.joinProgress > 0;
    result.joinStart = start;
    result.joinEnd = std::max(start, end);
    const bool clippedLeft = visible.left() > original.left() + 0.01;
    const bool clippedRight = visible.right() < original.right() - 0.01;
    qreal leftShoulder = 0, rightShoulder = 0;
    if (result.joined) {
        anchor.setBottom(visible.bottom() + (body.top() - visible.bottom()) * result.joinProgress);
        const qreal shoulder = input.shoulderRadius * result.joinProgress;
        if (!clippedLeft && visible.left() - shoulder >= body.left() + result.bodyRadius)
            leftShoulder = shoulder;
        if (!clippedRight && visible.right() + shoulder <= body.right() - result.bodyRadius)
            rightShoulder = shoulder;
    }
    const qreal inward = input.anchorRadius * (1 - result.joinProgress) * (1 - result.coveredProgress);
    QPainterPath outline = anchorOutline(anchor, input.anchorRadius, inward, leftShoulder,
                                         rightShoulder, clippedLeft, clippedRight);
    if (result.joined && anchor.bottom() > visible.bottom()
        && (start > visible.left() || end < visible.right())) {
        // Approach a body's straight edge smoothly when its rounded corner
        // leaves no room for a shoulder. A rectangular extension crop would
        // punch a step into the first/last anchor's outside contour.
        const QRectF bounds = outline.boundingRect();
        const qreal leftTaper = !clippedLeft && start > visible.left() && leftShoulder <= 0
            ? std::min(input.anchorRadius, start - visible.left()) * result.joinProgress : 0;
        const qreal rightTaper = !clippedRight && end < visible.right() && rightShoulder <= 0
            ? std::min(input.anchorRadius, visible.right() - end) * result.joinProgress : 0;
        const qreal leftTop = visible.bottom() - leftTaper;
        const qreal rightTop = visible.bottom() - rightTaper;
        QPainterPath allowed, extension;
        allowed.addRect(QRectF(bounds.left() - 1, bounds.top() - 1,
                               bounds.width() + 2, std::min(leftTop, rightTop) - bounds.top() + 1));
        const qreal extensionStart = leftShoulder > 0 ? visible.left() - leftShoulder : start;
        const qreal extensionEnd = rightShoulder > 0 ? visible.right() + rightShoulder : end;
        extension.moveTo(bounds.left(), leftTop - 0.01);
        if (leftTaper > 0)
            extension.quadTo(visible.left(), anchor.bottom(), extensionStart, anchor.bottom());
        else {
            extension.lineTo(extensionStart, leftTop - 0.01);
            extension.lineTo(extensionStart, anchor.bottom());
        }
        extension.lineTo(extensionEnd, anchor.bottom());
        if (rightTaper > 0)
            extension.quadTo(visible.right(), anchor.bottom(), bounds.right(), rightTop - 0.01);
        else {
            extension.lineTo(extensionEnd, rightTop - 0.01);
            extension.lineTo(bounds.right(), rightTop - 0.01);
        }
        extension.lineTo(bounds.right(), std::min(leftTop, rightTop) - 0.01);
        extension.lineTo(bounds.left(), std::min(leftTop, rightTop) - 0.01);
        extension.closeSubpath();
        outline = outline.intersected(allowed.united(extension));
    }
    result.anchorPath = inverse.map(outline);
    if (result.joined) {
        QPainterPath bodyRectangle;
        bodyRectangle.addRect(input.body);
        result.anchorPath = result.anchorPath.subtracted(bodyRectangle.subtracted(result.bodyPath));
    }
    result.anchor = inverse.mapRect(anchor);
    if (result.joined && result.joinProgress >= 1) {
        // A tiny overlap removes a numerical seam when two filled paths
        // share exactly an edge. It lies entirely inside the larger surface.
        QPainterPath overlap;
        overlap.addRect(QRectF(start, body.top() - 0.01, end - start, 0.02));
        result.mergedPath = result.bodyPath.united(result.anchorPath).united(inverse.map(overlap));
    } else result.mergedPath = result.bodyPath.united(result.anchorPath);
    return result;
}

SurfaceGeometry translatedSurfaceGeometry(const SurfaceGeometry& geometry, QPointF offset)
{
    SurfaceGeometry result = geometry;
    result.body.translate(offset);
    result.anchor.translate(offset);
    result.bodyPath.translate(offset);
    result.anchorPath.translate(offset);
    result.mergedPath.translate(offset);
    const qreal axis = geometry.edge == SurfaceEdge::Top || geometry.edge == SurfaceEdge::Bottom ? offset.x() : offset.y();
    result.joinStart += axis;
    result.joinEnd += axis;
    return result;
}

QRectF surfaceShadowClip(const SurfaceGeometry& geometry, qreal extent)
{
    QRectF clip = geometry.anchorPath.boundingRect().adjusted(-extent, -extent, extent, extent);
    if (!geometry.covered || geometry.coveredProgress <= 0) return clip;
    const qreal outside = extent * (1 - geometry.coveredProgress);
    switch (geometry.edge) {
    case SurfaceEdge::Top: clip.setBottom(geometry.anchor.bottom() + outside); break;
    case SurfaceEdge::Bottom: clip.setTop(geometry.anchor.top() - outside); break;
    case SurfaceEdge::Left: clip.setRight(geometry.anchor.right() + outside); break;
    case SurfaceEdge::Right: clip.setLeft(geometry.anchor.left() - outside); break;
    }
    return clip;
}

} // namespace window_system
