// SPDX-License-Identifier: MIT
#include "input_method_scopes.hpp"
#include <QWidget>
#include <array>

namespace window_system {
namespace {
constexpr std::array scopeKeys{"application", "window", "floatingWindow", "tabGroup", "tab", "pane"};
const QUuid applicationKey{QStringLiteral("{4ccbaaea-0b5b-4cb4-b5fb-1cc24c871889}")};
}

QString inputMethodScopeKey(InputMethodScope scope)
{
    const auto index = int(scope);
    return index >= 0 && index < int(scopeKeys.size()) ? QString::fromLatin1(scopeKeys[index]) : QString{};
}

std::optional<InputMethodScope> inputMethodScopeFromKey(const QString& key)
{
    for (int index = 0; index < int(scopeKeys.size()); ++index)
        if (key == QLatin1String(scopeKeys[index])) return InputMethodScope(index);
    return {};
}

InputMethodScopes::InputMethodScopes(std::unique_ptr<InputMethodBackend> backend)
    : backend_(std::move(backend)) {}

void InputMethodScopes::reset()
{
    states_.clear(); active_ = {}; focus_.clear(); modelState_ = 0;
}

void InputMethodScopes::setScope(InputMethodScope scope)
{
    if (scope_ == scope) return;
    scope_ = scope;
    reset();
}

QUuid InputMethodScopes::key(const LayoutModel& model, PaneId pane) const
{
    const auto tab = model.tabForPane(pane);
    const auto group = model.groupForTab(tab);
    const auto* record = model.group(group);
    const auto* window = record ? model.window(record->windowId) : nullptr;
    if (!window) return {};
    switch (scope_) {
    case InputMethodScope::Application: return applicationKey;
    case InputMethodScope::Window: return window->familyId;
    case InputMethodScope::FloatingWindow: return window->id;
    case InputMethodScope::TabGroup: return group;
    case InputMethodScope::Tab: return tab;
    case InputMethodScope::Pane: return pane;
    }
    return {};
}

QUuid InputMethodScopes::widgetKey(const LayoutModel& model, QWidget* widget) const
{
    if (!widget) return {};
    // Popup/dialog windows can have a pane as QObject parent. Stop at the
    // actual top-level so they never become a remembered content range.
    auto* top = widget->window();
    if (!model.window(top->property("windowId").toUuid())) return {};
    for (auto* current = widget; current; current = current->parentWidget()) {
        const auto pane = current->property("paneId").toUuid();
        if (!pane.isNull()) return key(model, pane);
        if (current == top) break;
    }
    const auto* window = model.window(top->property("windowId").toUuid());
    const auto* group = model.group(window->activeGroup);
    const auto* tab = group ? model.tab(group->activeTab) : nullptr;
    return tab ? key(model, tab->activePane) : QUuid{};
}

void InputMethodScopes::synchronize(const LayoutModel& model)
{
    if (modelState_ == model.stateId()) return;
    modelState_ = model.stateId();
    QSet<QUuid> live;
    for (const auto& tab : model.tabs())
        for (const auto pane : model.panes(tab.id)) live.insert(key(model, pane));
    // Capture once at creation time, before refresh reparents or hides editors.
    // Every new range inherits the source's current state, even before focus.
    InputMethodState inherited;
    bool captured = false;
    for (const auto& id : live) {
        if (states_.contains(id)) continue;
        if (!captured) { inherited = backend_->capture(focus_); captured = true; }
        states_.insert(id, inherited);
    }
    for (auto it = states_.begin(); it != states_.end();)
        if (!live.contains(it.key())) it = states_.erase(it); else ++it;
    // Keep the previous identity until focus reaches the destination, even
    // when it was deleted. The surviving destination must still be restored.
    if (live.isEmpty()) { active_ = {}; focus_.clear(); }
}

void InputMethodScopes::focus(const LayoutModel& model, QWidget* widget)
{
    const auto next = widgetKey(model, widget);
    if (next.isNull()) return; // Temporary UI and other applications do not switch ranges.
    if (next == active_) { focus_ = widget; return; }
    if (!active_.isNull() && states_.contains(active_))
        states_[active_] = backend_->capture(focus_);
    if (active_.isNull()) {
        // First entry (including returning after deletion/reset) starts from
        // the actual system state rather than restoring a startup snapshot.
        if (!states_.contains(next)) states_.insert(next, backend_->capture(widget));
        else if (!focus_) states_[next] = backend_->capture(widget);
    } else if (states_.contains(next)) {
        backend_->restore(states_.value(next), widget);
    } else {
        states_.insert(next, states_.value(active_));
    }
    active_ = next;
    focus_ = widget;
}

} // namespace window_system
