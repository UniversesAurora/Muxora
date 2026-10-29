// SPDX-License-Identifier: MIT
#include "masked_item.hpp"
#include "path_item.hpp"
#include "qml_scene_component.hpp"

#include <QQuickWindow>
#include <QQmlComponent>
#include <QVector4D>
#include <cmath>

namespace window_system::quick {
namespace {

QVector4D fadeRamp(const EdgeFade& fade)
{
    const bool horizontal = fade.edge == Qt::LeftEdge || fade.edge == Qt::RightEdge;
    const bool increasing = !fade.opacityRamp.isNull()
        || fade.edge == Qt::LeftEdge || fade.edge == Qt::TopEdge;
    QLineF ramp = fade.opacityRamp;
    if (ramp.isNull()) ramp = horizontal
        ? QLineF(fade.area.left(), 0, fade.area.right() + 1, 0)
        : QLineF(0, fade.area.top(), 0, fade.area.bottom() + 1);
    const auto delta = ramp.p2() - ramp.p1();
    const auto norm = QPointF::dotProduct(delta, delta);
    const auto gradient = delta / norm * (increasing ? 1 : -1);
    return {float(gradient.x()), float(gradient.y()),
            float((increasing ? 0 : 1) - QPointF::dotProduct(ramp.p1(), gradient)), 0};
}

} // namespace

MaskedItem::MaskedItem(QQuickItem* parent)
    : QQuickItem(parent), contour_(new PathItem(this, PathAntialiasing::Multisample))
{
    auto& component = sceneComponent(SceneComponent::Mask);
    layer_ = qobject_cast<QQuickItem*>(component.create());
    if (!layer_) qFatal("Cannot create Quick alpha mask: %s", qPrintable(component.errorString()));
    layer_->setParent(this);
    layer_->setParentItem(this);
    contour_->setFill(Qt::white);
    layer_->setProperty("contourItem", QVariant::fromValue(contour_));
}

QQuickItem* MaskedItem::contentItem() const
{
    return layer_->property("contentItem").value<QQuickItem*>();
}

bool MaskedItem::setFades(const QVector<EdgeFade>& fades)
{
    // A lane can have start/end fades on each of two scroll axes.
    if (fades.size() > 4) {
        fadesValid_ = false;
        layer_->setProperty("maskValid", false);
        return false;
    }
    fadesValid_ = true;
    for (int index = 0; index < 4; ++index) {
        QVector4D area, ramp;
        if (index < fades.size() && !fades[index].area.isEmpty()) {
            const auto& fade = fades[index];
            area = {float(fade.area.left()), float(fade.area.top()),
                    float(fade.area.right() + 1), float(fade.area.bottom() + 1)};
            ramp = fadeRamp(fade);
        }
        layer_->setProperty(qPrintable(QStringLiteral("fadeArea%1").arg(index)), QVariant::fromValue(area));
        layer_->setProperty(qPrintable(QStringLiteral("fadeRamp%1").arg(index)), QVariant::fromValue(ramp));
    }
    layer_->setProperty("hasFades", !fades.isEmpty());
    return updateMaskSize();
}

bool MaskedItem::setContour(const QPainterPath& contour)
{
    if (contourPath_ != contour) {
        if (!contour_->setPath(contour)) {
            contourValid_ = false;
            contourPath_ = {};
            layer_->setProperty("maskValid", false);
            return false;
        }
        contourPath_ = contour;
    }
    contourValid_ = true;
    layer_->setProperty("hasContour", !contour.isEmpty());
    contour_->setVisible(!contour.isEmpty());
    return updateMaskSize();
}

void MaskedItem::clearContour() { setContour({}); }

void MaskedItem::geometryChange(const QRectF& next, const QRectF& previous)
{
    QQuickItem::geometryChange(next, previous);
    layer_->setSize(next.size());
    contour_->setSize(next.size());
    updateMaskSize();
}

bool MaskedItem::updateMaskSize()
{
    // Bound each GPU layer before Qt allocates its render target, including
    // after a DPR change. No CPU backing image is used for these masks.
    const qreal ratio = window() ? window()->effectiveDevicePixelRatio() : 1;
    const qreal physicalWidth = std::ceil(width() * ratio);
    const qreal physicalHeight = std::ceil(height() * ratio);
    const bool masked = layer_->property("hasFades").toBool() || layer_->property("hasContour").toBool();
    const bool valid = contourValid_ && fadesValid_ && (!masked || (physicalWidth >= 0 && physicalHeight >= 0
        && physicalWidth <= 16384 && physicalHeight <= 16384
        && physicalWidth * physicalHeight <= 8 * 1024 * 1024));
    layer_->setProperty("maskValid", valid);
    return valid;
}

void MaskedItem::itemChange(ItemChange change, const ItemChangeData& data)
{
    QQuickItem::itemChange(change, data);
    if (layer_ && (change == ItemSceneChange || change == ItemDevicePixelRatioHasChanged))
        updateMaskSize();
}

} // namespace window_system::quick
