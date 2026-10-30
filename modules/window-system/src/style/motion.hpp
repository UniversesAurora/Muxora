// SPDX-License-Identifier: MIT
#pragma once

#include "frame_motion.hpp"
#include "motion_curve.hpp"
#include "label_presence.hpp"
#include <QPainter>
#include <QSignalBlocker>
#include <QVariantAnimation>
#include <algorithm>
#include <utility>

namespace window_system {

// Retarget from the displayed value, including a reversal halfway through.
// The owner supplies animation availability; this utility has no platform or
// tab dependency and uses Qt's shared animation driver, not a polling timer.
class ProgressAnimation final : public QVariantAnimation {
public:
    explicit ProgressAnimation(qreal initial = 0) : value_(initial), target_(initial)
    {
        connect(this, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) { value_ = value.toReal(); });
    }
    [[nodiscard]] qreal value() const { return value_; }
    [[nodiscard]] qreal target() const { return target_; }
    void jump(qreal value)
    {
        stop(); value_ = target_ = value;
    }
    void animateTo(qreal target, Motion motion, bool enabled, qreal distanceScale = 1)
    {
        if (target_ == target && (state() == Running || value_ == target)) return;
        const qreal from = value_;
        stop(); target_ = target;
        if (!enabled) { value_ = target; emit valueChanged(value_); return; }
        {
            // Configuring an animation at its previous time can emit an
            // intermediate value. Do not expose that value to layout.
            const QSignalBlocker quiet(this);
            setStartValue(from); setEndValue(target);
            setDuration(isSurfaceMotion(motion) ? std::max(1, qRound(motionDuration(motion)
                * std::clamp(std::abs(target - from) * distanceScale, 0.0, 1.0))) : motionDuration(motion));
            setEasingCurve(motionCurve(motion)); setCurrentTime(0);
            value_ = from;
        }
        start();
    }
private:
    qreal value_;
    qreal target_;
};

// The label's enter/leave treatment is reusable independently of what
// triggered it. Callers can drive visibility in either direction and keep
// logical geometry, hit testing and the label's own drawing unchanged.
struct LabelPresenceEffect {
    using Kind = LabelPresenceKind;

    template<class Draw>
    static void paint(QPainter& painter, const QRect& rect, qreal visibility, Draw draw, Kind kind = Kind::Lift)
    {
        paint(painter, labelPresencePose(rect, visibility, kind), std::move(draw));
    }

    template<class Draw>
    static void paint(QPainter& painter, const LabelPresencePose& pose, Draw draw)
    {
        if (pose.opacity <= 0) return;
        if (pose.complete()) { draw(); return; }
        painter.save();
        painter.setOpacity(painter.opacity() * pose.opacity);
        painter.translate(pose.offset); painter.scale(pose.scale, pose.scale);
        draw(); painter.restore();
    }
};

} // namespace window_system
