#pragma once
#include <window_system/window_manager.hpp>
#include <window_system/view_state.hpp>
#include <window_system/tab_appearance.hpp>
#include "panes/focus_projection.hpp"
#include "tab_viewport_state.hpp"
#include <QFrame>
#include <QHash>
#include <QPointer>
#include <QPixmap>

class QSplitter;
namespace window_system {
class GroupTabs;
class DockSurface final : public QFrame {
public:
    DockSurface(WindowManager* manager, WindowId window, QWidget* parent);
    void rebuild(const QHash<TabId, QPointer<TabContent>>& contents);
    void captureStripScrollOffsets();
    void restoreStripScrollOffsets();
    bool scrollStripAt(QPoint global, QPoint delta);
    [[nodiscard]] QHash<GroupId, QRect> groupRects() const;
    [[nodiscard]] GroupId groupAt(QPoint global) const;
    [[nodiscard]] QHash<GroupId, QRect> tabBarRects() const;
    [[nodiscard]] GroupId tabBarAt(QPoint global) const;
    [[nodiscard]] int tabInsertionIndex(GroupId group, QPoint global) const;
    [[nodiscard]] int dragLabelExtent(TabId tab, bool edge) const;
    [[nodiscard]] QPixmap dragLabelPixmap(TabId tab) const;
    void setDraggedTab(TabId tab);
    [[nodiscard]] QHash<Edge, QRect> edgeBarRects() const;
    [[nodiscard]] int edgeTabInsertionIndex(Edge edge, QPoint global) const;
    [[nodiscard]] QRect edgeTabInsertionRect(Edge edge, int index) const;
    void setDragHover(QPoint global);
    void setInsertionPreview(GroupId group, int index, int extent);
    void setEdgeInsertionPreview(Edge edge, int index, int extent);
    void clearInsertionPreviews();
    [[nodiscard]] QRect insertionPreviewRect(GroupId group) const;
    [[nodiscard]] QRect edgeInsertionPreviewRect(Edge edge) const;
    void setDragPresentation(bool compact, bool edge);
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
    void applyChangedRatios(LayoutTree node);
    [[nodiscard]] bool holdsPages(const GroupTabs& widget, const GroupRecord& group,
                                  const QHash<TabId, QPointer<TabContent>>& contents) const;
    QWidget* buildTree(LayoutTree node, const QHash<TabId, QPointer<TabContent>>& contents);
    GroupTabs* buildGroup(GroupId group, const QHash<TabId, QPointer<TabContent>>& contents);
    QWidget* buildEdge(Edge edge, const QHash<TabId, QPointer<TabContent>>& contents);
    QWidget* buildPinned(Edge edge, const QHash<TabId, QPointer<TabContent>>& contents);
    void updateRailLayout();
    void placePeek();
    void updateEdgeTabSelection(TabId tab);
    [[nodiscard]] bool centralTabBarVisible(bool requested) const;
    void savePinnedExtents();
    void restorePinnedExtents();
    void restoreFocusScope();
    void applyFocusScope();
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
    QHash<GroupId, GroupSignature> builtGroups_;
    StructureSignature builtStructure_;
    quint64 builtThemeRevision_ = 0;
    QHash<QUuid, double> builtRatios_;
    // Unchanged groups of the previous build, consumed while rebuilding.
    QHash<GroupId, QPointer<GroupTabs>> reusableGroups_;
    QHash<Edge, QPointer<QWidget>> edgeBars_;
    QHash<GroupId, TabViewportState> groupViewportStates_;
    QHash<Edge, int> edgeScrollOffsets_;
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
