// SPDX-License-Identifier: MIT
#pragma once

#include <Qt>
#include <algorithm>

namespace window_system {

struct TabKeyboardDecision {
    enum class Action { Ignore, Accept, ActivateTab, Collapse };
    Action action = Action::Ignore;
    int index = -1;
};

// Navigation follows model order across rows and sticky sections. The host
// applies selection, reveals its target and handles unconsumed keys normally.
[[nodiscard]] inline TabKeyboardDecision tabKeyboardDecision(
    int key, int current, int count, bool vertical, bool expanded)
{
    using Action = TabKeyboardDecision::Action;
    const bool backward = key == (vertical ? Qt::Key_Up : Qt::Key_Left);
    const bool forward = key == (vertical ? Qt::Key_Down : Qt::Key_Right);
    if ((backward || forward) && count)
        return {Action::ActivateTab, std::clamp(current + (forward ? 1 : -1), 0, count - 1)};
    if (key == Qt::Key_Home || key == Qt::Key_End)
        return count ? TabKeyboardDecision{Action::ActivateTab, key == Qt::Key_Home ? 0 : count - 1}
                     : TabKeyboardDecision{Action::Accept};
    if (key == Qt::Key_Escape && expanded) return {Action::Collapse};
    return {};
}

} // namespace window_system
