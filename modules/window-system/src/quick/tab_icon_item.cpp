// SPDX-License-Identifier: MIT
#include "tab_icon_item.hpp"

#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>

namespace window_system::quick {
namespace {

class IconNode final : public QSGSimpleTextureNode {
public:
    bool sync(QQuickWindow& window, const QImage& image, const QSize& logicalSize, quint64 revision)
    {
        if (revision_ == revision) return false;
        auto* next = window.createTextureFromImage(image);
        if (!next) return false;
        next->setFiltering(QSGTexture::Linear);
        setOwnsTexture(true);
        setTexture(next);
        setRect(QRectF(QPointF(), logicalSize));
        setSourceRect(QRectF(QPointF(), image.size()));
        revision_ = revision;
        return true;
    }

private:
    quint64 revision_ = 0;
};

} // namespace

struct TabIconItem::Prepared {
    QImage image;
    quint64 resourceRevision = 0;
};

TabIconItem::TabIconItem(QQuickItem* parent)
    : QQuickItem(parent), prepared_(std::make_unique<Prepared>())
{
    setFlag(ItemHasContents);
}

TabIconItem::~TabIconItem() = default;

void TabIconItem::prepare()
{
    prepared_->image = {};
    if (icon_.isNull() || logicalSize_.isEmpty()) {
        update();
        return;
    }
    const qreal ratio = window() ? window()->effectiveDevicePixelRatio() : 1;
    auto pixmap = icon_.pixmap(logicalSize_, ratio,
        enabled_ ? QIcon::Normal : QIcon::Disabled,
        selected_ ? QIcon::On : QIcon::Off);
    if (tint_.isValid() && !pixmap.isNull()) {
        QPainter painter(&pixmap);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(pixmap.rect(), tint_);
    }
    if (!pixmap.isNull()) {
        prepared_->image = pixmap.toImage();
        prepared_->image.setDevicePixelRatio(ratio);
    }
    prepared_->resourceRevision = ++resourceRevision_;
    update();
}

bool TabIconItem::setIcon(QIcon icon, QSize logicalSize, QColor tint, bool enabled, bool selected)
{
    const bool changed = icon_.cacheKey() != icon.cacheKey() || logicalSize_ != logicalSize || tint_ != tint
        || enabled_ != enabled || selected_ != selected;
    icon_ = std::move(icon);
    logicalSize_ = logicalSize;
    tint_ = tint;
    enabled_ = enabled;
    selected_ = selected;
    if (changed) {
        prepare();
        setSize(logicalSize_);
    }
    return !icon_.isNull() && !logicalSize_.isEmpty();
}

void TabIconItem::clearIcon()
{
    icon_ = {};
    logicalSize_ = {};
    tint_ = {};
    prepare();
}

quint64 TabIconItem::textureUploads() const { return textureUploads_.load(std::memory_order_relaxed); }

void TabIconItem::itemChange(ItemChange change, const ItemChangeData&)
{
    if (change == ItemSceneChange || change == ItemDevicePixelRatioHasChanged) prepare();
}

QSGNode* TabIconItem::updatePaintNode(QSGNode* previous, UpdatePaintNodeData*)
{
    if (!window() || prepared_->image.isNull()) {
        delete previous;
        return nullptr;
    }
    auto* node = previous ? static_cast<IconNode*>(previous) : new IconNode;
    if (node->sync(*window(), prepared_->image, logicalSize_, prepared_->resourceRevision))
        textureUploads_.fetch_add(1, std::memory_order_relaxed);
    return node;
}

} // namespace window_system::quick
