// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/layout_model.hpp>

#include <QHash>
#include <QPointer>
#include <QQuickItem>

namespace window_system {
class WindowManager;
}

namespace window_system::quick {

class ContentHost;
class GroupItem;
class SplitTreeItem;
class TabContentItem;

// One window's docking area in Quick, built from the model on every
// layoutChanged: the central tree of tab groups. Groups and pages keep their
// identity across rebuilds; user actions go through the manager's API.
class DockSurfaceItem final : public QQuickItem {
    Q_OBJECT
public:
    DockSurfaceItem(WindowManager& manager, ContentHost& host, WindowId window, QQuickItem* parent = nullptr);
    [[nodiscard]] WindowId windowId() const { return window_; }
    void sync();
    [[nodiscard]] QSize minimumSize() const;
    [[nodiscard]] GroupItem* group(GroupId id) const;
    // The layer above every group that draws dragged labels.
    [[nodiscard]] QQuickItem* foregroundLayer() const { return foreground_; }

protected:
    void geometryChange(const QRectF& now, const QRectF& previous) override;

private:
    GroupItem* ensureGroup(GroupId id);
    void syncGroup(GroupItem& view, const GroupRecord& record);
    TabContentItem* page(TabId tab);
    void layoutCenter();

    WindowManager& manager_;
    ContentHost& host_;
    WindowId window_;
    SplitTreeItem* center_;
    QQuickItem* foreground_;
    QHash<GroupId, QPointer<GroupItem>> groups_;
    QHash<TabId, QPointer<TabContentItem>> pages_;
    // The tabs each group's strip was built from; a change re-adds them.
    QHash<GroupId, QVector<TabId>> builtTabs_;
    bool syncing_ = false;
};

} // namespace window_system::quick
