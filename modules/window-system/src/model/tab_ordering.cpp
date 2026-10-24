// SPDX-License-Identifier: MIT
#include <QCoreApplication>
#include <window_system/layout_model.hpp>
#include <algorithm>
#include <utility>

namespace window_system {
namespace {
OperationResult good() { return {true, {}}; }
OperationResult bad(QString message) { return {false, std::move(message)}; }
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
bool visibleGroup(const GroupRecord* group)
{
    return group && group->location != GroupLocation::AutoHide;
}
bool acceptsDirection(const GroupRecord& target, Placement direction)
{
    if (direction < Placement::Left || direction > Placement::Bottom) return false;
    return target.location == GroupLocation::Central || target.location == GroupLocation::Pinned;
}
LayoutTree exchangeLeaves(LayoutTree tree, GroupId first, GroupId second)
{
    if (!tree) return {};
    if (tree->isLeaf()) {
        if (tree->itemId != first && tree->itemId != second) return tree;
        auto node = *tree;
        node.itemId = node.itemId == first ? second : first;
        return std::make_shared<LayoutNode>(std::move(node));
    }
    auto left = exchangeLeaves(tree->first, first, second);
    auto right = exchangeLeaves(tree->second, first, second);
    if (left == tree->first && right == tree->second) return tree;
    auto node = *tree; node.first = std::move(left); node.second = std::move(right);
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
        if (!model.tab(id) || color.size() > 128) return bad(QCoreApplication::translate("WindowSystem", "Invalid tab or color identity."));
        model.tabs_[id].colorId = std::move(color); return good();
    });
}
OperationResult LayoutModel::sortTabs(GroupId groupId, TabSortOrder sorting)
{
    return transact([&](LayoutModel& model) {
        if (!model.group(groupId) || int(sorting) < 0 || int(sorting) > 5)
            return bad(QCoreApplication::translate("WindowSystem", "Invalid tab sorting request."));
        auto& group = model.groups_[groupId];
        auto& tabs = group.tabs;
        const bool descending = int(sorting) % 2 == 1;
        // Sticky tabs stay at the front: each run is sorted on its own.
        const auto order = [&](TabId firstId, TabId secondId) {
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
        };
        std::stable_sort(tabs.begin(), tabs.begin() + group.stickyTabs, order);
        std::stable_sort(tabs.begin() + group.stickyTabs, tabs.end(), order);
        return good();
    });
}
OperationResult LayoutModel::moveGroup(GroupId source, GroupId target, Placement direction)
{
    return transact([&](LayoutModel& model) {
        if (source == target || !visibleGroup(model.group(source)) || !visibleGroup(model.group(target))
            || !acceptsDirection(*model.group(target), direction))
            return bad(QCoreApplication::translate("WindowSystem", "A group move requires distinct visible groups and a supported split direction."));
        const auto destination = model.groups_[target];
        const auto targetWindow = destination.windowId;
        model.synchronizePinnedLayouts();
        model.unlinkGroupUnchecked(source);
        auto& moved = model.groups_[source];
        moved.windowId = targetWindow; moved.location = destination.location; moved.edge = destination.edge;
        for (const auto tab : moved.tabs) model.tabs_[tab].pinnedPosition.reset();
        if (destination.location == GroupLocation::Central) {
            auto& tree = model.windows_[targetWindow].central;
            tree = replaceGroup(tree, target, adjacentTree(target, source, direction));
        } else {
            moved.edgeExtent = destination.edgeExtent;
            auto& order = model.windows_[targetWindow].edgeGroups;
            const bool outward = (destination.edge == Edge::Left && direction == Placement::Left)
                || (destination.edge == Edge::Right && direction == Placement::Right)
                || (destination.edge == Edge::Top && direction == Placement::Top)
                || (destination.edge == Edge::Bottom && direction == Placement::Bottom);
            order.insert(order.indexOf(target) + (outward ? 0 : 1), source);
            auto& tree = model.windows_[targetWindow].pinnedLayouts[size_t(destination.edge)];
            tree = replaceGroup(tree, target, adjacentTree(target, source, direction));
            model.rememberPinnedPositionsUnchecked(targetWindow, destination.edge);
        }
        model.windows_[targetWindow].activeGroup = source;
        model.eraseEmptyFloatingWindows(); return good();
    });
}
OperationResult LayoutModel::swapGroups(GroupId first, GroupId second)
{
    return transact([&](LayoutModel& model) {
        if (first == second || !visibleGroup(model.group(first)) || !visibleGroup(model.group(second)))
            return bad(QCoreApplication::translate("WindowSystem", "A group exchange requires two distinct visible groups."));
        const auto originalFirst = model.groups_[first], originalSecond = model.groups_[second];
        const QVector<WindowId> affected = originalFirst.windowId == originalSecond.windowId
            ? QVector<WindowId>{originalFirst.windowId} : QVector<WindowId>{originalFirst.windowId, originalSecond.windowId};
        for (const auto window : affected) {
            auto& record = model.windows_[window];
            record.central = exchangeLeaves(record.central, first, second);
            for (auto& tree : record.pinnedLayouts) tree = exchangeLeaves(tree, first, second);
            for (auto& id : record.edgeGroups) {
                if (id == first) id = second;
                else if (id == second) id = first;
            }
            if (record.activeGroup == first) record.activeGroup = second;
            else if (record.activeGroup == second) record.activeGroup = first;
        }
        const auto assignSlot = [](GroupRecord& group, const GroupRecord& slot) {
            group.windowId = slot.windowId; group.location = slot.location;
            group.edge = slot.edge; group.edgeExtent = slot.edgeExtent;
        };
        assignSlot(model.groups_[first], originalSecond); assignSlot(model.groups_[second], originalFirst);
        for (const auto id : {first, second})
            for (const auto tab : model.groups_[id].tabs) model.tabs_[tab].pinnedPosition.reset();
        for (const auto window : affected)
            for (const auto edge : {Edge::Left, Edge::Right, Edge::Top, Edge::Bottom})
                model.rememberPinnedPositionsUnchecked(window, edge);
        model.windows_[originalSecond.windowId].activeGroup = first;
        return good();
    });
}
OperationResult LayoutModel::mergeGroups(GroupId source, GroupId target, int insertionIndex)
{
    return transact([&](LayoutModel& model) {
        if (source == target || !visibleGroup(model.group(source)) || !visibleGroup(model.group(target))
            || insertionIndex < -1 || insertionIndex > model.group(target)->tabs.size())
            return bad(QCoreApplication::translate("WindowSystem", "A group merge requires distinct visible groups and a valid insertion index."));
        const auto carried = model.groups_[source];
        auto& destination = model.groups_[target];
        // Carried sticky tabs join the end of the target's sticky run; the
        // rest keep their order at the requested place after that run.
        for (qsizetype offset = 0; offset < carried.stickyTabs; ++offset)
            attachTabUnchecked(destination, carried.tabs[offset], -1, true);
        auto index = std::max<qsizetype>(insertionIndex < 0 ? destination.tabs.size()
            : insertionIndex + carried.stickyTabs, destination.stickyTabs);
        for (qsizetype offset = carried.stickyTabs; offset < carried.tabs.size(); ++offset)
            destination.tabs.insert(index++, carried.tabs[offset]);
        // This explicit edit establishes a new partition membership/order.
        // Dormant positions belonging to either old group must not undo it.
        for (const auto tab : destination.tabs) model.tabs_[tab].pinnedPosition.reset();
        destination.activeTab = carried.activeTab;
        const auto targetWindow = destination.windowId;
        const auto targetEdge = destination.edge;
        const auto targetLocation = destination.location;
        model.unlinkGroupUnchecked(source); model.groups_.remove(source);
        model.windows_[targetWindow].activeGroup = target;
        if (targetLocation == GroupLocation::Pinned) model.rememberPinnedPositionsUnchecked(targetWindow, targetEdge);
        model.eraseEmptyFloatingWindows(); return good();
    });
}
OperationResult LayoutModel::moveGroupToWindow(GroupId source, WindowId target, Placement direction)
{
    return transact([&](LayoutModel& model) {
        if (!model.group(source) || !model.window(target) || model.group(source)->windowId == target
            || !visibleGroup(model.group(source))
            || direction < Placement::Center || direction > Placement::Bottom) return bad(QCoreApplication::translate("WindowSystem", "Invalid group window move."));
        const auto central = model.centralGroups(target);
        const auto active = model.windows_[target].activeGroup;
        const auto targetGroup = central.contains(active) ? active : central.value(0);
        if (!targetGroup.isNull()) return model.moveGroup(source, targetGroup, direction == Placement::Center ? Placement::Right : direction);
        model.unlinkGroupUnchecked(source); model.groups_[source].windowId = target;
        model.groups_[source].location = GroupLocation::Central;
        for (const auto tab : model.groups_[source].tabs) model.tabs_[tab].pinnedPosition.reset();
        model.windows_[target].central = groupLeaf(source); model.windows_[target].activeGroup = source;
        model.eraseEmptyFloatingWindows(); return good();
    });
}
OperationResult LayoutModel::moveGroupToEdge(GroupId source, WindowId target, Edge edge, bool pinned)
{
    return transact([&](LayoutModel& model) {
        if (!visibleGroup(model.group(source)) || !model.window(target) || edge < Edge::Left || edge > Edge::Bottom)
            return bad(QCoreApplication::translate("WindowSystem", "A group edge move requires a visible group, window and valid edge."));
        model.unlinkGroupUnchecked(source);
        auto& group = model.groups_[source];
        group.windowId = target; group.location = pinned ? GroupLocation::Pinned : GroupLocation::AutoHide;
        const bool sameAxis = (group.edge == Edge::Left || group.edge == Edge::Right)
            == (edge == Edge::Left || edge == Edge::Right);
        if (!sameAxis) group.edgeExtent = edge == Edge::Left || edge == Edge::Right ? 250 : 170;
        group.edge = edge;
        for (const auto tab : group.tabs) model.tabs_[tab].pinnedPosition.reset();
        model.windows_[target].edgeGroups.append(source);
        if (pinned) {
            model.placePinnedGroupOutermostUnchecked(source);
            model.rememberPinnedPositionsUnchecked(target, edge);
        }
        model.windows_[target].activeGroup = source;
        model.eraseEmptyFloatingWindows(); return good();
    });
}
WindowId LayoutModel::detachGroup(GroupId source, WindowKind kind)
{
    WindowId created;
    const auto result = transact([&](LayoutModel& model) {
        if (!visibleGroup(model.group(source)) || (kind != WindowKind::Workbench && kind != WindowKind::Floating))
            return bad(QCoreApplication::translate("WindowSystem", "A group detach requires a visible group and valid window kind."));
        const auto originalWindow = model.group(source)->windowId;
        const auto owner = model.familyRoot(model.window(originalWindow)->familyId);
        created = model.createWindowUnchecked(kind, owner);
        return model.moveGroupToWindow(source, created);
    });
    return result ? created : WindowId{};
}
} // namespace window_system
