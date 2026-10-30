// SPDX-License-Identifier: MIT
#include "tab_content_item.hpp"
#include "content_host.hpp"
#include "split_tree_item.hpp"
#include <window_system/window_manager.hpp>

namespace window_system::quick {
namespace {

// Keeps every child at its own size: pane content fills its frame.
class PaneFrameItem final : public QQuickItem {
public:
    explicit PaneFrameItem(QQuickItem* parent) : QQuickItem(parent) { setClip(true); }

protected:
    void geometryChange(const QRectF& now, const QRectF& previous) override
    {
        QQuickItem::geometryChange(now, previous);
        if (now.size() != previous.size()) fill();
    }
    void itemChange(ItemChange change, const ItemChangeData& data) override
    {
        QQuickItem::itemChange(change, data);
        if (change == ItemChildAddedChange) fill();
    }

private:
    void fill()
    {
        for (auto* child : childItems()) { child->setPosition({}); child->setSize(size()); }
    }
};

} // namespace

TabContentItem::TabContentItem(WindowManager& manager, ContentHost& host, TabId tab, QQuickItem* parent)
    : QQuickItem(parent), manager_(manager), host_(host), tab_(tab), split_(new SplitTreeItem(this))
{
    const auto theme = manager.interfaceTheme();
    split_->setDividerMetrics(std::max(1, theme.metrics.splitterWidth), 6);
    connect(this, &QQuickItem::widthChanged, split_, [this] { split_->setSize(size()); });
    connect(this, &QQuickItem::heightChanged, split_, [this] { split_->setSize(size()); });
    connect(split_, &SplitTreeItem::ratioRequested, this, [this](QUuid node, double ratio) {
        const auto* record = manager_.model().group(manager_.model().groupForTab(tab_));
        if (record) manager_.execute(QStringLiteral("layout.setRatio"), {{"node", node}, {"ratio", ratio}}, record->windowId);
    });
}

TabContentItem::~TabContentItem()
{
    // Content outlives this page; leave it to the host, unparented.
    for (auto it = frames_.cbegin(); it != frames_.cend(); ++it)
        if (it.value() && host_.item(it.key()) && host_.item(it.key())->parentItem() == it.value()) host_.unmount(it.key());
}

QQuickItem* TabContentItem::frame(PaneId pane)
{
    auto& frame = frames_[pane];
    if (!frame) frame = new PaneFrameItem(split_);
    return frame;
}

void TabContentItem::sync()
{
    const auto* record = manager_.model().tab(tab_);
    if (!record) return;
    const auto panes = manager_.model().panes(tab_);
    QHash<QUuid, QPointer<QQuickItem>> leaves;
    for (const auto pane : panes) {
        auto* target = frame(pane);
        leaves.insert(pane, target);
        if (host_.item(pane) && host_.item(pane)->parentItem() != target) host_.mount(pane, target);
    }
    for (auto it = frames_.begin(); it != frames_.end();) {
        if (panes.contains(it.key())) { ++it; continue; }
        if (it.value()) it.value()->deleteLater();
        it = frames_.erase(it);
    }
    split_->setTree(record->panes);
    split_->setLeafItems(leaves);
    split_->setSize(size());
}

QSize TabContentItem::minimumSize() const { return split_->minimumSize(); }

} // namespace window_system::quick
