// SPDX-License-Identifier: MIT
#include "strip_layout.hpp"
#include <algorithm>
#include <limits>

namespace window_system {

QVector<int> stripRowWidths(std::span<const StripWidthInput> strips, int available)
{
    const int count = int(strips.size());
    QVector<int> sizes;
    qint64 total = 0;
    int elastic = -1;
    int priority = -1;
    for (int index = 0; index < count; ++index) {
        const auto& strip = strips[index];
        const int value = strip.elastic ? strip.minimumWidth : std::max(strip.minimumWidth, strip.preferredWidth);
        sizes.append(value); total += value;
        if (strip.elastic) elastic = index;
        if (strip.naturalWidthPriority) priority = index;
    }
    if (total <= available) {
        if (elastic >= 0) sizes[elastic] += int(qint64(available) - total);
        return sizes;
    }
    QVector<int> compressionOrder;
    const auto appendRange = [&compressionOrder](int begin, int end, int step) {
        for (int index = begin; index != end; index += step) compressionOrder.append(index);
    };
    const int leftEnd = elastic < 0 ? int(strips.size()) : elastic;
    if (priority >= 0 && (elastic < 0 || priority < elastic)) {
        // Keep the reset strip's head, giving way toward the trailing edge.
        appendRange(leftEnd - 1, priority, -1);
        compressionOrder.append(priority);
        appendRange(priority - 1, -1, -1);
        // Do not make room across an exhausted elastic strip: doing so would
        // restore a compressed prefix and move the head we promised to keep.
        if (elastic >= 0) appendRange(elastic + 1, int(strips.size()), 1);
    } else if (priority > elastic && elastic >= 0) {
        // Keep its tail and the following right-anchored strips unchanged.
        appendRange(elastic + 1, priority, 1);
        appendRange(leftEnd - 1, -1, -1);
        compressionOrder.append(priority);
        appendRange(priority + 1, int(strips.size()), 1);
    } else {
        appendRange(leftEnd - 1, -1, -1);
        if (elastic >= 0) appendRange(elastic + 1, int(strips.size()), 1);
    }
    for (const int index : compressionOrder) {
        if (total <= available) break;
        const int minimum = strips[index].minimumWidth;
        const int reduction = int(std::min<qint64>(total - available, sizes[index] - minimum));
        sizes[index] -= reduction; total -= reduction;
    }
    if (elastic >= 0) sizes[elastic] += int(std::max<qint64>(0, qint64(available) - total));
    return sizes;
}

StripRowLayout layoutStripRow(std::span<const StripWidthInput> strips, int width, int margin, int spacing)
{
    const int count = int(strips.size());
    const int available = std::max(0, width - 2 * margin - std::max(0, count - 1) * spacing);
    StripRowLayout result;
    result.widths = stripRowWidths(strips, available);
    result.positions.resize(count);
    int elastic = -1;
    int x = margin;
    for (int index = 0; index < count; ++index) {
        if (elastic < 0 && strips[index].elastic) elastic = index;
        result.positions[index] = x;
        x += result.widths[index] + spacing;
    }
    int right = width - margin;
    for (int index = count - 1; elastic >= 0 && index > elastic; --index) {
        result.positions[index] = right - result.widths[index];
        right = result.positions[index] - spacing;
    }
    return result;
}

StripAreasLayout layoutStripAreas(std::span<const StripAreaRowInput> rows)
{
    StripAreasLayout result;
    for (const auto& row : rows) if (row.occupied) result.heights[int(row.area)] += row.height;
    result.next[int(ToolbarArea::Bottom)] = result.heights[int(ToolbarArea::Bottom)];
    return result;
}

int StripAreasLayout::takeRow(ToolbarArea area, int height)
{
    if (area == ToolbarArea::Bottom) next[int(area)] -= height;
    const int position = next[int(area)];
    if (area == ToolbarArea::Top) next[int(area)] += height;
    return position;
}

std::optional<StripVacancyAdjustment> stripVacancyAdjustment(
    std::span<const StripSlotInput> strips, int removed, int spacing)
{
    Q_ASSERT(removed >= 0 && removed < int(strips.size()));
    if (!strips[removed].visible) return std::nullopt;
    int previous = -1, next = -1;
    bool rightOfElastic = false;
    for (int before = 0; before < removed; ++before) if (strips[before].visible) {
        previous = before;
        rightOfElastic |= strips[before].elastic;
    }
    for (int after = removed + 1; after < int(strips.size()); ++after)
        if (strips[after].visible) { next = after; break; }
    const int filler = rightOfElastic ? next : previous;
    const int across = rightOfElastic ? previous : next;
    if (filler < 0 || strips[filler].elastic || strips[filler].fixedWidth) return std::nullopt;
    StripVacancyAdjustment result{filler, -1};
    if (across >= 0 && !strips[across].elastic)
        result.requestedWidth = int(std::min<qint64>(std::numeric_limits<int>::max(),
            qint64(strips[filler].width) + strips[removed].width + spacing));
    return result;
}

int stripElasticOwner(std::span<const StripPolicyInput> strips, int preferred)
{
    int winner = -1;
    int required = -1;
    for (int index = 0; index < int(strips.size()); ++index) {
        const auto& strip = strips[index];
        if (strip.elasticAllowed && strip.elastic) winner = index;
        if (required < 0 && strip.elasticRequired) required = index;
    }
    if (preferred >= 0) {
        Q_ASSERT(preferred < int(strips.size()));
        if (strips[preferred].elasticAllowed && strips[preferred].elastic) winner = preferred;
    }
    return required >= 0 ? required : winner;
}

int stripWidthPriorityOwner(std::span<const StripPolicyInput> strips)
{
    int winner = -1;
    for (int index = 0; index < int(strips.size()); ++index)
        if (strips[index].naturalWidthPriority && !strips[index].elastic && !strips[index].hasRequestedWidth)
            winner = index;
    return winner;
}

} // namespace window_system
