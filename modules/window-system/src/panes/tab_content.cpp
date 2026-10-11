#include "tab_content.hpp"
#include <QHBoxLayout>
#include <QLabel>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>

namespace window_system {
TabContent::TabContent(WindowManager* manager, TabId tab, QWidget* parent)
    : QFrame(parent), manager_(manager), tab_(tab), layout_(new QVBoxLayout(this))
{
    setObjectName("TabPage");
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(0);
    setMinimumSize(160, 100);
}
void TabContent::rebuild(const TabRecord& record,
                         const QHash<PaneId, QPointer<QWidget>>& contents)
{
    frames_.clear();
    if (tree_) { layout_->removeWidget(tree_); delete tree_; }
    tree_ = buildNode(record.panes, record, contents);
    if (tree_) layout_->insertWidget(top_ ? 1 : 0, tree_, 1);
}
QWidget* TabContent::buildNode(LayoutTree node, const TabRecord& record,
                              const QHash<PaneId, QPointer<QWidget>>& contents)
{
    if (!node) return nullptr;
    if (!node->isLeaf()) {
        auto* splitter = new QSplitter(node->orientation, this);
        splitter->setObjectName("PaneSplitter");
        splitter->setChildrenCollapsible(false);
        splitter->addWidget(buildNode(node->first, record, contents));
        splitter->addWidget(buildNode(node->second, record, contents));
        splitter->setSizes({qRound(1000 * node->ratio), qRound(1000 * (1 - node->ratio))});
        const auto id = node->id;
        QObject::connect(splitter, &QSplitter::splitterMoved, manager_,
            [this, splitter, id] {
                const auto sizes = splitter->sizes();
                manager_->execute("layout.setRatio", {{"node", id},
                    {"ratio", double(sizes.value(0)) / qMax(1, sizes.value(0) + sizes.value(1))}});
            });
        return splitter;
    }
    auto* frame = new QFrame(this);
    frame->setObjectName("PaneHost");
    frame->setProperty("paneId", node->itemId);
    frame->setProperty("tabId", tab_);
    frame->setProperty("active", node->itemId == record.activePane);
    frame->setMinimumSize(140, 85);
    auto* column = new QVBoxLayout(frame);
    column->setContentsMargins(1, 1, 1, 1);
    column->setSpacing(0);
    // Keep one-pane tabs uncluttered; multiple panes have explicit local focus.
    if (manager_->model().panes(tab_).size() > 1) {
        auto* header = new QFrame(frame);
        header->setObjectName("PaneHeader");
        auto* row = new QHBoxLayout(header);
        row->setContentsMargins(6, 0, 2, 0);
        row->addWidget(new QLabel(QStringLiteral("窗格 %1").arg(
            manager_->model().panes(tab_).indexOf(node->itemId) + 1), header));
        row->addStretch();
        auto* close = new QToolButton(header);
        close->setIcon(iconFor("close", manager_->theme()));
        close->setToolTip(QStringLiteral("关闭这个窗格"));
        close->setAccessibleName(close->toolTip());
        row->addWidget(close);
        const auto pane = node->itemId;
        QObject::connect(close, &QToolButton::clicked, manager_, [this, pane] {
            manager_->closePane(tab_, pane);
        });
        column->addWidget(header);
    }
    if (auto content = contents.value(node->itemId)) {
        column->addWidget(content);
        content->show();
    }
    frames_.insert(node->itemId, frame);
    return frame;
}
void TabContent::setTopBar(QWidget* widget)
{
    if (top_ == widget) return;
    if (top_) { layout_->removeWidget(top_); delete top_; }
    top_ = widget;
    if (top_) { layout_->insertWidget(0, top_); top_->show(); }
}
void TabContent::setBottomBar(QWidget* widget)
{
    if (bottom_ == widget) return;
    if (bottom_) { layout_->removeWidget(bottom_); delete bottom_; }
    bottom_ = widget;
    if (bottom_) { layout_->addWidget(bottom_); bottom_->show(); }
}
QWidget* TabContent::takeTopBar()
{
    auto* result = top_.data();
    if (result) { layout_->removeWidget(result); result->setParent(nullptr); }
    top_.clear();
    return result;
}
QWidget* TabContent::takeBottomBar()
{
    auto* result = bottom_.data();
    if (result) { layout_->removeWidget(result); result->setParent(nullptr); }
    bottom_.clear();
    return result;
}
QHash<PaneId, QWidget*> TabContent::paneFrames() const { return frames_; }
} // namespace window_system
