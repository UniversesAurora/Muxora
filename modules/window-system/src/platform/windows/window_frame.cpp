// SPDX-License-Identifier: MIT
#include "window_frame.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QEnterEvent>
#include <QEvent>
#include <QMoveEvent>
#include <QPointer>
#include <QScopeGuard>
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
// Marks an HWND whose frame applyNativeFrame() has switched over. Stored on the
// HWND itself: it survives Qt's style and flag changes and dies with the HWND,
// even when Windows recycles the numeric handle.
constexpr auto frameAppliedProperty = L"WindowSystemCustomFrame";
constexpr UINT wmNcUahDrawFrame = 0x00af;

// Qt emulates full screen with a captionless popup style. Our own frame style
// always keeps WS_THICKFRAME, so this style can only be Qt's full screen.
bool isFullScreenStyle(DWORD style)
{
    return (style & WS_POPUP) && !(style & WS_THICKFRAME);
}

bool fullScreenRequested(QWidget* window)
{
    // QWindow is updated before QWidget receives WindowStateChange. Consult
    // both while Qt is applying a full-screen transition to the native style.
    if (window->isFullScreen() || (window->windowHandle()
        && window->windowHandle()->windowStates().testFlag(Qt::WindowFullScreen))) return true;
    // Qt flushes queued state events in the middle of that transition, so
    // both can briefly report an earlier state. The native style cannot.
    const auto handle = reinterpret_cast<HWND>(window->effectiveWinId());
    return handle && isFullScreenStyle(DWORD(GetWindowLongPtrW(handle, GWL_STYLE)));
}

DWORD normalFrameStyle(DWORD style, bool compact = false)
{
    // Keep caption/resize/state styles for DWM, but omit the stock system-menu
    // caption buttons. Removing WS_SYSMENU restores the Win11 HTMAXBUTTON Snap
    // flyout in normal windows with a completely custom client frame.
    // Our separately owned popup still dispatches the normal system commands.
    // Compact shapes also remove the caption to avoid a stock Basic frame.
    // Without WS_DLGFRAME there is no caption; WS_BORDER stays, so the sizing
    // frame keeps its width and the client does not move.
    const auto framed = (style | DWORD(WS_OVERLAPPEDWINDOW)) & ~DWORD(WS_POPUP | WS_SYSMENU);
    return compact ? framed & ~DWORD(WS_DLGFRAME) : framed;
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
};

NativeFrameState* findFrameState(QWidget* window)
{
    for (auto* child : window->children()) {
        // Runs for every native message; compare without allocating.
        if (child->objectName() == QLatin1StringView(frameStateName)) {
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

// The frame WM_NCCALCSIZE keeps outside a normal window's client: Windows'
// own left, right and bottom border for this style and DPI. The custom
// caption replaces the top.
RECT sizingFrame(HWND handle, QWidget* window)
{
    using AdjustFunction = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
    static const auto adjustForDpi = std::bit_cast<AdjustFunction>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi"));
    const auto style = DWORD(GetWindowLongPtrW(handle, GWL_STYLE));
    const auto extendedStyle = DWORD(GetWindowLongPtrW(handle, GWL_EXSTYLE));
    RECT frame{};
    const BOOL adjusted = adjustForDpi ? adjustForDpi(&frame, style, FALSE, extendedStyle, windowDpi(handle, window))
                                       : AdjustWindowRectEx(&frame, style, FALSE, extendedStyle);
    if (!adjusted) return {};
    return {-frame.left, 0, frame.right, frame.bottom};
}

bool clientScreenRect(HWND handle, RECT* client)
{
    if (!GetClientRect(handle, client)) return false;
    // A zero result is also a valid offset for a client at the screen origin.
    MapWindowPoints(handle, nullptr, reinterpret_cast<POINT*>(client), 2);
    return true;
}

POINT screenPosition(LPARAM position)
{
    // LOWORD/HIWORD lose the sign on monitors left/above the primary monitor.
    return {GET_X_LPARAM(position), GET_Y_LPARAM(position)};
}

// Maximized or full screen, the client reaches the screen edge, but at a
// fractional scale its last physical column or row maps past the last whole
// logical pixel: 3839 / 2 = 1919.5 at 200 %, 3839 / 1.5 = 2559.33 at 150 %.
// Qt then finds no child at that position and delivers the event to the
// window itself, so a caption button or toolbar against the right or bottom
// edge misses the pixel the pointer stops at. Move such a client mouse
// position inward until it maps to the last whole logical pixel or before.
void keepEdgePointerInside(QWidget* window, MSG* msg)
{
    if (!IsZoomed(msg->hwnd) && !fullScreenRequested(window)) return;
    const qreal ratio = std::max<qreal>(1.0, window->devicePixelRatioF());
    int x = GET_X_LPARAM(msg->lParam);
    int y = GET_Y_LPARAM(msg->lParam);
    const int originalX = x, originalY = y;
    while (x > 0 && x / ratio > window->width() - 1) --x;
    while (y > 0 && y / ratio > window->height() - 1) --y;
    if (x != originalX || y != originalY) msg->lParam = MAKELPARAM(x, y);
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
        // Only maximize/restore is a native caption hit, in every state: the
        // system needs HTMAXBUTTON for the Win11 Snap flyout. Minimize and
        // close stay ordinary Qt buttons. A native HTMINBUTTON/HTCLOSE would
        // make Windows track the hover itself and show its classic caption
        // tooltip next to Qt's. Maximized, the client area already reaches
        // the screen edge, so the edge pixels still belong to the Qt buttons.
        if (role != QStringLiteral("maximize") && role != QStringLiteral("restore")) return nullptr;
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

// Drops the axes a fixed window size cannot change from a native sizing hit.
// What remains of a corner is its other edge; nothing left is a plain border.
LRESULT allowedResizeHit(LRESULT hit, bool resizeX, bool resizeY)
{
    if (hit < HTLEFT || hit > HTBOTTOMRIGHT) return hit;
    const bool left = resizeX && (hit == HTLEFT || hit == HTTOPLEFT || hit == HTBOTTOMLEFT);
    const bool right = resizeX && (hit == HTRIGHT || hit == HTTOPRIGHT || hit == HTBOTTOMRIGHT);
    const bool top = resizeY && (hit == HTTOP || hit == HTTOPLEFT || hit == HTTOPRIGHT);
    const bool bottom = resizeY && (hit == HTBOTTOM || hit == HTBOTTOMLEFT || hit == HTBOTTOMRIGHT);
    if (!left && !right && !top && !bottom) return HTBORDER;
    return top ? (left ? HTTOPLEFT : right ? HTTOPRIGHT : HTTOP) :
        bottom ? (left ? HTBOTTOMLEFT : right ? HTBOTTOMRIGHT : HTBOTTOM) :
        left ? HTLEFT : HTRIGHT;
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
    // Compact previews change corners and border, never the compositor's policy.
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
    // The real sizing frame gives the window its DWM shadow and border, so
    // nothing extends into the client.
    const MARGINS margins{};
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
        expanded, fullscreen, quint32(cornerPreference), qint32(cornerResult), quint32(borderColor), qint32(borderResult)};
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
    if (!window || !window->isWindow()) return;
    // Applying the frame opts the window in: handleNativeFrame() serves only
    // windows that have this state. Create it to observe HWND creation and
    // replacement before requesting the handle.
    auto* state = frameState(window);
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
            // Qt copies the window menu while a new HWND still has WS_SYSMENU.
            // Drop that copy: our own menu is the only system menu.
            if (original & WS_SYSMENU) GetSystemMenu(handle, TRUE);
        }
        if (desired != original || presentationChanged) {
            // Region removal does not change the style. Recalculate the
            // custom client when leaving a compact presentation as well, so
            // Windows cannot retain the shaped HWND's previous NC geometry.
            // The client never moves or resizes: a new HWND still has Qt's
            // standard caption frame, so place the outer rectangle around
            // the current client in the same call that removes it.
            RECT client{}, outer{};
            UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_NOREDRAW;
            if (!IsZoomed(handle) && !IsIconic(handle) && clientScreenRect(handle, &client)) {
                const RECT frame = sizingFrame(handle, window);
                outer = {client.left - frame.left, client.top, client.right + frame.right, client.bottom + frame.bottom};
            } else flags |= SWP_NOMOVE | SWP_NOSIZE;
            SetPropW(handle, frameAppliedProperty, HANDLE(1));
            const BOOL recalculated = SetWindowPos(handle, nullptr, outer.left, outer.top,
                outer.right - outer.left, outer.bottom - outer.top, flags);
            if (!observed) return;
            if (recalculated) {
                // QWidget caches its frame offset and refreshes it only when
                // the window reports a move or resize. The client stayed in
                // place, so report the unchanged position: Qt then reads the
                // new margins instead of keeping its creation caption's.
                if (auto* qtWindow = window->windowHandle()) {
                    QMoveEvent moved(qtWindow->position(), qtWindow->position());
                    QCoreApplication::sendEvent(qtWindow, &moved);
                    if (!observed) return;
                }
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
    if (!window || !message || !result || !window->isWindow() || !findFrameState(window)) return false;
    auto* msg = static_cast<MSG*>(message);
    // Client mouse messages carry client coordinates; the wheel messages carry
    // screen coordinates. Qt goes on to translate this same message.
    if (msg->message >= WM_MOUSEFIRST && msg->message <= WM_MOUSELAST
        && msg->message != WM_MOUSEWHEEL && msg->message != WM_MOUSEHWHEEL) keepEdgePointerInside(window, msg);
    switch (msg->message) {
    case WM_STYLECHANGING:
        if (msg->wParam == WPARAM(GWL_STYLE) && msg->lParam) {
            // Leave Qt's full-screen style alone. Its state may still be stale.
            auto* styles = reinterpret_cast<STYLESTRUCT*>(msg->lParam);
            if (!(styles->styleNew & WS_CHILD) && !isFullScreenStyle(styles->styleNew))
                styles->styleNew = normalFrameStyle(styles->styleNew, window->property("windowSystemDragPresentation").toBool());
        }
        // Keep Qt's window flags, state bookkeeping and style-change dispatch.
        return false;
    case WM_GETMINMAXINFO:
        if (msg->lParam && !fullScreenRequested(window)) {
            // Initialize the Win32 defaults, then apply the host's public Qt
            // limits to the client, without a native caption's height.
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
                const auto framed = [](LONG extent, int extra) {
                    return LONG(std::min<qint64>(std::numeric_limits<LONG>::max(), qint64(extent) + extra));
                };
                // Tracking sizes are outer sizes: add the invisible borders
                // outside the client. A maximized HWND has them on all sides.
                const RECT frame = sizingFrame(msg->hwnd, window);
                const int frameX = maximized ? 2 * border : int(frame.left + frame.right);
                const int frameY = maximized ? 2 * border : int(frame.bottom);
                sizes->ptMinTrackSize = {framed(sizes->ptMinTrackSize.x, frameX), framed(sizes->ptMinTrackSize.y, frameY)};
                sizes->ptMaxTrackSize = {framed(maximumClientWidth, frameX), framed(maximumClientHeight, frameY)};
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
        // Until applyNativeFrame() switches a new HWND over, keep the frame Qt
        // created it with, so Qt's creation geometry holds; the switch then
        // keeps the client in place.
        if (!msg->lParam || !GetPropW(msg->hwnd, frameAppliedProperty)) return false;
        auto* bounds = msg->wParam ? &reinterpret_cast<NCCALCSIZE_PARAMS*>(msg->lParam)->rgrc[0]
                                  : reinterpret_cast<RECT*>(msg->lParam);
        // Qt applies the new style before it updates its window state, so the
        // style is what tells a full-screen frame apart, in both directions.
        if (isFullScreenStyle(DWORD(GetWindowLongPtrW(msg->hwnd, GWL_STYLE))) || IsIconic(msg->hwnd)) {
            // The client covers the whole window.
        } else if (IsZoomed(msg->hwnd)) {
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
        } else {
            // Keep Windows' sizing border outside the left, right and bottom
            // of the client, invisible like every Win32 window's, so the
            // client's edge pixels belong to its content. The custom caption
            // reaches the top. Compact previews keep the same frame, so a
            // drag never moves or resizes the client.
            const RECT frame = sizingFrame(msg->hwnd, window);
            bounds->left += frame.left;
            bounds->right -= frame.right;
            bounds->bottom -= frame.bottom;
        }
        *result = 0; // No native caption.
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
            const bool resizeX = window->minimumWidth() < window->maximumWidth();
            const bool resizeY = window->minimumHeight() < window->maximumHeight();
            RECT client{};
            GetClientRect(msg->hwnd, &client);
            MapWindowPoints(msg->hwnd, nullptr, reinterpret_cast<POINT*>(&client), 2);
            if (!PtInRect(&client, point)) {
                // The invisible left, right and bottom border: Windows' own
                // edges and corners, less the axes a fixed size cannot change.
                *result = allowedResizeHit(DefWindowProcW(msg->hwnd, msg->message, msg->wParam, msg->lParam), resizeX, resizeY);
                return true;
            }
            // The top has no border outside the client. A thin band at the
            // top of the custom caption resizes instead.
            if (resizeY && point.y < bounds.top + resizeBorder(msg->hwnd, window)) { *result = HTTOP; return true; }
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
    case WM_NCDESTROY:
        RemovePropW(msg->hwnd, frameAppliedProperty);
        return false;
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
