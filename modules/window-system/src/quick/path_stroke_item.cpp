// SPDX-License-Identifier: MIT
#include "path_stroke_item.hpp"
#include "path_geometry.hpp"
#include "qml_scene_component.hpp"

#include <QQmlComponent>
#include <QQuickWindow>
#include <cmath>

namespace window_system::quick {
namespace {

QString svgPath(const QPainterPath& path)
{
    QString result;
    QPointF start;
    const auto point = [](qreal x, qreal y) {
        return QString::number(x, 'g', 17) + QLatin1Char(' ') + QString::number(y, 'g', 17);
    };
    for (int index = 0; index < path.elementCount(); ++index) {
        const auto element = path.elementAt(index);
        switch (element.type) {
        case QPainterPath::MoveToElement:
            start = QPointF(element.x, element.y);
            result += QStringLiteral("M ") + point(element.x, element.y) + QLatin1Char(' ');
            break;
        case QPainterPath::LineToElement:
            result += QStringLiteral("L ") + point(element.x, element.y) + QLatin1Char(' ');
            break;
        case QPainterPath::CurveToElement: {
            const auto control = path.elementAt(++index);
            const auto end = path.elementAt(++index);
            result += QStringLiteral("C ") + point(element.x, element.y) + QLatin1Char(' ')
                + point(control.x, control.y) + QLatin1Char(' ') + point(end.x, end.y) + QLatin1Char(' ');
            break;
        }
        case QPainterPath::CurveToDataElement:
            Q_UNREACHABLE();
        }
        const auto end = path.elementAt(index);
        if (index > 0 && (index + 1 == path.elementCount() || path.elementAt(index + 1).isMoveTo())
            && QPointF(end.x, end.y) == start)
            result += QStringLiteral("Z ");
    }
    return result;
}

} // namespace

PathStrokeItem::PathStrokeItem(QQuickItem* parent) : QQuickItem(parent)
{
    auto& component = sceneComponent(SceneComponent::PathStroke);
    stroke_ = qobject_cast<QQuickItem*>(component.create());
    if (!stroke_) qFatal("Cannot create Quick path stroke: %s", qPrintable(component.errorString()));
    stroke_->setParent(this);
    stroke_->setParentItem(this);
}

bool PathStrokeItem::validPath(const QPainterPath& path, qreal width) const
{
    const PathGeometryOptions budget;
    const qreal ratio = window() ? window()->effectiveDevicePixelRatio() : 1;
    if (path.elementCount() > budget.maximumElements || !std::isfinite(width) || width <= 0
        || width * ratio > budget.maximumPhysicalExtent) return false;
    for (int index = 0; index < path.elementCount(); ++index) {
        const auto element = path.elementAt(index);
        if (!std::isfinite(element.x) || !std::isfinite(element.y)
            || std::abs(element.x * ratio) + width * ratio > budget.maximumPhysicalExtent
            || std::abs(element.y * ratio) + width * ratio > budget.maximumPhysicalExtent)
            return false;
    }
    return true;
}

bool PathStrokeItem::setStroke(const QPainterPath& path, const QColor& color, qreal width)
{
    if (!validPath(path, width)) {
        path_ = {};
        stroke_->setProperty("svgPath", QString{});
        return false;
    }
    if (path_ != path) {
        path_ = path;
        stroke_->setProperty("svgPath", svgPath(path));
    }
    strokeWidth_ = width;
    stroke_->setProperty("lineColor", color);
    stroke_->setProperty("lineWidth", width);
    return true;
}

void PathStrokeItem::geometryChange(const QRectF& next, const QRectF& previous)
{
    QQuickItem::geometryChange(next, previous);
    if (stroke_) stroke_->setSize(next.size());
}

void PathStrokeItem::itemChange(ItemChange change, const ItemChangeData& data)
{
    QQuickItem::itemChange(change, data);
    if (stroke_ && (change == ItemSceneChange || change == ItemDevicePixelRatioHasChanged)
        && !validPath(path_, strokeWidth_)) {
        path_ = {};
        stroke_->setProperty("svgPath", QString{});
    }
}

} // namespace window_system::quick
