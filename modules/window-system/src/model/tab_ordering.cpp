// SPDX-License-Identifier: MIT
#include <window_system/layout_model.hpp>
#include <algorithm>
#include <utility>

namespace window_system {
namespace {
OperationResult good() { return {true, {}}; }
OperationResult bad(const char* message) { return {false, QString::fromLatin1(message)}; }
LayoutTree groupLeaf(QUuid item)
{
    LayoutNode node; node.id = QUuid::createUuid(); node.itemId = item;
    return std::make_shared<LayoutNode>(std::move(node));
}
LayoutTree replaceGroup(LayoutTree tree, GroupId item, const LayoutTree& replacement)
{
    if (!tree) return {};
    if (tree->isLeaf()) return tree->itemId == item ? replacement : tree;
    const auto first = replaceGroup(tree->first, item, replacement);
    const auto second = replaceGroup(tree->second, item, replacement);
    if (!first) return second;
    if (!second) return first;
    auto node = *tree; node.first = first; node.second = second;
    return std::make_shared<LayoutNode>(std::move(node));
}
LayoutTree adjacentTree(GroupId target, GroupId added, Placement direction)
{
    LayoutNode node; node.id = QUuid::createUuid();
    node.orientation = direction == Placement::Left || direction == Placement::Right ? Qt::Horizontal : Qt::Vertical;
    const bool before = direction == Placement::Left || direction == Placement::Top;
    node.first = groupLeaf(before ? added : target); node.second = groupLeaf(before ? target : added);
    return std::make_shared<LayoutNode>(std::move(node));
}
}
quint64 LayoutModel::nextTabOrder()
{
    if (orderCounter_ >= maximumTabOrder) {
        QVector<quint64> ranks;
        ranks.reserve(tabs_.size() * 2);
        for (const auto& tab : std::as_const(tabs_)) {
            ranks.append(tab.openedOrder); ranks.append(tab.visitedOrder);
        }
        std::sort(ranks.begin(), ranks.end());
        ranks.erase(std::unique(ranks.begin(), ranks.end()), ranks.end());
        const auto compact = [&ranks](quint64 value) {
            return quint64(std::lower_bound(ranks.cbegin(), ranks.cend(), value) - ranks.cbegin()) + 1;
        };
        for (auto& tab : tabs_) {
            tab.openedOrder = compact(tab.openedOrder); tab.visitedOrder = compact(tab.visitedOrder);
        }
        orderCounter_ = quint64(ranks.size());
    }
    return ++orderCounter_;
}
OperationResult LayoutModel::setTabColor(TabId id, QString color)
{
    return transact([&](LayoutModel& model) {
        if (!model.tab(id) || color.size() > 128) return bad("Invalid tab or color identity.");
        model.tabs_[id].colorId = std::move(color); return good();
    });
}
OperationResult LayoutModel::sortTabs(GroupId groupId, TabSortOrder sorting)
{
    return transact([&](LayoutModel& model) {
        if (!model.group(groupId) || int(sorting) < 0 || int(sorting) > 5)
            return bad("Invalid tab sorting request.");
        auto& tabs = model.groups_[groupId].tabs;
        const bool descending = int(sorting) % 2 == 1;
        std::stable_sort(tabs.begin(), tabs.end(), [&](TabId firstId, TabId secondId) {
            const auto& first = model.tabs_[firstId];
            const auto& second = model.tabs_[secondId];
            int comparison = 0;
            if (sorting == TabSortOrder::AlphabeticalAscending || sorting == TabSortOrder::AlphabeticalDescending) {
                comparison = QString::compare(first.title, second.title, Qt::CaseInsensitive);
            } else {
                const bool opened = sorting == TabSortOrder::OpenedAscending || sorting == TabSortOrder::OpenedDescending;
                const auto firstOrder = opened ? first.openedOrder : first.visitedOrder;
                const auto secondOrder = opened ? second.openedOrder : second.visitedOrder;
                comparison = firstOrder < secondOrder ? -1 : firstOrder > secondOrder ? 1 : 0;
            }
            return descending ? comparison > 0 : comparison < 0;
        });
        return good();
    });
}
OperationResult LayoutModel::moveGroup(GroupId source, GroupId target, Placement direction)
{
    return transact([&](LayoutModel& model) {
        if (source == target || !model.group(source) || !model.group(target)
            || model.group(source)->location != GroupLocation::Central || model.group(target)->location != GroupLocation::Central
            || direction < Placement::Left || direction > Placement::Bottom) return bad("A group move requires two central groups and a split direction.");
        const auto targetWindow = model.groups_[target].windowId;
        model.unlinkGroupUnchecked(source);
        model.groups_[source].windowId = targetWindow;
        auto& tree = model.windows_[targetWindow].central;
        tree = replaceGroup(tree, target, adjacentTree(target, source, direction));
        model.windows_[targetWindow].activeGroup = source;
        model.eraseEmptyFloatingWindows(); return good();
    });
}
OperationResult LayoutModel::moveGroupToWindow(GroupId source, WindowId target, Placement direction)
{
    return transact([&](LayoutModel& model) {
        if (!model.group(source) || !model.window(target) || model.group(source)->windowId == target
            || model.group(source)->location != GroupLocation::Central
            || direction < Placement::Center || direction > Placement::Bottom) return bad("Invalid group window move.");
        const auto central = model.centralGroups(target);
        const auto active = model.windows_[target].activeGroup;
        const auto targetGroup = central.contains(active) ? active : central.value(0);
        if (!targetGroup.isNull()) return model.moveGroup(source, targetGroup, direction == Placement::Center ? Placement::Right : direction);
        model.unlinkGroupUnchecked(source); model.groups_[source].windowId = target;
        model.windows_[target].central = groupLeaf(source); model.windows_[target].activeGroup = source;
        model.eraseEmptyFloatingWindows(); return good();
    });
}
} // namespace window_system
