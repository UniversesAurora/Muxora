// SPDX-License-Identifier: MIT
#include "split_tree_layout.hpp"
#include <algorithm>

namespace window_system {

int SplitTreeLayout::append(const LayoutTree& tree)
{
    const int index = int(nodes_.size());
    nodes_.append({tree->id, tree->itemId, tree->orientation, tree->ratio});
    indexes_.insert(tree->id, index);
    if (!tree->isLeaf()) {
        const int first = append(tree->first);
        const int second = append(tree->second);
        nodes_[index].first = first;
        nodes_[index].second = second;
    }
    return index;
}

void SplitTreeLayout::setTree(LayoutTree tree)
{
    if (tree_ == tree) return;
    tree_ = std::move(tree);
    nodes_.clear();
    indexes_.clear();
    if (tree_) append(tree_);
    remeasure();
}

void SplitTreeLayout::setMinimumSizes(QHash<QUuid, QSize> minima)
{
    if (minima_ == minima) return;
    minima_ = std::move(minima);
    remeasure();
}

void SplitTreeLayout::setDividerWidth(int width)
{
    width = std::max(0, width);
    if (dividerWidth_ == width) return;
    dividerWidth_ = width;
    remeasure();
}

void SplitTreeLayout::setFocusedItem(QUuid item)
{
    focusedItem_ = item;
    focusedIndex_ = -1;
    if (item.isNull()) return;
    for (int index = 0; index < nodes_.size(); ++index)
        if (nodes_[index].leaf() && nodes_[index].item == item) { focusedIndex_ = index; break; }
}

QSize SplitTreeLayout::measure(int index)
{
    auto& node = nodes_[index];
    if (node.leaf()) return node.minimum = minima_.value(node.item, QSize(0, 0)).expandedTo(QSize(0, 0));
    const auto first = measure(node.first), second = measure(node.second);
    node.minimum = node.orientation == Qt::Horizontal
        ? QSize(first.width() + dividerWidth_ + second.width(), std::max(first.height(), second.height()))
        : QSize(std::max(first.width(), second.width()), first.height() + dividerWidth_ + second.height());
    return node.minimum;
}

void SplitTreeLayout::remeasure()
{
    if (!nodes_.isEmpty()) measure(0);
    setFocusedItem(focusedItem_);
}

QSize SplitTreeLayout::minimumSize() const
{
    return nodes_.isEmpty() ? QSize(0, 0) : nodes_[focusedIndex_ < 0 ? 0 : focusedIndex_].minimum;
}

void SplitTreeLayout::place(int index, QRect area)
{
    auto& node = nodes_[index];
    node.visible = true;
    node.area = area;
    if (node.leaf()) return;
    const bool horizontal = node.orientation == Qt::Horizontal;
    const int length = horizontal ? area.width() : area.height();
    const int available = std::max(0, length - dividerWidth_);
    const auto& firstNode = nodes_[node.first];
    const auto& secondNode = nodes_[node.second];
    const int lower = horizontal ? firstNode.minimum.width() : firstNode.minimum.height();
    const int upper = available - (horizontal ? secondNode.minimum.width() : secondNode.minimum.height());
    // The root was expanded to its subtree minimum, so this range is valid.
    const int firstLength = std::clamp(qRound(available * node.ratio), lower, upper);
    QRect first = area, second = area;
    if (horizontal) {
        first.setWidth(firstLength);
        node.divider = QRect(area.left() + firstLength, area.top(), dividerWidth_, area.height());
        second.setLeft(node.divider.right() + 1);
    } else {
        first.setHeight(firstLength);
        node.divider = QRect(area.left(), area.top() + firstLength, area.width(), dividerWidth_);
        second.setTop(node.divider.bottom() + 1);
    }
    place(node.first, first);
    place(node.second, second);
}

void SplitTreeLayout::arrange(QRect bounds)
{
    for (auto& node : nodes_) { node.visible = false; node.area = {}; node.divider = {}; }
    if (nodes_.isEmpty()) return;
    bounds.setSize(bounds.size().expandedTo(minimumSize()));
    place(focusedIndex_ < 0 ? 0 : focusedIndex_, bounds);
}

double SplitTreeLayout::ratioAt(int index, int position) const
{
    const auto& node = nodes_[index];
    const bool horizontal = node.orientation == Qt::Horizontal;
    const int extent = (horizontal ? node.area.width() : node.area.height()) - dividerWidth_;
    if (extent <= 0) return node.ratio;
    const int offset = position - (horizontal ? node.area.left() : node.area.top());
    const auto& first = nodes_[node.first];
    const auto& second = nodes_[node.second];
    const int lower = horizontal ? first.minimum.width() : first.minimum.height();
    const int upper = extent - (horizontal ? second.minimum.width() : second.minimum.height());
    return double(std::clamp(offset, lower, upper)) / extent;
}

} // namespace window_system
