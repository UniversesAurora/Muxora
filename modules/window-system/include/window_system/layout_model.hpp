#pragma once

#include <QHash>
#include <QJsonObject>
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

// Tab folders (tab strip design 12) gather adjacent tabs of one strip: a
// name, a color from the tab color presets, and whether it is collapsed.
using FolderId = QUuid;
struct TabFolderRecord {
    FolderId id;
    QString name;
    QString colorId;
    bool collapsed = false;
    bool operator==(const TabFolderRecord&) const = default;
};

struct GroupRecord {
    GroupId id;
    WindowId windowId;
    GroupLocation location = GroupLocation::Central;
    Edge edge = Edge::Left;
    QVector<TabId> tabs;
    // tabs[0, stickyTabs) are stuck to the front of this group's strip.
    // Auto-hide groups never have any.
    int stickyTabs = 0;
    TabId activeTab;
    int edgeExtent = 250;
    // A vertical tab strip's own width (0: the appearance default) and
    // whether it shows only icons.
    int tabStripWidth = 0;
    bool tabStripIconsOnly = false;
    // A name the user gave the group; empty when it has none.
    QString name;
    // The folders of this group's strip. Each one's tabs are adjacent, after
    // the sticky tabs; a folder without tabs does not exist.
    QVector<TabFolderRecord> folders;
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
    // Per-orientation policies survive pinning or window moves without
    // resizing pinned partitions.
    std::optional<PeekSizePolicy> verticalPeekSize;
    std::optional<PeekSizePolicy> horizontalPeekSize;
    QString colorId;
    // The tab folder holding this tab in its group; null when none.
    FolderId folder;
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
    // A new tab joins `folder` when given; it must be next to that folder's
    // tabs in the target group.
    TabId addTab(WindowId windowId, QString title, GroupId targetGroup = {}, int insertionIndex = -1, FolderId folder = {});
    // Create the tab directly in an adjacent central or pinned group in one revision.
    // The existing group's active tab, order and visit history stay intact.
    TabId addTabToNewGroup(WindowId windowId, QString title, GroupId targetGroup, Placement placement);
    OperationResult renameTab(TabId tabId, QString title);
    OperationResult setTabColor(TabId tabId, QString colorId);
    OperationResult sortTabs(GroupId group, TabSortOrder order);
    // Sticks a tab to the end of its group's sticky run, or unsticks it to
    // the first position after that run. Auto-hide groups cannot stick tabs.
    OperationResult setTabSticky(TabId tabId, bool sticky);
    OperationResult setGroupTabStrip(GroupId groupId, int width, bool iconsOnly);
    OperationResult renameGroup(GroupId groupId, QString name);
    // Tab folders. Creating one moves the tabs together at the first one's
    // place; any other change keeps the folder's tabs adjacent.
    [[nodiscard]] const TabFolderRecord* folder(FolderId folderId) const;
    [[nodiscard]] GroupId groupForFolder(FolderId folderId) const;
    [[nodiscard]] QVector<TabId> folderTabs(FolderId folderId) const;
    OperationResult createFolder(QVector<TabId> tabs, FolderId folderId, QString name = {}, QString colorId = {});
    OperationResult addTabToFolder(TabId tabId, FolderId folderId);
    OperationResult removeTabFromFolder(TabId tabId);
    OperationResult updateFolder(const TabFolderRecord& folder);
    OperationResult ungroupFolder(FolderId folderId);
    // Moves a whole folder among its strip's items (a tab or a folder) by
    // `steps`, cycling at the ends.
    OperationResult moveFolderWithinGroup(FolderId folderId, int steps);
    // Moves a whole folder into another group at `index` (-1 appends),
    // keeping its name, color and collapsed state.
    OperationResult moveFolderToGroup(FolderId folderId, GroupId target, int index = -1);
    OperationResult mergeFolder(FolderId source, FolderId target);
    // A folder moved to a new window keeps its name, color and state.
    WindowId detachFolder(FolderId folderId, WindowKind kind = WindowKind::Floating);
    // The folder's tabs become a new tab group beside the folder's group.
    GroupId convertFolderToGroup(FolderId folderId, Placement placement);
    // Every tab of `source` joins a new folder at the end of `target`.
    OperationResult convertGroupToFolder(GroupId source, GroupId target, FolderId folderId);
    [[nodiscard]] bool isTabSticky(TabId tabId) const;
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
    // At the end of a sticky run, `sticky` decides whether a dropped tab
    // joins it; without it the tab keeps its state. Elsewhere the position
    // decides (tab strip design 5). `folder` likewise decides the tab folder
    // at a folder's edge (a null ID for none); without it a tab placed
    // between two tabs of one folder joins it, and one that leaves its
    // folder's tabs leaves the folder (tab strip design 12).
    OperationResult moveTab(TabId tabId, GroupId targetGroup,
                            Placement placement = Placement::Center,
                            int insertionIndex = -1, std::optional<bool> sticky = {},
                            std::optional<FolderId> folder = {});
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
    // Missing dimensions restore their defaults. Explicit logical sizes require
    // width >= 140 and height >= 100, at most 16777215 (Qt's limit).
    // This does not resize pinned partitions or change tab placement.
    [[nodiscard]] PeekSizePolicy peekSizePolicy(TabId tabId, Edge edge) const;
    OperationResult setPeekSizePolicy(TabId tabId, Edge edge, PeekSizePolicy size);
    OperationResult moveGroupToCenter(GroupId groupId, GroupId targetGroup = {},
                                      Placement placement = Placement::Right);
    WindowId detachTab(TabId tabId, WindowKind kind = WindowKind::Floating);
    OperationResult promoteWindow(WindowId windowId);
    OperationResult demoteWindow(WindowId windowId, WindowId targetWorkbench);
    OperationResult reassignFamily(WindowId floatingWindow, WindowId targetWorkbench);
    // Tab folders come along. A non-null `asFolder` instead gathers every
    // tab into a new folder of that identity (tab strip design 12).
    OperationResult moveAllTabs(WindowId sourceWindow, GroupId targetGroup,
                                Placement placement = Placement::Center, int insertionIndex = -1,
                                std::optional<bool> sticky = {}, FolderId asFolder = {});
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
    // Order edits that keep each group's sticky run contiguous. detach returns
    // whether the tab was sticky. attach without an index puts a sticky tab at
    // the end of the run and any other tab at the end of the group; with an
    // index the position decides, and the slot right after the run keeps a
    // tab that was sticky.
    static bool detachTabUnchecked(GroupRecord& group, qsizetype index);
    static qsizetype attachTabUnchecked(GroupRecord& group, TabId tab, qsizetype index, bool sticky);
    // Keeps every folder adjacent, outside the sticky run and in one group;
    // drops folders left without tabs.
    void normalizeFoldersUnchecked();
    [[nodiscard]] FolderId folderOfTab(TabId id) const;
    // Tabs that moved into `target` together keep their folders there:
    // `folders` holds each tab's folder before the move.
    void carryFoldersUnchecked(const QHash<TabId, FolderId>& folders, const QVector<TabFolderRecord>& records, GroupId target);
    OperationResult moveTabUnchecked(TabId tabId, GroupId targetGroup,
                                     Placement placement, int insertionIndex, std::optional<bool> sticky = {},
                                     std::optional<FolderId> folder = {});
    OperationResult setTabPinnedUnchecked(TabId tabId, bool pinned);
    void placePinnedGroupOutermostUnchecked(GroupId groupId);
    void rememberPinnedPositionsUnchecked(WindowId windowId, Edge edge);
    OperationResult moveTabToEdgeBarUnchecked(TabId tabId, WindowId targetWindow,
                                              Edge edge, int insertionIndex);
    OperationResult moveAllTabsUnchecked(WindowId sourceWindow, GroupId targetGroup,
                                         Placement placement, int insertionIndex = -1,
                                         std::optional<bool> sticky = {}, FolderId asFolder = {});
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
