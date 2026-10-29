// SPDX-License-Identifier: MIT
#pragma once

#include "panes/split_tree_layout.hpp"
#include <QPointer>
#include <QQuickItem>

namespace window_system::quick {

class SplitHandleItem;

// Used for both groups and panes. The content host owns leaf objects; this
// adapter changes only their visual parent and never replaces their identity.
class SplitTreeItem final : public QQuickItem {
    Q_OBJECT
public:
    explicit SplitTreeItem(QQuickItem* parent = nullptr);
    ~SplitTreeItem() override;
    void setTree(LayoutTree tree);
    void setLeafItems(QHash<QUuid, QPointer<QQuickItem>> items);
    void setMinimumSizes(QHash<QUuid, QSize> minima);
    void setDividerMetrics(int width, int hitWidth);
    void setFocusedItem(QUuid item);
    [[nodiscard]] QSize minimumSize() const { return layout_.minimumSize(); }
    [[nodiscard]] QRect leafRect(QUuid item) const;
    [[nodiscard]] QRect dividerRect(QUuid node) const;

signals:
    void ratioRequested(QUuid node, double ratio);

protected:
    void geometryChange(const QRectF& now, const QRectF& previous) override;

private:
    friend class SplitHandleItem;
    void relayout();
    void requestRatio(QUuid node, QPointF position, qreal grabOffset);
    SplitTreeLayout layout_;
    QHash<QUuid, QPointer<QQuickItem>> leaves_;
    QHash<QUuid, SplitHandleItem*> handles_;
    int hitWidth_ = 6;
    bool layingOut_ = false;
    bool layoutPending_ = false;
};

} // namespace window_system::quick
