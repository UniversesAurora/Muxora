// SPDX-License-Identifier: MIT
#pragma once

namespace window_system {

// Row zero sits at the outer edge. Further bottom rows grow upward, while
// further top rows grow downward toward workspace content.
enum class ToolbarArea { Top, Bottom };

} // namespace window_system
