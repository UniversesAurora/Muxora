// SPDX-License-Identifier: MIT
#include "window_motion.hpp"

namespace window_system::quick {

WindowMotion::WindowMotion(QObject* parent) : QObject(parent) { clock_.start(); }

void WindowMotion::setWindow(QQuickWindow* window)
{
    if (window_ == window) return;
    disconnect(animateConnection_);
    disconnect(swapConnection_);
    disconnect(visibilityConnection_);
    window_ = window;
    if (!window) return;
    // afterAnimating is a GUI-thread signal; item updates made here enter
    // the upcoming synchronization. frameSwapped can come from the render
    // thread, so the next request is explicitly delivered on our GUI thread.
    animateConnection_ = connect(window, &QQuickWindow::afterAnimating, this, &WindowMotion::advance);
    swapConnection_ = connect(window, &QQuickWindow::frameSwapped, this, &WindowMotion::requestFrame,
                              Qt::QueuedConnection);
    visibilityConnection_ = connect(window, &QWindow::visibleChanged, this, &WindowMotion::requestFrame);
    requestFrame();
}

void WindowMotion::animateTo(qreal target, Motion kind, bool enabled, qreal distanceScale)
{
    const auto previous = motion_.value();
    motion_.animateTo(target, kind, enabled, clock_.nsecsElapsed(), distanceScale);
    requestFrame();
    if (previous != motion_.value()) emit valueChanged(motion_.value());
}

void WindowMotion::jump(qreal value)
{
    const auto previous = motion_.value();
    motion_.jump(value);
    if (previous != value) emit valueChanged(value);
}

void WindowMotion::advance()
{
    if (motion_.advance(clock_.nsecsElapsed())) emit valueChanged(motion_.value());
}

void WindowMotion::requestFrame()
{
    if (motion_.needsFrame() && window_ && window_->isVisible()) window_->update();
}

} // namespace window_system::quick
