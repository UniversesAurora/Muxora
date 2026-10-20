// SPDX-License-Identifier: MIT
#include "window_frame.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QEnterEvent>
#include <QEvent>
#include <QPointer>
#include <QHelpEvent>
#include <QScopeGuard>
#include <QStyle>
#include <QTimer>
#include <QToolTip>
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
// UxTheme's internal caption/frame paint notifications, also guarded by
// FramelessHelper's Windows backend. These are not public Win32 API contracts:
// https://github.com/wangwenx190/framelesshelper/blob/main/src/core/framelesshelper_win.cpp#L1247-L1285
constexpr UINT wmNcUahDrawCaption = 0x00ae;
constexpr UINT wmNcUahDrawFrame = 0x00af;

bool fullScreenRequested(QWidget* window)
{
    // QWindow is updated before QWidget receives WindowStateChange. Consult
    // both while Qt is applying a full-screen transition to the native style.
    return window->isFullScreen() || (window->windowHandle()
        && window->windowHandle()->windowStates().testFlag(Qt::WindowFullScreen));
}

DWORD normalFrameStyle(DWORD style, bool compact = false)
{
    // Keep caption/resize/state styles for DWM, but omit the stock system-menu
    // caption buttons. Removing WS_SYSMENU restores the Win11 HTMAXBUTTON Snap
    // flyout in normal windows with a completely custom client frame.
    // Our separately owned popup still dispatches the normal system commands.
    // Compact shapes also remove the caption to avoid a stock Basic frame.
    const auto framed = (style | DWORD(WS_OVERLAPPEDWINDOW)) & ~DWORD(WS_POPUP | WS_SYSMENU);
    return compact ? framed & ~DWORD(WS_CAPTION) : framed;
}

class NativeFrameState final : public QObject {
public:
    explicit NativeFrameState(QWidget* window) : QObject(window), window_(window)
    {
        setObjectName(QString::fromLatin1(frameStateName));
        window->installEventFilter(this);
        tipTimer_.setSingleShot(true);
        connect(&tipTimer_, &QTimer::timeout, this, [this] { showHoveredTip(); });
    }

    void setHovered(QAbstractButton* button, const QPoint& clientPosition = {})
    {
        if (hovered == button) return;
        tipTimer_.stop();
        if (tipShownFor_ && tipShownFor_ == hovered) QToolTip::hideText();
        tipShownFor_.clear();
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
            // Native caption hits get no Qt mouse moves, so Qt never starts
            // its tooltip timer. Use Qt's own timing: immediately while a tip
            // is already showing, otherwise after the style's wake-up delay.
            // Maximize/restore keep the native hover for the Snap flyout.
            const auto role = hovered ? hovered->property("role").toString() : QString();
            if (hovered && role != QStringLiteral("maximize") && role != QStringLiteral("restore")) {
                tipPosition_ = clientPosition;
                tipTimer_.start(QToolTip::isVisible() ? 20
                    : hovered->style()->styleHint(QStyle::SH_ToolTip_WakeUpDelay, nullptr, hovered));
            }
        }
    }

    void showHoveredTip()
    {
        if (!hovered || !window_) return;
        const QPoint local = hovered->mapFrom(window_, tipPosition_);
        QHelpEvent help(QEvent::ToolTip, local, hovered->mapToGlobal(local));
        tipShownFor_ = hovered;
        QApplication::sendEvent(hovered, &help);
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
    HWND recalculatedHandle = nullptr;
    bool recalculatedCompact = false;
    NativeTransitionConfiguration transitions;
    NativeFrameConfiguration configuration;
    NativePaintGuardActivity paintGuards;
    bool attributesReady = false;
    bool configuring = false;
    bool extending = false;


protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == window_ && event->type() == QEvent::WinIdChange) {
            // Invalidate even for a zero handle. Windows can recycle the same
            // numeric HWND after Qt changes flags, so identity alone is not a
            // sufficient cache key across a destroy/create pair.
            configuredHandle = nullptr;
            recalculatedHandle = nullptr;
            attributesReady = false;
            // Window flag changes may replace the HWND. Do not wait for a
            // later showEvent to make the replacement eligible for transitions.
            if (window_ && window_->testAttribute(Qt::WA_WState_Created) && window_->effectiveWinId())
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
    QTimer tipTimer_;
    QPoint tipPosition_;
    QPointer<QAbstractButton> tipShownFor_;
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

QAbstractButton* nativeCaptionButtonAt(QWidget* window, const QPoint& position)
{
    if (fullScreenRequested(window) || window->property("windowSystemDragPresentation").toBool()) return nullptr;
    for (auto* child = window->childAt(position); child && child != window; child = child->parentWidget()) {
        if (child->objectName() != QStringLiteral("CaptionButton")) continue;
        const auto role = child->property("role").toString();
        const bool maximize = role == QStringLiteral("maximize") || role == QStringLiteral("restore");
        const bool otherCaption = role == QStringLiteral("minimize") || role == QStringLiteral("close");
        // A restored window retains its resize rim around the ordinary Qt
        // minimize/close buttons. Maximized chrome has no resize rim: all
        // three controls own their complete screen-edge rectangle.
        if (!maximize && !(otherCaption && window->isMaximized())) return nullptr;
        auto* button = qobject_cast<QAbstractButton*>(child);
        return button && button->isVisible() && button->isEnabled() ? button : nullptr;
    }
    return nullptr;
}

LRESULT captionHit(const QAbstractButton* button)
{
    const auto role = button->property("role").toString();
    return role == QStringLiteral("close") ? HTCLOSE : role == QStringLiteral("minimize") ? HTMINBUTTON : HTMAXBUTTON;
}

bool isCaptionHit(WPARAM hit)
{
    return hit == HTMAXBUTTON || hit == HTMINBUTTON || hit == HTCLOSE;
}

void extendFrame(QWidget* window, HWND handle, bool force = false)
{
    auto* state = frameState(window);
    const bool compact = window->property("windowSystemDragPresentation").toBool();
    const bool fullscreen = fullScreenRequested(window);
    const bool expanded = fullscreen || IsZoomed(handle) || window->isMaximized()
        || (window->windowHandle() && window->windowHandle()->windowStates().testFlag(Qt::WindowMaximized));
    if (state->extending) return;
    if (!force && state->attributesReady && state->configuredHandle == handle && state->configuration.compact == compact
        && state->configuration.expanded == expanded && state->configuration.fullscreen == fullscreen) {
        BOOL enabled = FALSE;
        // A full-caption HWND with a region can lose its DWM NC surface. The
        // captionless transition used for compact previews retains composition;
        // do not treat a successful policy write alone as the actual NC state.
        if (SUCCEEDED(DwmGetWindowAttribute(handle, DWMWA_NCRENDERING_ENABLED, &enabled, sizeof(enabled)))
            && enabled) return;
    }
    const QPointer<NativeFrameState> retained(state);
    state->extending = true;
    const auto reset = qScopeGuard([retained] { if (retained) retained->extending = false; });
    const auto revision = state->configuration.revision + 1;
    // Retain DWM composition in every presentation. Switching it off invalidates
    // the extended frame and allows an intermediate basic non-client frame.
    // Compact previews change margins/corners, never the compositor's policy.
    const DWMNCRENDERINGPOLICY policy = DWMNCRP_ENABLED;
    const HRESULT policyResult = DwmSetWindowAttribute(handle, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy));
    if (!retained) return;
    // Permit the compositor's ordinary transitions. This is not an animation
    // request and does not override Windows' global animation/accessibility
    // preferences. We never change SPI_SETANIMATION or client-area settings.
    const BOOL forceDisableTransitions = FALSE;
    const HRESULT transitionResult = DwmSetWindowAttribute(handle, DWMWA_TRANSITIONS_FORCEDISABLED,
        &forceDisableTransitions, sizeof(forceDisableTransitions));
    if (!retained) return;
    const int clientMargin = compact || expanded ? 0 : 1;
    const MARGINS margins{clientMargin, clientMargin, clientMargin, clientMargin};
    const HRESULT marginsResult = DwmExtendFrameIntoClientArea(handle, &margins);
    if (!retained) return;
    // DWMWA_WINDOW_CORNER_PREFERENCE / DWMWCP_ROUND. Older Windows returns
    // E_INVALIDARG; its normal square corners and resize behavior remain valid.
    constexpr DWORD cornerPreferenceAttribute = 33;
    const DWORD cornerPreference = compact || expanded ? 1 : 2;
    const HRESULT cornerResult = DwmSetWindowAttribute(handle, cornerPreferenceAttribute, &cornerPreference, sizeof(cornerPreference));
    constexpr DWORD borderColorAttribute = 34;
    const COLORREF borderColor = compact || expanded ? COLORREF(0xfffffffe) : COLORREF(0xffffffff);
    const HRESULT borderResult = DwmSetWindowAttribute(handle, borderColorAttribute, &borderColor, sizeof(borderColor));
    if (!retained) return;
    state->configuredHandle = handle;
    state->transitions = {true, qint32(transitionResult), forceDisableTransitions != FALSE};
    state->configuration = {true, compact, true, qint32(policyResult), qint32(marginsResult), revision,
        expanded, fullscreen, clientMargin, quint32(cornerPreference), qint32(cornerResult), quint32(borderColor), qint32(borderResult)};
    state->attributesReady = SUCCEEDED(policyResult) && SUCCEEDED(transitionResult) && SUCCEEDED(marginsResult);
}

bool finishCaptionClick(QWidget* window, MSG* message, NativeFrameState* state, qintptr* result, bool fromScreen)
{
    if (!state->pressed) return false;
    const QPointer<QAbstractButton> button = state->pressed;
    const POINT nativePoint = screenPosition(message->lParam);
    const QPoint position = logicalClientPosition(window, message->hwnd, nativePoint, fromScreen);
    const bool activate = nativeCaptionButtonAt(window, position) == button;
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
    auto* state = frameState(window); // Observe HWND creation/replacement before requesting it.
    if (state->configuring) return;
    const QPointer<NativeFrameState> retained(state);
    const QPointer<QWidget> observed(window);
    state->configuring = true;
    const auto reset = qScopeGuard([retained] { if (retained) retained->configuring = false; });
    const auto handle = reinterpret_cast<HWND>(window->winId());
    if (!observed || !retained || !IsWindow(handle)) return;
    // Establish the compositor attributes before requesting the native frame
    // recalculation. A visible replacement must not paint the interim style.
    extendFrame(window, handle);
    if (!observed) return;
    if (!fullScreenRequested(window)) {
        const bool compact = window->property("windowSystemDragPresentation").toBool();
        const bool presentationChanged = state->recalculatedHandle != handle || state->recalculatedCompact != compact;
        const LONG_PTR original = GetWindowLongPtrW(handle, GWL_STYLE);
        const LONG_PTR desired = normalFrameStyle(DWORD(original), compact);
        if (desired != original) {
            SetWindowLongPtrW(handle, GWL_STYLE, desired);
            if (!observed) return;
        }
        if (desired != original || presentationChanged) {
            // Region removal does not change WS_CAPTION. Recalculate the
            // custom client when leaving a compact presentation as well, so
            // Windows cannot retain the shaped HWND's previous NC geometry.
            const BOOL recalculated = SetWindowPos(handle, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_NOREDRAW);
            if (!observed) return;
            if (recalculated) {
                state->recalculatedHandle = handle; state->recalculatedCompact = compact;
                ++state->paintGuards.clientRecalculations;
            } else state->recalculatedHandle = nullptr;
            window->update(); // Repaint the custom client, with no basic caption pass.
        }
    }
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

NativeFrameConfiguration nativeFrameConfiguration(QWidget* window)
{
#ifdef Q_OS_WIN
    if (window) if (const auto* state = findFrameState(window))
        if (state->configuredHandle == reinterpret_cast<HWND>(window->effectiveWinId())) return state->configuration;
#else
    Q_UNUSED(window)
#endif
    return {};
}

NativePaintGuardActivity nativePaintGuardActivity(QWidget* window)
{
#ifdef Q_OS_WIN
    if (window) if (const auto* state = findFrameState(window)) return state->paintGuards;
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
            if (!(styles->styleNew & WS_CHILD)) styles->styleNew = normalFrameStyle(styles->styleNew, window->property("windowSystemDragPresentation").toBool());
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
                // Retain the standard invisible maximized resize borders:
                // Qt computes logical client geometry by subtracting them
                // from the outer rectangle. NCCALCSIZE clips the real client
                // to the exact work area, so the chrome reaches screen edges.
                const int border = resizeBorder(msg->hwnd, window);
                sizes->ptMaxPosition = {monitor.rcWork.left - monitor.rcMonitor.left - border,
                                        monitor.rcWork.top - monitor.rcMonitor.top - border};
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
                const LONG maximumClientWidth = std::max(sizes->ptMinTrackSize.x, nativeSize(maximum.width(), false));
                const LONG maximumClientHeight = std::max(sizes->ptMinTrackSize.y, nativeSize(maximum.height(), false));
                const bool maximized = IsZoomed(msg->hwnd) || window->isMaximized()
                    || (window->windowHandle() && window->windowHandle()->windowStates().testFlag(Qt::WindowMaximized));
                // In normal state the custom client owns the whole rectangle.
                // A maximized HWND has invisible resize borders; its native
                // tracking cap must include them without relaxing normal drag.
                const int trackingFrame = maximized ? 2 * border : 0;
                const auto framed = [](LONG extent, int extra) {
                    return LONG(std::min<qint64>(std::numeric_limits<LONG>::max(), qint64(extent) + extra));
                };
                sizes->ptMaxTrackSize = {framed(maximumClientWidth, trackingFrame), framed(maximumClientHeight, trackingFrame)};
                sizes->ptMaxSize = {
                    framed(std::min(monitor.rcWork.right - monitor.rcWork.left, maximumClientWidth), 2 * border),
                    framed(std::min(monitor.rcWork.bottom - monitor.rcWork.top, maximumClientHeight), 2 * border)};
                *result = 0;
                return true;
            }
        }
        // Unavailable monitors and full-screen transitions keep Qt's handler.
        // Normal cases use Qt's public size limits, scaled for this HWND's
        // associated Qt window; WM_DPICHANGED remains entirely with Qt.
        return false;
    case WM_WINDOWPOSCHANGING:
        if (msg->lParam && IsZoomed(msg->hwnd) && !IsIconic(msg->hwnd) && !fullScreenRequested(window)) {
            // Qt's frameless WM_SIZE handler tries to replace a bounded
            // maximized outer rectangle with the whole work area. Reapply
            // this HWND's size hints before that correction changes the real
            // client size. An unchanged rectangle produces no resize loop.
            auto* position = reinterpret_cast<WINDOWPOS*>(msg->lParam);
            MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
            if (!(position->flags & (SWP_NOSIZE | SWP_NOMOVE))
                && GetMonitorInfoW(MonitorFromWindow(msg->hwnd, MONITOR_DEFAULTTONEAREST), &monitor)
                && position->x == monitor.rcMonitor.left && position->y == monitor.rcMonitor.top
                && position->cx == monitor.rcWork.right - monitor.rcWork.left
                && position->cy == monitor.rcWork.bottom - monitor.rcWork.top) {
                // Only recognize Qt's exact corrective work-area request.
                // Preserve proposed moves to another monitor or DPI rectangle.
                const QPointer<QWidget> observed(window);
                MINMAXINFO hints{};
                SendMessageW(msg->hwnd, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&hints));
                if (!observed || !IsWindow(msg->hwnd)) { *result = 0; return true; }
                if (hints.ptMaxSize.x <= 0 || hints.ptMaxSize.y <= 0) return false;
                position->cx = hints.ptMaxSize.x; position->cy = hints.ptMaxSize.y;
                position->x = monitor.rcMonitor.left + hints.ptMaxPosition.x;
                position->y = monitor.rcMonitor.top + hints.ptMaxPosition.y;
            }
        }
        return false;
    case WM_NCPAINT:
        // The complete frame is a Qt client. A native window region can make
        // DWM NC rendering ineligible even with DWMNCRP_ENABLED, so letting
        // DefWindowProc paint here would expose a basic caption while dragging.
        if (!window->property("windowSystemDragPresentation").toBool()) {
            BOOL enabled = FALSE;
            if (SUCCEEDED(DwmGetWindowAttribute(msg->hwnd, DWMWA_NCRENDERING_ENABLED, &enabled, sizeof(enabled))) && enabled)
                return false; // Preserve DWM's normal border, shadow and maximized frame.
        }
        ++frameState(window)->paintGuards.paintsSuppressed;
        *result = 0;
        return true;
    case WM_NCACTIVATE:
        // Keep the native activation bookkeeping, without its NC repaint.
        // Minimized windows retain the documented default processing.
        if (IsIconic(msg->hwnd)) return false;
        ++frameState(window)->paintGuards.activationsSuppressed;
        *result = DefWindowProcW(msg->hwnd, msg->message, msg->wParam, LPARAM(-1));
        return true;
    case wmNcUahDrawCaption:
    case wmNcUahDrawFrame:
        // Our caption/control strips may live anywhere in the client, so a
        // theme's stock caption must never overwrite its upper-right pixels.
        // Unlike blocking all WM_NCPAINT, this leaves DWM's border path intact.
        ++frameState(window)->paintGuards.themedPaintsSuppressed;
        *result = 0;
        return true;
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
                const int border = resizeBorder(msg->hwnd, window);
                bounds->left = std::max(bounds->left + border, monitor.rcWork.left);
                bounds->top = std::max(bounds->top + border, monitor.rcWork.top);
                bounds->right = std::min(bounds->right - border, monitor.rcWork.right);
                bounds->bottom = std::min(bounds->bottom - border, monitor.rcWork.bottom);
            }
        }
        *result = 0; // Keep the full client area; do not add a native caption.
        return true;
    }
    case WM_NCHITTEST: {
        if (window->property("windowSystemDragPresentation").toBool()) { *result = HTCLIENT; return true; }
        const POINT point = screenPosition(msg->lParam);
        RECT bounds{};
        if (!GetWindowRect(msg->hwnd, &bounds)) return false;
        const QPoint position = logicalClientPosition(window, msg->hwnd, point, true);
        // Caption controls sit against the top edge. Their visible rectangle
        // owns the hit before the resize rim, so its upper pixels also expose
        // the native Snap affordance in a restored window. The rest of every
        // edge/corner retains its ordinary resize hit.
        if (const auto* button = nativeCaptionButtonAt(window, position)) { *result = captionHit(button); return true; }
        if (!IsZoomed(msg->hwnd) && !fullScreenRequested(window)) {
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
        *result = HTCLIENT;
        // Keep title movement in Qt: a floating title carries the whole-window
        // docking payload; returning HTCAPTION would start a separate OS drag.
        return true;
    }
    case WM_NCMOUSEMOVE: {
        auto* state = frameState(window);
        if (!isCaptionHit(msg->wParam)) {
            state->setHovered(nullptr);
            return false;
        }
        const auto position = logicalClientPosition(window, msg->hwnd, screenPosition(msg->lParam), true);
        auto* button = nativeCaptionButtonAt(window, position);
        state->setHovered(button, position);
        if (!button) return false;
        if (!state->trackingNonClient) {
            TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT), TME_NONCLIENT | TME_LEAVE, msg->hwnd, 0};
            state->trackingNonClient = TrackMouseEvent(&tracking);
        }
        // Only maximize forwards native hover processing for the Win11 Snap
        // flyout. Minimize/close are painted by the actual accessible Qt button.
        *result = msg->wParam == HTMAXBUTTON
            ? DefWindowProcW(msg->hwnd, msg->message, msg->wParam, msg->lParam) : 0;
        return true;
    }
    case WM_NCMOUSEHOVER:
        // Default processing shows the classic system caption tooltip for
        // the native minimize/close hits; the Qt button shows the themed one
        // (see setHovered). Maximize keeps the native hover for Snap.
        if (msg->wParam == HTMINBUTTON || msg->wParam == HTCLOSE) { *result = 0; return true; }
        return false;
    case WM_NCMOUSELEAVE: {
        auto* state = frameState(window);
        state->trackingNonClient = false;
        state->setHovered(nullptr);
        return false;
    }
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONDBLCLK: {
        if (!isCaptionHit(msg->wParam)) return false;
        auto* state = frameState(window);
        const auto position = logicalClientPosition(window, msg->hwnd, screenPosition(msg->lParam), true);
        if (auto* button = nativeCaptionButtonAt(window, position)) {
            state->pressed = button;
            button->setDown(true);
            SetCapture(msg->hwnd);
            *result = 0;
            return true;
        }
        return false;
    }
    case WM_NCLBUTTONUP:
        return finishCaptionClick(window, msg, frameState(window), result, true);
    case WM_LBUTTONUP:
        return finishCaptionClick(window, msg, frameState(window), result, false);
    case WM_MOUSEMOVE: {
        auto* state = frameState(window);
        if (!state->pressed) {
            state->setHovered(nullptr);
            return false;
        }
        const auto position = logicalClientPosition(window, msg->hwnd, screenPosition(msg->lParam), false);
        state->pressed->setDown(nativeCaptionButtonAt(window, position) == state->pressed);
        *result = 0;
        return true;
    }
    case WM_CANCELMODE:
    case WM_CAPTURECHANGED:
        frameState(window)->cancelPress();
        return false;
    case WM_SYSKEYDOWN:
        if (msg->wParam == VK_SPACE) {
            showNativeSystemMenu(window, quintptr(msg->hwnd));
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
        extendFrame(window, msg->hwnd);
        return false;
    case WM_DWMCOMPOSITIONCHANGED:
        extendFrame(window, msg->hwnd, true);
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
