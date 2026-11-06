// SPDX-License-Identifier: MIT
#pragma once

namespace window_system {

// Whether the system asks for access key underlines at all times. Windows
// otherwise shows them only while the keyboard drives menus (Alt held, or a
// menu opened from the keyboard); elsewhere they always show.
[[nodiscard]] bool keyboardCuesAlwaysShown();

} // namespace window_system
