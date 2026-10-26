// SPDX-License-Identifier: MIT
#pragma once

#include <QImage>
#include <QLineF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRect>
#include <QRegion>
#include <QTransform>
#include <QVector>

namespace window_system {

struct EdgeFade {
    QRect area;
    Qt::Edge edge;
    QLineF opacityRamp;

    // Let a scrolling surface become opaque as it meets another surface.
    // The two perpendicular ramps add, so the fade ends on the joint rather
    // than leaving an abrupt transparent-to-opaque cut there.
    void convergeTo(Qt::Edge joint, QRectF surface)
    {
        const bool horizontal = edge == Qt::LeftEdge || edge == Qt::RightEdge;
        const bool horizontalJoint = joint == Qt::LeftEdge || joint == Qt::RightEdge;
        if (horizontal == horizontalJoint || area.isEmpty() || surface.isEmpty()) return;
        const bool fromStart = edge == Qt::LeftEdge || edge == Qt::TopEdge;
        const bool jointAtEnd = joint == Qt::RightEdge || joint == Qt::BottomEdge;
        const qreal extent = horizontal ? area.width() : area.height();
        const qreal thickness = horizontal ? surface.height() : surface.width();
        const qreal along = fromStart ? (horizontal ? area.left() : area.top())
            : (horizontal ? area.right() + 1 : area.bottom() + 1);
        const qreal across = horizontal ? (jointAtEnd ? surface.top() : surface.bottom())
            : (jointAtEnd ? surface.left() : surface.right());
        const QPointF start = horizontal ? QPointF(along, across) : QPointF(across, along);
        const qreal axis = (fromStart ? 1.0 : -1.0) / extent;
        const qreal normal = (jointAtEnd ? 1.0 : -1.0) / thickness;
        const QPointF gradient = horizontal ? QPointF(axis, normal) : QPointF(normal, axis);
        const qreal squared = QPointF::dotProduct(gradient, gradient);
        opacityRamp = QLineF(start, start + gradient / squared);
    }
};

// Draw in the caller's logical coordinates. Only the faded bands need a
// transparent image; the rest goes straight to the original paint device.
// A disjoint region partition keeps crossing fades from drawing content
// twice, while successive masks multiply their alpha at the intersection.
template<class Draw>
void paintEdgeFades(QPainter& painter, QRect bounds, const QVector<EdgeFade>& fades, Draw draw)
{
    if (!painter.isActive() || bounds.isEmpty()) return;
    QRegion faded;
    for (const auto& fade : fades) faded |= fade.area.intersected(bounds);

    const QRegion direct = QRegion(bounds).subtracted(faded);
    if (!direct.isEmpty()) {
        painter.save();
        painter.setClipRegion(direct, Qt::IntersectClip);
        draw(painter);
        painter.restore();
    }
    if (faded.isEmpty()) return;

    const auto deviceTransform = painter.deviceTransform();
    const auto callerClip = painter.hasClipping() ? painter.clipPath() : QPainterPath{};
    const qreal callerOpacity = painter.opacity();
    for (const QRect& band : faded) {
        // Align the raster to physical pixels, not rounded logical pixels.
        // This also preserves a fractional-DPR glyph's subpixel phase.
        const QRect deviceBand = deviceTransform.mapRect(QRectF(band)).toAlignedRect();
        if (deviceBand.isEmpty()) continue;
        QImage image(deviceBand.size(), QImage::Format_ARGB32_Premultiplied);
        if (image.isNull()) continue;
        image.fill(Qt::transparent);
        image.setDotsPerMeterX(qRound(painter.device()->logicalDpiX() / 0.0254));
        image.setDotsPerMeterY(qRound(painter.device()->logicalDpiY() / 0.0254));

        // The image uses physical-pixel coordinates; the source device's
        // DPR is already part of deviceTransform, so it is not applied twice.
        QPainter layer(&image);
        layer.setWorldTransform(deviceTransform * QTransform::fromTranslate(-deviceBand.x(), -deviceBand.y()));
        layer.setFont(painter.font());
        layer.setPen(painter.pen());
        layer.setBrush(painter.brush());
        layer.setBrushOrigin(painter.brushOrigin());
        layer.setBackground(painter.background());
        layer.setBackgroundMode(painter.backgroundMode());
        layer.setLayoutDirection(painter.layoutDirection());
        layer.setRenderHints(painter.renderHints());
        layer.setOpacity(callerOpacity);
        layer.setClipRect(band);
        if (painter.hasClipping()) layer.setClipPath(callerClip, Qt::IntersectClip);
        layer.save();
        draw(layer);
        layer.restore();

        layer.setOpacity(1);
        layer.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        for (const auto& fade : fades) {
            if (!fade.area.intersects(band)) continue;
            const bool horizontal = fade.edge == Qt::LeftEdge || fade.edge == Qt::RightEdge;
            const bool fromStart = fade.edge == Qt::LeftEdge || fade.edge == Qt::TopEdge;
            QLinearGradient mask(horizontal ? QPointF(fade.area.left(), 0) : QPointF(0, fade.area.top()),
                                 horizontal ? QPointF(fade.area.right() + 1, 0) : QPointF(0, fade.area.bottom() + 1));
            if (!fade.opacityRamp.isNull()) {
                mask.setStart(fade.opacityRamp.p1());
                mask.setFinalStop(fade.opacityRamp.p2());
            }
            const bool increasing = !fade.opacityRamp.isNull() || fromStart;
            mask.setColorAt(0, QColor(0, 0, 0, increasing ? 0 : 255));
            mask.setColorAt(1, QColor(0, 0, 0, increasing ? 255 : 0));
            layer.fillRect(fade.area, mask);
        }
        layer.end();

        painter.save();
        painter.setClipRect(band, Qt::IntersectClip);
        // The local image already contains the caller's opacity. Composite
        // it on the same device pixels without applying the transform twice.
        painter.resetTransform();
        const QTransform toLogical = painter.deviceTransform().inverted();
        painter.setOpacity(1);
        painter.drawImage(toLogical.mapRect(QRectF(deviceBand)), image);
        painter.restore();
    }
}

} // namespace window_system
