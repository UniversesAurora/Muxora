// SPDX-License-Identifier: MIT
#pragma once

#include "docking/tab_strip_controller.hpp"
#include <QElapsedTimer>
#include <QPointer>
#include <QQuickItem>
#include <QTimer>

namespace window_system::quick {

class TabForegroundItem;
class TabStripItem;

// A real Quick input/clock adapter for the shared controller. Hosts batch
// model changes through controller(), then call synchronize() once.
class TabBarItem final : public QQuickItem {
    Q_OBJECT
public:
    explicit TabBarItem(const Theme& theme, QQuickItem* parent = nullptr);
    ~TabBarItem() override;
    [[nodiscard]] TabStripController& controller() { return controller_; }
    [[nodiscard]] const TabStripController& controller() const { return controller_; }
    void synchronize();
    void setForegroundLayer(QQuickItem* layer);
    void setControls(TabStripControls controls);
    void setPopupOpen(bool open);
    void setAnimationPolicy(bool enabled);
    void setSmoothScrolling(bool enabled);
    [[nodiscard]] bool renderingAccepted() const { return renderingAccepted_; }
    [[nodiscard]] bool contains(const QPointF& point) const override;

signals:
    void currentChanged(int index);
    void viewChanged();
    void surfaceChanged();
    void reservedSizeChanged();
    void controlsChanged();
    void scrollInputStarted();
    void actionRequested(window_system::TabPointerDecision decision);
    void contextMenuRequested(int tab, int folder, QPoint global);
    void renderingRejected();

protected:
    void itemChange(ItemChange change, const ItemChangeData& data) override;
    void geometryChange(const QRectF& now, const QRectF& previous) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void mouseUngrabEvent() override;
    void hoverEnterEvent(QHoverEvent* event) override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool event(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void prepare();
    void attachWindow(QQuickWindow* window);
    void advance();
    void schedule();
    void dispatch(TabPointerDecision decision);
    bool handleContextMenu(QContextMenuEvent* event);
    TabStripController controller_;
    TabStripItem* base_;
    TabForegroundItem* foreground_;
    QPointer<QQuickItem> foregroundLayer_;
    QPointer<QQuickWindow> targetWindow_;
    QElapsedTimer clock_;
    QTimer wakeTimer_;
    QFont font_;
    TabStripControls controls_;
    QVector<QMetaObject::Connection> windowConnections_;
    QMetaObject::Connection advanceConnection_;
    QMetaObject::Connection frameConnection_;
    bool clockConnected_ = false;
    bool synchronizing_ = false;
    bool animations_ = true;
    bool smoothScrolling_ = true;
    bool renderingAccepted_ = true;
};

} // namespace window_system::quick
