// SPDX-License-Identifier: MIT
#include "edge_fade_item.hpp"

#include <QLinearGradient>
#include <QPainter>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>

#include <cmath>

namespace window_system::quick {
namespace {

class FadeNode final : public QSGSimpleTextureNode {
public:
    bool sync(QQuickWindow& window, const QImage& image, const QSizeF& size, quint64 revision)
    {
        if (revision_ == revision) return false;
        auto* next = window.createTextureFromImage(image);
        if (!next) return false;
        next->setFiltering(QSGTexture::Linear);
        setOwnsTexture(true);
        setTexture(next);
        setRect(QRectF(QPointF(), size));
        setSourceRect(QRectF(QPointF(), image.size()));
        revision_ = revision;
        return true;
    }

private:
    quint64 revision_ = 0;
};

bool sameFade(const EdgeFade& a, const EdgeFade& b)
{
    return a.area == b.area && a.edge == b.edge && a.opacityRamp == b.opacityRamp;
}

} // namespace

struct EdgeFadeItem::Prepared {
    QImage image;
    quint64 resourceRevision = 0;
};

EdgeFadeItem::EdgeFadeItem(QQuickItem* parent)
    : QQuickItem(parent), prepared_(std::make_unique<Prepared>())
{
    setFlag(ItemHasContents);
    setVisible(false);
}

EdgeFadeItem::~EdgeFadeItem() = default;

void EdgeFadeItem::prepareImage()
{
    prepared_->image = {};
    if (fade_.area.isEmpty() || !background_.isValid()) {
        update();
        return;
    }
    const qreal ratio = window() ? window()->effectiveDevicePixelRatio() : 1;
    QImage image(QSize(qCeil(fade_.area.width() * ratio), qCeil(fade_.area.height() * ratio)),
                 QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) {
        update();
        return;
    }
    image.fill(Qt::transparent);
    image.setDevicePixelRatio(ratio);
    const bool horizontal = fade_.edge == Qt::LeftEdge || fade_.edge == Qt::RightEdge;
    const bool fromStart = fade_.edge == Qt::LeftEdge || fade_.edge == Qt::TopEdge;
    QLinearGradient mask(horizontal ? QPointF(fade_.area.left(), 0) : QPointF(0, fade_.area.top()),
                         horizontal ? QPointF(fade_.area.right() + 1, 0)
                                    : QPointF(0, fade_.area.bottom() + 1));
    if (!fade_.opacityRamp.isNull()) mask.setStart(fade_.opacityRamp.p1());
    if (!fade_.opacityRamp.isNull()) mask.setFinalStop(fade_.opacityRamp.p2());
    const bool increasing = !fade_.opacityRamp.isNull() || fromStart;
    auto clear = background_;
    clear.setAlpha(0);
    mask.setColorAt(0, increasing ? clear : background_);
    mask.setColorAt(1, increasing ? background_ : clear);
    QPainter painter(&image);
    painter.translate(-fade_.area.topLeft());
    painter.fillRect(fade_.area, mask);
    prepared_->image = image;
    prepared_->resourceRevision = ++resourceRevision_;
    update();
}

bool EdgeFadeItem::setFade(const EdgeFade& fade, const QColor& background)
{
    const bool changed = !sameFade(fade_, fade) || background_ != background
        || prepared_->image.isNull();
    fade_ = fade;
    background_ = background;
    setPosition(fade.area.topLeft());
    setSize(fade.area.size());
    setVisible(!fade.area.isEmpty() && background.isValid());
    if (changed) prepareImage();
    return !prepared_->image.isNull();
}

void EdgeFadeItem::clearFade()
{
    setVisible(false);
    prepared_->image = {};
}

quint64 EdgeFadeItem::textureUploads() const { return textureUploads_.load(std::memory_order_relaxed); }

void EdgeFadeItem::itemChange(ItemChange change, const ItemChangeData&)
{
    if (change == ItemDevicePixelRatioHasChanged) prepareImage();
}

QSGNode* EdgeFadeItem::updatePaintNode(QSGNode* previous, UpdatePaintNodeData*)
{
    if (!window() || prepared_->image.isNull()) {
        delete previous;
        return nullptr;
    }
    auto* node = previous ? static_cast<FadeNode*>(previous) : new FadeNode;
    if (node->sync(*window(), prepared_->image, size(), prepared_->resourceRevision))
        textureUploads_.fetch_add(1, std::memory_order_relaxed);
    return node;
}

} // namespace window_system::quick
