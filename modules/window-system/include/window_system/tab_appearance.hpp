// SPDX-License-Identifier: MIT
#pragma once

#include <QColor>
#include <QString>
#include <QVector>

namespace window_system {

enum class TabBarPlacement { Top, Bottom };
// How sticky tabs are sized, as VS Code's workbench.editor.pinnedTabSizing:
// their natural width, only an icon or initial, or a fixed width that shows
// the start of the title.
enum class StickyTabSizing { Normal, Compact, Shrink };
// Which ways of closing leave a sticky tab open, as VS Code's
// workbench.editor.preventPinnedEditorClose: the close-tab shortcut, a
// middle click, both or neither.
enum class StickyTabCloseGuard { KeyboardAndMouse, Keyboard, Mouse, Never };

inline constexpr int compactStickyTabWidth = 38;
inline constexpr int shrinkStickyTabWidth = 80;

inline constexpr int minimumTabTitleWidth = 0;
inline constexpr int maximumTabTitleWidth = 1000000;
inline constexpr int defaultTabTitleWidth = 160;

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
    bool wheelExpandRows = false;
    bool showIcons = true;
    bool showNewTabButton = true;
    bool showNumbers = false;
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
    // Sticky tabs take a row of their own above the other tabs, as VS Code's
    // workbench.editor.pinnedTabsOnSeparateRow.
    bool stickyTabsOnSeparateRow = false;
    StickyTabCloseGuard preventStickyClose = StickyTabCloseGuard::KeyboardAndMouse;
    QVector<TabColorPreset> colors = defaultTabColorPresets();
    bool operator==(const TabAppearance&) const = default;
};

} // namespace window_system
