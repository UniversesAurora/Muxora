// SPDX-License-Identifier: MIT
#include "body_shadow_item.hpp"

#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGTextureMaterial>
#include <algorithm>

namespace window_system::quick {
namespace {
class BodyShadowNode final : public QSGGeometryNode {
public:
    BodyShadowNode() : mesh_(QSGGeometry::defaultAttributes_TexturedPoint2D(), 8 * 6)
    {
        mesh_.setDrawingMode(QSGGeometry::DrawTriangles);
        mesh_.setVertexDataPattern(QSGGeometry::DynamicPattern);
        material_.setFiltering(QSGTexture::Linear);
        setGeometry(&mesh_); setMaterial(&material_);
    }

    bool syncResource(QQuickWindow& window, const ElevationBodyPatch& patch, quint64 revision)
    {
        if (texture_ && revision_ == revision) return false;
        texture_.reset(window.createTextureFromImage(patch.image));
        Q_ASSERT(texture_);
        material_.setTexture(texture_.get());
        revision_ = revision;
        size_ = {};
        markDirty(QSGNode::DirtyMaterial);
        return true;
    }

    bool syncGeometry(const ElevationBodyPatch& patch, QSizeF size, qreal radius)
    {
        if (size_ == size) return false;
        auto* output = mesh_.vertexDataAsTexturedPoint2D();
        const auto write = [&](qreal x, qreal y, qreal u, qreal v) {
            output->set(float(x), float(y), float(u / patch.image.width()), float(v / patch.image.height()));
            ++output;
        };
        for (const auto& quad : patch.quads(QRectF(QPointF(), size), radius)) {
            const auto& d = quad.target;
            const auto& s = quad.sourcePixels;
            write(d.left(), d.top(), s.left(), s.top());
            write(d.right(), d.top(), s.right(), s.top());
            write(d.left(), d.bottom(), s.left(), s.bottom());
            write(d.left(), d.bottom(), s.left(), s.bottom());
            write(d.right(), d.top(), s.right(), s.top());
            write(d.right(), d.bottom(), s.right(), s.bottom());
        }
        size_ = size;
        markDirty(QSGNode::DirtyGeometry);
        return true;
    }

private:
    QSGGeometry mesh_;
    QSGTextureMaterial material_;
    std::unique_ptr<QSGTexture> texture_;
    QSizeF size_;
    quint64 revision_ = 0;
};
}

BodyShadowItem::BodyShadowItem(QQuickItem* parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents);
}

void BodyShadowItem::setShadow(qreal radius, const ElevationProfile& profile, const ElevationStyle& style)
{
    if (radius_ == radius && profile_ == profile && style_ == style) return;
    radius_ = radius; profile_ = profile; style_ = style;
    resourceDirty_ = true;
    prepareResource();
}

void BodyShadowItem::prepareResource()
{
    if (width() <= 0 || height() <= 0) { update(); return; }
    const qreal radius = std::max(qreal(0), std::min({radius_, width() / 2, height() / 2}));
    const qreal ratio = window() ? window()->effectiveDevicePixelRatio() : 1;
    if (resourceDirty_ || preparedRadius_ != radius || patch_.devicePixelRatio != ratio) {
        auto next = profile_.edgeOpacity <= 0 && profile_.key.opacity <= 0 && profile_.ambient.opacity <= 0
            ? ElevationBodyPatch{} : renderer_.bodyPatch(radius, profile_, style_, ratio);
        if (next.image.constBits() != patch_.image.constBits()) ++resourceRevision_;
        patch_ = std::move(next);
        preparedRadius_ = radius;
        resourceDirty_ = false;
    }
    update();
}

void BodyShadowItem::geometryChange(const QRectF& current, const QRectF& previous)
{
    QQuickItem::geometryChange(current, previous);
    if (current.size() != previous.size()) prepareResource();
}

void BodyShadowItem::itemChange(ItemChange change, const ItemChangeData& data)
{
    QQuickItem::itemChange(change, data);
    if (change == ItemSceneChange || change == ItemDevicePixelRatioHasChanged) prepareResource();
}

BodyShadowStats BodyShadowItem::renderStats() const
{
    return {textureUploads_.load(std::memory_order_relaxed), imageBytesUploaded_.load(std::memory_order_relaxed),
            geometryUpdates_.load(std::memory_order_relaxed)};
}

QSGNode* BodyShadowItem::updatePaintNode(QSGNode* previous, UpdatePaintNodeData*)
{
    if (patch_.image.isNull() || width() <= 0 || height() <= 0) { delete previous; return nullptr; }
    auto* node = previous ? static_cast<BodyShadowNode*>(previous) : new BodyShadowNode;
    if (node->syncResource(*window(), patch_, resourceRevision_)) {
        textureUploads_.fetch_add(1, std::memory_order_relaxed);
        imageBytesUploaded_.fetch_add(quint64(patch_.image.sizeInBytes()), std::memory_order_relaxed);
    }
    if (node->syncGeometry(patch_, size(), preparedRadius_)) geometryUpdates_.fetch_add(1, std::memory_order_relaxed);
    return node;
}

} // namespace window_system::quick
