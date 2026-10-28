// SPDX-License-Identifier: MIT
#pragma once

#include "edge_fade.hpp"
#include "tab_painter.hpp"
#include "tab_label_content.hpp"
#include <QUuid>
#include <memory>

namespace window_system {

struct TabStripLabelSample {
    QUuid id;
    // A synchronous raster adapter may bind its source's title cache by index.
    // Retained consumers use the ID and owned presentation, never this index.
    int sourceIndex = -1;
    std::shared_ptr<const TabLabelContent> content;
    TabLabelPresentation label;
};

struct TabStripFolderSample { QRect area; TabFolderPaintState state; };
struct TabStripFolderLine { QRect area; QColor color; };
struct TabStripFolderOutline { QRect area; TabShape shape; QColor color; };
struct TabStripScrollbar { QRect thumb; qreal opacity; bool emphasized; };

// One independently clipped lane. Regular labels lie below a frozen run;
// folder lines/outline lie above both. Selected labels end their own run.
struct TabStripLaneFrame {
    QRect viewport;
    QRect regularClip;
    QVector<EdgeFade> fades;
    QVector<TabStripFolderSample> folders;
    QVector<TabStripLabelSample> regular;
    QVector<TabStripLabelSample> frozen;
    QVector<TabStripFolderLine> folderLines;
    std::optional<TabStripFolderOutline> folderOutline;
};

// The strip's base plane, excluding its window-level lifted/departing labels
// and child controls. Published values own data, not widgets or text caches.
struct TabStripFrame {
    std::shared_ptr<const TabStripLabelStyle> style;
    QFont font;
    Qt::LayoutDirection direction = Qt::LeftToRight;
    bool vertical = false;
    bool windowActive = true;
    QRect background;
    QVector<QRect> separators;
    QVector<TabStripLaneFrame> lanes;
    QVector<TabStripScrollbar> scrollbars;
    QRect sectionSeparator;
    QRect overlayShadow;
    bool shadowAbove = false;
    qreal shadowOpacity = 0;
};

} // namespace window_system
