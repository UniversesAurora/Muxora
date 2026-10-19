#pragma once

#include <window_system/layout_model.hpp>
#include <window_system/theme.hpp>
#include <window_system/tab_appearance.hpp>
#include "tab_viewport_state.hpp"
#include <QFrame>
#include <QPointer>
#include <QSet>
#include <QTabBar>
#include <optional>

class QScrollArea;
class QMenu;
class QScrollBar;
class QStackedWidget;
class QToolButton;
class QTimer;

namespace window_system {

class DragTabBar final : public QTabBar {
public:
    explicit DragTabBar(QWidget* parent);
    std::function<void(int, QPoint)> drag;
    std::function<void(int, QPoint)> menu;
    std::function<void(int)> close;
    void setCloseIcon(QIcon icon);
    void setInsertionPreview(int index, int extent, QColor color);
    void setDraggedTab(TabId tab);
    [[nodiscard]] int draggedExtent() const;
    [[nodiscard]] QRect insertionPreviewRect() const;
    [[nodiscard]] QRect displayTabRect(int index) const;
    [[nodiscard]] int insertionIndexAt(QPoint local) const;
    [[nodiscard]] int insertionExtent() const { return insertionExtent_; }
    void setAppearance(const TabAppearance& appearance);
    void setTabMetadata(int index, const TabRecord& tab);
    [[nodiscard]] const TabColorPreset* tabPreset(int index) const;
    void layoutTabs(int availableWidth, bool expanded, int trailingActionsWidth = 0);
    [[nodiscard]] QSize displayedSize() const;
    [[nodiscard]] QPoint creationPosition() const;
    [[nodiscard]] int rowCount() const;
protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    QSize tabSizeHint(int index) const override;
    void tabRemoved(int index) override;
private:
    QRect closeRect(int index) const;
    int displayedTabAt(QPoint local) const;
    bool isDragged(int index) const;
    QPoint start_;
    int pressed_ = -1;
    int closePressed_ = -1;
    int hovered_ = -1;
    QIcon closeIcon_;
    int insertionIndex_ = -1;
    int insertionExtent_ = 0;
    QColor insertionColor_;
    int draggedIndex_ = -1;
    TabAppearance appearance_;
    struct Decoration { QString colorId; };
    QHash<int, Decoration> decorations_;
    QHash<int, QRect> displayedRects_;
    QRect previewRect_;
    QSize displayedSize_;
    QPoint creationPosition_;
    int rowCount_ = 1;
    int activeLogicalRow_ = 0;
    bool customLayout_ = false;
};

// The bar keeps natural tab widths inside a scroll viewport. Native QTabBar
// remains the input and stable-ID surface; scrolling never selects a new tab.
class GroupTabs final : public QFrame {
public:
    explicit GroupTabs(const Theme& theme, QWidget* parent);
    ~GroupTabs() override;
    std::function<void(int)> currentChanged;
    std::function<void(int)> closeRequested;
    std::function<void()> hoverGeometryChanged;
    std::function<void()> newTabRequested;
    std::function<QMenu*(QWidget*)> newTabMenuRequested;
    std::function<void(QPoint)> groupMenuRequested;
    DragTabBar* tabBar() const;
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
    int currentIndex() const;
    int count() const;
    // The page at a strip index, or null.
    [[nodiscard]] QWidget* page(int index) const;
    QRect viewportRectGlobal() const;
    int insertionIndex(QPoint global) const;
    void setDragHover(QPoint global);
    void setInsertionPreview(int index, int extent);
    void setDraggedTab(TabId tab);
    [[nodiscard]] QRect insertionPreviewRectGlobal() const;
    void setDragPresentation(bool compact, bool edge);
    void setPinnedActions(std::function<void()> current, std::function<void()> all);
    void setTabBarVisible(bool visible);
    void setAppearance(const TabAppearance& appearance, bool pinned = false);
    void setTabMetadata(int index, const TabRecord& tab);
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
private:
    void updateBarGeometry();
    void showTabList();
    void showNewTabMenu();
    void updateScrollBarVisibility();
    void updateScrollBarGeometry();
    void applyPendingScrollOffset();
    void queueViewportReflow();
    bool applyPendingViewportState();
    void cancelViewportReflow();
    [[nodiscard]] TabViewportState currentViewportState() const;
    void updateCanvasGeometry();
    QWidget* header_ = nullptr;
    QWidget* headerActions_ = nullptr;
    DragTabBar* bar_ = nullptr;
    QWidget* canvas_ = nullptr;
    QWidget* createButtons_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    QScrollBar* scrollbar_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QHash<TabId, QPointer<QWidget>> pages_;
    QToolButton* list_ = nullptr;
    Theme theme_;
    QPointer<QTimer> autoScroll_;
    QPointer<QMenu> tabListMenu_;
    QPointer<QMenu> newTabMenu_;
    QPointer<QWidget> viewportWindow_;
    int scrollDirection_ = 0;
    QSet<QObject*> hoveredSurfaces_;
    bool dragHovered_ = false;
    std::optional<int> pendingScrollOffset_;
    std::optional<TabViewportState> settledViewport_;
    std::optional<TabViewportState> pendingViewportState_;
    bool viewportReflowQueued_ = false;
    bool applyingViewportState_ = false;
    bool updatingBarGeometry_ = false;
    TabAppearance appearance_;
    bool pinned_ = false;
    bool rowsExpanded_ = true;
};

} // namespace window_system
