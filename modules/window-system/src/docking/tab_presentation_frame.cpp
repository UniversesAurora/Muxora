// SPDX-License-Identifier: MIT
#include "tab_bar.hpp"

namespace window_system {

void TabBar::setPresentationPublishing(bool enabled)
{
    if (presentationPublishing_ == enabled) return;
    presentationPublishing_ = enabled;
    ++presentationGeneration_;
    presentationPending_ = false;
    presentationFrame_.reset();
    if (enabled) invalidatePresentation();
    else presentationBuffers_ = {};
}

void TabBar::invalidatePresentation()
{
    presentationFrame_.reset();
    if (!presentationPublishing_ || presentationPending_) return;
    presentationPending_ = true;
    const auto generation = presentationGeneration_;
    QMetaObject::invokeMethod(this, [this, generation] {
        if (!presentationPublishing_ || generation != presentationGeneration_) return;
        presentationPending_ = false;
        emit presentationInvalidated();
    }, Qt::QueuedConnection);
}

void TabBar::requestRepaint()
{
    invalidatePresentation();
    QWidget::update();
}

std::shared_ptr<const TabPresentationFrame> TabBar::presentationFrame()
{
    if (presentationFrame_) return presentationFrame_;
    std::shared_ptr<TabPresentationFrame> next;
    for (auto& buffer : presentationBuffers_) if (!buffer || buffer.use_count() == 1) {
        if (!buffer) buffer = std::make_shared<TabPresentationFrame>();
        next = buffer;
        break;
    }
    if (!next) next = std::make_shared<TabPresentationFrame>();
    // Release the old component frames before choosing their reusable buffers.
    next->base.reset();
    next->foreground.reset();
    next->size = size();
    const auto currentFont = font();
    const auto direction = layoutDirection();
    next->base = captureStripFrame(currentFont, direction);
    next->foreground = captureForegroundFrame(currentFont, direction);
    presentationFrame_ = std::move(next);
    return presentationFrame_;
}

} // namespace window_system
