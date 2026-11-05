// SPDX-License-Identifier: MIT
#pragma once

#include "docking/drop_target.hpp"
#include "docking/drag_start.hpp"
#include "docking/tab_drag_visual.hpp"
#include "docking/tab_strip_controller.hpp"

#include <QAbstractNativeEventFilter>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <functional>
#include <optional>

class QQuickWindow;

namespace window_system {
class WindowManager;
}

namespace window_system::quick {

class DockSurfaceItem;

// A tab or folder carried with the mouse across the Quick windows, with the
// Widgets DragController's rules. Within its own strip the tab is lifted and
// follows the pointer. Leaving the strip tears it out into a floating window
// that follows the pointer; over a strip or rail the target opens its gap,
// over content the dock shows its guides. Targets come from the shared
// dropTargetAt() and drops go through the manager. Released elsewhere, the
// tab stays in its new window; Escape puts it back. A floating window
// dragged by its title is carried the same way, with all its tabs, and so
// is a folder once torn out. With the real mouse the carried window moves
// in the system's move loop, as a title drag does, and targets follow the
// moves it reports. Over a strip or rail the window hides, the target
// paints its tabs at the pointer, and the drag takes the mouse back from
// the loop until the pointer leaves. A target the pointer rests on comes
// to the front after the behavior's delay.
class PopupMenu;

class TabDrag final : public QObject, public QAbstractNativeEventFilter {
public:
    using SurfaceLookup = std::function<DockSurfaceItem*(WindowId)>;
    // Builds the Quick windows the model has now and returns this one's.
    using Realize = std::function<QQuickWindow*(WindowId)>;
    TabDrag(WindowManager& manager, SurfaceLookup surfaces, Realize realize, QObject* parent = nullptr);
    ~TabDrag() override;
    void beginTab(WindowId window, GroupId group, TabId tab, const DragStart& start);
    void beginFolder(WindowId window, GroupId group, FolderId folder, const DragStart& start);
    // An auto-hide tab from its rail: it moves among the rail's buttons, and
    // tears out like a strip's tab once the pointer leaves the rail.
    void beginRailTab(WindowId window, Edge edge, TabId tab, const DragStart& start);
    // A floating window by its title, from where the button went down.
    void beginWindow(WindowId window, QPoint pressGlobal);
    void cancel();
    [[nodiscard]] bool active() const { return active_; }
    [[nodiscard]] const DropTarget& target() const { return target_; }
    // The torn-out window while it follows the pointer.
    [[nodiscard]] WindowId movingWindow() const { return detached_ ? movingWindow_ : WindowId{}; }

protected:
    // Mouse moves and the release reach the window holding the mouse; they
    // are read there, before any item.
    bool eventFilter(QObject* watched, QEvent* event) override;
    // The system's move loop reports the carried window's moves and its end.
    bool nativeEventFilter(const QByteArray& type, void* message, qintptr* result) override;

private:
    void begin(WindowId window, GroupId group, const DragStart& start);
    // Tab during a drag: a list of the other windows, the chosen one becomes
    // the target window and comes forward, as in the Widgets drag.
    void selectTarget();
    void track(QPoint global);
    void queueSystemMove();
    void enterSystemMove();
    void systemMoveEnded(quint64 generation);
    bool detach(QPoint global);
    void update(QPoint global);
    void finish(QPoint global);
    void stop(bool restoreCarrier = true);
    [[nodiscard]] TabDragVisual carriedLabel(DockSurfaceItem& target);
    void restoreCarrier();
    void showCarrier();
    void leaveSystemMove();
    void releaseSystemMove();
    [[nodiscard]] WindowId windowAt(QPoint global) const;
    [[nodiscard]] DropTarget hit(QPoint global) const;
    [[nodiscard]] bool overSourceStrip(QPoint global) const;
    [[nodiscard]] bool carriedExists() const;
    [[nodiscard]] QVector<CarriedLabel> carriedLabels() const;
    [[nodiscard]] QVector<TabId> carriedTabs() const;
    [[nodiscard]] TabLayoutInput::Carried carried() const;
    [[nodiscard]] int carriedCount() const;

    WindowManager& manager_;
    SurfaceLookup surfaces_;
    Realize realize_;
    TabId tab_;
    FolderId folder_;
    WindowId sourceWindow_;
    GroupId sourceGroup_;
    int sourceIndex_ = -1;
    DragAnchor grab_;
    bool sticky_ = false;
    bool lifted_ = false;
    bool active_ = false;
    bool detached_ = false;
    WindowId movingWindow_;
    QPointer<QQuickWindow> moving_;
    // The drag started on this rail.
    std::optional<Edge> sourceRail_;
    // The pointer's place in the moving window.
    QPoint offset_;
    // A carried floating window's place, for Escape.
    QPoint originalPosition_;
    bool originalMaximized_ = false;
    // Set when the drag starts with the real button held.
    bool realInput_ = false;
    bool systemMoving_ = false;
    // Over a strip: the loop is ending and the drag takes the mouse back.
    bool leavingSystemMove_ = false;
    // The carried window shows only its tabs, in the target strip's look.
    bool carrierCompact_ = false;
    // Over a strip or a rail the target paints the tabs; the window hides.
    bool carrierHidden_ = false;
    // The carried content, taken as the window turns compact over a strip.
    QImage contentPicture_;
    bool updateQueued_ = false;
    // The target window waiting to come to the front, and the last one that
    // did; a window comes forward once per stay.
    QTimer revealTimer_;
    WindowId revealCandidate_;
    WindowId revealedWindow_;
    QPoint lastGlobal_;
    quintptr movingHandle_ = 0;
    // Ends queued by one move loop are ignored by the next drag.
    quint64 moveGeneration_ = 0;
    DropTarget target_;
    WindowId forcedWindow_;
    QPointer<PopupMenu> chooser_;
};

} // namespace window_system::quick
