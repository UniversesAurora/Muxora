// SPDX-License-Identifier: MIT
#include "wheel_guard.hpp"

#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QScrollBar>
#include <QWheelEvent>

namespace window_system {
namespace {
bool isValueControl(const QWidget* widget)
{
    if (qobject_cast<const QScrollBar*>(widget)) return false;
    return qobject_cast<const QComboBox*>(widget) || qobject_cast<const QAbstractSpinBox*>(widget)
        || qobject_cast<const QAbstractSlider*>(widget);
}

// The wheel first reaches the widget under the cursor, which may be the inner
// editor of a spin box or editable combo box. Stop at the window boundary, so
// an open combo or calendar popup keeps scrolling its own list.
QWidget* valueControlFor(QWidget* widget)
{
    for (; widget; widget = widget->parentWidget()) {
        if (isValueControl(widget)) return widget;
        if (widget->isWindow()) break;
    }
    return nullptr;
}
} // namespace

bool redirectValueControlWheel(QObject* watched, QEvent* event)
{
    if (event->type() != QEvent::Wheel) return false;
    auto* receiver = qobject_cast<QWidget*>(watched);
    auto* control = valueControlFor(receiver);
    if (!control) return false;
    auto* wheel = static_cast<QWheelEvent*>(event);
    // Qt does not propagate a wheel event it did not receive from the
    // platform, so walk the ancestors as its own propagation would, until one
    // (normally the enclosing scroll area's viewport) accepts it.
    wheel->ignore();
    for (auto* target = control->parentWidget(); target; target = target->parentWidget()) {
        QWheelEvent forwarded(receiver->mapTo(target, wheel->position()), wheel->globalPosition(),
            wheel->pixelDelta(), wheel->angleDelta(), wheel->buttons(), wheel->modifiers(),
            wheel->phase(), wheel->inverted(), wheel->source());
        QCoreApplication::sendEvent(target, &forwarded);
        if (forwarded.isAccepted()) { wheel->accept(); break; }
        if (target->isWindow()) break;
    }
    return true;
}

} // namespace window_system
