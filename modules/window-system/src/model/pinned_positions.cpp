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

LayoutTree copySkeleton(LayoutTree tree) {
    if (!tree)
        return {};
    auto node = *tree;
    node.id = QUuid::createUuid();
    node.first = copySkeleton(tree->first);
    node.second = copySkeleton(tree->second);
    return std::make_shared<LayoutNode>(std::move(node));
}

void collect(LayoutTree tree, QVector<GroupId> &groups) {
    if (!tree)
        return;
    if (tree->isLeaf()) {
        groups.append(tree->itemId);
        return;
    }
    collect(tree->first, groups);
    collect(tree->second, groups);
}

bool contains(LayoutTree tree, GroupId group) {
    return tree && (tree->isLeaf() ? tree->itemId == group
                                   : contains(tree->first, group) || contains(tree->second, group));
}

LayoutTree prune(LayoutTree tree, const QSet<GroupId> &allowed) {
    if (!tree)
        return {};
    if (tree->isLeaf())
        return allowed.contains(tree->itemId) ? tree : LayoutTree{};
    const auto first = prune(tree->first, allowed), second = prune(tree->second, allowed);
    if (!first)
        return second;
    if (!second)
        return first;
    if (first == tree->first && second == tree->second)
        return tree;
    auto node = *tree;
    node.first = first;
    node.second = second;
    return std::make_shared<LayoutNode>(std::move(node));
}

LayoutTree replaceNode(LayoutTree tree, QUuid nodeId, const LayoutTree &replacement) {
    if (!tree || tree->id == nodeId)
        return tree ? replacement : LayoutTree{};
    if (tree->isLeaf())
        return tree;
    const auto first = replaceNode(tree->first, nodeId, replacement);
    const auto second = replaceNode(tree->second, nodeId, replacement);
    if (first == tree->first && second == tree->second)
        return tree;
    auto node = *tree;
    node.first = first;
    node.second = second;
    return std::make_shared<LayoutNode>(std::move(node));
}

struct Ancestor {
    LayoutTree branch;
    LayoutTree sibling;
    bool first;
};

bool pathTo(LayoutTree tree, GroupId group, QVector<Ancestor> &path) {
    if (!tree)
        return false;
    if (tree->isLeaf())
        return tree->itemId == group;
    path.append({tree, tree->second, true});
    if (pathTo(tree->first, group, path))
        return true;
    path.removeLast();
    path.append({tree, tree->first, false});
    if (pathTo(tree->second, group, path))
        return true;
    path.removeLast();
    return false;
}

// The smallest surviving sibling ancestor preserves every edit inside that
// live subtree. Only its missing sibling slot is reconstructed, never a saved
// whole-side layout or another group's current split ratio.
LayoutTree anchorFor(LayoutTree tree, const QSet<GroupId> &siblings) {
    if (!tree)
        return {};
    if (tree->isLeaf())
        return siblings.contains(tree->itemId) ? tree : LayoutTree{};
    const auto first = anchorFor(tree->first, siblings), second = anchorFor(tree->second, siblings);
    return first && second ? tree : first ? first : second;
}

struct LiveSplit {
    QSet<GroupId> first;
    QSet<GroupId> second;
    LayoutTree tree;
};

using LiveSplits = QHash<QSet<GroupId>, LiveSplit>;

QSet<GroupId> indexLiveSplits(LayoutTree tree, LiveSplits &splits) {
    if (!tree)
        return {};
    if (tree->isLeaf())
        return {tree->itemId};
    auto first = indexLiveSplits(tree->first, splits);
    auto second = indexLiveSplits(tree->second, splits);
    auto groups = first | second;
    splits.insert(groups, {std::move(first), std::move(second), tree});
    return groups;
}

// Hidden-only branches keep their saved proportions. A branch whose two sides
// still exist follows the current split, including changes made while a third
// group was hidden. Otherwise hiding all remaining groups would revive an old
// ratio when they return. Set keys describe membership, not node identity:
// remembered nodes intentionally have identities separate from visible nodes.
QSet<GroupId> refreshLiveSplits(LayoutTree &remembered, const QSet<GroupId> &liveGroups,
                                const LiveSplits &splits) {
    if (!remembered)
        return {};
    if (remembered->isLeaf())
        return liveGroups.contains(remembered->itemId) ? QSet<GroupId>{remembered->itemId} : QSet<GroupId>{};
    auto node = *remembered;
    const auto first = refreshLiveSplits(node.first, liveGroups, splits);
    const auto second = refreshLiveSplits(node.second, liveGroups, splits);
    const auto groups = first | second;
    if (!first.isEmpty() && !second.isEmpty()) {
        const auto current = splits.constFind(groups);
        if (current != splits.cend() && current->first == first && current->second == second) {
            node.orientation = current->tree->orientation;
            node.ratio = current->tree->ratio;
        }
    }
    if (node.first != remembered->first || node.second != remembered->second ||
        node.orientation != remembered->orientation || node.ratio != remembered->ratio)
        remembered = std::make_shared<LayoutNode>(std::move(node));
    return groups;
}

bool insertRememberedSlot(LayoutTree &tree, LayoutTree remembered, GroupId group) {
    if (contains(tree, group))
        return true;
    QVector<Ancestor> path;
    if (!pathTo(remembered, group, path))
        return false;
    if (!tree) {
        tree = leaf(group);
        return true;
    }
    for (qsizetype index = path.size(); index-- > 0;) {
        const auto &step = path[index];
        QVector<GroupId> siblings;
        collect(step.sibling, siblings);
        const auto anchor = anchorFor(tree, QSet<GroupId>(siblings.cbegin(), siblings.cend()));
        if (!anchor)
            continue;
        LayoutNode node;
        node.id = QUuid::createUuid();
        node.orientation = step.branch->orientation;
        node.ratio = step.branch->ratio;
        node.first = step.first ? leaf(group) : anchor;
        node.second = step.first ? anchor : leaf(group);
        tree = replaceNode(tree, anchor->id, std::make_shared<LayoutNode>(std::move(node)));
        return true;
    }
    return false;
}

QSet<GroupId> savedGroups(const LayoutModel &model, WindowId window, Edge edge, bool onlyHidden) {
    QSet<GroupId> groups;
    for (const auto &current : model.groups()) {
        if (current.windowId != window || current.edge != edge ||
            current.location == GroupLocation::Central ||
            (onlyHidden && current.location != GroupLocation::AutoHide))
            continue;
        for (const auto tabId : current.tabs) {
            const auto *tab = model.tab(tabId);
            if (!tab)
                continue;
            const auto &saved = tab->pinnedPosition;
            if (!saved || saved->window != window || saved->edge != edge)
                continue;
            const auto *previous = model.group(saved->group);
            if (previous && (previous->windowId != window || previous->edge != edge ||
                             previous->location == GroupLocation::Central))
                continue;
            groups.insert(saved->group);
        }
    }
    return groups;
}

} // namespace

void LayoutModel::rememberPinnedLayoutUnchecked(WindowId window, Edge edge) {
    synchronizePinnedLayouts(window);
    auto &record = windows_[window];
    const auto live = record.pinnedLayouts[size_t(edge)];
    auto &remembered = record.pinnedRestoreLayouts[size_t(edge)];
    if (!live)
        return;
    if (!remembered) {
        remembered = copySkeleton(live);
        return;
    }
    // A group deliberately relocated since the previous capture has no saved
    // position. Remove its obsolete slot before merging its actual new path.
    remembered = prune(remembered, savedGroups(*this, window, edge, false));
    if (!remembered) {
        remembered = copySkeleton(live);
        return;
    }
    QVector<GroupId> visible;
    collect(live, visible);
    for (const auto group : visible) {
        if (insertRememberedSlot(remembered, live, group))
            continue;
        // All old anchors were removed. Keep a bounded fallback relation for
        // the still-dormant groups without replacing current live structure.
        LayoutNode node;
        node.id = QUuid::createUuid();
        node.orientation = edge == Edge::Left || edge == Edge::Right ? Qt::Horizontal : Qt::Vertical;
        node.first = remembered;
        node.second = leaf(group);
        remembered = std::make_shared<LayoutNode>(std::move(node));
    }
    LiveSplits splits;
    const auto liveGroups = indexLiveSplits(live, splits);
    refreshLiveSplits(remembered, liveGroups, splits);
}

void LayoutModel::restorePinnedLayoutUnchecked(WindowId window, Edge edge, GroupId group) {
    auto &record = windows_[window];
    insertRememberedSlot(record.pinnedLayouts[size_t(edge)], record.pinnedRestoreLayouts[size_t(edge)],
                         group);
}

void LayoutModel::reconcilePinnedRestoreLayouts() {
    for (auto &window : windows_)
        for (const auto edge : {Edge::Left, Edge::Right, Edge::Top, Edge::Bottom}) {
            auto &remembered = window.pinnedRestoreLayouts[size_t(edge)];
            if (!remembered)
                continue;
            const auto hidden = savedGroups(*this, window.id, edge, true);
            if (hidden.isEmpty()) {
                remembered.reset();
                continue;
            }
            auto allowed = hidden;
            QVector<GroupId> visible;
            collect(window.pinnedLayouts[size_t(edge)], visible);
            for (const auto group : visible)
                allowed.insert(group);
            remembered = prune(remembered, allowed);
            QVector<GroupId> remaining;
            collect(remembered, remaining);
            if (std::none_of(remaining.cbegin(), remaining.cend(),
                             [&hidden](GroupId group) { return hidden.contains(group); }))
                remembered.reset();
        }
}

} // namespace window_system
