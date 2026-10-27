// SPDX-License-Identifier: MIT
#include "path_item.hpp"

#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGVertexColorMaterial>
#include <algorithm>

namespace window_system::quick {
namespace {
class PathNode final : public QSGGeometryNode {
public:
    PathNode() : mesh(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0, 0, QSGGeometry::UnsignedIntType)
    {
        mesh.setDrawingMode(QSGGeometry::DrawTriangles);
        mesh.setVertexDataPattern(QSGGeometry::DynamicPattern);
        mesh.setIndexDataPattern(QSGGeometry::DynamicPattern);
        setGeometry(&mesh);
        setMaterial(&colorMaterial);
    }

    void sync(const PathGeometry& source, quint64 revision, const QColor& color)
    {
        if (generation == revision && fill == color) return;
        const auto vertices = source.fill.vertices.size() + source.fringe.vertices.size();
        const auto indices = source.fill.indices.size() + source.fringe.indices.size();
        if (mesh.vertexCount() != vertices || mesh.indexCount() != indices)
            mesh.allocate(int(vertices), int(indices));
        auto* vertex = mesh.vertexDataAsColoredPoint2D();
        auto* index = mesh.indexDataAsUInt();
        quint32 base = 0;
        for (const auto* part : {&source.fill, &source.fringe}) {
            for (const auto& point : part->vertices) {
                const qreal alpha = color.alphaF() * point.coverage;
                vertex->set(point.x, point.y, uchar(qRound(color.redF() * alpha * 255)),
                    uchar(qRound(color.greenF() * alpha * 255)), uchar(qRound(color.blueF() * alpha * 255)),
                    uchar(qRound(alpha * 255)));
                ++vertex;
            }
            for (const auto at : part->indices) *index++ = base + at;
            base += quint32(part->vertices.size());
        }
        generation = revision;
        fill = color;
        markDirty(QSGNode::DirtyGeometry);
    }

private:
    QSGGeometry mesh;
    QSGVertexColorMaterial colorMaterial;
    quint64 generation = 0;
    QColor fill;
};
} // namespace

PathItem::PathItem(QQuickItem* parent, PathAntialiasing mode) : QQuickItem(parent), antialiasing_(mode)
{
    setFlag(ItemHasContents);
    prepare({});
}

bool PathItem::prepare(const QPainterPath& path)
{
    PathGeometryOptions options;
    options.antialiasing = antialiasing_ == PathAntialiasing::Coverage;
    options.devicePixelRatio = window() ? window()->effectiveDevicePixelRatio() : 1;
    cache_.update(path, options);
    update();
    return cache_.geometry().valid();
}

bool PathItem::setPath(const QPainterPath& path)
{
    const bool accepted = prepare(path);
    // Do not retain a rejected, potentially oversized input for a later DPR
    // change. A rejected mesh stays empty until the caller supplies a new path.
    path_ = accepted ? path : QPainterPath{};
    return accepted;
}

void PathItem::setFill(const QColor& fill)
{
    if (fill_ == fill) return;
    fill_ = fill;
    update();
}

void PathItem::itemChange(ItemChange change, const ItemChangeData& data)
{
    QQuickItem::itemChange(change, data);
    if (change == ItemSceneChange || change == ItemDevicePixelRatioHasChanged) prepare(path_);
}

QSGNode* PathItem::updatePaintNode(QSGNode* previous, UpdatePaintNodeData*)
{
    if (!cache_.geometry().valid() || cache_.geometry().fill.indices.isEmpty() || fill_.alpha() == 0) {
        delete previous;
        return nullptr;
    }
    auto* node = previous ? static_cast<PathNode*>(previous) : new PathNode;
    node->sync(cache_.geometry(), cache_.generation(), fill_);
    return node;
}

} // namespace window_system::quick
