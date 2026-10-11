#include "drag_controller.hpp"
#include "chrome/workspace_window.hpp"
#include "docking/dock_surface.hpp"
#include <QApplication>
#include <QCursor>
#include <QDebug>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QTabBar>
#include <QTabWidget>
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
        const auto preview = target.preview.translated(-geometry().topLeft());
        p.fillRect(preview, QColor(138, 82, 199, 62));
        p.setPen(QPen(QColor("#8A52C7"), 2)); p.drawRect(preview.adjusted(1, 1, -1, -1));
        const QPoint center = target.groupRect.center() - geometry().topLeft();
        const QVector<QPoint> offsets = {{0, 0}, {-34, 0}, {34, 0}, {0, -34}, {0, 34}};
        for (int i = 0; i < offsets.size(); ++i) {
            const QRect square(center + offsets[i] - QPoint(14, 14), QSize(28, 28));
            p.setPen(QPen(QColor("#725591"), 1));
            p.setBrush(i == int(target.placement) && !target.toEdge ? QColor("#D9BDF5") : QColor("#FAF8FD"));
            p.drawRoundedRect(square, 2, 2);
            QRect inner = square.adjusted(5, 5, -5, -5);
            if (i == 1) inner.setWidth(inner.width() / 2);
            if (i == 2) inner.setLeft(inner.center().x());
            if (i == 3) inner.setHeight(inner.height() / 2);
            if (i == 4) inner.setTop(inner.center().y());
            p.fillRect(inner, QColor("#8A52C7"));
        }
        QString text;
        if (target.toEdge) text = QStringLiteral("收纳到边栏");
        else if (target.placement == Placement::Center) text = QStringLiteral("合并 %1 个标签").arg(count);
        else text = whole ? QStringLiteral("保留分组并停靠") : QStringLiteral("创建相邻标签组");
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
        const bool success = SetWindowPos(reinterpret_cast<HWND>(target->winId()), HWND_TOP, 0, 0, 0, 0,
                                          SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        if (!success) return;
#else
        target->raise();
#endif
        revealedWindow_ = revealCandidate_;
        update(QCursor::pos());
    });
}
DragController::~DragController() { stop(); }
bool DragController::isActive() const { return active_; }
void DragController::beginTab(TabId tab, QPoint global)
{
    if (active_ || !manager_->model().tab(tab)) return;
    tab_ = tab; wholeWindow_ = QUuid{};
    sourceWindow_ = manager_->model().group(manager_->model().groupForTab(tab))->windowId;
    start(global);
}
void DragController::beginWindow(WindowId window, QPoint global)
{
    const auto* record = manager_->model().window(window);
    if (active_ || !record || record->kind != WindowKind::Floating) return;
    wholeWindow_ = window; tab_ = QUuid{}; sourceWindow_ = window; start(global);
}
void DragController::start(QPoint global)
{
    capture_ = manager_->windowWidget(sourceWindow_);
    if (!capture_) return;
    active_ = true; startPosition_ = global; originalGeometry_ = capture_->geometry();
    originalMaximized_ = capture_->isMaximized(); windowOffset_ = global - capture_->pos();
    if (!wholeWindow_.isNull() && originalMaximized_) { capture_->showNormal(); windowOffset_ = QPoint(capture_->width() / 2, 15); }
    overlay_ = new DockOverlay;
    overlay_->whole = !wholeWindow_.isNull();
    overlay_->count = wholeWindow_.isNull() ? 1 : manager_->model().orderedTabs(wholeWindow_).size();
    qApp->installEventFilter(this); capture_->grabMouse(Qt::ClosedHandCursor);
    update(global);
}
WindowId DragController::candidateAt(QPoint global) const
{
    if (!forcedWindow_.isNull()) return forcedWindow_;
#ifdef Q_OS_WIN
    // Hit testing and group geometry must use the same input event. Reading the
    // current cursor here races with queued mouse moves and can select a different
    // window. QWindow maps screen-aware global coordinates into this window's
    // logical client space; ClientToScreen supplies the true native origin.
    if (!capture_ || !capture_->windowHandle()) return {};
    const auto local = capture_->windowHandle()->mapFromGlobal(QPointF(global));
    const auto ratio = capture_->devicePixelRatioF();
    POINT nativePoint{qRound(local.x() * ratio), qRound(local.y() * ratio)};
    if (!ClientToScreen(reinterpret_cast<HWND>(capture_->winId()), &nativePoint)) return {};
    for (HWND handle = GetTopWindow(nullptr); handle; handle = GetWindow(handle, GW_HWNDNEXT)) {
        if (!IsWindowVisible(handle) || IsIconic(handle)) continue;
        const auto extendedStyle = GetWindowLongPtr(handle, GWL_EXSTYLE);
        // Click-through overlays are not input targets. Layered transparent
        // windows can cover the entire desktop while letting mouse input pass.
        if ((extendedStyle & WS_EX_LAYERED) && (extendedStyle & WS_EX_TRANSPARENT)) continue;
        DWORD cloaked = 0;
        if (SUCCEEDED(DwmGetWindowAttribute(handle, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) continue;
        RECT rect{}; GetWindowRect(handle, &rect);
        if (!PtInRect(&rect, nativePoint)) continue;
        if (overlay_ && handle == reinterpret_cast<HWND>(overlay_->winId())) continue;
        if (!wholeWindow_.isNull() && capture_ && handle == reinterpret_cast<HWND>(capture_->winId())) continue;
        DWORD process = 0; GetWindowThreadProcessId(handle, &process);
        if (process != GetCurrentProcessId()) {
            if (qEnvironmentVariableIsSet("WINDOW_SYSTEM_TRACE_DRAG")) qDebug() << "drag blocked by other process" << process << global << nativePoint.x << nativePoint.y;
            return {};
        }
        for (const auto id : manager_->model().windowOrder()) {
            auto* widget = manager_->windowWidget(id);
            if (widget && reinterpret_cast<HWND>(widget->winId()) == handle && widget->rect().contains(widget->mapFromGlobal(global))) return id;
        }
    }
#else
    auto order = manager_->model().windowOrder();
    for (auto it = order.crbegin(); it != order.crend(); ++it) {
        if (*it == wholeWindow_) continue;
        auto* widget = manager_->windowWidget(*it);
        if (widget && widget->isVisible() && !widget->isMinimized() && widget->geometry().contains(global)) return *it;
    }
#endif
    return {};
}
DropTarget DragController::hit(QPoint global)
{
    DropTarget result;
    result.window = candidateAt(global);
    if (result.window.isNull() || result.window == wholeWindow_) return {};
    auto* window = static_cast<WorkspaceWindow*>(manager_->windowWidget(result.window));
    if (!window || !window->isEnabled()) return {};
    auto* surface = window->surface();
    const QRect bounds(surface->mapToGlobal(QPoint()), surface->size());
    if (!bounds.contains(global)) return {};
    result.group = surface->groupAt(global);
    result.groupRect = surface->groupRects().value(result.group, surface->centralRectGlobal());
    if (result.groupRect.isEmpty()) result.groupRect = bounds;
    result.preview = result.groupRect;
    const int margin = 22;
    if (global.x() < bounds.left() + margin) { result.toEdge = true; result.edge = Edge::Left; result.preview = QRect(bounds.topLeft(), QSize(26, bounds.height())); }
    else if (global.x() > bounds.right() - margin) { result.toEdge = true; result.edge = Edge::Right; result.preview = QRect(bounds.right() - 26, bounds.top(), 26, bounds.height()); }
    else if (global.y() < bounds.top() + margin) { result.toEdge = true; result.edge = Edge::Top; result.preview = QRect(bounds.topLeft(), QSize(bounds.width(), 26)); }
    else if (global.y() > bounds.bottom() - margin) { result.toEdge = true; result.edge = Edge::Bottom; result.preview = QRect(bounds.left(), bounds.bottom() - 26, bounds.width(), 26); }
    if (result.toEdge) return result;
    const auto center = result.groupRect.center();
    const QVector<QPair<QPoint, Placement>> targets = {{{0, 0}, Placement::Center}, {{-34, 0}, Placement::Left},
        {{34, 0}, Placement::Right}, {{0, -34}, Placement::Top}, {{0, 34}, Placement::Bottom}};
    for (const auto& target : targets) if (QRect(center + target.first - QPoint(14, 14), QSize(28, 28)).contains(global)) result.placement = target.second;
    // Near group boundaries also gives a generous keyboard/mouse-friendly target.
    if (global.x() < result.groupRect.left() + 34) result.placement = Placement::Left;
    else if (global.x() > result.groupRect.right() - 34) result.placement = Placement::Right;
    else if (global.y() > result.groupRect.bottom() - 34) result.placement = Placement::Bottom;
    if ((result.placement == Placement::Left || result.placement == Placement::Right) && result.groupRect.width() < 340) return {};
    if ((result.placement == Placement::Top || result.placement == Placement::Bottom) && result.groupRect.height() < 230) return {};
    switch (result.placement) {
    case Placement::Left: result.preview.setWidth(result.preview.width() / 2); break;
    case Placement::Right: result.preview.setLeft(center.x()); break;
    case Placement::Top: result.preview.setHeight(result.preview.height() / 2); break;
    case Placement::Bottom: result.preview.setTop(center.y()); break;
    case Placement::Center: break;
    }
    if (auto* widget = qApp->widgetAt(global)) {
        for (auto* cursor = widget; cursor; cursor = cursor->parentWidget()) {
            if (auto* bar = qobject_cast<QTabBar*>(cursor)) {
                const auto index = bar->tabAt(bar->mapFromGlobal(global));
                result.insertionIndex = index < 0 ? bar->count() : index;
                result.placement = Placement::Center; result.preview = result.groupRect; break;
            }
        }
    }
    return result;
}
void DragController::update(QPoint global)
{
    if (!active_) return;
    if (!wholeWindow_.isNull() && capture_) capture_->move(global - windowOffset_);
    target_ = hit(global);
    if (qEnvironmentVariableIsSet("WINDOW_SYSTEM_TRACE_DRAG")) qDebug() << "drag target" << global << target_.window << target_.group << "whole" << wholeWindow_;
    if (manager_->behavior().revealDuringDrag && target_.window != revealCandidate_) {
        revealTimer_.stop(); revealCandidate_ = target_.window;
        if (!revealCandidate_.isNull() && revealCandidate_ != sourceWindow_ && revealCandidate_ != revealedWindow_)
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
    const auto source = sourceWindow_;
    stop();
    bool success = false;
    if (!target.window.isNull()) {
        if (target.toEdge) {
            if (!tab.isNull()) {
                success = manager_->execute("workspace.moveTabToEdge", {{"tab", tab}, {"targetWindow", target.window}, {"edge", int(target.edge)}}, source);
            } else success = manager_->execute("window.moveAllToEdge", {{"source", whole}, {"targetWindow", target.window}, {"edge", int(target.edge)}}, source);
        } else if (!whole.isNull()) {
            success = manager_->execute("window.moveAllTabs", {{"source", whole}, {"targetWindow", target.window}, {"group", target.group}, {"placement", int(target.placement)}}, source);
        } else if (target.group.isNull()) {
            success = manager_->execute("workspace.moveTabToWindow", {{"tab", tab}, {"targetWindow", target.window}, {"placement", int(target.placement)}}, source);
        } else success = manager_->moveTab(tab, target.group, target.placement, target.insertionIndex);
    } else if (!tab.isNull()) {
        const auto window = manager_->detachTab(tab, manager_->behavior().dragOutsideKind);
        if (auto* widget = manager_->windowWidget(window)) { widget->move(global - QPoint(80, 15)); manager_->showWindow(window); success = true; }
    } else success = true; // A floating title dragged without a target just moves.
    if (success && !target.window.isNull()) {
        manager_->showWindow(target.window);
        if (!tab.isNull()) manager_->activateTab(tab);
    } else if (!success && !whole.isNull()) {
        if (auto* widget = manager_->windowWidget(whole)) widget->setGeometry(originalGeometry_);
    }
}
void DragController::cancel()
{
    if (!active_) return;
    const auto source = wholeWindow_;
    stop();
    if (!source.isNull()) if (auto* widget = manager_->windowWidget(source)) {
        widget->setGeometry(originalGeometry_);
        if (originalMaximized_) widget->showMaximized();
    }
}
void DragController::stop()
{
    revealTimer_.stop(); active_ = false; qApp->removeEventFilter(this);
    if (capture_ && QWidget::mouseGrabber() == capture_) capture_->releaseMouse();
    capture_.clear();
    if (overlay_) delete overlay_;
    overlay_.clear(); revealCandidate_ = QUuid{}; revealedWindow_ = QUuid{}; forcedWindow_ = QUuid{};
}
void DragController::selectTarget()
{
    if (!active_ || choosing_) return;
    choosing_ = true;
    QMenu menu(capture_);
    applyTheme(&menu, manager_->theme());
    for (const auto id : manager_->model().windowOrder()) {
        if (id == wholeWindow_) continue;
        auto* window = manager_->windowWidget(id);
        if (!window) continue;
        auto* action = menu.addAction(window->windowTitle()); action->setData(id);
    }
    if (auto* action = menu.exec(QCursor::pos())) {
        forcedWindow_ = action->data().toUuid();
        if (auto* widget = manager_->windowWidget(forcedWindow_)) {
            if (widget->isMinimized()) widget->showNormal();
            widget->raise();
        }
    }
    choosing_ = false;
    if (capture_ && active_) { capture_->grabMouse(Qt::ClosedHandCursor); update(QCursor::pos()); }
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
        if (key->key() == Qt::Key_Escape) cancel();
        else if (key->key() == Qt::Key_Tab) selectTarget();
        return true;
    }
    if (event->type() == QEvent::ApplicationDeactivate ||
        (event->type() == QEvent::UngrabMouse && watched == capture_)) cancel();
    return false;
}
} // namespace window_system
