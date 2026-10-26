// SPDX-License-Identifier: MIT
#pragma once

#include <QtTypes>
#include <optional>

namespace window_system {

// Protect a secondary wheel action after reaching the end of a scrollable
// surface. The caller supplies monotonic time; no timer or polling is needed.
class ScrollBoundaryGuard {
public:
    explicit ScrollBoundaryGuard(qint64 quietPeriod) : quietPeriod_(quietPeriod) {}
    void reset() { lastInput_.reset(); atBoundary_ = false; }
    void observe(bool atBoundary, qint64 time) { atBoundary_ = atBoundary; lastInput_ = time; }
    bool allowsExit(bool scrollable, qint64 time)
    {
        const bool allowed = !scrollable || (atBoundary_ && lastInput_ && time - *lastInput_ >= quietPeriod_);
        observe(true, time);
        return allowed;
    }
private:
    qint64 quietPeriod_;
    std::optional<qint64> lastInput_;
    bool atBoundary_ = false;
};

} // namespace window_system
