// SPDX-License-Identifier: MIT
#include "tab_group_geometry.hpp"
#include <algorithm>

namespace window_system {

int tabGroupColumnLimit(int groupWidth, int contentMinimum, int margin, int gap, int minimum, int maximum)
{
    return std::clamp(groupWidth - std::max(0, contentMinimum) - 2 * margin - gap, minimum, maximum);
}

QSize tabGroupAvailableSize(const TabGroupGeometryInput& input)
{
    const auto area = input.area();
    if (input.vertical()) return {input.columnWidth, std::max(0, area.height() - input.rowHeight - (input.titled ? input.rowHeight : 0))};
    const int width = input.compact && input.normalDragStyle ? std::min(area.width(), input.dragWidth) : area.width();
    return {std::max(1, width), area.height()};
}

TabGroupGeometry layoutTabGroup(const TabGroupGeometryInput& input)
{
    TabGroupGeometry result;
    const auto area = input.area();
    const int margin = input.margin();
    result.available = tabGroupAvailableSize(input);
    result.content = area;
    result.cardVisible = !input.compact && !input.edgeToEdge;
    if (input.barVisible && input.vertical()) {
        result.reveal = std::max(input.expansion, input.peek);
        const int titleHeight = input.titled ? input.rowHeight : 0;
        const int reserved = tabColumnExtent(input.iconColumnWidth, input.columnWidth, input.expansion);
        const int shown = tabColumnExtent(input.iconColumnWidth, input.columnWidth, result.reveal);
        const bool left = input.placement == TabBarPlacement::Left;
        const int x = area.left() + (left ? 0 : area.width() - shown);
        constexpr int pad = 3;
        result.panelOutset = qRound(input.columnOutset * input.peek);
        const int outset = result.panelOutset;
        result.panel = QRect(x - pad, area.top() - pad - outset, shown + 2 * pad, area.height() + 2 * (pad + outset));
        result.panelBody = QRect(pad, pad, shown, area.height() + 2 * outset);
        result.panelVisible = true;
        if (input.titled) {
            const int inset = input.verticalInset + 4;
            result.title = QRect(pad + inset, pad + outset, std::max(0, shown - 2 * inset), titleHeight);
        }
        result.titleVisible = input.titled && result.reveal > 0;
        result.bar = QRect(pad, pad + outset + titleHeight, shown, result.available.height());
        result.footer = QRect(pad, pad + outset + titleHeight + result.available.height(), shown, input.rowHeight);
        result.footerVisible = !input.compact;
        const int gap = input.compact ? 0 : input.columnGap;
        result.content = left ? QRect(area.left() + reserved + gap, area.top(), area.width() - reserved - gap, area.height())
            : QRect(area.left(), area.top(), area.width() - reserved - gap, area.height());
        const int edge = area.left() + (left ? reserved + gap - margin : area.width() - reserved - gap + margin);
        result.resizeHandle = QRect(edge - input.resizeBand / 2, area.top(), input.resizeBand, area.height());
        result.resizeVisible = (!input.iconMode || input.resizing) && !input.compact;
    } else if (input.barVisible) {
        const bool top = input.placement != TabBarPlacement::Bottom;
        result.bar = QRect(area.left(), top ? area.top() : area.bottom() + 1 - input.shownRows.height(),
            result.available.width(), input.shownRows.height());
        result.content = QRect(area.left(), top ? area.top() + input.reservedRows.height() : area.top(),
            area.width(), std::max(0, area.height() - input.reservedRows.height()));
    }
    result.card = result.content.adjusted(-margin, -margin, margin, margin);
    result.cardBody = QRect(QPoint(margin, margin), result.content.size());
    return result;
}

TabColumnControls layoutTabColumnControls(const TabColumnControlsInput& input)
{
    TabColumnControls result;
    const int room = input.shownWidth - 2 * input.inset;
    result.toggle = !input.iconsOnly && room >= input.newTabWidth + input.toggleWidth + 2;
    const int toggle = result.toggle ? input.toggleWidth : 0;
    const int types = input.typeWidth + input.controlGap;
    const int actions = input.actionCount * (input.actionWidth + 2);
    result.actions = !input.iconsOnly && !input.compact && room >= input.newTabWidth + types + toggle + actions + 2;
    const int remaining = room - toggle - (result.actions ? actions : 0) - 2;
    result.types = !input.iconsOnly && remaining >= input.newTabWidth + types;
    const int fullLabel = input.newTabWidth + input.textAdvance + 8;
    result.text = !input.iconsOnly && remaining >= fullLabel + (result.types ? types : 0);
    const int label = result.text ? fullLabel : input.newTabWidth;
    result.primary = QSize(label, std::max(1, input.rowHeight - 4));
    result.combined = QSize((result.types ? types : 0) + label, input.rowHeight);
    return result;
}

} // namespace window_system
