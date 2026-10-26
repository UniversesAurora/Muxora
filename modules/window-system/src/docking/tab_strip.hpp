#pragma once

#include "tab_bar.hpp"
#include "tab_viewport_state.hpp"
#include "style/elevation.hpp"
#include <window_system/layout_model.hpp>
#include <window_system/theme.hpp>
#include <window_system/tab_appearance.hpp>
#include <QFrame>
#include <QPointer>
#include <QSet>
#include <optional>

class QLabel;
class QMenu;
class QStackedWidget;
class QToolButton;
class QTimer;

namespace window_system {

// A tab group: its strip (TabBar) beside its pages. The strip goes above,
// below, left or right of the pages; a vertical strip has a footer with the
// new-tab and sidebar actions and an edge that resizes it.
class GroupTabs final : public QFrame {
public:
    explicit GroupTabs(const Theme& theme, QWidget* parent);
    ~GroupTabs() override;
    std::function<void(int)> currentChanged;
    std::function<void(int)> closeRequested;
    std::function<void(int)> unstickRequested;
    // Returns the real page for a tab whose strip currently holds a
    // lightweight placeholder. The callback is invoked only when needed.
    std::function<QWidget*(TabId)> pageRequested;
    void setStickyCount(int count);
    std::function<void()> hoverGeometryChanged;
    std::function<void()> newTabRequested;
    std::function<QMenu*(QWidget*)> newTabMenuRequested;
    std::function<void(QPoint)> groupMenuRequested;
    // A vertical strip's width or icon mode changed by the user; 0 resets
    // the width to the default.
    std::function<void(int, bool)> verticalStateChanged;
    // Whether this is its window's active group; the strip shows its accent
    // only while it is and the window is active.
    std::function<bool()> isActiveGroup;
    void refreshFocus();
    // Painting is delegated to the owning DockSurface, so shadows can cross
    // splitter gaps without snapshots of the group's contents.
    std::function<void()> surfaceInvalidated;
    void paintSurface(QPainter& painter, ElevationRenderer& renderer, QPoint offset, const QRect& exposed) const;
    [[nodiscard]] QRect surfaceBounds() const;
    [[nodiscard]] QRegion surfacePaintRegion() const;
    void invalidateSurfaceCorners(const QRect& area);
    TabBar* tabBar() const;
    void addTab(QWidget* widget, QIcon icon, QString title, TabId id);
    // Removes the tab and its page without deleting the page, which keeps
    // this group as its parent until it is added elsewhere. Returns null if
    // the page already left the stack. Does not call currentChanged.
    QWidget* takePage(TabId id);
    void setCurrentIndex(int index, bool reveal = true);
    [[nodiscard]] int scrollOffset() const;
    [[nodiscard]] TabViewportState captureViewportState() const;
    void restoreViewportState(TabViewportState state);
    void restoreScrollOffset(int offset);
    void scrollByWheel(QPoint delta);
    // Scrolls the strip under a global point; returns whether it moved.
    bool scrollStripAt(QPoint global, QPoint delta, bool smooth = true);
    int currentIndex() const;
    int count() const;
    // The page at a strip index, or null.
    [[nodiscard]] QWidget* page(int index) const;
    [[nodiscard]] bool pageIsPlaceholder(TabId tab) const;
    QRect viewportRectGlobal() const;
    [[nodiscard]] QRect contentRectGlobal() const;
    [[nodiscard]] QRect stripPanelRectGlobal() const;
    void restoreColumnPeek();
    int insertionIndex(QPoint global, TabLayoutInput::Carried carried = TabLayoutInput::Carried::Tab) const;
    void setDragHover(QPoint global);
    void clearDragHover(QPoint global);
    // The carried tabs open a gap at `index`, each as wide as it will be in
    // the section the drop position decides.
    void setInsertionPreview(int index, const QVector<CarriedLabel>& carried, bool carriedSticky = false,
                             TabLayoutInput::Carried kind = TabLayoutInput::Carried::Tab);
    [[nodiscard]] bool insertionSticky() const;
    // The tab folder the preview's block joins; null for none.
    [[nodiscard]] QUuid insertionFolder() const;
    void setDraggedFolder(QUuid folder);
    // Where in a folder's header a drag took hold of it.
    [[nodiscard]] QPoint folderGrabOffset(QUuid folder, QPoint global) const;
    [[nodiscard]] std::optional<bool> insertionRow(QPoint global) const;
    void setDraggedTab(TabId tab);
    // The dragged tab follows the pointer inside this strip; `grab` is where
    // in the tab it was taken. Its center, not the pointer, picks the slot.
    void setLiftedTab(QPoint global, QPoint grab);
    void clearLiftedTab();
    // A picture of dragged content shown over this group's pages, as if its
    // tab were already here (tab strip design 6.4); a null pixmap removes it.
    void setContentPreview(const QPixmap& picture);
    [[nodiscard]] QRect insertionPreviewRectGlobal() const;
    void setDragPresentation(bool compact, bool edge);
    bool setDragTabStyle(const TabBar& target, bool sticky);
    void setIncomingVisual(TabDragVisual visual, QPoint global = {});
    void setPinnedActions(std::function<void()> current, std::function<void()> all);
    void setTabBarVisible(bool visible);
    // Repaints with a new theme without rebuilding the strip.
    void setTheme(const Theme& theme);
    void setAppearance(const TabAppearance& appearance, bool pinned = false);
    void setTabMetadata(int index, const TabRecord& tab);
    void setGroupFocused(bool focused);
    struct SurfaceState { qreal focus; bool focused; TabBar::SurfaceState tabs; };
    [[nodiscard]] SurfaceState captureSurfaceState() const;
    void inheritSurface(const SurfaceState& previous, bool animate);
    // A vertical strip's own width (0: the appearance default) and icon mode.
    void setVerticalState(int width, bool iconMode);
    // The group's name, shown at the end of its strip when not empty.
    void setGroupName(const QString& name);
    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;
protected:
    bool event(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
private:
    void updateDragProbe(QPoint global) const;
    void layoutChildren();
    void updateCardJoin();
    void placeControls();
    void showTabList();
    void showNewTabMenu();
    void applyPendingScrollOffset();
    void queueViewportReflow();
    bool applyPendingViewportState();
    void cancelViewportReflow();
    [[nodiscard]] TabViewportState currentViewportState() const;
    void resolvePage(TabId tab);
    [[nodiscard]] int stickyInset(int index) const;
    [[nodiscard]] bool vertical() const;
    [[nodiscard]] int columnWidth() const;
    [[nodiscard]] int maximumColumnWidth() const;
    TabBar* bar_ = nullptr;
    QWidget* trailing_ = nullptr;
    QWidget* end_ = nullptr;
    QWidget* footer_ = nullptr;
    QWidget* columnPanel_ = nullptr;
    QVector<QToolButton*> sidebarButtons_;
    ProgressAnimation columnExpansion_{1};
    ProgressAnimation peekExpansion_{0};
    ProgressAnimation surfaceFocus_{0};
    bool surfaceFocused_ = false;
    QWidget* resizeHandle_ = nullptr;
    QWidget* contentPreview_ = nullptr;
    QWidget* card_ = nullptr;
    QLabel* name_ = nullptr;
    // A vertical strip's title: the group's name above its column.
    QLabel* title_ = nullptr;
    QString groupName_;
    QWidget* createButtons_ = nullptr;
    QToolButton* list_ = nullptr;
    QToolButton* iconModeButton_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QHash<TabId, QPointer<QWidget>> pages_;
    QSet<TabId> placeholders_;
    Theme theme_;
    QPointer<QTimer> autoScroll_;
    QPointer<QMenu> tabListMenu_;
    QPointer<QMenu> newTabMenu_;
    QPointer<QWidget> viewportWindow_;
    QPoint scrollDirection_;
    mutable QPoint dragProbe_;
    bool lifting_ = false;
    QPoint liftedGlobal_;
    QPoint liftedGrab_;
    bool dragHovered_ = false;
    std::optional<int> pendingScrollOffset_;
    std::optional<TabViewportState> settledViewport_;
    std::optional<TabViewportState> pendingViewportState_;
    bool viewportReflowQueued_ = false;
    bool applyingViewportState_ = false;
    bool layingOut_ = false;
    TabAppearance appearance_;
    bool pinned_ = false;
    bool compactPresentation_ = false;
    struct DragStyle {
        TabAppearance appearance;
        TabBarPlacement placement;
        int width;
        bool iconMode;
        int stickyCount;
    };
    std::optional<DragStyle> normalDragStyle_;
    QSize dragTabArea_;
    bool barVisible_ = true;
    int verticalWidth_ = 0;
    bool iconMode_ = false;
    // Dragging the vertical strip's edge.
    std::optional<int> resizeOrigin_;
    int resizeStartWidth_ = 0;
};

} // namespace window_system
