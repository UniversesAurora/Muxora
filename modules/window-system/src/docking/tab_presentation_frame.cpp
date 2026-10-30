// SPDX-License-Identifier: MIT
#include "tab_bar.hpp"

namespace window_system {

void TabBar::setPresentationPublishing(bool enabled)
{
    if (presentationPublishing_ == enabled) return;
    presentationPublishing_ = enabled;
    ++presentationGeneration_; presentationPending_ = false;
    if (enabled) invalidatePresentation();
}

void TabBar::invalidatePresentation()
{
    if (!presentationPublishing_ || presentationPending_) return;
    presentationPending_ = true;
    const auto generation = presentationGeneration_;
    QMetaObject::invokeMethod(this, [this, generation] {
        if (!presentationPublishing_ || generation != presentationGeneration_) return;
        presentationPending_ = false; emit presentationInvalidated();
    }, Qt::QueuedConnection);
}

void TabBar::requestRepaint()
{
    controller_.refreshPresentationLayers(); synchronizeController(); QWidget::update();
}

std::shared_ptr<const TabPresentationFrame> TabBar::presentationFrame()
{
    prepareController();
    auto frame = controller_.presentationFrame();
    synchronizeController(); return frame;
}

} // namespace window_system
