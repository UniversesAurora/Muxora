// SPDX-License-Identifier: MIT
#include "input_method_scopes.hpp"
#include <QWindow>
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

void InputMethodScopes::focus(const LayoutModel& model, PaneId pane, QWindow* window)
{
    const auto next = key(model, pane);
    if (next.isNull()) return; // Temporary UI and other applications do not switch ranges.
    if (next == active_) {
        // One logical range can move to a different native input context.
        // Preserve its live state even though its model identity is unchanged.
        if (focus_ != window && window) {
            if (focus_) states_[next] = backend_->capture(focus_);
            if (states_.contains(next)) backend_->restore(states_.value(next), window);
        }
        focus_ = window;
        return;
    }
    if (!active_.isNull() && states_.contains(active_))
        states_[active_] = backend_->capture(focus_);
    if (active_.isNull()) {
        // First entry (including returning after deletion/reset) starts from
        // the actual system state rather than restoring a startup snapshot.
        if (!states_.contains(next)) states_.insert(next, backend_->capture(window));
        else if (!focus_) states_[next] = backend_->capture(window);
    } else if (states_.contains(next)) {
        backend_->restore(states_.value(next), window);
    } else {
        states_.insert(next, states_.value(active_));
    }
    active_ = next;
    focus_ = window;
}

} // namespace window_system
