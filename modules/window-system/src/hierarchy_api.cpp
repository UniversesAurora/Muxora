// SPDX-License-Identifier: MIT
#include "window_manager_p.hpp"
#include <QVBoxLayout>
#include <QScopeGuard>

namespace window_system {
bool WindowManager::prepareHierarchyBars(const QVector<QPair<TabId, PaneId>> &tabs) {
    const QPointer<WindowManager> self(this);
    ++d->geometryUpdateDepth;
    const auto releaseGeometry = qScopeGuard([self] {
        if (self)
            --self->d->geometryUpdateDepth;
    });
    struct PendingBar {
        PaneId pane;
        bool top;
        QPointer<QWidget> content, outer, previous;
    };
    QVector<PendingBar> pending;
    for (const auto &[tab, pane] : tabs) {
        const QPointer<TabContent> view = d->tabs.value(tab);
        if (!view)
            continue;
        const auto content = d->panes.value(pane);
        for (bool top : {true, false}) {
            if (!view || d->tabs.value(tab) != view || d->panes.value(pane) != content ||
                d->model.tabForPane(pane).isNull())
                break;
            const auto previous = (top ? d->paneTopBars : d->paneBottomBars).value(pane);
            const QPointer<QWidget> outer =
                top ? view->takeTopBar(d->storage) : view->takeBottomBar(d->storage);
            if (!self)
                return false;
            if (!outer)
                continue;
            // Take every outer bar before combining ownership. The handoff
            // leaves the old pane presentations untouched until refresh.
            pending.append({pane, top, content, outer, previous});
        }
    }
    const auto discard = [this](const QPointer<QWidget> &bar) {
        if (bar && (!bar->parentWidget() || bar->parentWidget() == d->storage))
            bar->deleteLater();
    };
    for (const auto &item : pending) {
        if (!self)
            return false;
        const auto pane = item.pane;
        const bool top = item.top;
        const auto outer = item.outer, previous = item.previous;
        if (!outer)
            continue;
        auto current = [&] {
            return item.content && !d->model.tabForPane(pane).isNull() &&
                   d->panes.value(pane) == item.content &&
                   (top ? d->paneTopBars : d->paneBottomBars).value(pane) == previous;
        };
        if (!current()) {
            discard(outer);
            continue;
        }
        auto &bars = top ? d->paneTopBars : d->paneBottomBars;
        if (!previous) {
            bars.insert(pane, outer);
            outer->setParent(d->storage);
            if (!self)
                return false;
            continue;
        }
        const QPointer<QWidget> combined = new QWidget(d->storage);
        combined->setObjectName("ConvertedPaneBars");
        combined->setProperty("windowSystemManaged", true);
        auto *layout = new QVBoxLayout(combined);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        bars.insert(pane, combined);
        const auto stillOwned = [&] {
            return combined && item.content && !d->model.tabForPane(pane).isNull() &&
                   d->panes.value(pane) == item.content &&
                   (top ? d->paneTopBars : d->paneBottomBars).value(pane) == combined;
        };
        if (top)
            layout->addWidget(outer);
        else
            layout->addWidget(previous);
        if (!self || !combined)
            return false;
        if (!stillOwned()) {
            discard(combined);
            discard(outer);
            continue;
        }
        if (top) {
            if (previous)
                layout->addWidget(previous);
        } else if (outer)
            layout->addWidget(outer);
        if (!self)
            return false;
        if (!stillOwned()) {
            discard(combined);
            continue;
        }
        if (outer)
            outer->show();
        if (!self)
            return false;
        if (!stillOwned()) {
            discard(combined);
            continue;
        }
        if (previous)
            previous->show();
        if (!self)
            return false;
    }
    return true;
}

void WindowManager::usePerPaneBars(TabId tab) {
    for (const auto pane : d->model.panes(tab)) {
        auto &options = d->descriptors[pane];
        options.topBarSharing = PaneBarSharing::PerPane;
        options.bottomBarSharing = PaneBarSharing::PerPane;
    }
}

TabId WindowManager::convertGroupToTab(GroupId group, Placement placement) {
    const QPointer<WindowManager> self(this);
    d->drag->releaseForMutation();
    if (!self)
        return {};
    auto candidate = d->model;
    const auto result = candidate.convertGroupToTab(group, placement);
    if (result.isNull())
        return {};
    QVector<QPair<TabId, PaneId>> sources;
    for (const auto tab : d->model.group(group)->tabs)
        sources.append({tab, d->model.tab(tab)->activePane});
    // Publish one complete model before any QWidget ownership callback can run.
    d->model = std::move(candidate);
    usePerPaneBars(result);
    if (!prepareHierarchyBars(sources) || !self)
        return {};
    refresh();
    return self && d->model.tab(result) ? result : TabId{};
}

bool WindowManager::moveTabIntoTab(TabId source, TabId target, PaneId targetPane, Placement placement) {
    const QPointer<WindowManager> self(this);
    d->drag->releaseForMutation();
    if (!self)
        return false;
    auto candidate = d->model;
    if (!candidate.moveTabIntoTab(source, target, targetPane, placement))
        return false;
    const auto active = d->model.tab(source)->activePane;
    d->model = std::move(candidate);
    usePerPaneBars(target);
    if (!prepareHierarchyBars({{source, active}}) || !self)
        return false;
    refresh();
    return self && d->model.tab(target);
}

bool WindowManager::moveGroupIntoTab(GroupId source, TabId target, PaneId targetPane, Placement placement) {
    const QPointer<WindowManager> self(this);
    d->drag->releaseForMutation();
    if (!self)
        return false;
    auto candidate = d->model;
    if (!candidate.moveGroupIntoTab(source, target, targetPane, placement))
        return false;
    QVector<QPair<TabId, PaneId>> sources;
    for (const auto tab : d->model.group(source)->tabs)
        sources.append({tab, d->model.tab(tab)->activePane});
    d->model = std::move(candidate);
    usePerPaneBars(target);
    if (!prepareHierarchyBars(sources) || !self)
        return false;
    refresh();
    return self && d->model.tab(target);
}

GroupId WindowManager::convertTabToGroup(TabId tab, Placement placement) {
    const QPointer<WindowManager> self(this);
    d->drag->releaseForMutation();
    if (!self)
        return {};
    auto candidate = d->model;
    const auto result = candidate.convertTabToGroup(tab, placement);
    if (result.isNull())
        return {};
    const auto tabs = candidate.group(result)->tabs;
    for (int index = 0; index < tabs.size(); ++index)
        if (tabs[index] != tab) {
            const auto pane = candidate.tab(tabs[index])->activePane;
            const auto title = d->descriptors.value(pane).title;
            candidate.renameTab(tabs[index],
                                title.isEmpty() || title == candidate.tab(tab)->title
                                    ? candidate.tab(tab)->title + QStringLiteral(" · %1").arg(index + 1)
                                    : title);
        }
    d->model = std::move(candidate);
    for (const auto id : tabs)
        usePerPaneBars(id);
    refresh();
    return self && d->model.group(result) ? result : GroupId{};
}

TabId WindowManager::extractPaneToTab(TabId tab, PaneId pane) {
    const QPointer<WindowManager> self(this);
    d->drag->releaseForMutation();
    if (!self)
        return {};
    if (!d->model.panes(tab).contains(pane))
        return {};
    if (d->model.panes(tab).size() == 1)
        return tab;
    auto candidate = d->model;
    const auto result = candidate.extractPane(tab, pane, candidate.groupForTab(tab));
    if (result.isNull())
        return {};
    const auto title = d->descriptors.value(pane).title;
    if (!title.isEmpty())
        candidate.renameTab(result, title);
    d->model = std::move(candidate);
    usePerPaneBars(result);
    refresh();
    return self && d->model.tab(result) ? result : TabId{};
}
} // namespace window_system
