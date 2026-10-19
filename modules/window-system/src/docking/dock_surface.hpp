#pragma once
#include <window_system/window_manager.hpp>
#include <window_system/view_state.hpp>
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
