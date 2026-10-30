// SPDX-License-Identifier: MIT
#include "dock_surface_item.hpp"
#include "content_host.hpp"
#include "button_item.hpp"
#include "group_item.hpp"
#include "peek_item.hpp"
#include "rail_button_item.hpp"
#include "rail_item.hpp"
#include "split_tree_item.hpp"
#include "tab_bar_item.hpp"
#include "tab_content_item.hpp"
#include "docking/dock_values.hpp"
#include <window_system/ui_scale.hpp>
#include <window_system/window_manager.hpp>

#include <QCoreApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
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
    // As the Widgets surface's grid: the bottom rail owns both lower corners,
    // the side rails the upper ones. Only perpendicular rails are separated;
    // the workspace stays flush with each rail.
    const auto shown = [this](Edge edge) { const auto& rail = rails_[size_t(edge)]; return rail && rail->isVisible(); };
    const auto thickness = [&](Edge edge) { return shown(edge) ? rails_[size_t(edge)]->thickness() : 0; };
    const int w = int(width()), h = int(height());
    const int left = thickness(Edge::Left), right = thickness(Edge::Right);
    const int top = thickness(Edge::Top), bottom = thickness(Edge::Bottom);
    const int sideHeight = h - (shown(Edge::Bottom) ? bottom + (shown(Edge::Left) || shown(Edge::Right) ? 2 : 0) : 0);
    if (shown(Edge::Left)) { rails_[size_t(Edge::Left)]->setPosition({}); rails_[size_t(Edge::Left)]->setSize(QSizeF(left, sideHeight)); }
    if (shown(Edge::Right)) {
        rails_[size_t(Edge::Right)]->setPosition(QPointF(w - right, 0));
        rails_[size_t(Edge::Right)]->setSize(QSizeF(right, sideHeight));
    }
    if (shown(Edge::Top)) {
        const int first = shown(Edge::Left) ? left + 2 : 0, last = shown(Edge::Right) ? w - right - 2 : w;
        rails_[size_t(Edge::Top)]->setPosition(QPointF(first, 0));
        rails_[size_t(Edge::Top)]->setSize(QSizeF(std::max(0, last - first), top));
    }
    if (shown(Edge::Bottom)) {
        rails_[size_t(Edge::Bottom)]->setPosition(QPointF(0, h - bottom));
        rails_[size_t(Edge::Bottom)]->setSize(QSizeF(w, bottom));
    }
    inner_ = QRect(left, top, std::max(0, w - left - right), std::max(0, h - top - bottom));
    const int margin = uiPixels(surfaceOuterMargin);
    const QSize area(std::max(0, inner_.width() - 2 * margin), std::max(0, inner_.height() - 2 * margin));
    center_->setTree(composedTree(area));
    center_->setPosition(inner_.topLeft() + QPointF(margin, margin));
    center_->setSize(area);
    foreground_->setSize(size());
    placePeek();
}

void DockSurfaceItem::syncRails()
{
    const auto& model = manager_.model();
    const auto* window = model.window(window_);
    const auto theme = manager_.interfaceTheme();
    const auto appearance = manager_.tabAppearance();
    const bool shown = manager_.sideBarsVisible(window_);
    for (const auto edge : {Edge::Left, Edge::Right, Edge::Top, Edge::Bottom}) {
        QVector<QPair<TabId, QString>> tabs;
        QHash<TabId, GroupId> groups;
        for (const auto id : window->edgeGroups)
            if (const auto* group = model.group(id); group && group->edge == edge && group->location == GroupLocation::AutoHide)
                for (const auto tab : group->tabs) { tabs.append({tab, model.tab(tab)->title}); groups.insert(tab, id); }
        auto& rail = rails_[size_t(edge)];
        if (tabs != railTabs_[size_t(edge)]) {
            // Buttons keep no state worth carrying; the scroll position does.
            std::optional<RailItem::ScrollAnchor> anchor;
            if (rail) { anchor = rail->scrollAnchor(); delete rail.data(); }
            railTabs_[size_t(edge)] = tabs;
            if (!tabs.isEmpty()) {
                rail = new RailItem(edge, theme, this);
                for (const auto& [tab, title] : tabs) {
                    auto* button = new RailButtonItem(theme, tab, title, rail->vertical(), theme.metrics.edgeThickness, rail);
                    button->clicked = [this, tab, group = groups.value(tab)] {
                        if (peekTab_ == tab) closePeek(); else showPeek(group, tab);
                    };
                    // Context menus arrive with the Quick menus (10.4).
                    button->close = [this, tab] { manager_.closeTab(tab); };
                    rail->addButton(button);
                }
                auto* pinAll = new ButtonItem(theme, rail);
                pinAll->setIcon(iconFor(QStringLiteral("pin-all"), theme));
                pinAll->setToolTip(QCoreApplication::translate("WindowSystem", "Pin all sidebar tabs"));
                connect(pinAll, &ButtonItem::clicked, this, [this, edge] { manager_.pinEdge(window_, edge, true); });
                rail->setTail(pinAll);
                rail->thicknessChanged = [this] { layoutCenter(); };
                rail->hoverGeometryChanged = [this] { placePeek(); };
                if (anchor) rail->restoreScrollAnchor(*anchor);
            }
        }
        if (!rail) continue;
        rail->setVisible(shown);
        int ordinal = 0;
        for (auto* button : rail->buttons()) {
            const auto tab = button->tab();
            button->setPreset(manager_.tabColorPreset(model.tab(tab)->colorId));
            button->setTitleClipping(appearance.clipSidebarTitles, appearance.maximumSidebarTitleWidth);
            ++ordinal;
            button->setPresentation(appearance.showSidebarIcons && manager_.tabOptions(tab).showIcon
                ? iconFor(QStringLiteral("window"), theme) : QIcon{}, appearance.showSidebarNumbers ? ordinal : 0);
            button->setChecked(tab == peekTab_);
        }
    }
}

void DockSurfaceItem::showPeek(GroupId group, TabId tab)
{
    closePeek();
    const auto* record = manager_.model().tab(tab);
    if (!record) return;
    peekGroup_ = group; peekTab_ = tab;
    auto* panel = new PeekItem(manager_.interfaceTheme(), this);
    panel->setZ(5);
    panel->setTitle(record->title, QGuiApplication::font());
    panel->setPage(page(tab));
    panel->pinRequested = [this, tab] { manager_.pinTab(tab, true); };
    panel->closeRequested = [this, tab] { manager_.closeTab(tab); };
    peek_ = panel;
    if (window()) window()->installEventFilter(this);
    for (const auto& rail : rails_)
        if (rail) for (auto* button : rail->buttons()) button->setChecked(button->tab() == tab);
    placePeek();
    manager_.activateTab(tab);
}

void DockSurfaceItem::closePeek()
{
    if (!peek_) return;
    if (auto* page = peek_->page()) { page->setVisible(false); page->setParentItem(nullptr); }
    delete peek_.data();
    if (window()) window()->removeEventFilter(this);
    peekGroup_ = {}; peekTab_ = {};
    for (const auto& rail : rails_)
        if (rail) for (auto* button : rail->buttons()) button->setChecked(false);
}

// The Widgets panel's size and placement: a requested or default extent
// across the edge, following its own rail button along it.
void DockSurfaceItem::placePeek()
{
    if (!peek_) return;
    const auto* group = manager_.model().group(peekGroup_);
    if (!group) return;
    const auto edge = group->edge;
    const bool vertical = edge == Edge::Left || edge == Edge::Right;
    const auto requested = manager_.peekSizePolicy(peekTab_, edge);
    const auto available = inner_;
    const int h = std::min(available.height(), requested.height.value_or(vertical ? available.height() : 170));
    const int w = std::min(available.width(), requested.width.value_or(vertical ? 250 : available.width()));
    QPoint origin = available.topLeft();
    if (const auto& rail = rails_[size_t(edge)]; rail && rail->isVisible())
        for (auto* button : rail->buttons()) {
            if (button->tab() != peekTab_) continue;
            const auto trigger = button->mapRectToItem(this, button->boundingRect()).toRect();
            if (vertical) origin.setY(std::clamp(trigger.top(), available.top(), std::max(available.top(), available.bottom() - h + 1)));
            else origin.setX(std::clamp(trigger.left(), available.left(), std::max(available.left(), available.right() - w + 1)));
            break;
        }
    switch (edge) {
    case Edge::Left: peek_->setPosition(QPointF(available.x(), origin.y())); break;
    case Edge::Right: peek_->setPosition(QPointF(available.right() - w + 1, origin.y())); break;
    case Edge::Top: peek_->setPosition(QPointF(origin.x(), available.y())); break;
    case Edge::Bottom: peek_->setPosition(QPointF(origin.x(), available.bottom() - h + 1)); break;
    }
    peek_->setSize(QSizeF(w, h));
}

bool DockSurfaceItem::eventFilter(QObject* watched, QEvent* event)
{
    if (peek_ && watched == window()) {
        if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            closePeek();
            return true;
        }
        if (event->type() == QEvent::MouseButtonPress) {
            const auto point = mapFromScene(static_cast<QMouseEvent*>(event)->scenePosition());
            const auto inside = [&point](const QQuickItem* item) {
                return item && item->isVisible() && QRectF(item->position(), item->size()).contains(point);
            };
            bool onRail = false;
            for (const auto& rail : rails_) onRail = onRail || inside(rail);
            if (!inside(peek_) && !onRail) closePeek();
        }
    }
    return QQuickItem::eventFilter(watched, event);
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
    syncRails();
    // A panel closes when its tab leaves this window's sidebar.
    if (peek_) {
        const auto* group = model.group(model.groupForTab(peekTab_));
        if (!group || group->id != peekGroup_ || group->windowId != window_ || group->location != GroupLocation::AutoHide) closePeek();
        else peek_->setTitle(model.tab(peekTab_)->title, QGuiApplication::font());
    }
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
RailItem* DockSurfaceItem::rail(Edge edge) const { return rails_[size_t(edge)]; }
PeekItem* DockSurfaceItem::peek() const { return peek_; }

QRect DockSurfaceItem::groupRect(GroupId id) const { return center_->leafRect(id).translated(center_->position().toPoint()); }

QSize DockSurfaceItem::minimumSize() const
{
    const int margin = uiPixels(surfaceOuterMargin);
    const auto thickness = [this](Edge edge) {
        const auto& rail = rails_[size_t(edge)];
        return rail && rail->isVisible() ? rail->thickness() : 0;
    };
    return center_->minimumSize() + QSize(2 * margin + thickness(Edge::Left) + thickness(Edge::Right),
                                          2 * margin + thickness(Edge::Top) + thickness(Edge::Bottom));
}

} // namespace window_system::quick
