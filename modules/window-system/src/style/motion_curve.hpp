// SPDX-License-Identifier: MIT
#pragma once

#include <QEasingCurve>
#include <QPointF>

namespace window_system {

enum class Motion { Relocate, Reveal, Presence, Hover, SurfaceLift, SurfaceLower, SurfaceChangeLift, SurfaceChangeLower };
[[nodiscard]] constexpr int motionDuration(Motion motion)
{
    return motion == Motion::Hover ? 100 : motion == Motion::Relocate ? 150
        : motion == Motion::SurfaceLift ? 240 : motion == Motion::Reveal ? 200
        : motion == Motion::SurfaceLower || motion == Motion::SurfaceChangeLift || motion == Motion::SurfaceChangeLower ? 200 : 180;
}

[[nodiscard]] inline bool isSurfaceMotion(Motion motion)
{
    return motion == Motion::SurfaceLift || motion == Motion::SurfaceLower
        || motion == Motion::SurfaceChangeLift || motion == Motion::SurfaceChangeLower;
}

[[nodiscard]] inline QEasingCurve motionCurve(Motion motion)
{
    if (!isSurfaceMotion(motion)) return QEasingCurve(QEasingCurve::OutCubic);
    QEasingCurve curve(QEasingCurve::BezierSpline);
    const bool lifting = motion == Motion::SurfaceLift || motion == Motion::SurfaceChangeLift;
    curve.addCubicBezierSegment(lifting ? QPointF(0.05, 0.7) : QPointF(0.2, 0),
                               lifting ? QPointF(0.1, 1.0) : QPointF(0, 1.0), QPointF(1, 1));
    return curve;
}

} // namespace window_system
