// SPDX-License-Identifier: MIT
#pragma once

#include <QColor>
#include <QString>
#include <QVector>

namespace window_system {

enum class TabBarPlacement { Top, Bottom };

inline constexpr int minimumTabTitleWidth = 16;
inline constexpr int maximumTabTitleWidth = 2048;
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
    QVector<TabColorPreset> colors = defaultTabColorPresets();
    bool operator==(const TabAppearance&) const = default;
};

} // namespace window_system
