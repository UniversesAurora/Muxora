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
    const auto theme = manager.interfaceTheme();
    center_->setDividerMetrics(std::max(1, theme.metrics.groupSplitterWidth), TabMetrics{}.resizeBand);
    connect(center_, &SplitTreeItem::ratioRequested, this, [this](QUuid node, double ratio) {
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
    center_->setPosition(QPointF(margin, margin));
    center_->setSize(QSizeF(std::max(0.0, width() - 2 * margin), std::max(0.0, height() - 2 * margin)));
    foreground_->setSize(size());
}

TabContentItem* DockSurfaceItem::page(TabId tab)
{
    auto& page = pages_[tab];
    if (!page) page = new TabContentItem(manager_, host_, tab);
    page->sync();
    return page;
}

GroupItem* DockSurfaceItem::ensureGroup(GroupId id)
{
    if (auto existing = groups_.value(id)) return existing;
    auto* view = new GroupItem(manager_.interfaceTheme(), center_);
    view->setForegroundLayer(foreground_);
    groups_.insert(id, view);
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
    QHash<QUuid, QPointer<QQuickItem>> leaves;
    QHash<QUuid, QSize> minima;
    QSet<GroupId> live;
    for (const auto id : manager_.model().centralGroups(window_)) {
        const auto* group = manager_.model().group(id);
        if (!group) continue;
        auto* view = ensureGroup(id);
        syncGroup(*view, *group);
        leaves.insert(id, view); live.insert(id);
        if (auto* page = pages_.value(group->activeTab).data()) view->setContentMinimum(page->minimumSize());
        minima.insert(id, view->minimumSize());
    }
    for (auto it = groups_.begin(); it != groups_.end();) {
        if (live.contains(it.key())) { ++it; continue; }
        if (it.value()) {
            for (const auto tab : builtTabs_.value(it.key())) it.value()->takePage(tab);
            it.value()->deleteLater();
        }
        builtTabs_.remove(it.key());
        it = groups_.erase(it);
    }
    // Pages of tabs that left this window or closed release their content.
    for (auto it = pages_.begin(); it != pages_.end();) {
        const auto* group = manager_.model().group(manager_.model().groupForTab(it.key()));
        if (group && group->windowId == window_) { ++it; continue; }
        if (it.value()) it.value()->deleteLater();
        it = pages_.erase(it);
    }
    center_->setTree(record->central);
    center_->setLeafItems(leaves);
    center_->setMinimumSizes(minima);
    layoutCenter();
    syncing_ = false;
}

GroupItem* DockSurfaceItem::group(GroupId id) const { return groups_.value(id); }

QSize DockSurfaceItem::minimumSize() const
{
    const int margin = uiPixels(surfaceOuterMargin);
    return center_->minimumSize() + QSize(2 * margin, 2 * margin);
}

} // namespace window_system::quick
