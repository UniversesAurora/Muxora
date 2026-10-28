// SPDX-License-Identifier: MIT
#pragma once

#include "style/motion_curve.hpp"
#include <QtGlobal>

namespace window_system {

// The owning window supplies a monotonic nanosecond clock at its frame cadence.
// This value owns no timer, QObject or render-thread resource. Retargeting starts
// at the last displayed sample rather than evaluating an unpresented old frame.
class FrameMotion final {
public:
    explicit FrameMotion(qreal initial = 0) : value_(initial), target_(initial), from_(initial) {}

    [[nodiscard]] qreal value() const { return value_; }
    [[nodiscard]] qreal target() const { return target_; }
    [[nodiscard]] bool needsFrame() const { return running_; }
    [[nodiscard]] int durationMs() const { return int(durationNs_ / 1'000'000); }

    void jump(qreal value);
    void animateTo(qreal target, Motion motion, bool enabled, qint64 timestampNs, qreal distanceScale = 1);
    // Returns whether the displayed value changed. An idle value does not ask
    // the owner to schedule another frame, even when the clock keeps advancing.
    bool advance(qint64 timestampNs);

private:
    [[nodiscard]] qint64 monotonicTime(qint64 timestampNs);

    qreal value_;
    qreal target_;
    qreal from_;
    QEasingCurve curve_;
    qint64 startedNs_ = 0;
    qint64 durationNs_ = 0;
    qint64 latestNs_ = 0;
    bool hasTimestamp_ = false;
    bool running_ = false;
};

} // namespace window_system
