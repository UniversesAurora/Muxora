// SPDX-License-Identifier: MIT
#include "input_method_scopes.hpp"
#include <QWidget>

namespace window_system {
namespace {
PaneId paneForWidget(const LayoutModel& model, QWidget* widget)
{
    if (!widget) return {};
    // A popup can have a pane as QObject parent. Stop at its real top-level;
    // dialogs without a model window never become a remembered content range.
    auto* top = widget->window();
    const auto* window = model.window(top->property("windowId").toUuid());
    if (!window) return {};
    for (auto* current = widget; current; current = current->parentWidget()) {
        const auto pane = current->property("paneId").toUuid();
        if (!pane.isNull()) return pane;
        if (current == top) break;
    }
    const auto* group = model.group(window->activeGroup);
    const auto* tab = group ? model.tab(group->activeTab) : nullptr;
    return tab ? tab->activePane : PaneId{};
}
}

void InputMethodScopes::focus(const LayoutModel& model, QWidget* widget)
{
    focus(model, paneForWidget(model, widget), widget ? widget->window()->windowHandle() : nullptr);
}

InputMethodState InputMethodBackend::capture(QWidget* focus)
{
    return capture(focus ? focus->window()->windowHandle() : nullptr);
}

void InputMethodBackend::restore(const InputMethodState& state, QWidget* focus)
{
    restore(state, focus ? focus->window()->windowHandle() : nullptr);
}

} // namespace window_system
