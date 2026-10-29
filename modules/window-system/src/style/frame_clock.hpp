// SPDX-License-Identifier: MIT
#pragma once

#include <QAbstractAnimation>
#include <QPointer>
#include <algorithm>
#include <functional>

namespace window_system {

// Scheduling adapter for the Widgets transition period. Values and curves
// belong to Core presentation states, so this clock does no QVariant or
// easing interpolation. Quick supplies timestamps from its window instead.
class FrameClock : public QAbstractAnimation {
public:
    explicit FrameClock(QObject* parent = nullptr) : QAbstractAnimation(parent) {}
    std::function<void(qint64)> advance;
    [[nodiscard]] qint64 now(qint64 monotonicNs) const
    {
        return std::max(monotonicNs, originNs_ + qint64(currentTime()) * 1'000'000);
    }

    void restart(qint64 timestampNs, int durationMs)
    {
        const QPointer<FrameClock> alive(this);
        preparing_ = true;
        stop();
        if (!alive) return;
        originNs_ = timestampNs; durationMs_ = durationMs; setCurrentTime(0);
        if (!alive) return;
        preparing_ = false;
        start();
    }
    [[nodiscard]] int duration() const override { return durationMs_; }

protected:
    void updateCurrentTime(int elapsedMs) override
    {
        if (preparing_ || elapsedMs == 0) return;
        const auto sample = advance;
        if (sample) sample(originNs_ + qint64(elapsedMs) * 1'000'000);
    }

private:
    qint64 originNs_ = 0;
    int durationMs_ = 0;
    bool preparing_ = false;
};

} // namespace window_system
