#include <window_system/layout_model.hpp>

#include <QJsonArray>
#include <QJsonValue>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <utility>

namespace window_system {
namespace {

OperationResult success() { return {true, {}}; }
OperationResult failure(QString message) { return {false, std::move(message)}; }

LayoutTree leaf(QUuid itemId)
{
    LayoutNode node;
    node.id = QUuid::createUuid();
    node.itemId = itemId;
    return std::make_shared<LayoutNode>(std::move(node));
}

LayoutTree split(LayoutTree target, LayoutTree added, Placement placement)
{
    LayoutNode node;
    node.id = QUuid::createUuid();
    node.orientation = (placement == Placement::Left || placement == Placement::Right)
        ? Qt::Horizontal : Qt::Vertical;
    const bool before = placement == Placement::Left || placement == Placement::Top;
    node.first = before ? std::move(added) : std::move(target);
    node.second = before ? std::move(target) : std::move(added);
    return std::make_shared<LayoutNode>(std::move(node));
}

LayoutTree replace(LayoutTree tree, QUuid itemId, const LayoutTree& replacement)
{
    if (!tree) return {};
    if (tree->isLeaf()) return tree->itemId == itemId ? replacement : tree;
    auto first = replace(tree->first, itemId, replacement);
    auto second = replace(tree->second, itemId, replacement);
    if (!first) return second;
    if (!second) return first;
    if (first == tree->first && second == tree->second) return tree;
    auto node = *tree;
    node.first = std::move(first);
    node.second = std::move(second);
    return std::make_shared<LayoutNode>(std::move(node));
}

LayoutTree replaceRatio(LayoutTree tree, QUuid nodeId, double ratio, bool& found)
{
    if (!tree) return {};
    if (tree->id == nodeId && !tree->isLeaf()) {
        auto node = *tree;
        node.ratio = ratio;
        found = true;
        return std::make_shared<LayoutNode>(std::move(node));
    }
    if (tree->isLeaf()) return tree;
    auto first = replaceRatio(tree->first, nodeId, ratio, found);
    auto second = replaceRatio(tree->second, nodeId, ratio, found);
    if (first == tree->first && second == tree->second) return tree;
    auto node = *tree;
    node.first = std::move(first);
    node.second = std::move(second);
    return std::make_shared<LayoutNode>(std::move(node));
}

void collect(LayoutTree tree, QVector<QUuid>& items)
{
    if (!tree) return;
    if (tree->isLeaf()) items.append(tree->itemId);
    else {
        collect(tree->first, items);
        collect(tree->second, items);
    }
}

LayoutTree findLeaf(LayoutTree tree, QUuid itemId)
{
    if (!tree) return {};
    if (tree->isLeaf()) return tree->itemId == itemId ? tree : LayoutTree{};
    if (auto result = findLeaf(tree->first, itemId)) return result;
    return findLeaf(tree->second, itemId);
}

LayoutTree swapLeafItems(LayoutTree tree, QUuid firstId, QUuid secondId)
{
    if (tree->isLeaf()) {
        if (tree->itemId != firstId && tree->itemId != secondId) return tree;
        auto node = *tree;
        node.itemId = node.itemId == firstId ? secondId : firstId;
        return std::make_shared<LayoutNode>(std::move(node));
    }
    const auto first = swapLeafItems(tree->first, firstId, secondId);
    const auto second = swapLeafItems(tree->second, firstId, secondId);
    if (first == tree->first && second == tree->second) return tree;
    auto node = *tree;
    node.first = first;
    node.second = second;
    return std::make_shared<LayoutNode>(std::move(node));
}

QString uuidText(QUuid id) { return id.toString(QUuid::WithoutBraces); }
QJsonValue treeJson(LayoutTree tree)
{
    if (!tree) return QJsonValue(QJsonValue::Null);
    QJsonObject result{{"id", uuidText(tree->id)}};
    if (tree->isLeaf()) result.insert("itemId", uuidText(tree->itemId));
    else {
        result.insert("orientation", tree->orientation == Qt::Horizontal ? "horizontal" : "vertical");
        result.insert("ratio", tree->ratio);
        result.insert("first", treeJson(tree->first));
        result.insert("second", treeJson(tree->second));
    }
    return result;
}

QString edgeText(Edge edge)
{
    switch (edge) {
    case Edge::Left: return "left";
    case Edge::Right: return "right";
    case Edge::Top: return "top";
    case Edge::Bottom: return "bottom";
    }
    return {};
}

QString locationText(GroupLocation location)
{
    switch (location) {
    case GroupLocation::Central: return "central";
    case GroupLocation::Pinned: return "pinned";
    case GroupLocation::AutoHide: return "autoHide";
    }
    return {};
}

// Persistence is untrusted input. Limits also prevent pathological recursion.
constexpr int maximumTreeDepth = 64;
constexpr int maximumRecords = 10000;
constexpr int minimumPeekWidth = 140;
constexpr int minimumPeekHeight = 100;
// Keep the model independent of Qt Widgets while respecting QWIDGETSIZE_MAX.
constexpr int maximumPeekDimension = 16777215;

bool validPeekSize(QSize size)
{
    return size.width() >= minimumPeekWidth && size.height() >= minimumPeekHeight
        && size.width() <= maximumPeekDimension && size.height() <= maximumPeekDimension;
}

bool validPeekPolicy(const PeekSizePolicy& size)
{
    return (!size.width || (*size.width >= minimumPeekWidth && *size.width <= maximumPeekDimension))
        && (!size.height || (*size.height >= minimumPeekHeight && *size.height <= maximumPeekDimension));
}

QJsonObject peekPolicyJson(const PeekSizePolicy& size)
{
    QJsonObject object;
    if (size.width) object.insert("width", *size.width);
    if (size.height) object.insert("height", *size.height);
    return object;
}

OperationResult readPeekPolicy(const QJsonValue& value, PeekSizePolicy& policy)
{
    if (!value.isObject()) return failure("A directional peek size must be an object.");
    const auto object = value.toObject();
    for (const auto& key : object.keys())
        if (key != "width" && key != "height") return failure("A directional peek size has an unknown axis.");
    for (const auto* key : {"width", "height"}) {
        if (!object.contains(QLatin1String(key))) continue;
        const auto dimension = object.value(QLatin1String(key));
        const auto number = dimension.toDouble();
        const auto minimum = QLatin1String(key) == QLatin1String("width") ? minimumPeekWidth : minimumPeekHeight;
        if (!dimension.isDouble() || !std::isfinite(number) || number < minimum
            || number > maximumPeekDimension || std::floor(number) != number)
            return failure("Directional peek dimensions must be bounded integers.");
        if (QLatin1String(key) == QLatin1String("width")) policy.width = int(number);
        else policy.height = int(number);
    }
    return success();
}

class JsonReader {
public:
    QString error;
    int nodeCount = 0;

    QUuid id(const QJsonObject& object, const char* key, bool nullable = false)
    {
        const auto value = object.value(QLatin1String(key));
        if (!value.isString()) {
            fail(QStringLiteral("Field %1 must be a UUID string.").arg(QLatin1String(key)));
            return {};
        }
        const QUuid result(value.toString());
        if (result.isNull() && (!nullable || !value.toString().isEmpty()))
            fail(QStringLiteral("Field %1 contains an invalid UUID.").arg(QLatin1String(key)));
        return result;
    }

    QVector<QUuid> ids(const QJsonObject& object, const char* key)
    {
        QVector<QUuid> result;
        const auto value = object.value(QLatin1String(key));
        if (!value.isArray() || value.toArray().size() > maximumRecords) {
            fail(QStringLiteral("Field %1 must be a bounded UUID array.").arg(QLatin1String(key)));
            return result;
        }
        for (const auto& item : value.toArray()) {
            if (!item.isString() || QUuid(item.toString()).isNull()) {
                fail(QStringLiteral("Field %1 contains an invalid UUID.").arg(QLatin1String(key)));
                return {};
            }
            result.append(QUuid(item.toString()));
        }
        return result;
    }

    LayoutTree tree(const QJsonValue& value, int depth = 0)
    {
        if (value.isNull()) return {};
        if (!value.isObject() || depth > maximumTreeDepth || ++nodeCount > maximumRecords) {
            fail("A layout tree is malformed or exceeds the resource limit.");
            return {};
        }
        const auto object = value.toObject();
        LayoutNode node;
        node.id = id(object, "id");
        if (object.contains("itemId")) {
            if (object.contains("first") || object.contains("second"))
                fail("A leaf cannot also contain children.");
            node.itemId = id(object, "itemId");
        } else {
            const auto orientation = object.value("orientation").toString();
            if (orientation != "horizontal" && orientation != "vertical")
                fail("A split has an invalid orientation.");
            node.orientation = orientation == "horizontal" ? Qt::Horizontal : Qt::Vertical;
            if (!object.value("ratio").isDouble()) fail("A split ratio must be numeric.");
            node.ratio = object.value("ratio").toDouble();
            node.first = tree(object.value("first"), depth + 1);
            node.second = tree(object.value("second"), depth + 1);
            if (!node.first || !node.second) fail("A split needs two children.");
        }
        return std::make_shared<LayoutNode>(std::move(node));
    }

    void fail(QString message) { if (error.isEmpty()) error = std::move(message); }
};

QJsonArray idArray(const QVector<QUuid>& ids)
{
    QJsonArray result;
    for (auto id : ids) result.append(uuidText(id));
    return result;
}

} // namespace

const QHash<WindowId, WindowRecord>& LayoutModel::windows() const noexcept { return windows_; }
const QHash<GroupId, GroupRecord>& LayoutModel::groups() const noexcept { return groups_; }
const QHash<TabId, TabRecord>& LayoutModel::tabs() const noexcept { return tabs_; }
const WindowRecord* LayoutModel::window(WindowId id) const noexcept
{
    const auto iterator = windows_.constFind(id);
    return iterator == windows_.cend() ? nullptr : &iterator.value();
}
const GroupRecord* LayoutModel::group(GroupId id) const noexcept
{
    const auto iterator = groups_.constFind(id);
    return iterator == groups_.cend() ? nullptr : &iterator.value();
}
const TabRecord* LayoutModel::tab(TabId id) const noexcept
{
    const auto iterator = tabs_.constFind(id);
    return iterator == tabs_.cend() ? nullptr : &iterator.value();
}
QVector<WindowId> LayoutModel::windowOrder() const { return windowOrder_; }
quint64 LayoutModel::revision() const noexcept { return revision_; }
GroupId LayoutModel::groupForTab(TabId id) const
{
    for (auto iterator = groups_.cbegin(); iterator != groups_.cend(); ++iterator)
        if (iterator->tabs.contains(id)) return iterator.key();
    return {};
}
WindowId LayoutModel::familyRoot(FamilyId id) const
{
    for (auto windowId : windowOrder_) {
        const auto& record = windows_[windowId];
        if (record.familyId == id && record.kind == WindowKind::Workbench) return windowId;
    }
    return {};
}
QVector<GroupId> LayoutModel::centralGroups(WindowId id) const
{
    QVector<GroupId> result;
    if (const auto* record = window(id)) collect(record->central, result);
    return result;
}
QVector<TabId> LayoutModel::orderedTabs(WindowId id) const
{
    QVector<TabId> result;
    const auto* record = window(id);
    if (!record) return result;
    for (auto groupId : centralGroups(id)) result += groups_[groupId].tabs;
    for (auto edge : {Edge::Top, Edge::Right, Edge::Bottom, Edge::Left})
        for (auto location : {GroupLocation::Pinned, GroupLocation::AutoHide})
            for (auto groupId : record->edgeGroups) {
                const auto& groupRecord = groups_[groupId];
                if (groupRecord.edge == edge && groupRecord.location == location)
                    result += groupRecord.tabs;
            }
    return result;
}
QVector<PaneId> LayoutModel::panes(TabId id) const
{
    QVector<PaneId> result;
    if (const auto* record = tab(id)) collect(record->panes, result);
    return result;
}
TabId LayoutModel::tabForPane(PaneId id) const
{
    if (id.isNull()) return {};
    for (auto it = tabs_.cbegin(); it != tabs_.cend(); ++it)
        if (findLeaf(it.value().panes, id)) return it.key();
    return {};
}
bool LayoutModel::familyHasTabs(FamilyId id) const
{
    for (const auto& record : windows_)
        if (record.familyId == id && !orderedTabs(record.id).isEmpty()) return true;
    return false;
}

OperationResult LayoutModel::transact(const std::function<OperationResult(LayoutModel&)>& operation)
{
    LayoutModel candidate = *this;
    auto result = operation(candidate);
    if (!result) return result;
    result = candidate.validate();
    if (!result) return result;
    candidate.revision_ = revision_ + 1;
    *this = std::move(candidate);
    return success();
}

WindowId LayoutModel::createWindowUnchecked(WindowKind kind, WindowId ownerWorkbench)
{
    const auto id = QUuid::createUuid();
    const auto familyId = kind == WindowKind::Workbench
        ? QUuid::createUuid() : windows_[ownerWorkbench].familyId;
    WindowRecord record;
    record.id = id;
    record.familyId = familyId;
    record.kind = kind;
    windows_.insert(id, std::move(record));
    windowOrder_.append(id);
    return id;
}
WindowId LayoutModel::createWindow(WindowKind kind, WindowId ownerWorkbench)
{
    WindowId created;
    const auto result = transact([&](LayoutModel& model) {
        if (kind == WindowKind::Floating) {
            const auto* owner = model.window(ownerWorkbench);
            if (!owner || owner->kind != WindowKind::Workbench)
                return failure("A floating window needs an existing workbench owner.");
        }
        created = model.createWindowUnchecked(kind, ownerWorkbench);
        return success();
    });
    return result ? created : WindowId{};
}
GroupId LayoutModel::createGroupUnchecked(WindowId windowId)
{
    const auto id = QUuid::createUuid();
    GroupRecord groupRecord;
    groupRecord.id = id;
    groupRecord.windowId = windowId;
    groups_.insert(id, std::move(groupRecord));
    auto& record = windows_[windowId];
    const auto added = leaf(id);
    record.central = record.central ? split(record.central, added, Placement::Right) : added;
    record.activeGroup = id;
    return id;
}
TabId LayoutModel::addTab(WindowId windowId, QString title, GroupId targetGroup, int insertionIndex)
{
    TabId created;
    const auto result = transact([&](LayoutModel& model) {
        if (!model.window(windowId)) return failure("The target window does not exist.");
        auto target = targetGroup;
        if (!target.isNull()) {
            const auto* record = model.group(target);
            if (!record || record->windowId != windowId)
                return failure("The target group does not belong to the window.");
        } else {
            target = model.windows_[windowId].activeGroup;
            if (target.isNull()) target = model.createGroupUnchecked(windowId);
        }
        created = QUuid::createUuid();
        const auto paneId = QUuid::createUuid();
        TabRecord tabRecord;
        tabRecord.id = created; tabRecord.title = std::move(title);
        tabRecord.panes = leaf(paneId); tabRecord.activePane = paneId;
        tabRecord.openedOrder = tabRecord.visitedOrder = model.nextTabOrder();
        model.tabs_.insert(created, std::move(tabRecord));
        auto& record = model.groups_[target];
        if (insertionIndex < -1 || insertionIndex > record.tabs.size()) return failure("The insertion index is invalid.");
        record.tabs.insert(insertionIndex < 0 ? record.tabs.size() : insertionIndex, created);
        record.activeTab = created;
        model.windows_[windowId].activeGroup = target;
        return success();
    });
    return result ? created : TabId{};
}
TabId LayoutModel::addTabToNewGroup(WindowId windowId, QString title, GroupId targetGroup, Placement placement)
{
    TabId created;
    const auto result = transact([&](LayoutModel& model) {
        const auto* target = model.group(targetGroup);
        if (!model.window(windowId) || !target || target->windowId != windowId
            || target->location != GroupLocation::Central
            || placement < Placement::Left || placement > Placement::Bottom)
            return failure("An adjacent group needs a central target in its window and a split direction.");
        created = QUuid::createUuid();
        const auto pane = QUuid::createUuid();
        TabRecord record;
        record.id = created; record.title = std::move(title);
        record.panes = leaf(pane); record.activePane = pane;
        record.openedOrder = record.visitedOrder = model.nextTabOrder();
        model.tabs_.insert(created, std::move(record));
        // Append only inside the unpublished candidate. Never activate the
        // temporary membership: removing it must not choose a neighboring tab
        // or alter the source group's viewport anchor and visit history.
        model.groups_[targetGroup].tabs.append(created);
        return model.moveTabUnchecked(created, targetGroup, placement, -1);
    });
    return result ? created : TabId{};
}
OperationResult LayoutModel::renameTab(TabId id, QString title)
{
    return transact([&](LayoutModel& model) {
        if (!model.tab(id)) return failure("The tab does not exist.");
        model.tabs_[id].title = std::move(title);
        return success();
    });
}
OperationResult LayoutModel::activateTab(TabId id)
{
    return transact([&](LayoutModel& model) {
        const auto groupId = model.groupForTab(id);
        if (groupId.isNull()) return failure("The tab does not exist.");
        auto& record = model.groups_[groupId];
        if (record.activeTab != id) model.tabs_[id].visitedOrder = model.nextTabOrder();
        record.activeTab = id;
        model.windows_[record.windowId].activeGroup = groupId;
        return success();
    });
}
OperationResult LayoutModel::activatePane(TabId tabId, PaneId paneId)
{
    return transact([&](LayoutModel& model) {
        if (!model.panes(tabId).contains(paneId)) return failure("The pane does not belong to the tab.");
        model.tabs_[tabId].activePane = paneId;
        const auto groupId = model.groupForTab(tabId);
        if (model.groups_[groupId].activeTab != tabId) model.tabs_[tabId].visitedOrder = model.nextTabOrder();
        model.groups_[groupId].activeTab = tabId;
        model.windows_[model.groups_[groupId].windowId].activeGroup = groupId;
        return success();
    });
}

void LayoutModel::unlinkGroupUnchecked(GroupId groupId)
{
    const auto record = groups_[groupId];
    auto& windowRecord = windows_[record.windowId];
    if (record.location == GroupLocation::Central)
        windowRecord.central = replace(windowRecord.central, groupId, {});
    else windowRecord.edgeGroups.removeAll(groupId);
    if (windowRecord.activeGroup == groupId) {
        const auto central = centralGroups(record.windowId);
        windowRecord.activeGroup = !central.isEmpty() ? central.first()
            : (!windowRecord.edgeGroups.isEmpty() ? windowRecord.edgeGroups.first() : GroupId{});
    }
}
void LayoutModel::eraseEmptyGroup(GroupId id)
{
    if (!groups_.contains(id) || !groups_[id].tabs.isEmpty()) return;
    unlinkGroupUnchecked(id);
    groups_.remove(id);
}
void LayoutModel::eraseEmptyFloatingWindows()
{
    const auto order = windowOrder_;
    for (auto id : order) {
        if (windows_[id].kind == WindowKind::Floating && orderedTabs(id).isEmpty()) {
            windows_.remove(id);
            windowOrder_.removeAll(id);
        }
    }
}
OperationResult LayoutModel::moveTabUnchecked(TabId tabId, GroupId targetGroup,
                                               Placement placement, int insertionIndex)
{
    const auto sourceId = groupForTab(tabId);
    if (sourceId.isNull() || !groups_.contains(targetGroup))
        return failure("The source tab or target group no longer exists.");
    auto destinationId = targetGroup;
    const auto targetWindow = groups_[targetGroup].windowId;
    if (placement != Placement::Center) {
        if (groups_[targetGroup].location != GroupLocation::Central)
            return failure("Directional splitting requires a central target group.");
        destinationId = QUuid::createUuid();
        GroupRecord destination;
        destination.id = destinationId;
        destination.windowId = targetWindow;
        groups_.insert(destinationId, std::move(destination));
        auto& tree = windows_[targetWindow].central;
        tree = replace(tree, targetGroup, split(findLeaf(tree, targetGroup), leaf(destinationId), placement));
    }
    auto& source = groups_[sourceId];
    const auto oldIndex = source.tabs.indexOf(tabId);
    source.tabs.removeAt(oldIndex);
    if (source.activeTab == tabId)
        source.activeTab = source.tabs.isEmpty() ? TabId{} : source.tabs[std::min(oldIndex, source.tabs.size() - 1)];
    auto& target = groups_[destinationId];
    auto index = insertionIndex < 0 ? int(target.tabs.size())
        : std::clamp(insertionIndex, 0, int(target.tabs.size()));
    target.tabs.insert(index, tabId);
    target.activeTab = tabId;
    windows_[targetWindow].activeGroup = destinationId;
    eraseEmptyGroup(sourceId);
    return success();
}
OperationResult LayoutModel::moveTab(TabId tabId, GroupId targetGroup, Placement placement, int insertionIndex)
{
    return transact([&](LayoutModel& model) {
        auto result = model.moveTabUnchecked(tabId, targetGroup, placement, insertionIndex);
        if (result) model.eraseEmptyFloatingWindows();
        return result;
    });
}
OperationResult LayoutModel::moveTabToWindow(TabId tabId, WindowId targetWindow, Placement placement)
{
    return transact([&](LayoutModel& model) {
        if (!model.window(targetWindow)) return failure("The target window does not exist.");
        auto target = model.windows_[targetWindow].activeGroup;
        auto effectivePlacement = placement;
        if (target.isNull()) {
            target = model.createGroupUnchecked(targetWindow);
            // There is no existing group to split in an empty window.
            effectivePlacement = Placement::Center;
        }
        auto result = model.moveTabUnchecked(tabId, target, effectivePlacement, -1);
        if (result) model.eraseEmptyFloatingWindows();
        return result;
    });
}
OperationResult LayoutModel::moveGroupToEdge(GroupId groupId, Edge edge, bool pinned)
{
    return transact([&](LayoutModel& model) {
        if (!model.group(groupId)) return failure("The group does not exist.");
        const auto windowId = model.groups_[groupId].windowId;
        model.unlinkGroupUnchecked(groupId);
        auto& record = model.groups_[groupId];
        record.edge = edge;
        record.edgeExtent = edge == Edge::Top || edge == Edge::Bottom ? 170 : 250;
        record.location = pinned ? GroupLocation::Pinned : GroupLocation::AutoHide;
        // Ordered from the outer edge inward. An explicit pinned drop always
        // creates the partition nearest that edge rather than joining a group.
        auto& edgeGroups = model.windows_[windowId].edgeGroups;
        if (pinned) {
            auto position = edgeGroups.size();
            for (int i = 0; i < edgeGroups.size(); ++i) {
                const auto& other = model.groups_[edgeGroups[i]];
                if (other.edge == edge && other.location == GroupLocation::Pinned) { position = i; break; }
            }
            edgeGroups.insert(position, groupId);
        } else edgeGroups.append(groupId);
        for (const auto tab : record.tabs) model.tabs_[tab].pinnedPosition.reset();
        model.windows_[windowId].activeGroup = groupId;
        return success();
    });
}
OperationResult LayoutModel::moveTabToEdge(TabId tabId, Edge edge, bool pinned)
{
    return transact([&](LayoutModel& model) {
        auto source = model.groupForTab(tabId);
        if (source.isNull()) return failure("The tab does not exist.");
        if (model.groups_[source].tabs.size() > 1) {
            const auto window = model.groups_[source].windowId;
            const auto isolated = model.createGroupUnchecked(window);
            const auto moved = model.moveTabUnchecked(tabId, isolated, Placement::Center, -1);
            if (!moved) return moved;
            source = isolated;
        }
        return model.moveGroupToEdge(source, edge, pinned);
    });
}
QVector<TabId> LayoutModel::autoHiddenTabs(WindowId windowId, Edge edge) const
{
    QVector<TabId> result;
    if (const auto* record = window(windowId)) for (const auto groupId : record->edgeGroups) {
        const auto* candidate = group(groupId);
        if (candidate && candidate->edge == edge && candidate->location == GroupLocation::AutoHide)
            result += candidate->tabs;
    }
    return result;
}
OperationResult LayoutModel::moveTabToEdgeBarUnchecked(TabId tabId, WindowId targetWindow, Edge edge, int insertionIndex)
{
    const auto sourceId = groupForTab(tabId);
    if (sourceId.isNull() || !window(targetWindow)) return failure("The source tab or target window does not exist.");
    if (edge < Edge::Left || edge > Edge::Bottom || insertionIndex < -1)
        return failure("The edge or insertion index is invalid.");
    auto visibleTabs = autoHiddenTabs(targetWindow, edge);
    visibleTabs.removeAll(tabId);
    const int index = insertionIndex < 0 ? int(visibleTabs.size())
        : std::clamp(insertionIndex, 0, int(visibleTabs.size()));
    GroupId destination;
    int groupIndex = 0;
    if (!visibleTabs.isEmpty()) {
        const auto adjacent = visibleTabs[index < visibleTabs.size() ? index : visibleTabs.size() - 1];
        destination = groupForTab(adjacent);
        groupIndex = groups_[destination].tabs.indexOf(adjacent) + (index == visibleTabs.size() ? 1 : 0);
        if (sourceId == destination && groups_[sourceId].tabs.indexOf(tabId) < groupIndex) --groupIndex;
    } else if (groups_[sourceId].windowId == targetWindow && groups_[sourceId].edge == edge
               && groups_[sourceId].location == GroupLocation::AutoHide) {
        destination = sourceId;
    } else {
        destination = QUuid::createUuid();
        GroupRecord record;
        record.id = destination; record.windowId = targetWindow; record.edge = edge;
        record.location = GroupLocation::AutoHide;
        record.edgeExtent = edge == Edge::Top || edge == Edge::Bottom ? 170 : 250;
        groups_.insert(destination, std::move(record));
        windows_[targetWindow].edgeGroups.append(destination);
    }
    // Reordering keeps a dormant pinned partition; moving to another side or
    // window establishes a new placement instead of retaining a stale origin.
    auto& saved = tabs_[tabId].pinnedPosition;
    if (saved && (saved->window != targetWindow || saved->edge != edge)) saved.reset();
    return moveTabUnchecked(tabId, destination, Placement::Center, groupIndex);
}
OperationResult LayoutModel::moveTabToEdgeBar(TabId tabId, WindowId targetWindow, Edge edge, int insertionIndex)
{
    return transact([&](LayoutModel& model) {
        auto result = model.moveTabToEdgeBarUnchecked(tabId, targetWindow, edge, insertionIndex);
        if (result) model.eraseEmptyFloatingWindows();
        return result;
    });
}
OperationResult LayoutModel::moveAllTabsToEdgeBar(WindowId sourceWindow, WindowId targetWindow, Edge edge, int insertionIndex)
{
    return transact([&](LayoutModel& model) {
        if (!model.window(sourceWindow) || !model.window(targetWindow) || sourceWindow == targetWindow)
            return failure("Whole-window movement requires two existing, different windows.");
        const auto payload = model.orderedTabs(sourceWindow);
        if (payload.isEmpty()) return failure("The source window has no tabs.");
        if (insertionIndex < -1) return failure("The insertion index is invalid.");
        const int count = int(model.autoHiddenTabs(targetWindow, edge).size());
        int index = insertionIndex < 0 ? count : std::clamp(insertionIndex, 0, count);
        for (const auto tab : payload) {
            const auto result = model.moveTabToEdgeBarUnchecked(tab, targetWindow, edge, index++);
            if (!result) return result;
        }
        model.eraseEmptyFloatingWindows();
        return success();
    });
}
OperationResult LayoutModel::setGroupPinned(GroupId groupId, bool pinned)
{
    return transact([&](LayoutModel& model) {
        const auto* record = model.group(groupId);
        if (!record || record->location == GroupLocation::Central)
            return failure("Only an edge group can change its pinned state.");
        if ((record->location == GroupLocation::Pinned) == pinned) return success();
        if (!pinned) model.rememberPinnedPositionsUnchecked(record->windowId, record->edge);
        model.groups_[groupId].location = pinned ? GroupLocation::Pinned : GroupLocation::AutoHide;
        if (pinned) {
            const auto& saved = model.tabs_[record->activeTab].pinnedPosition;
            if (saved && saved->group == groupId && saved->window == record->windowId && saved->edge == record->edge) {
                auto& order = model.windows_[record->windowId].edgeGroups;
                order.removeAll(groupId);
                order.insert(std::clamp(saved->groupIndex, 0, int(order.size())), groupId);
            } else model.placePinnedGroupOutermostUnchecked(groupId);
        }
        return success();
    });
}
void LayoutModel::placePinnedGroupOutermostUnchecked(GroupId groupId)
{
    const auto& group = groups_[groupId];
    auto& order = windows_[group.windowId].edgeGroups;
    order.removeAll(groupId);
    int index = int(order.size());
    for (int i = 0; i < order.size(); ++i) {
        const auto& other = groups_[order[i]];
        if (other.edge == group.edge && other.location == GroupLocation::Pinned) { index = i; break; }
    }
    order.insert(index, groupId);
}
void LayoutModel::rememberPinnedPositionsUnchecked(WindowId windowId, Edge edge)
{
    const auto order = windows_[windowId].edgeGroups;
    const bool rebase = std::none_of(order.cbegin(), order.cend(), [&](GroupId id) {
        const auto& group = groups_[id];
        return group.edge == edge && group.location == GroupLocation::AutoHide
            && std::any_of(group.tabs.cbegin(), group.tabs.cend(), [&](TabId tab) {
                const auto& saved = tabs_[tab].pinnedPosition;
                return saved && saved->window == windowId && saved->edge == edge;
            });
    });
    // Capture the entire side before its first removal, retaining ranks even
    // when earlier partitions disappear. Shared by group and tab operations.
    for (int groupIndex = 0; groupIndex < order.size(); ++groupIndex) {
        const auto& group = groups_[order[groupIndex]];
        if (group.edge != edge || group.location != GroupLocation::Pinned) continue;
        for (int i = 0; i < group.tabs.size(); ++i) {
            auto& saved = tabs_[group.tabs[i]].pinnedPosition;
            if (rebase || !saved || saved->window != windowId || saved->group != group.id)
                saved = TabRecord::PinnedPosition{windowId, group.id, edge, groupIndex, i, group.edgeExtent};
            else saved->extent = group.edgeExtent;
        }
    }
}
OperationResult LayoutModel::setTabPinnedUnchecked(TabId tabId, bool pinned)
{
    const auto sourceId = groupForTab(tabId);
    const auto* source = group(sourceId);
    if (!source || source->location == GroupLocation::Central)
        return failure("Only an edge tab can change its pinned state.");
    if ((source->location == GroupLocation::Pinned) == pinned) return success();
    const auto windowId = source->windowId;
    const auto edge = source->edge;
    const auto extent = source->edgeExtent;
    GroupId targetId;
    int insertion = -1;
    if (!pinned) {
        rememberPinnedPositionsUnchecked(windowId, edge);
        targetId = createGroupUnchecked(windowId);
        unlinkGroupUnchecked(targetId);
        auto& target = groups_[targetId];
        target.location = GroupLocation::AutoHide;
        target.edge = edge;
        target.edgeExtent = extent;
        windows_[windowId].edgeGroups.append(targetId);
    } else {
        const auto saved = tabs_[tabId].pinnedPosition;
        if (saved && saved->window == windowId && saved->edge == edge) {
            const auto* previous = group(saved->group);
            if (previous && previous->windowId == windowId && previous->edge == edge
                && previous->location == GroupLocation::Pinned) targetId = saved->group;
            else if (!previous || (previous->id == sourceId && previous->tabs.size() == 1)) {
                if (previous) {
                    groups_[sourceId].location = GroupLocation::Pinned;
                    auto& order = windows_[windowId].edgeGroups;
                    order.removeAll(sourceId);
                    order.insert(std::clamp(saved->groupIndex, 0, int(order.size())), sourceId);
                    return success();
                }
                targetId = saved->group;
                GroupRecord target;
                target.id = targetId; target.windowId = windowId;
                target.location = GroupLocation::Pinned; target.edge = edge;
                target.edgeExtent = saved->extent;
                groups_.insert(targetId, target);
                auto& order = windows_[windowId].edgeGroups;
                order.insert(std::clamp(saved->groupIndex, 0, int(order.size())), targetId);
            }
            if (!targetId.isNull()) {
                insertion = 0;
                for (const auto member : groups_[targetId].tabs) {
                    const auto& rank = tabs_[member].pinnedPosition;
                    if (!rank || rank->group != saved->group || rank->tabIndex < saved->tabIndex) ++insertion;
                }
            }
        }
        if (targetId.isNull()) {
            if (source->tabs.size() == 1) {
                groups_[sourceId].location = GroupLocation::Pinned;
                placePinnedGroupOutermostUnchecked(sourceId);
                return success();
            }
            targetId = createGroupUnchecked(windowId);
            unlinkGroupUnchecked(targetId);
            groups_[targetId].location = GroupLocation::Pinned;
            groups_[targetId].edge = edge;
            groups_[targetId].edgeExtent = extent;
            windows_[windowId].edgeGroups.append(targetId);
            placePinnedGroupOutermostUnchecked(targetId);
        }
    }
    return moveTabUnchecked(tabId, targetId, Placement::Center, insertion);
}
OperationResult LayoutModel::setTabPinned(TabId tabId, bool pinned)
{
    return transact([&](LayoutModel& model) { return model.setTabPinnedUnchecked(tabId, pinned); });
}
OperationResult LayoutModel::setEdgePinned(WindowId windowId, Edge edge, bool pinned)
{
    return transact([&](LayoutModel& model) {
        const auto* window = model.window(windowId);
        if (!window) return failure("The window does not exist.");
        if (edge != Edge::Left && edge != Edge::Right && edge != Edge::Top && edge != Edge::Bottom)
            return failure("The edge direction is invalid.");
        QVector<TabId> tabs;
        auto groupOrder = window->edgeGroups;
        if (pinned) std::reverse(groupOrder.begin(), groupOrder.end());
        for (auto id : groupOrder) {
            const auto* group = model.group(id);
            if (!group || group->edge != edge) continue;
            if (pinned && group->location == GroupLocation::AutoHide
                && std::none_of(group->tabs.cbegin(), group->tabs.cend(), [&](TabId tab) {
                    const auto& saved = model.tabs_[tab].pinnedPosition;
                    return saved && saved->window == windowId && saved->edge == edge;
                })) {
                // A whole-side action keeps a previously untouched group
                // together; individual pin actions still move just one tab.
                model.groups_[id].location = GroupLocation::Pinned;
                model.placePinnedGroupOutermostUnchecked(id);
            } else tabs += group->tabs;
        }
        for (auto tab : tabs) {
            const auto result = model.setTabPinnedUnchecked(tab, pinned);
            if (!result) return result;
        }
        return success();
    });
}
OperationResult LayoutModel::setEdgeGroupExtent(GroupId groupId, int extent)
{
    return transact([&](LayoutModel& model) {
        const auto* group = model.group(groupId);
        if (!group || group->location == GroupLocation::Central || extent < 32 || extent > 32768)
            return failure("Edge extents require an edge group and a size between 32 and 32768.");
        model.groups_[groupId].edgeExtent = extent;
        return success();
    });
}
OperationResult LayoutModel::setPeekSize(TabId tabId, std::optional<QSize> size)
{
    return transact([&](LayoutModel& model) {
        if (!model.tab(tabId)) return failure("The tab does not exist.");
        if (size && !validPeekSize(*size))
            return failure("Peek sizes require width 140..16777215 and height 100..16777215.");
        model.tabs_[tabId].peekSize = size;
        model.tabs_[tabId].verticalPeekSize.reset();
        model.tabs_[tabId].horizontalPeekSize.reset();
        return success();
    });
}
PeekSizePolicy LayoutModel::peekSizePolicy(TabId tabId, Edge edge) const
{
    const auto* record = tab(tabId);
    if (!record) return {};
    const bool vertical = edge == Edge::Left || edge == Edge::Right;
    const auto& policy = vertical ? record->verticalPeekSize : record->horizontalPeekSize;
    if (policy) return *policy;
    if (!record->peekSize) return {};
    // Older snapshots remember both cross-edge defaults, while stretching the
    // other axis. Do not turn their formerly ignored dimension into a new size.
    return vertical ? PeekSizePolicy{record->peekSize->width(), std::nullopt}
                    : PeekSizePolicy{std::nullopt, record->peekSize->height()};
}
OperationResult LayoutModel::setPeekSizePolicy(TabId tabId, Edge edge, PeekSizePolicy size)
{
    return transact([&](LayoutModel& model) {
        if (!model.tab(tabId)) return failure("The tab does not exist.");
        if (edgeText(edge).isEmpty()) return failure("The peek edge is invalid.");
        if (!validPeekPolicy(size)) return failure("Directional peek dimensions are outside the supported range.");
        auto& record = model.tabs_[tabId];
        (edge == Edge::Left || edge == Edge::Right ? record.verticalPeekSize : record.horizontalPeekSize) = size;
        return success();
    });
}
OperationResult LayoutModel::moveGroupToCenter(GroupId groupId, GroupId targetGroup, Placement placement)
{
    return transact([&](LayoutModel& model) {
        if (!model.group(groupId)) return failure("The source group does not exist.");
        const auto sourceWindow = model.groups_[groupId].windowId;
        auto target = targetGroup;
        if (target == groupId) return failure("A group cannot be inserted into itself.");
        if (!target.isNull() && (!model.group(target) || model.groups_[target].location != GroupLocation::Central))
            return failure("The target must be a central group.");
        if (target.isNull()) {
            auto central = model.centralGroups(sourceWindow);
            central.removeAll(groupId);
            if (!central.isEmpty()) target = central.first();
        }
        if (!target.isNull() && placement == Placement::Center) {
            const auto copiedTabs = model.groups_[groupId].tabs;
            for (auto tabId : copiedTabs) {
                auto result = model.moveTabUnchecked(tabId, target, Placement::Center, -1);
                if (!result) return result;
            }
            model.eraseEmptyFloatingWindows();
            return success();
        }
        model.unlinkGroupUnchecked(groupId);
        const auto targetWindow = target.isNull() ? sourceWindow : model.groups_[target].windowId;
        auto& record = model.groups_[groupId];
        record.windowId = targetWindow;
        record.location = GroupLocation::Central;
        auto& tree = model.windows_[targetWindow].central;
        if (target.isNull()) tree = leaf(groupId);
        else tree = replace(tree, target, split(findLeaf(tree, target), leaf(groupId), placement));
        model.windows_[targetWindow].activeGroup = groupId;
        model.eraseEmptyFloatingWindows();
        return success();
    });
}
WindowId LayoutModel::detachTab(TabId tabId, WindowKind kind)
{
    WindowId created;
    const auto result = transact([&](LayoutModel& model) {
        const auto source = model.groupForTab(tabId);
        if (source.isNull()) return failure("The tab does not exist.");
        const auto family = model.windows_[model.groups_[source].windowId].familyId;
        created = model.createWindowUnchecked(kind, model.familyRoot(family));
        const auto target = model.createGroupUnchecked(created);
        auto moved = model.moveTabUnchecked(tabId, target, Placement::Center, -1);
        if (moved) model.eraseEmptyFloatingWindows();
        return moved;
    });
    return result ? created : WindowId{};
}
OperationResult LayoutModel::promoteWindow(WindowId windowId)
{
    return transact([&](LayoutModel& model) {
        const auto* record = model.window(windowId);
        if (!record || record->kind != WindowKind::Floating)
            return failure("Only a floating window can become a workbench.");
        model.windows_[windowId].kind = WindowKind::Workbench;
        model.windows_[windowId].familyId = QUuid::createUuid();
        return success();
    });
}
OperationResult LayoutModel::demoteWindow(WindowId windowId, WindowId targetWorkbench)
{
    return transact([&](LayoutModel& model) {
        const auto* source = model.window(windowId);
        const auto* target = model.window(targetWorkbench);
        if (!source || source->kind != WindowKind::Workbench || !target
            || target->kind != WindowKind::Workbench || windowId == targetWorkbench)
            return failure("Demotion requires two different workbench windows.");
        const auto previousFamily = source->familyId;
        const auto nextFamily = target->familyId;
        for (auto& record : model.windows_)
            if (record.familyId == previousFamily) record.familyId = nextFamily;
        model.windows_[windowId].kind = WindowKind::Floating;
        return success();
    });
}
OperationResult LayoutModel::reassignFamily(WindowId windowId, WindowId targetWorkbench)
{
    return transact([&](LayoutModel& model) {
        const auto* source = model.window(windowId);
        const auto* target = model.window(targetWorkbench);
        if (!source || source->kind != WindowKind::Floating || !target || target->kind != WindowKind::Workbench)
            return failure("Reassignment needs a floating window and a workbench.");
        model.windows_[windowId].familyId = target->familyId;
        return success();
    });
}

OperationResult LayoutModel::moveAllTabs(WindowId sourceWindow, GroupId targetGroup, Placement placement, int insertionIndex)
{
    return transact([&](LayoutModel& model) {
        return model.moveAllTabsUnchecked(sourceWindow, targetGroup, placement, insertionIndex);
    });
}

LayoutTree LayoutModel::takeWindowTreeUnchecked(WindowId sourceWindow, WindowId targetWindow)
{
    auto carried = windows_[sourceWindow].central;
    const auto edgeGroups = windows_[sourceWindow].edgeGroups;
    auto carriedGroups = centralGroups(sourceWindow);
    for (auto edge : {Edge::Top, Edge::Right, Edge::Bottom, Edge::Left}) {
        for (auto location : {GroupLocation::Pinned, GroupLocation::AutoHide}) {
            for (auto groupId : edgeGroups) {
                auto& record = groups_[groupId];
                if (record.edge != edge || record.location != location) continue;
                const auto direction = edge == Edge::Top ? Placement::Top
                    : edge == Edge::Right ? Placement::Right
                    : edge == Edge::Bottom ? Placement::Bottom : Placement::Left;
                carried = carried ? split(carried, leaf(groupId), direction) : leaf(groupId);
                carriedGroups.append(groupId);
            }
        }
    }
    for (auto groupId : carriedGroups) {
        auto& record = groups_[groupId];
        record.windowId = targetWindow;
        record.location = GroupLocation::Central;
    }
    auto& sourceRecord = windows_[sourceWindow];
    sourceRecord.central.reset();
    sourceRecord.edgeGroups.clear();
    sourceRecord.activeGroup = GroupId{};
    return carried;
}

OperationResult LayoutModel::moveAllTabsUnchecked(WindowId sourceWindow, GroupId targetGroup, Placement placement, int insertionIndex)
{
    const auto* source = window(sourceWindow);
    const auto* target = group(targetGroup);
    if (!source || !target || target->windowId == sourceWindow)
        return failure("Whole-window movement requires a different target window.");
    const auto ordered = orderedTabs(sourceWindow);
    if (ordered.isEmpty()) return failure("The source window has no tabs.");
    const auto targetWindow = target->windowId;
    const auto activeGroup = source->activeGroup;
    const auto activeTab = group(activeGroup) ? groups_[activeGroup].activeTab : ordered.first();
    if (placement == Placement::Center) {
        auto index = insertionIndex;
        for (auto tabId : ordered) {
            auto moved = moveTabUnchecked(tabId, targetGroup, Placement::Center, index);
            if (!moved) return moved;
            if (index >= 0) index = groups_[targetGroup].tabs.indexOf(tabId) + 1;
        }
        groups_[targetGroup].activeTab = activeTab;
        windows_[targetWindow].activeGroup = targetGroup;
    } else {
        if (target->location != GroupLocation::Central)
            return failure("Directional whole-window movement needs a central target group.");
        const auto carried = takeWindowTreeUnchecked(sourceWindow, targetWindow);
        auto& targetTree = windows_[targetWindow].central;
        targetTree = replace(targetTree, targetGroup,
                             split(findLeaf(targetTree, targetGroup), carried, placement));
        windows_[targetWindow].activeGroup = activeGroup;
    }
    eraseEmptyFloatingWindows();
    return success();
}

OperationResult LayoutModel::moveAllTabsToWindow(WindowId sourceWindow, WindowId targetWindow, Placement placement)
{
    return transact([&](LayoutModel& model) {
        const auto* source = model.window(sourceWindow);
        const auto* target = model.window(targetWindow);
        if (!source || !target || sourceWindow == targetWindow)
            return failure("Whole-window movement requires two existing, different windows.");
        if (model.orderedTabs(sourceWindow).isEmpty()) return failure("The source window has no tabs.");
        if (!target->activeGroup.isNull())
            return model.moveAllTabsUnchecked(sourceWindow, target->activeGroup, placement);
        if (placement == Placement::Center) {
            const auto destination = model.createGroupUnchecked(targetWindow);
            return model.moveAllTabsUnchecked(sourceWindow, destination, Placement::Center);
        }
        const auto activeGroup = source->activeGroup;
        const auto carried = model.takeWindowTreeUnchecked(sourceWindow, targetWindow);
        model.windows_[targetWindow].central = carried;
        model.windows_[targetWindow].activeGroup = activeGroup;
        model.eraseEmptyFloatingWindows();
        return success();
    });
}
OperationResult LayoutModel::moveAllTabsToEdge(WindowId sourceWindow, WindowId targetWindow, Edge edge, bool pinned)
{
    return transact([&](LayoutModel& model) {
        if (!model.window(sourceWindow) || !model.window(targetWindow) || sourceWindow == targetWindow)
            return failure("Whole-window edge movement needs two existing, different windows.");
        if (model.orderedTabs(sourceWindow).isEmpty()) return failure("The source window has no tabs.");
        // Create an isolated destination directly. No temporary window, family,
        // central target or transient merge with unrelated tabs is needed.
        const auto destination = model.createGroupUnchecked(targetWindow);
        const auto moved = model.moveAllTabsUnchecked(sourceWindow, destination, Placement::Center);
        if (!moved) return moved;
        return model.moveGroupToEdge(destination, edge, pinned);
    });
}

void LayoutModel::eraseTabUnchecked(TabId tabId)
{
    const auto groupId = groupForTab(tabId);
    if (groupId.isNull()) return;
    auto& record = groups_[groupId];
    const auto oldIndex = record.tabs.indexOf(tabId);
    record.tabs.removeAt(oldIndex);
    if (record.activeTab == tabId)
        record.activeTab = record.tabs.isEmpty() ? TabId{}
            : record.tabs[std::min(oldIndex, record.tabs.size() - 1)];
    tabs_.remove(tabId);
    eraseEmptyGroup(groupId);
}
OperationResult LayoutModel::removeTab(TabId tabId)
{
    return transact([&](LayoutModel& model) {
        if (!model.tab(tabId)) return failure("The tab does not exist.");
        model.eraseTabUnchecked(tabId);
        model.eraseEmptyFloatingWindows();
        return success();
    });
}
OperationResult LayoutModel::closeWindow(WindowId windowId)
{
    return transact([&](LayoutModel& model) {
        const auto* record = model.window(windowId);
        if (!record) return failure("The window does not exist.");
        QVector<WindowId> closing;
        for (auto id : model.windowOrder_)
            if (id == windowId || (record->kind == WindowKind::Workbench && model.windows_[id].familyId == record->familyId))
                closing.append(id);
        for (auto id : closing) {
            for (auto tabId : model.orderedTabs(id)) model.eraseTabUnchecked(tabId);
            model.windows_.remove(id);
            model.windowOrder_.removeAll(id);
        }
        return success();
    });
}
PaneId LayoutModel::splitPane(TabId tabId, PaneId paneId, Placement placement)
{
    PaneId created;
    const auto result = transact([&](LayoutModel& model) {
        if (placement == Placement::Center || !model.panes(tabId).contains(paneId))
            return failure("Pane splitting needs a valid pane and a direction.");
        created = QUuid::createUuid();
        auto& record = model.tabs_[tabId];
        record.panes = replace(record.panes, paneId,
                               split(findLeaf(record.panes, paneId), leaf(created), placement));
        record.activePane = created;
        return success();
    });
    return result ? created : PaneId{};
}
OperationResult LayoutModel::removePane(TabId tabId, PaneId paneId)
{
    return transact([&](LayoutModel& model) {
        if (!model.panes(tabId).contains(paneId)) return failure("The pane does not belong to the tab.");
        auto& record = model.tabs_[tabId];
        record.panes = replace(record.panes, paneId, {});
        if (!record.panes) model.eraseTabUnchecked(tabId);
        else if (record.activePane == paneId) record.activePane = model.panes(tabId).first();
        model.eraseEmptyFloatingWindows();
        return success();
    });
}
OperationResult LayoutModel::movePane(TabId sourceTab, PaneId paneId, TabId targetTab,
                                      PaneId targetPane, Placement placement)
{
    return transact([&](LayoutModel& model) {
        if (sourceTab == targetTab || !model.panes(sourceTab).contains(paneId) || !model.tab(targetTab))
            return failure("Pane transfer needs different existing source and target tabs.");
        if (placement != Placement::Left && placement != Placement::Right
            && placement != Placement::Top && placement != Placement::Bottom)
            return failure("Pane transfer needs a split direction.");
        if (targetPane.isNull()) targetPane = model.tabs_[targetTab].activePane;
        if (!model.panes(targetTab).contains(targetPane)) return failure("The target pane does not belong to the target tab.");
        const auto carried = findLeaf(model.tabs_[sourceTab].panes, paneId);
        auto& destination = model.tabs_[targetTab];
        destination.panes = replace(destination.panes, targetPane,
            split(findLeaf(destination.panes, targetPane), carried, placement));
        destination.activePane = paneId;
        auto& source = model.tabs_[sourceTab];
        source.panes = replace(source.panes, paneId, {});
        if (!source.panes) model.eraseTabUnchecked(sourceTab);
        else if (source.activePane == paneId) source.activePane = model.panes(sourceTab).first();
        const auto targetGroup = model.groupForTab(targetTab);
        model.groups_[targetGroup].activeTab = targetTab;
        model.windows_[model.groups_[targetGroup].windowId].activeGroup = targetGroup;
        model.eraseEmptyFloatingWindows();
        return success();
    });
}
OperationResult LayoutModel::swapPanes(TabId tabId, PaneId first, PaneId second)
{
    return transact([&](LayoutModel& model) {
        const auto panes = model.panes(tabId);
        if (first == second || !panes.contains(first) || !panes.contains(second))
            return failure("Pane swapping needs two different panes in the same tab.");
        model.tabs_[tabId].panes = swapLeafItems(model.tabs_[tabId].panes, first, second);
        return success();
    });
}
TabId LayoutModel::movePaneToWindow(TabId sourceTab, PaneId paneId, WindowId targetWindow,
                                    Placement placement)
{
    TabId destination;
    const auto result = transact([&](LayoutModel& model) {
        if (!model.panes(sourceTab).contains(paneId) || !model.window(targetWindow))
            return failure("The source pane or target window does not exist.");
        const auto* sourceGroup = model.group(model.groupForTab(sourceTab));
        if (!sourceGroup || sourceGroup->windowId == targetWindow)
            return failure("Window transfer needs a different target window.");
        const auto targetGroup = model.windows_[targetWindow].activeGroup;
        if (const auto* group = model.group(targetGroup)) {
            destination = group->activeTab;
            return model.movePane(sourceTab, paneId, destination, {}, placement);
        }
        if (placement != Placement::Left && placement != Placement::Right
            && placement != Placement::Top && placement != Placement::Bottom)
            return failure("Pane transfer needs a split direction.");
        const auto createdGroup = model.createGroupUnchecked(targetWindow);
        destination = model.extractPane(sourceTab, paneId, createdGroup);
        return destination.isNull() ? failure("The pane could not be transferred.") : success();
    });
    return result ? destination : TabId{};
}
TabId LayoutModel::extractPane(TabId sourceTab, PaneId paneId, GroupId targetGroup, Placement placement)
{
    TabId created;
    const auto result = transact([&](LayoutModel& model) {
        if (!model.panes(sourceTab).contains(paneId) || !model.group(targetGroup))
            return failure("The source pane or target group does not exist.");
        const auto sourceGroup = model.groupForTab(sourceTab);
        // Insert first so extraction of the last pane into its own group cannot
        // invalidate that destination during the transaction.
        created = QUuid::createUuid();
        const auto carried = findLeaf(model.tabs_[sourceTab].panes, paneId);
        const auto title = model.tabs_[sourceTab].title;
        TabRecord added;
        added.id = created; added.title = title; added.panes = carried; added.activePane = paneId;
        added.openedOrder = added.visitedOrder = model.nextTabOrder();
        model.tabs_.insert(created, std::move(added));
        model.groups_[sourceGroup].tabs.append(created);
        auto moved = model.moveTabUnchecked(created, targetGroup, placement, -1);
        if (!moved) return moved;
        auto& source = model.tabs_[sourceTab];
        source.panes = replace(source.panes, paneId, {});
        if (!source.panes) model.eraseTabUnchecked(sourceTab);
        else if (source.activePane == paneId) source.activePane = model.panes(sourceTab).first();
        model.eraseEmptyFloatingWindows();
        return success();
    });
    return result ? created : TabId{};
}
OperationResult LayoutModel::setSplitRatio(QUuid nodeId, double ratio)
{
    return transact([&](LayoutModel& model) {
        if (!std::isfinite(ratio) || ratio < 0.05 || ratio > 0.95)
            return failure("Split ratios must stay between 0.05 and 0.95.");
        bool found = false;
        for (auto& record : model.windows_) record.central = replaceRatio(record.central, nodeId, ratio, found);
        for (auto& record : model.tabs_) record.panes = replaceRatio(record.panes, nodeId, ratio, found);
        return found ? success() : failure("The split node does not exist.");
    });
}

OperationResult LayoutModel::validate() const
{
    if (orderCounter_ > maximumTabOrder) return failure("The ordering counter exceeds the exact JSON integer range.");
    if (windows_.size() > maximumRecords || groups_.size() > maximumRecords || tabs_.size() > maximumRecords)
        return failure("The layout exceeds the record limit.");
    if (windowOrder_.size() != windows_.size()) return failure("Window ordering is incomplete.");
    QSet<QUuid> objectIds;
    QSet<QUuid> orderedWindows;
    QHash<FamilyId, int> roots;
    QSet<FamilyId> families;
    for (auto id : windowOrder_) {
        if (!windows_.contains(id) || orderedWindows.contains(id)) return failure("Window ordering contains invalid references.");
        orderedWindows.insert(id);
    }
    auto registerId = [&](QUuid id) {
        if (id.isNull() || objectIds.contains(id)) return false;
        objectIds.insert(id);
        return true;
    };
    for (auto iterator = windows_.cbegin(); iterator != windows_.cend(); ++iterator) {
        const auto& record = iterator.value();
        if (iterator.key() != record.id || !registerId(record.id) || record.familyId.isNull())
            return failure("Window identities are invalid.");
        if (record.kind != WindowKind::Workbench && record.kind != WindowKind::Floating)
            return failure("A window kind is invalid.");
        families.insert(record.familyId);
        if (record.kind == WindowKind::Workbench) ++roots[record.familyId];
        if (!record.activeGroup.isNull() && (!groups_.contains(record.activeGroup)
            || groups_[record.activeGroup].windowId != record.id)) return failure("The active group is not in its window.");
    }
    for (auto family : families)
        if (roots.value(family) != 1) return failure("Each window family must have exactly one workbench root.");
    for (auto iterator = groups_.cbegin(); iterator != groups_.cend(); ++iterator) {
        const auto& record = iterator.value();
        if (iterator.key() != record.id || !registerId(record.id) || !windows_.contains(record.windowId))
            return failure("Group identities or window references are invalid.");
        if ((record.edge != Edge::Left && record.edge != Edge::Right && record.edge != Edge::Top && record.edge != Edge::Bottom)
            || (record.location != GroupLocation::Central && record.location != GroupLocation::Pinned
                && record.location != GroupLocation::AutoHide)) return failure("A group placement is invalid.");
        if (record.tabs.isEmpty() || !record.tabs.contains(record.activeTab))
            return failure("Groups must contain tabs and a valid active tab.");
        if (record.edgeExtent < 32 || record.edgeExtent > 32768) return failure("An edge extent is invalid.");
    }
    for (auto iterator = tabs_.cbegin(); iterator != tabs_.cend(); ++iterator) {
        if (iterator.key() != iterator->id || !registerId(iterator->id)) return failure("Tab identities are invalid.");
        if (iterator->colorId.size() > 128 || iterator->openedOrder > orderCounter_ || iterator->visitedOrder > orderCounter_)
            return failure("Tab appearance or ordering metadata is invalid.");
        if (iterator->peekSize && !validPeekSize(*iterator->peekSize))
            return failure("A peek size is outside the supported dimensions.");
        if ((iterator->verticalPeekSize && !validPeekPolicy(*iterator->verticalPeekSize))
            || (iterator->horizontalPeekSize && !validPeekPolicy(*iterator->horizontalPeekSize)))
            return failure("A directional peek size is outside the supported dimensions.");
        if (const auto& saved = iterator->pinnedPosition) {
            if (saved->window.isNull() || saved->group.isNull() || saved->groupIndex < 0
                || saved->groupIndex > maximumRecords || saved->tabIndex < 0 || saved->tabIndex > maximumRecords
                || saved->extent < 32 || saved->extent > 32768)
                return failure("A pinned position is malformed.");
        }
    }

    QSet<QUuid> treeIds;
    QSet<GroupId> placedGroups;
    QSet<TabId> placedTabs;
    QSet<PaneId> placedPanes;
    int nodeCount = 0;
    std::function<OperationResult(LayoutTree, int, const std::function<OperationResult(QUuid)>&)> walk;
    walk = [&](LayoutTree tree, int depth, const auto& visit) -> OperationResult {
        if (!tree) return success();
        if (depth > maximumTreeDepth || ++nodeCount > maximumRecords || tree->id.isNull()
            || treeIds.contains(tree->id) || objectIds.contains(tree->id))
            return failure("Tree nodes are duplicated, invalid or exceed the resource limit.");
        treeIds.insert(tree->id);
        if (tree->isLeaf()) {
            if (tree->itemId.isNull()) return failure("A tree leaf has no item.");
            return visit(tree->itemId);
        }
        if (!tree->itemId.isNull() || !tree->first || !tree->second
            || (tree->orientation != Qt::Horizontal && tree->orientation != Qt::Vertical)
            || !std::isfinite(tree->ratio) || tree->ratio < 0.05 || tree->ratio > 0.95)
            return failure("A split node is malformed.");
        auto result = walk(tree->first, depth + 1, visit);
        return result ? walk(tree->second, depth + 1, visit) : result;
    };
    for (const auto& windowRecord : windows_) {
        auto result = walk(windowRecord.central, 0, [&](QUuid groupId) {
            const auto* record = group(groupId);
            if (!record || record->windowId != windowRecord.id || record->location != GroupLocation::Central
                || placedGroups.contains(groupId)) return failure("Central groups have invalid or duplicate placement.");
            placedGroups.insert(groupId);
            return success();
        });
        if (!result) return result;
        for (auto groupId : windowRecord.edgeGroups) {
            const auto* record = group(groupId);
            if (!record || record->windowId != windowRecord.id || record->location == GroupLocation::Central
                || placedGroups.contains(groupId)) return failure("Edge groups have invalid or duplicate placement.");
            placedGroups.insert(groupId);
        }
        if ((!windowRecord.central && windowRecord.edgeGroups.isEmpty()) != windowRecord.activeGroup.isNull())
            return failure("A window's active group does not match its contents.");
    }
    if (placedGroups.size() != groups_.size()) return failure("The layout contains orphan groups.");
    for (const auto& record : groups_)
        for (auto tabId : record.tabs) {
            if (!tabs_.contains(tabId) || placedTabs.contains(tabId)) return failure("Tabs have invalid or duplicate placement.");
            placedTabs.insert(tabId);
        }
    if (placedTabs.size() != tabs_.size()) return failure("The layout contains orphan tabs.");
    for (const auto& record : tabs_) {
        if (!record.panes) return failure("A tab must contain at least one pane.");
        QSet<PaneId> localPanes;
        auto result = walk(record.panes, 0, [&](QUuid paneId) {
            if (objectIds.contains(paneId) || treeIds.contains(paneId) || placedPanes.contains(paneId))
                return failure("Pane identities are duplicated or invalid.");
            placedPanes.insert(paneId);
            localPanes.insert(paneId);
            return success();
        });
        if (!result) return result;
        if (!localPanes.contains(record.activePane)) return failure("A tab's active pane is invalid.");
    }
    for (auto id : treeIds)
        if (placedPanes.contains(id)) return failure("A pane identity collides with a tree node.");
    return success();
}

QJsonObject LayoutModel::toJson() const
{
    QJsonArray windows;
    QJsonArray groups;
    QJsonArray tabs;
    for (auto windowId : windowOrder_) {
        const auto& record = windows_[windowId];
        windows.append(QJsonObject{{"id", uuidText(record.id)}, {"familyId", uuidText(record.familyId)},
            {"kind", record.kind == WindowKind::Workbench ? "workbench" : "floating"},
            {"central", treeJson(record.central)}, {"edgeGroups", idArray(record.edgeGroups)},
            {"activeGroup", record.activeGroup.isNull() ? QString{} : uuidText(record.activeGroup)}});
        auto orderedGroups = centralGroups(windowId);
        orderedGroups += record.edgeGroups;
        for (auto groupId : orderedGroups) {
            const auto& groupRecord = groups_[groupId];
            groups.append(QJsonObject{{"id", uuidText(groupRecord.id)}, {"windowId", uuidText(groupRecord.windowId)},
                {"location", locationText(groupRecord.location)}, {"edge", edgeText(groupRecord.edge)},
                {"tabs", idArray(groupRecord.tabs)}, {"activeTab", uuidText(groupRecord.activeTab)},
                {"edgeExtent", groupRecord.edgeExtent}});
            for (auto tabId : groupRecord.tabs) {
                const auto& tabRecord = tabs_[tabId];
                QJsonObject tabObject{{"id", uuidText(tabRecord.id)}, {"title", tabRecord.title},
                    {"panes", treeJson(tabRecord.panes)}, {"activePane", uuidText(tabRecord.activePane)}};
                tabObject.insert("colorId", tabRecord.colorId);
                tabObject.insert("openedOrder", double(tabRecord.openedOrder));
                tabObject.insert("visitedOrder", double(tabRecord.visitedOrder));
                if (const auto& size = tabRecord.peekSize)
                    tabObject.insert("peekSize", QJsonObject{{"width", size->width()}, {"height", size->height()}});
                if (const auto& size = tabRecord.verticalPeekSize)
                    tabObject.insert("verticalPeekSize", peekPolicyJson(*size));
                if (const auto& size = tabRecord.horizontalPeekSize)
                    tabObject.insert("horizontalPeekSize", peekPolicyJson(*size));
                if (const auto& saved = tabRecord.pinnedPosition) {
                    tabObject.insert("pinnedPosition", QJsonObject{{"window", uuidText(saved->window)},
                        {"group", uuidText(saved->group)}, {"edge", edgeText(saved->edge)},
                        {"groupIndex", saved->groupIndex}, {"tabIndex", saved->tabIndex}, {"extent", saved->extent}});
                }
                tabs.append(tabObject);
            }
        }
    }
    return QJsonObject{{"schemaVersion", 1}, {"windows", windows}, {"groups", groups}, {"tabs", tabs},
        {"orderCounter", double(orderCounter_)}};
}
OperationResult LayoutModel::fromJson(const QJsonObject& json)
{
    if (!json.value("schemaVersion").isDouble() || json.value("schemaVersion").toDouble() != 1.0)
        return failure("Unsupported layout schema version.");
    for (const auto* key : {"windows", "groups", "tabs"})
        if (!json.value(QLatin1String(key)).isArray() || json.value(QLatin1String(key)).toArray().size() > maximumRecords)
            return failure("Layout records must be bounded arrays.");
    LayoutModel candidate;
    JsonReader reader;
    if (json.contains("orderCounter")) {
        const auto value = json.value("orderCounter");
        if (!value.isDouble() || value.toDouble() < 0 || value.toDouble() > double(maximumTabOrder)
            || std::floor(value.toDouble()) != value.toDouble()) return failure("Invalid ordering counter.");
        candidate.orderCounter_ = quint64(value.toDouble());
    }
    for (const auto& value : json.value("windows").toArray()) {
        if (!value.isObject()) return failure("A window record must be an object.");
        const auto object = value.toObject();
        WindowRecord record;
        record.id = reader.id(object, "id");
        record.familyId = reader.id(object, "familyId");
        const auto kind = object.value("kind").toString();
        if (kind != "workbench" && kind != "floating") reader.fail("Invalid window kind.");
        record.kind = kind == "workbench" ? WindowKind::Workbench : WindowKind::Floating;
        record.central = reader.tree(object.value("central"));
        record.edgeGroups = reader.ids(object, "edgeGroups");
        record.activeGroup = reader.id(object, "activeGroup", true);
        if (candidate.windows_.contains(record.id)) reader.fail("Duplicate window identity.");
        candidate.windows_.insert(record.id, record);
        candidate.windowOrder_.append(record.id);
    }
    for (const auto& value : json.value("groups").toArray()) {
        if (!value.isObject()) return failure("A group record must be an object.");
        const auto object = value.toObject();
        GroupRecord record;
        record.id = reader.id(object, "id");
        record.windowId = reader.id(object, "windowId");
        const auto location = object.value("location").toString();
        if (location != "central" && location != "pinned" && location != "autoHide") reader.fail("Invalid group location.");
        record.location = location == "central" ? GroupLocation::Central
            : location == "pinned" ? GroupLocation::Pinned : GroupLocation::AutoHide;
        const auto edge = object.value("edge").toString();
        if (edge != "left" && edge != "right" && edge != "top" && edge != "bottom") reader.fail("Invalid edge direction.");
        record.edge = edge == "left" ? Edge::Left : edge == "right" ? Edge::Right
            : edge == "top" ? Edge::Top : Edge::Bottom;
        record.tabs = reader.ids(object, "tabs");
        record.activeTab = reader.id(object, "activeTab");
        if (object.contains("edgeExtent")) {
            const auto extent = object.value("edgeExtent");
            if (!extent.isDouble() || extent.toDouble() < 32 || extent.toDouble() > 32768
                || std::floor(extent.toDouble()) != extent.toDouble()) return failure("An edge extent must be a bounded integer.");
            record.edgeExtent = extent.toInt();
        } else if (record.edge == Edge::Top || record.edge == Edge::Bottom) record.edgeExtent = 170;
        if (candidate.groups_.contains(record.id)) reader.fail("Duplicate group identity.");
        candidate.groups_.insert(record.id, record);
    }
    QVector<TabId> legacyOrdering;
    for (const auto& value : json.value("tabs").toArray()) {
        if (!value.isObject()) return failure("A tab record must be an object.");
        const auto object = value.toObject();
        TabRecord record;
        record.id = reader.id(object, "id");
        if (!object.value("title").isString()) reader.fail("A tab title must be a string.");
        record.title = object.value("title").toString();
        record.panes = reader.tree(object.value("panes"));
        record.activePane = reader.id(object, "activePane");
        if (object.contains("colorId") && (!object.value("colorId").isString() || object.value("colorId").toString().size() > 128))
            return failure("Invalid tab color identity.");
        record.colorId = object.value("colorId").toString();
        if (object.contains("openedOrder") != object.contains("visitedOrder"))
            return failure("Tab ordering values must be provided together.");
        for (auto key : {"openedOrder", "visitedOrder"}) {
            if (!object.contains(key)) continue;
            const auto value = object.value(key);
            if (!value.isDouble() || value.toDouble() < 0 || value.toDouble() > double(maximumTabOrder)
                || std::floor(value.toDouble()) != value.toDouble()) return failure("Invalid tab ordering value.");
            (QLatin1String(key) == QLatin1String("openedOrder") ? record.openedOrder : record.visitedOrder) = quint64(value.toDouble());
            candidate.orderCounter_ = std::max(candidate.orderCounter_, quint64(value.toDouble()));
        }
        if (!object.contains("openedOrder")) legacyOrdering.append(record.id);
        if (object.contains("peekSize")) {
            if (!object.value("peekSize").isObject()) return failure("A peek size must be an object.");
            const auto size = object.value("peekSize").toObject();
            for (const auto* key : {"width", "height"}) {
                const auto dimension = size.value(QLatin1String(key));
                const auto minimum = QLatin1String(key) == QLatin1String("width") ? minimumPeekWidth : minimumPeekHeight;
                const auto number = dimension.toDouble();
                if (!dimension.isDouble() || !std::isfinite(number) || number < minimum
                    || number > maximumPeekDimension || std::floor(number) != number)
                    return failure("Peek dimensions must be bounded integers.");
            }
            record.peekSize = QSize(size.value("width").toInt(), size.value("height").toInt());
        }
        for (const auto* key : {"verticalPeekSize", "horizontalPeekSize"}) {
            if (!object.contains(QLatin1String(key))) continue;
            PeekSizePolicy policy;
            const auto result = readPeekPolicy(object.value(QLatin1String(key)), policy);
            if (!result) return result;
            (QLatin1String(key) == QLatin1String("verticalPeekSize") ? record.verticalPeekSize : record.horizontalPeekSize) = policy;
        }
        if (object.contains("pinnedPosition")) {
            if (!object.value("pinnedPosition").isObject()) return failure("A pinned position must be an object.");
            const auto savedObject = object.value("pinnedPosition").toObject();
            TabRecord::PinnedPosition saved;
            saved.window = reader.id(savedObject, "window"); saved.group = reader.id(savedObject, "group");
            const auto edge = savedObject.value("edge").toString();
            if (edge != "left" && edge != "right" && edge != "top" && edge != "bottom")
                return failure("A pinned position has an invalid edge.");
            saved.edge = edge == "left" ? Edge::Left : edge == "right" ? Edge::Right
                : edge == "top" ? Edge::Top : Edge::Bottom;
            for (const auto* key : {"groupIndex", "tabIndex"}) {
                const auto value = savedObject.value(QLatin1String(key));
                if (!value.isDouble() || value.toDouble() < 0 || value.toDouble() > maximumRecords
                    || std::floor(value.toDouble()) != value.toDouble())
                    return failure("Pinned position indices must be bounded integers.");
            }
            saved.groupIndex = savedObject.value("groupIndex").toInt();
            saved.tabIndex = savedObject.value("tabIndex").toInt();
            if (savedObject.contains("extent")) {
                const auto extent = savedObject.value("extent");
                if (!extent.isDouble() || extent.toDouble() < 32 || extent.toDouble() > 32768
                    || std::floor(extent.toDouble()) != extent.toDouble()) return failure("A pinned position extent is invalid.");
                saved.extent = extent.toInt();
            }
            record.pinnedPosition = saved;
        }
        if (candidate.tabs_.contains(record.id)) reader.fail("Duplicate tab identity.");
        candidate.tabs_.insert(record.id, record);
    }
    if (!reader.error.isEmpty()) return failure(reader.error);
    for (const auto tab : legacyOrdering) {
        const auto order = candidate.nextTabOrder();
        candidate.tabs_[tab].openedOrder = candidate.tabs_[tab].visitedOrder = order;
    }
    auto result = candidate.validate();
    if (!result) return result;
    candidate.revision_ = revision_ + 1;
    *this = std::move(candidate);
    return success();
}

} // namespace window_system
