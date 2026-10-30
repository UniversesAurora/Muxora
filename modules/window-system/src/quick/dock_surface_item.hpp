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
class PeekItem;
class RailItem;
class SplitTreeItem;
class TabContentItem;

// One window's docking area in Quick, built from the model on every
// layoutChanged: the central tree of tab groups, the pinned regions around
// it and the auto-hide rails at the window's edges, whose tabs open in a
// panel over the rest. Groups and pages keep their identity across
// rebuilds; user actions go through the manager's API.
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
    [[nodiscard]] RailItem* rail(Edge edge) const;
    [[nodiscard]] PeekItem* peek() const;
    [[nodiscard]] TabId peekTab() const { return peekTab_; }
    void showPeek(GroupId group, TabId tab);
    void closePeek();

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
