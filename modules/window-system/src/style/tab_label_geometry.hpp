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

inline constexpr int tabLabelGap = 4;
// Each end of a regular or pinned tab, and its trailing close target.
inline constexpr int tabBarPadding = 7;
inline constexpr int tabCloseSize = 16;
// A press on a tab's center must select or drag it, never close it. A short
// title without an icon would otherwise put the center inside the close
// target: (width - 1) / 2 < width - padding - close needs this width.
inline constexpr int tabBarMinimumWidth = 2 * (tabBarPadding + tabCloseSize);
// Each end of an auto-hide rail tab.
inline constexpr int edgeTabPadding = 8;

[[nodiscard]] inline qint64 tabLabelWidth(TabLabelParts parts)
{
    qint64 width = 0;
    int shown = 0;
    for (const int part : {parts.number, parts.icon, parts.title})
        if (part > 0) { width += part; ++shown; }
    return width + qint64(std::max(0, shown - 1)) * tabLabelGap;
}

[[nodiscard]] inline int clampedExtent(qint64 extent)
{
    return int(std::clamp<qint64>(extent, 0, QWIDGETSIZE_MAX));
}

// The label, one gap and the close target between the two end paddings.
[[nodiscard]] inline int tabBarTabWidth(TabLabelParts parts)
{
    const auto label = tabLabelWidth(parts);
    return clampedExtent(std::max<qint64>(tabBarMinimumWidth,
        2 * tabBarPadding + tabCloseSize + (label > 0 ? label + tabLabelGap : 0)));
}

// The label between the two end paddings; `minimum` keeps an empty label at
// least as long as the rail is thick.
[[nodiscard]] inline int edgeTabExtent(TabLabelParts parts, int minimum)
{
    return clampedExtent(std::max<qint64>(minimum, 2 * edgeTabPadding + tabLabelWidth(parts)));
}

// The label area of a regular or pinned tab, excluding its paddings, the gap
// and the close target on the trailing side.
[[nodiscard]] inline QRect tabBarLabelArea(const QRect& tab, Qt::LayoutDirection direction)
{
    const QRect logical(tab.left() + tabBarPadding, tab.top(),
                        std::max(0, tab.width() - 2 * tabBarPadding - tabCloseSize - tabLabelGap), tab.height());
    return QStyle::visualRect(direction, tab, logical);
}

[[nodiscard]] inline QRect tabBarCloseRect(const QRect& tab, Qt::LayoutDirection direction)
{
    const QRect logical(tab.right() - tabBarPadding - tabCloseSize + 1,
                        tab.top() + (tab.height() - tabCloseSize) / 2, tabCloseSize, tabCloseSize);
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
                                                  Qt::LayoutDirection direction)
{
    TabLabelRects result;
    qint64 x = area.left();
    const qint64 end = qint64(area.left()) + area.width();
    const auto take = [&](int width) {
        if (width <= 0) return QRect{};
        const auto available = std::max<qint64>(0, end - x);
        const QRect logical(int(x), area.top(), int(std::min<qint64>(width, available)), area.height());
        x += qint64(width) + tabLabelGap;
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
