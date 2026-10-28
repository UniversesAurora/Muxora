// SPDX-License-Identifier: MIT
#pragma once

#include "style/elevation.hpp"
#include "shadow_stats.hpp"
#include <QQuickItem>
#include <atomic>

namespace window_system::quick {

// A body shadow retains the shared cold sprite. Resizing changes perimeter quads,
// not an image covering the body. The scene graph owns its window's texture.
class BodyShadowItem final : public QQuickItem {
    Q_OBJECT
public:
    explicit BodyShadowItem(QQuickItem* parent = nullptr);
    void setShadow(qreal radius, const ElevationProfile& profile, const ElevationStyle& style);
    // In local coordinates. An anchor patch contributes this region instead.
    void setExcluded(const QRectF& area);
    [[nodiscard]] ShadowRenderStats renderStats() const;

protected:
    QSGNode* updatePaintNode(QSGNode* previous, UpdatePaintNodeData*) override;
    void geometryChange(const QRectF& current, const QRectF& previous) override;
    void itemChange(ItemChange change, const ItemChangeData& data) override;

private:
    void prepareResource();
    ElevationRenderer renderer_;
    ElevationBodyPatch patch_;
    ElevationProfile profile_;
    ElevationStyle style_;
    qreal radius_ = 0;
    qreal preparedRadius_ = -1;
    bool resourceDirty_ = true;
    quint64 resourceRevision_ = 0;
    quint64 geometryRevision_ = 0;
    QRectF excluded_;
    std::atomic<quint64> textureUploads_{0};
    std::atomic<quint64> imageBytesUploaded_{0};
    std::atomic<quint64> geometryUpdates_{0};
};

} // namespace window_system::quick
