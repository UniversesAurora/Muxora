// SPDX-License-Identifier: MIT
#pragma once

#include "tab_drag_visual.hpp"
#include "style/tab_strip_frame.hpp"

namespace window_system {

struct TabForegroundShadow {
    SurfaceGeometry geometry;
    ElevationProfile body;
    ElevationProfile anchor;
    LabelPresencePose presence;
    bool incremental = false;
};

struct TabIncomingSample {
    QPoint origin;
    TabDragVisual visual;
    SurfaceGeometry geometry;
};

// Window-level plane, in strip coordinates. Incoming and locally carried
// labels precede settling labels; shadows precede every label contribution.
struct TabForegroundFrame {
    std::shared_ptr<const TabStripLabelStyle> style;
    QFont font;
    Qt::LayoutDirection direction = Qt::LeftToRight;
    bool vertical = false;
    ElevationStyle elevation;
    QVector<TabForegroundShadow> shadows;
    std::optional<TabIncomingSample> incoming;
    std::optional<TabStripFolderSample> folder;
    QVector<TabStripLabelSample> carried;
    std::optional<TabStripFolderLine> folderLine;
    QVector<TabStripLabelSample> settling;
};

} // namespace window_system
