// SPDX-License-Identifier: MIT
#pragma once

#include "style/elevation.hpp"

#include <QImage>
#include <QQuickItem>
#include <atomic>
#include <memory>

namespace window_system::quick {

// A one-dimensional elevation shade used at the edge of an expanding row
// overlay. The gradient image is retained; animation changes item opacity.
class OverlayBandItem final : public QQuickItem {
public:
    explicit OverlayBandItem(QQuickItem* parent = nullptr);
    ~OverlayBandItem() override;

    void setBand(const QRect& band, bool above, const ElevationStyle& style, qreal progress);
    void clearBand();
    [[nodiscard]] quint64 textureUploads() const;
    [[nodiscard]] quint64 geometryUpdates() const;

protected:
    QSGNode* updatePaintNode(QSGNode* previous, UpdatePaintNodeData*) override;
    void itemChange(ItemChange change, const ItemChangeData& data) override;
    void geometryChange(const QRectF& current, const QRectF& previous) override;

private:
    struct Prepared;
    void prepareImage();
    bool above_ = false;
    ElevationStyle style_;
    quint64 resourceRevision_ = 0;
    std::unique_ptr<Prepared> prepared_;
    std::atomic<quint64> textureUploads_{0};
    std::atomic<quint64> geometryUpdates_{0};
};

} // namespace window_system::quick
