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
    if (next.bounds != patch_.bounds || next.clip != patch_.clip || next.stretchAt != patch_.stretchAt
        || next.stretch != patch_.stretch) ++geometryRevision_;
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
    const auto quads = patch_.quads();
    if (quads.isEmpty()) { delete previous; return nullptr; }
    auto* node = previous ? static_cast<ShadowNode*>(previous) : new ShadowNode(elevationAnchorQuads);
    if (node->syncResource(*window(), patch_.image, resourceRevision_)) {
        textureUploads_.fetch_add(1, std::memory_order_relaxed);
        imageBytesUploaded_.fetch_add(quint64(patch_.image.sizeInBytes()), std::memory_order_relaxed);
    }
    if (node->syncGeometry(std::span<const ElevationPatchQuad>(quads.constData(), size_t(quads.size())), geometryRevision_)) geometryUpdates_.fetch_add(1, std::memory_order_relaxed);
    return node;
}

} // namespace window_system::quick
