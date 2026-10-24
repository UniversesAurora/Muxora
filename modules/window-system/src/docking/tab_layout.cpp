// SPDX-License-Identifier: MIT
#include "tab_layout.hpp"

#include <algorithm>
#include <limits>
#include <optional>

namespace window_system {
namespace {

// One placed thing: a tab (>= 0) or a gap cell (-1 - cell).
struct Item {
    int id = 0;
    int extent = 0;
    [[nodiscard]] bool isTab() const { return id >= 0; }
};

struct Sections {
    QVector<Item> sticky;
    QVector<Item> regular;
};

Sections sectionsOf(const TabLayoutInput& input)
{
    Sections result;
    const int count = int(input.extents.size());
    for (int index = 0; index < count; ++index) {
        if (index == input.removed) continue;
        (index < input.stickyCount ? result.sticky : result.regular).append({index, input.extents.at(index)});
    }
    if (input.insertion >= 0 && !input.block.isEmpty()) {
        auto& section = input.insertionSticky ? result.sticky : result.regular;
        const int start = input.insertionSticky ? 0 : int(result.sticky.size());
        const int position = std::clamp(input.insertion - start, 0, int(section.size()));
        for (int cell = int(input.block.size()) - 1; cell >= 0; --cell)
            section.insert(position, {-1 - cell, input.block.at(cell)});
    }
    return result;
}

void place(TabLayout& layout, const Item& item, const QRect& rect, int lane)
{
    if (item.isTab()) { layout.tabs[item.id] = rect; layout.lanes[item.id] = lane; }
    else {
        const int cell = -1 - item.id;
        if (layout.gap.size() <= cell) layout.gap.resize(cell + 1);
        layout.gap[cell] = rect; layout.gapLane = lane;
    }
}

int clampOffset(int offset, int extent, int length) { return std::clamp(offset, 0, std::max(0, extent - length)); }

// One horizontal row that scrolls sideways.
void layoutSingleRows(const TabLayoutInput& input, const Sections& sections, TabLayout& layout)
{
    const int row = input.rowHeight + input.rowGap;
    const bool separate = input.separateStickyRow && input.stickyCount > 0;
    struct Placed { Item item; QRect rect; bool sticky; };
    const auto lay = [&](const QVector<Item>& items, int y, int& x, bool sticky, QVector<Placed>& out) {
        for (const auto& item : items) { out.append({item, QRect(x, y, item.extent, input.rowHeight), sticky}); x += item.extent; }
    };
    const auto finish = [&](QVector<Placed>& placed, int y, int extent, int offset, bool sticky, int frozen, bool trailing) {
        TabLayoutLane lane;
        lane.viewport = QRect(0, y, input.width, input.rowHeight);
        if (trailing && input.trailingExtent > 0) {
            layout.trailing = QRect(extent, y, input.trailingExtent, input.rowHeight);
            extent += input.trailingExtent;
            layout.trailingLane = int(layout.laneList.size());
        }
        lane.extent = extent; lane.sticky = sticky; lane.frozen = frozen;
        lane.offset = clampOffset(offset, extent, input.width);
        const int index = int(layout.laneList.size());
        for (auto& entry : placed) place(layout, entry.item, entry.sticky && frozen ? entry.rect : entry.rect.translated(-lane.offset, 0), index);
        if (layout.trailingLane == index) layout.trailing.translate(-lane.offset, 0);
        layout.laneList.append(lane);
    };
    if (separate) {
        QVector<Placed> top; int x = 0;
        lay(sections.sticky, 0, x, true, top);
        const bool regularLane = !sections.regular.isEmpty();
        finish(top, 0, x, input.stickyOffset, true, 0, !regularLane);
        if (regularLane) {
            QVector<Placed> bottom; int bx = 0;
            lay(sections.regular, row, bx, false, bottom);
            finish(bottom, row, bx, input.regularOffset, false, 0, true);
        }
    } else {
        QVector<Placed> placed; int x = 0;
        lay(sections.sticky, 0, x, true, placed);
        const int frozen = input.freezeSticky && !sections.sticky.isEmpty() ? x : 0;
        lay(sections.regular, 0, x, false, placed);
        finish(placed, 0, x, input.regularOffset, false, frozen, true);
        if (frozen) layout.separator = QRect(frozen - 1, 0, 1, input.rowHeight);
    }
    if (separate) layout.stickyRows = 1;
    const int rows = int(layout.laneList.size());
    layout.size = QSize(input.width, rows * input.rowHeight + std::max(0, rows - 1) * input.rowGap);
}

// Rows that wrap; collapsed, one row of each section.
void layoutWrappedRows(const TabLayoutInput& input, const Sections& sections, TabLayout& layout)
{
    const bool separate = input.separateStickyRow && input.stickyCount > 0;
    const int wrap = std::max(1, input.width - (input.collapsed ? input.trailingExtent : 0));
    struct Row { QVector<std::pair<Item, int>> items; int end = 0; bool sticky = false; };
    QVector<Row> rows;
    const auto wrapItems = [&](const QVector<Item>& items, bool sticky, bool newRow) {
        if (newRow || rows.isEmpty()) rows.append(Row{{}, 0, sticky});
        for (auto item : items) {
            item.extent = std::min(item.extent, wrap);
            if (rows.last().end && rows.last().end + item.extent > wrap) rows.append(Row{{}, 0, sticky});
            rows.last().items.append({item, rows.last().end});
            rows.last().end += item.extent;
        }
    };
    if (separate) {
        wrapItems(sections.sticky, true, true);
        if (!sections.regular.isEmpty()) wrapItems(sections.regular, false, true);
    } else {
        QVector<Item> all = sections.sticky; all += sections.regular;
        wrapItems(all, false, true);
    }
    int stickyRows = 0;
    for (const auto& row : rows) stickyRows += row.sticky ? 1 : 0;
    const auto rowOf = [&](int tab) {
        for (int index = 0; index < rows.size(); ++index)
            for (const auto& [item, x] : rows.at(index).items) if (item.id == tab) return index;
        return -1;
    };
    QVector<int> shown;
    if (input.collapsed) {
        const int currentRow = input.current >= 0 && input.current != input.removed ? rowOf(input.current) : -1;
        if (separate) {
            const bool currentSticky = currentRow >= 0 && currentRow < stickyRows;
            layout.stickyRow = currentSticky ? currentRow : std::clamp(input.collapsedStickyRow, 0, std::max(0, stickyRows - 1));
            shown.append(layout.stickyRow);
            if (rows.size() > stickyRows) {
                const int regularRows = int(rows.size()) - stickyRows;
                layout.regularRow = currentRow >= stickyRows ? currentRow - stickyRows
                    : std::clamp(input.collapsedRegularRow, 0, regularRows - 1);
                shown.append(stickyRows + layout.regularRow);
            }
        } else {
            layout.regularRow = currentRow >= 0 ? currentRow : std::clamp(input.collapsedRegularRow, 0, std::max(0, int(rows.size()) - 1));
            shown.append(layout.regularRow);
        }
    } else for (int index = 0; index < rows.size(); ++index) shown.append(index);
    // Trailing controls follow the last shown row, or start a row of their own.
    if (input.trailingExtent > 0) {
        auto& last = rows[shown.last()];
        if (!input.collapsed && last.end && last.end + input.trailingExtent > input.width) {
            rows.append(Row{{}, 0, last.sticky}); shown.append(int(rows.size()) - 1);
        }
    }
    const int pitch = input.rowHeight + input.rowGap;
    const int total = int(shown.size()) * pitch - input.rowGap;
    const int cap = input.height > 0 && !input.collapsed ? std::max(input.height, 2 * input.rowHeight + input.rowGap) : total;
    TabLayoutLane lane;
    lane.vertical = true;
    lane.viewport = QRect(0, 0, input.width, std::min(total, cap));
    lane.extent = total;
    lane.offset = clampOffset(input.regularOffset, total, lane.viewport.height());
    for (int position = 0; position < shown.size(); ++position) {
        const auto& row = rows.at(shown.at(position));
        const int y = position * pitch - lane.offset;
        for (const auto& [item, x] : row.items) place(layout, item, QRect(x, y, item.extent, input.rowHeight), 0);
        if (position + 1 == shown.size() && input.trailingExtent > 0) {
            layout.trailing = QRect(row.end, y, input.trailingExtent, input.rowHeight);
            layout.trailingLane = 0;
        }
    }
    for (const int index : shown) layout.stickyRows += separate && rows.at(index).sticky ? 1 : 0;
    for (int index = 0; index < rows.size(); ++index) if (!shown.contains(index))
        for (const auto& [item, x] : rows.at(index).items) layout.hidden += item.isTab() ? 1 : 0;
    layout.laneList.append(lane);
    layout.size = QSize(input.width, lane.viewport.height());
}

// A column: sticky rows or tiles above, then the other rows; each scrolls.
void layoutColumn(const TabLayoutInput& input, const Sections& sections, TabLayout& layout)
{
    const int rowWidth = std::max(1, input.width - 2 * input.inset);
    const bool hasSticky = input.stickyCount > 0;
    int top = 0;
    if (hasSticky) {
        QVector<std::pair<Item, QRect>> placed;
        int content = 0;
        if (input.stickyTiles) {
            const int perRow = std::max(1, (rowWidth + input.rowGap) / (input.tile.width() + input.rowGap));
            for (int index = 0; index < sections.sticky.size(); ++index) {
                const QRect rect(input.inset + (index % perRow) * (input.tile.width() + input.rowGap),
                                 (index / perRow) * (input.tile.height() + input.rowGap), input.tile.width(), input.tile.height());
                placed.append({sections.sticky.at(index), rect}); content = rect.bottom() + 1;
            }
        } else for (int index = 0; index < sections.sticky.size(); ++index) {
            const QRect rect(input.inset, index * (input.rowHeight + input.rowGap), rowWidth, input.rowHeight);
            placed.append({sections.sticky.at(index), rect}); content = rect.bottom() + 1;
        }
        TabLayoutLane lane;
        lane.vertical = true; lane.sticky = true;
        const int limit = input.height > 0 ? std::max(input.rowHeight, input.height / 3) : content;
        lane.viewport = QRect(0, 0, input.width, std::min(content, limit));
        lane.extent = content;
        lane.offset = clampOffset(input.stickyOffset, content, lane.viewport.height());
        for (const auto& [item, rect] : placed) place(layout, item, rect.translated(0, -lane.offset), 0);
        layout.laneList.append(lane);
        top = lane.viewport.height();
        // A line between the sections, with a little space around it.
        layout.separator = QRect(input.inset, top + 2, rowWidth, 1);
        top += 5;
    }
    TabLayoutLane lane;
    lane.vertical = true;
    lane.viewport = QRect(0, top, input.width, std::max(0, (input.height > 0 ? input.height : top) - top));
    int content = 0;
    QVector<std::pair<Item, QRect>> placed;
    for (int index = 0; index < sections.regular.size(); ++index) {
        const QRect rect(input.inset, top + index * (input.rowHeight + input.rowGap), rowWidth, input.rowHeight);
        placed.append({sections.regular.at(index), rect}); content = index * (input.rowHeight + input.rowGap) + input.rowHeight;
    }
    if (input.height <= 0) lane.viewport.setHeight(content);
    lane.extent = content;
    lane.offset = clampOffset(input.regularOffset, content, lane.viewport.height());
    const int laneIndex = int(layout.laneList.size());
    for (const auto& [item, rect] : placed) place(layout, item, rect.translated(0, -lane.offset), laneIndex);
    layout.laneList.append(lane);
    layout.size = QSize(input.width, input.height > 0 ? input.height : top + content);
}

} // namespace

QRect TabLayout::visibleRect(int index) const
{
    if (index < 0 || index >= tabs.size() || lanes.at(index) < 0) return {};
    const auto& lane = laneList.at(lanes.at(index));
    auto area = tabs.at(index).intersected(lane.viewport);
    // Tabs scrolling under a frozen sticky run show only what lies past it.
    if (lane.frozen && index >= sticky && area.left() < lane.viewport.left() + lane.frozen)
        area.setLeft(lane.viewport.left() + lane.frozen);
    return area.isValid() ? area : QRect{};
}

int TabLayout::tabAt(QPoint point) const
{
    // Sticky tabs lie above the tabs scrolling under them.
    for (int index = 0; index < tabs.size(); ++index) if (index < sticky && visibleRect(index).contains(point)) return index;
    for (int index = sticky; index < tabs.size(); ++index) if (visibleRect(index).contains(point)) return index;
    return -1;
}

TabLayout layoutTabs(const TabLayoutInput& input)
{
    TabLayout layout;
    const int count = int(input.extents.size());
    layout.tabs.resize(count);
    layout.lanes.fill(-1, count);
    layout.sticky = std::clamp(input.stickyCount, 0, count);
    const auto sections = sectionsOf(input);
    if (input.orientation == TabLayoutInput::Orientation::Vertical) layoutColumn(input, sections, layout);
    else if (input.multiline) layoutWrappedRows(input, sections, layout);
    else layoutSingleRows(input, sections, layout);
    return layout;
}

TabInsertion tabInsertionAt(TabLayoutInput input, QPoint probe, int stickyReach)
{
    input.insertion = -1; input.block.clear();
    const auto layout = layoutTabs(input);
    const int count = int(input.extents.size());
    QVector<int> remaining;
    for (int index = 0; index < count; ++index) if (index != input.removed) remaining.append(index);
    int stickyRemaining = 0;
    for (const int index : remaining) stickyRemaining += index < input.stickyCount ? 1 : 0;
    const bool vertical = input.orientation == TabLayoutInput::Orientation::Vertical;
    const auto center = [&](int index) { const auto rect = layout.tabs.at(index); return vertical ? rect.center().y() : rect.center().x(); };
    const int along = vertical ? probe.y() : probe.x();

    // A section of its own (separate sticky row or vertical section) decides
    // stickiness by where the probe is.
    std::optional<bool> section;
    if (input.stickyCount > 0) {
        if (vertical) section = probe.y() < (layout.laneList.size() > 1 ? layout.laneList.at(1).viewport.top() : std::numeric_limits<int>::max());
        else if (input.separateStickyRow) {
            const auto& lane = layout.laneList.value(0);
            const int pitch = input.rowHeight + input.rowGap;
            section = probe.y() + (input.multiline ? lane.offset : 0) < std::max(1, layout.stickyRows) * pitch - input.rowGap / 2;
        }
    }
    // The tabs on the probe's row (or all of a column section).
    QVector<int> candidates;
    for (const int index : remaining) {
        if (section && (index < input.stickyCount) != *section) continue;
        if (layout.lanes.at(index) < 0) continue;
        candidates.append(index);
    }
    if (!vertical && input.multiline) {
        int best = std::numeric_limits<int>::max(), rowTop = 0;
        for (const int index : candidates) {
            const auto rect = layout.tabs.at(index);
            const int distance = probe.y() < rect.top() ? rect.top() - probe.y() : probe.y() > rect.bottom() ? probe.y() - rect.bottom() : 0;
            if (distance < best) { best = distance; rowTop = rect.top(); }
        }
        QVector<int> row;
        for (const int index : candidates) if (layout.tabs.at(index).top() == rowTop) row.append(index);
        candidates = row;
    }
    if (vertical && input.stickyTiles && section && *section) {
        // Tiles: rows first, then the probe's position along its row.
        TabInsertion result{0, true};
        for (const int index : candidates) {
            const auto rect = layout.tabs.at(index);
            if (rect.bottom() < probe.y() - input.rowGap / 2 || (rect.top() <= probe.y() && rect.center().x() < probe.x()))
                ++result.index;
        }
        return result;
    }
    // Index among the remaining tabs: the first candidate's position in the
    // remaining order, plus the candidates whose center lies before the probe.
    int position = 0;
    int firstPosition = -1;
    for (int index = 0; index < remaining.size(); ++index) if (candidates.contains(remaining.at(index))) { firstPosition = index; break; }
    if (firstPosition < 0) {
        // An empty section: its start.
        position = section && !*section ? stickyRemaining : 0;
    } else {
        position = firstPosition;
        const auto& lane = layout.laneList.value(layout.lanes.at(candidates.first()));
        const bool overFrozen = lane.frozen && along < lane.viewport.left() + lane.frozen;
        for (const int index : candidates) {
            // Over a frozen sticky run only the sticky tabs count; past it
            // only the tabs scrolling under it do.
            if (lane.frozen && (index < input.stickyCount) != overFrozen) { if (index < input.stickyCount) ++position; continue; }
            if (center(index) < along) ++position;
        }
        // Past the last candidate of a section: right after it.
        if (section && !*section && position < stickyRemaining) position = stickyRemaining;
    }
    TabInsertion result{position, false};
    if (section) result.sticky = *section;
    else if (input.stickyCount > 0) {
        if (position < stickyRemaining) result.sticky = true;
        else if (position == stickyRemaining) {
            // Between the last sticky tab and the first other one: sticky up
            // to the end of the sticky run, or a little further for a block
            // that was sticky.
            int end = std::numeric_limits<int>::min();
            for (const int index : remaining) if (index < input.stickyCount && layout.lanes.at(index) >= 0) {
                const auto rect = layout.tabs.at(index);
                if (!input.multiline || probe.y() >= rect.top() - input.rowGap && probe.y() <= rect.bottom() + input.rowGap)
                    end = std::max(end, vertical ? rect.bottom() + 1 : rect.right() + 1);
            }
            if (end == std::numeric_limits<int>::min() && stickyRemaining == 0 && !layout.laneList.isEmpty())
                end = vertical ? layout.laneList.first().viewport.top() : layout.laneList.first().viewport.left() - layout.laneList.first().offset;
            result.sticky = end != std::numeric_limits<int>::min() && along < end + std::max(0, stickyReach);
        }
    }
    return result;
}

} // namespace window_system
