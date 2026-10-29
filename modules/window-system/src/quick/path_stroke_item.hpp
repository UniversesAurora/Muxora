// SPDX-License-Identifier: MIT
#pragma once

#include <QColor>
#include <QPainterPath>
#include <QQuickItem>

namespace window_system::quick {

// Qt Quick's curve renderer owns stroking, including closed outlines and holes.
// Geometry is serialized only when the source path changes, never per frame.
class PathStrokeItem final : public QQuickItem {
public:
    explicit PathStrokeItem(QQuickItem* parent = nullptr);
    bool setStroke(const QPainterPath& path, const QColor& color, qreal width);

protected:
    void geometryChange(const QRectF& next, const QRectF& previous) override;
    void itemChange(ItemChange change, const ItemChangeData& data) override;

private:
    bool validPath(const QPainterPath& path, qreal width) const;
    QQuickItem* stroke_ = nullptr;
    QPainterPath path_;
    qreal strokeWidth_ = 1;
};

} // namespace window_system::quick
