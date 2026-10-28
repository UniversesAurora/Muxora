// SPDX-License-Identifier: MIT
#pragma once

#include "tab_visual_style.hpp"
#include <window_system/tab_appearance.hpp>
#include <QIcon>
#include <memory>
#include <optional>

namespace window_system {

// GUI resource descriptions shared by live labels and retained frames.
// An adapter prepares fonts and icon pixels before scene-graph consumption.
struct TabStripLabelStyle {
    TabMetrics metrics;
    TabPalette palette;
    QFont font;
    Qt::LayoutDirection direction = Qt::LeftToRight;
    int logicalDpiX = 0;
    int logicalDpiY = 0;
    QIcon closeIcon;
    QIcon stickyIcon;
    QIcon addIcon;
};

struct TabLabelContent {
    std::shared_ptr<const TabStripLabelStyle> style;
    QString title;
    QString number;
    int numberExtent = 0;
    QIcon icon;
    std::optional<TabColorPreset> preset;
};

} // namespace window_system
