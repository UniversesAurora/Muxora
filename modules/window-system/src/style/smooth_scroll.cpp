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

SmoothScrollAnimation::SmoothScrollAnimation(QObject* parent) : FrameClock(parent)
{
    clock_.start();
    advance = [this](qint64 timestampNs) {
        if (!motion_.advance(timestampNs)) return;
        // Applying a scroll position can destroy the owning view and clock.
        const auto apply = applyValue;
        if (apply) apply(motion_.value());
    };
}

void SmoothScrollAnimation::stop()
{
    const QPointer<SmoothScrollAnimation> alive(this);
    FrameClock::stop();
    if (alive) motion_.cancel();
}

void SmoothScrollAnimation::stopAt(qreal current)
{
    const QPointer<SmoothScrollAnimation> alive(this);
    FrameClock::stop();
    if (alive) motion_.stopAt(current);
}

void SmoothScrollAnimation::scrollBy(qreal delta, qreal current, qreal minimum, qreal maximum, bool animate)
{
    const auto timestamp = now(clock_.nsecsElapsed());
    if (!motion_.scrollBy(delta, current, minimum, maximum, timestamp, animate)) return;
    if (motion_.needsFrame()) restart(timestamp, motion_.durationMs());
    else {
        const QPointer<SmoothScrollAnimation> alive(this);
        FrameClock::stop();
        if (!alive) return;
        const auto apply = applyValue;
        if (apply) apply(motion_.value());
    }
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
