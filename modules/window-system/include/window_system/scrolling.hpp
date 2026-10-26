// SPDX-License-Identifier: MIT
#pragma once

class QWidget;

namespace window_system {

/// Disable interpolation for a widget and its descendants, for example a
/// terminal that implements its own scrolling. Input still reaches the widget.
/// This is a runtime presentation policy, not a saved layout preference.
void setSmoothScrollingEnabled(QWidget* widget, bool enabled);
[[nodiscard]] bool isSmoothScrollingEnabled(const QWidget* widget);

} // namespace window_system
