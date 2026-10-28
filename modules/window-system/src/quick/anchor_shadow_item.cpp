// SPDX-License-Identifier: MIT
#include "anchor_shadow_item.hpp"
#include "shadow_node.hpp"

namespace window_system::quick {

AnchorShadowItem::AnchorShadowItem(QQuickItem* parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents);
}

void AnchorShadowItem::setShadow(const SurfaceGeometry& geometry, const ElevationProfile& body,
    const ElevationProfile& anchor, const ElevationStyle& style, bool incremental)
{
    geometry_ = geometry; body_ = body; anchor_ = anchor; style_ = style; incremental_ = incremental;
    prepareResource();
}

void AnchorShadowItem::prepareResource()
{
    auto next = renderer_.anchorPatch(geometry_, body_, anchor_, style_,
        window() ? window()->effectiveDevicePixelRatio() : 1, incremental_);
    if (next.image.constBits() != patch_.image.constBits()) ++resourceRevision_;
    if (next.bounds != patch_.bounds || next.clip != patch_.clip) ++geometryRevision_;
    patch_ = std::move(next);
    update();
}

void AnchorShadowItem::itemChange(ItemChange change, const ItemChangeData& data)
{
    QQuickItem::itemChange(change, data);
    if (change == ItemSceneChange || change == ItemDevicePixelRatioHasChanged) prepareResource();
}

ShadowRenderStats AnchorShadowItem::renderStats() const
{
    return {textureUploads_.load(std::memory_order_relaxed), imageBytesUploaded_.load(std::memory_order_relaxed),
            geometryUpdates_.load(std::memory_order_relaxed)};
}

QSGNode* AnchorShadowItem::updatePaintNode(QSGNode* previous, UpdatePaintNodeData*)
{
    const auto target = patch_.bounds.intersected(patch_.clip);
    if (patch_.image.isNull() || target.isEmpty()) { delete previous; return nullptr; }
    auto* node = previous ? static_cast<ShadowNode*>(previous) : new ShadowNode(1);
    if (node->syncResource(*window(), patch_.image, resourceRevision_)) {
        textureUploads_.fetch_add(1, std::memory_order_relaxed);
        imageBytesUploaded_.fetch_add(quint64(patch_.image.sizeInBytes()), std::memory_order_relaxed);
    }
    const auto& bounds = patch_.bounds;
    const qreal x = patch_.image.width() / bounds.width(), y = patch_.image.height() / bounds.height();
    const std::array<ElevationPatchQuad, 1> quads{{{target,
        {(target.left() - bounds.left()) * x, (target.top() - bounds.top()) * y, target.width() * x, target.height() * y}}}};
    if (node->syncGeometry(quads, geometryRevision_)) geometryUpdates_.fetch_add(1, std::memory_order_relaxed);
    return node;
}

} // namespace window_system::quick
