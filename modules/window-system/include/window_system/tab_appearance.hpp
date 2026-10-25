// SPDX-License-Identifier: MIT
#pragma once

#include <QColor>
#include <QString>
#include <QVector>
#include <window_system/tab_strip_limits.hpp>

namespace window_system {

// Left and right make a vertical tab strip beside the content.
enum class TabBarPlacement { Top, Bottom, Left, Right };
// Whether multiple rows collapse to one row of each section, and how the
// expanded rows show: pushing the content down or as an overlay over it.
enum class TabRowCollapse { Off, Push, Overlay };
// How sticky tabs are sized, as VS Code's workbench.editor.pinnedTabSizing:
// their natural width, only an icon or initial, or the start of the title.
enum class StickyTabSizing { Normal, Compact, Shrink };
// Which ways of closing leave a sticky tab open, as VS Code's
// workbench.editor.preventPinnedEditorClose: the close-tab shortcut, a
// middle click, both or neither.
enum class StickyTabCloseGuard { KeyboardAndMouse, Keyboard, Mouse, Never };

inline constexpr int compactStickyTabWidth = 38;

inline constexpr int minimumTabTitleWidth = 0;
inline constexpr int maximumTabTitleWidth = 1000000;
inline constexpr int defaultTabTitleWidth = 160;
// Shrunk sticky tabs limit only their title text, separately from the regular
// title clipping. With an icon, the default makes a tab about as wide as VS
// Code's fixed 80.
inline constexpr int defaultShrinkStickyTitleWidth = 40;

// Every state is explicit. Hosts need not derive text or icon colors from a
// background, and the auto-hide rail remains independent of central tabs.
struct TabVisualColors {
    QColor background;
    QColor text;
    QColor line;
    QColor icon;
    bool operator==(const TabVisualColors&) const = default;
};

struct TabColorPreset {
    QString id;
    QString name;
    TabVisualColors groupActive;
    TabVisualColors groupInactive;
    TabVisualColors autoHideActive;
    TabVisualColors autoHideInactive;
    bool operator==(const TabColorPreset&) const = default;
};

[[nodiscard]] QVector<TabColorPreset> defaultTabColorPresets();

struct TabAppearance {
    TabBarPlacement centralPlacement = TabBarPlacement::Top;
    TabBarPlacement pinnedPlacement = TabBarPlacement::Top;
    bool multiline = false;
    TabRowCollapse collapseRows = TabRowCollapse::Off;
    // With collapsing rows, the wheel expands (down) and collapses (up).
    bool wheelExpandRows = false;
    int verticalTabWidth = defaultVerticalTabWidth;
    bool showIcons = true;
    bool showNewTabButton = true;
    bool showNumbers = false;
    // Show named groups' names in their strips (tab strip design 13).
    bool showGroupNames = true;
    bool showSidebarIcons = false;
    bool showSidebarNumbers = false;
    // Limit only the displayed text in Qt logical pixels, never the stored
    // title or the space reserved for icons, numbers, and close controls.
    bool clipTitles = false;
    int maximumTitleWidth = defaultTabTitleWidth;
    // Auto-hide rails have a separate text extent from regular and pinned bars.
    bool clipSidebarTitles = false;
    int maximumSidebarTitleWidth = defaultTabTitleWidth;
    // Compact and shrunk sticky tabs stay in view while the other tabs of a
    // single-row strip scroll under them.
    StickyTabSizing stickySizing = StickyTabSizing::Normal;
    int shrinkStickyTitleWidth = defaultShrinkStickyTitleWidth;
    // Sticky tabs take a row of their own above the other tabs, as VS Code's
    // workbench.editor.pinnedTabsOnSeparateRow.
    bool stickyTabsOnSeparateRow = false;
    StickyTabCloseGuard preventStickyClose = StickyTabCloseGuard::KeyboardAndMouse;
    QVector<TabColorPreset> colors = defaultTabColorPresets();
    bool operator==(const TabAppearance&) const = default;
};

} // namespace window_system
