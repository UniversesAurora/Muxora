// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/layout_model.hpp>
#include "docking/drop_target.hpp"
#include "docking/tab_strip_controller.hpp"

#include <QHash>
#include <QPointer>
#include <QQuickItem>
#include <array>
#include <optional>

namespace window_system {
class WindowManager;
}

namespace window_system::quick {

class ContentHost;
class DockGuideItem;
class GroupItem;
class PeekItem;
class RailItem;
class SplitTreeItem;
class TabContentItem;
class TabDrag;

// One window's docking area in Quick, built from the model on every
// layoutChanged: the central tree of tab groups, the pinned regions around
// it and the auto-hide rails at the window's edges, whose tabs open in a
// panel over the rest. Groups and pages keep their identity across
// rebuilds; user actions go through the manager's API. Drags read it through
// the shared drop-target queries, in global coordinates.
class DockSurfaceItem final : public QQuickItem, public DropSurface {
    Q_OBJECT
public:
    DockSurfaceItem(WindowManager& manager, ContentHost& host, WindowId window, QQuickItem* parent = nullptr);
    [[nodiscard]] WindowId windowId() const { return window_; }
    void sync();
    [[nodiscard]] QSize minimumSize() const;
    [[nodiscard]] GroupItem* group(GroupId id) const;
    // In this item's coordinates.
    [[nodiscard]] QRect groupRect(GroupId id) const;
    // The layer above every group that draws dragged labels.
    [[nodiscard]] QQuickItem* foregroundLayer() const { return foreground_; }
    [[nodiscard]] RailItem* rail(Edge edge) const;
    [[nodiscard]] PeekItem* peek() const;
    [[nodiscard]] TabId peekTab() const { return peekTab_; }
    void showPeek(GroupId group, TabId tab);
    void closePeek();
    // The workspace's drag, which the strips start.
    void setTabDrag(TabDrag* drag) { drag_ = drag; }

    [[nodiscard]] QRect globalRect() const override;
    [[nodiscard]] QHash<GroupId, QRect> groupRects() const override;
    [[nodiscard]] QRect groupContentRect(GroupId group) const override;
    [[nodiscard]] QRect centralRectGlobal() const override;
    [[nodiscard]] QHash<GroupId, QRect> tabBarRects() const override;
    [[nodiscard]] QVector<DockStrip> dockStrips() const override;
    [[nodiscard]] int tabInsertionIndex(GroupId group, QPoint global, TabLayoutInput::Carried carried) const override;
    [[nodiscard]] std::optional<bool> tabInsertionRow(GroupId group, QPoint global) const override;
    [[nodiscard]] QHash<Edge, QRect> edgeBarRects() const override;
    [[nodiscard]] int edgeTabInsertionIndex(Edge edge, QPoint global, QRect carriedGlobal) const override;
    [[nodiscard]] QRect edgeTabInsertionRect(Edge edge, int index) const override;

    // The drag's presentation.
    void setDraggedTab(TabId tab);
    void setDraggedFolder(FolderId folder);
    // The gap in one group's strip; a null group closes the gap.
    void showInsertion(GroupId group, int index, const QVector<CarriedLabel>& carried, bool carriedSticky,
                       TabLayoutInput::Carried kind);
    void showGuides(const DropTarget& target, int count, bool whole);
    void clearGuides();
    // A carried window over another window's strip: its strips show only
    // their tabs in that strip's look, and their label is painted there.
    void setDragPresentation(bool compact);
    bool setDragTabStyle(const DockSurfaceItem& target, GroupId group, bool sticky);
    [[nodiscard]] TabDragVisual dragLabelVisual() const;
    // The carried label at the pointer in one group's strip; a null group
    // clears it.
    void showIncoming(GroupId group, const TabDragVisual& visual, QPoint global);
    void clearDragPresentation();

protected:
    void geometryChange(const QRectF& now, const QRectF& previous) override;
    // Watches the window while a panel is open: Escape or a click elsewhere
    // closes it, and the click still reaches its target.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // A pinned side: its group tree and the split joining it to the rest.
    // The split exists only in this view. Its ratio follows the groups'
    // saved extents, so the center takes every change of the window size.
    struct PinnedRegion {
        LayoutTree tree;
        QUuid split = QUuid::createUuid();
        int available = 0;
        std::optional<int> dragged;
    };
    GroupItem* ensureGroup(GroupId id, const GroupRecord& record);
    void releaseGroup(GroupId id);
    void syncGroup(GroupItem& view, const GroupRecord& record);
    TabContentItem* page(TabId tab);
    void layoutCenter();
    void syncRails();
    void placePeek();
    [[nodiscard]] LayoutTree composedTree(QSize area);
    void resizePinned(Edge edge, double ratio);

    WindowManager& manager_;
    ContentHost& host_;
    WindowId window_;
    SplitTreeItem* center_;
    QQuickItem* foreground_;
    DockGuideItem* guides_;
    TabDrag* drag_ = nullptr;
    GroupId insertionGroup_;
    GroupId incomingGroup_;
    int divider_ = 1;
    LayoutTree central_;
    std::array<PinnedRegion, 4> pinned_;
    QHash<GroupId, QPointer<GroupItem>> groups_;
    // Pinned groups carry their unpin actions; a group that changes place
    // gets a new view.
    QHash<GroupId, GroupLocation> locations_;
    QHash<TabId, QPointer<TabContentItem>> pages_;
    std::array<QPointer<RailItem>, 4> rails_;
    // The tabs and titles each rail was built from; a change rebuilds it.
    std::array<QVector<QPair<TabId, QString>>, 4> railTabs_;
    QPointer<PeekItem> peek_;
    GroupId peekGroup_;
    TabId peekTab_;
    // Inside the visible rails.
    QRect inner_;
    // The tabs each group's strip was built from; a change re-adds them.
    QHash<GroupId, QVector<TabId>> builtTabs_;
    bool syncing_ = false;
};

} // namespace window_system::quick
