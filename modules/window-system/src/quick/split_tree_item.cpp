// SPDX-License-Identifier: MIT
#include "split_tree_item.hpp"
#include <QCursor>
#include <QMouseEvent>
#include <QSet>
#include <algorithm>
#include <utility>

namespace window_system::quick {

class SplitHandleItem final : public QQuickItem {
public:
    SplitHandleItem(SplitTreeItem* tree, QUuid node) : QQuickItem(tree), tree_(tree), node_(node)
    {
        setAcceptedMouseButtons(Qt::LeftButton);
        setZ(1);
    }

    void setDivider(QRect divider, Qt::Orientation orientation, int hitWidth)
    {
        orientation_ = orientation;
        divider_ = divider;
        const bool horizontal = orientation == Qt::Horizontal;
        const int pad = std::max(0, hitWidth - (horizontal ? divider.width() : divider.height()));
        const auto hit = horizontal ? divider.adjusted(-pad / 2, 0, pad - pad / 2, 0)
                                    : divider.adjusted(0, -pad / 2, 0, pad - pad / 2);
        setPosition(hit.topLeft());
        setSize(hit.size());
        setCursor(horizontal ? Qt::SplitHCursor : Qt::SplitVCursor);
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        const auto point = mapToItem(tree_, event->position());
        grabOffset_ = orientation_ == Qt::Horizontal ? point.x() - divider_.left() : point.y() - divider_.top();
        event->accept();
    }
    void mouseMoveEvent(QMouseEvent* event) override
    {
        event->accept();
        // Recompute in the tree's coordinates: this handle moves during drag.
        tree_->requestRatio(node_, mapToItem(tree_, event->position()), grabOffset_);
    }
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        event->accept();
        tree_->requestRatio(node_, mapToItem(tree_, event->position()), grabOffset_);
    }

private:
    SplitTreeItem* tree_;
    QUuid node_;
    Qt::Orientation orientation_ = Qt::Horizontal;
    QRect divider_;
    qreal grabOffset_ = 0;
};

SplitTreeItem::SplitTreeItem(QQuickItem* parent) : QQuickItem(parent) {}

SplitTreeItem::~SplitTreeItem()
{
    for (const auto& item : std::as_const(leaves_)) if (item && item->parentItem() == this) item->setParentItem(nullptr);
}

void SplitTreeItem::setTree(LayoutTree tree)
{
    layout_.setTree(std::move(tree));
    relayout();
}

void SplitTreeItem::setLeafItems(QHash<QUuid, QPointer<QQuickItem>> items)
{
    const auto previous = leaves_;
    const QPointer<SplitTreeItem> alive(this);
    for (auto it = previous.cbegin(); it != previous.cend(); ++it)
        if (it.value() && items.value(it.key()) != it.value() && it.value()->parentItem() == this) {
            it.value()->setParentItem(nullptr);
            if (!alive) return;
        }
    leaves_ = std::move(items);
    const auto adopted = leaves_;
    for (const auto& item : adopted) if (item) {
        item->setParentItem(this);
        if (!alive) return;
    }
    relayout();
}

void SplitTreeItem::setMinimumSizes(QHash<QUuid, QSize> minima)
{
    layout_.setMinimumSizes(std::move(minima));
    relayout();
}

void SplitTreeItem::setDividerMetrics(int width, int hitWidth)
{
    layout_.setDividerWidth(width);
    hitWidth_ = hitWidth;
    relayout();
}

void SplitTreeItem::setFocusedItem(QUuid item)
{
    layout_.setFocusedItem(item);
    relayout();
}

void SplitTreeItem::geometryChange(const QRectF& now, const QRectF& previous)
{
    const QPointer<SplitTreeItem> alive(this);
    QQuickItem::geometryChange(now, previous);
    if (alive && now.size() != previous.size()) relayout();
}

void SplitTreeItem::relayout()
{
    if (layingOut_) { layoutPending_ = true; return; }
    layingOut_ = true;
    const QPointer<SplitTreeItem> alive(this);
    do {
        layoutPending_ = false;
        layout_.arrange(QRect(QPoint(), size().toSize()));
        QSet<QUuid> live, liveLeaves;
        // Host geometry signals can edit the model synchronously. Keep this
        // pass immutable and restart with the latest state after the callback.
        const auto nodes = layout_.nodes();
        for (const auto& node : nodes) {
            if (node.leaf()) {
                liveLeaves.insert(node.item);
                const auto item = leaves_.value(node.item);
                if (!item) continue;
                item->setVisible(node.visible);
                if (!alive) return;
                if (layoutPending_) break;
                if (node.visible && item) {
                    item->setPosition(node.area.topLeft());
                    if (!alive) return;
                    if (layoutPending_) break;
                    if (item) item->setSize(node.area.size());
                    if (!alive) return;
                    if (layoutPending_) break;
                }
                continue;
            }
            live.insert(node.id);
            auto*& handle = handles_[node.id];
            if (!handle) handle = new SplitHandleItem(this, node.id);
            handle->setVisible(node.visible);
            if (node.visible) handle->setDivider(node.divider, node.orientation, hitWidth_);
        }
        if (layoutPending_) continue;
        const auto leaves = leaves_;
        for (auto it = leaves.cbegin(); it != leaves.cend(); ++it) {
            if (it.value() && !liveLeaves.contains(it.key())) it.value()->setVisible(false);
            if (!alive) return;
            if (layoutPending_) break;
        }
        if (layoutPending_) continue;
        for (auto it = handles_.begin(); it != handles_.end();)
            if (!live.contains(it.key())) { delete it.value(); it = handles_.erase(it); }
            else ++it;
    } while (layoutPending_);
    layingOut_ = false;
}

void SplitTreeItem::requestRatio(QUuid id, QPointF position, qreal grabOffset)
{
    const int index = layout_.indexOf(id);
    if (index < 0) return; // A model edit may remove the captured divider.
    const auto& node = layout_.nodes()[index];
    const int coordinate = qRound((node.orientation == Qt::Horizontal ? position.x() : position.y()) - grabOffset);
    const double ratio = layout_.ratioAt(index, coordinate);
    // The caller commits through LayoutModel; rejected edits leave this view
    // unchanged. Ratio validity belongs to the model, not another UI rule.
    emit ratioRequested(id, ratio);
}

QRect SplitTreeItem::leafRect(QUuid id) const
{
    for (const auto& node : layout_.nodes()) if (node.leaf() && node.item == id && node.visible) return node.area;
    return {};
}

QRect SplitTreeItem::dividerRect(QUuid id) const
{
    const int index = layout_.indexOf(id);
    return index >= 0 && layout_.nodes()[index].visible ? layout_.nodes()[index].divider : QRect{};
}

} // namespace window_system::quick
