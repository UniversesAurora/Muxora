// SPDX-License-Identifier: MIT
#pragma once

#include <QEasingCurve>
#include <QtGlobal>

namespace window_system {

// One-dimensional impulse scrolling sampled by the owning window's monotonic
// nanosecond clock. Retargeting uses the last displayed sample and the actual
// current position supplied by the adapter, including scrollbar rounding.
class ScrollMotionState final {
public:
    [[nodiscard]] qreal value() const { return value_; }
    [[nodiscard]] qreal target() const { return target_; }
    [[nodiscard]] bool needsFrame() const { return running_; }
    [[nodiscard]] int durationMs() const { return durationMs_; }

    // Returns false for invalid input without changing state. An accepted
    // immediate request still lets the adapter notify its consumer, even if
    // the resulting value equals the preceding sample.
    bool scrollBy(qreal delta, qreal current, qreal minimum, qreal maximum,
                  qint64 timestampNs, bool animate = true);
    void stopAt(qreal current);
    void cancel() { running_ = false; }
    // Returns whether the displayed value changed; an idle model needs no
    // scheduling or callbacks, even when its owner advances another motion.
    bool advance(qint64 timestampNs);

private:
    [[nodiscard]] qreal velocity() const;
    [[nodiscard]] qint64 monotonicTime(qint64 timestampNs);

    qreal value_ = 0;
    qreal target_ = 0;
    qreal from_ = 0;
    qreal progress_ = 0;
    QEasingCurve curve_;
    qint64 startedNs_ = 0;
    qint64 latestNs_ = 0;
    int durationMs_ = 0;
    bool hasTimestamp_ = false;
    bool running_ = false;
};

} // namespace window_system
