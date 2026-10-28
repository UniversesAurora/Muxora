// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/scrolling.hpp>
#include "frame_clock.hpp"
#include "scroll_motion_state.hpp"
#include <QElapsedTimer>
#include <functional>

class QEvent;
class QWidget;

namespace window_system {

// One-dimensional impulse scrolling. The caller owns position/range and
// applies each frame without relaying the original wheel event or rebuilding
// its layout. Pointer dragging and other direct navigation call stopAt().
class SmoothScrollAnimation final : public FrameClock {
public:
    explicit SmoothScrollAnimation(QObject* parent = nullptr);
    std::function<void(qreal)> applyValue;
    void scrollBy(qreal delta, qreal current, qreal minimum, qreal maximum, bool animate = true);
    void stopAt(qreal current);
    void stop();
    [[nodiscard]] qreal value() const { return motion_.value(); }
    [[nodiscard]] qreal target() const { return motion_.target(); }
private:
    ScrollMotionState motion_;
    QElapsedTimer clock_;
};

[[nodiscard]] bool smoothScrollAnimationsAllowed(const QWidget* widget);

// Called by the window manager's application filter, after value-control
// wheel forwarding. Touchpad pixel deltas and Ctrl-wheel keep native handling.
bool filterSmoothScroll(QObject* watched, QEvent* event);

} // namespace window_system
