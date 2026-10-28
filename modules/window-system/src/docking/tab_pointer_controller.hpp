// SPDX-License-Identifier: MIT
#pragma once

#include "drag_start.hpp"
#include <Qt>
#include <utility>

namespace window_system {

struct TabPointerHit {
    int tab = -1;
    // On release, test the pressed action rectangle independently of tab.
    int action = -1;
    int folder = -1;
    bool folderAdd = false;
};

struct TabPointerDecision {
    enum class Action { None, ActivateTab, CloseTab, UnstickTab, ToggleFolder, AddTabToFolder, DragTab, DragFolder };
    Action action = Action::None;
    int index = -1;
    DragStart start;
};

// The adapter supplies hits and executes decisions. Consumed press state is
// cleared before returning because the resulting action may destroy the host.
class TabPointerController final {
public:
    [[nodiscard]] TabPointerDecision press(Qt::MouseButton button, TabPointerHit hit, DragStart start)
    {
        if (button == Qt::MiddleButton) { middlePressed_ = hit.tab; return {}; }
        if (button != Qt::LeftButton) return {};
        pressStart_ = start;
        if (hit.tab < 0 && hit.folder >= 0) { folderPressed_ = hit.folder; return {}; }
        if (hit.tab >= 0 && hit.action == hit.tab) { actionPressed_ = hit.tab; return {}; }
        tabPressed_ = hit.tab;
        return hit.tab >= 0 ? TabPointerDecision{Action::ActivateTab, hit.tab} : TabPointerDecision{};
    }

    [[nodiscard]] TabPointerDecision move(Qt::MouseButtons buttons, QPoint global, int dragDistance)
    {
        if (!buttons.testFlag(Qt::LeftButton) || !pressStart_.crossedThreshold(global, dragDistance)) return {};
        if (folderPressed_ >= 0)
            return {Action::DragFolder, std::exchange(folderPressed_, -1), pressStart_.movedTo(global)};
        if (tabPressed_ >= 0)
            return {Action::DragTab, std::exchange(tabPressed_, -1), pressStart_.movedTo(global)};
        return {};
    }

    [[nodiscard]] TabPointerDecision release(Qt::MouseButton button, TabPointerHit hit,
                                            int stickyCount, bool protectStickyMiddle)
    {
        if (button == Qt::MiddleButton) {
            const int index = std::exchange(middlePressed_, -1);
            if (index >= 0 && hit.tab == index && !(index < stickyCount && protectStickyMiddle))
                return {Action::CloseTab, index};
            return {};
        }
        tabPressed_ = -1;
        if (folderPressed_ >= 0) {
            const int index = std::exchange(folderPressed_, -1);
            if (button == Qt::LeftButton && hit.folder == index)
                return {hit.folderAdd ? Action::AddTabToFolder : Action::ToggleFolder, index};
            return {};
        }
        if (actionPressed_ >= 0) {
            const int index = std::exchange(actionPressed_, -1);
            if (button == Qt::LeftButton && hit.action == index)
                return {index < stickyCount ? Action::UnstickTab : Action::CloseTab, index};
        }
        return {};
    }

    [[nodiscard]] int pressedTab() const { return tabPressed_; }
    [[nodiscard]] int pressedAction() const { return actionPressed_; }
    [[nodiscard]] int pressedFolder() const { return folderPressed_; }
    void cancelTabs() { tabPressed_ = actionPressed_ = middlePressed_ = -1; }
    void cancelFolders() { folderPressed_ = -1; }

private:
    using Action = TabPointerDecision::Action;
    DragStart pressStart_;
    int tabPressed_ = -1;
    int actionPressed_ = -1;
    int middlePressed_ = -1;
    int folderPressed_ = -1;
};

} // namespace window_system
