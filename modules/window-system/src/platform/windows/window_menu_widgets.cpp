// SPDX-License-Identifier: MIT
#include "window_menu.hpp"

#include <QWidget>
#include <QWindow>

namespace window_system {

quintptr nativeSystemMenuHandle(QWidget* window)
{
    return window && window->isWindow() && window->effectiveWinId()
        ? nativeSystemMenuHandle(window->windowHandle()) : 0;
}

void showNativeSystemMenu(QWidget* window, quintptr nativeWindow)
{
    if (window) showNativeSystemMenu(window->windowHandle(), nativeWindow);
}

} // namespace window_system
