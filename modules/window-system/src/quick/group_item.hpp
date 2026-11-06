// SPDX-License-Identifier: MIT
#pragma once

#include "docking/tab_drag_visual.hpp"
#include "docking/tab_group_controller.hpp"
#include <window_system/layout_model.hpp>

#include <QElapsedTimer>
#include <QHash>
#include <QPointer>
#include <QQuickItem>
#include <functional>
#include <memory>

namespace window_system::quick {

class ButtonItem;
class GroupSurfaceItem;
class PathItem;
class PictureItem;
class TabBarItem;
class TabLabelTextItem;

// A tab group in Quick: its strip beside its pages, on a card. The rules for
// placing the strip, the card, the column and the viewport belong to the
// shared TabGroupController; this item applies them and owns the controls.
// Pages are items owned by their host; the group only parents the current one.
class GroupItem final : public QQuickItem {
    Q_OBJECT
public:
    explicit GroupItem(const Theme& theme, QQuickItem* parent = nullptr);
    ~GroupItem() override;

    [[nodiscard]] TabBarItem* tabBar() const { return bar_; }
    [[nodiscard]] TabGroupController& group() { return *group_; }
    // The window-level layer that draws dragged labels above other groups.
    void setForegroundLayer(QQuickItem* layer);

    // A null page is created through pageRequested when its tab is shown.
    void addTab(QQuickItem* page, const QIcon& icon, const QString& title, TabId id);
    // Removes the tab; returns its page, reparented to nothing, or null.
    QQuickItem* takePage(TabId id);
    void setCurrentIndex(int index, bool reveal = true);
    [[nodiscard]] int currentIndex() const;
    [[nodiscard]] int count() const;
    [[nodiscard]] QQuickItem* page(TabId id) const { return pages_.value(id); }

    void setTheme(const Theme& theme);
    void setAppearance(const TabAppearance& appearance, bool pinned = false);
    void setStickyCount(int count);
    void setTabMetadata(int index, const TabRecord& tab);
    void setGroupName(const QString& name);
    void setVerticalState(int width, bool iconMode);
    void setTabBarVisible(bool visible);
    void setContentEdgeToEdge(bool enabled);
    void setGroupFocused(bool focused);
    void setPinnedActions(std::function<void()> current, std::function<void()> all);
    void setTabListOpen(bool open);
    void setNewTabMenuOpen(bool open);
    // The largest minimum any page declares; the column never takes it.
    void setContentMinimum(QSize minimum);
    [[nodiscard]] QSize minimumSize() const;
    [[nodiscard]] QSize preferredSize() const;

    [[nodiscard]] TabViewportState captureViewportState() const;
    void restoreViewportState(TabViewportState state);
    void restoreScrollOffset(int offset);
    // In this item's coordinates.
    [[nodiscard]] QRect contentRect() const;
    [[nodiscard]] QRect stripRect() const;

    // Dragging, in global coordinates, as the Widgets GroupTabs.
    [[nodiscard]] QRect contentRectGlobal() const;
    [[nodiscard]] QRect stripRectGlobal() const;
    // A column's panel body; the strip otherwise.
    [[nodiscard]] QRect stripPanelRectGlobal() const;
    [[nodiscard]] Edge stripEdge() const;
    [[nodiscard]] DragAnchor tabGrabAnchor(TabId tab, QPoint global) const;
    [[nodiscard]] DragAnchor folderGrabAnchor(FolderId folder, QPoint global);
    [[nodiscard]] int insertionIndex(QPoint global, TabLayoutInput::Carried carried);
    void setDraggedTab(TabId tab);
    void setDraggedFolder(FolderId folder);
    void setLiftedTab(QPoint global, const DragAnchor& grab);
    void clearLiftedTab();
    void setInsertionPreview(int index, const QVector<CarriedLabel>& carried, bool carriedSticky, TabLayoutInput::Carried kind);
    [[nodiscard]] bool insertionSticky() const;
    [[nodiscard]] FolderId insertionFolder() const;
    [[nodiscard]] QRect insertionPreviewRectGlobal() const;
    // A carried window's strip shows only its tabs and takes the target
    // strip's look (Widgets GroupTabs); the target then paints its label.
    // Leaving the compact presentation restores the strip's own look.
    void setDragPresentation(bool compact);
    bool setDragTabStyle(const GroupItem& target, bool sticky);
    // The strip's tabs as a carried label, in this item's coordinates.
    [[nodiscard]] QRect carrierBounds() const;
    [[nodiscard]] QPoint carrierAnchor() const;
    [[nodiscard]] TabCarrierPiece carrierPiece(QPoint offset);
    [[nodiscard]] TabId currentTab() const;
    [[nodiscard]] QRect currentTabRect() const;
    // An empty visual clears it.
    void setIncomingVisual(TabDragVisual visual, QPoint global);
    [[nodiscard]] QRect incomingRectGlobal() const;
    // The shown page as a picture, global device pixels at the window's
    // ratio; null without a page.
    [[nodiscard]] QImage pagePicture() const;
    // A picture over the pages while a carried tab is over the strip; a
    // null one shows the pages again.
    void setContentPreview(QImage picture);
    // A wheel step over the strip during a drag, as the strip takes one.
    bool scrollStripAt(QPoint global, QPoint delta, bool smooth);
    // A carrier's tab rectangles in this item's coordinates.
    [[nodiscard]] LabelPositions carrierTabRects() const;
    // Where the dropped tabs start, global: the lifted tab, or the incoming
    // label laid out as the carrier showed it. settleDroppedTabs() slides
    // them from there once the model has moved them.
    [[nodiscard]] LabelPositions captureDropPositions(std::optional<LabelPositions> carrier) const;
    void settleDroppedTabs(const LabelPositions& global);

    std::function<QQuickItem*(TabId)> pageRequested;
    std::function<void(int)> currentChanged;
    // Close, unstick, drag and folder actions decided by the strip.
    std::function<void(const TabPointerDecision&)> stripAction;
    // A tab's or folder's context menu; both -1 over empty strip.
    std::function<void(int, int, QPoint)> stripMenuRequested;
    std::function<void()> newTabRequested;
    // Global rectangle of the button that asked, for anchoring a popup.
    std::function<void(QRect)> newTabMenuRequested;
    std::function<void(QRect)> tabListRequested;
    std::function<void(QPoint)> groupMenuRequested;
    std::function<void(int, bool)> verticalStateChanged;
    std::function<bool()> isActiveGroup;
    std::function<void()> minimumSizeChanged;

protected:
    void geometryChange(const QRectF& now, const QRectF& previous) override;
    void itemChange(ItemChange change, const ItemChangeData& data) override;
    void hoverEnterEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;

private:
    template <class Apply> void edit(Apply&& apply);
    void prepareGroup();
    void applyGroupChanges();
    void layoutChildren();
    void placeControls(const TabGroupPlacement& placement);
    void applyStripControls();
    void syncSurface();
    void syncCard(const TabGroupGeometry& geometry);
    void scheduleMotion();
    void advanceMotion();
    void resolvePage(TabId tab);
    void showCurrentPage();
    void attachWindow(QQuickWindow* window);
    [[nodiscard]] bool vertical() const;
    [[nodiscard]] QRect globalRect(const QQuickItem* item) const;

    Theme theme_;
    TabBarItem* bar_;
    std::unique_ptr<TabGroupController> group_;
    GroupSurfaceItem* surface_;
    // The card under the pages, and its rounded corners over them.
    PathItem* card_;
    PathItem* corners_;
    QQuickItem* content_;
    PictureItem* preview_ = nullptr;
    ButtonItem* create_;
    ButtonItem* types_;
    ButtonItem* list_;
    ButtonItem* count_;
    ButtonItem* iconMode_;
    QVector<ButtonItem*> sidebar_;
    TabLabelTextItem* name_;
    TabLabelTextItem* title_;
    QQuickItem* resizeHandle_;
    QHash<TabId, QPointer<QQuickItem>> pages_;
    QPointer<QQuickItem> shownPage_;
    QPointer<QQuickWindow> window_;
    QMetaObject::Connection frameConnection_;
    QVector<QMetaObject::Connection> windowConnections_;
    QElapsedTimer clock_;
    QSize contentMinimum_;
    QSize countSize_;
    bool layingOut_ = false;
    bool reflowQueued_ = false;
    bool createsHovered_ = false;
    bool newTabMenuOpen_ = false;
    QObject* accessibility_;
};

} // namespace window_system::quick
