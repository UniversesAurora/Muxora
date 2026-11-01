// SPDX-License-Identifier: MIT
#pragma once

#include "docking/drop_target.hpp"
#include "docking/drag_start.hpp"
#include "docking/tab_strip_controller.hpp"

#include <QObject>
#include <functional>

namespace window_system {
class WindowManager;
}

namespace window_system::quick {

class DockSurfaceItem;

// A tab or folder carried with the mouse across the Quick windows. Within
// its own strip the tab is lifted and follows the pointer; elsewhere the
// strips show the insertion gap and the dock shows its guides. Targets come
// from the shared dropTargetAt(), and the drop goes through the manager as
// with the Widgets DragController. Nothing changes in the model before the
// drop. Tearing out into a new window and the sidebars come later (10.3,
// sub-step 5): a release outside every target leaves the tab where it was.
class TabDrag final : public QObject {
public:
    using SurfaceLookup = std::function<DockSurfaceItem*(WindowId)>;
    TabDrag(WindowManager& manager, SurfaceLookup surfaces, QObject* parent = nullptr);
    ~TabDrag() override;
    void beginTab(WindowId window, GroupId group, TabId tab, const DragStart& start);
    void beginFolder(WindowId window, GroupId group, FolderId folder, const DragStart& start);
    void cancel();
    [[nodiscard]] bool active() const { return active_; }
    [[nodiscard]] const DropTarget& target() const { return target_; }

protected:
    // Mouse moves and the release reach the window under the system's
    // capture; they are read there, before any item.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void begin(WindowId window, GroupId group, const DragStart& start);
    void update(QPoint global);
    void finish(QPoint global);
    void stop();
    [[nodiscard]] DropTarget hit(QPoint global) const;
    [[nodiscard]] bool overSourceStrip(QPoint global) const;
    [[nodiscard]] bool carriedExists() const;
    [[nodiscard]] QVector<CarriedLabel> carriedLabels() const;
    [[nodiscard]] TabLayoutInput::Carried carried() const;
    [[nodiscard]] int carriedCount() const;

    WindowManager& manager_;
    SurfaceLookup surfaces_;
    TabId tab_;
    FolderId folder_;
    WindowId sourceWindow_;
    GroupId sourceGroup_;
    DragAnchor grab_;
    bool sticky_ = false;
    bool lifted_ = false;
    bool active_ = false;
    DropTarget target_;
};

} // namespace window_system::quick
