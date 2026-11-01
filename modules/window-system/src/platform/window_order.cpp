// SPDX-License-Identifier: MIT
#include "window_order.hpp"

#include <QGuiApplication>
#include <QWindow>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <dwmapi.h>
#endif

namespace window_system {

QWindow* stackedWindowAt(QPoint global, const std::function<bool(const QWindow*)>& accept)
{
    const auto windows = QGuiApplication::topLevelWindows();
    const auto contains = [global](const QWindow* window) {
        return QRect(QPoint(), window->size()).contains(window->mapFromGlobal(global));
    };
#ifdef Q_OS_WIN
    // Resolve the point in native coordinates through a window of ours, so
    // per-monitor scaling is applied as Qt applies it to that window.
    const QWindow* reference = nullptr;
    for (const auto* window : windows) if (!reference && window->handle() && window->isVisible()) reference = window;
    if (!reference) return nullptr;
    const auto local = reference->mapFromGlobal(QPointF(global)) * reference->devicePixelRatio();
    POINT point{qRound(local.x()), qRound(local.y())};
    if (!ClientToScreen(reinterpret_cast<HWND>(reference->winId()), &point)) return nullptr;
    for (HWND handle = GetTopWindow(nullptr); handle; handle = GetWindow(handle, GW_HWNDNEXT)) {
        if (!IsWindowVisible(handle) || IsIconic(handle)) continue;
        const auto extendedStyle = GetWindowLongPtr(handle, GWL_EXSTYLE);
        if ((extendedStyle & WS_EX_LAYERED) && (extendedStyle & WS_EX_TRANSPARENT)) continue;
        DWORD cloaked = 0;
        if (SUCCEEDED(DwmGetWindowAttribute(handle, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) continue;
        RECT rect{};
        if (!GetWindowRect(handle, &rect) || !PtInRect(&rect, point)) continue;
        DWORD process = 0;
        GetWindowThreadProcessId(handle, &process);
        if (process != GetCurrentProcessId()) return nullptr;
        for (auto* window : windows)
            if (window->handle() && reinterpret_cast<HWND>(window->winId()) == handle && accept(window) && contains(window))
                return window;
    }
    return nullptr;
#else
    for (auto it = windows.crbegin(); it != windows.crend(); ++it)
        if ((*it)->isVisible() && (*it)->visibility() != QWindow::Minimized && accept(*it) && contains(*it)) return *it;
    return nullptr;
#endif
}

} // namespace window_system
