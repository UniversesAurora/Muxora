// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/layout_model.hpp>

namespace window_system {

enum class ViewMode { Normal, WindowFullscreen, GroupFullscreen, PaneFullscreen };
enum class WindowBar { Toolbars, StatusBar, TabBars, SideBars };

struct BarVisibility {
    bool toolbars = true;
    bool statusBar = true;
    bool tabBars = true;
    bool sideBars = true;
    bool operator==(const BarVisibility&) const = default;
};

// Stable IDs select a projection of the existing layout; entering a focused
// scope neither moves nor duplicates its content. Effective visibility is
// derived from the window's visibility preferences and Zen mode, not persisted
// as a second set of fullscreen preferences.
struct WindowViewState {
    ViewMode mode = ViewMode::Normal;
    bool zen = false;
    GroupId group;
    TabId tab;
    PaneId pane;
    BarVisibility effective;
    bool operator==(const WindowViewState&) const = default;
};

} // namespace window_system
