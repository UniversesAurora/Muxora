// SPDX-License-Identifier: MIT
#include <QCoreApplication>
#include <window_system/layout_model.hpp>
#include <algorithm>

namespace window_system {
namespace {
bool directional(Placement placement) {
    return placement >= Placement::Left && placement <= Placement::Bottom;
}
LayoutTree join(LayoutTree target, LayoutTree added, Placement placement) {
    LayoutNode node;
    node.id = QUuid::createUuid();
    node.orientation =
        placement == Placement::Left || placement == Placement::Right ? Qt::Horizontal : Qt::Vertical;
    const bool before = placement == Placement::Left || placement == Placement::Top;
    node.first = before ? added : target;
    node.second = before ? target : added;
    return std::make_shared<LayoutNode>(std::move(node));
}
LayoutTree find(LayoutTree tree, PaneId pane) {
    if (!tree)
        return {};
    if (tree->isLeaf())
        return tree->itemId == pane ? tree : LayoutTree{};
    if (auto found = find(tree->first, pane))
        return found;
    return find(tree->second, pane);
}
LayoutTree replace(LayoutTree tree, PaneId pane, LayoutTree replacement) {
    if (tree->isLeaf())
        return tree->itemId == pane ? replacement : tree;
    const auto first = replace(tree->first, pane, replacement),
               second = replace(tree->second, pane, replacement);
    if (first == tree->first && second == tree->second)
        return tree;
    auto node = *tree;
    node.first = first;
    node.second = second;
    return std::make_shared<LayoutNode>(std::move(node));
}
LayoutTree combine(const QVector<LayoutTree> &trees, int begin, int end, Placement placement) {
    if (end - begin == 1)
        return trees[begin];
    const int middle = begin + (end - begin) / 2;
    return join(combine(trees, begin, middle, placement), combine(trees, middle, end, placement), placement);
}
} // namespace

TabId LayoutModel::convertGroupToTab(GroupId groupId, Placement placement) {
    TabId retained;
    const auto result = transact([&](LayoutModel &model) -> OperationResult {
        const auto *group = model.group(groupId);
        if (!group || group->location == GroupLocation::AutoHide || group->tabs.isEmpty() ||
            !directional(placement))
            return {false, QCoreApplication::translate("WindowSystem", "Converting a group needs a visible group and a split direction.")};
        const auto tabs = group->tabs;
        retained = group->activeTab;
        QVector<LayoutTree> trees;
        for (const auto tab : tabs)
            trees.append(model.tabs_[tab].panes);
        const auto tree = combine(trees, 0, trees.size(), placement);
        for (const auto tab : tabs)
            if (tab != retained)
                model.eraseTabUnchecked(tab);
        model.tabs_[retained].panes = tree;
        model.groups_[groupId].activeTab = retained;
        model.windows_[model.groups_[groupId].windowId].activeGroup = groupId;
        return {true, {}};
    });
    return result ? retained : TabId{};
}

OperationResult LayoutModel::moveTabIntoTab(TabId sourceTab, TabId targetTab, PaneId targetPane,
                                            Placement placement) {
    return transact([&](LayoutModel &model) -> OperationResult {
        const auto *source = model.tab(sourceTab);
        const auto *target = model.tab(targetTab);
        if (!source || !target || sourceTab == targetTab || !directional(placement))
            return {false, QCoreApplication::translate("WindowSystem", "Transferring a tab needs different existing tabs and a split direction.")};
        if (targetPane.isNull())
            targetPane = target->activePane;
        const auto destinationLeaf = find(target->panes, targetPane);
        if (!destinationLeaf)
            return {false, QCoreApplication::translate("WindowSystem", "The target pane does not belong to the target tab.")};
        const auto carried = source->panes;
        const auto active = source->activePane;
        const auto combined = replace(target->panes, targetPane, join(destinationLeaf, carried, placement));
        model.eraseTabUnchecked(sourceTab);
        model.tabs_[targetTab].panes = combined;
        model.tabs_[targetTab].activePane = active;
        const auto group = model.groupForTab(targetTab);
        model.groups_[group].activeTab = targetTab;
        model.windows_[model.groups_[group].windowId].activeGroup = group;
        model.eraseEmptyFloatingWindows();
        return {true, {}};
    });
}

OperationResult LayoutModel::moveGroupIntoTab(GroupId sourceGroup, TabId targetTab, PaneId targetPane,
                                              Placement placement) {
    return transact([&](LayoutModel &model) -> OperationResult {
        const auto *group = model.group(sourceGroup);
        const auto *target = model.tab(targetTab);
        if (!group || group->location == GroupLocation::AutoHide || !target ||
            group->tabs.contains(targetTab) || !directional(placement))
            return {false,
                    QCoreApplication::translate("WindowSystem", "Transferring a group needs a visible source and a target tab outside that group.")};
        if (targetPane.isNull())
            targetPane = target->activePane;
        const auto destinationLeaf = find(target->panes, targetPane);
        if (!destinationLeaf)
            return {false, QCoreApplication::translate("WindowSystem", "The target pane does not belong to the target tab.")};
        const auto tabs = group->tabs;
        const auto active = model.tabs_[group->activeTab].activePane;
        QVector<LayoutTree> trees;
        for (const auto tab : tabs)
            trees.append(model.tabs_[tab].panes);
        const auto combined =
            replace(target->panes, targetPane,
                    join(destinationLeaf, combine(trees, 0, trees.size(), placement), placement));
        for (const auto tab : tabs)
            model.eraseTabUnchecked(tab);
        model.tabs_[targetTab].panes = combined;
        model.tabs_[targetTab].activePane = active;
        const auto destination = model.groupForTab(targetTab);
        model.groups_[destination].activeTab = targetTab;
        model.windows_[model.groups_[destination].windowId].activeGroup = destination;
        model.eraseEmptyFloatingWindows();
        return {true, {}};
    });
}

GroupId LayoutModel::convertTabToGroup(TabId tabId, Placement placement) {
    GroupId destination;
    const auto result = transact([&](LayoutModel &model) -> OperationResult {
        const auto *source = model.tab(tabId);
        const auto *group = model.group(model.groupForTab(tabId));
        if (!source || !group || group->location == GroupLocation::AutoHide || !directional(placement))
            return {false, QCoreApplication::translate("WindowSystem", "Converting a tab needs a visible tab and a group split direction.")};
        const auto original = *source;
        const auto panes = model.panes(tabId);
        destination = group->id;
        if (group->tabs.size() > 1) {
            const auto moved = model.moveTabUnchecked(tabId, destination, placement, -1);
            if (!moved)
                return moved;
            destination = model.groupForTab(tabId);
        }
        QVector<TabId> tabs;
        for (const auto pane : panes) {
            auto record = original;
            record.panes = find(original.panes, pane);
            record.activePane = pane;
            if (pane != original.activePane) {
                record.id = QUuid::createUuid();
                record.openedOrder = record.visitedOrder = model.nextTabOrder();
            }
            tabs.append(record.id);
            model.tabs_.insert(record.id, std::move(record));
        }
        // Every tab split from a sticky tab stays sticky; the group holds no other tabs.
        model.groups_[destination].stickyTabs = model.groups_[destination].stickyTabs ? int(tabs.size()) : 0;
        model.groups_[destination].tabs = tabs;
        model.groups_[destination].activeTab = tabId;
        model.windows_[model.groups_[destination].windowId].activeGroup = destination;
        return {true, {}};
    });
    return result ? destination : GroupId{};
}
} // namespace window_system
