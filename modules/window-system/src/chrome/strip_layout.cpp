// SPDX-License-Identifier: MIT
#include "strip_layout.hpp"
#include <algorithm>

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

} // namespace window_system
