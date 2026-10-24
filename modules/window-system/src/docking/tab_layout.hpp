// SPDX-License-Identifier: MIT
#pragma once

#include <QPoint>
#include <QRect>
#include <QSize>
#include <QVector>

namespace window_system {

// The geometry of one tab strip, computed without widgets. Painting, clicks,
// tooltips, drag targets and accessibility all read this one result
// (docs/design/tab-strip-design.md, ADR 0011).
//
// Coordinates belong to the tab area: the strip without its outer margin and,
// for a vertical strip, without its footer. A lane is one scrolled region:
// a single row of a one-row strip, all rows of a multi-row strip, or one
// section of a vertical strip.
struct TabLayoutInput {
    enum class Orientation { Horizontal, Vertical };
    Orientation orientation = Orientation::Horizontal;
    int width = 0;
    // Vertical: the height for both sections. Horizontal multi-row: the
    // most the rows may take before they scroll; 0 leaves them unlimited.
    int height = 0;
    int rowHeight = 30;
    int rowGap = 2;
    // Horizontal: every tab's width as shown, sticky sizing already applied.
    QVector<int> extents;
    int stickyCount = 0;
    bool multiline = false;
    bool separateStickyRow = false;
    // One row with sticky and other tabs together: the sticky tabs stay at
    // the lane's start while the others scroll under them.
    bool freezeSticky = false;
    // Vertical: sticky tabs as icon tiles instead of rows.
    bool stickyTiles = false;
    QSize tile{36, 32};
    // Vertical rows and tiles keep this distance from both sides.
    int inset = 6;
    // Horizontal: the controls after the last tab (count, new tab, list).
    int trailingExtent = 0;
    // Multi-row: show one row of each section.
    bool collapsed = false;
    int current = -1;
    // The rows a collapsed strip shows for a section that does not hold the
    // current tab; clamped to the section's rows.
    int collapsedStickyRow = 0;
    int collapsedRegularRow = 0;
    // Scroll positions along each lane's axis; clamped in the result.
    int stickyOffset = 0;
    int regularOffset = 0;
    // Drag. `removed` is a tab of this strip that left its slot. A carried
    // block opens a gap of `block` cells at `insertion`, an index among the
    // remaining tabs, in the sticky or the regular section.
    int removed = -1;
    int insertion = -1;
    bool insertionSticky = false;
    QVector<int> block;
};

struct TabLayoutLane {
    QRect viewport;
    // The content length along the lane's axis, and the clamped offset.
    int extent = 0;
    int offset = 0;
    bool vertical = false;
    bool sticky = false;
    // A frozen sticky run covers this much of the viewport's start.
    int frozen = 0;
    [[nodiscard]] int maximumOffset() const { return qMax(0, extent - (vertical ? viewport.height() : viewport.width())); }
};

struct TabLayout {
    // Per tab: the displayed rectangle (null when collapsed away or removed)
    // and its lane (-1 then).
    QVector<QRect> tabs;
    QVector<int> lanes;
    QVector<QRect> gap;
    int gapLane = -1;
    QVector<TabLayoutLane> laneList;
    QRect trailing;
    int trailingLane = -1;
    // Tabs a collapsed strip hides, and the rows it shows.
    int hidden = 0;
    int stickyRow = 0;
    int regularRow = 0;
    // The tab area's preferred size.
    QSize size;
    // The line after a frozen sticky run or between vertical sections.
    QRect separator;
    // The input's sticky count, and the shown rows that hold sticky tabs on
    // rows of their own.
    int sticky = 0;
    int stickyRows = 0;

    // The part of a tab its lane shows; empty when it is scrolled away.
    [[nodiscard]] QRect visibleRect(int index) const;
    [[nodiscard]] int tabAt(QPoint point) const;
};

[[nodiscard]] TabLayout layoutTabs(const TabLayoutInput& input);

struct TabInsertion {
    int index = -1;
    bool sticky = false;
};
// Where a carried block lands when the center of its grabbed label is at
// `probe`. Decided on the strip without any gap, so the gap that opens never
// moves the decision. Right after the sticky tabs a block stays sticky up to
// `stickyReach` past their end; callers pass half the width of a block that
// was sticky.
[[nodiscard]] TabInsertion tabInsertionAt(TabLayoutInput input, QPoint probe, int stickyReach = 0);

} // namespace window_system
