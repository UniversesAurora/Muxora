// SPDX-License-Identifier: MIT
#pragma once
#include <QtGlobal>
#include <limits>

namespace window_system {
namespace detail { inline double interfaceScaleValue = 1; }
[[nodiscard]] inline double interfaceScale() { return detail::interfaceScaleValue; }
[[nodiscard]] inline int uiPixels(int value)
{
    const double scaled = value * interfaceScale();
    if (scaled >= std::numeric_limits<int>::max()) return std::numeric_limits<int>::max();
    if (scaled <= std::numeric_limits<int>::min()) return std::numeric_limits<int>::min();
    return qRound(scaled);
}
[[nodiscard]] inline double uiPixelsF(double value) { return value * interfaceScale(); }
} // namespace window_system
