// SPDX-License-Identifier: MIT
#include "tab_drag.hpp"
#include "dock_surface_item.hpp"
#include "group_item.hpp"
#include "rail_item.hpp"
#include "window_chrome_item.hpp"
#include "platform/window_order.hpp"
#include <window_system/window_manager.hpp>

#include <QCursor>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQuickWindow>
#include <QTimer>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace window_system::quick {

namespace {
constexpr qreal overRailOpacity = 0.4;

// The real primary button, which only real input holds.
bool buttonHeld()
{
#ifdef Q_OS_WIN
    return GetAsyncKeyState(GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON) & 0x8000;
#else
    return false;
#endif
}
} // namespace

TabDrag::TabDrag(WindowManager& manager, SurfaceLookup surfaces, Realize realize, QObject* parent)
    : QObject(parent), manager_(manager), surfaces_(std::move(surfaces)), realize_(std::move(realize)) {}

TabDrag::~TabDrag() { if (active_) stop(); }

void TabDrag::beginTab(WindowId window, GroupId group, TabId tab, const DragStart& start)
{
    if (active_ || !manager_.model().tab(tab)) return;
    tab_ = tab; folder_ = {};
    sticky_ = manager_.isTabSticky(tab);
    begin(window, group, start);
}

void TabDrag::beginRailTab(WindowId window, Edge edge, TabId tab, const DragStart& start)
{
    auto* surface = surfaces_(window);
    auto* rail = surface ? surface->rail(edge) : nullptr;
    if (active_ || !rail || !manager_.model().tab(tab)) return;
    tab_ = tab; folder_ = {}; sticky_ = false;
    sourceWindow_ = window; sourceGroup_ = manager_.model().groupForTab(tab); sourceRail_ = edge;
    sourceIndex_ = int(manager_.model().autoHiddenTabs(window, edge).indexOf(tab));
    detached_ = false; movingWindow_ = {}; moving_ = nullptr;
    grab_ = start.anchor;
    rail->setDraggedTab(tab);
    track(start.currentGlobal);
}

void TabDrag::beginFolder(WindowId window, GroupId group, FolderId folder, const DragStart& start)
{
    if (active_ || !manager_.model().folder(folder)) return;
    tab_ = {}; folder_ = folder;
    sticky_ = false;
    begin(window, group, start);
}

void TabDrag::begin(WindowId window, GroupId group, const DragStart& start)
{
    auto* surface = surfaces_(window);
    auto* view = surface ? surface->group(group) : nullptr;
    if (!view) return;
    sourceWindow_ = window; sourceGroup_ = group; sourceRail_.reset();
    // A folder's place is its first tab's.
    const auto first = tab_.isNull() ? manager_.model().folderTabs(folder_).value(0) : tab_;
    sourceIndex_ = int(manager_.model().group(group)->tabs.indexOf(first));
    detached_ = false; movingWindow_ = {}; moving_ = nullptr;
    grab_ = start.anchor;
    if (!grab_.sourceSize.isValid())
        grab_ = folder_.isNull() ? view->tabGrabAnchor(tab_, start.pressGlobal) : view->folderGrabAnchor(folder_, start.pressGlobal);
    if (folder_.isNull()) surface->setDraggedTab(tab_); else surface->setDraggedFolder(folder_);
    track(start.currentGlobal);
}

// As the Widgets drag: the window keeps its place under the pointer, or a
// maximized one restores under it.
void TabDrag::beginWindow(WindowId window, QPoint pressGlobal)
{
    const auto* record = manager_.model().window(window);
    auto* surface = surfaces_(window);
    auto* quickWindow = surface ? surface->window() : nullptr;
    if (active_ || !record || record->kind != WindowKind::Floating || !quickWindow) return;
    tab_ = {}; folder_ = {}; sticky_ = false;
    sourceWindow_ = window; sourceGroup_ = {}; sourceIndex_ = -1; sourceRail_.reset();
    detached_ = true; movingWindow_ = window; moving_ = quickWindow;
    grab_ = {};
    originalPosition_ = quickWindow->position();
    originalMaximized_ = quickWindow->windowStates() & Qt::WindowMaximized;
    offset_ = pressGlobal - quickWindow->position();
    if (originalMaximized_) {
        quickWindow->showNormal();
        offset_ = QPoint(quickWindow->width() / 2, WindowChromeItem::rowHeight() / 2);
    }
    quickWindow->setMouseGrabEnabled(true);
    track(pressGlobal);
    queueSystemMove();
}

void TabDrag::track(QPoint global)
{
    active_ = true; lifted_ = false; target_ = {};
    realInput_ = buttonHeld();
    qApp->installEventFilter(this);
    QGuiApplication::setOverrideCursor(Qt::ClosedHandCursor);
    update(global);
}

// Entered on the next event-loop turn, outside the handler of the press or
// move that started it.
void TabDrag::queueSystemMove()
{
    QTimer::singleShot(0, this, [this, generation = moveGeneration_] {
        if (generation == moveGeneration_) enterSystemMove();
    });
}

void TabDrag::enterSystemMove()
{
    if (!active_ || !detached_ || !moving_ || systemMoving_ || leavingSystemMove_ || !realInput_ || !buttonHeld()) return;
    // The loop keeps the window where it is relative to the pointer, which
    // may have moved on since the last event.
    moving_->setPosition(QCursor::pos() - offset_);
    moving_->setMouseGrabEnabled(false);
    movingHandle_ = quintptr(moving_->winId());
    qApp->installNativeEventFilter(this);
    systemMoving_ = moving_->startSystemMove();
    if (systemMoving_) return;
    qApp->removeNativeEventFilter(this);
    moving_->setMouseGrabEnabled(true);
}

// As the Widgets drag: the system checks for losing the capture after a
// message returns, so the loop ends by a release once it holds the capture.
// Windows enters the loop before it takes the capture, so the release is
// retried as the loop reports.
void TabDrag::leaveSystemMove()
{
    leavingSystemMove_ = true;
    releaseSystemMove();
}

void TabDrag::releaseSystemMove()
{
#ifdef Q_OS_WIN
    QTimer::singleShot(0, this, [this, generation = moveGeneration_] {
        if (generation == moveGeneration_ && leavingSystemMove_ && GetCapture() == reinterpret_cast<HWND>(movingHandle_))
            ReleaseCapture();
    });
#endif
}

void TabDrag::systemMoveEnded(quint64 generation)
{
    if (generation != moveGeneration_ || !active_) return;
    systemMoving_ = false;
    qApp->removeNativeEventFilter(this);
    const bool handingBack = std::exchange(leavingSystemMove_, false);
    if (!buttonHeld()) return finish(QCursor::pos());
    // Still held: over a strip the drag takes the mouse back; otherwise
    // Escape ended the loop.
    if (!handingBack) return cancel();
    if (moving_) moving_->setMouseGrabEnabled(true);
    update(QCursor::pos());
}

bool TabDrag::nativeEventFilter(const QByteArray&, void* message, qintptr*)
{
#ifdef Q_OS_WIN
    const auto* msg = static_cast<const MSG*>(message);
    if (!systemMoving_ || quintptr(msg->hwnd) != movingHandle_) return false;
    if (leavingSystemMove_) {
        if (msg->message == WM_ENTERSIZEMOVE || msg->message == WM_MOVING || msg->message == WM_MOVE) releaseSystemMove();
    }
    if (leavingSystemMove_ && msg->message != WM_EXITSIZEMOVE) return false;
    if (msg->message == WM_MOVING || msg->message == WM_MOVE) {
        // Targets and their presentation change outside the system's handler.
        if (std::exchange(updateQueued_, true)) return false;
        QTimer::singleShot(0, this, [this, generation = moveGeneration_] {
            updateQueued_ = false;
            if (generation == moveGeneration_ && active_) update(QCursor::pos());
        });
    } else if (msg->message == WM_EXITSIZEMOVE) {
        QTimer::singleShot(0, this, [this, generation = moveGeneration_] { systemMoveEnded(generation); });
    }
#else
    Q_UNUSED(message);
#endif
    return false;
}

void TabDrag::cancel()
{
    if (!active_) return;
    const bool detached = detached_;
    const auto tab = tab_;
    stop();
    if (detached && carried() == TabLayoutInput::Carried::Tabs) {
        // A carried window goes back where it was.
        if (!moving_) return;
        if (originalMaximized_) moving_->showMaximized(); else moving_->setPosition(originalPosition_);
        return;
    }
    if (!detached || !carriedExists()) return;
    // The torn-out window closes once its tab or folder is back.
    const bool group = manager_.model().group(sourceGroup_) != nullptr;
    if (!group && !manager_.model().window(sourceWindow_)) return;
    if (!folder_.isNull()) {
        if (group) manager_.moveTabFolderToGroup(folder_, sourceGroup_, sourceIndex_);
        else manager_.moveTabFolderToWindow(folder_, sourceWindow_);
        return;
    }
    // A rail's tab goes back to its rail, whose group may be gone.
    if (sourceRail_) manager_.moveTabToEdgeBar(tab, sourceWindow_, *sourceRail_, sourceIndex_);
    else if (group) manager_.moveTab(tab, sourceGroup_, Placement::Center, sourceIndex_, sticky_);
    else manager_.moveTabToWindow(tab, sourceWindow_);
    manager_.showWindow(sourceWindow_);
    manager_.activateTab(tab);
}

void TabDrag::stop(bool restoreCarrier)
{
    active_ = false;
    ++moveGeneration_;
    updateQueued_ = false;
    leavingSystemMove_ = false;
#ifdef Q_OS_WIN
    // A drag ended from elsewhere also ends the system's move loop.
    if (std::exchange(systemMoving_, false)) ReleaseCapture();
#endif
    qApp->removeNativeEventFilter(this);
    qApp->removeEventFilter(this);
    QGuiApplication::restoreOverrideCursor();
    for (const auto id : manager_.model().windowOrder())
        if (auto* surface = surfaces_(id)) surface->clearDragPresentation();
    if (moving_) moving_->setMouseGrabEnabled(false);
    // A window dropped into a strip stays hidden until it closes.
    if (restoreCarrier) showCarrier();
    lifted_ = false;
    target_ = {};
}

void TabDrag::showCarrier()
{
    restoreCarrier();
    if (moving_) moving_->setOpacity(1);
}

// As the Widgets drag: the tab or folder moves into a new floating window,
// which then follows the pointer by its title. Nothing else changes in the
// model. A folder still drops only into strips, and keeps its folder.
bool TabDrag::detach(QPoint global)
{
    if (auto* source = surfaces_(sourceWindow_)) source->clearDragPresentation();
    lifted_ = false;
    const auto id = folder_.isNull() ? manager_.detachTab(tab_, WindowKind::Floating)
                                     : manager_.detachTabFolder(folder_, WindowKind::Floating);
    auto* window = id.isNull() ? nullptr : realize_(id);
    if (!window) return false;
    detached_ = true; movingWindow_ = id; moving_ = window;
    offset_ = QPoint(window->width() / 2, WindowChromeItem::rowHeight() / 2);
    window->setPosition(global - offset_);
    window->setMouseGrabEnabled(true);
    queueSystemMove();
    return true;
}

bool TabDrag::carriedExists() const
{
    if (!folder_.isNull()) return manager_.model().folder(folder_) != nullptr;
    return tab_.isNull() ? manager_.model().window(movingWindow_) != nullptr : manager_.model().tab(tab_) != nullptr;
}

// As the Widgets drag: the tab leaves its strip only some way past it, a
// tab height across a horizontal strip and 24 beside a vertical one.
bool TabDrag::overSourceStrip(QPoint global) const
{
    const auto* surface = surfaces_(sourceWindow_);
    const auto strip = surface ? surface->tabBarRects().value(sourceGroup_) : QRect{};
    if (strip.isEmpty()) return false;
    const bool column = strip.height() > strip.width();
    return (column ? strip.adjusted(-24, 0, 24, 0) : strip.adjusted(0, -28, 0, 28)).contains(global);
}

DropTarget TabDrag::hit(QPoint global) const
{
    // The moving window is under the pointer; the target is below it.
    const auto* moving = moving_.data();
    auto* top = stackedWindowAt(global, [this, moving](const QWindow* candidate) {
        return candidate != moving && surfaces_(candidate->property("windowId").toUuid());
    });
    const auto window = top ? top->property("windowId").toUuid() : WindowId{};
    const auto* surface = surfaces_(window);
    if (!surface) return {};
    DropQuery query;
    query.global = global;
    query.carried = carried();
    // The guides on screen keep the pointer while it stays within their cross.
    if (target_.window == window && target_.insertionIndex < 0) query.shown = &target_;
    query.edgeThickness = manager_.interfaceTheme().metrics.edgeThickness;
    auto result = dropTargetAt(*surface, manager_.model(), query);
    if (!result.windowRect.isNull()) result.window = window;
    // A folder moves between strips only; the guides take single tabs.
    if (!folder_.isNull() && (result.insertionIndex < 0 || result.toEdge)) result.accepted = false;
    return result;
}

void TabDrag::update(QPoint global)
{
    if (!carriedExists()) return cancel();
    auto* source = surfaces_(sourceWindow_);
    auto* sourceView = source ? source->group(sourceGroup_) : nullptr;
    auto* sourceRail = source && sourceRail_ ? source->rail(*sourceRail_) : nullptr;
    if (detached_) {
        if (!moving_ || !manager_.model().window(movingWindow_)) return cancel();
        if (!systemMoving_) moving_->setPosition(global - offset_);
        target_ = hit(global);
    } else if (sourceRail_) {
        if (!sourceRail) return cancel();
        // As the Widgets drag: the tab stays on its rail while over it.
        if (sourceRail->viewportRectGlobal().contains(global)) {
            target_ = {};
            target_.window = sourceWindow_; target_.accepted = true;
            target_.toEdge = target_.toEdgeBar = true; target_.edge = *sourceRail_;
            target_.windowRect = source->globalRect();
            target_.insertionIndex = sourceRail->insertionIndex(global);
        } else {
            if (!detach(global)) return cancel();
            target_ = hit(global);
        }
    } else if (!sourceView) {
        return cancel();
    } else if (overSourceStrip(global)) {
        sourceView->setLiftedTab(global, grab_);
        lifted_ = true;
        target_ = {};
        target_.window = sourceWindow_; target_.group = sourceGroup_; target_.accepted = true;
        target_.windowRect = source->globalRect();
        target_.insertionIndex = sourceView->insertionIndex(global, carried());
    } else {
        if (!detach(global)) return cancel();
        target_ = hit(global);
    }
    const bool insertion = target_.accepted && target_.insertionIndex >= 0 && !target_.toEdgeBar;
    const bool railInsertion = target_.accepted && target_.insertionIndex >= 0 && target_.toEdgeBar;
    const auto labels = carriedLabels();
    TabDragVisual incoming;
    for (const auto id : manager_.model().windowOrder()) {
        auto* surface = surfaces_(id);
        if (!surface) continue;
        const bool here = id == target_.window;
        surface->showInsertion(here && insertion ? target_.group : GroupId{}, target_.insertionIndex, labels, sticky_, carried());
        if (here && insertion) if (auto* view = surface->group(target_.group)) {
            target_.insertionSticky = view->insertionSticky();
            target_.insertionFolder = view->insertionFolder();
            target_.preview = view->insertionPreviewRectGlobal();
            if (detached_) incoming = carriedLabel(*surface);
        }
        surface->showIncoming(here && !incoming.empty() ? target_.group : GroupId{}, incoming, global);
        const auto rail = here && railInsertion ? std::optional<Edge>(target_.edge) : std::nullopt;
        surface->showRailInsertion(rail, target_.insertionIndex, rail ? surface->railExtent(target_.edge, carriedTabs()) : 0);
        if (rail) target_.preview = surface->railInsertionRectGlobal(target_.edge);
        if (here && !insertion) surface->showGuides(target_, carriedCount(), carried() == TabLayoutInput::Carried::Tabs);
        else surface->clearGuides();
    }
    if (incoming.empty()) restoreCarrier();
    // Over a strip the strip paints the label and the window hides in place,
    // keeping the mouse; elsewhere the system moves the window again.
    // Over a rail the window lets the gap show through, until it takes the
    // shape of a rail button as the Widgets carrier does.
    if (moving_) moving_->setOpacity(!incoming.empty() ? 0 : railInsertion ? overRailOpacity : 1);
    const bool placing = insertion || railInsertion;
    if (placing && systemMoving_) leaveSystemMove();
    else if (!placing && detached_ && !systemMoving_) queueSystemMove();
}

// As the Widgets carrier: the window's strips show only their tabs, in the
// target strip's look, and that picture is what the target paints.
TabDragVisual TabDrag::carriedLabel(DockSurfaceItem& target)
{
    auto* carrier = surfaces_(movingWindow_);
    if (!carrier) return {};
    if (!std::exchange(carrierCompact_, true)) carrier->setDragPresentation(true);
    carrier->setDragTabStyle(target, target_.group, target_.insertionSticky);
    auto visual = carrier->dragLabelVisual();
    if (grab_.sourceSize.isValid()) visual.sourceAnchor = grab_;
    return visual;
}

void TabDrag::restoreCarrier()
{
    if (!std::exchange(carrierCompact_, false)) return;
    if (auto* carrier = surfaces_(movingWindow_)) carrier->setDragPresentation(false);
}

void TabDrag::finish(QPoint global)
{
    update(global);
    if (!active_) return;
    const auto target = target_;
    const auto tab = tab_, folder = folder_;
    // Commands run in the window that holds the tab now.
    const auto source = detached_ ? movingWindow_ : sourceWindow_;
    const bool detached = detached_;
    // A carrier hidden over the strip stays hidden while its tabs move out
    // and the emptied window closes.
    const bool hidden = carrierCompact_;
    stop(!hidden);
    if (!target.accepted) {
        if (detached && !tab.isNull() && manager_.behavior().dragOutsideKind == WindowKind::Workbench) manager_.promoteWindow(source);
        return;
    }
    bool success = false;
    const bool inserting = target.insertionIndex >= 0;
    if (!folder.isNull()) success = manager_.moveTabFolderToGroup(folder, target.group, target.insertionIndex);
    else if (tab.isNull()) {
        // A carried window: all its tabs go.
        if (target.toEdgeBar) success = manager_.execute(QStringLiteral("window.moveAllToEdgeBar"),
            {{"source", source}, {"targetWindow", target.window}, {"edge", int(target.edge)}, {"index", target.insertionIndex}}, source);
        else if (target.toEdge) success = manager_.execute(QStringLiteral("window.moveAllToEdge"),
            {{"source", source}, {"targetWindow", target.window}, {"edge", int(target.edge)}, {"pinned", target.pinned}}, source);
        else {
            QVariantMap args{{"source", source}, {"targetWindow", target.window}, {"group", target.group},
                             {"placement", int(target.placement)}, {"index", target.insertionIndex}};
            if (inserting) args.insert("sticky", target.insertionSticky);
            success = manager_.execute(QStringLiteral("window.moveAllTabs"), args, source);
        }
    }
    else if (target.toEdgeBar)
        success = manager_.execute(QStringLiteral("workspace.moveTabToEdgeBar"),
            {{"tab", tab}, {"targetWindow", target.window}, {"edge", int(target.edge)}, {"index", target.insertionIndex}}, source);
    else if (target.toEdge)
        success = manager_.execute(QStringLiteral("workspace.moveTabToEdge"),
            {{"tab", tab}, {"targetWindow", target.window}, {"edge", int(target.edge)}, {"pinned", target.pinned}}, source);
    else if (target.group.isNull())
        success = manager_.execute(QStringLiteral("workspace.moveTabToWindow"),
            {{"tab", tab}, {"targetWindow", target.window}, {"placement", int(target.placement)}}, source);
    // The drop follows the preview, which decides stickiness by where the
    // strip showed the gap, and the folder it joins.
    else success = manager_.moveTab(tab, target.group, target.placement, target.insertionIndex,
        inserting ? std::optional<bool>(target.insertionSticky) : std::nullopt,
        inserting ? std::optional<FolderId>(target.insertionFolder) : std::nullopt);
    if (hidden && (!success || manager_.model().window(source))) showCarrier();
    carrierCompact_ = false;
    if (!success) return;
    manager_.showWindow(target.window);
    if (!tab.isNull()) manager_.activateTab(tab);
}

bool TabDrag::eventFilter(QObject* watched, QEvent* event)
{
    if (!active_ || !qobject_cast<QWindow*>(watched)) return QObject::eventFilter(watched, event);
    switch (event->type()) {
    case QEvent::MouseMove:
        // Qt places a move through its window as the window is now. A
        // carried window has moved since, so real input reads the pointer.
        update(realInput_ ? QCursor::pos() : static_cast<QMouseEvent*>(event)->globalPosition().toPoint());
        return true;
    case QEvent::MouseButtonRelease:
        // The strip still sees the release and forgets its press.
        if (static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton)
            finish(static_cast<QMouseEvent*>(event)->globalPosition().toPoint());
        return false;
    case QEvent::KeyPress:
        if (static_cast<QKeyEvent*>(event)->key() != Qt::Key_Escape) return true;
        cancel();
        return true;
    default:
        return QObject::eventFilter(watched, event);
    }
}

QVector<CarriedLabel> TabDrag::carriedLabels() const
{
    QVector<CarriedLabel> labels;
    auto tabs = carriedTabs();
    if (const auto* folder = manager_.model().folder(folder_)) {
        // A folder: its header, then its tabs while open.
        labels.append({folder->name, false, true, folder->collapsed, int(tabs.size())});
        if (folder->collapsed) return labels;
    }
    for (const auto id : tabs) if (const auto* record = manager_.model().tab(id))
        labels.append({record->title, manager_.tabOptions(id).showIcon});
    return labels;
}

QVector<TabId> TabDrag::carriedTabs() const
{
    if (!folder_.isNull()) return manager_.model().folderTabs(folder_);
    return tab_.isNull() ? manager_.model().orderedTabs(movingWindow_) : QVector<TabId>{tab_};
}

TabLayoutInput::Carried TabDrag::carried() const
{
    if (!folder_.isNull()) return TabLayoutInput::Carried::Folder;
    return tab_.isNull() ? TabLayoutInput::Carried::Tabs : TabLayoutInput::Carried::Tab;
}

int TabDrag::carriedCount() const
{
    return int(carriedTabs().size());
}

} // namespace window_system::quick
