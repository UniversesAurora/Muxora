// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/layout_model.hpp>
#include <QRect>
#include <QSize>

namespace window_system {

// A compiled view of the model's existing tree. Minima are measured once by
// the content adapter; resizing only assigns rectangles, with no host calls.
class SplitTreeLayout final {
public:
    struct Node {
        QUuid id;
        QUuid item;
        Qt::Orientation orientation = Qt::Horizontal;
        double ratio = .5;
        int first = -1;
        int second = -1;
        QSize minimum;
        QRect area;
        QRect divider;
        bool visible = false;
        [[nodiscard]] bool leaf() const { return first < 0; }
    };

    void setTree(LayoutTree tree);
    void setMinimumSizes(QHash<QUuid, QSize> minima);
    void setDividerWidth(int width);
    // Projection changes only this view, never the persisted model tree.
    void setFocusedItem(QUuid item);
    void arrange(QRect bounds);
    [[nodiscard]] QSize minimumSize() const;
    [[nodiscard]] const QVector<Node>& nodes() const { return nodes_; }
    [[nodiscard]] int indexOf(QUuid node) const { return indexes_.value(node, -1); }
    [[nodiscard]] double ratioAt(int node, int dividerPosition) const;

private:
    int append(const LayoutTree& tree);
    QSize measure(int index);
    void place(int index, QRect area);
    void remeasure();
    LayoutTree tree_;
    QVector<Node> nodes_;
    QHash<QUuid, int> indexes_;
    QHash<QUuid, QSize> minima_;
    int dividerWidth_ = 1;
    QUuid focusedItem_;
    int focusedIndex_ = -1;
};

} // namespace window_system
