// SPDX-License-Identifier: MIT
#include "window_frame.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QEnterEvent>
#include <QEvent>
#include <QPointer>
#include <QVariant>
#include <QWidget>
#include <QWindow>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <dwmapi.h>
#include <windowsx.h>
#endif

namespace window_system {
#ifdef Q_OS_WIN
namespace {

constexpr auto frameStateName = "_windowSystemNativeFrameState";

bool fullScreenRequested(QWidget* window)
{
    // QWindow is updated before QWidget receives WindowStateChange. Consult
    // both while Qt is applying a full-screen transition to the native style.
    return window->isFullScreen() || (window->windowHandle()
        && window->windowHandle()->windowStates().testFlag(Qt::WindowFullScreen));
}

DWORD normalFrameStyle(DWORD style)
{
    // Removing the painted caption is the responsibility of WM_NCCALCSIZE.
    // Its native style must remain: DWM recognizes a normal overlapped window
    // for show/hide and minimize/maximize transitions, including custom frames.
    return (style | DWORD(WS_OVERLAPPEDWINDOW)) & ~DWORD(WS_POPUP);
}

class NativeFrameState final : public QObject {
public:
    explicit NativeFrameState(QWidget* window) : QObject(window), window_(window)
    {
        setObjectName(QString::fromLatin1(frameStateName));
        window->installEventFilter(this);
    }

    void setHovered(QAbstractButton* button, const QPoint& clientPosition = {})
    {
        if (hovered == button) return;
        if (hovered) {
            hovered->setAttribute(Qt::WA_UnderMouse, false);
            QEvent leave(QEvent::Leave);
            QApplication::sendEvent(hovered, &leave);
            if (hovered) hovered->update();
        }
        hovered = button;
        if (hovered && window_) {
            const QPoint local = hovered->mapFrom(window_, clientPosition);
            const QPoint global = window_->mapToGlobal(clientPosition);
            hovered->setAttribute(Qt::WA_UnderMouse, true);
            QEnterEvent enter{QPointF(local), QPointF(clientPosition), QPointF(global)};
            QApplication::sendEvent(hovered, &enter);
            if (hovered) hovered->update();
        }
    }

    void cancelPress()
    {
        if (pressed) pressed->setDown(false);
        pressed.clear();
    }

    QPointer<QAbstractButton> hovered;
    QPointer<QAbstractButton> pressed;
    bool trackingNonClient = false;
    HWND configuredHandle = nullptr;
    NativeTransitionConfiguration transitions;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == window_ && event->type() == QEvent::WinIdChange
            && window_ && window_->testAttribute(Qt::WA_WState_Created)
            && window_->effectiveWinId()) {
            // Window flag changes may replace the HWND. Do not wait for a
            // later showEvent to make the replacement eligible for transitions.
            applyNativeFrame(window_);
        }
        if (event->type() == QEvent::WindowDeactivate || event->type() == QEvent::WindowStateChange) {
            cancelPress();
            setHovered(nullptr);
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QPointer<QWidget> window_;
};

NativeFrameState* findFrameState(QWidget* window)
{
    for (auto* child : window->children()) {
        if (child->objectName() == QString::fromLatin1(frameStateName)) {
            if (auto* state = dynamic_cast<NativeFrameState*>(child)) return state;
        }
    }
    return nullptr;
}

NativeFrameState* frameState(QWidget* window)
{
    if (auto* state = findFrameState(window)) return state;
    return new NativeFrameState(window);
}

UINT windowDpi(HWND handle, QWidget* window)
{
    // Resolve the per-window APIs so this source does not impose a newer SDK
    // import requirement. The fallback still uses this window's Qt screen.
    using DpiFunction = UINT(WINAPI*)(HWND);
    static const auto dpiForWindow = std::bit_cast<DpiFunction>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (dpiForWindow) {
        const UINT dpi = dpiForWindow(handle);
        if (dpi) return dpi;
    }
    return UINT(std::max(96, qRound(window->devicePixelRatioF() * 96.0)));
}

int resizeBorder(HWND handle, QWidget* window)
{
    using MetricFunction = int(WINAPI*)(int, UINT);
    static const auto metricsForDpi = std::bit_cast<MetricFunction>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetSystemMetricsForDpi"));
    const UINT dpi = windowDpi(handle, window);
    if (metricsForDpi) {
        return std::max(1, metricsForDpi(SM_CXSIZEFRAME, dpi) + metricsForDpi(SM_CXPADDEDBORDER, dpi));
    }
    return std::max(1, qRound(6.0 * window->devicePixelRatioF()));
}

POINT screenPosition(LPARAM position)
{
    // LOWORD/HIWORD lose the sign on monitors left/above the primary monitor.
    return {GET_X_LPARAM(position), GET_Y_LPARAM(position)};
}

QPoint logicalClientPosition(QWidget* window, HWND handle, POINT point, bool fromScreen)
{
    if (fromScreen) ScreenToClient(handle, &point);
    const qreal ratio = std::max<qreal>(1.0, window->devicePixelRatioF());
    return {qFloor(point.x / ratio), qFloor(point.y / ratio)};
}

QAbstractButton* maximizeButtonAt(QWidget* window, const QPoint& position)
{
    for (auto* child = window->childAt(position); child && child != window; child = child->parentWidget()) {
        const auto role = child->property("role").toString();
        if (child->objectName() == QStringLiteral("CaptionButton") &&
            (role == QStringLiteral("maximize") || role == QStringLiteral("restore"))) {
            auto* button = qobject_cast<QAbstractButton*>(child);
            return button && button->isVisible() && button->isEnabled() ? button : nullptr;
        }
    }
    return nullptr;
}

void extendFrame(QWidget* window, HWND handle)
{
    const DWMNCRENDERINGPOLICY policy = DWMNCRP_ENABLED;
    DwmSetWindowAttribute(handle, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy));
    // Permit the compositor's ordinary transitions. This is not an animation
    // request and does not override Windows' global animation/accessibility
    // preferences. We never change SPI_SETANIMATION or client-area settings.
    const BOOL forceDisableTransitions = FALSE;
    const HRESULT transitionResult = DwmSetWindowAttribute(handle, DWMWA_TRANSITIONS_FORCEDISABLED,
        &forceDisableTransitions, sizeof(forceDisableTransitions));
    auto* state = frameState(window);
    state->configuredHandle = handle;
    state->transitions = {true, qint32(transitionResult), forceDisableTransitions != FALSE};
    const MARGINS margins{1, 1, 1, 1};
    DwmExtendFrameIntoClientArea(handle, &margins);
    // DWMWA_WINDOW_CORNER_PREFERENCE / DWMWCP_ROUND. Older Windows returns
    // E_INVALIDARG; its normal square corners and resize behavior remain valid.
    constexpr DWORD cornerPreferenceAttribute = 33;
    const DWORD cornerPreference = 2;
    DwmSetWindowAttribute(handle, cornerPreferenceAttribute, &cornerPreference, sizeof(cornerPreference));
}

void showSystemMenu(QWidget* window, HWND handle)
{
    const HMENU menu = GetSystemMenu(handle, FALSE);
    if (!menu) return;
    const bool maximized = IsZoomed(handle);
    const bool minimized = IsIconic(handle);
    const LONG_PTR style = GetWindowLongPtrW(handle, GWL_STYLE);
    const auto enable = [menu](UINT command, bool allowed) {
        EnableMenuItem(menu, command, MF_BYCOMMAND | (allowed ? MF_ENABLED : MF_GRAYED));
    };
    enable(SC_RESTORE, maximized || minimized);
    enable(SC_MOVE, !maximized && !minimized);
    enable(SC_SIZE, !maximized && !minimized && (style & WS_THICKFRAME));
    enable(SC_MINIMIZE, !minimized && (style & WS_MINIMIZEBOX));
    enable(SC_MAXIMIZE, !maximized && (style & WS_MAXIMIZEBOX));
    RECT bounds{};
    if (!GetWindowRect(handle, &bounds)) return;
    const int inset = std::max(1, qRound(8 * window->devicePixelRatioF()));
    const int titleHeight = std::max(1, qRound(32 * window->devicePixelRatioF()));
    const UINT command = TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_RIGHTBUTTON,
                                         bounds.left + inset, bounds.top + titleHeight, handle, nullptr);
    if (command && IsWindow(handle)) PostMessageW(handle, WM_SYSCOMMAND, command, 0);
}

bool finishMaximizeClick(QWidget* window, MSG* message, NativeFrameState* state, qintptr* result, bool fromScreen)
{
    if (!state->pressed) return false;
    const QPointer<QAbstractButton> button = state->pressed;
    const POINT nativePoint = screenPosition(message->lParam);
    const QPoint position = logicalClientPosition(window, message->hwnd, nativePoint, fromScreen);
    const bool activate = maximizeButtonAt(window, position) == button;
    state->cancelPress();
    if (GetCapture() == message->hwnd) ReleaseCapture();
    *result = 0;
    // Call the real accessible Qt button once, rather than bypassing its slot
    // with a second native SC_MAXIMIZE/SC_RESTORE transition.
    if (activate && button && button->isEnabled()) button->click();
    return true;
}

} // namespace
#endif

void applyNativeFrame(QWidget* window)
{
#ifdef Q_OS_WIN
    if (!window || !window->isWindow() || !window->windowFlags().testFlag(Qt::FramelessWindowHint)) return;
    frameState(window); // Observe HWND creation/replacement before requesting it.
    const auto handle = reinterpret_cast<HWND>(window->winId());
    if (!IsWindow(handle)) return;
    if (!fullScreenRequested(window)) {
        const LONG_PTR original = GetWindowLongPtrW(handle, GWL_STYLE);
        const LONG_PTR desired = normalFrameStyle(DWORD(original));
        if (desired != original) {
            SetWindowLongPtrW(handle, GWL_STYLE, desired);
            SetWindowPos(handle, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
    }
    extendFrame(window, handle);
#else
    Q_UNUSED(window)
#endif
}

NativeTransitionConfiguration nativeTransitionConfiguration(QWidget* window)
{
#ifdef Q_OS_WIN
    if (window) if (const auto* state = findFrameState(window))
        if (state->configuredHandle == reinterpret_cast<HWND>(window->effectiveWinId())) return state->transitions;
#else
    Q_UNUSED(window)
#endif
    return {};
}

bool hasNormalNativePlacement(const QWidget* window)
{
#ifdef Q_OS_WIN
    const auto handle = window ? reinterpret_cast<HWND>(window->effectiveWinId()) : nullptr;
    return !handle || (!IsZoomed(handle) && !IsIconic(handle));
#else
    Q_UNUSED(window)
    return true;
#endif
}

bool handleNativeFrame(QWidget* window, void* message, qintptr* result)
{
#ifdef Q_OS_WIN
    if (!window || !message || !result || !window->isWindow()
        || !window->windowFlags().testFlag(Qt::FramelessWindowHint)) return false;
    auto* msg = static_cast<MSG*>(message);
    switch (msg->message) {
    case WM_STYLECHANGING:
        if (msg->wParam == WPARAM(GWL_STYLE) && msg->lParam && !fullScreenRequested(window)) {
            auto* styles = reinterpret_cast<STYLESTRUCT*>(msg->lParam);
            if (!(styles->styleNew & WS_CHILD)) styles->styleNew = normalFrameStyle(styles->styleNew);
        }
        // Keep Qt's window flags, state bookkeeping and style-change dispatch.
        return false;
    case WM_GETMINMAXINFO:
        if (msg->lParam && !fullScreenRequested(window)) {
            // Initialize the Win32 defaults, then apply the host's public Qt
            // limits without adding a normal painted caption's frame margins.
            // This custom client owns the full normal window rectangle.
            DefWindowProcW(msg->hwnd, msg->message, msg->wParam, msg->lParam);
            MONITORINFO monitor{};
            monitor.cbSize = sizeof(MONITORINFO);
            if (GetMonitorInfoW(MonitorFromWindow(msg->hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) {
                auto* sizes = reinterpret_cast<MINMAXINFO*>(msg->lParam);
                // Supply work-area limits and public Qt track hints. Windows
                // may add its normal invisible maximized borders; NCCALCSIZE
                // constrains the complete custom client to this visible area.
                sizes->ptMaxPosition = {monitor.rcWork.left - monitor.rcMonitor.left,
                                        monitor.rcWork.top - monitor.rcMonitor.top};
                const qreal ratio = window->devicePixelRatioF();
                const auto nativeSize = [ratio](int logical, bool minimum) {
                    const double scaled = logical * ratio;
                    const double rounded = minimum ? std::ceil(scaled) : std::floor(scaled);
                    return LONG(std::clamp(rounded, 0.0, double(std::numeric_limits<LONG>::max())));
                };
                auto minimum = window->minimumSize();
                auto maximum = window->maximumSize();
                if (auto* qtWindow = window->windowHandle()) {
                    minimum = minimum.expandedTo(qtWindow->minimumSize());
                    maximum = maximum.boundedTo(qtWindow->maximumSize());
                }
                sizes->ptMinTrackSize = {nativeSize(minimum.width(), true), nativeSize(minimum.height(), true)};
                sizes->ptMaxTrackSize = {
                    std::max(sizes->ptMinTrackSize.x, nativeSize(maximum.width(), false)),
                    std::max(sizes->ptMinTrackSize.y, nativeSize(maximum.height(), false))};
                sizes->ptMaxSize = {
                    std::min(monitor.rcWork.right - monitor.rcWork.left, sizes->ptMaxTrackSize.x),
                    std::min(monitor.rcWork.bottom - monitor.rcWork.top, sizes->ptMaxTrackSize.y)};
                *result = 0;
                return true;
            }
        }
        // Unavailable monitors and full-screen transitions keep Qt's handler.
        // Normal cases use Qt's public size limits, scaled for this HWND's
        // associated Qt window; WM_DPICHANGED remains entirely with Qt.
        return false;
    case WM_NCCALCSIZE: {
        if (!msg->lParam) return false;
        if (IsZoomed(msg->hwnd) && !IsIconic(msg->hwnd) && !fullScreenRequested(window)) {
            auto* bounds = msg->wParam ? &reinterpret_cast<NCCALCSIZE_PARAMS*>(msg->lParam)->rgrc[0]
                                      : reinterpret_cast<RECT*>(msg->lParam);
            MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
            if (GetMonitorInfoW(MonitorFromRect(bounds, MONITOR_DEFAULTTONEAREST), &monitor)) {
                // Windows retains invisible resize borders around a maximized
                // overlapped window. Its client area must exclude that frame,
                // so custom chrome is fully visible inside the work area.
                bounds->left = std::max(bounds->left, monitor.rcWork.left);
                bounds->top = std::max(bounds->top, monitor.rcWork.top);
                bounds->right = std::min(bounds->right, monitor.rcWork.right);
                bounds->bottom = std::min(bounds->bottom, monitor.rcWork.bottom);
            }
        }
        *result = 0; // Keep the full client area; do not add a native caption.
        return true;
    }
    case WM_NCHITTEST: {
        const POINT point = screenPosition(msg->lParam);
        RECT bounds{};
        if (!GetWindowRect(msg->hwnd, &bounds)) return false;
        if (!IsZoomed(msg->hwnd) && !window->isFullScreen()) {
            const int border = resizeBorder(msg->hwnd, window);
            const bool resizeX = window->minimumWidth() < window->maximumWidth();
            const bool resizeY = window->minimumHeight() < window->maximumHeight();
            const bool left = resizeX && point.x < bounds.left + border;
            const bool right = resizeX && point.x >= bounds.right - border;
            const bool top = resizeY && point.y < bounds.top + border;
            const bool bottom = resizeY && point.y >= bounds.bottom - border;
            if (left || right || top || bottom) {
                *result = top ? (left ? HTTOPLEFT : right ? HTTOPRIGHT : HTTOP) :
                    bottom ? (left ? HTBOTTOMLEFT : right ? HTBOTTOMRIGHT : HTBOTTOM) :
                    left ? HTLEFT : HTRIGHT;
                return true;
            }
        }
        const QPoint position = logicalClientPosition(window, msg->hwnd, point, true);
        *result = !window->isFullScreen() && maximizeButtonAt(window, position) ? HTMAXBUTTON : HTCLIENT;
        // Keep title movement in Qt: a floating title carries the whole-window
        // docking payload; returning HTCAPTION would start a separate OS drag.
        return true;
    }
    case WM_NCMOUSEMOVE: {
        auto* state = frameState(window);
        if (msg->wParam != HTMAXBUTTON) {
            state->setHovered(nullptr);
            return false;
        }
        const auto position = logicalClientPosition(window, msg->hwnd, screenPosition(msg->lParam), true);
        state->setHovered(maximizeButtonAt(window, position), position);
        if (!state->trackingNonClient) {
            TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT), TME_NONCLIENT | TME_LEAVE, msg->hwnd, 0};
            state->trackingNonClient = TrackMouseEvent(&tracking);
        }
        // HTMAXBUTTON plus native non-client processing makes this custom
        // caption eligible for the Windows 11 snap-layout hover menu.
        *result = DefWindowProcW(msg->hwnd, msg->message, msg->wParam, msg->lParam);
        return true;
    }
    case WM_NCMOUSELEAVE: {
        auto* state = frameState(window);
        state->trackingNonClient = false;
        state->setHovered(nullptr);
        return false;
    }
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONDBLCLK: {
        if (msg->wParam != HTMAXBUTTON) return false;
        auto* state = frameState(window);
        const auto position = logicalClientPosition(window, msg->hwnd, screenPosition(msg->lParam), true);
        if (auto* button = maximizeButtonAt(window, position)) {
            state->pressed = button;
            button->setDown(true);
            SetCapture(msg->hwnd);
            *result = 0;
            return true;
        }
        return false;
    }
    case WM_NCLBUTTONUP:
        return finishMaximizeClick(window, msg, frameState(window), result, true);
    case WM_LBUTTONUP:
        return finishMaximizeClick(window, msg, frameState(window), result, false);
    case WM_MOUSEMOVE: {
        auto* state = frameState(window);
        if (!state->pressed) {
            state->setHovered(nullptr);
            return false;
        }
        const auto position = logicalClientPosition(window, msg->hwnd, screenPosition(msg->lParam), false);
        state->pressed->setDown(maximizeButtonAt(window, position) == state->pressed);
        *result = 0;
        return true;
    }
    case WM_CANCELMODE:
    case WM_CAPTURECHANGED:
        frameState(window)->cancelPress();
        return false;
    case WM_SYSKEYDOWN:
        if (msg->wParam == VK_SPACE) {
            showSystemMenu(window, msg->hwnd);
            *result = 0;
            return true;
        }
        return false;
    case WM_SYSCHAR:
        if (msg->wParam == VK_SPACE) {
            *result = 0;
            return true;
        }
        return false;
    case WM_ACTIVATE:
    case WM_DWMCOMPOSITIONCHANGED:
        extendFrame(window, msg->hwnd);
        return false;
    default:
        // In particular, Qt retains WM_GETMINMAXINFO / WM_DPICHANGED handling
        // for live size constraints and per-monitor logical geometry changes.
        return false;
    }
#else
    Q_UNUSED(window)
    Q_UNUSED(message)
    Q_UNUSED(result)
    return false;
#endif
}

} // namespace window_system
