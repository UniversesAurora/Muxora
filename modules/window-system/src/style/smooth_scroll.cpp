// SPDX-License-Identifier: MIT
#include "smooth_scroll.hpp"
#include "motion.hpp"

#include <QAbstractScrollArea>
#include <QAbstractItemView>
#include <QApplication>
#include <QDynamicPropertyChangeEvent>
#include <QEasingCurve>
#include <QPointer>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStyle>
#include <QStyleHints>
#include <QWheelEvent>
#include <QWidget>
#include <algorithm>
#include <cmath>

namespace window_system {
namespace {
constexpr auto policyProperty = "windowSystemSmoothScrollingEnabled";
constexpr auto controllerProperty = "windowSystemSmoothScrollController";

// Edge's impulse model uses a distance-bounded duration and keeps the
// instantaneous velocity when a wheel gesture extends its destination. Only
// the curve's parameters are shared; Qt owns scheduling and interpolation.
QEasingCurve impulseCurve(qreal initialSlope)
{
    const qreal slope = std::max(qreal(0), initialSlope);
    const qreal x = slope > 4 ? 1 / slope : 0.25;
    QEasingCurve curve(QEasingCurve::BezierSpline);
    curve.addCubicBezierSegment(QPointF(x, std::min(qreal(1), x * slope)),
                               QPointF(0, 1), QPointF(1, 1));
    return curve;
}

class AreaScroller final : public QObject {
public:
    explicit AreaScroller(QAbstractScrollArea* area) : QObject(area), area_(area)
    {
        area->setProperty(controllerProperty, QVariant::fromValue(static_cast<QObject*>(this)));
        area->installEventFilter(this);
        for (int axis = 0; axis < 2; ++axis) {
            motions_[axis] = new SmoothScrollAnimation(this);
            auto* bar = scrollbar(axis);
            motions_[axis]->applyValue = [this, axis](qreal value) {
                if (!smoothScrollAnimationsAllowed(area_)) {
                    motions_[axis]->stopAt(scrollbar(axis)->value());
                    return;
                }
                const QPointer<AreaScroller> alive(this);
                writing_[axis] = true;
                scrollbar(axis)->setValue(qRound(value));
                if (alive) writing_[axis] = false;
            };
            connect(bar, &QScrollBar::valueChanged, this, [this, axis](int value) {
                if (!writing_[axis]) motions_[axis]->stopAt(value);
            });
            connect(bar, &QScrollBar::rangeChanged, this, [this, axis] {
                motions_[axis]->stopAt(scrollbar(axis)->value());
            });
            connect(bar, &QScrollBar::sliderPressed, this, [this, axis] {
                motions_[axis]->stopAt(scrollbar(axis)->value());
            });
            connect(bar, &QScrollBar::actionTriggered, this, [this, axis] {
                motions_[axis]->stopAt(scrollbar(axis)->value());
            });
        }
    }
    void stop()
    {
        const QPointer<AreaScroller> alive(this);
        for (int axis = 0; axis < 2 && alive; ++axis) motions_[axis]->stopAt(scrollbar(axis)->value());
    }
    bool wheel(QWheelEvent* event)
    {
        if (!event->pixelDelta().isNull() || event->modifiers().testFlag(Qt::ControlModifier)
            || !smoothScrollAnimationsAllowed(area_)) {
            stop();
            return false;
        }
        const bool horizontal = event->modifiers().testFlag(Qt::ShiftModifier)
            || (std::abs(event->angleDelta().x()) > std::abs(event->angleDelta().y()));
        int axis = horizontal ? 0 : 1;
        if (!horizontal && scrollbar(axis)->maximum() == scrollbar(axis)->minimum()
            && scrollbar(0)->maximum() > scrollbar(0)->minimum()) axis = 0;
        auto* bar = scrollbar(axis);
        if (bar->maximum() == bar->minimum()) return false;
        // A host's per-item model/view must keep its own discrete semantics.
        // Owned views can opt into ScrollPerPixel at their creation site.
        if (const auto* view = qobject_cast<const QAbstractItemView*>(area_)) {
            const auto mode = axis ? view->verticalScrollMode() : view->horizontalScrollMode();
            if (mode == QAbstractItemView::ScrollPerItem) return false;
        }
        const int delta = horizontal && event->angleDelta().x() ? event->angleDelta().x() : event->angleDelta().y();
        if (!delta) return false;
        const qreal steps = -qreal(delta) / 120;
        const int page = bar->pageStep();
        const int lines = QApplication::styleHints()->wheelScrollLines();
        const qreal distance = steps * (lines < 0 ? page : std::min(page, bar->singleStep() * lines));
        if (qFuzzyIsNull(distance)) return false;
        motions_[axis]->scrollBy(distance, bar->value(), bar->minimum(), bar->maximum());
        event->accept();
        return true;
    }
protected:
    bool eventFilter(QObject*, QEvent* event) override
    {
        if (event->type() == QEvent::Hide) stop();
        return false;
    }
private:
    QScrollBar* scrollbar(int axis) const { return axis ? area_->verticalScrollBar() : area_->horizontalScrollBar(); }
    QAbstractScrollArea* area_;
    SmoothScrollAnimation* motions_[2]{};
    bool writing_[2]{};
};

AreaScroller* existingController(QAbstractScrollArea* area)
{
    return dynamic_cast<AreaScroller*>(area->property(controllerProperty).value<QObject*>());
}
} // namespace

void setSmoothScrollingEnabled(QWidget* widget, bool enabled)
{
    if (!widget) return;
    const QPointer<QWidget> alive(widget);
    widget->setProperty(policyProperty, enabled);
    if (!alive) return;
    if (!enabled) {
        QList<QPointer<QAbstractScrollArea>> areas;
        if (auto* area = qobject_cast<QAbstractScrollArea*>(widget)) areas.append(area);
        for (auto* area : widget->findChildren<QAbstractScrollArea*>()) areas.append(area);
        for (const auto& area : areas)
            if (area) if (auto* control = existingController(area)) control->stop();
    }
}

bool isSmoothScrollingEnabled(const QWidget* widget)
{
    for (; widget; widget = widget->parentWidget()) {
        const auto policy = widget->property(policyProperty);
        if (policy.isValid() && !policy.toBool()) return false;
        if (widget->isWindow()) break;
    }
    return true;
}

bool smoothScrollAnimationsAllowed(const QWidget* widget)
{
    return widget && isSmoothScrollingEnabled(widget)
        && motionAnimationsEnabled()
        && widget->style()->styleHint(QStyle::SH_Widget_Animate, nullptr, widget);
}

SmoothScrollAnimation::SmoothScrollAnimation(QObject* parent) : QVariantAnimation(parent)
{
    connect(this, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        value_ = value.toReal();
        // The consumer can delete its widget (and this motion) while applying
        // a frame. Keep the callable alive independently of that object.
        const auto apply = applyValue;
        if (apply) apply(value_);
    });
}

qreal SmoothScrollAnimation::velocity() const
{
    if (state() != Running || duration() <= 0) return 0;
    const qreal time = qreal(currentTime()) / duration();
    const qreal a = std::max(qreal(0), time - 0.0001), b = std::min(qreal(1), time + 0.0001);
    const qreal gradient = (easingCurve().valueForProgress(b) - easingCurve().valueForProgress(a)) / (b - a);
    return gradient * (endValue().toReal() - startValue().toReal()) / duration();
}

void SmoothScrollAnimation::stopAt(qreal current)
{
    const QPointer<SmoothScrollAnimation> alive(this);
    stop();
    if (!alive) return;
    value_ = target_ = current;
}

void SmoothScrollAnimation::scrollBy(qreal delta, qreal current, qreal minimum, qreal maximum, bool animate)
{
    if (!std::isfinite(delta) || !std::isfinite(current) || !std::isfinite(minimum)
        || !std::isfinite(maximum) || minimum > maximum) return;
    const bool continuing = state() == Running && delta * (target_ - value_) > 0;
    const qreal speed = continuing ? velocity() : 0;
    const qreal from = std::clamp(current, minimum, maximum);
    const qreal destination = std::clamp((continuing ? target_ : from) + delta, minimum, maximum);
    const QPointer<SmoothScrollAnimation> alive(this);
    stopAt(from);
    if (!alive) return;
    target_ = destination;
    if (!animate || qFuzzyCompare(from + 1, destination + 1)) {
        value_ = destination;
        emit valueChanged(value_);
        return;
    }
    const int milliseconds = qRound(std::clamp(std::abs(destination - from) * 1.5, qreal(200), qreal(500)));
    {
        const QSignalBlocker blocker(this);
        setStartValue(from); setEndValue(destination); setDuration(milliseconds);
        setEasingCurve(impulseCurve(speed * milliseconds / (destination - from)));
        setCurrentTime(0);
        value_ = from;
    }
    start();
}

bool filterSmoothScroll(QObject* watched, QEvent* event)
{
    if (event->type() != QEvent::Wheel) return false;
    auto* widget = qobject_cast<QWidget*>(watched);
    if (!widget) return false;
    auto* area = qobject_cast<QAbstractScrollArea*>(widget);
    if (!area) {
        area = qobject_cast<QAbstractScrollArea*>(widget->parentWidget());
        if (!area || area->viewport() != widget) return false;
    }
    // A child control must get the first opportunity to consume its input.
    // A terminal, editor or custom tab strip is not intercepted merely
    // because it happens to live somewhere below a scroll area.
    if (area->property("windowSystemOwnWheelScrolling").toBool()) return false;
    auto* control = existingController(area);
    if (!control) control = new AreaScroller(area);
    return control->wheel(static_cast<QWheelEvent*>(event));
}

} // namespace window_system
