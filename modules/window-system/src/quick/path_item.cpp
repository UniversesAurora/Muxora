// SPDX-License-Identifier: MIT
#include "path_item.hpp"

#include <QQuickWindow>
#include "path_node.hpp"

namespace window_system::quick {

PathItem::PathItem(QQuickItem* parent, PathAntialiasing mode) : QQuickItem(parent), antialiasing_(mode)
{
    setFlag(ItemHasContents);
    prepare({});
}

bool PathItem::prepare(const QPainterPath& path)
{
    PathGeometryOptions options;
    options.antialiasing = antialiasing_ == PathAntialiasing::Coverage;
    options.devicePixelRatio = window() ? window()->effectiveDevicePixelRatio() : 1;
    cache_.update(path, options);
    update();
    return cache_.geometry().valid();
}

bool PathItem::setPath(const QPainterPath& path)
{
    const bool accepted = prepare(path);
    // Do not retain a rejected, potentially oversized input for a later DPR
    // change. A rejected mesh stays empty until the caller supplies a new path.
    path_ = accepted ? path : QPainterPath{};
    return accepted;
}

void PathItem::setAntialiasingMode(PathAntialiasing mode)
{
    if (antialiasing_ == mode) return;
    antialiasing_ = mode;
    prepare(path_);
}

void PathItem::setFill(const QColor& fill)
{
    if (fill_ == fill) return;
    fill_ = fill;
    update();
}

void PathItem::itemChange(ItemChange change, const ItemChangeData& data)
{
    QQuickItem::itemChange(change, data);
    if (change == ItemSceneChange || change == ItemDevicePixelRatioHasChanged) prepare(path_);
}

QSGNode* PathItem::updatePaintNode(QSGNode* previous, UpdatePaintNodeData*)
{
    if (!cache_.geometry().valid() || cache_.geometry().fill.indices.isEmpty() || fill_.alpha() == 0) {
        delete previous;
        return nullptr;
    }
    auto* node = previous ? static_cast<PathNode*>(previous) : new PathNode;
    node->sync(cache_.geometry(), cache_.generation(), fill_);
    return node;
}

} // namespace window_system::quick
