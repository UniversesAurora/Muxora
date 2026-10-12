// SPDX-License-Identifier: MIT
#include "drag_controller.hpp"
#include "window_manager_p.hpp"
#include "dock_guides.hpp"
#include <QApplication>
#include <QCursor>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QWindow>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <dwmapi.h>
#endif

namespace window_system {
class DockOverlay final : public QWidget {
public:
    DropTarget target;
    int count = 1;
    bool whole = false;
    explicit DockOverlay() : QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus) {
        setObjectName("DockOverlay"); setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_TransparentForMouseEvents); setAttribute(Qt::WA_ShowWithoutActivating);
    }
protected:
    void paintEvent(QPaintEvent*) override {
        if (target.window.isNull()) return;
        QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
        const auto origin = geometry().topLeft();
        if (target.accepted) {
            const auto preview = target.preview.translated(-origin);
            p.fillRect(preview, QColor(138, 82, 199, 62));
            p.setPen(QPen(QColor("#8A52C7"), 2)); p.drawRect(preview.adjusted(1, 1, -1, -1));
        }
        for (const auto& guide : dockGuides(target.windowRect, target.groupRect, target.splitGroup)) {
            const auto square = guide.rect.translated(-origin);
            const bool selected = target.accepted && target.insertionIndex < 0 && target.toEdge == guide.toEdge &&
                (guide.toEdge ? target.edge == guide.edge && target.pinned == guide.pinned : target.placement == guide.placement);
            p.setPen(QPen(QColor("#725591"), 1));
            p.setBrush(selected ? QColor("#D9BDF5") : QColor("#FAF8FD"));
            p.drawRoundedRect(square, 2, 2);
            auto inner = square.adjusted(6, 6, -6, -6);
            if (guide.toEdge) {
                if (guide.edge == Edge::Left) inner.setWidth(guide.pinned ? 8 : 3);
                if (guide.edge == Edge::Right) inner.setLeft(inner.right() - (guide.pinned ? 8 : 3));
                if (guide.edge == Edge::Top) inner.setHeight(guide.pinned ? 8 : 3);
                if (guide.edge == Edge::Bottom) inner.setTop(inner.bottom() - (guide.pinned ? 8 : 3));
            } else {
                if (guide.placement == Placement::Left) inner.setWidth(inner.width() / 2);
                if (guide.placement == Placement::Right) inner.setLeft(inner.center().x());
                if (guide.placement == Placement::Top) inner.setHeight(inner.height() / 2);
                if (guide.placement == Placement::Bottom) inner.setTop(inner.center().y());
            }
            p.fillRect(inner, QColor("#8A52C7"));
        }
        if (!target.accepted) return;
        const auto text = target.toEdgeBar ? QStringLiteral("插入 %1 个边栏标签").arg(count)
            : target.toEdge ? (target.pinned ? QStringLiteral("固定到边栏") : QStringLiteral("收纳到边栏"))
            : target.insertionIndex >= 0 ? QStringLiteral("插入 %1 个标签").arg(count)
            : target.placement == Placement::Center ? QStringLiteral("合并 %1 个标签").arg(count)
            : whole ? QStringLiteral("保留分组并停靠") : QStringLiteral("创建相邻标签组");
        const auto preview = target.preview.translated(-origin);
        const QRect label(preview.topLeft() + QPoint(8, 8), QSize(qMax(180, fontMetrics().horizontalAdvance(text) + 20), 28));
        p.fillRect(label, QColor("#4D356E")); p.setPen(Qt::white); p.drawText(label, Qt::AlignCenter, text);
    }
};

DragController::DragController(WindowManager* manager) : QObject(manager), manager_(manager)
{
    revealTimer_.setSingleShot(true);
    QObject::connect(&revealTimer_, &QTimer::timeout, this, [this] {
        if (!active_ || revealCandidate_.isNull()) return;
        auto* target = manager_->windowWidget(revealCandidate_);
        if (!target || target->isMinimized()) return;
#ifdef Q_OS_WIN
        if (!SetWindowPos(reinterpret_cast<HWND>(target->winId()), HWND_TOP, 0, 0, 0, 0,
                          SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE)) return;
#else
        target->raise();
#endif
        revealedWindow_ = revealCandidate_;
        if (capture_) capture_->raise();
        update(lastPosition_);
    });
}
DragController::~DragController() { stop(); }
bool DragController::isActive() const { return active_; }
void DragController::beginTab(TabId tab, QPoint global)
{
    if (active_ || !manager_->model().tab(tab)) return;
    tab_ = tab; wholeWindow_ = WindowId{}; detached_ = true;
    sourceWindow_ = manager_->model().group(manager_->model().groupForTab(tab))->windowId;
    originalModel_ = manager_->d->model; originalOptions_ = manager_->d->options;
    originalWindows_.clear(); originalMaximizedWindows_.clear();
    for (const auto id : manager_->model().windowOrder()) if (auto* window = manager_->windowWidget(id)) {
        originalWindows_.insert(id, static_cast<WorkspaceWindow*>(window)->normalPlacementGeometry());
        if (window->isMaximized()) originalMaximizedWindows_.insert(id);
    }
    auto* source = static_cast<WorkspaceWindow*>(manager_->windowWidget(sourceWindow_));
    originalSourceStrips_ = source ? source->saveStripLayout() : QJsonObject{};
    const auto group = manager_->model().groupForTab(tab);
    auto origin = source ? source->surface()->groupRects().value(group) : QRect();
    if (origin.isEmpty() && source) if (auto* peek = source->findChild<QWidget*>("PeekPanel"); peek && peek->isVisible())
        origin = QRect(peek->mapToGlobal(QPoint()), peek->size());
    if (origin.isEmpty() && source) if (const auto* record = manager_->model().group(group); record && record->location == GroupLocation::AutoHide)
        origin = source->surface()->edgeBarRects().value(record->edge);
    movingWindow_ = manager_->detachTabImpl(tab, WindowKind::Floating, [this, global, origin](QWidget* window) {
        windowOffset_ = QPoint(qBound(28, global.x() - origin.left(), window->width() - 28), 16);
        window->move(global - windowOffset_);
    });
    if (movingWindow_.isNull()) { detached_ = false; return; }
    capture_ = manager_->windowWidget(movingWindow_);
    if (!capture_) return;
    start(global);
}
void DragController::beginWindow(WindowId window, QPoint global)
{
    const auto* record = manager_->model().window(window);
    if (active_ || !record || record->kind != WindowKind::Floating) return;
    wholeWindow_ = window; tab_ = TabId{}; sourceWindow_ = window; movingWindow_ = window; detached_ = false;
    capture_ = manager_->windowWidget(window);
    if (!capture_) return;
    originalGeometry_ = static_cast<WorkspaceWindow*>(capture_.data())->normalPlacementGeometry();
    originalMaximized_ = capture_->isMaximized();
    windowOffset_ = global - capture_->pos();
    if (originalMaximized_) { capture_->showNormal(); windowOffset_ = QPoint(capture_->width() / 2, 16); }
    start(global);
}
void DragController::start(QPoint global)
{
    if (!capture_) return;
    active_ = true; startPosition_ = global;
    overlay_ = new DockOverlay; overlay_->whole = !wholeWindow_.isNull();
    overlay_->count = wholeWindow_.isNull() ? 1 : manager_->model().orderedTabs(wholeWindow_).size();
    qApp->installEventFilter(this); capture_->grabMouse(Qt::ClosedHandCursor);
    update(global);
}
WindowId DragController::candidateAt(QPoint global) const
{
    if (!forcedWindow_.isNull() && forcedWindow_ != movingWindow_) return forcedWindow_;
#ifdef Q_OS_WIN
    if (!capture_ || !capture_->windowHandle()) return {};
    // Resolve the same queued event in Qt and native coordinates. Do not sample
    // GetCursorPos, which can already point at a later mouse event.
    const auto local = capture_->windowHandle()->mapFromGlobal(QPointF(global));
    const auto ratio = capture_->devicePixelRatioF();
    POINT nativePoint{qRound(local.x() * ratio), qRound(local.y() * ratio)};
    if (!ClientToScreen(reinterpret_cast<HWND>(capture_->winId()), &nativePoint)) return {};
    for (HWND handle = GetTopWindow(nullptr); handle; handle = GetWindow(handle, GW_HWNDNEXT)) {
        if (!IsWindowVisible(handle) || IsIconic(handle)) continue;
        const auto extendedStyle = GetWindowLongPtr(handle, GWL_EXSTYLE);
        if ((extendedStyle & WS_EX_LAYERED) && (extendedStyle & WS_EX_TRANSPARENT)) continue;
        DWORD cloaked = 0;
        if (SUCCEEDED(DwmGetWindowAttribute(handle, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) continue;
        RECT rect{}; GetWindowRect(handle, &rect);
        if (!PtInRect(&rect, nativePoint)) continue;
        if (overlay_ && handle == reinterpret_cast<HWND>(overlay_->winId())) continue;
        if (capture_ && handle == reinterpret_cast<HWND>(capture_->winId())) continue;
        DWORD process = 0; GetWindowThreadProcessId(handle, &process);
        if (process != GetCurrentProcessId()) return {};
        for (const auto id : manager_->model().windowOrder()) {
            if (id == movingWindow_) continue;
            auto* widget = manager_->windowWidget(id);
            if (widget && reinterpret_cast<HWND>(widget->winId()) == handle &&
                widget->rect().contains(widget->mapFromGlobal(global))) return id;
        }
    }
#else
    const auto order = manager_->model().windowOrder();
    for (auto it = order.crbegin(); it != order.crend(); ++it) {
        if (*it == movingWindow_) continue;
        auto* widget = manager_->windowWidget(*it);
        if (widget && widget->isVisible() && !widget->isMinimized() && widget->geometry().contains(global)) return *it;
    }
#endif
    return {};
}
DropTarget DragController::hit(QPoint global)
{
    DropTarget result; result.window = candidateAt(global);
    if (result.window.isNull() || result.window == movingWindow_) return {};
    auto* window = static_cast<WorkspaceWindow*>(manager_->windowWidget(result.window));
    if (!window || !window->isEnabled()) return {};
    auto* surface = window->surface();
    result.windowRect = QRect(surface->mapToGlobal(QPoint()), surface->size());
    if (!result.windowRect.contains(global)) return {};
    result.group = surface->groupAt(global);
    if (const auto* group = manager_->model().group(result.group)) result.splitGroup = group->location == GroupLocation::Central;
    result.groupRect = surface->groupRects().value(result.group, surface->centralRectGlobal());
    if (result.groupRect.isEmpty()) result.groupRect = result.windowRect;
    // A displayed side bar is an insertion strip along its full axis, including
    // the area where the outer guide would otherwise overlap that strip.
    const auto edgeBars = surface->edgeBarRects();
    for (auto it = edgeBars.cbegin(); it != edgeBars.cend(); ++it) if (it.value().contains(global)) {
        result.accepted = true; result.toEdge = true; result.edge = it.key(); result.pinned = false;
        result.toEdgeBar = true;
        result.insertionIndex = surface->edgeTabInsertionIndex(it.key(), global);
        break;
    }
    for (const auto& guide : dockGuides(result.windowRect, result.groupRect, result.splitGroup)) if (!result.accepted && guide.rect.contains(global)) {
        result.accepted = true; result.toEdge = guide.toEdge; result.pinned = guide.pinned;
        result.edge = guide.edge; result.placement = guide.placement; break;
    }
    // A visible tab strip is an insertion target. Content bodies and invisible
    // edge margins deliberately have no implicit drop operation.
    const auto barGroup = surface->tabBarAt(global);
    if (!result.accepted && !barGroup.isNull()) {
        result.accepted = true; result.group = barGroup; result.toEdge = false;
        result.placement = Placement::Center;
        result.groupRect = surface->groupRects().value(barGroup);
        result.insertionIndex = surface->tabInsertionIndex(barGroup, global);
    }
    if (!result.accepted) return result;
    result.preview = result.groupRect;
    if (result.toEdge) {
        const auto bounds = result.windowRect;
        const int extent = result.pinned ? 160 : 26;
        switch (result.edge) {
        case Edge::Left: result.preview = QRect(bounds.topLeft(), QSize(extent, bounds.height())); break;
        case Edge::Right: result.preview = QRect(bounds.right() - extent + 1, bounds.top(), extent, bounds.height()); break;
        case Edge::Top: result.preview = QRect(bounds.topLeft(), QSize(bounds.width(), extent)); break;
        case Edge::Bottom: result.preview = QRect(bounds.left(), bounds.bottom() - extent + 1, bounds.width(), extent); break;
        }
        if (result.toEdgeBar) result.preview = surface->edgeTabInsertionRect(result.edge, result.insertionIndex);
    } else {
        const auto center = result.preview.center();
        switch (result.placement) {
        case Placement::Left: result.preview.setWidth(result.preview.width() / 2); break;
        case Placement::Right: result.preview.setLeft(center.x()); break;
        case Placement::Top: result.preview.setHeight(result.preview.height() / 2); break;
        case Placement::Bottom: result.preview.setTop(center.y()); break;
        case Placement::Center: break;
        }
        if (result.insertionIndex >= 0) {
            const auto strip = surface->tabBarRects().value(result.group);
            result.preview = QRect(global.x() - 2, strip.top(), 4, strip.height());
        }
    }
    return result;
}
void DragController::update(QPoint global)
{
    if (!active_) return;
    lastPosition_ = global;
    if (capture_) capture_->move(global - windowOffset_);
    target_ = hit(global);
    for (const auto id : manager_->model().windowOrder()) {
        auto* window = static_cast<WorkspaceWindow*>(manager_->windowWidget(id));
        if (window) window->surface()->setDragHover(id == target_.window ? global : QPoint(-100000, -100000));
    }
    if (manager_->behavior().revealDuringDrag && target_.window != revealCandidate_) {
        revealTimer_.stop(); revealCandidate_ = target_.window;
        if (!revealCandidate_.isNull() && revealCandidate_ != movingWindow_ && revealCandidate_ != revealedWindow_)
            revealTimer_.start(manager_->behavior().revealDelayMs);
    }
    if (target_.window.isNull()) { if (overlay_) overlay_->hide(); return; }
    auto* window = manager_->windowWidget(target_.window);
    overlay_->target = target_;
    overlay_->setGeometry(QRect(window->mapToGlobal(QPoint()), window->size()));
    overlay_->show(); overlay_->raise(); overlay_->update();
}
void DragController::finish(QPoint global)
{
    if (!active_) return;
    update(global);
    const auto target = target_; const auto tab = tab_; const auto whole = wholeWindow_;
    const auto moving = movingWindow_; const auto source = sourceWindow_;
    stop();
    bool success = !target.accepted;
    if (target.accepted) {
        if (target.toEdgeBar) {
            if (!tab.isNull()) success = manager_->execute("workspace.moveTabToEdgeBar",
                {{"tab", tab}, {"targetWindow", target.window}, {"edge", int(target.edge)}, {"index", target.insertionIndex}}, moving);
            else success = manager_->execute("window.moveAllToEdgeBar",
                {{"source", whole}, {"targetWindow", target.window}, {"edge", int(target.edge)}, {"index", target.insertionIndex}}, source);
        } else if (target.toEdge) {
            if (!tab.isNull()) success = manager_->execute("workspace.moveTabToEdge",
                {{"tab", tab}, {"targetWindow", target.window}, {"edge", int(target.edge)}, {"pinned", target.pinned}}, moving);
            else success = manager_->execute("window.moveAllToEdge",
                {{"source", whole}, {"targetWindow", target.window}, {"edge", int(target.edge)}, {"pinned", target.pinned}}, source);
        } else if (!whole.isNull()) success = manager_->execute("window.moveAllTabs",
            {{"source", whole}, {"targetWindow", target.window}, {"group", target.group},
             {"placement", int(target.placement)}, {"index", target.insertionIndex}}, source);
        else if (target.group.isNull()) success = manager_->execute("workspace.moveTabToWindow",
            {{"tab", tab}, {"targetWindow", target.window}, {"placement", int(target.placement)}}, moving);
        else success = manager_->moveTab(tab, target.group, target.placement, target.insertionIndex);
    } else if (!tab.isNull() && manager_->behavior().dragOutsideKind == WindowKind::Workbench)
        success = manager_->promoteWindow(moving);
    if (success && target.accepted) {
        manager_->showWindow(target.window);
        if (!tab.isNull()) manager_->activateTab(tab);
    }
    // A failed or absent target leaves the real floating window and live content
    // intact. No second tear-out is created when the button is released.
    originalModel_ = {}; originalOptions_.clear(); originalWindows_.clear(); originalMaximizedWindows_.clear();
    originalSourceStrips_ = {};
    detached_ = false;
}
void DragController::cancel()
{
    if (!active_) return;
    const bool detached = detached_;
    const auto window = wholeWindow_; const auto geometry = originalGeometry_; const bool maximized = originalMaximized_;
    stop();
    if (detached) {
        manager_->d->model = originalModel_; manager_->d->options = originalOptions_;
        manager_->refresh();
        if (auto* source = static_cast<WorkspaceWindow*>(manager_->windowWidget(sourceWindow_)))
            source->restoreStripLayout(originalSourceStrips_);
        for (auto it = originalWindows_.cbegin(); it != originalWindows_.cend(); ++it) if (auto* widget = manager_->windowWidget(it.key())) {
            static_cast<WorkspaceWindow*>(widget)->restorePlacement(it.value(), originalMaximizedWindows_.contains(it.key()));
        }
        manager_->showWindow(sourceWindow_); manager_->activateTab(tab_);
    } else if (auto* widget = manager_->windowWidget(window)) {
        static_cast<WorkspaceWindow*>(widget)->restorePlacement(geometry, maximized);
    }
    originalModel_ = {}; originalOptions_.clear(); originalWindows_.clear(); originalMaximizedWindows_.clear(); detached_ = false;
    originalSourceStrips_ = {};
}
void DragController::releaseForMutation()
{
    if (!active_) return;
    stop(); detached_ = false;
    originalModel_ = {}; originalOptions_.clear(); originalWindows_.clear(); originalMaximizedWindows_.clear();
    originalSourceStrips_ = {};
}
void DragController::stop()
{
    revealTimer_.stop(); active_ = false; qApp->removeEventFilter(this);
    if (capture_ && QWidget::mouseGrabber() == capture_) capture_->releaseMouse();
    capture_.clear();
    if (overlay_) delete overlay_;
    overlay_.clear(); revealCandidate_ = WindowId{}; revealedWindow_ = WindowId{}; forcedWindow_ = WindowId{};
    for (const auto id : manager_->model().windowOrder()) {
        auto* window = static_cast<WorkspaceWindow*>(manager_->windowWidget(id));
        if (window) window->surface()->setDragHover(QPoint(-100000, -100000));
    }
}
void DragController::selectTarget()
{
    if (!active_ || choosing_) return;
    choosing_ = true; QMenu menu(capture_); applyTheme(&menu, manager_->theme());
    for (const auto id : manager_->model().windowOrder()) {
        if (id == movingWindow_) continue;
        if (auto* window = manager_->windowWidget(id)) {
            auto* action = menu.addAction(window->windowTitle()); action->setData(id);
        }
    }
    if (auto* action = menu.exec(lastPosition_)) {
        forcedWindow_ = action->data().toUuid();
        if (auto* widget = manager_->windowWidget(forcedWindow_)) {
            if (widget->isMinimized()) widget->showNormal();
            widget->raise();
        }
    }
    choosing_ = false;
    if (capture_ && active_) { capture_->grabMouse(Qt::ClosedHandCursor); update(lastPosition_); }
}
bool DragController::eventFilter(QObject* watched, QEvent* event)
{
    if (!active_ || choosing_) return false;
    if (event->type() == QEvent::MouseMove) { update(static_cast<QMouseEvent*>(event)->globalPosition().toPoint()); return true; }
    if (event->type() == QEvent::MouseButtonRelease && static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
        finish(static_cast<QMouseEvent*>(event)->globalPosition().toPoint()); return true;
    }
    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape) cancel(); else if (key->key() == Qt::Key_Tab) selectTarget();
        return true;
    }
    if (event->type() == QEvent::ApplicationDeactivate ||
        (event->type() == QEvent::UngrabMouse && watched == capture_)) cancel();
    return false;
}
} // namespace window_system
