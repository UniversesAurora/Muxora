#pragma once

#include <QHash>
#include <QJsonObject>
#include <QSize>
#include <QString>
#include <QUuid>
#include <QVector>

#include <functional>
#include <array>
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
enum class TabSortOrder { AlphabeticalAscending, AlphabeticalDescending, OpenedAscending, OpenedDescending, VisitedAscending, VisitedDescending };

// Missing dimensions use the default cross-edge extent or stretch along the
// edge. Left/right share one policy; top/bottom share a separate policy.
struct PeekSizePolicy {
    std::optional<int> width;
    std::optional<int> height;
    bool operator==(const PeekSizePolicy&) const = default;
};

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
    // Each pinned side has its own two-dimensional group tree. Auto-hide
    // records remain ordered in edgeGroups and never enter these trees.
    std::array<LayoutTree, 4> pinnedLayouts;
    // One bounded skeleton per side remembers dormant pinned slots. Nodes have
    // independent identities; it never stores tab contents or a layout history.
    std::array<LayoutTree, 4> pinnedRestoreLayouts;
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
    // Compatibility cross-edge defaults from older snapshots/API calls. An
    // unspecified along-edge dimension continues to stretch the available area.
    std::optional<QSize> peekSize;
    // Explicit per-orientation policies override the legacy defaults and
    // survive pinning or window moves without resizing pinned partitions.
    std::optional<PeekSizePolicy> verticalPeekSize;
    std::optional<PeekSizePolicy> horizontalPeekSize;
    QString colorId;
    quint64 openedOrder = 0;
    quint64 visitedOrder = 0;
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
    [[nodiscard]] LayoutTree pinnedLayout(WindowId window, Edge edge) const;
    [[nodiscard]] QVector<TabId> orderedTabs(WindowId id) const;
    [[nodiscard]] QVector<TabId> autoHiddenTabs(WindowId windowId, Edge edge) const;
    [[nodiscard]] QVector<PaneId> panes(TabId id) const;
    [[nodiscard]] TabId tabForPane(PaneId id) const;
    [[nodiscard]] bool familyHasTabs(FamilyId id) const;
    // Counts committed changes of this model. A host that replaces the whole
    // model (restoring a snapshot, rolling back a copy) can repeat a value.
    [[nodiscard]] quint64 revision() const noexcept;
    // Identifies the committed records: copies share it, and every
    // successful operation or fromJson() assigns a new process-wide value,
    // so equal IDs mean identical records even when revisions repeat.
    // A default-constructed model has 0. Not serialized.
    [[nodiscard]] quint64 stateId() const noexcept;
    // The tree that holds a split node: a window's central tree, one pinned
    // side tree of a window, or a tab's pane tree. Layout-tree information
    // only; it says nothing about the widgets a host builds for that tree.
    struct NodeOwner {
        enum class Kind : quint8 { Central, Pinned, Tab } kind = Kind::Central;
        QUuid owner;            // WindowId (Central/Pinned) or TabId (Tab)
        Edge edge = Edge::Left; // Pinned only
        bool operator==(const NodeOwner&) const = default;
    };
    // O(1) on a committed model. nullopt for unknown IDs, leaves and the
    // remembered pinned restore skeletons, which setSplitRatio() cannot edit.
    [[nodiscard]] std::optional<NodeOwner> ownerOfNode(QUuid node) const;

    // Failed identifier-returning operations return a null UUID without changes.
    WindowId createWindow(WindowKind kind = WindowKind::Workbench,
                          WindowId ownerWorkbench = {});
    TabId addTab(WindowId windowId, QString title, GroupId targetGroup = {}, int insertionIndex = -1);
    // Create the tab directly in an adjacent central or pinned group in one revision.
    // The existing group's active tab, order and visit history stay intact.
    TabId addTabToNewGroup(WindowId windowId, QString title, GroupId targetGroup, Placement placement);
    OperationResult renameTab(TabId tabId, QString title);
    OperationResult setTabColor(TabId tabId, QString colorId);
    OperationResult sortTabs(GroupId group, TabSortOrder order);
    // Exchange complete visible-group slots (tree position or pinned edge),
    // preserving both group identities, contents and each slot's extent.
    OperationResult swapGroups(GroupId first, GroupId second);
    // Keep the target slot and append/insert the source tabs in stable order.
    // Auto-hide records are an implementation detail, not visible tab groups.
    OperationResult mergeGroups(GroupId source, GroupId target, int insertionIndex = -1);
    // Relocate the source beside the target; this never means exchange/merge.
    // All four directions retain the target's central or pinned region.
    OperationResult moveGroup(GroupId group, GroupId target, Placement placement);
    OperationResult moveGroupToWindow(GroupId group, WindowId target, Placement placement = Placement::Right);
    OperationResult moveGroupToEdge(GroupId group, WindowId target, Edge edge, bool pinned);
    WindowId detachGroup(GroupId group, WindowKind kind = WindowKind::Floating);
    // Activation and setSplitRatio() skip the full copy-and-validate
    // transaction; they always advance the revision, even when nothing
    // changes. Adding state to activation means updating that fast path too.
    OperationResult activateTab(TabId tabId);
    OperationResult activatePane(TabId tabId, PaneId paneId);
    OperationResult moveTab(TabId tabId, GroupId targetGroup,
                            Placement placement = Placement::Center,
                            int insertionIndex = -1);
    OperationResult moveTabToWindow(TabId tabId, WindowId targetWindow,
                                    Placement placement = Placement::Center);
    OperationResult moveGroupToEdge(GroupId groupId, Edge edge, bool pinned = false);
    OperationResult moveTabToEdge(TabId tabId, Edge edge, bool pinned = false);
    // The index addresses the side's visible tab sequence after removing the
    // moving tab; -1 appends. Internal edge groups do not create UI boundaries.
    OperationResult moveTabToEdgeBar(TabId tabId, WindowId targetWindow, Edge edge,
                                     int insertionIndex = -1);
    OperationResult moveAllTabsToEdgeBar(WindowId sourceWindow, WindowId targetWindow,
                                         Edge edge, int insertionIndex = -1);
    OperationResult setGroupPinned(GroupId groupId, bool pinned);
    OperationResult setTabPinned(TabId tabId, bool pinned);
    OperationResult setEdgePinned(WindowId windowId, Edge edge, bool pinned);
    OperationResult setEdgeGroupExtent(GroupId groupId, int extent);
    // nullopt restores the default. Explicit logical sizes require width >= 140
    // and height >= 100, with each dimension at most 16777215 (Qt's limit).
    // This does not resize pinned partitions or change tab placement.
    OperationResult setPeekSize(TabId tabId, std::optional<QSize> size);
    [[nodiscard]] PeekSizePolicy peekSizePolicy(TabId tabId, Edge edge) const;
    OperationResult setPeekSizePolicy(TabId tabId, Edge edge, PeekSizePolicy size);
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
    // Transfer into a target tab's pane tree. Last-pane transfer removes the
    // empty source tab/group, while keeping the carried leaf and Pane ID.
    OperationResult movePane(TabId sourceTab, PaneId paneId, TabId targetTab,
                             PaneId targetPane = {}, Placement placement = Placement::Right);
    OperationResult swapPanes(TabId tabId, PaneId first, PaneId second);
    // Existing active tab receives a split. An empty window receives a new tab.
    TabId movePaneToWindow(TabId sourceTab, PaneId paneId, WindowId targetWindow,
                           Placement placement = Placement::Right);
    TabId extractPane(TabId sourceTab, PaneId paneId, GroupId targetGroup,
                      Placement placement = Placement::Center);
    TabId convertGroupToTab(GroupId group, Placement placement = Placement::Right);
    OperationResult moveGroupIntoTab(GroupId group, TabId targetTab, PaneId targetPane = {}, Placement placement = Placement::Right);
    OperationResult moveTabIntoTab(TabId sourceTab, TabId targetTab, PaneId targetPane = {}, Placement placement = Placement::Right);
    GroupId convertTabToGroup(TabId tab, Placement placement = Placement::Right);
    OperationResult setSplitRatio(QUuid nodeId, double ratio);

    [[nodiscard]] QJsonObject toJson() const;
    OperationResult fromJson(const QJsonObject& json);
    [[nodiscard]] OperationResult validate() const;

private:
    // Model tests inspect and invalidate the lookup index; hosts never define it.
    friend struct LayoutModelTestAccess;
    OperationResult transact(const std::function<OperationResult(LayoutModel&)>& operation);
    void rebuildIndex();
    void scanIndex(QHash<TabId, GroupId>& tabGroup, QHash<PaneId, TabId>& paneTab,
                   QHash<QUuid, NodeOwner>& nodeOwner) const;
    bool activateIndexed(TabId tabId, GroupId groupId, PaneId paneId);
    void synchronizePinnedLayouts(WindowId onlyWindow = {});
    void rememberPinnedLayoutUnchecked(WindowId window, Edge edge);
    void restorePinnedLayoutUnchecked(WindowId window, Edge edge, GroupId group);
    void reconcilePinnedRestoreLayouts();
    // JSON stores exact integers through 2^53 - 1. Rare rollover compacts ranks.
    static constexpr quint64 maximumTabOrder = (quint64(1) << 53) - 1;
    quint64 nextTabOrder();
    void eraseEmptyGroup(GroupId id);
    void eraseEmptyFloatingWindows();
    WindowId createWindowUnchecked(WindowKind kind, WindowId ownerWorkbench);
    GroupId createGroupUnchecked(WindowId windowId);
    void unlinkGroupUnchecked(GroupId groupId);
    void eraseTabUnchecked(TabId tabId);
    OperationResult moveTabUnchecked(TabId tabId, GroupId targetGroup,
                                     Placement placement, int insertionIndex);
    OperationResult setTabPinnedUnchecked(TabId tabId, bool pinned);
    void placePinnedGroupOutermostUnchecked(GroupId groupId);
    void rememberPinnedPositionsUnchecked(WindowId windowId, Edge edge);
    OperationResult moveTabToEdgeBarUnchecked(TabId tabId, WindowId targetWindow,
                                              Edge edge, int insertionIndex);
    OperationResult moveAllTabsUnchecked(WindowId sourceWindow, GroupId targetGroup,
                                         Placement placement, int insertionIndex = -1);
    LayoutTree takeWindowTreeUnchecked(WindowId sourceWindow, WindowId targetWindow);

    QHash<WindowId, WindowRecord> windows_;
    QHash<GroupId, GroupRecord> groups_;
    QHash<TabId, TabRecord> tabs_;
    quint64 orderCounter_ = 0;
    QVector<WindowId> windowOrder_;
    quint64 revision_ = 0;
    quint64 stateId_ = 0;
    // Reverse lookups derived from the records above. They are not serialized.
    // transact() invalidates them on its candidate, so an operation in
    // progress falls back to scanning; every committed state rebuilds them
    // before validate(), which also checks that they match the records.
    QHash<TabId, GroupId> tabGroup_;
    QHash<PaneId, TabId> paneTab_;
    QHash<QUuid, NodeOwner> nodeOwner_;
    bool indexValid_ = false;
};

} // namespace window_system
