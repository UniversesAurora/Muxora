// SPDX-License-Identifier: MIT
#pragma once
#include <QFontMetrics>
#include <QWidget>
#include <window_system/ui_scale.hpp>
#include <algorithm>

namespace window_system {
// Scoped to temporary auto-hide panels; ordinary group headers are independent.
inline constexpr int peekHeaderHeight = 20;
inline constexpr int peekButtonExtent = 18;
inline void updatePeekHeaderHeight(QWidget* header)
{
    header->setFixedHeight(std::max(uiPixels(peekHeaderHeight), header->fontMetrics().height() + uiPixels(2)));
}
} // namespace window_system
