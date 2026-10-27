// SPDX-License-Identifier: MIT
#pragma once

#include <QPointF>
#include <QRectF>
#include <algorithm>

namespace window_system {

enum class LabelPresenceKind { Lift, Planar };

// A renderer-independent affine pose. It belongs to the label, never its
// content card; scene graph nodes can use the same coefficients directly.
struct LabelPresencePose {
    qreal opacity = 1;
    qreal scale = 1;
    QPointF offset;

    [[nodiscard]] bool complete() const { return opacity == 1 && scale == 1 && offset.isNull(); }
    [[nodiscard]] QRectF mapRect(QRectF rect) const
    {
        return {rect.topLeft() * scale + offset, rect.size() * scale};
    }
    [[nodiscard]] LabelPresencePose translated(QPointF origin) const
    {
        auto result = *this;
        result.offset += origin * (1 - scale);
        return result;
    }
};

[[nodiscard]] inline LabelPresencePose labelPresencePose(QRectF rect, qreal visibility,
                                                       LabelPresenceKind kind = LabelPresenceKind::Lift)
{
    const qreal progress = std::clamp(visibility, qreal(0), qreal(1));
    if (kind == LabelPresenceKind::Planar) return {progress > 0 ? qreal(1) : qreal(0), 1, {}};
    const qreal scale = 0.88 + 0.12 * progress;
    return {progress, scale, rect.center() * (1 - scale) + QPointF(0, 3 * (1 - progress))};
}

} // namespace window_system
