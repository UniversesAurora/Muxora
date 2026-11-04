// SPDX-License-Identifier: MIT
#pragma once

class QWindow;

namespace window_system {
// Apply to an existing platform window only. Client rendering owns the single
// square border; the native popup must not add a second rim.
void preparePopupFrame(QWindow* popup);
} // namespace window_system
