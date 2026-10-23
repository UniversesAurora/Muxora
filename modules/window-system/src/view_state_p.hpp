// SPDX-License-Identifier: MIT
#pragma once

#include <window_system/view_state.hpp>

namespace window_system {

[[nodiscard]] bool validViewMode(ViewMode mode);
[[nodiscard]] bool validZenFullscreenMode(ViewMode mode);

} // namespace window_system
