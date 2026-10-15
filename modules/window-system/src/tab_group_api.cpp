// SPDX-License-Identifier: MIT
#include "window_manager_p.hpp"
#include <QPointer>

namespace window_system {
bool WindowManager::setTabColor(TabId tab, QString color)
{
    if (!color.isEmpty() && !tabColorPreset(color)) return false;
    return applyResult(d->model.setTabColor(tab, std::move(color)));
}
bool WindowManager::sortTabs(GroupId group, TabSortOrder order) { return applyResult(d->model.sortTabs(group, order)); }
bool WindowManager::moveGroup(GroupId group, GroupId target, Placement placement) { return applyResult(d->model.moveGroup(group, target, placement)); }
bool WindowManager::moveGroupToAdjacentGroup(GroupId group, Placement direction)
{
    const auto* record = d->model.group(group); if (!record || record->location != GroupLocation::Central) return false;
    const auto target = adjacentCentralGroup(group, direction);
    if (target.isNull()) return false;
    const auto rectangles = d->windows.value(record->windowId)->surface()->groupRects();
    const auto sourceCenter = rectangles.value(group).center();
    const auto targetCenter = rectangles.value(target).center();
    // At an outer edge the neighbor is across the cyclic boundary. Insert
    // beyond that farthest group, rather than before it in the old direction.
    if (direction == Placement::Left && targetCenter.x() > sourceCenter.x()) direction = Placement::Right;
    else if (direction == Placement::Right && targetCenter.x() < sourceCenter.x()) direction = Placement::Left;
    else if (direction == Placement::Top && targetCenter.y() > sourceCenter.y()) direction = Placement::Bottom;
    else if (direction == Placement::Bottom && targetCenter.y() < sourceCenter.y()) direction = Placement::Top;
    return moveGroup(group, target, direction);
}
bool WindowManager::moveGroupToWindow(GroupId group, WindowId target, Placement placement)
{
    const QPointer<WindowManager> self(this);
    if (!applyResult(d->model.moveGroupToWindow(group, target, placement)) || !self) return false;
    showWindow(target); return self && focusGroup(group);
}
bool WindowManager::moveGroupToWindowCycle(GroupId group, bool forward)
{
    const auto* record = d->model.group(group); if (!record) return false;
    const auto target = adjacentWindow(record->windowId, forward);
    return !target.isNull() && moveGroupToWindow(group, target);
}
bool WindowManager::closeGroup(GroupId group)
{
    const auto* record = d->model.group(group);
    if (!record) return false;
    const auto tabs = record->tabs;
    return closeTabs(tabs);
}
} // namespace window_system
