// SPDX-License-Identifier: MIT
#include "scroll_motion_state.hpp"

#include <QPointF>
#include <algorithm>
#include <cmath>

namespace window_system {
namespace {
// Preserve the existing Edge impulse parameters and bounded duration. Only
// scheduling moves to the adapter; QEasingCurve still evaluates the spline.
QEasingCurve impulseCurve(qreal initialSlope)
{
    const qreal slope = std::max(qreal(0), initialSlope);
    const qreal x = slope > 4 ? 1 / slope : 0.25;
    QEasingCurve curve(QEasingCurve::BezierSpline);
    curve.addCubicBezierSegment(QPointF(x, std::min(qreal(1), x * slope)),
                               QPointF(0, 1), QPointF(1, 1));
    return curve;
}
} // namespace

qint64 ScrollMotionState::monotonicTime(qint64 timestampNs)
{
    if (!hasTimestamp_ || timestampNs > latestNs_) latestNs_ = timestampNs;
    hasTimestamp_ = true;
    return latestNs_;
}

qreal ScrollMotionState::velocity() const
{
    if (!running_ || durationMs_ <= 0) return 0;
    const qreal a = std::max(qreal(0), progress_ - 0.0001);
    const qreal b = std::min(qreal(1), progress_ + 0.0001);
    const qreal gradient = (curve_.valueForProgress(b) - curve_.valueForProgress(a)) / (b - a);
    return gradient * (target_ - from_) / durationMs_;
}

void ScrollMotionState::stopAt(qreal current)
{
    value_ = target_ = from_ = current;
    running_ = false;
}

bool ScrollMotionState::scrollBy(qreal delta, qreal current, qreal minimum, qreal maximum,
                                qint64 timestampNs, bool animate)
{
    if (!std::isfinite(delta) || !std::isfinite(current) || !std::isfinite(minimum)
        || !std::isfinite(maximum) || minimum > maximum) return false;
    const bool continuing = running_ && delta * (target_ - value_) > 0;
    const qreal speed = continuing ? velocity() : 0;
    const qreal from = std::clamp(current, minimum, maximum);
    const qreal destination = std::clamp((continuing ? target_ : from) + delta, minimum, maximum);
    const auto now = monotonicTime(timestampNs);
    stopAt(from);
    target_ = destination;
    if (!animate || qFuzzyCompare(from + 1, destination + 1)) {
        value_ = destination;
        return true;
    }
    durationMs_ = qRound(std::clamp(std::abs(destination - from) * 1.5, qreal(200), qreal(500)));
    curve_ = impulseCurve(speed * durationMs_ / (destination - from));
    progress_ = 0;
    startedNs_ = now;
    running_ = true;
    return true;
}

bool ScrollMotionState::advance(qint64 timestampNs)
{
    const auto now = monotonicTime(timestampNs);
    if (!running_) return false;
    const qreal previous = value_;
    const quint64 elapsed = quint64(now) - quint64(startedNs_);
    const quint64 duration = quint64(durationMs_) * 1'000'000;
    if (elapsed >= duration) {
        value_ = target_;
        progress_ = 1;
        running_ = false;
    } else {
        progress_ = qreal(elapsed) / qreal(duration);
        value_ = from_ + (target_ - from_) * curve_.valueForProgress(progress_);
    }
    return value_ != previous;
}

} // namespace window_system
