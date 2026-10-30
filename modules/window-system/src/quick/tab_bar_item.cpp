// SPDX-License-Identifier: MIT
#include "tab_bar_item.hpp"
#include "tab_foreground_item.hpp"
#include "tab_strip_item.hpp"
#include <QContextMenuEvent>
#include <QGuiApplication>
#include <QHoverEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QScreen>
#include <QStyleHints>
#include <QWheelEvent>
#include <algorithm>
#include <chrono>
#include <utility>

namespace window_system::quick {

TabBarItem::TabBarItem(const Theme& theme, QQuickItem* parent)
    : QQuickItem(parent), controller_(theme), base_(new TabStripItem(this)),
      foreground_(new TabForegroundItem(this)), font_(QGuiApplication::font())
{
    setAcceptedMouseButtons(Qt::LeftButton | Qt::MiddleButton | Qt::RightButton);
    setAcceptHoverEvents(true);
    setFocusPolicy(Qt::TabFocus);
    foreground_->setZ(1);
    clock_.start();
    wakeTimer_.setSingleShot(true);
    wakeTimer_.setTimerType(Qt::PreciseTimer);
    connect(&wakeTimer_, &QTimer::timeout, this, &TabBarItem::advance);
    qGuiApp->installEventFilter(this);
    attachWindow(window());
}

TabBarItem::~TabBarItem() { qGuiApp->removeEventFilter(this); }

void TabBarItem::setForegroundLayer(QQuickItem* layer)
{
    const QPointer<TabBarItem> alive(this);
    foregroundLayer_ = layer;
    foreground_->setParentItem(layer ? layer : this);
    if (alive) synchronize();
}

void TabBarItem::setControls(TabStripControls controls) { controls_ = controls; synchronize(); }
void TabBarItem::setPopupOpen(bool open) { prepare(); controller_.setPopupOpen(open); synchronize(); }
void TabBarItem::setAnimationPolicy(bool enabled) { animations_ = enabled; synchronize(); }
void TabBarItem::setSmoothScrolling(bool enabled) { smoothScrolling_ = enabled; synchronize(); }

void TabBarItem::prepare()
{
    controller_.setTimestampNs(clock_.nsecsElapsed());
    auto environment = controller_.environment();
    environment.size = size().toSize();
    environment.font = font_;
    environment.direction = QGuiApplication::layoutDirection();
    environment.controls = controls_;
    environment.controls.ownerVisible = isVisible();
    environment.globalOrigin = mapToGlobal(QPointF()).toPoint();
    environment.visible = isVisible() && targetWindow_ && targetWindow_->isVisible()
        && !(targetWindow_->windowStates() & Qt::WindowMinimized);
    environment.windowActive = targetWindow_ && targetWindow_->isActive();
    environment.animations = animations_ && motionAnimationsEnabled();
    environment.smoothScrolling = smoothScrolling_;
    if (targetWindow_) {
        environment.windowArea = QRect(mapFromItem(targetWindow_->contentItem(), QPointF()).toPoint(), targetWindow_->size());
        if (const auto* screen = targetWindow_->screen()) {
            environment.logicalDpiX = qRound(screen->logicalDotsPerInchX());
            environment.logicalDpiY = qRound(screen->logicalDotsPerInchY());
        }
    }
    controller_.setEnvironment(std::move(environment));
}

void TabBarItem::attachWindow(QQuickWindow* window)
{
    if (targetWindow_ == window) return;
    for (const auto& connection : std::as_const(windowConnections_)) disconnect(connection);
    windowConnections_.clear();
    disconnect(advanceConnection_); disconnect(frameConnection_); clockConnected_ = false;
    targetWindow_ = window;
    if (window) {
        windowConnections_.append(connect(window, &QWindow::visibleChanged, this, [this] { advance(); }));
        windowConnections_.append(connect(window, &QWindow::windowStateChanged, this, [this] { advance(); }));
        windowConnections_.append(connect(window, &QWindow::activeChanged, this, [this] { synchronize(); }));
        windowConnections_.append(connect(window, &QWindow::screenChanged, this, [this] { synchronize(); }));
    }
    synchronize();
}

void TabBarItem::advance()
{
    prepare();
    controller_.advance(clock_.nsecsElapsed());
    synchronize();
}

void TabBarItem::schedule()
{
    const auto& environment = controller_.environment();
    const bool animate = targetWindow_ && environment.visible && controller_.needsFrame();
    if (animate && !clockConnected_) {
        advanceConnection_ = connect(targetWindow_, &QQuickWindow::afterAnimating, this, &TabBarItem::advance);
        frameConnection_ = connect(targetWindow_, &QQuickWindow::frameSwapped, this, &TabBarItem::schedule, Qt::QueuedConnection);
        clockConnected_ = true;
    } else if (!animate && clockConnected_) {
        disconnect(advanceConnection_); disconnect(frameConnection_); clockConnected_ = false;
    }
    if (animate) targetWindow_->update();
    if (const auto deadline = controller_.nextWakeupNs(); deadline && environment.visible) {
        const auto remaining = std::max<qint64>(0, *deadline - clock_.nsecsElapsed());
        wakeTimer_.start(std::chrono::milliseconds((remaining + 999'999) / 1'000'000));
    } else wakeTimer_.stop();
}

void TabBarItem::synchronize()
{
    if (synchronizing_) return;
    synchronizing_ = true;
    prepare();
    const QPointer<TabBarItem> alive(this);
    for (;;) {
        const auto changes = controller_.takeChanges();
        if (changes.presentation || changes.repaint) {
            const auto frame = controller_.presentationFrame();
            base_->setSize(frame->size);
            const bool accepted = base_->setFrame(frame->base);
            renderingAccepted_ = foreground_->setFrame(frame->foreground) && accepted;
            if (!renderingAccepted_) {
                base_->clearFrame(); foreground_->clearFrame();
                emit renderingRejected();
                if (!alive) return;
            }
        }
        if (foregroundLayer_) foreground_->setPosition(mapToItem(foregroundLayer_, QPointF()));
        else foreground_->setPosition({});
        foreground_->setVisible(isVisible());
        schedule();
        if (changes.controls) emit controlsChanged();
        if (!alive) return;
        if (changes.surface) emit surfaceChanged();
        if (!alive) return;
        if (changes.view) emit viewChanged();
        if (!alive) return;
        if (changes.reservedSize) emit reservedSizeChanged();
        if (!alive) return;
        if (changes.current) emit currentChanged(*changes.current);
        if (!alive) return;
        if (!changes.presentation && !changes.repaint && !changes.controls && !changes.surface
            && !changes.view && !changes.reservedSize && !changes.current) break;
    }
    synchronizing_ = false;
}

bool TabBarItem::contains(const QPointF& point) const
{
    return QQuickItem::contains(point) || controller_.scrollbarHitRect().contains(point.toPoint());
}

void TabBarItem::itemChange(ItemChange change, const ItemChangeData& data)
{
    const QPointer<TabBarItem> alive(this);
    QQuickItem::itemChange(change, data);
    if (!alive) return;
    if (change == ItemSceneChange) attachWindow(data.window);
    else if (change == ItemVisibleHasChanged || change == ItemDevicePixelRatioHasChanged) synchronize();
}

void TabBarItem::geometryChange(const QRectF& now, const QRectF& previous)
{
    const QPointer<TabBarItem> alive(this);
    QQuickItem::geometryChange(now, previous);
    if (alive && now != previous) synchronize();
}

void TabBarItem::dispatch(TabPointerDecision decision)
{
    const QPointer<TabBarItem> alive(this);
    synchronize();
    if (alive && decision.action != TabPointerDecision::Action::None
        && decision.action != TabPointerDecision::Action::ActivateTab) emit actionRequested(decision);
}

void TabBarItem::mousePressEvent(QMouseEvent* event)
{
    prepare();
    event->accept();
    const auto decision = controller_.pointerPress(event->button(), event->position().toPoint(), event->globalPosition().toPoint());
    dispatch(decision);
}

void TabBarItem::mouseMoveEvent(QMouseEvent* event)
{
    prepare(); event->accept();
    dispatch(controller_.pointerMove(event->buttons(), event->position().toPoint(), event->globalPosition().toPoint(),
        QGuiApplication::styleHints()->startDragDistance()));
}

void TabBarItem::mouseReleaseEvent(QMouseEvent* event)
{
    prepare(); event->accept();
    dispatch(controller_.pointerRelease(event->button(), event->position().toPoint()));
}

void TabBarItem::mouseDoubleClickEvent(QMouseEvent* event) { mousePressEvent(event); }
void TabBarItem::mouseUngrabEvent()
{
    prepare();
    (void)controller_.pointerRelease(Qt::LeftButton, {-1, -1});
    (void)controller_.pointerRelease(Qt::MiddleButton, {-1, -1});
    synchronize();
}

void TabBarItem::hoverEnterEvent(QHoverEvent* event) { prepare(); controller_.enter(event->position().toPoint()); synchronize(); }
void TabBarItem::hoverMoveEvent(QHoverEvent* event)
{
    prepare();
    (void)controller_.pointerMove(Qt::NoButton, event->position().toPoint(), mapToGlobal(event->position()).toPoint(),
        QGuiApplication::styleHints()->startDragDistance());
    synchronize();
}
void TabBarItem::hoverLeaveEvent(QHoverEvent*) { prepare(); controller_.leave(); synchronize(); }

void TabBarItem::wheelEvent(QWheelEvent* event)
{
    prepare();
    const auto delta = event->pixelDelta().isNull() ? event->angleDelta() / 3 : event->pixelDelta();
    const auto decision = controller_.routeScroll(event->position().toPoint(), delta);
    if (decision.action == TabWheelDecision::Action::Ignore) { event->ignore(); return; }
    event->accept();
    const bool smooth = event->pixelDelta().isNull();
    const QPointer<TabBarItem> alive(this);
    emit scrollInputStarted();
    if (!alive) return;
    (void)controller_.applyScroll(decision, smooth);
    synchronize();
}

void TabBarItem::keyPressEvent(QKeyEvent* event)
{
    prepare();
    if (controller_.keyPress(event->key())) event->accept(); else event->ignore();
    synchronize();
}

bool TabBarItem::handleContextMenu(QContextMenuEvent* event)
{
    prepare();
    const int tab = controller_.tabAt(event->pos());
    const int folder = tab < 0 ? controller_.folderAt(event->pos()) : -1;
    const QPointer<TabBarItem> alive(this);
    setPopupOpen(true);
    if (alive) emit contextMenuRequested(tab, folder, event->globalPos());
    return true;
}

bool TabBarItem::event(QEvent* event)
{
    // The dedicated virtual hook is Qt 7-only. Qt 6 delivers this through
    // QObject::event(), before its attached-menu implementation consumes it.
    if (event->type() == QEvent::ContextMenu) return handleContextMenu(static_cast<QContextMenuEvent*>(event));
    return QQuickItem::event(event);
}

bool TabBarItem::eventFilter(QObject* watched, QEvent* event)
{
    const QPointer<QObject> receiver(watched);
    if (event->type() == QEvent::ApplicationFontChange || event->type() == QEvent::ApplicationLayoutDirectionChange) {
        font_ = QGuiApplication::font(); synchronize();
    } else if (controller_.watchesOutsideInput() && controller_.dismissesOutsideInput()
        && event->type() == QEvent::MouseButtonPress) {
        const auto global = static_cast<QMouseEvent*>(event)->globalPosition();
        if (!contains(mapFromGlobal(global))) { prepare(); controller_.setExpanded(false); synchronize(); }
    }
    return !receiver;
}

} // namespace window_system::quick
