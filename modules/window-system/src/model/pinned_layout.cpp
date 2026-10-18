// SPDX-License-Identifier: MIT
#include <window_system/layout_model.hpp>
#include <QSet>
#include <algorithm>

namespace window_system {
namespace {
LayoutTree leaf(GroupId group) {
    LayoutNode node;
    node.id = QUuid::createUuid();
    node.itemId = group;
    return std::make_shared<LayoutNode>(std::move(node));
}
LayoutTree prune(LayoutTree tree, const QSet<GroupId> &allowed, QSet<GroupId> &placed) {
    if (!tree)
        return {};
    if (tree->isLeaf()) {
        if (!allowed.contains(tree->itemId))
            return {};
        placed.insert(tree->itemId);
        return tree;
    }
    auto first = prune(tree->first, allowed, placed);
    auto second = prune(tree->second, allowed, placed);
    if (!first)
        return second;
    if (!second)
        return first;
    if (first == tree->first && second == tree->second)
        return tree;
    auto node = *tree;
    node.first = std::move(first);
    node.second = std::move(second);
    return std::make_shared<LayoutNode>(std::move(node));
}
LayoutTree insert(LayoutTree tree, GroupId target, GroupId added, Qt::Orientation axis, bool before,
                  const QHash<GroupId, GroupRecord> &records) {
    if (tree->isLeaf()) {
        if (tree->itemId != target)
            return tree;
        LayoutNode split;
        split.id = QUuid::createUuid();
        split.orientation = axis;
        split.first = before ? leaf(added) : tree;
        split.second = before ? tree : leaf(added);
        const double addedExtent = records[added].edgeExtent;
        const double targetExtent = records[target].edgeExtent;
        split.ratio =
            std::clamp((before ? addedExtent : targetExtent) / (addedExtent + targetExtent), .05, .95);
        return std::make_shared<LayoutNode>(std::move(split));
    }
    const auto first = insert(tree->first, target, added, axis, before, records);
    const auto second = insert(tree->second, target, added, axis, before, records);
    if (first == tree->first && second == tree->second)
        return tree;
    auto node = *tree;
    node.first = first;
    node.second = second;
    return std::make_shared<LayoutNode>(std::move(node));
}
LayoutTree balanced(const QVector<GroupId> &groups, qsizetype begin, qsizetype end, Qt::Orientation axis,
                    const QHash<GroupId, GroupRecord> &records) {
    if (begin == end)
        return {};
    if (end - begin == 1)
        return leaf(groups[begin]);
    const auto middle = begin + (end - begin) / 2;
    LayoutNode node;
    node.id = QUuid::createUuid();
    node.orientation = axis;
    node.first = balanced(groups, begin, middle, axis, records);
    node.second = balanced(groups, middle, end, axis, records);
    double firstExtent = 0, totalExtent = 0;
    for (auto index = begin; index < end; ++index) {
        const double extent = records[groups[index]].edgeExtent;
        totalExtent += extent;
        if (index < middle)
            firstExtent += extent;
    }
    node.ratio = std::clamp(firstExtent / totalExtent, .05, .95);
    return std::make_shared<LayoutNode>(std::move(node));
}
} // namespace
LayoutTree LayoutModel::pinnedLayout(WindowId id, Edge edge) const {
    const auto *record = window(id);
    return record && edge >= Edge::Left && edge <= Edge::Bottom ? record->pinnedLayouts[size_t(edge)]
                                                                : LayoutTree{};
}
void LayoutModel::synchronizePinnedLayouts(WindowId onlyWindow) {
    for (auto &window : windows_) {
        if (!onlyWindow.isNull() && window.id != onlyWindow)
            continue;
        for (const auto edge : {Edge::Left, Edge::Right, Edge::Top, Edge::Bottom}) {
            QVector<GroupId> ordered;
            for (const auto id : window.edgeGroups) {
                const auto *record = group(id);
                if (record && record->windowId == window.id && record->location == GroupLocation::Pinned &&
                    record->edge == edge)
                    ordered.append(id);
            }
            if (edge == Edge::Right || edge == Edge::Bottom)
                std::reverse(ordered.begin(), ordered.end());
            const QSet<GroupId> allowed(ordered.cbegin(), ordered.cend());
            QSet<GroupId> placed;
            auto &tree = window.pinnedLayouts[size_t(edge)];
            tree = prune(tree, allowed, placed);
            const auto axis = edge == Edge::Left || edge == Edge::Right ? Qt::Horizontal : Qt::Vertical;
            if (!tree) {
                tree = balanced(ordered, 0, ordered.size(), axis, groups_);
                continue;
            }
            for (qsizetype index = 0; index < ordered.size(); ++index) {
                const auto id = ordered[index];
                if (placed.contains(id))
                    continue;
                GroupId neighbor;
                bool before = false;
                for (qsizetype next = index + 1; next < ordered.size(); ++next)
                    if (placed.contains(ordered[next])) {
                        neighbor = ordered[next];
                        before = true;
                        break;
                    }
                if (neighbor.isNull())
                    for (qsizetype previous = index; previous > 0; --previous)
                        if (placed.contains(ordered[previous - 1])) {
                            neighbor = ordered[previous - 1];
                            break;
                        }
                tree = insert(tree, neighbor, id, axis, before, groups_);
                placed.insert(id);
            }
        }
    }
}
} // namespace window_system
