// SPDX-License-Identifier: MIT
#include "workspace_window.hpp"
#include "command_strip.hpp"
#include "docking/dock_surface.hpp"
#include "window_manager_p.hpp"
#include <QLabel>
#include <QScopeGuard>
#include <QVBoxLayout>

namespace window_system {

void WorkspaceWindow::applyViewState(const WindowViewState& state)
{
    const QPointer<WorkspaceWindow> self(this);
    const QPointer<WindowManager> owner(manager_);
    const QPointer<QWidget> previousFocus(focusWidget());
    const QPointer<QWidget> activeContent(manager_->paneWidget(manager_->context(id_).pane));
    const bool alreadyProjecting = manager_->d->projectingViews.contains(id_);
    manager_->d->projectingViews.insert(id_);
    applyingViewState_ = true;
    const auto reset = qScopeGuard([self, owner, window = id_, alreadyProjecting] {
        if (self) self->applyingViewState_ = false;
        if (owner && !alreadyProjecting) owner->d->projectingViews.remove(window);
    });
    const bool wasFullscreen = viewState_.mode != ViewMode::Normal;
    const bool fullscreen = state.mode != ViewMode::Normal;
    if (fullscreen && !wasFullscreen) {
        viewNormalPlacement_ = normalPlacementGeometry();
        viewNormalMaximized_ = placementMaximized();
    }
    viewState_ = state;
    if (fullscreen) surface_->setFocusScope(state);
    if (!self) return;
    const bool ordinary = dragPresentation_ == DragPresentation::Window;
    if (chrome_) chrome_->setVisible(ordinary && state.effective.toolbars);
    if (bottomChrome_) bottomChrome_->setVisible(ordinary && state.effective.toolbars);
    if (status_) status_->setVisible(ordinary && state.effective.statusBar);
    updateFrameMargins();
    column_->activate();
    if (fullscreen) {
        if (!isFullScreen()) showFullScreen();
    } else {
        if (wasFullscreen || isFullScreen())
            restorePlacement(viewNormalPlacement_.isValid() ? viewNormalPlacement_ : normalPlacement_, viewNormalMaximized_);
        if (!self) return;
        surface_->setFocusScope(state);
    }
    if (!self) return;
    // Hiding chrome can make Qt choose a different group's first focusable
    // control. Keep that intermediate focus out of the model and return input
    // to the existing active content when the previous control disappeared.
    // QWidget also remembers the focus child of an inactive window. Updating
    // it keeps the other window's current focus and restores this one on return.
    if (activeContent && activeContent->isVisible() && (!previousFocus || !previousFocus->isVisible()))
        activeContent->setFocus(Qt::OtherFocusReason);
    if (!self) return;
    updateWindowControlState();
    updateCommandStates();
}

} // namespace window_system
