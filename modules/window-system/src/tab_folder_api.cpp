// SPDX-License-Identifier: MIT
#include "window_manager_p.hpp"
#include <QPointer>

namespace window_system {
FolderId WindowManager::createTabFolder(const QVector<TabId>& tabs, QString name, QString colorId)
{
    if (!colorId.isEmpty() && !tabColorPreset(colorId)) return {};
    const auto id = QUuid::createUuid();
    return applyResult(d->model.createFolder(tabs, id, std::move(name), std::move(colorId))) ? id : FolderId{};
}
bool WindowManager::addTabToFolder(TabId tab, FolderId folder) { return applyResult(d->model.addTabToFolder(tab, folder)); }
bool WindowManager::removeTabFromFolder(TabId tab) { return applyResult(d->model.removeTabFromFolder(tab)); }
bool WindowManager::updateTabFolder(const TabFolderRecord& folder)
{
    if (!folder.colorId.isEmpty() && !tabColorPreset(folder.colorId)) return false;
    auto changed = folder; changed.name = changed.name.trimmed();
    return applyResult(d->model.updateFolder(changed));
}
bool WindowManager::ungroupTabFolder(FolderId folder) { return applyResult(d->model.ungroupFolder(folder)); }
bool WindowManager::closeTabFolder(FolderId folder)
{
    const auto tabs = d->model.folderTabs(folder);
    return !tabs.isEmpty() && closeTabs(tabs);
}
bool WindowManager::moveTabFolder(FolderId folder, int steps) { return applyResult(d->model.moveFolderWithinGroup(folder, steps)); }
bool WindowManager::moveTabFolderToGroup(FolderId folder, GroupId group, int insertionIndex)
{
    const auto* target = d->model.group(group);
    if (!target) return false;
    const auto window = target->windowId;
    const QPointer<WindowManager> self(this);
    if (!applyResult(d->model.moveFolderToGroup(folder, group, insertionIndex)) || !self) return false;
    const auto tabs = d->model.folderTabs(folder);
    showWindow(window);
    return self && !tabs.isEmpty() && activateTab(tabs.first());
}
bool WindowManager::moveTabFolderToWindow(FolderId folder, WindowId window)
{
    const auto* record = d->model.window(window);
    if (!record || d->model.groupForFolder(folder).isNull()) return false;
    auto target = record->activeGroup;
    if (const auto* group = d->model.group(target); !group || group->location == GroupLocation::AutoHide) {
        const auto central = d->model.centralGroups(window);
        target = central.isEmpty() ? GroupId{} : central.first();
    }
    return !target.isNull() && moveTabFolderToGroup(folder, target);
}
WindowId WindowManager::detachTabFolder(FolderId folder, WindowKind kind) { return detachFolderImpl(folder, kind, {}); }
bool WindowManager::mergeTabFolders(FolderId source, FolderId target) { return applyResult(d->model.mergeFolder(source, target)); }
GroupId WindowManager::convertTabFolderToGroup(FolderId folder, Placement placement)
{
    const auto created = d->model.convertFolderToGroup(folder, placement);
    if (created.isNull()) return {};
    const QPointer<WindowManager> self(this);
    refresh(); if (!self) return {};
    focusGroup(created);
    return created;
}
FolderId WindowManager::convertGroupToFolder(GroupId group, GroupId target)
{
    const auto id = QUuid::createUuid();
    return applyResult(d->model.convertGroupToFolder(group, target, id)) ? id : FolderId{};
}
FolderId WindowManager::adjacentTabFolder(FolderId folder, bool forward) const
{
    const auto* group = d->model.group(d->model.groupForFolder(folder));
    if (!group || group->folders.size() < 2) return {};
    // In strip order, cycling.
    QVector<FolderId> order;
    for (const auto tab : group->tabs) {
        const auto in = d->model.tab(tab)->folder;
        if (!in.isNull() && !order.contains(in)) order.append(in);
    }
    const int count = int(order.size());
    return order.at((order.indexOf(folder) + (forward ? 1 : count - 1)) % count);
}
} // namespace window_system
