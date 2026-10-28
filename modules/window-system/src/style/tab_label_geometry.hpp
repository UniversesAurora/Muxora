// SPDX-License-Identifier: MIT
#pragma once

#include <QRect>
#include <window_system/ui_scale.hpp>
#include <algorithm>

namespace window_system {

enum class TabShape {
    // A horizontal tab whose outer corners are rounded; `Top` means the strip
    // is above the content.
    Top,
    Bottom,
    // A row or icon tile of a vertical strip.
    Row,
    Tile,
};

// The positions of a tab's slots. A slot that is not shown is null. Later
// features fill these slots (pane activity in the icon or action slot,
// inline rename over the title) without changing the layout.
struct TabParts {
    QRect tab;
    QRect number;
    QRect icon;
    QRect title;
    QRect action;
};


// One tab label laid out along its text direction: [number][icon][title].
// Only the parts that are shown take space and adjacent parts are separated
// by a single gap, so a title clipped to zero leaves no gap behind the icon.
// Tab bars, auto-hide rails and their drag previews size and paint from these
// values, which keeps a label's measured extent and its drawing identical.
struct TabLabelParts {
    int number = 0; // Advance of the ordinal text; 0 when hidden.
    int icon = 0;   // Icon side; 0 when hidden.
    int title = 0;  // Displayed title width after any clipping.
};

// The gap between the parts of an auto-hide rail label.
inline constexpr int tabLabelGap = 4;
// Regular and pinned tabs (tab strip design 2.1): the space at each end, the
// gap between parts, and the action target with the gap before it and the
// space after it. Only a tab whose action always shows (a sticky tab's)
// reserves room for it; a close button covers the title's end on hover.
inline constexpr int tabBarLeading = 10;
inline constexpr int tabPartGap = 6;
inline constexpr int tabActionGap = 4;
inline constexpr int tabActionSize = 20;
inline constexpr int tabBarTrailing = 5;
// A press on a tab's center must select or drag it, never hit its action:
// (width - 1) / 2 < width - trailing - action needs this width.
inline constexpr int tabBarMinimumWidth = 2 * (tabBarTrailing + tabActionSize);
// Each end of an auto-hide rail tab.
inline constexpr int edgeTabPadding = 8;
// Shared layout bound; an adapter still clips to its actual viewport.
inline constexpr int tabLabelMaximumExtent = 0x00ffffff;

[[nodiscard]] inline QRect visualTabLabelRect(Qt::LayoutDirection direction, const QRect& bounds, QRect logical)
{
    if (direction != Qt::LeftToRight) logical.moveLeft(bounds.left() + bounds.right() - logical.right());
    return logical;
}

[[nodiscard]] inline qint64 tabLabelWidth(TabLabelParts parts, int gap = uiPixels(tabLabelGap))
{
    qint64 width = 0;
    int shown = 0;
    for (const int part : {parts.number, parts.icon, parts.title})
        if (part > 0) { width += part; ++shown; }
    return width + qint64(std::max(0, shown - 1)) * gap;
}

[[nodiscard]] inline int clampedExtent(qint64 extent)
{
    return int(std::clamp<qint64>(extent, 0, tabLabelMaximumExtent));
}

// The label between the end spaces, and the reserved action if any.
[[nodiscard]] inline int tabBarTabWidth(TabLabelParts parts, bool reserveAction = false)
{
    const auto label = tabLabelWidth(parts, uiPixels(tabPartGap));
    const qint64 end = reserveAction ? uiPixels(tabActionGap) + uiPixels(tabActionSize) + uiPixels(tabBarTrailing) : uiPixels(tabBarLeading);
    return clampedExtent(std::max<qint64>(uiPixels(tabBarMinimumWidth), uiPixels(tabBarLeading) + label + end));
}

// The label between the two end paddings; `minimum` keeps an empty label at
// least as long as the rail is thick.
[[nodiscard]] inline int edgeTabExtent(TabLabelParts parts, int minimum)
{
    return clampedExtent(std::max<qint64>(minimum, 2 * uiPixels(edgeTabPadding) + tabLabelWidth(parts)));
}

// The label area of a regular or pinned tab: up to its reserved action, or
// up to the end space when the action only covers the title on hover.
[[nodiscard]] inline QRect tabBarLabelArea(const QRect& tab, Qt::LayoutDirection direction, bool reserveAction = false)
{
    const int end = reserveAction ? uiPixels(tabActionGap) + uiPixels(tabActionSize) + uiPixels(tabBarTrailing) : uiPixels(tabBarLeading);
    const QRect logical(tab.left() + uiPixels(tabBarLeading), tab.top(), std::max(0, tab.width() - uiPixels(tabBarLeading) - end), tab.height());
    return visualTabLabelRect(direction, tab, logical);
}

[[nodiscard]] inline QRect tabBarCloseRect(const QRect& tab, Qt::LayoutDirection direction)
{
    const QRect logical(tab.right() - uiPixels(tabBarTrailing) - uiPixels(tabActionSize) + 1,
                        tab.top() + (tab.height() - uiPixels(tabActionSize)) / 2, uiPixels(tabActionSize), uiPixels(tabActionSize));
    return visualTabLabelRect(direction, tab, logical);
}

struct TabLabelRects {
    QRect number;
    QRect icon;
    QRect title;
};

// Places the shown parts from the leading edge of `area`. A part that does not
// fit is cut at the area's trailing edge; the title takes what remains.
[[nodiscard]] inline TabLabelRects layoutTabLabel(const QRect& area, TabLabelParts parts,
                                                  Qt::LayoutDirection direction, int gap = uiPixels(tabLabelGap))
{
    TabLabelRects result;
    qint64 x = area.left();
    const qint64 end = qint64(area.left()) + area.width();
    const auto take = [&](int width) {
        if (width <= 0) return QRect{};
        const auto available = std::max<qint64>(0, end - x);
        const QRect logical(int(x), area.top(), int(std::min<qint64>(width, available)), area.height());
        x += qint64(width) + gap;
        return visualTabLabelRect(direction, area, logical);
    };
    result.number = take(parts.number);
    const auto icon = take(parts.icon);
    if (!icon.isNull())
        result.icon = QRect(icon.left(), area.top() + (area.height() - parts.icon) / 2, icon.width(), parts.icon);
    result.title = take(parts.title);
    return result;
}

[[nodiscard]] inline TabParts layoutTabParts(const QRect& tab, TabShape shape, TabLabelParts label, bool iconOnly, bool action,
                           bool reserveAction, Qt::LayoutDirection direction)
{
    TabParts result;
    result.tab = tab;
    if (iconOnly || shape == TabShape::Tile) {
        const QRect icon(QPoint(), QSize(label.icon, label.icon));
        result.icon = label.icon ? icon.translated(tab.center() - icon.center()) : QRect{};
        result.title = label.icon ? QRect{} : tab;
        return result;
    }
    // Slots run along the text direction and sit centered in the tab.
    const QRect content = tab;
    if (action) result.action = tabBarCloseRect(content, direction);
    const auto area = tabBarLabelArea(content, direction, action && reserveAction);
    auto rects = layoutTabLabel(area, label, direction, uiPixels(tabPartGap));
    result.number = rects.number; result.icon = rects.icon; result.title = rects.title;
    // A title slot keeps the label area's full extent when it is the last
    // part, so the fade lands at the label's real end.
    return result;
}

struct TabColumnLabelMetrics {
    int width;
    int railWidth;
    int iconSize;
    int rowHeight;
};

// The outer icon rail stays fixed as the title column reveals; right columns
// mirror the rail and action slots independently of the UI text direction.
[[nodiscard]] inline TabParts layoutColumnTabParts(const QRect& rect, TabLabelParts label, bool iconOnly,
    bool action, bool sticky, bool right, const TabColumnLabelMetrics& metrics)
{
    TabParts parts; parts.tab = rect;
    const auto direction = right ? Qt::RightToLeft : Qt::LeftToRight;
    const int center = right ? metrics.width - metrics.railWidth / 2 : metrics.railWidth / 2;
    if (label.icon) parts.icon = QRect(center - label.icon / 2, rect.center().y() - label.icon / 2, label.icon, label.icon);
    if (iconOnly) {
        if (!label.icon) parts.title = QRect(center - metrics.iconSize / 2, rect.top(), metrics.iconSize, rect.height());
        return parts;
    }
    if (action) parts.action = tabBarCloseRect(rect, direction);
    const int gap = uiPixels(tabPartGap);
    int begin = right ? rect.left() + uiPixels(tabBarTrailing) : center + metrics.iconSize / 2 + gap;
    int end = right ? center - metrics.iconSize / 2 - gap : rect.right() + 1 - uiPixels(tabBarTrailing);
    if (action && sticky) {
        if (right) begin = parts.action.right() + 1 + uiPixels(tabActionGap);
        else end = parts.action.left() - uiPixels(tabActionGap);
    }
    const int readableTitle = std::min(label.title, metrics.rowHeight - 10);
    if (label.number && end - begin >= label.number + gap + readableTitle) {
        parts.number = QRect(right ? end - label.number : begin, rect.top(), label.number, rect.height());
        if (right) end -= label.number + gap; else begin += label.number + gap;
    }
    parts.title = QRect(begin, rect.top(), std::max(0, end - begin), rect.height());
    return parts;
}

} // namespace window_system
