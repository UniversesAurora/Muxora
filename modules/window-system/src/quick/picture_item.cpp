// SPDX-License-Identifier: MIT
#include "picture_item.hpp"

#include <QQuickWindow>
#include <QSGImageNode>
#include <QSGRectangleNode>

namespace window_system::quick {
namespace {

class PictureNode final : public QSGNode {
public:
    explicit PictureNode(QQuickWindow& window)
        : fill(window.createRectangleNode()), image(window.createImageNode())
    {
        appendChildNode(fill); appendChildNode(image);
        image->setOwnsTexture(true);
        image->setFiltering(QSGTexture::Nearest);
    }
    QSGRectangleNode* fill;
    QSGImageNode* image;
};

} // namespace

PictureItem::PictureItem(QQuickItem* parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents);
}

void PictureItem::setPicture(QImage picture)
{
    picture_ = std::move(picture);
    fill_ = picture_.isNull() ? QColor() : picture_.pixelColor(picture_.width() - 1, picture_.height() - 1);
    pictureChanged_ = true;
    update();
}

void PictureItem::geometryChange(const QRectF& now, const QRectF& previous)
{
    QQuickItem::geometryChange(now, previous);
    if (now.size() != previous.size()) update();
}

QSGNode* PictureItem::updatePaintNode(QSGNode* previous, UpdatePaintNodeData*)
{
    if (!window() || picture_.isNull()) {
        delete previous;
        pictureChanged_ = true;
        return nullptr;
    }
    auto* node = previous ? static_cast<PictureNode*>(previous) : new PictureNode(*window());
    node->fill->setRect(boundingRect());
    node->fill->setColor(fill_);
    if (std::exchange(pictureChanged_, false)) {
        node->image->setTexture(window()->createTextureFromImage(picture_));
        node->image->setRect(QRectF(QPointF(), picture_.deviceIndependentSize()));
    }
    return node;
}

} // namespace window_system::quick
