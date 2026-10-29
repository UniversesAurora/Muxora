// SPDX-License-Identifier: MIT
#include "overlay_band_item.hpp"

#include <QLinearGradient>
#include <QPainter>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>

#include <algorithm>

namespace window_system::quick {
namespace {

class OverlayNode final : public QSGSimpleTextureNode {
public:
    bool sync(QQuickWindow& window, const QImage& image, quint64 revision)
    {
        if (revision_ == revision) return false;
        auto* next = window.createTextureFromImage(image);
        if (!next) return false;
        next->setFiltering(QSGTexture::Linear);
        setOwnsTexture(true);
        setTexture(next);
        setSourceRect(QRectF(QPointF(), image.size()));
        revision_ = revision;
        return true;
    }

private:
    quint64 revision_ = 0;
};

} // namespace

struct OverlayBandItem::Prepared {
    QImage image;
    quint64 resourceRevision = 0;
};

OverlayBandItem::OverlayBandItem(QQuickItem* parent)
    : QQuickItem(parent), prepared_(std::make_unique<Prepared>())
{
    setFlag(ItemHasContents);
    setVisible(false);
}

OverlayBandItem::~OverlayBandItem() = default;

void OverlayBandItem::prepareImage()
{
    prepared_->image = {};
    if (style_.highContrast) {
        update();
        return;
    }
    QImage image(1, 64, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    auto edge = style_.shadow;
    edge.setAlphaF(edge.alphaF() * elevationProfile(ElevationLevel::RowsOverlay).key.opacity);
    auto clear = edge;
    clear.setAlpha(0);
    QLinearGradient shade(0, above_ ? 63 : 0, 0, above_ ? 0 : 63);
    shade.setColorAt(0, edge);
    shade.setColorAt(1, clear);
    QPainter painter(&image);
    painter.fillRect(image.rect(), shade);
    prepared_->image = image;
    prepared_->resourceRevision = ++resourceRevision_;
    update();
}

void OverlayBandItem::setBand(const QRect& band, bool above, const ElevationStyle& style,
                              qreal progress)
{
    const bool changed = above_ != above || style_ != style || prepared_->image.isNull();
    above_ = above;
    style_ = style;
    setPosition(band.topLeft());
    setSize(band.size());
    setOpacity(std::clamp(progress, qreal(0), qreal(1)));
    setVisible(!band.isEmpty() && progress > 0 && !style.highContrast);
    if (changed) prepareImage();
}

void OverlayBandItem::clearBand()
{
    setVisible(false);
    prepared_->image = {};
}

quint64 OverlayBandItem::textureUploads() const { return textureUploads_.load(std::memory_order_relaxed); }
quint64 OverlayBandItem::geometryUpdates() const { return geometryUpdates_.load(std::memory_order_relaxed); }

void OverlayBandItem::geometryChange(const QRectF& current, const QRectF& previous)
{
    QQuickItem::geometryChange(current, previous);
    if (current.size() != previous.size()) update();
}

void OverlayBandItem::itemChange(ItemChange change, const ItemChangeData& data)
{
    QQuickItem::itemChange(change, data);
    if (change == ItemDevicePixelRatioHasChanged) update();
}

QSGNode* OverlayBandItem::updatePaintNode(QSGNode* previous, UpdatePaintNodeData*)
{
    if (!window() || prepared_->image.isNull() || style_.highContrast) {
        delete previous;
        return nullptr;
    }
    auto* node = previous ? static_cast<OverlayNode*>(previous) : new OverlayNode;
    if (node->sync(*window(), prepared_->image, prepared_->resourceRevision))
        textureUploads_.fetch_add(1, std::memory_order_relaxed);
    const QRectF target(QPointF(), size());
    if (node->rect() != target) {
        node->setRect(target);
        geometryUpdates_.fetch_add(1, std::memory_order_relaxed);
    }
    return node;
}

} // namespace window_system::quick
