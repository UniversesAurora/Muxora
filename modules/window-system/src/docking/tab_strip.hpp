#pragma once

#include <window_system/layout_model.hpp>
#include <window_system/theme.hpp>
#include <window_system/tab_appearance.hpp>
#include "tab_viewport_state.hpp"
#include "style/tab_label_geometry.hpp"
#include "style/tab_title_layout.hpp"
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
    // Sticky tabs show a pin in the close slot; pressing it unsticks the tab.
    std::function<void(int)> unstick;
    void setCloseIcon(QIcon icon);
    void setStickyIcon(QIcon icon);
    // Separates sticky tabs kept in view from the tabs scrolling under them.
    void setStickyBorderColor(QColor color) { stickyBorder_ = color; update(); }
    void setStickyCount(int count);
    [[nodiscard]] int stickyCount() const { return stickyCount_; }
    // The strip's scroll offset. Compact and shrunk sticky tabs of a single
    // row are drawn this far along, so they stay at the viewport's start.
    void setStickyOffset(int offset);
    // The width those sticky tabs cover at the viewport's start; 0 when the
    // sticky tabs scroll with the others.
    [[nodiscard]] int stickyRunExtent() const;
    // `carriedSticky` says whether the dropped tab was sticky; dropping it
    // right after the sticky tabs then keeps it sticky. With sticky tabs on
    // their own row, `stickyRow` is the row the pointer is over and decides.
    void setInsertionPreview(int index, int extent, QColor color, bool carriedSticky = false,
                             std::optional<bool> stickyRow = {});
    // Whether a point is over the sticky tabs' own row; empty without one.
    [[nodiscard]] std::optional<bool> insertionRowAt(QPoint local) const;
    [[nodiscard]] int lineHeight() const { return lineHeight_; }
    // Whether a drop at the insertion preview makes the tab sticky.
    [[nodiscard]] bool insertionSticky() const { return insertionSticky_; }
    [[nodiscard]] bool insertionCarriesSticky() const { return carriedSticky_; }
    // The width of a tab with its sticky sizing ignored.
    [[nodiscard]] int naturalTabWidth(int index) const;
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
    bool event(QEvent* event) override;
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
    TabLabelParts labelParts(int index) const;
    int displayedTabAt(QPoint local) const;
    bool isDragged(int index) const;
    // A compact sticky tab shows only its icon or initial and has no pin.
    bool isCompactSticky(int index) const;
    // The fixed width of a compact or shrunk sticky tab.
    int sizedStickyWidth() const;
    // Sticky tabs, and a sticky insertion preview, stay in view.
    bool stickyFloats() const;
    // Sticky tabs take rows of their own above the others.
    bool separateRow() const;
    int stickyShift() const;
    void updateInsertionSticky();
    void refreshSizeHints();
    QPoint start_;
    int pressed_ = -1;
    int closePressed_ = -1;
    int middlePressed_ = -1;
    int hovered_ = -1;
    bool closeHovered_ = false;
    QIcon closeIcon_;
    QIcon stickyIcon_;
    QColor stickyBorder_;
    int stickyCount_ = 0;
    int stickyOffset_ = 0;
    int stickyRunEnd_ = 0;
    int stickyRowsHeight_ = 0;
    int lineHeight_ = 28;
    std::optional<bool> stickyRow_;
    bool carriedSticky_ = false;
    bool insertionSticky_ = false;
    int insertionIndex_ = -1;
    int insertionExtent_ = 0;
    QColor insertionColor_;
    int draggedIndex_ = -1;
    TabAppearance appearance_;
    struct Decoration { QString colorId; };
    QHash<int, Decoration> decorations_;
    mutable QHash<int, TabTitleLayout> titleLayouts_;
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
    std::function<void(int)> unstickRequested;
    void setStickyCount(int count);
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
    void setInsertionPreview(int index, int extent, bool carriedSticky = false, std::optional<bool> stickyRow = {});
    [[nodiscard]] bool insertionSticky() const;
    [[nodiscard]] std::optional<bool> insertionRow(QPoint global) const;
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
    // The part of the viewport's start that sticky tabs kept in view cover
    // for the tab at `index`; 0 for those sticky tabs themselves.
    [[nodiscard]] int stickyInset(int index) const;
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
    std::optional<bool> stickyRow_;
};

} // namespace window_system
