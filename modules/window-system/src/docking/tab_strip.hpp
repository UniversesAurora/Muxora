#pragma once

#include <window_system/layout_model.hpp>
#include <window_system/theme.hpp>
#include <QFrame>
#include <QPointer>
#include <QSet>
#include <QTabBar>

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
    [[nodiscard]] QRect insertionPreviewRect() const;
    [[nodiscard]] QRect displayTabRect(int index) const;
    [[nodiscard]] int insertionIndexAt(QPoint local) const;
    [[nodiscard]] int insertionExtent() const { return insertionExtent_; }
protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    QSize tabSizeHint(int index) const override;
private:
    QRect closeRect(int index) const;
    QPoint start_;
    int pressed_ = -1;
    int closePressed_ = -1;
    int hovered_ = -1;
    QIcon closeIcon_;
    int insertionIndex_ = -1;
    int insertionExtent_ = 0;
    QColor insertionColor_;
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
    DragTabBar* tabBar() const;
    void addTab(QWidget* widget, QIcon icon, QString title, TabId id);
    void setCurrentIndex(int index);
    int currentIndex() const;
    int count() const;
    QRect viewportRectGlobal() const;
    int insertionIndex(QPoint global) const;
    void setDragHover(QPoint global);
    void setInsertionPreview(int index, int extent);
    [[nodiscard]] QRect insertionPreviewRectGlobal() const;
    void setDragPresentation(bool compact, bool edge);
    void setPinnedActions(std::function<void()> current, std::function<void()> all);
    void setTabBarVisible(bool visible);
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
private:
    void updateBarGeometry();
    void showTabList();
    void updateScrollBarVisibility();
    void updateScrollBarGeometry();
    QWidget* header_ = nullptr;
    DragTabBar* bar_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    QScrollBar* scrollbar_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QToolButton* list_ = nullptr;
    Theme theme_;
    QPointer<QTimer> autoScroll_;
    QPointer<QMenu> tabListMenu_;
    int scrollDirection_ = 0;
    QSet<QObject*> hoveredSurfaces_;
    bool dragHovered_ = false;
};

} // namespace window_system
