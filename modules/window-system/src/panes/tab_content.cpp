#include "tab_content.hpp"
#include <QSplitter>
#include <QStyle>
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
                         const QHash<PaneId, QPointer<QWidget>>& contents,
                         const QHash<PaneId, QPointer<QWidget>>& paneTopBars,
                         const QHash<PaneId, QPointer<QWidget>>& paneBottomBars)
{
    // Pane bars occupy the tab's two shared slots. They never enter the split
    // tree, so changing focus cannot change pane dimensions or create a title row.
    paneTopBars_.clear();
    paneBottomBars_.clear();
    for (const auto pane : manager_->model().panes(tab_)) {
        if (auto bar = paneTopBars.value(pane)) paneTopBars_.insert(pane, bar);
        if (auto bar = paneBottomBars.value(pane)) paneBottomBars_.insert(pane, bar);
    }
    activePane_ = record.activePane;
    frames_.clear();
    if (tree_) { layout_->removeWidget(tree_); delete tree_; }
    tree_ = buildNode(record.panes, record, contents);
    const int topIndex = visibleTop_ ? layout_->indexOf(visibleTop_) : -1;
    if (tree_) layout_->insertWidget(topIndex >= 0 ? topIndex + 1 : 0, tree_, 1);
    updateBars();
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
    if (top_) {
        if (visibleTop_ == top_) { layout_->removeWidget(top_); visibleTop_.clear(); }
        delete top_;
    }
    top_ = widget;
    if (top_) { top_->setParent(this); top_->hide(); }
    updateBars();
}
void TabContent::setBottomBar(QWidget* widget)
{
    if (bottom_ == widget) return;
    if (bottom_) {
        if (visibleBottom_ == bottom_) { layout_->removeWidget(bottom_); visibleBottom_.clear(); }
        delete bottom_;
    }
    bottom_ = widget;
    if (bottom_) { bottom_->setParent(this); bottom_->hide(); }
    updateBars();
}
QWidget* TabContent::takeTopBar()
{
    auto* result = top_.data();
    if (result) {
        if (visibleTop_ == result) { layout_->removeWidget(result); visibleTop_.clear(); }
        result->setParent(nullptr);
    }
    top_.clear();
    updateBars();
    return result;
}
QWidget* TabContent::takeBottomBar()
{
    auto* result = bottom_.data();
    if (result) {
        if (visibleBottom_ == result) { layout_->removeWidget(result); visibleBottom_.clear(); }
        result->setParent(nullptr);
    }
    bottom_.clear();
    updateBars();
    return result;
}
QHash<PaneId, QWidget*> TabContent::paneFrames() const { return frames_; }

void TabContent::setActivePane(PaneId pane)
{
    if (activePane_ == pane) return;
    activePane_ = pane;
    for (auto* frame : frames_) {
        frame->setProperty("active", frame->property("paneId").toUuid() == pane);
        frame->style()->unpolish(frame);
        frame->style()->polish(frame);
        frame->update();
    }
    updateBars();
}

void TabContent::replaceVisibleBar(QPointer<QWidget>& slot, QWidget* next, bool top)
{
    // A manager refresh parks the same widget before rebuilding. Checking only
    // pointer equality would leave it hidden and detached from this layout.
    if (slot && slot != next) {
        if (slot->parentWidget() == this) {
            layout_->removeWidget(slot);
            slot->hide();
        }
    }
    slot = next;
    if (!slot) return;
    slot->setParent(this);
    layout_->removeWidget(slot);
    if (top) layout_->insertWidget(0, slot);
    else layout_->addWidget(slot);
    slot->show();
}

void TabContent::updateBars()
{
    auto* top = paneTopBars_.value(activePane_).data();
    auto* bottom = paneBottomBars_.value(activePane_).data();
    replaceVisibleBar(visibleTop_, top ? top : top_.data(), true);
    replaceVisibleBar(visibleBottom_, bottom ? bottom : bottom_.data(), false);
}
} // namespace window_system
