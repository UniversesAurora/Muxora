// SPDX-License-Identifier: MIT
#pragma once

#include <QRect>
#include <QStyle>
#include <QWidget>
#include <algorithm>

namespace window_system {

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

[[nodiscard]] inline qint64 tabLabelWidth(TabLabelParts parts, int gap = tabLabelGap)
{
    qint64 width = 0;
    int shown = 0;
    for (const int part : {parts.number, parts.icon, parts.title})
        if (part > 0) { width += part; ++shown; }
    return width + qint64(std::max(0, shown - 1)) * gap;
}

[[nodiscard]] inline int clampedExtent(qint64 extent)
{
    return int(std::clamp<qint64>(extent, 0, QWIDGETSIZE_MAX));
}

// The label between the end spaces, and the reserved action if any.
[[nodiscard]] inline int tabBarTabWidth(TabLabelParts parts, bool reserveAction = false)
{
    const auto label = tabLabelWidth(parts, tabPartGap);
    const qint64 end = reserveAction ? tabActionGap + tabActionSize + tabBarTrailing : tabBarLeading;
    return clampedExtent(std::max<qint64>(tabBarMinimumWidth, tabBarLeading + label + end));
}

// The label between the two end paddings; `minimum` keeps an empty label at
// least as long as the rail is thick.
[[nodiscard]] inline int edgeTabExtent(TabLabelParts parts, int minimum)
{
    return clampedExtent(std::max<qint64>(minimum, 2 * edgeTabPadding + tabLabelWidth(parts)));
}

// The label area of a regular or pinned tab: up to its reserved action, or
// up to the end space when the action only covers the title on hover.
[[nodiscard]] inline QRect tabBarLabelArea(const QRect& tab, Qt::LayoutDirection direction, bool reserveAction = false)
{
    const int end = reserveAction ? tabActionGap + tabActionSize + tabBarTrailing : tabBarLeading;
    const QRect logical(tab.left() + tabBarLeading, tab.top(), std::max(0, tab.width() - tabBarLeading - end), tab.height());
    return QStyle::visualRect(direction, tab, logical);
}

[[nodiscard]] inline QRect tabBarCloseRect(const QRect& tab, Qt::LayoutDirection direction)
{
    const QRect logical(tab.right() - tabBarTrailing - tabActionSize + 1,
                        tab.top() + (tab.height() - tabActionSize) / 2, tabActionSize, tabActionSize);
    return QStyle::visualRect(direction, tab, logical);
}

struct TabLabelRects {
    QRect number;
    QRect icon;
    QRect title;
};

// Places the shown parts from the leading edge of `area`. A part that does not
// fit is cut at the area's trailing edge; the title takes what remains.
[[nodiscard]] inline TabLabelRects layoutTabLabel(const QRect& area, TabLabelParts parts,
                                                  Qt::LayoutDirection direction, int gap = tabLabelGap)
{
    TabLabelRects result;
    qint64 x = area.left();
    const qint64 end = qint64(area.left()) + area.width();
    const auto take = [&](int width) {
        if (width <= 0) return QRect{};
        const auto available = std::max<qint64>(0, end - x);
        const QRect logical(int(x), area.top(), int(std::min<qint64>(width, available)), area.height());
        x += qint64(width) + gap;
        return QStyle::visualRect(direction, area, logical);
    };
    result.number = take(parts.number);
    const auto icon = take(parts.icon);
    if (!icon.isNull())
        result.icon = QRect(icon.left(), area.top() + (area.height() - parts.icon) / 2, icon.width(), parts.icon);
    result.title = take(parts.title);
    return result;
}

} // namespace window_system
