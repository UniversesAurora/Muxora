// SPDX-License-Identifier: MIT
#pragma once

#include "motion_curve.hpp"
#include <QPainter>
#include <QSignalBlocker>
#include <QVariantAnimation>
#include <algorithm>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace window_system {

[[nodiscard]] inline bool motionAnimationsEnabled()
{
    if (qEnvironmentVariable("WINDOW_SYSTEM_ANIMATIONS") == QLatin1StringView("0")) return false;
#ifdef Q_OS_WIN
    BOOL enabled = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0) && !enabled) return false;
#endif
    return true;
}

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
    enum class Kind { Lift, Planar };

    [[nodiscard]] static QRect planarRect(QRect open, QRect closed, qreal visibility)
    {
        const qreal progress = std::clamp(visibility, 0.0, 1.0);
        const auto blend = [progress](int a, int b) { return qRound(a + (b - a) * progress); };
        return {blend(closed.x(), open.x()), blend(closed.y(), open.y()),
            std::max(0, blend(closed.width(), open.width())), std::max(0, blend(closed.height(), open.height()))};
    }

    template<class Draw>
    static void paint(QPainter& painter, const QRect& rect, qreal visibility, Draw draw, Kind kind = Kind::Lift)
    {
        const qreal progress = std::clamp(visibility, 0.0, 1.0);
        if (progress <= 0) return;
        if (kind == Kind::Planar) { draw(); return; }
        painter.save();
        painter.setOpacity(painter.opacity() * progress);
        const QPointF center = QRectF(rect).center();
        const qreal scale = 0.88 + 0.12 * progress;
        painter.translate(center + QPointF(0, 3 * (1 - progress)));
        painter.scale(scale, scale); painter.translate(-center);
        draw(); painter.restore();
    }
};

} // namespace window_system
