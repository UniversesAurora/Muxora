// SPDX-License-Identifier: MIT
#include "dock_surface_item.hpp"
#include "content_host.hpp"
#include "group_item.hpp"
#include "split_tree_item.hpp"
#include "tab_bar_item.hpp"
#include "tab_content_item.hpp"
#include "docking/dock_values.hpp"
#include <window_system/ui_scale.hpp>
#include <window_system/window_manager.hpp>

#include <QQuickWindow>

namespace window_system::quick {

DockSurfaceItem::DockSurfaceItem(WindowManager& manager, ContentHost& host, WindowId window, QQuickItem* parent)
    : QQuickItem(parent), manager_(manager), host_(host), window_(window),
      center_(new SplitTreeItem(this)), foreground_(new QQuickItem(this))
{
    foreground_->setZ(10);
    divider_ = std::max(1, manager.interfaceTheme().metrics.groupSplitterWidth);
    center_->setDividerMetrics(divider_, TabMetrics{}.resizeBand);
    connect(center_, &SplitTreeItem::ratioRequested, this, [this](QUuid node, double ratio) {
        for (const auto edge : {Edge::Left, Edge::Right, Edge::Top, Edge::Bottom})
            if (pinned_[size_t(edge)].split == node) return resizePinned(edge, ratio);
        manager_.execute(QStringLiteral("layout.setRatio"), {{"node", node}, {"ratio", ratio}}, window_);
    });
}

void DockSurfaceItem::geometryChange(const QRectF& now, const QRectF& previous)
{
    QQuickItem::geometryChange(now, previous);
    if (now.size() != previous.size()) layoutCenter();
}

void DockSurfaceItem::layoutCenter()
{
    const int margin = uiPixels(surfaceOuterMargin);
    const QSize area(std::max(0, int(width()) - 2 * margin), std::max(0, int(height()) - 2 * margin));
    center_->setTree(composedTree(area));
    center_->setPosition(QPointF(margin, margin));
    center_->setSize(area);
    foreground_->setSize(size());
}

// As the Widgets surface nests its splitters: the top and bottom regions
// span the window, the left and right regions sit beside the center.
LayoutTree DockSurfaceItem::composedTree(QSize area)
{
    const auto& model = manager_.model();
    struct Split { Edge edge; Qt::Orientation axis; double ratio; };
    QVector<Split> splits; // Outermost first.
    int lengths[] = {area.height(), area.width()};
    for (const auto edge : {Edge::Top, Edge::Bottom, Edge::Left, Edge::Right}) {
        auto& region = pinned_[size_t(edge)];
        if (!region.tree) continue;
        const bool across = edge == Edge::Left || edge == Edge::Right;
        const auto axis = across ? Qt::Horizontal : Qt::Vertical;
        int& length = lengths[across ? 1 : 0];
        region.available = std::max(1, length - divider_);
        const int extent = std::clamp(region.dragged.value_or(pinnedExtent(region.tree, model, axis, divider_)), 0, region.available);
        length = region.available - extent;
        const bool leading = edge == Edge::Left || edge == Edge::Top;
        splits.append({edge, axis, double(leading ? extent : region.available - extent) / region.available});
    }
    auto tree = central_;
    for (auto it = splits.crbegin(); it != splits.crend(); ++it) {
        const auto& region = pinned_[size_t(it->edge)];
        if (!tree) { tree = region.tree; continue; }
        const bool leading = it->edge == Edge::Left || it->edge == Edge::Top;
        auto node = std::make_shared<LayoutNode>();
        node->id = region.split; node->orientation = it->axis; node->ratio = it->ratio;
        node->first = leading ? region.tree : tree;
        node->second = leading ? tree : region.tree;
        tree = std::move(node);
    }
    return tree;
}

// A dragged region keeps the dragged extent until the model holds it: each
// pinned group saves its new size, as the Widgets surface does.
void DockSurfaceItem::resizePinned(Edge edge, double ratio)
{
    auto& region = pinned_[size_t(edge)];
    const bool leading = edge == Edge::Left || edge == Edge::Top;
    const int first = qRound(region.available * ratio);
    region.dragged = leading ? first : region.available - first;
    layoutCenter();
    const bool across = edge == Edge::Left || edge == Edge::Right;
    QVector<QPair<GroupId, int>> extents;
    for (auto it = locations_.cbegin(); it != locations_.cend(); ++it) {
        const auto* record = manager_.model().group(it.key());
        if (it.value() != GroupLocation::Pinned || !record || record->edge != edge) continue;
        const auto rect = center_->leafRect(it.key());
        extents.append({it.key(), across ? rect.width() : rect.height()});
    }
    const QPointer<DockSurfaceItem> self(this);
    for (const auto& [group, extent] : extents) {
        manager_.execute(QStringLiteral("layout.setEdgeExtent"), {{"group", group}, {"extent", extent}}, window_);
        if (!self) return;
    }
}

TabContentItem* DockSurfaceItem::page(TabId tab)
{
    auto& page = pages_[tab];
    if (!page) page = new TabContentItem(manager_, host_, tab);
    page->sync();
    return page;
}

void DockSurfaceItem::releaseGroup(GroupId id)
{
    if (const auto view = groups_.take(id)) {
        for (const auto tab : builtTabs_.value(id)) view->takePage(tab);
        view->deleteLater();
    }
    builtTabs_.remove(id);
    locations_.remove(id);
}

GroupItem* DockSurfaceItem::ensureGroup(GroupId id, const GroupRecord& record)
{
    if (locations_.value(id, record.location) != record.location) releaseGroup(id);
    if (auto existing = groups_.value(id)) return existing;
    auto* view = new GroupItem(manager_.interfaceTheme(), center_);
    view->setForegroundLayer(foreground_);
    groups_.insert(id, view);
    locations_.insert(id, record.location);
    if (record.location == GroupLocation::Pinned)
        view->setPinnedActions([this, id] {
            if (const auto* current = manager_.model().group(id)) manager_.pinTab(current->activeTab, false);
        }, [this, edge = record.edge] { manager_.pinEdge(window_, edge, false); });
    const auto tabAt = [view](int index) { return view->tabBar()->controller().tabData(index).toUuid(); };
    view->pageRequested = [this](TabId tab) -> QQuickItem* { return page(tab); };
    view->currentChanged = [this, tabAt](int index) {
        if (syncing_ || index < 0) return;
        manager_.activateTab(tabAt(index));
    };
    view->stripAction = [this, view, id, tabAt](const TabPointerDecision& decision) {
        using Action = TabPointerDecision::Action;
        const auto folderId = [view](int folder) {
            const auto& folders = view->tabBar()->controller().folders();
            return folder >= 0 && folder < folders.size() ? folders.at(folder).id : QUuid{};
        };
        switch (decision.action) {
        case Action::CloseTab: manager_.closeTab(tabAt(decision.index)); break;
        case Action::UnstickTab: manager_.setTabSticky(tabAt(decision.index), false); break;
        case Action::ToggleFolder:
            if (const auto* record = manager_.model().folder(folderId(decision.index))) {
                auto changed = *record; changed.collapsed = !changed.collapsed;
                manager_.updateTabFolder(changed);
            }
            break;
        case Action::AddTabToFolder: manager_.requestNewTab(window_, id, {}, folderId(decision.index)); break;
        default: break; // Dragging arrives with the Quick drag (10.3, sub-step 5).
        }
    };
    view->newTabRequested = [this, id] { manager_.requestNewTab(window_, id); };
    view->verticalStateChanged = [this, id](int width, bool iconsOnly) {
        if (manager_.model().group(id)) manager_.setGroupTabStrip(id, width, iconsOnly);
    };
    view->isActiveGroup = [this, id] {
        const auto* record = manager_.model().window(window_);
        return record && record->activeGroup == id;
    };
    return view;
}

void DockSurfaceItem::syncGroup(GroupItem& view, const GroupRecord& record)
{
    const auto& model = manager_.model();
    if (builtTabs_.value(record.id) != record.tabs) {
        // Pages keep their identity; only the strip's entries are replaced.
        for (const auto tab : builtTabs_.value(record.id)) view.takePage(tab);
        const auto theme = manager_.interfaceTheme();
        for (const auto tab : record.tabs)
            view.addTab(nullptr, manager_.tabOptions(tab).showIcon ? iconFor(QStringLiteral("window"), theme) : QIcon{},
                        model.tab(tab)->title, tab);
        builtTabs_.insert(record.id, record.tabs);
    }
    auto& strip = view.tabBar()->controller();
    view.tabBar()->edit([&] {
        for (int index = 0; index < record.tabs.size(); ++index) {
            const auto& tab = *model.tab(record.tabs.at(index));
            strip.setTabText(index, tab.title);
            strip.setTabMetadata(index, tab);
        }
        strip.setFolders(stripFolders(model, record));
    });
    view.setAppearance(manager_.tabAppearance(), record.location == GroupLocation::Pinned);
    view.setStickyCount(record.stickyTabs);
    view.setGroupName(record.name);
    view.setVerticalState(record.tabStripWidth, record.tabStripIconsOnly);
    const int active = int(record.tabs.indexOf(record.activeTab));
    if (active >= 0 && active != view.currentIndex()) view.setCurrentIndex(active);
    if (auto* page = pages_.value(record.activeTab).data()) page->sync();
    const auto* windowRecord = model.window(window_);
    view.setGroupFocused(window() && window()->isActive() && windowRecord && windowRecord->activeGroup == record.id);
}

void DockSurfaceItem::sync()
{
    const auto* record = manager_.model().window(window_);
    if (!record) return;
    syncing_ = true;
    const auto& model = manager_.model();
    central_ = record->central;
    for (const auto edge : {Edge::Left, Edge::Right, Edge::Top, Edge::Bottom}) {
        pinned_[size_t(edge)].tree = model.pinnedLayout(window_, edge);
        pinned_[size_t(edge)].dragged.reset();
    }
    QHash<QUuid, QPointer<QQuickItem>> leaves;
    QHash<QUuid, QSize> minima;
    for (const auto& group : model.groups()) {
        if (group.windowId != window_ || group.location == GroupLocation::AutoHide) continue;
        auto* view = ensureGroup(group.id, group);
        syncGroup(*view, group);
        leaves.insert(group.id, view);
        if (auto* page = pages_.value(group.activeTab).data()) view->setContentMinimum(page->minimumSize());
        minima.insert(group.id, view->minimumSize());
    }
    for (const auto id : groups_.keys())
        if (!leaves.contains(id)) releaseGroup(id);
    // Pages of tabs that left this window or closed release their content.
    for (auto it = pages_.begin(); it != pages_.end();) {
        const auto* group = manager_.model().group(manager_.model().groupForTab(it.key()));
        if (group && group->windowId == window_) { ++it; continue; }
        if (it.value()) it.value()->deleteLater();
        it = pages_.erase(it);
    }
    center_->setLeafItems(leaves);
    center_->setMinimumSizes(minima);
    layoutCenter();
    syncing_ = false;
}

GroupItem* DockSurfaceItem::group(GroupId id) const { return groups_.value(id); }

QRect DockSurfaceItem::groupRect(GroupId id) const { return center_->leafRect(id).translated(center_->position().toPoint()); }

QSize DockSurfaceItem::minimumSize() const
{
    const int margin = uiPixels(surfaceOuterMargin);
    return center_->minimumSize() + QSize(2 * margin, 2 * margin);
}

} // namespace window_system::quick
