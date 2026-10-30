// SPDX-License-Identifier: MIT
#pragma once

#include "style/tab_label_geometry.hpp"
#include <window_system/tab_appearance.hpp>
#include <window_system/ui_scale.hpp>

#include <QFontMetrics>
#include <algorithm>

namespace window_system {

inline constexpr int edgeTabIconSize = 16;
// The rail label: ordinal, icon and title along the rail's text direction.
inline TabLabelParts edgeTabParts(const QFontMetrics& metrics, int natural, bool icon, int number,
                                  bool clipTitles = false, int maximumTitleWidth = defaultTabTitleWidth)
{
    return {number > 0 ? metrics.horizontalAdvance(QString::number(number)) : 0, icon ? uiPixels(edgeTabIconSize) : 0,
            clipTitles ? std::min(natural, uiPixels(maximumTitleWidth)) : natural};
}
inline int edgeTabExtent(const QFontMetrics& metrics, int natural, bool icon, int number, int thickness,
                         bool clipTitles = false, int maximumTitleWidth = defaultTabTitleWidth)
{
    return edgeTabExtent(edgeTabParts(metrics, natural, icon, number, clipTitles, maximumTitleWidth), thickness);
}

} // namespace window_system
