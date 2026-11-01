// SPDX-License-Identifier: MIT
#pragma once

#include <QPoint>
#include <functional>

class QWindow;

namespace window_system {

// This process's top-level window under a global point in the system's
// stacking order, passing over windows `accept` refuses (a window being
// dragged, popups) and windows that let input through. Null over another
// process's window or the desktop.
[[nodiscard]] QWindow* stackedWindowAt(QPoint global, const std::function<bool(const QWindow*)>& accept);

} // namespace window_system
