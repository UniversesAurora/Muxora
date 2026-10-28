// SPDX-License-Identifier: MIT
#pragma once

#include "style/elevation.hpp"
#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGTextureMaterial>
#include <memory>
#include <span>

namespace window_system::quick {

// Render-thread ownership shared by body perimeters and local anchor patches.
// Capacity is fixed; Qt 6.10 changes the live vertex count without reallocating.
class ShadowNode final : public QSGGeometryNode {
public:
    explicit ShadowNode(int quads)
        : mesh_(QSGGeometry::defaultAttributes_TexturedPoint2D(), quads * 6), capacity_(quads)
    {
        mesh_.setDrawingMode(QSGGeometry::DrawTriangles);
        mesh_.setVertexDataPattern(QSGGeometry::DynamicPattern);
        material_.setFiltering(QSGTexture::Linear);
        setGeometry(&mesh_); setMaterial(&material_);
    }

    bool syncResource(QQuickWindow& window, const QImage& image, quint64 revision)
    {
        if (texture_ && resourceRevision_ == revision) return false;
        texture_.reset(window.createTextureFromImage(image));
        Q_ASSERT(texture_);
        material_.setTexture(texture_.get());
        imageSize_ = image.size(); resourceRevision_ = revision; geometryDirty_ = true;
        markDirty(QSGNode::DirtyMaterial);
        return true;
    }

    bool syncGeometry(std::span<const ElevationPatchQuad> quads, quint64 revision)
    {
        if (!geometryDirty_ && geometryRevision_ == revision) return false;
        Q_ASSERT(quads.size() <= size_t(capacity_));
        mesh_.setVertexCount(int(quads.size()) * 6);
        auto* output = mesh_.vertexDataAsTexturedPoint2D();
        const auto write = [&](qreal x, qreal y, qreal u, qreal v) {
            output->set(float(x), float(y), float(u / imageSize_.width()), float(v / imageSize_.height()));
            ++output;
        };
        for (const auto& quad : quads) {
            const auto& d = quad.target; const auto& s = quad.sourcePixels;
            write(d.left(), d.top(), s.left(), s.top());
            write(d.right(), d.top(), s.right(), s.top());
            write(d.left(), d.bottom(), s.left(), s.bottom());
            write(d.left(), d.bottom(), s.left(), s.bottom());
            write(d.right(), d.top(), s.right(), s.top());
            write(d.right(), d.bottom(), s.right(), s.bottom());
        }
        geometryRevision_ = revision; geometryDirty_ = false;
        markDirty(QSGNode::DirtyGeometry);
        return true;
    }

private:
    QSGGeometry mesh_;
    QSGTextureMaterial material_;
    std::unique_ptr<QSGTexture> texture_;
    QSize imageSize_;
    int capacity_;
    quint64 resourceRevision_ = 0;
    quint64 geometryRevision_ = 0;
    bool geometryDirty_ = true;
};

} // namespace window_system::quick
