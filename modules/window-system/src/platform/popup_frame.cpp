// SPDX-License-Identifier: MIT
#include "popup_frame.hpp"
#include <QWindow>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <dwmapi.h>
#endif

namespace window_system {

void preparePopupFrame(QWindow* popup)
{
    if (!popup || !popup->handle()) return;
#ifdef Q_OS_WIN
    const auto handle = reinterpret_cast<HWND>(popup->winId());
    const auto style = GetWindowLongPtrW(handle, GWL_STYLE);
    const auto squareStyle = style & ~(WS_BORDER | WS_DLGFRAME | WS_THICKFRAME);
    const auto extended = GetWindowLongPtrW(handle, GWL_EXSTYLE);
    const auto squareExtended = extended & ~(WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE);
    if (style != squareStyle || extended != squareExtended) {
        SetWindowLongPtrW(handle, GWL_STYLE, squareStyle);
        SetWindowLongPtrW(handle, GWL_EXSTYLE, squareExtended);
        SetWindowPos(handle, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    constexpr DWORD cornerPreferenceAttribute = 33;
    const DWORD squareCorners = 1; // DWMWCP_DONOTROUND
    DwmSetWindowAttribute(handle, cornerPreferenceAttribute, &squareCorners, sizeof(squareCorners));
    constexpr DWORD borderColorAttribute = 34;
    const COLORREF noBorder = 0xfffffffe; // DWMWA_COLOR_NONE
    DwmSetWindowAttribute(handle, borderColorAttribute, &noBorder, sizeof(noBorder));
#endif
}

} // namespace window_system
