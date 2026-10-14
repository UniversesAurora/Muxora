#include "tab_content.hpp"
#include <QApplication>
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
    // The manager parks content and pane bars before rebuilding hosts. Sharing
    // policy changes only their presentation; the same widgets remain adopted.
    focusProjection_.restore();
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
    setFocusedPane(focusedPane_);
}
QWidget* TabContent::buildNode(LayoutTree node, const TabRecord& record,
                              const QHash<PaneId, QPointer<QWidget>>& contents)
{
    if (!node) return nullptr;
    if (!node->isLeaf()) {
        auto* splitter = new QSplitter(node->orientation, this);
        splitter->setObjectName("PaneSplitter");
        splitter->setHandleWidth(qMax(1, manager_->theme().metrics.splitterWidth));
        splitter->setProperty("splitNodeId", node->id);
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
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    if (auto content = contents.value(node->itemId)) {
        column->addWidget(content, 1);
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

void TabContent::setFocusedPane(PaneId pane)
{
    focusProjection_.restore();
    focusedPane_ = frames_.contains(pane) ? pane : PaneId{};
    if (focusedPane_.isNull()) return;
    focusProjection_.apply(frames_.value(focusedPane_), this);
    layout_->activate();
}

void TabContent::setBarsVisible(bool visible)
{
    if (barsVisible_ == visible) return;
    barsVisible_ = visible;
    updateBars();
    layout_->activate();
}

void TabContent::setBarSharing(PaneBarSharing top, PaneBarSharing bottom)
{
    if (topSharing_ == top && bottomSharing_ == bottom) return;
    topSharing_ = top;
    bottomSharing_ = bottom;
    updateBars();
}

void TabContent::setActivePane(PaneId pane)
{
    if (activePane_ == pane) return;
    const QPointer<TabContent> retained(this);
    activePane_ = pane;
    QVector<QPointer<QWidget>> frames;
    frames.reserve(frames_.size());
    for (auto* frame : frames_) frames.append(frame);
    for (const auto& frame : frames) {
        if (!frame) continue;
        frame->setProperty("active", frame->property("paneId").toUuid() == pane);
        if (!retained) return;
        if (!frame) continue;
        frame->style()->unpolish(frame);
        if (!retained) return;
        if (!frame) continue;
        frame->style()->polish(frame);
        if (!retained) return;
        if (frame) frame->update();
    }
    updateBars();
}

void TabContent::replaceVisibleBar(QPointer<QWidget>& slot, QWidget* next, bool top)
{
    const QPointer<TabContent> retained(this);
    const QPointer<QWidget> requested(next);
    const QPointer<QWidget> previous(slot);
    // A manager refresh parks the same widget before rebuilding. Checking only
    // pointer equality would leave it hidden and detached from this layout.
    if (previous && previous != requested) {
        if (previous->parentWidget() == this) {
            layout_->removeWidget(previous);
            if (!retained) return;
            if (previous) previous->hide();
            if (!retained) return;
        }
    }
    slot = requested;
    if (!requested) return;
    requested->setParent(this);
    if (!retained || !requested || slot != requested) return;
    layout_->removeWidget(requested);
    if (!retained || !requested || slot != requested) return;
    if (top) layout_->insertWidget(0, requested);
    else layout_->addWidget(requested);
    if (!retained || !requested || slot != requested) return;
    requested->setVisible(barsVisible_);
}

void TabContent::updateBars()
{
    const QPointer<TabContent> retained(this);
    QPointer<QWidget> focus = QApplication::focusWidget();
    if (focus && !isAncestorOf(focus)) focus.clear();
    const QPointer<QWidget> top = topSharing_ == PaneBarSharing::Shared ? paneTopBars_.value(activePane_) : QPointer<QWidget>{};
    const QPointer<QWidget> bottom = bottomSharing_ == PaneBarSharing::Shared ? paneBottomBars_.value(activePane_) : QPointer<QWidget>{};
    replaceVisibleBar(visibleTop_, top ? top.data() : top_.data(), true);
    if (!retained) return;
    replaceVisibleBar(visibleBottom_, bottom ? bottom.data() : bottom_.data(), false);
    if (!retained) return;
    placePaneBars(paneTopBars_, topSharing_, top, true);
    if (!retained) return;
    placePaneBars(paneBottomBars_, bottomSharing_, bottom, false);
    if (!retained) return;
    if (focus && focus->isVisible() && !focus->hasFocus()) focus->setFocus(Qt::OtherFocusReason);
}

void TabContent::placePaneBars(const QHash<PaneId, QPointer<QWidget>>& bars,
                               PaneBarSharing sharing, QWidget* shared, bool top)
{
    const QPointer<TabContent> retained(this);
    const auto requested = bars;
    for (auto it = requested.cbegin(); it != requested.cend(); ++it) {
        if (!retained) return;
        const QPointer<QWidget> bar = it.value();
        if (!bar || bar == shared) continue;
        const QPointer<QWidget> frame = sharing == PaneBarSharing::PerPane ? frames_.value(it.key()) : nullptr;
        const QPointer<QWidget> target = frame ? frame.data() : static_cast<QWidget*>(this);
        if (bar->parentWidget() != target) {
            if (auto* previous = bar->parentWidget(); previous && previous->layout())
                previous->layout()->removeWidget(bar);
            if (!retained || !bar || !target) return;
            bar->setParent(target);
            if (!retained || !bar || !target) return;
        }
        if (!frame) {
            layout_->removeWidget(bar);
            if (!retained || !bar) return;
            bar->hide();
            continue;
        }
        const QPointer<QVBoxLayout> column = static_cast<QVBoxLayout*>(frame->layout());
        if (!column) continue;
        const int index = top ? 0 : column->count() - 1;
        if (column->indexOf(bar) != index) {
            column->removeWidget(bar);
            if (!retained || !bar || !column) return;
            if (top) column->insertWidget(0, bar);
            else column->addWidget(bar);
            if (!retained || !bar) return;
        }
        bar->setVisible(barsVisible_);
    }
}
} // namespace window_system
