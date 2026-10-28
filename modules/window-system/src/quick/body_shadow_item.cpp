// SPDX-License-Identifier: MIT
#include "body_shadow_item.hpp"

#include "shadow_node.hpp"
#include <algorithm>

namespace window_system::quick {
namespace {
// Subtract an axis-aligned hole from each perimeter quad. At most four
// pieces remain; texture coordinates still refer to the original sprite.
std::span<const ElevationPatchQuad> bodyQuads(const ElevationBodyPatch& patch, QSizeF size,
    qreal radius, const QRectF& excluded, std::array<ElevationPatchQuad, 32>& storage)
{
    int count = 0;
    for (const auto& quad : patch.quads(QRectF(QPointF(), size), radius)) {
        const auto append = [&](const QRectF& area) {
            if (area.isEmpty()) return;
            const auto& d = quad.target; const auto& s = quad.sourcePixels;
            storage[count++] = {area, {s.x() + (area.x() - d.x()) * s.width() / d.width(),
                s.y() + (area.y() - d.y()) * s.height() / d.height(),
                area.width() * s.width() / d.width(), area.height() * s.height() / d.height()}};
        };
        const auto cut = quad.target.intersected(excluded);
        const auto& d = quad.target;
        if (cut.isEmpty()) { storage[count++] = quad; continue; }
        append({d.left(), d.top(), d.width(), cut.top() - d.top()});
        append({d.left(), cut.bottom(), d.width(), d.bottom() - cut.bottom()});
        append({d.left(), cut.top(), cut.left() - d.left(), cut.height()});
        append({cut.right(), cut.top(), d.right() - cut.right(), cut.height()});
    }
    return {storage.data(), size_t(count)};
}
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

void BodyShadowItem::setExcluded(const QRectF& area)
{
    if (excluded_ == area) return;
    excluded_ = area; ++geometryRevision_; update();
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
    ++geometryRevision_; update();
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

ShadowRenderStats BodyShadowItem::renderStats() const
{
    return {textureUploads_.load(std::memory_order_relaxed), imageBytesUploaded_.load(std::memory_order_relaxed),
            geometryUpdates_.load(std::memory_order_relaxed)};
}

QSGNode* BodyShadowItem::updatePaintNode(QSGNode* previous, UpdatePaintNodeData*)
{
    if (patch_.image.isNull() || width() <= 0 || height() <= 0) { delete previous; return nullptr; }
    auto* node = previous ? static_cast<ShadowNode*>(previous) : new ShadowNode(32);
    if (node->syncResource(*window(), patch_.image, resourceRevision_)) {
        textureUploads_.fetch_add(1, std::memory_order_relaxed);
        imageBytesUploaded_.fetch_add(quint64(patch_.image.sizeInBytes()), std::memory_order_relaxed);
    }
    std::array<ElevationPatchQuad, 32> quads;
    if (node->syncGeometry(bodyQuads(patch_, size(), preparedRadius_, excluded_, quads), geometryRevision_))
        geometryUpdates_.fetch_add(1, std::memory_order_relaxed);
    return node;
}

} // namespace window_system::quick
