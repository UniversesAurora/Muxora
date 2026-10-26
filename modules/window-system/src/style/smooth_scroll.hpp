// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/scrolling.hpp>
#include <QVariantAnimation>
#include <functional>

class QEvent;
class QWidget;

namespace window_system {

// One-dimensional impulse scrolling. The caller owns position/range and
// applies each frame without relaying the original wheel event or rebuilding
// its layout. Pointer dragging and other direct navigation call stopAt().
class SmoothScrollAnimation final : public QVariantAnimation {
public:
    explicit SmoothScrollAnimation(QObject* parent = nullptr);
    std::function<void(qreal)> applyValue;
    void scrollBy(qreal delta, qreal current, qreal minimum, qreal maximum, bool animate = true);
    void stopAt(qreal current);
    [[nodiscard]] qreal value() const { return value_; }
    [[nodiscard]] qreal target() const { return target_; }
private:
    [[nodiscard]] qreal velocity() const;
    qreal value_ = 0;
    qreal target_ = 0;
};

[[nodiscard]] bool smoothScrollAnimationsAllowed(const QWidget* widget);

// Called by the window manager's application filter, after value-control
// wheel forwarding. Touchpad pixel deltas and Ctrl-wheel keep native handling.
bool filterSmoothScroll(QObject* watched, QEvent* event);

} // namespace window_system
