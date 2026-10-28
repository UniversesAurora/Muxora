// SPDX-License-Identifier: MIT
#pragma once

#include "tab_layout.hpp"
#include <window_system/tab_appearance.hpp>
#include <QFont>

namespace window_system {

[[nodiscard]] inline QPoint tabStripOrigin(TabBarPlacement placement, int outerMargin, int shadowExtent)
{
    if (placement == TabBarPlacement::Left || placement == TabBarPlacement::Right) return {};
    return {0, placement == TabBarPlacement::Bottom ? shadowExtent : outerMargin};
}

struct TabStripRows {
    bool multiline = false;
    bool showAll = true;
    bool stickyMultiline = false;
    bool stickyShowAll = true;
    bool separateSticky = false;
    bool hasSticky = false;
    bool expanded = false;
    bool dragExpanded = false;
    bool temporaryOverlay = false;
    TabRowExpansion expansion = TabRowExpansion::Overlay;
    qreal reveal = 0;

    [[nodiscard]] bool wrapped() const { return multiline || (separateSticky && hasSticky && stickyMultiline); }
    [[nodiscard]] bool collapsible() const {
        return (multiline && !showAll) || (separateSticky && hasSticky && stickyMultiline && !stickyShowAll);
    }
    [[nodiscard]] bool collapsed() const { return collapsible() && !expanded && !dragExpanded; }
    [[nodiscard]] bool overlay() const {
        return collapsible() && ((temporaryOverlay && (dragExpanded || reveal > 0))
            || ((expanded || reveal > 0) && expansion == TabRowExpansion::Overlay));
    }
};

struct TabStripGeometry {
    TabBarPlacement placement = TabBarPlacement::Top;
    TabStripRows rows;
    int outerMargin = 0;
    int shadowBlur = 0;
    int rowHeight = 30;
    bool iconsOnly = false;
    int iconColumnWidth = 0;
    int minimumColumnWidth = 0;
    int maximumColumnWidth = 0;

    [[nodiscard]] bool vertical() const { return placement == TabBarPlacement::Left || placement == TabBarPlacement::Right; }
    [[nodiscard]] int shadowExtent() const { return !vertical() && rows.overlay() ? qRound(shadowBlur * rows.reveal) : 0; }
    [[nodiscard]] QPoint origin() const {
        return tabStripOrigin(placement, outerMargin, shadowExtent());
    }
    [[nodiscard]] QSize area(QSize size) const {
        return {size.width(), qMax(0, size.height() - (vertical() ? 0 : outerMargin) - shadowExtent())};
    }
};

struct TabStripControls {
    int trailingWidth = 0;
    int endWidth = 0;
    bool trailingPresent = false;
    bool endPresent = false;
    QSize countSize;
    int countMinimumWidth = 0;
    int countPadding = 0;
    bool countPresent = false;
    // Own visibility reserves space even when the adapter is not exposed.
    bool countShown = false;
    bool ownerVisible = false;
};

// The adapter measures labels, headers and controls. Arrangement owns no
// controls, callbacks, timers or paint devices; its result is also usable
// by a non-Widgets renderer.
struct TabStripArrangementInput {
    TabStripGeometry geometry;
    TabLayoutInput labels;
    QSize group;
    TabStripControls controls;
    QFont font;
};

struct TabStripRowMeasure { QSize available; int closed = 0; int push = 0; int overlay = 0; };
struct TabStripControlRects { QRect trailing; QRect end; QRect count; };
struct TabStripArrangement {
    TabLayoutInput input;
    TabLayout layout;
    TabStripControls controls;
    QString countText;
    bool countMeasured = false;
    TabStripControlRects controlRects;
    int stickyOffset = 0;
    int regularOffset = 0;
    int rowsOffset = 0;
    int collapsedStickyRow = 0;
    int collapsedRegularRow = 0;
};

[[nodiscard]] TabLayoutInput tabStripLayoutInput(const TabStripArrangementInput& input, QSize area, bool collapsed);
[[nodiscard]] TabStripRowMeasure measureTabStripRows(const TabStripArrangementInput& input, QSize available);
[[nodiscard]] QSize tabStripReservedSize(const TabStripGeometry& geometry, QSize available, const TabStripRowMeasure& rows = {});
[[nodiscard]] QSize tabStripDisplaySize(const TabStripGeometry& geometry, QSize available, const TabStripRowMeasure& rows,
                                      const TabLayoutInput* preview = nullptr);
[[nodiscard]] TabStripControlRects tabStripControlRects(const TabStripGeometry& geometry, const TabLayout& layout,
                                                       const TabStripControls& controls);
[[nodiscard]] TabStripArrangement arrangeTabStrip(const TabStripArrangementInput& input, QSize size);

} // namespace window_system
