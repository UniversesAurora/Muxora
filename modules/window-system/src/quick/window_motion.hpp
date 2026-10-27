// SPDX-License-Identifier: MIT
#pragma once

#include "frame_motion.hpp"
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QQuickWindow>

namespace window_system::quick {

// Binds a value animation to the owning Quick window's frames. There is no
// polling timer; completed or hidden animations request no additional frame.
class WindowMotion final : public QObject {
    Q_OBJECT
public:
    explicit WindowMotion(QObject* parent = nullptr);
    void setWindow(QQuickWindow* window);
    void animateTo(qreal target, Motion kind, bool enabled, qreal distanceScale = 1);
    void jump(qreal value);
    [[nodiscard]] qreal value() const { return motion_.value(); }
    [[nodiscard]] bool running() const { return motion_.needsFrame(); }

signals:
    void valueChanged(qreal value);

private:
    void requestFrame();
    void advance();
    QPointer<QQuickWindow> window_;
    QElapsedTimer clock_;
    FrameMotion motion_;
    QMetaObject::Connection animateConnection_;
    QMetaObject::Connection swapConnection_;
    QMetaObject::Connection visibilityConnection_;
};

} // namespace window_system::quick
