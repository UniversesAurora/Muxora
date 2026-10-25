// SPDX-License-Identifier: MIT
#include <QCoreApplication>
#include "window_manager_p.hpp"
#include <QPointer>

namespace window_system {
bool WindowManager::setTabColor(TabId tab, QString color)
{
    if (!color.isEmpty() && !tabColorPreset(color)) return false;
    return applyResult(d->model.setTabColor(tab, std::move(color)));
}
bool WindowManager::sortTabs(GroupId group, TabSortOrder order) { return applyResult(d->model.sortTabs(group, order)); }
bool WindowManager::setTabSticky(TabId tab, bool sticky)
{
    if (d->model.tab(tab) && d->model.isTabSticky(tab) == sticky) return true;
    return applyResult(d->model.setTabSticky(tab, sticky));
}
bool WindowManager::isTabSticky(TabId tab) const { return d->model.isTabSticky(tab); }
bool WindowManager::renameGroup(GroupId group, QString name)
{
    return applyResult(d->model.renameGroup(group, std::move(name)));
}
QString WindowManager::groupDisplayName(GroupId group) const
{
    const auto* record = d->model.group(group);
    if (!record) return {};
    if (!record->name.isEmpty()) return record->name;
    const auto* active = d->model.tab(record->activeTab);
    return QCoreApplication::translate("WindowSystem", "Tab group: %1").arg(active ? active->title : QString());
}
bool WindowManager::setGroupTabStrip(GroupId group, int width, bool iconsOnly)
{
    return applyResult(d->model.setGroupTabStrip(group, width, iconsOnly));
}
bool WindowManager::swapGroups(GroupId first, GroupId second)
{
    const QPointer<WindowManager> self(this);
    if (!applyResult(d->model.swapGroups(first, second)) || !self) return false;
    const auto* moved = d->model.group(first);
    if (!moved) return false;
    showWindow(moved->windowId); return self && focusGroup(first);
}
bool WindowManager::swapGroupDirection(GroupId group, Placement direction)
{
    const auto target = adjacentVisibleGroup(group, direction);
    return !target.isNull() && swapGroups(group, target);
}
bool WindowManager::mergeGroups(GroupId source, GroupId target, int insertionIndex)
{
    const QPointer<WindowManager> self(this);
    if (!applyResult(d->model.mergeGroups(source, target, insertionIndex)) || !self) return false;
    const auto* merged = d->model.group(target);
    if (!merged) return false;
    showWindow(merged->windowId); return self && focusGroup(target);
}
bool WindowManager::mergeGroupDirection(GroupId group, Placement direction)
{
    const auto target = adjacentVisibleGroup(group, direction);
    return !target.isNull() && mergeGroups(group, target);
}
bool WindowManager::moveGroup(GroupId group, GroupId target, Placement placement)
{
    const QPointer<WindowManager> self(this);
    if (!applyResult(d->model.moveGroup(group, target, placement)) || !self) return false;
    const auto* moved = d->model.group(group);
    if (!moved) return false;
    showWindow(moved->windowId); return self && focusGroup(group);
}
bool WindowManager::moveGroupToAdjacentGroup(GroupId group, Placement direction)
{
    const auto* record = d->model.group(group); if (!record || record->location == GroupLocation::AutoHide) return false;
    const auto target = adjacentVisibleGroup(group, direction, true);
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
bool WindowManager::moveGroupToEdge(GroupId group, WindowId target, Edge edge, bool pinned)
{
    const QPointer<WindowManager> self(this);
    if (!applyResult(d->model.moveGroupToEdge(group, target, edge, pinned)) || !self) return false;
    showWindow(target);
    // Auto-hide is a completed collection action. Focusing its hidden content
    // would select the rail tab and immediately reopen the transient panel.
    return self && (!pinned || focusGroup(group));
}
WindowId WindowManager::detachGroup(GroupId group, WindowKind kind)
{
    const auto* source = d->model.group(group);
    if (!source) return {};
    auto options = d->options.value(source->windowId);
    const auto created = d->model.detachGroup(group, kind);
    if (created.isNull()) return {};
    const auto bars = d->visibilityProfiles.value(kind);
    options.toolbarsVisible = bars.toolbars; options.statusBarVisible = bars.statusBar;
    options.tabBarsVisible = bars.tabBars; options.sideBarsVisible = bars.sideBars;
    d->options.insert(created, options); d->visibilityKinds.insert(created, kind);
    d->windowVisibility[created].insert(kind, bars);
    const QPointer<WindowManager> self(this);
    refresh(); if (!self) return {};
    showWindow(created); if (!self || !focusGroup(group)) return {};
    return created;
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
