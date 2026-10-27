// SPDX-License-Identifier: MIT
#include "frame_motion.hpp"

#include <algorithm>
#include <cmath>

namespace window_system {

qint64 FrameMotion::monotonicTime(qint64 timestampNs)
{
    if (!hasTimestamp_ || timestampNs > latestNs_) latestNs_ = timestampNs;
    hasTimestamp_ = true;
    return latestNs_;
}

void FrameMotion::jump(qreal value)
{
    value_ = target_ = from_ = value;
    running_ = false;
}

void FrameMotion::animateTo(qreal target, Motion motion, bool enabled, qint64 timestampNs, qreal distanceScale)
{
    const auto now = monotonicTime(timestampNs);
    if (!enabled) { jump(target); return; }
    if (target_ == target && (running_ || value_ == target)) return;
    from_ = value_;
    target_ = target;
    if (from_ == target_) { running_ = false; return; }
    const int durationMs = isSurfaceMotion(motion)
        ? std::max(1, qRound(motionDuration(motion)
            * std::clamp(std::abs(target - from_) * distanceScale, 0.0, 1.0)))
        : motionDuration(motion);
    durationNs_ = qint64(durationMs) * 1'000'000;
    curve_ = motionCurve(motion);
    startedNs_ = now;
    running_ = true;
}

bool FrameMotion::advance(qint64 timestampNs)
{
    const auto now = monotonicTime(timestampNs);
    if (!running_) return false;
    const qreal previous = value_;
    const quint64 elapsed = quint64(now) - quint64(startedNs_);
    if (elapsed >= quint64(durationNs_)) {
        value_ = target_;
        running_ = false;
    } else {
        const qreal progress = qreal(elapsed) / qreal(durationNs_);
        value_ = from_ + (target_ - from_) * curve_.valueForProgress(progress);
    }
    return value_ != previous;
}

} // namespace window_system
