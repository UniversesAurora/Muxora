// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/layout_model.hpp>

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
class GroupItem;
class SplitTreeItem;
class TabContentItem;

// One window's docking area in Quick, built from the model on every
// layoutChanged: the central tree of tab groups with the pinned regions
// around it. Groups and pages keep their identity across rebuilds; user
// actions go through the manager's API.
class DockSurfaceItem final : public QQuickItem {
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

protected:
    void geometryChange(const QRectF& now, const QRectF& previous) override;

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
    [[nodiscard]] LayoutTree composedTree(QSize area);
    void resizePinned(Edge edge, double ratio);

    WindowManager& manager_;
    ContentHost& host_;
    WindowId window_;
    SplitTreeItem* center_;
    QQuickItem* foreground_;
    int divider_ = 1;
    LayoutTree central_;
    std::array<PinnedRegion, 4> pinned_;
    QHash<GroupId, QPointer<GroupItem>> groups_;
    // Pinned groups carry their unpin actions; a group that changes place
    // gets a new view.
    QHash<GroupId, GroupLocation> locations_;
    QHash<TabId, QPointer<TabContentItem>> pages_;
    // The tabs each group's strip was built from; a change re-adds them.
    QHash<GroupId, QVector<TabId>> builtTabs_;
    bool syncing_ = false;
};

} // namespace window_system::quick
