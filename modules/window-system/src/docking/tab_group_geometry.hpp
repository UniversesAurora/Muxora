// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/tab_appearance.hpp>
#include <QRect>

namespace window_system {

[[nodiscard]] inline int tabColumnExtent(int icons, int titles, qreal reveal) { return qRound(icons + (titles - icons) * reveal); }

struct TabGroupGeometryInput {
    QRect bounds;
    TabBarPlacement placement = TabBarPlacement::Top;
    bool compact = false;
    bool edgeToEdge = false;
    bool barVisible = true;
    bool titled = false;
    bool iconMode = false;
    bool resizing = false;
    bool normalDragStyle = false;
    int dragWidth = 0;
    int cardMargin = 0;
    int columnGap = 0;
    int columnWidth = 0;
    int iconColumnWidth = 0;
    int columnOutset = 0;
    int verticalInset = 0;
    int rowHeight = 30;
    int resizeBand = 0;
    qreal expansion = 1;
    qreal peek = 0;
    // Horizontal row sizes come from the shared strip arrangement.
    QSize reservedRows;
    QSize shownRows;

    [[nodiscard]] bool vertical() const { return placement == TabBarPlacement::Left || placement == TabBarPlacement::Right; }
    [[nodiscard]] int margin() const { return compact || edgeToEdge ? 0 : cardMargin; }
    [[nodiscard]] QRect area() const { const int pad = margin(); return bounds.adjusted(pad, pad, -pad, -pad); }
};

struct TabGroupGeometry {
    QSize available;
    QRect content;
    QRect card;
    QRect cardBody; // In card coordinates.
    QRect panel;   // In group coordinates.
    QRect panelBody; // In panel coordinates.
    int panelOutset = 0;
    // Bar, footer and title use panel coordinates for a vertical strip,
    // group coordinates otherwise. All other output rectangles are local
    // to the group unless explicitly marked.
    QRect bar;
    QRect footer;
    QRect title;
    QRect resizeHandle;
    qreal reveal = 0;
    bool cardVisible = false;
    bool panelVisible = false;
    bool footerVisible = false;
    bool titleVisible = false;
    bool resizeVisible = false;
};

struct TabColumnControlsInput {
    int shownWidth = 0;
    int inset = 0;
    int rowHeight = 30;
    int newTabWidth = 0;
    int toggleWidth = 0;
    int typeWidth = 0;
    int controlGap = 0;
    int actionWidth = 0;
    int actionCount = 0;
    int textAdvance = 0;
    bool iconsOnly = false;
    bool compact = false;
};

struct TabColumnControls {
    bool toggle = false;
    bool actions = false;
    bool types = false;
    bool text = false;
    QSize primary;
    QSize combined;
};

[[nodiscard]] int tabGroupColumnLimit(int groupWidth, int contentMinimum, int margin, int gap, int minimum, int maximum);
[[nodiscard]] QSize tabGroupAvailableSize(const TabGroupGeometryInput& input);
[[nodiscard]] TabGroupGeometry layoutTabGroup(const TabGroupGeometryInput& input);
[[nodiscard]] TabColumnControls layoutTabColumnControls(const TabColumnControlsInput& input);

} // namespace window_system
