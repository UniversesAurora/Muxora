// SPDX-License-Identifier: MIT
#pragma once

#include "path_geometry.hpp"
#include <QColor>
#include <QSGGeometryNode>
#include <QSGVertexColorMaterial>

namespace window_system::quick {

// Retained fill and coverage fringe shared by paths and surface composition.
// Input geometry is prepared on the GUI thread; this node owns its GPU state.
class PathNode final : public QSGGeometryNode {
public:
    PathNode() : mesh_(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0, 0, QSGGeometry::UnsignedIntType)
    {
        mesh_.setDrawingMode(QSGGeometry::DrawTriangles);
        mesh_.setVertexDataPattern(QSGGeometry::DynamicPattern);
        mesh_.setIndexDataPattern(QSGGeometry::DynamicPattern);
        setGeometry(&mesh_); setMaterial(&material_);
    }

    bool sync(const PathGeometryCache& cache, const QColor& color) { return sync(cache.geometry(), cache.generation(), color); }
    bool sync(const PathGeometry& source, quint64 revision, const QColor& color)
    {
        if (generation_ == revision && color_ == color) return false;
        const auto vertices = source.fill.vertices.size() + source.fringe.vertices.size();
        const auto indices = source.fill.indices.size() + source.fringe.indices.size();
        if (mesh_.vertexCount() != vertices || mesh_.indexCount() != indices) mesh_.allocate(int(vertices), int(indices));
        auto* vertex = mesh_.vertexDataAsColoredPoint2D();
        auto* index = mesh_.indexDataAsUInt();
        quint32 base = 0;
        for (const auto* part : {&source.fill, &source.fringe}) {
            for (const auto& point : part->vertices) {
                const qreal alpha = color.alphaF() * point.coverage;
                (vertex++)->set(point.x, point.y, uchar(qRound(color.redF() * alpha * 255)),
                    uchar(qRound(color.greenF() * alpha * 255)), uchar(qRound(color.blueF() * alpha * 255)), uchar(qRound(alpha * 255)));
            }
            for (const auto at : part->indices) *index++ = base + at;
            base += quint32(part->vertices.size());
        }
        generation_ = revision; color_ = color;
        markDirty(QSGNode::DirtyGeometry);
        return true;
    }

private:
    QSGGeometry mesh_;
    QSGVertexColorMaterial material_;
    quint64 generation_ = 0;
    QColor color_;
};

} // namespace window_system::quick
