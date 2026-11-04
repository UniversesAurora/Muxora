// SPDX-License-Identifier: MIT
#include "dock_surface_item.hpp"
#include "content_host.hpp"
#include "dock_guide_item.hpp"
#include "button_item.hpp"
#include "group_item.hpp"
#include "peek_item.hpp"
#include "popup_menu.hpp"
#include "rail_button_item.hpp"
#include "rail_item.hpp"
#include "split_tree_item.hpp"
#include "tab_drag.hpp"
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

DockSurfaceItem::DockSurfaceItem(WindowManager& manager, ContentHost& host, ContentBars& bars, WindowId window, QQuickItem* parent)
    : QQuickItem(parent), manager_(manager), host_(host), bars_(bars), window_(window),
      center_(new SplitTreeItem(this)), foreground_(new QQuickItem(this)), guides_(new DockGuideItem(this))
{
    foreground_->setZ(10);
    guides_->setZ(20);
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
    guides_->setSize(size());
    placePeek();
}

void DockSurfaceItem::syncRails()
{
    const auto& model = manager_.model();
    const auto* window = model.window(window_);
    const auto theme = manager_.interfaceTheme();
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
                    button->menu = [this, tab, group = groups.value(tab)](QPoint global) {
                        showContextMenu(CommandMenuSurface::Tab, group, tab, global);
                    };
                    button->close = [this, tab] { manager_.closeTab(tab); };
                    button->drag = [this, edge, tab](DragStart start) { if (drag_) drag_->beginRailTab(window_, edge, tab, start); };
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
            configureRailButton(*button, ++ordinal);
            button->setChecked(button->tab() == peekTab_);
        }
    }
}

void DockSurfaceItem::configureRailButton(RailButtonItem& button, int ordinal) const
{
    const auto tab = button.tab();
    const auto appearance = manager_.tabAppearance();
    button.setPreset(manager_.tabColorPreset(manager_.model().tab(tab)->colorId));
    button.setTitleClipping(appearance.clipSidebarTitles, appearance.maximumSidebarTitleWidth);
    button.setPresentation(appearance.showSidebarIcons && manager_.tabOptions(tab).showIcon
        ? iconFor(QStringLiteral("window"), manager_.interfaceTheme()) : QIcon{}, appearance.showSidebarNumbers ? ordinal : 0);
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
    if (const auto* owner = manager_.model().group(group)) {
        const auto edge = owner->edge;
        panel->setEdge(edge);
        panel->requestedSize = [this, tab, edge] { return manager_.peekSizePolicy(tab, edge); };
        panel->resizeRequested = [this, tab, edge](PeekSizePolicy size) {
            if (manager_.setPeekSizePolicy(tab, edge, size)) placePeek();
        };
        // As the Widgets panel header: the title drags the tab, as its rail
        // button does.
        panel->dragRequested = [this, tab, edge](DragStart start) { if (drag_) drag_->beginRailTab(window_, edge, tab, start); };
    }
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
    if (!focusGroup_.isNull()) {
        auto leaf = std::make_shared<LayoutNode>();
        leaf->itemId = focusGroup_;
        return leaf;
    }
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
    auto page = pages_.value(tab);
    if (!page) {
        page = new TabContentItem(manager_, host_, bars_, tab);
        // QObject ownership follows this surface; visual parenting follows
        // the group or peek panel. Moving content never transfers the page.
        page->setParent(this);
        page->setVisible(false);
        pages_.insert(tab, page);
    }
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

void DockSurfaceItem::showMenu(const QVector<MenuItemDescription>& items, const CommandContext& context,
                               QPoint global, TabBarItem* strip)
{
    // Finish the previous hold before opening another menu on the same strip.
    // Its later closed notification must not collapse the new menu's strip.
    delete menu_.data();
    auto* menu = new PopupMenu(manager_.interfaceTheme(), this);
    menu_ = menu;
    const QPointer<WindowManager> owner(&manager_);
    const QPointer<TabBarItem> bar(strip);
    if (bar) bar->setPopupOpen(true);
    connect(menu, &PopupMenu::closed, menu, &QObject::deleteLater);
    connect(menu, &QObject::destroyed, this, [bar] { if (bar) bar->setPopupOpen(false); });
    connect(menu, &PopupMenu::triggered, menu, [owner, context](const MenuItemDescription& item) {
        if (!owner) return;
        if (item.kind == MenuItemDescription::Kind::TabCreation) {
            owner->requestNewTab(context.window, context.group, item.commandId);
            return;
        }
        auto captured = context;
        for (auto it = item.arguments.cbegin(); it != item.arguments.cend(); ++it) captured.args.insert(it.key(), it.value());
        owner->executeInContext(item.commandId, captured);
    });
    if (!menu->open(this, global, items)) delete menu;
}

void DockSurfaceItem::showContextMenu(CommandMenuSurface surface, GroupId group, TabId tab,
                                      QPoint global, TabBarItem* strip)
{
    const auto* record = manager_.model().group(group);
    if (!record || record->windowId != window_ || (!tab.isNull() && !record->tabs.contains(tab))) {
        if (strip) strip->setPopupOpen(false);
        return;
    }
    auto captured = manager_.context(window_);
    captured.group = group; captured.tab = tab.isNull() ? record->activeTab : tab;
    const auto* target = manager_.model().tab(captured.tab);
    captured.pane = target ? target->activePane : PaneId{};
    const QPointer<DockSurfaceItem> self(this);
    const QPointer<TabBarItem> bar(strip);
    const QPointer<WindowManager> owner(&manager_);
    QVector<MenuItemDescription> items;
    if (surface == CommandMenuSurface::Tab || surface == CommandMenuSurface::Group) {
        items = describeTabPresentationMenu(owner, captured);
        if (!self || !owner) return;
    }
    auto commands = describeContextMenu(surface, owner, captured);
    if (!self || !owner) return;
    const auto* current = owner->model().group(group);
    if (!current || (!captured.tab.isNull() && !current->tabs.contains(captured.tab))) {
        if (bar) bar->setPopupOpen(false);
        return;
    }
    if (!items.isEmpty() && !commands.isEmpty()) {
        MenuItemDescription separator; separator.kind = MenuItemDescription::Kind::Separator;
        items.append(std::move(separator));
    }
    items += commands;
    showMenu(items, captured, global, bar);
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
        case Action::DragTab: if (drag_) drag_->beginTab(window_, id, tabAt(decision.index), decision.start); break;
        case Action::DragFolder: if (drag_) drag_->beginFolder(window_, id, folderId(decision.index), decision.start); break;
        default: break;
        }
    };
    view->newTabRequested = [this, id] { manager_.requestNewTab(window_, id); };
    view->stripMenuRequested = [this, view, id, tabAt](int tab, int folder, QPoint global) {
        if (folder >= 0) {
            const auto& folders = view->tabBar()->controller().folders();
            const auto tabs = manager_.model().folderTabs(folders.at(folder).id);
            if (tabs.isEmpty()) { view->tabBar()->setPopupOpen(false); return; }
            const auto* group = manager_.model().group(id);
            const auto target = group && tabs.contains(group->activeTab) ? group->activeTab : tabs.value(0);
            showContextMenu(CommandMenuSurface::TabFolder, id, target, global, view->tabBar());
        } else showContextMenu(tab >= 0 ? CommandMenuSurface::Tab : CommandMenuSurface::Group,
            id, tab >= 0 ? tabAt(tab) : TabId{}, global, view->tabBar());
    };
    view->groupMenuRequested = [this, id](QPoint global) { showContextMenu(CommandMenuSurface::Group, id, {}, global); };
    view->newTabMenuRequested = [this, view, id](QRect trigger) {
        const QPointer<DockSurfaceItem> self(this);
        const QPointer<TabBarItem> bar(view->tabBar());
        auto captured = manager_.context(window_); captured.group = id;
        const auto items = describeTabCreationMenu(&manager_, window_, id);
        if (self) showMenu(items, captured, trigger.bottomLeft() + QPoint(0, 1), bar);
    };
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
    // As the Widgets surface's focus scope: in group or pane fullscreen only
    // that group shows, a sidebar one included; zen hides every strip.
    const auto state = manager_.windowViewState(window_);
    const auto* focused = model.group(state.group);
    focusGroup_ = (state.mode == ViewMode::GroupFullscreen || state.mode == ViewMode::PaneFullscreen)
        && focused && focused->windowId == window_ ? focused->id : GroupId{};
    if (!focusGroup_.isNull()) closePeek();
    const bool centralStrips = state.effective.tabBars && !manager_.singleTabHidesTabBar(window_);
    QHash<QUuid, QPointer<QQuickItem>> leaves;
    QHash<QUuid, QSize> minima;
    for (const auto& group : model.groups()) {
        if (group.windowId != window_) continue;
        if (focusGroup_.isNull() ? group.location == GroupLocation::AutoHide : group.id != focusGroup_) continue;
        auto* view = ensureGroup(group.id, group);
        syncGroup(*view, group);
        view->setTabBarVisible(!state.zen && (group.location != GroupLocation::Central || centralStrips));
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
        else {
            peek_->setTitle(model.tab(peekTab_)->title, QGuiApplication::font());
            if (auto* page = pages_.value(peekTab_).data()) page->sync();
        }
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

QRect DockSurfaceItem::globalRect() const { return QRect(mapToGlobal(QPointF()).toPoint(), size().toSize()); }

QHash<GroupId, QRect> DockSurfaceItem::groupRects() const
{
    QHash<GroupId, QRect> result;
    for (auto it = groups_.cbegin(); it != groups_.cend(); ++it)
        if (it.value() && it.value()->isVisible())
            result.insert(it.key(), QRect(it.value()->mapToGlobal(QPointF()).toPoint(), it.value()->size().toSize()));
    return result;
}

QRect DockSurfaceItem::groupContentRect(GroupId group) const
{
    const auto view = groups_.value(group);
    return view ? view->contentRectGlobal() : QRect{};
}

QRect DockSurfaceItem::centralRectGlobal() const
{
    return QRect(center_->mapToGlobal(QPointF()).toPoint(), center_->size().toSize());
}

QHash<GroupId, QRect> DockSurfaceItem::tabBarRects() const
{
    QHash<GroupId, QRect> result;
    for (auto it = groups_.cbegin(); it != groups_.cend(); ++it)
        if (it.value() && it.value()->isVisible())
            if (const auto strip = it.value()->stripRectGlobal(); !strip.isEmpty()) result.insert(it.key(), strip);
    return result;
}

QVector<DockStrip> DockSurfaceItem::dockStrips() const
{
    QVector<DockStrip> result;
    for (const auto& view : groups_)
        if (view && view->isVisible())
            if (const auto panel = view->stripPanelRectGlobal(); !panel.isEmpty()) result.append({panel, view->stripEdge()});
    return result;
}

int DockSurfaceItem::tabInsertionIndex(GroupId group, QPoint global, TabLayoutInput::Carried carried) const
{
    const auto view = groups_.value(group);
    return view ? view->insertionIndex(global, carried) : -1;
}

std::optional<bool> DockSurfaceItem::tabInsertionRow(GroupId, QPoint) const { return std::nullopt; }

QHash<Edge, QRect> DockSurfaceItem::edgeBarRects() const
{
    QHash<Edge, QRect> result;
    for (const auto edge : {Edge::Left, Edge::Right, Edge::Top, Edge::Bottom})
        if (const auto& rail = rails_[size_t(edge)]; rail && rail->isVisible())
            result.insert(edge, QRect(rail->mapToGlobal(QPointF()).toPoint(), rail->size().toSize()));
    return result;
}

// Inserting among a rail's buttons comes with the sidebar drag; until then a
// drop on a rail appends to it and the whole rail is the preview.
int DockSurfaceItem::edgeTabInsertionIndex(Edge edge, QPoint global, QRect carriedGlobal) const
{
    const auto& rail = rails_[size_t(edge)];
    return rail && rail->isVisible() ? rail->insertionIndex(global, carriedGlobal) : -1;
}

// The rail's gap once the drag has opened it, the whole rail before that.
QRect DockSurfaceItem::edgeTabInsertionRect(Edge edge, int) const
{
    const auto gap = railInsertionRectGlobal(edge);
    return gap.isEmpty() ? edgeBarRects().value(edge) : gap;
}

void DockSurfaceItem::setDraggedTab(TabId tab)
{
    for (const auto& view : groups_) if (view) view->setDraggedTab(tab);
}

void DockSurfaceItem::setDraggedFolder(FolderId folder)
{
    for (const auto& view : groups_) if (view) view->setDraggedFolder(folder);
}

void DockSurfaceItem::showInsertion(GroupId group, int index, const QVector<CarriedLabel>& carried, bool carriedSticky,
                                    TabLayoutInput::Carried kind)
{
    if (insertionGroup_ != group)
        if (const auto previous = groups_.value(std::exchange(insertionGroup_, group))) previous->setInsertionPreview(-1, {}, false, kind);
    if (const auto view = groups_.value(group)) view->setInsertionPreview(index, carried, carriedSticky, kind);
}

void DockSurfaceItem::showGuides(const DropTarget& target, int count, bool whole) { guides_->setTarget(target, count, whole); }
void DockSurfaceItem::clearGuides() { guides_->clear(); }

void DockSurfaceItem::setDragPresentation(bool compact)
{
    for (const auto& view : groups_) if (view) view->setDragPresentation(compact);
}

bool DockSurfaceItem::setDragTabStyle(const DockSurfaceItem& target, GroupId group, bool sticky)
{
    const auto* view = target.groups_.value(group).data();
    if (!view) return false;
    bool changed = false;
    for (const auto& carried : groups_) if (carried) changed = carried->setDragTabStyle(*view, sticky) || changed;
    return changed;
}

// As the Widgets DockSurface: every visible strip's tabs, in this item's
// coordinates, with the first strip's anchor and current tab.
TabDragVisual DockSurfaceItem::dragLabelVisual() const
{
    TabDragVisual visual;
    QRect bounds;
    QPoint anchor;
    for (const auto& view : groups_) if (view && view->isVisible()) {
        const auto rect = view->carrierBounds();
        if (rect.isEmpty()) continue;
        const auto offset = view->mapToItem(this, QPointF()).toPoint();
        if (visual.pieces.isEmpty()) anchor = offset + view->carrierAnchor();
        bounds |= rect.translated(offset);
        visual.pieces.append(view->carrierPiece(offset));
        if (visual.identity.isNull() && !view->currentTab().isNull()) {
            visual.active = view->currentTabRect().translated(offset);
            visual.identity = view->currentTab();
        }
    }
    if (visual.empty()) return visual;
    for (auto& piece : visual.pieces) piece.offset -= bounds.topLeft();
    visual.size = bounds.size();
    visual.anchor = anchor - bounds.topLeft();
    visual.active.translate(-bounds.topLeft());
    return visual;
}

LabelPositions DockSurfaceItem::carrierDropPositions() const
{
    QRect bounds;
    for (const auto& view : groups_) if (view && view->isVisible())
        bounds |= view->carrierBounds().translated(view->mapToItem(this, QPointF()).toPoint());
    LabelPositions positions;
    for (const auto& view : groups_) if (view && view->isVisible() && !view->carrierBounds().isEmpty()) {
        const auto offset = view->mapToItem(this, QPointF()).toPoint() - bounds.topLeft();
        const auto rects = view->carrierTabRects();
        for (auto it = rects.cbegin(); it != rects.cend(); ++it) positions.insert(it.key(), it.value().translated(offset));
    }
    return positions;
}

void DockSurfaceItem::showIncoming(GroupId group, const TabDragVisual& visual, QPoint global)
{
    if (incomingGroup_ != group)
        if (const auto previous = groups_.value(std::exchange(incomingGroup_, group))) previous->setIncomingVisual({}, {});
    if (const auto view = groups_.value(group)) view->setIncomingVisual(visual, global);
}

QImage DockSurfaceItem::activeContentPicture() const
{
    const auto* window = manager_.model().window(window_);
    const auto view = window ? groups_.value(window->activeGroup) : QPointer<GroupItem>{};
    return view ? view->pagePicture() : QImage{};
}

void DockSurfaceItem::showContentPreview(GroupId group, const QImage& picture)
{
    if (previewGroup_ != group)
        if (const auto previous = groups_.value(std::exchange(previewGroup_, group))) previous->setContentPreview({});
    if (const auto view = groups_.value(group)) view->setContentPreview(picture);
}

void DockSurfaceItem::showRailInsertion(std::optional<Edge> edge, int index, int extent)
{
    if (railInsertion_ != edge)
        if (const auto previous = std::exchange(railInsertion_, edge))
            if (const auto& rail = rails_[size_t(*previous)]) rail->setInsertionPreview(-1, 0);
    if (edge) if (const auto& rail = rails_[size_t(*edge)]) rail->setInsertionPreview(index, extent);
}

// As the Widgets DockSurface: the buttons the tabs would get there, with
// the number the next button would show.
int DockSurfaceItem::railExtent(Edge edge, const QVector<TabId>& tabs) const
{
    const auto appearance = manager_.tabAppearance();
    const auto& rail = rails_[size_t(edge)];
    int number = rail ? int(rail->buttons().size()) : 0;
    int extent = 0;
    for (const auto tab : tabs) if (const auto* record = manager_.model().tab(tab)) {
        ++number;
        extent += RailButtonItem::extent(record->title, appearance.showSidebarIcons && manager_.tabOptions(tab).showIcon,
            appearance.showSidebarNumbers ? number : 0, manager_.interfaceTheme().metrics.edgeThickness,
            appearance.clipSidebarTitles, appearance.maximumSidebarTitleWidth);
    }
    return extent;
}

// As the Widgets carrier over a rail (tab strip design 6.7), but painted
// by the target as the strips paint theirs: the buttons the tabs would get
// there, numbered as railExtent() measures them, the first one centered on
// the pointer.
void DockSurfaceItem::showRailCarrier(std::optional<Edge> edge, const QVector<TabId>& tabs, QPoint global)
{
    if (!edge || tabs.isEmpty()) {
        delete railCarrier_.data();
        railCarrierEdge_.reset(); railCarrierTabs_.clear();
        return;
    }
    const bool vertical = *edge == Edge::Left || *edge == Edge::Right;
    if (!railCarrier_ || railCarrierEdge_ != edge || railCarrierTabs_ != tabs) {
        delete railCarrier_.data();
        railCarrier_ = new QQuickItem(this);
        railCarrier_->setZ(15);
        railCarrierEdge_ = edge; railCarrierTabs_ = tabs;
        const auto theme = manager_.interfaceTheme();
        const auto& rail = rails_[size_t(*edge)];
        int ordinal = rail ? int(rail->buttons().size()) : 0;
        qreal along = 0;
        QSizeF size;
        for (const auto tab : tabs) if (const auto* record = manager_.model().tab(tab)) {
            auto* button = new RailButtonItem(theme, tab, record->title, vertical, theme.metrics.edgeThickness, railCarrier_);
            button->setAcceptedMouseButtons(Qt::NoButton);
            button->setAcceptHoverEvents(false);
            configureRailButton(*button, ++ordinal);
            button->setChecked(true);
            const QSizeF extent = button->preferredSize();
            button->setSize(extent);
            button->setPosition(vertical ? QPointF(0, along) : QPointF(along, 0));
            along += vertical ? extent.height() : extent.width();
            size = vertical ? QSizeF(std::max(size.width(), extent.width()), along)
                            : QSizeF(along, std::max(size.height(), extent.height()));
        }
        railCarrier_->setSize(size);
    }
    const auto buttons = railCarrier_->childItems();
    const auto anchor = buttons.isEmpty() ? QPointF() : buttons.first()->boundingRect().center() + buttons.first()->position();
    railCarrier_->setPosition(mapFromGlobal(QPointF(global)) - anchor);
}

QRect DockSurfaceItem::railInsertionRectGlobal(Edge edge) const
{
    const auto& rail = rails_[size_t(edge)];
    return rail ? rail->insertionPreviewRect() : QRect{};
}

void DockSurfaceItem::clearDragPresentation()
{
    showInsertion({}, -1, {}, false, TabLayoutInput::Carried::Tab);
    showIncoming({}, {}, {});
    showContentPreview({}, {});
    showRailInsertion({}, -1, 0);
    showRailCarrier({}, {}, {});
    for (const auto& rail : rails_) if (rail) rail->setDraggedTab({});
    clearGuides();
    for (const auto& view : groups_) if (view) {
        view->clearLiftedTab();
        view->setDraggedTab({});
        view->setDraggedFolder({});
    }
}

bool DockSurfaceItem::scrollStripAt(QPoint global, QPoint delta, bool smooth)
{
    for (const auto& view : groups_)
        if (view && view->isVisible() && view->scrollStripAt(global, delta, smooth)) return true;
    for (const auto& rail : rails_)
        if (rail && rail->isVisible() && rail->viewportRectGlobal().contains(global)) {
            rail->scrollByWheel(delta, smooth);
            return true;
        }
    return false;
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
