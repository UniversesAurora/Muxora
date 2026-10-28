// SPDX-License-Identifier: MIT
#include "tab_strip_arrangement.hpp"
#include <QFontMetrics>
#include <algorithm>

namespace window_system {

TabLayoutInput tabStripLayoutInput(const TabStripArrangementInput& source, QSize area, bool collapsed)
{
    auto input = source.labels;
    const auto& geometry = source.geometry;
    const auto& rows = geometry.rows;
    input.orientation = geometry.vertical() ? TabLayoutInput::Orientation::Vertical : TabLayoutInput::Orientation::Horizontal;
    input.rowHeight = geometry.rowHeight;
    input.width = area.width();
    if (geometry.vertical()) input.height = area.height();
    else {
        input.multiline = rows.multiline; input.stickyMultiline = rows.stickyMultiline;
        input.separateStickyRow = rows.separateSticky;
        input.collapsed = collapsed && !rows.showAll; input.stickyCollapsed = collapsed && !rows.stickyShowAll;
        input.height = !rows.wrapped() || source.group.height() <= 0 ? 0
            : int(source.group.height() * (rows.overlay() ? 0.5 : 0.4));
        input.trailingExtent = source.controls.trailingWidth;
        input.endExtent = source.controls.endWidth + (source.controls.countShown ? source.controls.countSize.width() : 0);
    }
    return input;
}

TabStripRowMeasure measureTabStripRows(const TabStripArrangementInput& source, QSize available)
{
    auto input = tabStripLayoutInput(source, QSize(available.width(), 0), true);
    input.height = std::max(0, int(available.height() * 0.4));
    input.block.clear(); input.insertion = -1; input.removed = -1; input.removedFolder = -1;
    const auto& geometry = source.geometry;
    const int closed = std::max(geometry.rowHeight, layoutTabs(input).size.height()) + geometry.outerMargin;
    int push = closed, overlay = closed;
    if (geometry.rows.collapsible()) {
        input = tabStripLayoutInput(source, QSize(available.width(), 0), false);
        input.height = std::max(0, int(available.height() * 0.4));
        input.block.clear(); input.insertion = -1; input.removed = -1; input.removedFolder = -1;
        push = std::max(geometry.rowHeight, layoutTabs(input).size.height()) + geometry.outerMargin;
        // The overlay keeps the live drag gap; reservation ignores it.
        input = tabStripLayoutInput(source, QSize(available.width(), 0), false);
        input.height = std::max(0, int(available.height() * 0.5));
        overlay = std::min(available.height(), layoutTabs(input).size.height() + geometry.outerMargin + geometry.shadowBlur);
    }
    return {available, closed, push, overlay};
}

QSize tabStripReservedSize(const TabStripGeometry& geometry, QSize available, const TabStripRowMeasure& rows)
{
    if (geometry.vertical()) return {geometry.iconsOnly ? geometry.iconColumnWidth
        : std::clamp(available.width(), geometry.minimumColumnWidth, geometry.maximumColumnWidth), available.height()};
    const auto& state = geometry.rows;
    const qreal progress = state.collapsible() && state.expansion == TabRowExpansion::Push && !state.temporaryOverlay ? state.reveal : 0;
    return {available.width(), qRound(rows.closed + (rows.push - rows.closed) * progress)};
}

QSize tabStripDisplaySize(const TabStripGeometry& geometry, QSize available, const TabStripRowMeasure& rows,
                         const TabLayoutInput* preview)
{
    auto size = tabStripReservedSize(geometry, available, rows);
    if (geometry.vertical()) return size;
    if (geometry.rows.overlay()) size.setHeight(qRound(size.height() + (rows.overlay - size.height()) * geometry.rows.reveal));
    else if (preview) size.setHeight(std::max(size.height(), layoutTabs(*preview).size.height() + geometry.outerMargin));
    return size;
}

TabStripControlRects tabStripControlRects(const TabStripGeometry& geometry, const TabLayout& layout,
                                         const TabStripControls& controls)
{
    TabStripControlRects result;
    if (geometry.vertical()) return result;
    if (!layout.trailing.isNull() && controls.trailingPresent)
        result.trailing = QRect(layout.trailing.topLeft() + geometry.origin(), QSize(controls.trailingWidth, geometry.rowHeight));
    if (!layout.end.isNull()) {
        auto area = layout.end.translated(geometry.origin());
        if (controls.endPresent) {
            result.end = QRect(area.right() - controls.endWidth + 1, area.top(), controls.endWidth, geometry.rowHeight);
            area.setRight(area.right() - controls.endWidth);
        }
        if (controls.countShown && controls.ownerVisible)
            result.count = QRect(QPoint(area.right() - controls.countSize.width() + 1, area.top()), controls.countSize);
    }
    return result;
}

TabStripArrangement arrangeTabStrip(const TabStripArrangementInput& source, QSize size)
{
    TabStripArrangement result;
    auto working = source;
    auto& controls = working.controls;
    const auto& geometry = source.geometry;
    const auto area = geometry.area(size);
    bool countShown = false;
    if (geometry.rows.collapsible() && !geometry.vertical()) {
        auto probe = tabStripLayoutInput(source, area, true); probe.block.clear(); probe.insertion = -1;
        probe.endExtent = controls.endWidth;
        countShown = layoutTabs(probe).hidden > 0;
    }
    controls.countShown = controls.countPresent && countShown && !geometry.rows.dragExpanded;
    // Shrink once, then grow only: proportional digits can otherwise cycle
    // between a wider button with more hidden rows and a narrower one.
    bool canShrink = true;
    for (;;) {
        result.input = tabStripLayoutInput(working, area, geometry.rows.collapsed());
        result.layout = layoutTabs(result.input);
        if (!controls.countShown || !controls.ownerVisible) break;
        result.countMeasured = true;
        result.countText = geometry.rows.collapsed() ? QStringLiteral("+%1").arg(result.layout.hidden) : QStringLiteral("\u2212");
        const int needed = std::max(controls.countMinimumWidth, QFontMetrics(source.font).horizontalAdvance(result.countText) + controls.countPadding);
        const int width = controls.countSize.width();
        if (needed == width || (needed < width && !canShrink)) break;
        canShrink = false;
        controls.countSize = QSize(needed, geometry.rowHeight);
    }
    result.controls = controls;
    result.controlRects = tabStripControlRects(geometry, result.layout, controls);
    result.stickyOffset = source.labels.stickyOffset; result.regularOffset = source.labels.regularOffset;
    result.rowsOffset = source.labels.rowsOffset;
    result.collapsedStickyRow = source.labels.collapsedStickyRow; result.collapsedRegularRow = source.labels.collapsedRegularRow;
    // Return clamped state separately from the input used for this layout.
    for (const auto& lane : result.layout.laneList) {
        if (!geometry.vertical() && lane.vertical) result.rowsOffset = lane.offset;
        else (lane.sticky ? result.stickyOffset : result.regularOffset) = lane.offset;
    }
    if (!geometry.vertical() && geometry.rows.collapsed()) {
        result.collapsedStickyRow = result.layout.stickyRow;
        result.collapsedRegularRow = result.layout.regularRow;
    }
    return result;
}

} // namespace window_system
