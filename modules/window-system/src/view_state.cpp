// SPDX-License-Identifier: MIT
#include "window_manager_p.hpp"
#include "view_state_p.hpp"

namespace window_system {
namespace {

bool& visibleFlag(BarVisibility& bars, WindowBar bar)
{
    switch (bar) {
    case WindowBar::Toolbars: return bars.toolbars;
    case WindowBar::StatusBar: return bars.statusBar;
    case WindowBar::TabBars: return bars.tabBars;
    case WindowBar::SideBars: return bars.sideBars;
    }
    Q_UNREACHABLE();
}

} // namespace

bool validViewMode(ViewMode mode)
{
    return mode == ViewMode::Normal || mode == ViewMode::WindowFullscreen
        || mode == ViewMode::GroupFullscreen || mode == ViewMode::PaneFullscreen;
}

bool validZenFullscreenMode(ViewMode mode)
{
    return mode == ViewMode::WindowFullscreen || mode == ViewMode::GroupFullscreen || mode == ViewMode::PaneFullscreen;
}

BarVisibility WindowManager::preferredBarVisibility(WindowId window) const
{
    if (!d->model.window(window)) return {false, false, false, false};
    const auto options = d->options.value(window);
    return {options.toolbarsVisible, options.statusBarVisible, options.tabBarsVisible, options.sideBarsVisible};
}

WindowViewState WindowManager::windowViewState(WindowId window) const
{
    auto state = d->viewStates.value(window);
    if (!d->model.window(window)) { state.effective = {false, false, false, false}; return state; }
    state.effective = preferredBarVisibility(window);
    if (state.mode == ViewMode::GroupFullscreen) {
        if (const auto* group = d->model.group(state.group)) {
            state.tab = group->activeTab;
            if (const auto* tab = d->model.tab(state.tab)) state.pane = tab->activePane;
        }
    }
    if (state.zen) state.effective = {false, false, false, false};
    return state;
}

BarVisibility WindowManager::barVisibility(WindowId window) const { return windowViewState(window).effective; }
ViewMode WindowManager::viewMode(WindowId window) const { return d->viewStates.value(window).mode; }
bool WindowManager::zenMode(WindowId window) const { return d->viewStates.value(window).zen; }
bool WindowManager::tabBarsVisible(WindowId window) const { return barVisibility(window).tabBars; }
bool WindowManager::sideBarsVisible(WindowId window) const { return barVisibility(window).sideBars; }
bool WindowManager::toolbarsVisible(WindowId window) const { return barVisibility(window).toolbars; }
bool WindowManager::statusBarVisible(WindowId window) const { return barVisibility(window).statusBar; }
bool WindowManager::setTabBarsVisible(WindowId window, bool visible) { return setBarVisible(window, WindowBar::TabBars, visible); }
bool WindowManager::setSideBarsVisible(WindowId window, bool visible) { return setBarVisible(window, WindowBar::SideBars, visible); }
bool WindowManager::setToolbarsVisible(WindowId window, bool visible) { return setBarVisible(window, WindowBar::Toolbars, visible); }
bool WindowManager::setStatusBarVisible(WindowId window, bool visible) { return setBarVisible(window, WindowBar::StatusBar, visible); }

bool WindowManager::setBarVisible(WindowId window, WindowBar bar, bool visible)
{
    if (!d->model.window(window) || !d->windows.value(window)) return false;
    auto& state = d->viewStates[window];
    // Zen is a projection, not a second preference store. Hiding an already
    // hidden bar leaves the pre-Zen settings untouched, including other windows.
    if (state.zen && !visible) return true;
    const bool leavingZen = state.zen;
    if (visible) state.zen = false;
    bool changed = leavingZen;
    auto bars = preferredBarVisibility(window);
    changed = changed || visibleFlag(bars, bar) != visible;
    visibleFlag(bars, bar) = visible;
    auto& options = d->options[window];
    options.toolbarsVisible = bars.toolbars; options.statusBarVisible = bars.statusBar;
    options.tabBarsVisible = bars.tabBars; options.sideBarsVisible = bars.sideBars;
    const auto kind = d->model.window(window)->kind;
    d->windowVisibility[window].insert(kind, bars);
    // Only an explicit edit updates the shared default. Merely saving an
    // older window must never roll it back to that window's stale values.
    auto profile = d->visibilityProfiles.value(kind);
    changed = changed || visibleFlag(profile, bar) != visible;
    visibleFlag(profile, bar) = visible;
    d->visibilityProfiles.insert(kind, profile);
    if (!changed) return true;
    QPointer<WindowManager> self(this);
    applyWindowViewState(window);
    if (self) emit layoutChanged();
    return true;
}

void WindowManager::syncVisibilityKind(WindowId window)
{
    const auto* record = d->model.window(window);
    if (!record) return;
    const auto kind = record->kind;
    if (d->visibilityKinds.value(window, kind) == kind) { d->visibilityKinds.insert(window, kind); return; }
    const auto previous = d->visibilityKinds.value(window);
    d->windowVisibility[window].insert(previous, preferredBarVisibility(window));
    const auto bars = d->windowVisibility.value(window).value(kind, d->visibilityProfiles.value(kind));
    auto& options = d->options[window];
    options.toolbarsVisible = bars.toolbars; options.statusBarVisible = bars.statusBar;
    options.tabBarsVisible = bars.tabBars; options.sideBarsVisible = bars.sideBars;
    d->windowVisibility[window].insert(kind, bars); d->visibilityKinds.insert(window, kind);
}

bool WindowManager::setViewMode(WindowId window, ViewMode mode)
{
    if (!validViewMode(mode) || !d->model.window(window) || !d->windows.value(window)
        || (d->drag && d->drag->isActive())) return false;
    auto state = d->viewStates.value(window);
    if (state.mode == mode) return true;
    state.mode = mode; state.group = GroupId(); state.tab = TabId(); state.pane = PaneId();
    if (mode == ViewMode::Normal) state.zen = false;
    if (mode == ViewMode::GroupFullscreen || mode == ViewMode::PaneFullscreen) {
        const auto active = context(window);
        const auto* group = d->model.group(active.group);
        const auto* tab = d->model.tab(active.tab);
        if (!group || group->windowId != window || !tab || !d->model.panes(active.tab).contains(active.pane)) return false;
        state.group = active.group; state.tab = active.tab; state.pane = active.pane;
    }
    d->viewStates.insert(window, state);
    QPointer<WindowManager> self(this);
    applyWindowViewState(window);
    if (self) emit layoutChanged();
    return true;
}

bool WindowManager::setZenMode(WindowId window, bool enabled)
{
    if (!d->model.window(window) || !d->windows.value(window)) return false;
    auto state = d->viewStates.value(window);
    if (state.zen == enabled) return true;
    if (enabled && state.mode == ViewMode::Normal) {
        if (!validZenFullscreenMode(d->behavior.zenFullscreenMode) || (d->drag && d->drag->isActive())) return false;
        state.mode = d->behavior.zenFullscreenMode;
        state.group = GroupId{}; state.tab = TabId{}; state.pane = PaneId{};
        if (state.mode != ViewMode::WindowFullscreen) {
            const auto active = context(window);
            const auto* group = d->model.group(active.group);
            const auto* tab = d->model.tab(active.tab);
            if (!group || group->windowId != window || !tab || !d->model.panes(active.tab).contains(active.pane)) return false;
            state.group = active.group; state.tab = active.tab; state.pane = active.pane;
        }
    }
    state.zen = enabled;
    // Enter the requested fullscreen scope and Zen together: observers never
    // see an intermediate fullscreen frame with the bars still visible.
    d->viewStates.insert(window, state);
    QPointer<WindowManager> self(this);
    applyWindowViewState(window);
    if (self) emit layoutChanged();
    return true;
}

void WindowManager::reconcileWindowViewState(WindowId window)
{
    auto& state = d->viewStates[window];
    if (state.mode == ViewMode::Normal || state.mode == ViewMode::WindowFullscreen) return;
    if (state.mode == ViewMode::PaneFullscreen) {
        const auto tab = d->model.tabForPane(state.pane);
        const auto group = d->model.groupForTab(tab);
        const auto* record = d->model.group(group);
        if (!record || record->windowId != window) { state = {}; return; }
        state.tab = tab; state.group = group;
        const auto* target = d->model.tab(tab);
        if (record->activeTab != tab || target->activePane != state.pane)
            d->model.activatePane(tab, state.pane);
    } else {
        const auto* group = d->model.group(state.group);
        if (!group || group->windowId != window || group->activeTab.isNull()) { state = {}; return; }
        state.tab = group->activeTab;
        state.pane = d->model.tab(state.tab)->activePane;
    }
}

void WindowManager::applyWindowViewState(WindowId id)
{
    if (!d->model.window(id)) return;
    reconcileWindowViewState(id);
    if (auto window = d->windows.value(id)) {
        window->setOptions(d->options.value(id));
        window->applyViewState(windowViewState(id));
    }
}

void WindowManager::applyWindowViewStates()
{
    const auto order = d->model.windowOrder();
    const QPointer<WindowManager> self(this);
    for (const auto id : order) {
        applyWindowViewState(id);
        if (!self) return;
    }
}

void WindowManager::leaveFocusScopeFor(TabId tab, PaneId pane)
{
    const auto group = d->model.groupForTab(tab);
    const auto* record = d->model.group(group); if (!record) return;
    const auto state = d->viewStates.value(record->windowId);
    if ((state.mode == ViewMode::GroupFullscreen && state.group != group)
        || (state.mode == ViewMode::PaneFullscreen && (state.tab != tab || (!pane.isNull() && state.pane != pane))))
        setViewMode(record->windowId, ViewMode::WindowFullscreen);
}

} // namespace window_system
