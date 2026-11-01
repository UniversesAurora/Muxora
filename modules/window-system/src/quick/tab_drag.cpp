// SPDX-License-Identifier: MIT
#include "tab_drag.hpp"
#include "dock_surface_item.hpp"
#include "group_item.hpp"
#include <window_system/window_manager.hpp>

#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPointer>
#include <QWindow>

namespace window_system::quick {

TabDrag::TabDrag(WindowManager& manager, SurfaceLookup surfaces, QObject* parent)
    : QObject(parent), manager_(manager), surfaces_(std::move(surfaces)) {}

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
    grab_ = start.anchor;
    if (!grab_.sourceSize.isValid())
        grab_ = folder_.isNull() ? view->tabGrabAnchor(tab_, start.pressGlobal) : view->folderGrabAnchor(folder_, start.pressGlobal);
    active_ = true; lifted_ = false; target_ = {};
    if (folder_.isNull()) surface->setDraggedTab(tab_); else surface->setDraggedFolder(folder_);
    qApp->installEventFilter(this);
    QGuiApplication::setOverrideCursor(Qt::ClosedHandCursor);
    update(start.currentGlobal);
}

void TabDrag::cancel()
{
    if (active_) stop();
}

void TabDrag::stop()
{
    active_ = false;
    qApp->removeEventFilter(this);
    QGuiApplication::restoreOverrideCursor();
    for (const auto id : manager_.model().windowOrder())
        if (auto* surface = surfaces_(id)) surface->clearDragPresentation();
    lifted_ = false;
    target_ = {};
}

bool TabDrag::carriedExists() const
{
    return folder_.isNull() ? manager_.model().tab(tab_) != nullptr : manager_.model().folder(folder_) != nullptr;
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
    auto* top = QGuiApplication::topLevelAt(global);
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
    auto* source = surfaces_(sourceWindow_);
    auto* sourceView = source ? source->group(sourceGroup_) : nullptr;
    if (!sourceView || !carriedExists()) return cancel();
    if (overSourceStrip(global)) {
        sourceView->setLiftedTab(global, grab_);
        lifted_ = true;
        target_ = {};
        target_.window = sourceWindow_; target_.group = sourceGroup_; target_.accepted = true;
        target_.windowRect = source->globalRect();
        target_.insertionIndex = sourceView->insertionIndex(global, carried());
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
        if (here && !insertion) surface->showGuides(target_, carriedCount(), false);
        else surface->clearGuides();
    }
}

void TabDrag::finish(QPoint global)
{
    update(global);
    if (!active_) return;
    const auto target = target_;
    const auto tab = tab_, folder = folder_;
    const auto source = sourceWindow_;
    stop();
    if (!target.accepted) return;
    bool success = false;
    const bool inserting = target.insertionIndex >= 0;
    if (!folder.isNull()) success = manager_.moveTabFolderToGroup(folder, target.group, target.insertionIndex);
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
        update(static_cast<QMouseEvent*>(event)->globalPosition().toPoint());
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
    auto tabs = QVector<TabId>{tab_};
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
    return folder_.isNull() ? TabLayoutInput::Carried::Tab : TabLayoutInput::Carried::Folder;
}

int TabDrag::carriedCount() const
{
    return folder_.isNull() ? 1 : int(manager_.model().folderTabs(folder_).size());
}

} // namespace window_system::quick
