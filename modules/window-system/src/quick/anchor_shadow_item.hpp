// SPDX-License-Identifier: MIT
#pragma once

#include "shadow_stats.hpp"
#include "style/elevation.hpp"
#include <QQuickItem>
#include <atomic>

namespace window_system::quick {

// One anchor's bounded contribution. The owning surface excludes patchClip()
// from its body shadow, or uses incremental pixels during presence animation.
class AnchorShadowItem final : public QQuickItem {
    Q_OBJECT
public:
    explicit AnchorShadowItem(QQuickItem* parent = nullptr);
    void setShadow(const SurfaceGeometry& geometry, const ElevationProfile& body,
                   const ElevationProfile& anchor, const ElevationStyle& style, bool incremental = false);
    [[nodiscard]] QRectF patchClip() const { return patch_.clip; }
    [[nodiscard]] ShadowRenderStats renderStats() const;

protected:
    QSGNode* updatePaintNode(QSGNode* previous, UpdatePaintNodeData*) override;
    void itemChange(ItemChange change, const ItemChangeData& data) override;

private:
    void prepareResource();
    ElevationRenderer renderer_;
    SurfaceGeometry geometry_;
    ElevationProfile body_, anchor_;
    ElevationStyle style_;
    bool incremental_ = false;
    ElevationAnchorPatch patch_;
    quint64 resourceRevision_ = 0;
    quint64 geometryRevision_ = 0;
    std::atomic<quint64> textureUploads_{0};
    std::atomic<quint64> imageBytesUploaded_{0};
    std::atomic<quint64> geometryUpdates_{0};
};

} // namespace window_system::quick
