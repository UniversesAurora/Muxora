// SPDX-License-Identifier: MIT
#pragma once

#include <QtGlobal>

class QWidget;

namespace window_system {

// Internal native border integration. Calling these functions on other
// platforms is harmless; the Qt widget continues to use its normal behavior.
void applyNativeFrame(QWidget* window);
bool handleNativeFrame(QWidget* window, void* message, qintptr* result);

} // namespace window_system
