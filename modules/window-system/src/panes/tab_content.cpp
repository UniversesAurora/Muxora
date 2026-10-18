#include "tab_content.hpp"
#include "window_manager_p.hpp"
#include <QApplication>
#include <QSplitter>
#include <QStyle>
#include <QScopeGuard>
#include <QVBoxLayout>
#include <algorithm>

namespace window_system {
namespace {
class PaneHostFrame final : public QFrame {
public:
    PaneHostFrame(QWidget* content, QWidget* parent) : QFrame(parent), content_(content) {}
    QSize minimumSizeHint() const override
    {
        auto result = QFrame::minimumSizeHint().expandedTo(QSize(0, 0));
        QSize accounted;
        if (content_ && layout()) {
            const int index = layout()->indexOf(content_);
            if (index >= 0) accounted = layout()->itemAt(index)->minimumSize();
        }
        const auto explicitMinimum = content_ ? content_->minimumSize() : QSize();
        const auto hintedMinimum = content_ ? content_->minimumSizeHint() : QSize();
        const auto minimum = [](int explicitValue, int hint, int fallback) {
            return explicitValue > 0 ? explicitValue : hint > 0 ? hint : fallback;
        };
        const QSize requested(minimum(explicitMinimum.width(), hintedMinimum.width(), 140),
                              minimum(explicitMinimum.height(), hintedMinimum.height(), 85));
        // A pane's fallback applies only to an axis without a content request.
        // Leave the frame's explicit minimum unset: setting a hard minimum
        // would override a larger child-layout hint inside QSplitter.
        result.rwidth() += std::max(0, requested.width() - std::max(0, accounted.width()));
        result.rheight() += std::max(0, requested.height() - std::max(0, accounted.height()));
        return result;
    }
private:
    QPointer<QWidget> content_;
};
}

TabContent::TabContent(WindowManager* manager, TabId tab, QWidget* parent)
    : QFrame(parent), manager_(manager), tab_(tab), layout_(new QVBoxLayout(this))
{
    setObjectName("TabPage");
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(0);
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
    if (tree_) {
        layout_->removeWidget(tree_);
        // A host can request a rebuild from a pane's resize callback. The old
        // PaneHost layout is still on Qt's geometry stack until that callback
        // returns, even after its live content has been parked elsewhere.
        if (manager_->d->geometryUpdateDepth) {
            tree_->hide();
            tree_->deleteLater();
        } else delete tree_;
    }
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
    auto* frame = new PaneHostFrame(contents.value(node->itemId), this);
    frame->setObjectName("PaneHost");
    frame->setProperty("paneId", node->itemId);
    frame->setProperty("tabId", tab_);
    frame->setProperty("active", node->itemId == record.activePane);
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
QWidget* TabContent::takeTopBar(QWidget* owner)
{
    const QPointer<TabContent> self(this);
    const QPointer<QWidget> result = top_;
    top_.clear();
    if (result) {
        if (visibleTop_ == result) { layout_->removeWidget(result); visibleTop_.clear(); }
        result->setParent(owner);
        if (!self) return result.data();
    }
    return result.data();
}
QWidget* TabContent::takeBottomBar(QWidget* owner)
{
    const QPointer<TabContent> self(this);
    const QPointer<QWidget> result = bottom_;
    bottom_.clear();
    if (result) {
        if (visibleBottom_ == result) { layout_->removeWidget(result); visibleBottom_.clear(); }
        result->setParent(owner);
        if (!self) return result.data();
    }
    return result.data();
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
    const QPointer<WindowManager> owner(manager_);
    if (!owner) return;
    ++owner->d->geometryUpdateDepth;
    const auto releaseGeometry = qScopeGuard([owner] { if (owner) --owner->d->geometryUpdateDepth; });
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
    if (!retained) return;
    // PaneHost notifications queue a splitter layout request. Invalidate the
    // pane tree's QWidgetItem cache before activating ancestors: otherwise the
    // page layout can republish its old shared-bar minimum into every parent.
    if (tree_) tree_->updateGeometry();
    if (!isVisible()) { updateGeometry(); return; }
    for (QPointer<QWidget> widget(this); widget;) {
        const QPointer<QWidget> parent = widget->parentWidget();
        if (auto* splitter = qobject_cast<QSplitter*>(widget.data())) splitter->refresh();
        else if (auto* parentLayout = widget->layout()) {
            parentLayout->invalidate(); parentLayout->activate();
        }
        if (!retained || !owner || !widget) return;
        widget->updateGeometry();
        if (!retained || !owner) return;
        widget = parent;
    }
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
        if (!retained || !frame) return;
        frame->updateGeometry();
    }
}
} // namespace window_system
