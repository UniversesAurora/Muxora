#pragma once

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QUuid>
#include <QVector>

#include <functional>
#include <memory>
#include <optional>

namespace window_system {

using WindowId = QUuid;
using FamilyId = QUuid;
using GroupId = QUuid;
using TabId = QUuid;
using PaneId = QUuid;

enum class WindowKind { Workbench, Floating };
enum class Placement { Center, Left, Right, Top, Bottom };
enum class Edge { Left, Right, Top, Bottom };
enum class GroupLocation { Central, Pinned, AutoHide };

// Immutable nodes allow snapshots to share unchanged trees safely. An item is a
// GroupId in a central tree and a PaneId in a tab's pane tree.
struct LayoutNode;
using LayoutTree = std::shared_ptr<const LayoutNode>;
struct LayoutNode {
    QUuid id;
    QUuid itemId;
    Qt::Orientation orientation = Qt::Horizontal;
    double ratio = 0.5;
    LayoutTree first;
    LayoutTree second;

    [[nodiscard]] bool isLeaf() const noexcept { return !first && !second; }
};

struct WindowRecord {
    WindowId id;
    FamilyId familyId;
    WindowKind kind = WindowKind::Workbench;
    LayoutTree central;
    // Edge groups have explicit order; their location and edge live in records.
    QVector<GroupId> edgeGroups;
    GroupId activeGroup;
};

struct GroupRecord {
    GroupId id;
    WindowId windowId;
    GroupLocation location = GroupLocation::Central;
    Edge edge = Edge::Left;
    QVector<TabId> tabs;
    TabId activeTab;
    int edgeExtent = 250;
};

struct TabRecord {
    TabId id;
    QString title;
    LayoutTree panes;
    PaneId activePane;
    struct PinnedPosition {
        WindowId window;
        GroupId group;
        Edge edge = Edge::Left;
        int groupIndex = 0;
        int tabIndex = 0;
        int extent = 250;
    };
    // A dormant group ID is intentional: unpinning its final tab removes the
    // visible group, while repinning can reconstruct that same partition.
    std::optional<PinnedPosition> pinnedPosition;
};

struct OperationResult {
    bool ok = false;
    QString error;
    explicit operator bool() const noexcept { return ok; }
};

class LayoutModel {
public:
    [[nodiscard]] const QHash<WindowId, WindowRecord>& windows() const noexcept;
    [[nodiscard]] const QHash<GroupId, GroupRecord>& groups() const noexcept;
    [[nodiscard]] const QHash<TabId, TabRecord>& tabs() const noexcept;
    [[nodiscard]] const WindowRecord* window(WindowId id) const noexcept;
    [[nodiscard]] const GroupRecord* group(GroupId id) const noexcept;
    [[nodiscard]] const TabRecord* tab(TabId id) const noexcept;
    [[nodiscard]] QVector<WindowId> windowOrder() const;
    [[nodiscard]] GroupId groupForTab(TabId id) const;
    [[nodiscard]] WindowId familyRoot(FamilyId id) const;
    [[nodiscard]] QVector<GroupId> centralGroups(WindowId id) const;
    [[nodiscard]] QVector<TabId> orderedTabs(WindowId id) const;
    [[nodiscard]] QVector<PaneId> panes(TabId id) const;
    [[nodiscard]] bool familyHasTabs(FamilyId id) const;
    [[nodiscard]] quint64 revision() const noexcept;

    // Failed identifier-returning operations return a null UUID without changes.
    WindowId createWindow(WindowKind kind = WindowKind::Workbench,
                          WindowId ownerWorkbench = {});
    TabId addTab(WindowId windowId, QString title, GroupId targetGroup = {});
    OperationResult renameTab(TabId tabId, QString title);
    OperationResult activateTab(TabId tabId);
    OperationResult activatePane(TabId tabId, PaneId paneId);
    OperationResult moveTab(TabId tabId, GroupId targetGroup,
                            Placement placement = Placement::Center,
                            int insertionIndex = -1);
    OperationResult moveTabToWindow(TabId tabId, WindowId targetWindow,
                                    Placement placement = Placement::Center);
    OperationResult moveGroupToEdge(GroupId groupId, Edge edge, bool pinned = false);
    OperationResult moveTabToEdge(TabId tabId, Edge edge, bool pinned = false);
    OperationResult setGroupPinned(GroupId groupId, bool pinned);
    OperationResult setTabPinned(TabId tabId, bool pinned);
    OperationResult setEdgePinned(WindowId windowId, Edge edge, bool pinned);
    OperationResult setEdgeGroupExtent(GroupId groupId, int extent);
    OperationResult moveGroupToCenter(GroupId groupId, GroupId targetGroup = {},
                                      Placement placement = Placement::Right);
    WindowId detachTab(TabId tabId, WindowKind kind = WindowKind::Floating);
    OperationResult promoteWindow(WindowId windowId);
    OperationResult demoteWindow(WindowId windowId, WindowId targetWorkbench);
    OperationResult reassignFamily(WindowId floatingWindow, WindowId targetWorkbench);
    OperationResult moveAllTabs(WindowId sourceWindow, GroupId targetGroup,
                                Placement placement = Placement::Center, int insertionIndex = -1);
    OperationResult moveAllTabsToWindow(WindowId sourceWindow, WindowId targetWindow,
                                        Placement placement = Placement::Center);
    OperationResult moveAllTabsToEdge(WindowId sourceWindow, WindowId targetWindow,
                                     Edge edge, bool pinned = false);
    OperationResult removeTab(TabId tabId);
    // Closing a Workbench closes its whole family; permission belongs to the host.
    OperationResult closeWindow(WindowId windowId);
    PaneId splitPane(TabId tabId, PaneId paneId, Placement placement);
    OperationResult removePane(TabId tabId, PaneId paneId);
    TabId extractPane(TabId sourceTab, PaneId paneId, GroupId targetGroup,
                      Placement placement = Placement::Center);
    OperationResult setSplitRatio(QUuid nodeId, double ratio);

    [[nodiscard]] QJsonObject toJson() const;
    OperationResult fromJson(const QJsonObject& json);
    [[nodiscard]] OperationResult validate() const;

private:
    OperationResult transact(const std::function<OperationResult(LayoutModel&)>& operation);
    void eraseEmptyGroup(GroupId id);
    void eraseEmptyFloatingWindows();
    WindowId createWindowUnchecked(WindowKind kind, WindowId ownerWorkbench);
    GroupId createGroupUnchecked(WindowId windowId);
    void unlinkGroupUnchecked(GroupId groupId);
    void eraseTabUnchecked(TabId tabId);
    OperationResult moveTabUnchecked(TabId tabId, GroupId targetGroup,
                                     Placement placement, int insertionIndex);
    OperationResult setTabPinnedUnchecked(TabId tabId, bool pinned);
    OperationResult moveAllTabsUnchecked(WindowId sourceWindow, GroupId targetGroup,
                                         Placement placement, int insertionIndex = -1);
    LayoutTree takeWindowTreeUnchecked(WindowId sourceWindow, WindowId targetWindow);

    QHash<WindowId, WindowRecord> windows_;
    QHash<GroupId, GroupRecord> groups_;
    QHash<TabId, TabRecord> tabs_;
    QVector<WindowId> windowOrder_;
    quint64 revision_ = 0;
};

} // namespace window_system
