// SPDX-License-Identifier: MIT
#include "tab_drag.hpp"
#include "dock_surface_item.hpp"
#include "group_item.hpp"
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
// Over a strip the torn-out window lets the strip's gap show through, until
// it turns into a tab as the Widgets carrier does.
constexpr qreal overStripOpacity = 0.4;

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
    sourceWindow_ = window; sourceGroup_ = group;
    sourceIndex_ = tab_.isNull() ? -1 : int(manager_.model().group(group)->tabs.indexOf(tab_));
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
    sourceWindow_ = window; sourceGroup_ = {}; sourceIndex_ = -1;
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
    if (!active_ || !detached_ || !moving_ || systemMoving_ || !realInput_ || !buttonHeld()) return;
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

void TabDrag::systemMoveEnded(quint64 generation)
{
    if (generation != moveGeneration_ || !active_) return;
    systemMoving_ = false;
    qApp->removeNativeEventFilter(this);
    // The loop ends with the button still held only when Escape ended it.
    if (buttonHeld()) cancel();
    else finish(QCursor::pos());
}

bool TabDrag::nativeEventFilter(const QByteArray&, void* message, qintptr*)
{
#ifdef Q_OS_WIN
    const auto* msg = static_cast<const MSG*>(message);
    if (!systemMoving_ || quintptr(msg->hwnd) != movingHandle_) return false;
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
    if (!detached || !manager_.model().tab(tab)) return;
    // The torn-out window closes once its tab is back.
    if (manager_.model().group(sourceGroup_)) manager_.moveTab(tab, sourceGroup_, Placement::Center, sourceIndex_, sticky_);
    else if (manager_.model().window(sourceWindow_)) manager_.moveTabToWindow(tab, sourceWindow_);
    else return;
    manager_.showWindow(sourceWindow_);
    manager_.activateTab(tab);
}

void TabDrag::stop()
{
    active_ = false;
    ++moveGeneration_;
    updateQueued_ = false;
#ifdef Q_OS_WIN
    // A drag ended from elsewhere also ends the system's move loop.
    if (std::exchange(systemMoving_, false)) ReleaseCapture();
#endif
    qApp->removeNativeEventFilter(this);
    qApp->removeEventFilter(this);
    QGuiApplication::restoreOverrideCursor();
    for (const auto id : manager_.model().windowOrder())
        if (auto* surface = surfaces_(id)) surface->clearDragPresentation();
    if (moving_) {
        moving_->setOpacity(1);
        moving_->setMouseGrabEnabled(false);
    }
    lifted_ = false;
    target_ = {};
}

// As the Widgets drag: the tab moves into a new floating window, which then
// follows the pointer by its title. Nothing else changes in the model.
bool TabDrag::detach(QPoint global)
{
    if (auto* source = surfaces_(sourceWindow_)) source->clearDragPresentation();
    lifted_ = false;
    const auto id = manager_.detachTab(tab_, WindowKind::Floating);
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
    if (!folder_.isNull() && result.insertionIndex < 0) result.accepted = false;
    return result;
}

void TabDrag::update(QPoint global)
{
    if (!carriedExists()) return cancel();
    auto* source = surfaces_(sourceWindow_);
    auto* sourceView = source ? source->group(sourceGroup_) : nullptr;
    if (detached_) {
        if (!moving_ || !manager_.model().window(movingWindow_)) return cancel();
        if (!systemMoving_) moving_->setPosition(global - offset_);
        target_ = hit(global);
    } else if (!sourceView) {
        return cancel();
    } else if (overSourceStrip(global)) {
        sourceView->setLiftedTab(global, grab_);
        lifted_ = true;
        target_ = {};
        target_.window = sourceWindow_; target_.group = sourceGroup_; target_.accepted = true;
        target_.windowRect = source->globalRect();
        target_.insertionIndex = sourceView->insertionIndex(global, carried());
    } else if (!tab_.isNull()) {
        if (!detach(global)) return cancel();
        target_ = hit(global);
    } else {
        if (std::exchange(lifted_, false)) sourceView->clearLiftedTab();
        target_ = hit(global);
    }
    const bool insertion = target_.accepted && target_.insertionIndex >= 0 && !target_.toEdgeBar;
    const auto labels = carriedLabels();
    for (const auto id : manager_.model().windowOrder()) {
        auto* surface = surfaces_(id);
        if (!surface) continue;
        const bool here = id == target_.window;
        surface->showInsertion(here && insertion ? target_.group : GroupId{}, target_.insertionIndex, labels, sticky_, carried());
        if (here && insertion) if (auto* view = surface->group(target_.group)) {
            target_.insertionSticky = view->insertionSticky();
            target_.insertionFolder = view->insertionFolder();
            target_.preview = view->insertionPreviewRectGlobal();
        }
        if (here && !insertion) surface->showGuides(target_, carriedCount(), carried() == TabLayoutInput::Carried::Tabs);
        else surface->clearGuides();
    }
    if (moving_) moving_->setOpacity(insertion ? overStripOpacity : 1);
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
    stop();
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
    auto tabs = tab_.isNull() ? manager_.model().orderedTabs(movingWindow_) : QVector<TabId>{tab_};
    if (const auto* folder = manager_.model().folder(folder_)) {
        // A folder: its header, then its tabs while open.
        tabs = manager_.model().folderTabs(folder_);
        labels.append({folder->name, false, true, folder->collapsed, int(tabs.size())});
        if (folder->collapsed) return labels;
    }
    for (const auto id : tabs) if (const auto* record = manager_.model().tab(id))
        labels.append({record->title, manager_.tabOptions(id).showIcon});
    return labels;
}

TabLayoutInput::Carried TabDrag::carried() const
{
    if (!folder_.isNull()) return TabLayoutInput::Carried::Folder;
    return tab_.isNull() ? TabLayoutInput::Carried::Tabs : TabLayoutInput::Carried::Tab;
}

int TabDrag::carriedCount() const
{
    if (!folder_.isNull()) return int(manager_.model().folderTabs(folder_).size());
    return tab_.isNull() ? int(manager_.model().orderedTabs(movingWindow_).size()) : 1;
}

} // namespace window_system::quick
