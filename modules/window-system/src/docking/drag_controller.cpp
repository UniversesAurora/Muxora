// SPDX-License-Identifier: MIT
#include <QCoreApplication>
#include "drag_controller.hpp"
#include "window_manager_p.hpp"
#include "dock_guides.hpp"
#include "drag_presentation.hpp"
#include "commands/popup_menu.hpp"
#include "platform/windows/window_frame.hpp"
#include <QApplication>
#include <QCursor>
#include <QKeyEvent>
#include <QLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QAbstractNativeEventFilter>
#include <QScopedValueRollback>
#include <QScopeGuard>
#include <QWindow>
#include <QWheelEvent>
#include <utility>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <dwmapi.h>
#endif

namespace window_system {
namespace {
bool affectsDockGeometry(QEvent::Type type)
{
    switch (type) {
    case QEvent::Move:
    case QEvent::Resize:
    case QEvent::LayoutRequest:
    case QEvent::WindowStateChange:
    case QEvent::ScreenChangeInternal:
    case QEvent::DevicePixelRatioChange:
    case QEvent::Show:
    case QEvent::Hide:
        return true;
    default:
        return false;
    }
}
} // namespace
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
        for (const auto& guide : target.guides) {
            const auto square = guide.rect.translated(-origin);
            const bool selected = target.accepted && target.insertionIndex < 0 && target.toEdge == guide.toEdge &&
                (guide.toEdge ? target.edge == guide.edge && target.pinned == guide.pinned : target.placement == guide.placement);
            p.setPen(QPen(QColor("#725591"), 1));
            p.setBrush(selected ? QColor("#D9BDF5") : QColor("#FAF8FD"));
            p.drawRoundedRect(square, 2, 2);
            const int padding = std::clamp(square.width() / 5, 2, 6);
            auto inner = square.adjusted(padding, padding, -padding, -padding);
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
        const auto text = target.toEdgeBar ? QCoreApplication::translate("WindowSystem", "Insert %n sidebar tabs", nullptr, count)
            : target.toEdge ? (target.pinned ? QCoreApplication::translate("WindowSystem", "Pin in sidebar") : QCoreApplication::translate("WindowSystem", "Auto-hide on sidebar"))
            : target.insertionIndex >= 0 ? QCoreApplication::translate("WindowSystem", "Insert %n tabs", nullptr, count)
            : target.placement == Placement::Center ? QCoreApplication::translate("WindowSystem", "Merge %n tabs", nullptr, count)
            : whole ? QCoreApplication::translate("WindowSystem", "Dock while preserving tab groups") : QCoreApplication::translate("WindowSystem", "Create adjacent tab group");
        const auto preview = target.preview.translated(-origin);
        const QRect label(preview.topLeft() + QPoint(8, 8), QSize(qMax(180, fontMetrics().horizontalAdvance(text) + 20), 28));
        p.fillRect(label, QColor("#4D356E")); p.setPen(Qt::white); p.drawText(label, Qt::AlignCenter, text);
    }
};

// Watches the moving window while the system's move loop drives it.
class NativeMoveWatcher final : public QAbstractNativeEventFilter {
public:
    NativeMoveWatcher(DragController* controller, quintptr window, quint64 generation)
        : controller_(controller), window_(window), generation_(generation) {}
    bool nativeEventFilter(const QByteArray&, void* message, qintptr*) override
    {
#ifdef Q_OS_WIN
        const auto* msg = static_cast<const MSG*>(message);
        if (quintptr(msg->hwnd) != window_ || !controller_) return false;
        if (msg->message == WM_MOVE || msg->message == WM_MOVING) controller_->nativeMoved();
        else if (msg->message == WM_ENTERSIZEMOVE) controller_->nativeMoveBegan(generation_);
        else if (msg->message == WM_EXITSIZEMOVE && !ending_) {
            ending_ = true;
            const QPointer<DragController> controller = controller_;
            const auto generation = generation_;
            QTimer::singleShot(0, controller, [controller, generation] { if (controller) controller->nativeMoveEnded(generation); });
        }
#else
        Q_UNUSED(message);
#endif
        return false;
    }
private:
    QPointer<DragController> controller_;
    quintptr window_ = 0;
    quint64 generation_ = 0;
    bool ending_ = false;
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
DragController::~DragController()
{
    stop();
    if (nativeWatcher_) { qApp->removeNativeEventFilter(nativeWatcher_); delete nativeWatcher_; }
}
bool DragController::nativeMoveAvailable() const
{
#ifdef Q_OS_WIN
    // Only real mouse input can drive the system's move loop; synthetic Qt
    // events (tests, automation) keep the drag's own capture.
    return nativeInput_ && capture_ && capture_->windowHandle() && !labelOnly_;
#else
    return false;
#endif
}
void DragController::enterNativeMove()
{
#ifdef Q_OS_WIN
    if (nativeMoving_ || leavingNativeMove_ || !nativeMoveAvailable()) return;
    const bool left = !GetSystemMetrics(SM_SWAPBUTTON);
    if (!(GetAsyncKeyState(left ? VK_LBUTTON : VK_RBUTTON) & 0x8000)) return;
    const QPointer<DragController> self(this);
    // Tear-out builds a window synchronously. The physical pointer can move
    // far beyond the event which started it before the new HWND is ready.
    // Give Windows the current cursor anchor, not that stale event position.
    const auto current = QCursor::pos();
    capture_->move(current - normalOffset_);
    if (!self || !active_ || !capture_) return;
    lastPosition_ = current;
    transferringCapture_ = true;
    if (QWidget::mouseGrabber() == capture_) capture_->releaseMouse();
    if (!self) return;
    transferringCapture_ = false;
    if (!active_ || !capture_ || !capture_->windowHandle()) return;
    if (!nativeWatcher_) {
        nativeWatcher_ = new NativeMoveWatcher(this, quintptr(capture_->winId()), ++nativeMoveGeneration_);
        qApp->installNativeEventFilter(nativeWatcher_);
    }
    nativeMoveStarted_ = false;
    nativeMoving_ = true;
    const bool started = capture_->windowHandle()->startSystemMove();
    if (!self) return;
    if (!started) {
        nativeMoving_ = false;
        if (nativeWatcher_) { qApp->removeNativeEventFilter(nativeWatcher_); delete nativeWatcher_; nativeWatcher_ = nullptr; }
        if (self && active_ && capture_) capture_->grabMouse(Qt::ClosedHandCursor);
    }
#endif
}
void DragController::leaveNativeMove()
{
#ifdef Q_OS_WIN
    if (!nativeMoving_) return;
    // The system checks for capture loss after this native message returns.
    // Regrabbing the same HWND here keeps its move loop running, even though
    // the window has already become a compact label. Wait for EXITSIZEMOVE.
    leavingNativeMove_ = true; nativeMoving_ = false;
    transferringCapture_ = true;
    // startSystemMove posts its system command. A leave can arrive before
    // Windows has entered the loop or taken capture. Releasing now would do
    // nothing; complete the release after the loop actually starts instead.
    if (nativeMoveStarted_) {
        if (capture_ && GetCapture() == reinterpret_cast<HWND>(capture_->effectiveWinId())) ReleaseCapture();
        else queueNativeRelease();
    }
#endif
}
void DragController::nativeMoveBegan(quint64 generation)
{
    if (generation != nativeMoveGeneration_ || !active_) return;
    nativeMoveStarted_ = true;
    if (leavingNativeMove_) queueNativeRelease();
}
void DragController::queueNativeRelease()
{
#ifdef Q_OS_WIN
    if (nativeReleaseQueued_) return;
    nativeReleaseQueued_ = true;
    const auto generation = nativeMoveGeneration_;
    QTimer::singleShot(0, this, [self = QPointer<DragController>(this), generation] {
        if (!self || generation != self->nativeMoveGeneration_) return;
        self->nativeReleaseQueued_ = false;
        if (!self->active_ || !self->leavingNativeMove_ || !self->nativeMoveStarted_ || !self->capture_) return;
        // ENTERSIZEMOVE precedes Windows' SetCapture. The queued callback
        // runs after that message, when release can actually end the loop.
        if (GetCapture() == reinterpret_cast<HWND>(self->capture_->effectiveWinId())) {
            ReleaseCapture();
        }
    });
#endif
}
void DragController::nativeMoved()
{
    if (active_ && leavingNativeMove_) { queueNativeRelease(); return; }
    if (!active_ || !nativeMoving_) return;
    lastPosition_ = QCursor::pos();
    refreshHover();
}
void DragController::nativeMoveEnded(quint64 generation)
{
    if (generation != nativeMoveGeneration_) return;
    nativeMoveStarted_ = false; nativeReleaseQueued_ = false;
    if (nativeWatcher_) { qApp->removeNativeEventFilter(nativeWatcher_); delete nativeWatcher_; nativeWatcher_ = nullptr; }
    const bool handingBack = std::exchange(leavingNativeMove_, false);
    transferringCapture_ = false;
    if (!active_) return;
    if (handingBack) {
#ifdef Q_OS_WIN
        const bool left = !GetSystemMetrics(SM_SWAPBUTTON);
        if (GetAsyncKeyState(left ? VK_LBUTTON : VK_RBUTTON) & 0x8000) {
            const QPointer<DragController> self(this);
            if (capture_) capture_->grabMouse(Qt::ClosedHandCursor);
            if (!self || !active_) return;
            update(QCursor::pos());
            if (self && active_ && target_.insertionIndex < 0) enterNativeMove();
            return;
        }
#endif
    }
    nativeMoving_ = false;
    if (!handingBack && capture_) {
        // Windows owns the final placement of a native move. Its initial
        // cursor offset can change (DPI, native snapping); finish() must not
        // move the window back using our offset from before the system loop.
        windowOffset_ = QCursor::pos() - capture_->pos();
        normalOffset_ = windowOffset_;
    }
    // The system's loop consumed the button's release, so Qt still holds the
    // widget it was pressed on and sends no hover to the others. Deliver the
    // release once the drop is done.
    const auto releaseStaleButton = qScopeGuard([] {
        QTimer::singleShot(0, qApp, [] {
            const auto global = QCursor::pos();
            auto* window = QGuiApplication::topLevelAt(global);
            if (!window) window = QGuiApplication::focusWindow();
            if (!window) return;
            const auto local = window->mapFromGlobal(QPointF(global));
            QMouseEvent release(QEvent::MouseButtonRelease, local, local, QPointF(global), Qt::LeftButton,
                                Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &release);
        });
    });
#ifdef Q_OS_WIN
    // The loop ended with the button still held only when Escape canceled it.
    const bool left = !GetSystemMetrics(SM_SWAPBUTTON);
    if (GetAsyncKeyState(left ? VK_LBUTTON : VK_RBUTTON) & 0x8000) { cancel(); return; }
#endif
    finish(QCursor::pos());
}
bool DragController::isActive() const { return active_; }
void DragController::beginFolder(FolderId folder, QPoint global)
{
    beginFolder(folder, DragStart{global, global, {}});
}
void DragController::beginFolder(FolderId folder, const DragStart& start)
{
    const auto tabs = manager_->model().folderTabs(folder);
    if (hasStableLayout() || tabs.isEmpty()) return;
    folder_ = folder;
    beginTab(tabs.first(), start);
    if (!active_) folder_ = {};
}
void DragController::beginTab(TabId tab, QPoint global)
{
    beginTab(tab, DragStart{global, global, {}});
}
void DragController::beginTab(TabId tab, const DragStart& startPoint)
{
    const auto global = startPoint.currentGlobal;
    if (hasStableLayout() || !manager_->model().tab(tab)) return;
    if (!manager_->model().folder(folder_) || !manager_->model().folderTabs(folder_).contains(tab)) folder_ = {};
    originalLayout_ = manager_->saveLayout();
    const QPointer<DragController> self(this);
    preparing_ = true;
    const auto endPreparation = qScopeGuard([self] { if (self) self->preparing_ = false; });
    tab_ = tab; wholeWindow_ = WindowId{}; detached_ = false; movingWindow_ = WindowId{};
    sourceWindow_ = manager_->model().group(manager_->model().groupForTab(tab))->windowId;
    sourceGroup_ = manager_->model().groupForTab(tab);
    const auto* sourceGroup = manager_->model().group(sourceGroup_);
    sourceIsEdge_ = sourceGroup->location == GroupLocation::AutoHide;
    sourceEdge_ = sourceGroup->edge;
    originalModel_ = manager_->d->model; originalOptions_ = manager_->d->options;
    originalWindowVisibility_ = manager_->d->windowVisibility; originalVisibilityKinds_ = manager_->d->visibilityKinds;
    originalWindows_.clear(); originalMaximizedWindows_.clear();
    for (const auto id : manager_->model().windowOrder()) if (auto* window = manager_->windowWidget(id)) {
        originalWindows_.insert(id, static_cast<WorkspaceWindow*>(window)->normalPlacementGeometry());
        if (window->isMaximized()) originalMaximizedWindows_.insert(id);
    }
    auto* source = static_cast<WorkspaceWindow*>(manager_->windowWidget(sourceWindow_));
    originalSourceStrips_ = source ? source->saveStripLayout() : QJsonObject{};
    originalSourceInactiveStrips_ = source ? source->saveInactiveStripLayouts() : QJsonObject{};
    if (!source) { originalLayout_ = {}; return; }
    const auto strip = sourceIsEdge_ ? source->surface()->edgeBarRects().value(sourceEdge_)
        : source->surface()->tabBarRects().value(sourceGroup_);
    labelOnly_ = strip.contains(startPoint.pressGlobal);
    liftGrab_ = startPoint.anchor;
    if (!liftGrab_.sourceSize.isValid())
        liftGrab_ = folder_.isNull() ? source->surface()->tabGrabAnchor(tab, startPoint.pressGlobal)
                                    : source->surface()->folderGrabAnchor(folder_, startPoint.pressGlobal);
    capture_ = source;
    if (labelOnly_) {
        // A group's strip lifts the tab itself (tab strip design 6.1); a rail
        // shows a label window following the pointer.
        if (sourceIsEdge_) {
            const auto pixmap = source->surface()->dragLabelPixmap(tab);
            if (pixmap.isNull()) { labelOnly_ = false; originalLayout_ = {}; return; }
            labelPreview_ = new DragLabelPreview(pixmap, liftGrab_);
        }
        source->surface()->captureStripScrollOffsets();
        if (folder_.isNull()) source->surface()->setDraggedTab(tab);
        else source->surface()->setDraggedFolder(folder_);
        if (!sourceIsEdge_) source->surface()->setLiftedTab(sourceGroup_, global, liftGrab_);
        const int extent = source->surface()->dragLabelExtent(tab, sourceIsEdge_);
        const int index = sourceIsEdge_ ? manager_->model().autoHiddenTabs(sourceWindow_, sourceEdge_).indexOf(tab)
            : manager_->model().group(sourceGroup_)->tabs.indexOf(tab);
        if (sourceIsEdge_) source->surface()->setEdgeInsertionPreview(sourceEdge_, index, extent);
        else source->surface()->setInsertionPreview(sourceGroup_, index, carriedLabels(), originalModel_.isTabSticky(tab), carriedKind());
        source->surface()->restoreStripScrollOffsets();
    } else if (!detach(global)) { if (self) originalLayout_ = {}; return; }
    if (self) start(global);
}
bool DragController::detach(QPoint global)
{
    const QPointer<DragController> self(this);
    const bool wasActive = active_;
    transferringCapture_ = true;
    QPointer<WorkspaceWindow> source = static_cast<WorkspaceWindow*>(manager_->windowWidget(sourceWindow_));
    if (source) {
        source->surface()->captureStripScrollOffsets();
        source->surface()->clearLiftedTabs();
        source->surface()->setDraggedTab({}); source->surface()->setDraggedFolder({}); source->surface()->clearInsertionPreviews();
        source->surface()->restoreStripScrollOffsets();
    }
    if (capture_ && QWidget::mouseGrabber() == capture_) capture_->releaseMouse();
    if (!self) return false;
    auto* preview = labelPreview_.data(); labelPreview_.clear();
    delete preview;
    if (!self) return false;
    labelPreview_.clear(); labelOnly_ = false;
    if ((wasActive && !active_) || !manager_->model().tab(tab_)) { transferringCapture_ = false; return false; }
    const auto tab = tab_;
    const auto place = [self, global](QWidget* window) {
        if (!self) return;
        auto* workspace = static_cast<WorkspaceWindow*>(window);
        self->windowOffset_ = workspace->mapToGlobal(workspace->captionDragAnchor()) - workspace->pos();
        window->move(global - self->windowOffset_);
    };
    const auto moving = folder_.isNull() ? manager_->detachTabImpl(tab, WindowKind::Floating, place, true)
                                         : manager_->detachFolderImpl(folder_, WindowKind::Floating, place, true);
    if (!self) return false;
    transferringCapture_ = false; movingWindow_ = moving;
    if (movingWindow_.isNull()) return false;
    // From here a folder travels as its window: dropped whole, folder kept.
    if (!folder_.isNull()) { wholeWindow_ = movingWindow_; tab_ = {}; }
    capture_ = manager_->windowWidget(movingWindow_);
    if (!capture_) return false;
    // Showing can finalize the custom frame's margins and toolbar geometry.
    // Re-anchor once before entering the system move loop, using that frame.
    auto* movingWindow = static_cast<WorkspaceWindow*>(capture_.data());
    windowOffset_ = movingWindow->mapToGlobal(movingWindow->captionDragAnchor()) - movingWindow->pos();
    movingWindow->move(global - windowOffset_);
    detached_ = true; normalOffset_ = windowOffset_;
    if (active_) capture_->grabMouse(Qt::ClosedHandCursor);
    return self && capture_;
}
void DragController::beginWindow(WindowId window, QPoint global)
{
    const auto* record = manager_->model().window(window);
    if (hasStableLayout() || !record || record->kind != WindowKind::Floating) return;
    originalLayout_ = manager_->saveLayout();
    const QScopedValueRollback preparing(preparing_, true);
    wholeWindow_ = window; tab_ = TabId{}; folder_ = {}; sourceWindow_ = window; movingWindow_ = window; detached_ = false; labelOnly_ = false;
    liftGrab_ = {};
    capture_ = manager_->windowWidget(window);
    if (!capture_) { originalLayout_ = {}; return; }
    originalGeometry_ = static_cast<WorkspaceWindow*>(capture_.data())->normalPlacementGeometry();
    originalMaximized_ = capture_->isMaximized();
    windowOffset_ = global - capture_->pos();
    if (originalMaximized_) { capture_->showNormal(); windowOffset_ = QPoint(capture_->width() / 2, 16); }
    start(global);
}
void DragController::start(QPoint global)
{
    if (!capture_) return;
    const QPointer<DragController> self(this);
    active_ = true; preparing_ = false; startPosition_ = global;
    ++hoverGeneration_; hoverUpdateQueued_ = false;
    normalOffset_ = windowOffset_;
#ifdef Q_OS_WIN
    {
        const bool left = !GetSystemMetrics(SM_SWAPBUTTON);
        nativeInput_ = GetAsyncKeyState(left ? VK_LBUTTON : VK_RBUTTON) & 0x8000;
    }
#endif
    overlay_ = new DockOverlay; overlay_->whole = !wholeWindow_.isNull();
    overlay_->count = !folder_.isNull() ? manager_->model().folderTabs(folder_).size()
        : wholeWindow_.isNull() ? 1 : manager_->model().orderedTabs(wholeWindow_).size();
    qApp->installEventFilter(this); capture_->grabMouse(Qt::ClosedHandCursor);
    if (self) update(global);
    if (self && active_ && !labelOnly_ && target_.insertionIndex < 0) enterNativeMove();
}
WindowId DragController::candidateAt(QPoint global) const
{
    if (!forcedWindow_.isNull() && forcedWindow_ != movingWindow_) { return forcedWindow_; }
#ifdef Q_OS_WIN
    if (!capture_ || !capture_->windowHandle()) { return {}; }
    // Resolve the same queued event in Qt and native coordinates. Do not sample
    // GetCursorPos, which can already point at a later mouse event.
    const auto local = capture_->windowHandle()->mapFromGlobal(QPointF(global));
    const auto ratio = capture_->devicePixelRatioF();
    POINT nativePoint{qRound(local.x() * ratio), qRound(local.y() * ratio)};
    if (!ClientToScreen(reinterpret_cast<HWND>(capture_->winId()), &nativePoint)) { return {}; }
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
        if (process != GetCurrentProcessId()) { return {}; }
        for (const auto id : manager_->model().windowOrder()) {
            if (id == movingWindow_) continue;
            auto* widget = manager_->windowWidget(id);
            if (widget && reinterpret_cast<HWND>(widget->winId()) == handle &&
                widget->rect().contains(widget->mapFromGlobal(global))) { return id; }
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
    const auto groups = surface->groupRects();
    bool retainedCross = false;
    if (overlay_ && overlay_->isVisible() && target_.window == result.window && groups.contains(target_.group)) {
        QRect cross;
        for (const auto& guide : target_.guides) if (!guide.toEdge) cross = cross.united(guide.rect);
        // The cross can extend over a neighbouring group after avoiding edge
        // targets. Its arms and the gaps between them belong to the same
        // control until the pointer leaves the complete displayed bounds.
        retainedCross = cross.contains(global);
        if (retainedCross) result.group = target_.group;
    }
    if (const auto* group = manager_->model().group(result.group)) result.splitGroup = group->location != GroupLocation::AutoHide;
    result.groupRect = surface->groupRects().value(result.group, surface->centralRectGlobal());
    if (result.groupRect.isEmpty()) result.groupRect = result.windowRect;
    // A displayed side bar is an insertion strip along its full axis, including
    // the area where the outer guide would otherwise overlap that strip.
    const auto edgeBars = surface->edgeBarRects();
    for (auto it = edgeBars.cbegin(); it != edgeBars.cend(); ++it) if (!retainedCross && it.value().contains(global)) {
        result.accepted = true; result.toEdge = true; result.edge = it.key(); result.pinned = false;
        result.toEdgeBar = true;
        result.insertionIndex = surface->edgeTabInsertionIndex(it.key(), global);
        break;
    }
    // A visible tab strip is an insertion target. Content bodies and invisible
    // edge margins deliberately have no implicit drop operation.
    const auto barGroup = surface->tabBarAt(global);
    if (!retainedCross && !result.accepted && !barGroup.isNull()) {
        result.accepted = true; result.group = barGroup; result.toEdge = false;
        result.placement = Placement::Center;
        result.groupRect = surface->groupRects().value(barGroup);
        result.insertionIndex = surface->tabInsertionIndex(barGroup, global, carriedKind());
        result.insertionRow = surface->tabInsertionRow(barGroup, global);
    }
    const auto strips = surface->dockStrips();
    // The content geometry already excludes strips on any side. Treating a
    // full-height vertical strip as a top header reduces the guide area to
    // the footer and incorrectly shrinks and lowers the group cross.
    auto guideArea = surface->groupContentRect(result.group);
    if (guideArea.isEmpty()) guideArea = result.groupRect;
    result.guides = dockGuides(result.windowRect, guideArea, result.splitGroup, strips, edgeBars, DockGuideArea::Content);
    for (const auto& guide : result.guides) if (!result.accepted && guide.rect.contains(global)) {
        result.accepted = true; result.toEdge = guide.toEdge; result.pinned = guide.pinned;
        result.edge = guide.edge; result.placement = guide.placement; break;
    }
    if (!result.accepted) return result;
    result.preview = result.groupRect;
    if (result.toEdge) {
        const auto bounds = result.windowRect;
        const int extent = result.pinned ? 160 : manager_->interfaceTheme().metrics.edgeThickness;
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
    const QPointer<DragController> self(this);
    lastPosition_ = global;
    if (labelOnly_) {
        if (updateSourceStrip(global)) return;
        if (!detach(global)) { if (self) cancel(); return; }
        if (!self || !active_) return;
        // Over the desktop the system moves the new window from here on.
        QTimer::singleShot(0, this, [self] { if (self && self->active_ && self->target_.insertionIndex < 0) self->enterNativeMove(); });
    }
    target_ = hit(global);
    const bool insertion = target_.accepted && target_.insertionIndex >= 0;
    qint64 carriedExtent = 0;
    const auto tabs = tab_.isNull() ? manager_->model().orderedTabs(movingWindow_) : QVector<TabId>{tab_};
    if (const auto* moving = dynamic_cast<WorkspaceWindow*>(capture_.data()))
        for (const auto id : tabs) carriedExtent += moving->surface()->dragLabelExtent(id, target_.toEdgeBar);
    const int extent = int(std::clamp<qint64>(carriedExtent, 1, QWIDGETSIZE_MAX));
    for (const auto id : manager_->model().windowOrder()) {
        auto* window = static_cast<WorkspaceWindow*>(manager_->windowWidget(id));
        if (!window) continue;
        auto* surface = window->surface();
        if (id == target_.window && insertion) {
            if (target_.toEdgeBar) surface->setEdgeInsertionPreview(target_.edge, target_.insertionIndex, extent);
            else {
                // Stickiness is the tab's before the drag; a tear-out window does not change it.
                surface->setInsertionPreview(target_.group, target_.insertionIndex, carriedLabels(),
                    !tab_.isNull() && originalModel_.isTabSticky(tab_), carriedKind());
                target_.insertionSticky = surface->insertionSticky(target_.group);
                target_.insertionFolder = surface->insertionFolder(target_.group);
            }
            target_.preview = target_.toEdgeBar ? surface->edgeInsertionPreviewRect(target_.edge)
                : surface->insertionPreviewRect(target_.group);
        } else surface->clearInsertionPreviews();
        surface->setDragHover(id == target_.window ? global : QPoint(-100000, -100000));
        if (id != target_.window || !insertion || target_.toEdgeBar) surface->setIncomingVisual({}, {});
    }
    if (QPointer<WorkspaceWindow> moving = dynamic_cast<WorkspaceWindow*>(capture_.data())) {
        const auto presentation = !insertion ? WorkspaceWindow::DragPresentation::Window
            : !target_.toEdgeBar ? WorkspaceWindow::DragPresentation::Tab
            : WorkspaceWindow::DragPresentation(int(WorkspaceWindow::DragPresentation::Left) + int(target_.edge));
        // Over a strip the drag places the window itself.
        if (insertion && nativeMoving_) leaveNativeMove();
        // Do not change the HWND's frame or region inside Windows' still
        // unwinding move loop. It would replay the old window rectangle over
        // the compact shape, leaving stale borders and a displaced anchor.
        if (leavingNativeMove_) return;
        // The target group shows the content as if its tab were already
        // there; the picture is taken before the window shrinks to its tabs.
        if (presentation == WorkspaceWindow::DragPresentation::Tab && moving->dragPresentation() == WorkspaceWindow::DragPresentation::Window)
            contentPicture_ = moving->surface()->activeContentPicture();
        const auto previewGroup = presentation == WorkspaceWindow::DragPresentation::Tab ? target_.group : GroupId{};
        if (previewGroup != previewGroup_ || target_.window != previewWindow_) {
            if (auto* old = static_cast<WorkspaceWindow*>(manager_->windowWidget(previewWindow_))) old->surface()->setContentPreview({}, {});
            previewGroup_ = previewGroup; previewWindow_ = previewGroup.isNull() ? WindowId{} : target_.window;
            if (auto* now = static_cast<WorkspaceWindow*>(manager_->windowWidget(previewWindow_))) now->surface()->setContentPreview(previewGroup_, contentPicture_);
        }
        if (moving->dragPresentation() != presentation) {
            moving->setDragPresentation(presentation);
            if (!self || !active_ || !moving) return;
            if (presentation == WorkspaceWindow::DragPresentation::Tab)
                if (auto* targetWindow = dynamic_cast<WorkspaceWindow*>(manager_->windowWidget(target_.window)))
                    moving->setDragTabStyle(*targetWindow, target_.group, target_.insertionSticky);
            if (!self || !active_ || !moving) return;
            // QWidget::move positions a top-level frame; the label anchor is
            // in client coordinates. Native frame margins can change while
            // the window switches between its normal and compact shapes.
            if (presentation != WorkspaceWindow::DragPresentation::Tab) {
                windowOffset_ = presentation == WorkspaceWindow::DragPresentation::Window ? normalOffset_
                    : moving->mapToGlobal(moving->dragLabelAnchor()) - moving->pos();
                if (!nativeMoving_) moving->move(global - windowOffset_);
            }
            // Back over the desktop, the system moves it again.
            if (!insertion) QTimer::singleShot(0, this, [self] { if (self && self->active_ && self->target_.insertionIndex < 0) self->enterNativeMove(); });
        } else if (!nativeMoving_) {
            if (presentation == WorkspaceWindow::DragPresentation::Tab)
                if (auto* targetWindow = dynamic_cast<WorkspaceWindow*>(manager_->windowWidget(target_.window)))
                    moving->setDragTabStyle(*targetWindow, target_.group, target_.insertionSticky);
            if (!self || !active_ || !moving) return;
            if (presentation != WorkspaceWindow::DragPresentation::Tab) {
                if (insertion) windowOffset_ = moving->mapToGlobal(moving->dragLabelAnchor()) - moving->pos();
                moving->move(global - windowOffset_);
            }
        }
        if (presentation == WorkspaceWindow::DragPresentation::Tab) {
            if (auto* targetWindow = dynamic_cast<WorkspaceWindow*>(manager_->windowWidget(target_.window))) {
                auto visual = moving->surface()->dragLabelVisual();
                if (liftGrab_.sourceSize.isValid()) visual.sourceAnchor = liftGrab_;
                if (visual.paint) {
                    targetWindow->surface()->setIncomingVisual(target_.group, std::move(visual), global);
                    if (!self || !active_ || !moving) return;
                    moving->setDragLabelHidden(true);
                }
            }
        } else moving->setDragLabelHidden(false);
        if (!self || !active_) return;
    }
    if (manager_->behavior().revealDuringDrag && target_.window != revealCandidate_) {
        revealTimer_.stop(); revealCandidate_ = target_.window;
        if (!revealCandidate_.isNull() && revealCandidate_ != movingWindow_ && revealCandidate_ != revealedWindow_)
            revealTimer_.start(manager_->behavior().revealDelayMs);
    }
    if (target_.window.isNull() || insertion) { if (overlay_) overlay_->hide(); return; }
    auto* window = manager_->windowWidget(target_.window);
    overlay_->target = target_;
    overlay_->setGeometry(QRect(window->mapToGlobal(QPoint()), window->size()));
    overlay_->show(); overlay_->raise(); overlay_->update();
}
QVector<CarriedLabel> DragController::carriedLabels() const
{
    QVector<CarriedLabel> labels;
    auto tabs = tab_.isNull() ? manager_->model().orderedTabs(movingWindow_) : QVector<TabId>{tab_};
    if (const auto* folder = manager_->model().folder(folder_)) {
        // A folder: its header, then its tabs while open.
        tabs = manager_->model().folderTabs(folder_);
        labels.append({folder->name, false, true, folder->collapsed, int(tabs.size())});
        if (folder->collapsed) return labels;
    }
    for (const auto id : tabs) if (const auto* record = manager_->model().tab(id))
        labels.append({record->title, manager_->tabOptions(id).showIcon});
    return labels;
}
TabLayoutInput::Carried DragController::carriedKind() const
{
    if (!folder_.isNull()) return TabLayoutInput::Carried::Folder;
    return tab_.isNull() ? TabLayoutInput::Carried::Tabs : TabLayoutInput::Carried::Tab;
}
bool DragController::updateSourceStrip(QPoint global)
{
    auto* window = static_cast<WorkspaceWindow*>(manager_->windowWidget(sourceWindow_));
    if (!window || !manager_->model().tab(tab_)) return false;
    auto* surface = window->surface();
    const auto strip = sourceIsEdge_ ? surface->edgeBarRects().value(sourceEdge_)
        : surface->tabBarRects().value(sourceGroup_);
    // A lifted tab tears off only some way past its strip (tab strip design
    // 6.2): a tab height across a horizontal strip, 24 beside a vertical one.
    const bool column = strip.height() > strip.width();
    const auto zone = sourceIsEdge_ ? strip : column ? strip.adjusted(-24, 0, 24, 0) : strip.adjusted(0, -28, 0, 28);
    if (!zone.contains(global)) return false;
    if (!sourceIsEdge_) surface->setLiftedTab(sourceGroup_, global, liftGrab_);
    target_ = {}; target_.window = sourceWindow_; target_.group = sourceGroup_;
    target_.accepted = true; target_.toEdge = sourceIsEdge_; target_.toEdgeBar = sourceIsEdge_;
    target_.edge = sourceEdge_;
    target_.insertionIndex = sourceIsEdge_ ? surface->edgeTabInsertionIndex(sourceEdge_, global)
        : surface->tabInsertionIndex(sourceGroup_, global, carriedKind());
    const int extent = surface->dragLabelExtent(tab_, sourceIsEdge_);
    if (sourceIsEdge_) surface->setEdgeInsertionPreview(sourceEdge_, target_.insertionIndex, extent);
    else {
        target_.insertionRow = surface->tabInsertionRow(sourceGroup_, global);
        surface->setInsertionPreview(sourceGroup_, target_.insertionIndex, carriedLabels(), originalModel_.isTabSticky(tab_), carriedKind());
        target_.insertionSticky = surface->insertionSticky(sourceGroup_);
        target_.insertionFolder = surface->insertionFolder(sourceGroup_);
    }
    surface->setDragHover(global);
    if (labelPreview_) labelPreview_->follow(global);
    if (overlay_) overlay_->hide();
    return true;
}
void DragController::refreshHover()
{
    if (!active_ || hoverUpdateQueued_) return;
    hoverUpdateQueued_ = true;
    const auto generation = hoverGeneration_;
    QTimer::singleShot(0, this, [this, generation] {
        if (generation != hoverGeneration_) return;
        hoverUpdateQueued_ = false;
        if (active_ && !choosing_) update(lastPosition_);
    });
}
bool DragController::belongsToDropWindow(QObject* object) const
{
    auto* widget = qobject_cast<QWidget*>(object);
    if (!widget) {
        if (auto* layout = qobject_cast<QLayout*>(object)) widget = layout->parentWidget();
    }
    if (widget) {
        const auto* window = widget->window();
        if ((!labelOnly_ && window == capture_.data()) || window == overlay_.data() || window == labelPreview_.data()) return false;
        const auto id = window->property("windowId").toUuid();
        return !id.isNull() && id != movingWindow_ && manager_->windowWidget(id) == window;
    }
    const auto* native = qobject_cast<QWindow*>(object);
    if (!native || (!labelOnly_ && capture_ && native == capture_->windowHandle())
        || (overlay_ && native == overlay_->windowHandle())) return false;
    for (const auto id : manager_->model().windowOrder()) if (id != movingWindow_) {
        const auto* window = manager_->windowWidget(id);
        if (window && window->windowHandle() == native) return true;
    }
    return false;
}
void DragController::finish(QPoint global)
{
    if (!active_) return;
    const QPointer<DragController> self(this);
    update(global);
    if (!self || !active_) return;
    const auto target = target_; const auto tab = tab_; const auto whole = wholeWindow_;
    const auto moving = movingWindow_; const auto source = sourceWindow_;
    const auto folder = folder_; const bool detachedFolder = !folder_.isNull() && detached_;
    const QPointer<WorkspaceWindow> carrier = dynamic_cast<WorkspaceWindow*>(capture_.data());
    const QPointer<WorkspaceWindow> dropWindow = dynamic_cast<WorkspaceWindow*>(manager_->windowWidget(target.window));
    const auto dropPositions = target.accepted && target.insertionIndex >= 0 && !target.toEdgeBar && dropWindow
        ? dropWindow->surface()->captureDropPositions(target.group) : LabelPositions{};
    const bool embeddedDrop = target.accepted && target.insertionIndex >= 0 && carrier
        && carrier->dragPresentation() == WorkspaceWindow::DragPresentation::Tab;
    if (embeddedDrop) setNativeDropCommit(carrier, true);
    finalizing_ = true;
    // An embedded carrier must not flash its full chrome before it vanishes.
    stop(!embeddedDrop);
    if (!self) return;
    bool success = !target.accepted;
    if (target.accepted && !folder.isNull() && !detachedFolder) {
        // Still in its strip: the folder moves among the strip's items.
        success = target.insertionIndex < 0 || manager_->moveTabFolderToGroup(folder, target.group, target.insertionIndex);
    } else if (target.accepted) {
        if (target.toEdgeBar) {
            if (!tab.isNull()) success = manager_->execute("workspace.moveTabToEdgeBar",
                {{"tab", tab}, {"targetWindow", target.window}, {"edge", int(target.edge)}, {"index", target.insertionIndex}}, moving.isNull() ? source : moving);
            else success = manager_->execute("window.moveAllToEdgeBar",
                {{"source", whole}, {"targetWindow", target.window}, {"edge", int(target.edge)}, {"index", target.insertionIndex}}, source);
        } else if (target.toEdge) {
            if (!tab.isNull()) success = manager_->execute("workspace.moveTabToEdge",
                {{"tab", tab}, {"targetWindow", target.window}, {"edge", int(target.edge)}, {"pinned", target.pinned}}, moving);
            else success = manager_->execute("window.moveAllToEdge",
                {{"source", whole}, {"targetWindow", target.window}, {"edge", int(target.edge)}, {"pinned", target.pinned}}, source);
        } else if (!whole.isNull()) {
            QVariantMap args{{"source", whole}, {"targetWindow", target.window}, {"group", target.group},
                             {"placement", int(target.placement)}, {"index", target.insertionIndex}};
            // The drop position decides stickiness for the whole window too;
            // a carried folder is never sticky.
            if (target.insertionIndex >= 0) args.insert("sticky", target.insertionSticky && folder.isNull());
            success = manager_->execute("window.moveAllTabs", args, source);
        }
        else if (target.group.isNull()) success = manager_->execute("workspace.moveTabToWindow",
            {{"tab", tab}, {"targetWindow", target.window}, {"placement", int(target.placement)}}, moving);
        else {
            // The drop follows the preview, which decides stickiness by where
            // the strip showed the gap, and the folder it joins.
            const bool inserting = target.insertionIndex >= 0;
            success = manager_->moveTab(tab, target.group, target.placement, target.insertionIndex,
                                        inserting ? std::optional<bool>(target.insertionSticky) : std::nullopt,
                                        inserting ? std::optional<FolderId>(target.insertionFolder) : std::nullopt);
        }
    } else if (detached_ && !tab.isNull() && manager_->behavior().dragOutsideKind == WindowKind::Workbench)
        success = manager_->promoteWindow(moving);
    if (!self) return;
    if (embeddedDrop && carrier && manager_->model().window(moving)) {
        // A rejected move leaves the real window intact. Restore both its
        // shape and its ordinary animation policy, under the drop guard.
        carrier->setDragPresentation(WorkspaceWindow::DragPresentation::Window);
        if (!self) return;
        if (carrier) carrier->move(global - normalOffset_);
        if (!self) return;
        if (carrier) { carrier->setDragLabelHidden(false); setNativeDropCommit(carrier, false); }
    }
    if (success && target.accepted) {
        manager_->showWindow(target.window);
        if (!self) return;
        if (!tab.isNull()) manager_->activateTab(tab);
    }
    if (!self) return;
    if (embeddedDrop) for (const auto id : manager_->model().windowOrder()) {
        // Keep the target's picture until the real page is mounted and
        // activated; clearing it in stop() briefly exposes the old page.
        if (auto* window = static_cast<WorkspaceWindow*>(manager_->windowWidget(id))) {
            window->surface()->setContentPreview({}, {});
            window->surface()->setIncomingVisual({}, {});
        }
        if (!self) return;
    }
    folder_ = {};
    if (!self) return;
    // A failed or absent target leaves the real floating window and live content
    // intact. No second tear-out is created when the button is released.
    originalModel_ = {}; originalOptions_.clear(); originalWindowVisibility_.clear(); originalVisibilityKinds_.clear();
    originalWindows_.clear(); originalMaximizedWindows_.clear();
    originalSourceStrips_ = {};
    originalSourceInactiveStrips_ = {};
    detached_ = false;
    completeLayoutTransaction();
    if (self && success && dropWindow) dropWindow->surface()->settleDroppedTabs(target.group, dropPositions);
}
void DragController::cancel()
{
    if (!active_) return;
    const QPointer<DragController> self(this);
    const bool detached = detached_;
    const auto window = wholeWindow_; const auto geometry = originalGeometry_; const bool maximized = originalMaximized_;
    finalizing_ = true;
    stop();
    if (!self) return;
    if (detached) {
        manager_->d->model = originalModel_; manager_->d->options = originalOptions_;
        manager_->d->windowVisibility = originalWindowVisibility_; manager_->d->visibilityKinds = originalVisibilityKinds_;
        manager_->refresh();
        if (!self) return;
        if (auto* source = static_cast<WorkspaceWindow*>(manager_->windowWidget(sourceWindow_)))
            source->restoreStripLayouts(originalSourceInactiveStrips_, originalSourceStrips_);
        if (!self) return;
        for (auto it = originalWindows_.cbegin(); it != originalWindows_.cend(); ++it) if (auto* widget = manager_->windowWidget(it.key())) {
            static_cast<WorkspaceWindow*>(widget)->restorePlacement(it.value(), originalMaximizedWindows_.contains(it.key()));
            if (!self) return;
        }
        manager_->showWindow(sourceWindow_);
        if (!self) return;
        manager_->activateTab(tab_);
        if (!self) return;
        if (auto* source = static_cast<WorkspaceWindow*>(manager_->windowWidget(sourceWindow_)))
            source->surface()->restoreStripScrollOffsets();
    } else if (auto* widget = manager_->windowWidget(window)) {
        static_cast<WorkspaceWindow*>(widget)->restorePlacement(geometry, maximized);
        if (!self) return;
    }
    originalModel_ = {}; originalOptions_.clear(); originalWindowVisibility_.clear(); originalVisibilityKinds_.clear();
    originalWindows_.clear(); originalMaximizedWindows_.clear(); detached_ = false; folder_ = {};
    originalSourceStrips_ = {};
    originalSourceInactiveStrips_ = {};
    completeLayoutTransaction();
}
void DragController::completeLayoutTransaction()
{
    finalizing_ = false; originalLayout_ = {};
    // A synchronous host saver sees the final model only after every view and
    // placement has been restored or committed, including focus callbacks.
    emit manager_->layoutChanged();
}
void DragController::releaseForMutation()
{
    if (!active_) return;
    const QPointer<DragController> self(this);
    stop();
    if (!self) return;
    detached_ = false;
    originalModel_ = {}; originalOptions_.clear(); originalWindowVisibility_.clear(); originalVisibilityKinds_.clear();
    originalWindows_.clear(); originalMaximizedWindows_.clear();
    originalSourceStrips_ = {};
    originalSourceInactiveStrips_ = {};
}
void DragController::stop(bool restorePresentation)
{
    const QPointer<DragController> self(this);
    // Invalidate a queued end from this gesture before a new one can start.
    ++nativeMoveGeneration_;
    active_ = false;
#ifdef Q_OS_WIN
    // A drag ended from elsewhere (closing, a layout change) also ends the
    // system's move loop.
    if (nativeMoving_ || leavingNativeMove_) { nativeMoving_ = false; transferringCapture_ = true; ReleaseCapture(); }
#endif
    leavingNativeMove_ = false; transferringCapture_ = false;
    nativeMoveStarted_ = false; nativeReleaseQueued_ = false;
    if (nativeWatcher_) { qApp->removeNativeEventFilter(nativeWatcher_); delete nativeWatcher_; nativeWatcher_ = nullptr; }
    nativeInput_ = false;
    contentPicture_ = {}; previewGroup_ = {}; previewWindow_ = {};
    revealTimer_.stop(); active_ = false; qApp->removeEventFilter(this);
    ++hoverGeneration_; hoverUpdateQueued_ = false;
    if (!finalizing_) originalLayout_ = {};
    if (restorePresentation) if (auto* moving = dynamic_cast<WorkspaceWindow*>(capture_.data())) {
        moving->setDragPresentation(WorkspaceWindow::DragPresentation::Window);
        if (!self) return;
        if (capture_) static_cast<WorkspaceWindow*>(capture_.data())->setDragLabelHidden(false);
    }
    if (!self) return;
    if (capture_ && QWidget::mouseGrabber() == capture_) capture_->releaseMouse();
    if (!self) return;
    capture_.clear();
    auto* preview = labelPreview_.data(); labelPreview_.clear();
    delete preview;
    if (!self) return;
    labelPreview_.clear(); labelOnly_ = false;
    auto* overlay = overlay_.data(); overlay_.clear();
    delete overlay;
    if (!self) return;
    overlay_.clear(); revealCandidate_ = WindowId{}; revealedWindow_ = WindowId{}; forcedWindow_ = WindowId{};
    for (const auto id : manager_->model().windowOrder()) {
        auto* window = static_cast<WorkspaceWindow*>(manager_->windowWidget(id));
        if (window) {
            window->surface()->captureStripScrollOffsets();
            window->surface()->clearLiftedTabs();
            if (restorePresentation) {
                window->surface()->setContentPreview({}, {});
                window->surface()->setIncomingVisual({}, {});
            }
            window->surface()->setDraggedTab({});
            window->surface()->setDraggedFolder({});
            window->surface()->clearInsertionPreviews();
            window->surface()->restoreStripScrollOffsets();
            window->surface()->clearDragHover(QCursor::pos());
        }
    }
}
void DragController::selectTarget()
{
    if (!active_ || choosing_) return;
    const QPointer<DragController> self(this);
    if (labelOnly_ && !detach(lastPosition_)) { if (self) cancel(); return; }
    if (!self || !active_) return;
    choosing_ = true;
    QPointer<QMenu> menu = new DismissOnlyMenu(capture_); applyTheme(menu, manager_->interfaceTheme());
    for (const auto id : manager_->model().windowOrder()) {
        if (id == movingWindow_) continue;
        if (auto* window = manager_->windowWidget(id)) {
            auto* action = menu->addAction(window->windowTitle()); action->setData(id);
        }
    }
    auto* action = menu->exec(lastPosition_);
    if (!self) { if (menu) delete menu; return; }
    if (menu && action && active_) {
        forcedWindow_ = action->data().toUuid();
        if (auto* widget = manager_->windowWidget(forcedWindow_)) {
            if (widget->isMinimized()) widget->showNormal();
            widget->raise();
        }
    }
    if (menu) delete menu;
    if (!self) return;
    choosing_ = false;
    if (capture_ && active_) { capture_->grabMouse(Qt::ClosedHandCursor); if (self) update(lastPosition_); }
}
bool DragController::eventFilter(QObject* watched, QEvent* event)
{
    if (!active_ || choosing_) return false;
    // State changes arrive before their final geometry/layout in some native
    // transitions. Recompute from the last queued mouse position after those
    // notifications, while keeping this drag's carrier movement out of the
    // invalidation path. A stationary pointer must not leave old guides alive.
    if (affectsDockGeometry(event->type()) && belongsToDropWindow(watched)) refreshHover();
    if (event->type() == QEvent::MouseMove) {
        const auto position = static_cast<QMouseEvent*>(event)->globalPosition().toPoint();
        update(position); return true;
    }
    if (event->type() == QEvent::Wheel) {
        const QPointer<DragController> self(this);
        const auto* wheel = static_cast<QWheelEvent*>(event);
        const auto global = wheel->globalPosition().toPoint();
        const auto delta = wheel->pixelDelta().isNull() ? wheel->angleDelta() / 3 : wheel->pixelDelta();
        // Mouse capture belongs to the source/carrier. Route scrolling to the
        // actual strip under the pointer without dispatching another event.
        const auto targetWindow = labelOnly_ ? sourceWindow_ : candidateAt(global);
        auto* window = static_cast<WorkspaceWindow*>(manager_->windowWidget(targetWindow));
        if (window && window->surface()->scrollStripAt(global, delta, wheel->pixelDelta().isNull())) {
            event->accept(); if (self && active_) update(global); return true;
        }
        return false;
    }
    if (event->type() == QEvent::MouseButtonRelease && static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
        finish(static_cast<QMouseEvent*>(event)->globalPosition().toPoint()); return true;
    }
    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape) cancel(); else if (key->key() == Qt::Key_Tab) selectTarget();
        return true;
    }
    // The system's move loop holds the mouse; losing Qt's grab then is expected.
    if (!nativeMoving_ && (event->type() == QEvent::ApplicationDeactivate ||
        (event->type() == QEvent::UngrabMouse && watched == capture_ && !transferringCapture_))) cancel();
    return false;
}
} // namespace window_system
