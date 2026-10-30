// SPDX-License-Identifier: MIT
#pragma once

#include <QtGlobal>

class QWidget;
class QWindow;

namespace window_system {

// The custom frame deliberately has no WS_SYSMENU. Its separately owned native
// popup keeps the operating system's command labels and WM_SYSCOMMAND behavior.
// The returned handle is borrowed and valid until the owning window is destroyed.
[[nodiscard]] quintptr nativeSystemMenuHandle(QWidget* window);
void showNativeSystemMenu(QWidget* window, quintptr nativeWindow);
[[nodiscard]] quintptr nativeSystemMenuHandle(QWindow* window);
void showNativeSystemMenu(QWindow* window, quintptr nativeWindow);

} // namespace window_system
