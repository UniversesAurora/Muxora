// SPDX-License-Identifier: MIT
#pragma once

#include "elevation.hpp"
#include "window_state_color.hpp"
#include <window_system/theme.hpp>
#include <QFont>
#include <QSize>

namespace window_system {

// Every dimension of a tab strip in one place (tab strip design 2.1). The
// layout, the painter and the widget take their numbers from here, so a
// change of sizes is one edit. Values are logical pixels.
struct TabMetrics {
    int rowHeight = 28;
    int rowGap = 2;
    // The strip's own background shows above (or below) horizontal tabs.
    int outerMargin = 3;
    int radius = 6;
    int controlRadius = 4;
    int accentLine = 2;
    int separator = 1;
    int edgeFade = 16;
    int titleFade = 12;
    int scrollbar = 5;
    int newTabWidth = 26;
    int typeWidth = 12;
    int listWidth = 24;
    int controlGap = 1;
    int countMinimumWidth = 26;
    int verticalInset = 4;
    int columnGap = 1;
    int columnOverlayOutset = 4;
    QSize tile{26, 26};
    int verticalAccent = 3;
    int verticalAccentLength = 16;
    int verticalIconWidth = 44;
    int resizeBand = 10;
    int iconCollapseTravel = 12;
    int shadowBlur = int(elevationProfile(ElevationLevel::RowsOverlay).key.blur);
    int iconSize = 16;
    // Tab group cards (window interaction design: cards and focus): the
    // space a group keeps around its card and the card's corner radius.
    int cardMargin = 2;
    int cardRadius = 8;
    // Tab folders (tab strip design 12): a horizontal header is a chip this
    // much shorter than a tab, padded inside and spaced outside; without a
    // name it is a dot. The folder's line, the active tab's outline in the
    // folder color, and the indent of vertical folder rows.
    int folderChipInset = 3;
    int folderChipPadding = 8;
    int folderChipMargin = 4;
    int folderDot = 12;
    int folderLine = 2;
    qreal folderOutline = 1.5;
    int folderIndent = 12;
    int folderButton = 20;

    // Rows grow with a large font instead of clipping their text.
    [[nodiscard]] static TabMetrics forTheme(const Theme& theme, const QFont& font);
};

// The colors a tab strip uses, taken from the theme. When the theme becomes
// a color registry (phase 2), only this mapping changes.
struct TabPalette {
    QColor strip;
    QColor active;
    QColor hover;
    QColor pressed;
    QColor text;
    QColor mutedText;
    QColor accent;
    QColor separator;
    QColor border;
    QColor shadow;
    ElevationStyle elevation;
    WindowStateColor tileBorder;
    WindowStateColor tileHover;
    WindowStateColor tilePressed;
    WindowStateColor sectionSeparator;
    [[nodiscard]] static TabPalette forTheme(const Theme& theme);
};

[[nodiscard]] ElevationStyle elevationStyleForTheme(const Theme& theme);

} // namespace window_system
