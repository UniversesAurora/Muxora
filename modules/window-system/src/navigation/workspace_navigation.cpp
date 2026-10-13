// SPDX-License-Identifier: MIT
#include "window_manager_p.hpp"
#include <window_system/window_manager.hpp>

#include <QPointer>
#include <QRect>
#include <QScopeGuard>
#include <algorithm>
#include <tuple>

namespace window_system {
namespace {
bool isDirection(Placement direction)
{
    return direction == Placement::Left || direction == Placement::Right
        || direction == Placement::Top || direction == Placement::Bottom;
}

QUuid spatialNeighbor(const QHash<QUuid, QRect>& rectangles, const QVector<QUuid>& order,
                       QUuid current, Placement direction, std::optional<int> anchor = {})
{
    if (!isDirection(direction)) return {};
    const auto origin = rectangles.value(current);
    if (origin.isEmpty()) return {};
    const bool horizontal = direction == Placement::Left || direction == Placement::Right;
    const int sign = direction == Placement::Left || direction == Placement::Top ? -1 : 1;
    const auto primary = [horizontal](const QRect& rect) { return horizontal ? rect.center().x() : rect.center().y(); };
    const auto secondary = [horizontal](const QRect& rect) { return horizontal ? rect.center().y() : rect.center().x(); };
    const auto projectionGap = [horizontal, origin, anchor](const QRect& rect) {
        const int low = horizontal ? rect.top() : rect.left();
        const int high = horizontal ? rect.bottom() : rect.right();
        const int originLow = anchor.value_or(horizontal ? origin.top() : origin.left());
        const int originHigh = anchor.value_or(horizontal ? origin.bottom() : origin.right());
        return std::max({0, low - originHigh, originLow - high});
    };
    std::optional<int> nearestProjection;
    for (const auto id : order) {
        const auto rect = rectangles.value(id);
        if (id == current || rect.isEmpty() || primary(rect) == primary(origin)) continue;
        const int gap = projectionGap(rect);
        if (!nearestProjection || gap < *nearestProjection) nearestProjection = gap;
    }
    if (!nearestProjection) return {};
    for (const bool wrap : {false, true}) {
        QUuid best;
        std::optional<std::tuple<int, int, int>> bestScore;
        for (int index = 0; index < order.size(); ++index) {
            const auto id = order[index];
            const auto rect = rectangles.value(id);
            if (id == current || rect.isEmpty() || projectionGap(rect) != *nearestProjection) continue;
            const int delta = primary(rect) - primary(origin);
            if (delta == 0 || (!wrap && delta * sign <= 0)) continue;
            // Prefer matching projection. A wrap selects the opposite extreme
            // on the same axis; it never crosses into another navigation layer.
            const auto score = std::tuple{wrap ? primary(rect) * sign : std::abs(delta),
                anchor && *nearestProjection > 0 ? 0 : std::abs(secondary(rect) - anchor.value_or(secondary(origin))), index};
            if (!bestScore || score < *bestScore) { best = id; bestScore = score; }
        }
        if (!best.isNull()) return best;
    }
    return {};
}
}
bool WindowManager::moveTabToWindow(TabId tab, WindowId target, Placement placement)
{
    if (!applyResult(d->model.moveTabToWindow(tab, target, placement))) return false;
    showWindow(target);
    return activateTab(tab);
}
bool WindowManager::reorderTab(TabId tab, bool forward)
{
    const auto* group = d->model.group(d->model.groupForTab(tab));
    if (!group) return false;
    const auto tabs = group->location == GroupLocation::AutoHide
        ? d->model.autoHiddenTabs(group->windowId, group->edge) : group->tabs;
    if (tabs.size() < 2) return false;
    const int index = (tabs.indexOf(tab) + (forward ? 1 : int(tabs.size()) - 1)) % int(tabs.size());
    return group->location == GroupLocation::AutoHide
        ? moveTabToEdgeBar(tab, group->windowId, group->edge, index)
        : moveTab(tab, group->id, Placement::Center, index);
}
GroupId WindowManager::adjacentGroup(TabId tab, Placement direction, std::optional<int> anchor) const
{
    const auto source = d->model.groupForTab(tab);
    const auto* group = d->model.group(source);
    const auto window = group ? d->windows.value(group->windowId) : QPointer<WorkspaceWindow>{};
    if (!window) return {};
    auto order = d->model.centralGroups(group->windowId);
    order += d->model.window(group->windowId)->edgeGroups;
    return spatialNeighbor(window->surface()->groupRects(), order, source, direction, anchor);
}
PaneId WindowManager::adjacentPane(TabId tab, PaneId pane, Placement direction, std::optional<int> anchor) const
{
    QHash<QUuid, QRect> rectangles;
    if (const auto view = d->tabs.value(tab)) {
        const auto frames = view->paneFrames();
        for (auto it = frames.cbegin(); it != frames.cend(); ++it)
            if (it.value()->isVisible()) rectangles.insert(it.key(), QRect(it.value()->mapToGlobal(QPoint()), it.value()->size()));
    }
    return spatialNeighbor(rectangles, d->model.panes(tab), pane, direction, anchor);
}
WindowId WindowManager::adjacentWindow(WindowId window, bool forward) const
{
    const auto order = d->model.windowOrder();
    const int current = order.indexOf(window);
    if (current < 0 || order.size() < 2) return {};
    return order[(current + (forward ? 1 : int(order.size()) - 1)) % int(order.size())];
}
bool WindowManager::moveTabToAdjacentGroup(TabId tab, Placement direction)
{
    const auto target = adjacentGroup(tab, direction);
    return !target.isNull() && moveTab(tab, target) && activateTab(tab);
}
bool WindowManager::moveTabToWindowCycle(TabId tab, bool forward)
{
    const auto* group = d->model.group(d->model.groupForTab(tab));
    const auto target = group ? adjacentWindow(group->windowId, forward) : WindowId{};
    return !target.isNull() && moveTabToWindow(tab, target);
}
bool WindowManager::movePane(TabId sourceTab, PaneId pane, TabId targetTab, PaneId targetPane, Placement placement)
{
    const auto* sourceGroup = d->model.group(d->model.groupForTab(sourceTab));
    const auto sourceWindow = sourceGroup ? sourceGroup->windowId : WindowId{};
    const auto targetOptions = tabOptions(targetTab);
    const auto result = d->model.movePane(sourceTab, pane, targetTab, targetPane, placement);
    if (!result) return false;
    auto& descriptor = d->descriptors[pane];
    descriptor.topBarSharing = targetOptions.topBarSharing;
    descriptor.bottomBarSharing = targetOptions.bottomBarSharing;
    refresh();
    const auto targetWindow = d->model.group(d->model.groupForTab(targetTab))->windowId;
    if (targetWindow != sourceWindow) showWindow(targetWindow);
    return activateTab(targetTab) && focusPane(targetTab, pane);
}
bool WindowManager::movePaneToAdjacentTab(TabId tab, PaneId pane, bool forward)
{
    const auto* group = d->model.group(d->model.groupForTab(tab));
    if (!group || group->tabs.size() < 2) return false;
    const int index = (group->tabs.indexOf(tab) + (forward ? 1 : int(group->tabs.size()) - 1)) % int(group->tabs.size());
    return movePane(tab, pane, group->tabs[index], {}, forward ? Placement::Left : Placement::Right);
}
bool WindowManager::movePaneToAdjacentGroup(TabId tab, PaneId pane, Placement direction)
{
    const auto* target = d->model.group(adjacentGroup(tab, direction));
    if (!target) return false;
    const auto placement = direction == Placement::Left ? Placement::Right : direction == Placement::Right ? Placement::Left
        : direction == Placement::Top ? Placement::Bottom : Placement::Top;
    return movePane(tab, pane, target->activeTab, {}, placement);
}
bool WindowManager::movePaneToWindow(TabId tab, PaneId pane, WindowId target, Placement placement)
{
    const auto* window = d->model.window(target);
    const auto* group = window ? d->model.group(window->activeGroup) : nullptr;
    const auto targetOptions = group ? tabOptions(group->activeTab) : tabOptions(tab);
    const auto destination = d->model.movePaneToWindow(tab, pane, target, placement);
    if (destination.isNull()) return false;
    auto& descriptor = d->descriptors[pane];
    descriptor.topBarSharing = targetOptions.topBarSharing;
    descriptor.bottomBarSharing = targetOptions.bottomBarSharing;
    refresh(); showWindow(target);
    return activateTab(destination) && focusPane(destination, pane);
}
bool WindowManager::movePaneToWindowCycle(TabId tab, PaneId pane, bool forward)
{
    const auto* group = d->model.group(d->model.groupForTab(tab));
    const auto target = group ? adjacentWindow(group->windowId, forward) : WindowId{};
    return !target.isNull() && movePaneToWindow(tab, pane, target);
}
bool WindowManager::swapPanes(TabId tab, PaneId first, PaneId second)
{
    if (!applyResult(d->model.swapPanes(tab, first, second))) return false;
    return activateTab(tab) && focusPane(tab, first);
}
bool WindowManager::swapPaneDirection(TabId tab, PaneId pane, Placement direction)
{
    const auto target = adjacentPane(tab, pane, direction);
    return !target.isNull() && swapPanes(tab, pane, target);
}
bool WindowManager::navigate(Placement direction, bool panes, const CommandContext& source)
{
    if (!isDirection(direction)) return false;
    const auto axis = direction == Placement::Left || direction == Placement::Right ? Qt::Horizontal : Qt::Vertical;
    auto& anchor = d->navigationAnchor;
    if (!anchor || anchor->window != source.window || anchor->panes != panes || anchor->axis != axis
        || (panes && anchor->tab != source.tab)) {
        QRect origin;
        if (panes) {
            if (const auto view = d->tabs.value(source.tab)) if (auto* frame = view->paneFrames().value(source.pane))
                origin = QRect(frame->mapToGlobal(QPoint()), frame->size());
        } else if (const auto view = d->windows.value(source.window)) origin = view->surface()->groupRects().value(source.group);
        if (origin.isEmpty()) return false;
        anchor = Private::NavigationAnchor{source.window, source.tab, panes, axis,
            axis == Qt::Horizontal ? origin.center().y() : origin.center().x()};
    }
    const auto best = panes ? adjacentPane(source.tab, source.pane, direction, anchor->coordinate)
                            : adjacentGroup(source.tab, direction, anchor->coordinate);
    if (best.isNull()) return false;
    d->directionalNavigation = true;
    const auto finishNavigation = qScopeGuard([this] { d->directionalNavigation = false; });
    return panes ? focusPane(source.tab, best) : focusGroup(best);
}
} // namespace window_system
