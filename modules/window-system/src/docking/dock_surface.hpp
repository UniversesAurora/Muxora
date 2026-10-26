#pragma once
#include <window_system/window_manager.hpp>
#include <window_system/view_state.hpp>
#include <window_system/tab_appearance.hpp>
#include "panes/focus_projection.hpp"
#include "edge_strip.hpp"
#include "dock_guides.hpp"
#include "tab_layout.hpp"
#include "tab_viewport_state.hpp"
#include "style/tab_title_layout.hpp"
#include "style/elevation.hpp"
#include <array>
#include <QFrame>
#include <QHash>
#include <QPointer>
#include <QPixmap>

class QSplitter;
namespace window_system {
struct CarriedLabel;
struct TabDragVisual;
class EdgeTabButton;
class GroupTabs;
class DockSurface final : public QFrame {
public:
    DockSurface(WindowManager* manager, WindowId window, QWidget* parent);
    ~DockSurface() override;
    void rebuild(const QHash<TabId, QPointer<TabContent>>& contents);
    void captureStripScrollOffsets();
    void restoreStripScrollOffsets();
    // Applies a changed tab appearance to the existing strips and rail
    // buttons. Returns false when the built strips no longer match the model;
    // the caller then refreshes.
    bool updateTabAppearance();
    bool scrollStripAt(QPoint global, QPoint delta, bool smooth = true);
    // Shows the focus accent on the strip of each window's active group.
    void refreshGroupFocus();
    // Uses surface coordinates, including when replayed beneath a card's
    // rounded corner. Never renders QWidget contents or calls itself.
    void paintBackplate(QPainter& painter, const QRect& exposed);
    [[nodiscard]] QHash<GroupId, QRect> groupRects() const;
    [[nodiscard]] QRect groupContentRect(GroupId group) const;
    [[nodiscard]] GroupId groupAt(QPoint global) const;
    [[nodiscard]] QHash<GroupId, QRect> tabBarRects() const;
    [[nodiscard]] QVector<DockStrip> dockStrips() const;
    [[nodiscard]] GroupId tabBarAt(QPoint global) const;
    [[nodiscard]] int tabInsertionIndex(GroupId group, QPoint global, TabLayoutInput::Carried carried = TabLayoutInput::Carried::Tab) const;
    [[nodiscard]] int dragLabelExtent(TabId tab, bool edge) const;
    [[nodiscard]] QPixmap dragLabelPixmap(TabId tab) const;
    void setDraggedTab(TabId tab);
    // The dragged tab of a strip, drawn lifted under the pointer while the
    // drag stays in that strip; a null tab ends it everywhere.
    void setLiftedTab(GroupId group, QPoint global, QPoint grab);
    void clearLiftedTabs();
    // A picture of dragged content over one group's pages; a null group
    // removes every picture.
    void setContentPreview(GroupId group, const QPixmap& picture);
    // The active page of this surface's active group, painted now.
    [[nodiscard]] QPixmap activeContentPicture() const;
    // Where in its tab a drag took hold of it.
    [[nodiscard]] QPoint tabGrabOffset(TabId tab, QPoint global) const;
    [[nodiscard]] QHash<Edge, QRect> edgeBarRects() const;
    [[nodiscard]] int edgeTabInsertionIndex(Edge edge, QPoint global) const;
    [[nodiscard]] QRect edgeTabInsertionRect(Edge edge, int index) const;
    void setDragHover(QPoint global);
    void clearDragHover(QPoint global);
    void setInsertionPreview(GroupId group, int index, const QVector<CarriedLabel>& carried, bool carriedSticky = false,
                             TabLayoutInput::Carried kind = TabLayoutInput::Carried::Tab);
    // The tab folder a drop at the group's insertion preview joins; null for none.
    [[nodiscard]] FolderId insertionFolder(GroupId group) const;
    // A tab folder carried within its own strip; null ends it everywhere.
    void setDraggedFolder(FolderId folder);
    [[nodiscard]] QPoint folderGrabOffset(FolderId folder, QPoint global) const;
    // Whether a point is over the group's row of sticky tabs; empty without one.
    [[nodiscard]] std::optional<bool> tabInsertionRow(GroupId group, QPoint point) const;
    // Whether a drop at the group's insertion preview makes the tab sticky.
    [[nodiscard]] bool insertionSticky(GroupId group) const;
    void setEdgeInsertionPreview(Edge edge, int index, int extent);
    void clearInsertionPreviews();
    [[nodiscard]] QRect insertionPreviewRect(GroupId group) const;
    [[nodiscard]] QRect edgeInsertionPreviewRect(Edge edge) const;
    void setDragPresentation(bool compact, bool edge);
    bool setDragTabStyle(const DockSurface& target, GroupId group, bool sticky);
    [[nodiscard]] TabDragVisual dragLabelVisual() const;
    void setIncomingVisual(GroupId group, TabDragVisual visual, QPoint global = {});
    [[nodiscard]] QHash<QUuid, QRect> captureDropPositions(GroupId group) const;
    void settleDroppedTabs(GroupId group, const QHash<QUuid, QRect>& globalPositions);
    void selectTab(TabId tab);
    void showEdgeGroup(GroupId group, TabId tab = {});
    void closePeek();
    void updatePeekSize(TabId tab);
    void setTabBarsVisible(bool visible);
    void setSideBarsVisible(bool visible);
    void setFocusScope(const WindowViewState& state);
    [[nodiscard]] QRect centralRectGlobal() const;
    // The splitter built for a central or pinned tree split node, or null.
    [[nodiscard]] QSplitter* splitterFor(QUuid node) const;
protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    // Everything a group's strip is built from. A group whose signature and
    // pages are unchanged keeps its GroupTabs across rebuilds.
    struct GroupSignature {
        QVector<TabId> tabs;
        TabId activeTab;
        GroupLocation location = GroupLocation::Central;
        Edge edge = Edge::Left;
        QStringList titles;
        QStringList colorIds;
        QVector<bool> icons;
        int stickyTabs = 0;
        TabAppearance appearance;
        quint64 themeRevision = 0;
        bool operator==(const GroupSignature&) const = default;
    };
    [[nodiscard]] GroupSignature signatureOf(const GroupRecord& group) const;
    // Split nodes (ID, orientation) and leaf groups of the central and pinned
    // trees in preorder; ratios are not part of the structure.
    using StructureSignature = QVector<QPair<QUuid, int>>;
    [[nodiscard]] StructureSignature structureSignature() const;
    bool updateInPlace(const QHash<TabId, QPointer<TabContent>>& contents);
    [[nodiscard]] QVector<QPair<GroupId, TabId>> edgeTabs(Edge edge) const;
    void configureEdgeButton(EdgeTabButton& button, TabId tab, int ordinal, const TabAppearance& appearance) const;
    void applyChangedRatios(LayoutTree node);
    [[nodiscard]] bool holdsPages(const GroupTabs& widget, const GroupRecord& group,
                                  const QHash<TabId, QPointer<TabContent>>& contents) const;
    QWidget* buildTree(LayoutTree node, const QHash<TabId, QPointer<TabContent>>& contents);
    GroupTabs* buildGroup(GroupId group, const QHash<TabId, QPointer<TabContent>>& contents);
    QWidget* buildEdge(Edge edge, const QHash<TabId, QPointer<TabContent>>& contents);
    QWidget* buildPinned(Edge edge, const QHash<TabId, QPointer<TabContent>>& contents);
    void invalidateGroupSurface(GroupId group);
    void updateRailLayout();
    void placePeek();
    void updateEdgeTabSelection(TabId tab);
    [[nodiscard]] bool centralTabBarVisible(bool requested) const;
    void savePinnedExtents();
    void restorePinnedExtents();
    void restoreFocusScope();
    void applyFocusScope();
    void refreshContentEdgeToEdge();
    [[nodiscard]] bool hasContentFocusScope() const;
    [[nodiscard]] bool ownsTab(TabId tab) const;
    QPointer<WindowManager> manager_;
    WindowId window_;
    QPointer<QWidget> root_;
    QPointer<QWidget> center_;
    QPointer<QFrame> peek_;
    QPointer<QWidget> peekResizeHandle_;
    QPointer<QWidget> peekSecondaryResizeHandle_;
    QPointer<QWidget> peekCornerResizeHandle_;
    GroupId peekGroup_;
    TabId peekTab_;
    quint64 peekGeneration_ = 0;
    quint64 rebuildGeneration_ = 0;
    QHash<GroupId, QPointer<GroupTabs>> groups_;
    QHash<GroupId, QRegion> surfaceBounds_;
    ElevationRenderer elevation_;
    bool compactPresentation_ = false;
    // Measurements only, kept separately for ordinary and sidebar formatting.
    // Rebuilds release removed tabs; no history of old drag views is retained.
    mutable QHash<TabId, std::array<TabTitleLayout, 2>> dragTitleLayouts_;
    QHash<GroupId, GroupSignature> builtGroups_;
    StructureSignature builtStructure_;
    quint64 builtThemeRevision_ = 0;
    QHash<QUuid, double> builtRatios_;
    // Unchanged groups of the previous build, consumed while rebuilding.
    QHash<GroupId, QPointer<GroupTabs>> reusableGroups_;
    QHash<Edge, QPointer<QWidget>> edgeBars_;
    QHash<GroupId, TabViewportState> groupViewportStates_;
    QHash<Edge, EdgeStrip::ScrollAnchor> edgeScrollAnchors_;
    // The appearance the current rails were built with. Only a change of
    // their label lengths makes a raw offset point at unrelated tabs.
    TabAppearance edgeAppearance_;
    TabAppearance edgeAnchorAppearance_;
    QHash<Edge, QPointer<QSplitter>> pinnedRegions_;
    QHash<QUuid, QPointer<QSplitter>> splitters_;
    QPointer<QSplitter> horizontal_;
    QPointer<QSplitter> vertical_;
    QHash<TabId, QPointer<TabContent>> contents_;
    WindowViewState focusState_;
    FocusProjection focusProjection_;
    QPointer<GroupTabs> focusedAutoHideGroup_;
    bool focusedVerticalWasHidden_ = false;
    GroupId previousPeekGroup_;
    TabId previousPeekTab_;
};
} // namespace window_system
