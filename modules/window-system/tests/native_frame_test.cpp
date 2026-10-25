// SPDX-License-Identifier: MIT
#include "platform/windows/window_frame.hpp"
#include "chrome/workspace_window.hpp"
#include "chrome/command_strip.hpp"
#include "docking/dock_surface.hpp"
#include "authored_mouse_input.hpp"
#include "native_input.hpp"

#include <window_system/window_manager.hpp>

#include <QApplication>
#include <QAbstractNativeEventFilter>
#include <QLabel>
#include <QLayout>
#include <QOperatingSystemVersion>
#include <QImage>
#include <QPalette>
#include <QPointer>
#include <QMouseEvent>
#include <QScreen>
#include <QScopeGuard>
#include <QSet>
#include <QVector>
#include <QCursor>
#include <QToolTip>
#include <QTimer>
#include <QTest>
#include <QWindow>
#include <cmath>
#include <iterator>

#include <qt_windows.h>
#include <dwmapi.h>

using namespace window_system;

namespace {

void addWindowKinds()
{
    QTest::addColumn<bool>("floating");
    QTest::newRow("workbench") << false;
    QTest::newRow("floating") << true;
}

WindowId createWindow(WindowManager& manager, bool floating)
{
    const WindowOptions options{"Native frame", QSize(740, 520)};
    const auto owner = manager.createWindow(WindowKind::Workbench, options);
    return floating ? manager.createWindow(WindowKind::Floating, options, owner) : owner;
}

bool hasNativeRegion(HWND handle)
{
    const auto region = CreateRectRgn(0, 0, 0, 0);
    if (!region) return false;
    const int kind = GetWindowRgn(handle, region);
    DeleteObject(region);
    return kind != ERROR;
}
void verifyNativeMask(QWidget* window)
{
    const auto handle = reinterpret_cast<HWND>(window->effectiveWinId());
    const auto actual = CreateRectRgn(0, 0, 0, 0), expected = CreateRectRgn(0, 0, 0, 0);
    const auto release = qScopeGuard([&] { DeleteObject(actual); DeleteObject(expected); });
    QVERIFY(actual && expected);
    QVERIFY(GetWindowRgn(handle, actual) != ERROR);
    const auto ratio = window->devicePixelRatioF();
    for (const auto rect : window->mask()) {
        const auto part = CreateRectRgn(qRound(rect.left() * ratio), qRound(rect.top() * ratio),
                                       qRound((rect.right() + 1) * ratio), qRound((rect.bottom() + 1) * ratio));
        QVERIFY(part);
        const int combined = CombineRgn(expected, expected, part, RGN_OR);
        DeleteObject(part);
        QVERIFY(combined != ERROR);
    }
    // A window region is in window coordinates; the client starts inside the
    // sizing border.
    RECT outer{}; QVERIFY(GetWindowRect(handle, &outer));
    POINT origin{}; QVERIFY(ClientToScreen(handle, &origin));
    QVERIFY(OffsetRgn(expected, origin.x - outer.left, origin.y - outer.top) != ERROR);
    QVERIFY(EqualRgn(actual, expected));
    if (!window->isVisible()) return;
    // Native input must follow the shape, including the empty part of the tab
    // row. Capture does not change WindowFromPoint's geometric hit result.
    std::optional<QPoint> inside, outside;
    for (int y = 2; y < window->height() && (!inside || !outside); y += 3)
        for (int x = 2; x < window->width() && (!inside || !outside); x += 3) {
            const QPoint point(x, y);
            if (window->mask().contains(point)) { if (!inside) inside = point; }
            else if (!outside) outside = point;
        }
    QVERIFY(inside);
    const auto rootAt = [handle, ratio](QPoint point) {
        POINT native{qRound(point.x() * ratio), qRound(point.y() * ratio)};
        if (!ClientToScreen(handle, &native)) return HWND(nullptr);
        return GetAncestor(WindowFromPoint(native), GA_ROOT);
    };
    QCOMPARE(rootAt(*inside), handle);
    if (outside) QVERIFY(rootAt(*outside) != handle);
}

void verifyNativeStyle(QWidget* window)
{
    const auto handle = reinterpret_cast<HWND>(window->winId());
    QVERIFY(IsWindow(handle));
    QVERIFY(window->windowHandle());
    for (const auto flag : {Qt::WindowMinimizeButtonHint, Qt::WindowMaximizeButtonHint,
                            Qt::WindowSystemMenuHint, Qt::WindowCloseButtonHint}) {
        QVERIFY(window->windowFlags().testFlag(flag));
        QVERIFY(window->windowHandle()->flags().testFlag(flag));
    }
    // Qt keeps its framed-window bookkeeping, so geometry() is the client
    // that WM_NCCALCSIZE leaves inside Windows' sizing border.
    QVERIFY(!window->windowFlags().testFlag(Qt::FramelessWindowHint));
    const auto style = DWORD(GetWindowLongPtrW(handle, GWL_STYLE));
    const bool compact = window->property("windowSystemDragPresentation").toBool();
    // The restored-window Snap flyout was independently reproduced with real
    // mouse input only after removing WS_SYSMENU. Keep every other overlapped
    // capability exact; a custom-owned menu preserves the system commands.
    const auto framedStyle = DWORD(WS_OVERLAPPEDWINDOW) & ~DWORD(WS_SYSMENU);
    const auto expectedStyle = compact ? framedStyle & ~DWORD(WS_DLGFRAME) : framedStyle;
    QCOMPARE(style & DWORD(WS_OVERLAPPEDWINDOW), expectedStyle);
    QCOMPARE(style & DWORD(WS_SYSMENU), DWORD(0));
    QCOMPARE(style & DWORD(WS_POPUP), DWORD(0));
    const auto extendedStyle = DWORD(GetWindowLongPtrW(handle, GWL_EXSTYLE));
    QCOMPARE(extendedStyle & DWORD(WS_EX_LAYERED), DWORD(0));
    QCOMPARE(window->windowOpacity(), qreal(1.0));
    // This attribute is documented for Set, not Get; its getter returns
    // E_INVALIDARG. Verify the actual write's HRESULT and argument instead.
    const auto transitions = nativeTransitionConfiguration(window);
    QVERIFY(transitions.attempted);
    QCOMPARE(transitions.result, qint32(S_OK));
    QVERIFY(!transitions.forcedDisabled);
    BOOL nonClientEnabled = FALSE;
    QCOMPARE(DwmGetWindowAttribute(handle, DWMWA_NCRENDERING_ENABLED, &nonClientEnabled, sizeof(nonClientEnabled)), S_OK);
    // The independent Win32 fixtures retain the full-caption region constraint
    // and prove the captionless transition used here keeps NC composition.
    QVERIFY(nonClientEnabled);
    if (compact && !window->mask().isEmpty()) verifyNativeMask(window);
}

void verifyMaximizedWorkArea(QWidget* window)
{
    const auto handle = reinterpret_cast<HWND>(window->winId());
    RECT outer{}, client{};
    MONITORINFO monitor{sizeof(MONITORINFO)};
    QVERIFY(GetMonitorInfoW(MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST), &monitor));
    QVERIFY(GetWindowRect(handle, &outer));
    QVERIFY(GetClientRect(handle, &client));
    POINT origin{};
    QVERIFY(ClientToScreen(handle, &origin));
    // A standard overlapped HWND retains invisible resize borders beyond the
    // work area. An independent, non-Qt Win32 fixture confirms that behavior.
    // The visible frame and the complete custom client must match the work area.
    RECT visible{};
    QCOMPARE(DwmGetWindowAttribute(handle, DWMWA_EXTENDED_FRAME_BOUNDS, &visible, sizeof(visible)), S_OK);
    QCOMPARE(visible.left, monitor.rcWork.left); QCOMPARE(visible.top, monitor.rcWork.top);
    QCOMPARE(visible.right, monitor.rcWork.right); QCOMPARE(visible.bottom, monitor.rcWork.bottom);
    QVERIFY(outer.left <= visible.left && outer.top <= visible.top);
    QVERIFY(outer.right >= visible.right && outer.bottom >= visible.bottom);
    QCOMPARE(origin.x, monitor.rcWork.left); QCOMPARE(origin.y, monitor.rcWork.top);
    QCOMPARE(client.right - client.left, monitor.rcWork.right - monitor.rcWork.left);
    QCOMPARE(client.bottom - client.top, monitor.rcWork.bottom - monitor.rcWork.top);
}

class FirstShowProbe final : public QAbstractNativeEventFilter {
public:
    struct Sample { HRESULT result; bool enabled; bool shaped; DWORD style; };
    explicit FirstShowProbe(HWND handle) : handle_(handle) { qApp->installNativeEventFilter(this); }
    ~FirstShowProbe() override { qApp->removeNativeEventFilter(this); }
    QVector<Sample> samples;
    bool nativeEventFilter(const QByteArray&, void* message, qintptr*) override {
        const auto* event = static_cast<const MSG*>(message);
        if (!event || event->hwnd != handle_) return false;
        const bool showing = event->message == WM_SHOWWINDOW && event->wParam;
        const bool positioning = event->message == WM_WINDOWPOSCHANGING && event->lParam
            && (reinterpret_cast<const WINDOWPOS*>(event->lParam)->flags & SWP_SHOWWINDOW);
        if (showing || positioning) {
            BOOL enabled = FALSE;
            const HRESULT result = DwmGetWindowAttribute(handle_, DWMWA_NCRENDERING_ENABLED, &enabled, sizeof(enabled));
            samples.append({result, enabled != FALSE, hasNativeRegion(handle_), DWORD(GetWindowLongPtrW(handle_, GWL_STYLE))});
        }
        return false;
    }
private:
    HWND handle_;
};

// QWindow publishes its mask cache after the native SetWindowRgn call. A
// synchronous placement callback can therefore replay its previous empty mask
// while QWidget has already published the compact shape. Inject that ordering
// once; do not fail a GDI call or retry the operation under test.
class PreviousMaskReplay final : public QAbstractNativeEventFilter {
public:
    explicit PreviousMaskReplay(QWidget* window) : window_(window), handle_(reinterpret_cast<HWND>(window->effectiveWinId()))
    { qApp->installNativeEventFilter(this); }
    ~PreviousMaskReplay() override { qApp->removeNativeEventFilter(this); }
    int replays = 0;
    bool shapedBeforeReplay = false;
    bool cleared = false;
    bool nativeEventFilter(const QByteArray&, void* message, qintptr*) override
    {
        const auto* event = static_cast<const MSG*>(message);
        if (replays || !window_ || !event || event->hwnd != handle_ || event->message != WM_WINDOWPOSCHANGED
            || window_->mask().isEmpty() || !window_->windowHandle() || !window_->windowHandle()->mask().isEmpty()) return false;
        ++replays;
        shapedBeforeReplay = hasNativeRegion(handle_);
        window_->windowHandle()->setMask(window_->windowHandle()->mask());
        cleared = !hasNativeRegion(handle_);
        return false;
    }
private:
    QPointer<QWidget> window_;
    HWND handle_;
};

bool readScreenPixels(QWidget* window, const QVector<QPoint>& positions, QVector<COLORREF>& pixels, QString* reason = nullptr)
{
    const auto dc = GetDC(nullptr);
    if (!dc) { if (reason) *reason = "GetDC failed"; return false; }
    const auto release = qScopeGuard([dc] { ReleaseDC(nullptr, dc); });
    const auto handle = reinterpret_cast<HWND>(window->effectiveWinId());
    const qreal ratio = window->devicePixelRatioF();
    pixels.clear();
    for (const auto logical : positions) {
        POINT native{qRound(logical.x() * ratio), qRound(logical.y() * ratio)};
        if (!ClientToScreen(handle, &native)) { if (reason) *reason = "ClientToScreen failed"; return false; }
        const auto root = GetAncestor(WindowFromPoint(native), GA_ROOT);
        if (root != handle) {
            if (reason) *reason = QString("Ownership %1,%2: root %3 wanted %4").arg(native.x).arg(native.y).arg(quintptr(root)).arg(quintptr(handle));
            return false;
        }
        const auto color = GetPixel(dc, native.x, native.y);
        if (color == CLR_INVALID) { if (reason) *reason = "GetPixel failed"; return false; }
        pixels.append(color);
    }
    return true;
}

QVector<COLORREF> renderedPixels(QWidget* window, const QVector<QPoint>& positions)
{
    const auto rendered = window->grab().toImage();
    QVector<COLORREF> colors;
    for (const auto point : positions) {
        const auto color = rendered.pixelColor(qRound(point.x() * rendered.devicePixelRatio()),
                                               qRound(point.y() * rendered.devicePixelRatio()));
        colors.append(RGB(color.red(), color.green(), color.blue()));
    }
    return colors;
}

void sendDragMouse(QWidget* receiver, QEvent::Type type, QPoint global, Qt::MouseButton button = Qt::NoButton)
{
    test::authoredMouseInput().takePriority();
    QMouseEvent event(type, receiver->mapFromGlobal(global), global, button,
                      type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(receiver, &event);
    test::authoredMouseInput().takePriority();
    QApplication::processEvents();
}

} // namespace

// Windows-only target. These contracts verify the native compositor's eligible
// frame and real state transitions, not the temporal pixels of DWM's animation.
// Visual animation and accessibility-setting acceptance remain desktop checks.
class NativeFrameTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanup() { test::authoredMouseInput().finish(); }
    void nativeRegionsDisableNcRenderingDespiteTheEnabledPolicy();
    void removingSysMenuMakesTheNativeMenuUnavailable_data();
    void removingSysMenuMakesTheNativeMenuUnavailable();
    void captionlessRegionKeepsCompositingAfterNativeShowAndRecalculation_data();
    void captionlessRegionKeepsCompositingAfterNativeShowAndRecalculation();
    void frameIsPreparedBeforeTheFirstShow_data();
    void frameIsPreparedBeforeTheFirstShow();
    void customSystemMenuPreservesStandardCommandsAndStates_data();
    void customSystemMenuPreservesStandardCommandsAndStates();
    void customSystemMenuHonorsFixedResizeAxes_data();
    void customSystemMenuHonorsFixedResizeAxes();
    void nativeSystemCommandsKeepWindowTransitionsAndCloseGuard_data() { addWindowKinds(); }
    void nativeSystemCommandsKeepWindowTransitionsAndCloseGuard();
    void customSystemMenuFollowsWindowLifetime_data() { addWindowKinds(); }
    void customSystemMenuFollowsWindowLifetime();
    void preparationIsIdempotentAndDoesNotShowOrMoveTheWindow();
    void nativeCaptionDoesNotConsumeClientSpace_data();
    void nativeCaptionDoesNotConsumeClientSpace();
    void qtFlagChangesKeepTheNativeFrame_data();
    void qtFlagChangesKeepTheNativeFrame();
    void qtStateChangesKeepNativeStylesAndRestoreGeometry_data();
    void qtStateChangesKeepNativeStylesAndRestoreGeometry();
    void expandedModesFillNativeClientWithoutFrameOrRoundCorners_data();
    void expandedModesFillNativeClientWithoutFrameOrRoundCorners();
    void initiallyMaximizedWindowPreservesNormalGeometry_data();
    void initiallyMaximizedWindowPreservesNormalGeometry();
    void preparationLeavesSystemAnimationPreferencesUntouched();
    void detachedWindowRemainsOpaqueAndKeepsItsContentWhileMoving();
    void publicSizeLimitsRemainEffective_data() { addWindowKinds(); }
    void publicSizeLimitsRemainEffective();
    void nativeCorrectionPreservesOtherPlacementRequests_data();
    void nativeCorrectionPreservesOtherPlacementRequests();
    void compactFirstShowAlreadyHasThePreparedNativeFrame_data() { addWindowKinds(); }
    void compactFirstShowAlreadyHasThePreparedNativeFrame();
    void presentationChangesRetainHandleCaptureAndCompositing_data();
    void presentationChangesRetainHandleCaptureAndCompositing();
    void compactMaskCommitSurvivesPreviousCacheReplay_data() { presentationChangesRetainHandleCaptureAndCompositing_data(); }
    void compactMaskCommitSurvivesPreviousCacheReplay();
    void cachedPreparationAndCompositionRefreshKeepTheNativeFrame_data() { addWindowKinds(); }
    void cachedPreparationAndCompositionRefreshKeepTheNativeFrame();
    void nativePaintMessagesCannotReplaceTheCustomClient_data();
    void nativePaintMessagesCannotReplaceTheCustomClient();
    void reorderedWholeWindowDragCannotRestoreTheStockCaption_data();
    void reorderedWholeWindowDragCannotRestoreTheStockCaption();
    void compactPreviewHasNoFrameAcrossItsTransparentGap_data();
    void compactPreviewHasNoFrameAcrossItsTransparentGap();
};

void NativeFrameTest::initTestCase()
{
    qApp->setQuitOnLastWindowClosed(false);
}

void NativeFrameTest::removingSysMenuMakesTheNativeMenuUnavailable_data()
{
    QTest::addColumn<bool>("absentAtCreation");
    QTest::addColumn<bool>("cachedBeforeRemoval");
    QTest::newRow("removed-after-creation") << false << true;
    QTest::newRow("removed-before-first-menu-read") << false << false;
    QTest::newRow("absent-at-creation") << true << false;
}

void NativeFrameTest::removingSysMenuMakesTheNativeMenuUnavailable()
{
    // This fixture calls only Win32. A system-menu copy obtained before removing
    // the style survives; a window without that copy cannot rely on the API to
    // supply a menu after removal. Neither case uses our separately owned popup.
    QFETCH(bool, absentAtCreation);
    QFETCH(bool, cachedBeforeRemoval);
    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSW type{}; type.lpfnWndProc = DefWindowProcW; type.hInstance = instance;
    type.lpszClassName = L"MuxoraIndependentSystemMenuFixture";
    QVERIFY(RegisterClassW(&type));
    const auto unregister = qScopeGuard([instance] { UnregisterClassW(L"MuxoraIndependentSystemMenuFixture", instance); });
    const DWORD normalStyle = DWORD(WS_OVERLAPPEDWINDOW);
    const DWORD customStyle = normalStyle & ~DWORD(WS_SYSMENU);
    const auto handle = CreateWindowExW(0, type.lpszClassName, L"Independent menu",
                                      absentAtCreation ? customStyle : normalStyle,
                                      100, 100, 600, 400, nullptr, nullptr, instance, nullptr);
    QVERIFY(handle);
    const auto destroy = qScopeGuard([handle] { DestroyWindow(handle); });
    HMENU originalMenu = nullptr;
    if (!absentAtCreation) {
        if (cachedBeforeRemoval) {
            originalMenu = GetSystemMenu(handle, FALSE); QVERIFY(originalMenu);
            QCOMPARE(GetMenuItemCount(originalMenu), 7);
            for (const auto command : {SC_RESTORE, SC_MOVE, SC_SIZE, SC_MINIMIZE, SC_MAXIMIZE, SC_CLOSE})
                QVERIFY(GetMenuState(originalMenu, UINT(command), MF_BYCOMMAND) != UINT(-1));
        }
        SetWindowLongPtrW(handle, GWL_STYLE, customStyle);
        QVERIFY(SetWindowPos(handle, nullptr, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED));
    }
    QCOMPARE(DWORD(GetWindowLongPtrW(handle, GWL_STYLE)) & normalStyle, customStyle);
    const auto removedMenu = GetSystemMenu(handle, FALSE);
    if (cachedBeforeRemoval) {
        QCOMPARE(removedMenu, originalMenu); QVERIFY(IsMenu(removedMenu));
        QCOMPARE(GetMenuItemCount(removedMenu), 7);
        for (const auto command : {SC_RESTORE, SC_MOVE, SC_SIZE, SC_MINIMIZE, SC_MAXIMIZE, SC_CLOSE})
            QVERIFY(GetMenuState(removedMenu, UINT(command), MF_BYCOMMAND) != UINT(-1));
    } else QVERIFY(!removedMenu);
    SetWindowLongPtrW(handle, GWL_STYLE, normalStyle);
    QVERIFY(SetWindowPos(handle, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED));
    const auto restoredMenu = GetSystemMenu(handle, FALSE); QVERIFY(restoredMenu);
    QCOMPARE(GetMenuItemCount(restoredMenu), 7);
    if (cachedBeforeRemoval) QCOMPARE(restoredMenu, originalMenu);
}

void NativeFrameTest::nativeRegionsDisableNcRenderingDespiteTheEnabledPolicy()
{
    // No Qt window or module function participates in this fixture. It proves
    // the platform constraint independently of our presentation implementation.
    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSW type{}; type.lpfnWndProc = DefWindowProcW; type.hInstance = instance;
    type.lpszClassName = L"MuxoraIndependentRegionFrameFixture";
    QVERIFY(RegisterClassW(&type));
    const auto unregister = qScopeGuard([instance] { UnregisterClassW(L"MuxoraIndependentRegionFrameFixture", instance); });
    const auto handle = CreateWindowExW(0, type.lpszClassName, L"Independent frame", WS_OVERLAPPEDWINDOW,
                                      100, 100, 600, 400, nullptr, nullptr, instance, nullptr);
    QVERIFY(handle);
    const auto destroy = qScopeGuard([handle] { DestroyWindow(handle); });
    const DWMNCRENDERINGPOLICY policy = DWMNCRP_ENABLED;
    QCOMPARE(DwmSetWindowAttribute(handle, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy)), S_OK);
    BOOL enabled = FALSE;
    QCOMPARE(DwmGetWindowAttribute(handle, DWMWA_NCRENDERING_ENABLED, &enabled, sizeof(enabled)), S_OK);
    QVERIFY(enabled);
    const auto region = CreateRectRgn(0, 0, 180, 30);
    const auto body = CreateRectRgn(0, 30, 600, 400);
    bool transferred = false;
    const auto deleteBody = qScopeGuard([body] { DeleteObject(body); });
    const auto deleteRegion = qScopeGuard([region, &transferred] {
        // Successful SetWindowRgn transfers ownership to the system.
        if (!transferred) DeleteObject(region);
    });
    QVERIFY(region && body);
    QVERIFY(CombineRgn(region, region, body, RGN_OR) != ERROR);
    const int assigned = SetWindowRgn(handle, region, FALSE);
    transferred = assigned != 0;
    QVERIFY(assigned);
    QVERIFY(hasNativeRegion(handle));
    QCOMPARE(DwmSetWindowAttribute(handle, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy)), S_OK);
    QCOMPARE(DwmGetWindowAttribute(handle, DWMWA_NCRENDERING_ENABLED, &enabled, sizeof(enabled)), S_OK);
    QVERIFY(!enabled);
    QVERIFY(SetWindowRgn(handle, nullptr, FALSE));
    QVERIFY(!hasNativeRegion(handle));
    QCOMPARE(DwmSetWindowAttribute(handle, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy)), S_OK);
    QCOMPARE(DwmGetWindowAttribute(handle, DWMWA_NCRENDERING_ENABLED, &enabled, sizeof(enabled)), S_OK);
    QVERIFY(enabled);
}

void NativeFrameTest::captionlessRegionKeepsCompositingAfterNativeShowAndRecalculation_data()
{
    QTest::addColumn<bool>("marginsBeforeStyle");
    QTest::addColumn<bool>("hasSystemMenu");
    QTest::newRow("margins-after-style") << false << true;
    QTest::newRow("production-margins-before-style") << true << true;
    QTest::newRow("no-system-menu-margins-after-style") << false << false;
    QTest::newRow("production-no-system-menu-margins-before-style") << true << false;
}
void NativeFrameTest::captionlessRegionKeepsCompositingAfterNativeShowAndRecalculation()
{
    // No Qt window or WindowSystem function participates. Initial full-caption
    // composition, followed by the captionless/full-client transition, matters:
    // creating a captionless HWND directly does not establish the same state.
    QFETCH(bool, marginsBeforeStyle);
    QFETCH(bool, hasSystemMenu);
    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSW type{};
    type.lpfnWndProc = [](HWND handle, UINT message, WPARAM wParam, LPARAM lParam) -> LRESULT {
        if (message == WM_NCCALCSIZE) return 0;
        return DefWindowProcW(handle, message, wParam, lParam);
    };
    type.hInstance = instance;
    type.lpszClassName = L"MuxoraIndependentCaptionlessRegionFixture";
    QVERIFY(RegisterClassW(&type));
    const auto unregister = qScopeGuard([instance] { UnregisterClassW(L"MuxoraIndependentCaptionlessRegionFixture", instance); });
    const DWORD initialStyle = hasSystemMenu ? DWORD(WS_OVERLAPPEDWINDOW)
                                            : DWORD(WS_OVERLAPPEDWINDOW) & ~DWORD(WS_SYSMENU);
    const DWORD style = initialStyle & ~DWORD(WS_DLGFRAME);
    const auto handle = CreateWindowExW(0, type.lpszClassName, L"Captionless", initialStyle, 100, 100, 600, 400, nullptr, nullptr, instance, nullptr);
    QVERIFY(handle); const auto destroy = qScopeGuard([handle] { DestroyWindow(handle); });
    const DWMNCRENDERINGPOLICY initialPolicy = DWMNCRP_ENABLED;
    QCOMPARE(DwmSetWindowAttribute(handle, DWMWA_NCRENDERING_POLICY, &initialPolicy, sizeof(initialPolicy)), S_OK);
    const MARGINS initialMargins{1, 1, 1, 1};
    QCOMPARE(DwmExtendFrameIntoClientArea(handle, &initialMargins), S_OK);
    const MARGINS margins{};
    if (marginsBeforeStyle) QCOMPARE(DwmExtendFrameIntoClientArea(handle, &margins), S_OK);
    SetWindowLongPtrW(handle, GWL_STYLE, style);
    QVERIFY(SetWindowPos(handle, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED));
    const auto region = CreateRectRgn(0, 0, 180, 30), body = CreateRectRgn(0, 30, 600, 400);
    const auto expected = CreateRectRgn(0, 0, 0, 0), actual = CreateRectRgn(0, 0, 0, 0);
    bool transferred = false;
    const auto regions = qScopeGuard([&] { if (!transferred) DeleteObject(region); DeleteObject(body); DeleteObject(expected); DeleteObject(actual); });
    QVERIFY(region && body && expected && actual);
    QVERIFY(CombineRgn(region, region, body, RGN_OR) != ERROR);
    QVERIFY(CombineRgn(expected, region, nullptr, RGN_COPY) != ERROR);
    transferred = SetWindowRgn(handle, region, FALSE) != 0; QVERIFY(transferred);
    const DWMNCRENDERINGPOLICY policy = DWMNCRP_ENABLED;
    QCOMPARE(DwmSetWindowAttribute(handle, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy)), S_OK);
    QCOMPARE(DwmExtendFrameIntoClientArea(handle, &margins), S_OK);
    BOOL enabled = FALSE;
    QCOMPARE(DwmGetWindowAttribute(handle, DWMWA_NCRENDERING_ENABLED, &enabled, sizeof(enabled)), S_OK); QVERIFY(enabled);
    QVERIFY(GetWindowRgn(handle, actual) != ERROR); QVERIFY(EqualRgn(actual, expected));
    ShowWindow(handle, SW_SHOWNOACTIVATE);
    QVERIFY(SetWindowPos(handle, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED));
    QApplication::processEvents(); QCOMPARE(DwmFlush(), S_OK);
    QVERIFY(IsWindowVisible(handle));
    QCOMPARE(DWORD(GetWindowLongPtrW(handle, GWL_STYLE)) & DWORD(WS_OVERLAPPEDWINDOW), style);
    QCOMPARE(DwmGetWindowAttribute(handle, DWMWA_NCRENDERING_ENABLED, &enabled, sizeof(enabled)), S_OK); QVERIFY(enabled);
    QVERIFY(GetWindowRgn(handle, actual) != ERROR); QVERIFY(EqualRgn(actual, expected));
}

void NativeFrameTest::frameIsPreparedBeforeTheFirstShow_data() { addWindowKinds(); }
void NativeFrameTest::frameIsPreparedBeforeTheFirstShow()
{
    QFETCH(bool, floating);
    WindowManager manager;
    const auto id = createWindow(manager, floating);
    auto* window = manager.windowWidget(id);
    QVERIFY(window);
    QVERIFY(!window->isVisible());
    // A test calling winId() first would itself create the HWND and conceal a
    // regression in initialization order. Require the constructor to prepare it.
    QVERIFY(window->testAttribute(Qt::WA_WState_Created));
    QVERIFY(window->effectiveWinId());
    QVERIFY(!IsWindowVisible(reinterpret_cast<HWND>(window->effectiveWinId())));
    verifyNativeStyle(window);
}

void NativeFrameTest::customSystemMenuPreservesStandardCommandsAndStates_data()
{
    QTest::addColumn<bool>("floating"); QTest::addColumn<int>("state");
    for (const bool floating : {false, true}) for (const int state : {0, 1, 2})
        QTest::newRow(qPrintable(QString("%1-%2").arg(floating ? "floating" : "workbench")
            .arg(state == 0 ? "restored" : state == 1 ? "maximized" : "minimized"))) << floating << state;
}

void NativeFrameTest::customSystemMenuPreservesStandardCommandsAndStates()
{
    QFETCH(bool, floating); QFETCH(int, state);
    WindowManager manager; const auto id = createWindow(manager, floating);
    auto* window = manager.windowWidget(id); QVERIFY(window);
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(window));
    if (state == 1) { window->showMaximized(); QTRY_VERIFY(window->isMaximized()); }
    if (state == 2) { window->showMinimized(); QTRY_VERIFY(window->isMinimized()); }
    const auto handle = reinterpret_cast<HWND>(window->effectiveWinId());
    const auto geometry = window->geometry();
    verifyNativeStyle(window);
    QVERIFY(!GetSystemMenu(handle, FALSE));
    const auto menu = reinterpret_cast<HMENU>(nativeSystemMenuHandle(window));
    QVERIFY(menu && IsMenu(menu));
    QCOMPARE(GetMenuItemCount(menu), 7);
    QSet<UINT> actualCommands;
    int separators = 0;
    const UINT expectedCommands[]{SC_RESTORE, SC_MOVE, SC_SIZE, SC_MINIMIZE, SC_MAXIMIZE, SC_CLOSE};
    size_t nextCommand = 0;
    for (int position = 0; position < GetMenuItemCount(menu); ++position) {
        MENUITEMINFOW item{}; item.cbSize = sizeof(item); item.fMask = MIIM_FTYPE | MIIM_ID | MIIM_SUBMENU;
        QVERIFY(GetMenuItemInfoW(menu, UINT(position), TRUE, &item));
        QVERIFY(!item.hSubMenu);
        if (item.fType & MFT_SEPARATOR) {
            QCOMPARE(position, 5); QCOMPARE(item.fType, UINT(MFT_SEPARATOR)); ++separators;
        } else {
            QCOMPARE(item.fType, UINT(MFT_STRING));
            QVERIFY(nextCommand < std::size(expectedCommands));
            QCOMPARE(item.wID, expectedCommands[nextCommand++]);
            actualCommands.insert(item.wID);
        }
    }
    QCOMPARE(separators, 1);
    QCOMPARE(nextCommand, std::size(expectedCommands));
    QCOMPARE(actualCommands, (QSet<UINT>{SC_RESTORE, SC_MOVE, SC_SIZE, SC_MINIMIZE, SC_MAXIMIZE, SC_CLOSE}));
    const auto enabled = [menu](UINT command) {
        return !(GetMenuState(menu, command, MF_BYCOMMAND) & (MF_DISABLED | MF_GRAYED));
    };
    for (const auto command : actualCommands) {
        QVERIFY(GetMenuState(menu, command, MF_BYCOMMAND) != UINT(-1));
        wchar_t label[128]{};
        QVERIFY(GetMenuStringW(menu, command, label, int(std::size(label)), MF_BYCOMMAND) > 0);
    }
    QCOMPARE(enabled(SC_RESTORE), state != 0);
    QCOMPARE(enabled(SC_MOVE), state == 0);
    QCOMPARE(enabled(SC_SIZE), state == 0);
    QCOMPARE(enabled(SC_MINIMIZE), state != 2);
    QCOMPARE(enabled(SC_MAXIMIZE), state != 1);
    QVERIFY(enabled(SC_CLOSE));
    // Reading/updating the menu must not restore the removed style, replace the
    // HWND, move the window or change its current native placement.
    QCOMPARE(reinterpret_cast<HWND>(window->effectiveWinId()), handle);
    QCOMPARE(window->geometry(), geometry);
    QCOMPARE(window->isMaximized(), state == 1); QCOMPARE(window->isMinimized(), state == 2);
    QCOMPARE(DWORD(GetWindowLongPtrW(handle, GWL_STYLE)) & DWORD(WS_SYSMENU), DWORD(0));
}

void NativeFrameTest::nativeSystemCommandsKeepWindowTransitionsAndCloseGuard()
{
    QFETCH(bool, floating);
    WindowManager manager; const auto id = createWindow(manager, floating);
    const auto tab = manager.addTab(id, new QLabel("Guarded live content"), {"Guarded", "test.label", {}});
    QVERIFY(!tab.isNull());
    QPointer<QWidget> window = manager.windowWidget(id); QVERIFY(window);
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(window));
    const auto handle = reinterpret_cast<HWND>(window->effectiveWinId());
    const auto normalGeometry = window->geometry();
    const auto menu = reinterpret_cast<HMENU>(nativeSystemMenuHandle(window)); QVERIFY(menu && IsMenu(menu));
    SendMessageW(handle, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
    QTRY_VERIFY(window->isMaximized()); QVERIFY(IsZoomed(handle));
    verifyNativeStyle(window); verifyMaximizedWorkArea(window);
    SendMessageW(handle, WM_SYSCOMMAND, SC_RESTORE, 0);
    QTRY_VERIFY(!window->isMaximized()); QTRY_COMPARE(window->geometry(), normalGeometry);
    SendMessageW(handle, WM_SYSCOMMAND, SC_MINIMIZE, 0);
    QTRY_VERIFY(window->isMinimized()); QVERIFY(IsIconic(handle));
    SendMessageW(handle, WM_SYSCOMMAND, SC_RESTORE, 0);
    QTRY_VERIFY(!window->isMinimized()); QTRY_COMPARE(window->geometry(), normalGeometry);
    QCOMPARE(reinterpret_cast<HWND>(window->effectiveWinId()), handle); verifyNativeStyle(window);
    QVector<TabId> requested;
    manager.setCloseGuard([&requested](const QVector<TabId>& tabs) { requested = tabs; return false; });
    SendMessageW(handle, WM_SYSCOMMAND, SC_CLOSE, 0);
    // WM_CLOSE enters Qt's asynchronous QWindowSystemInterface queue. Returning
    // from the native SendMessage does not mean QWidget's close guard ran yet.
    QTRY_COMPARE(requested, QVector<TabId>{tab});
    QVERIFY(manager.model().window(id)); QVERIFY(manager.model().tab(tab)); QVERIFY(window->isVisible());
    QVERIFY(IsWindow(handle)); QVERIFY(IsMenu(menu));
    manager.setCloseGuard([](const QVector<TabId>&) { return true; });
    SendMessageW(handle, WM_SYSCOMMAND, SC_CLOSE, 0);
    QTRY_VERIFY(!manager.model().window(id)); QVERIFY(!manager.model().tab(tab));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(!window); QVERIFY(!IsWindow(handle)); QVERIFY(!IsMenu(menu));
}

void NativeFrameTest::customSystemMenuHonorsFixedResizeAxes_data()
{
    QTest::addColumn<bool>("floating"); QTest::addColumn<int>("fixedAxes");
    for (const bool floating : {false, true}) for (const int axes : {1, 2, 3})
        QTest::newRow(qPrintable(QString("%1-fixed-%2").arg(floating ? "floating" : "workbench")
            .arg(axes == 1 ? "width" : axes == 2 ? "height" : "both"))) << floating << axes;
}

void NativeFrameTest::customSystemMenuHonorsFixedResizeAxes()
{
    QFETCH(bool, floating); QFETCH(int, fixedAxes);
    WindowManager manager; const auto id = createWindow(manager, floating);
    auto* window = manager.windowWidget(id); QVERIFY(window);
    if (fixedAxes & 1) window->setFixedWidth(window->width());
    if (fixedAxes & 2) window->setFixedHeight(window->height());
    const auto handle = reinterpret_cast<HWND>(window->effectiveWinId());
    const auto menu = reinterpret_cast<HMENU>(nativeSystemMenuHandle(window)); QVERIFY(menu && IsMenu(menu));
    const auto sizeState = GetMenuState(menu, SC_SIZE, MF_BYCOMMAND);
    QVERIFY(sizeState != UINT(-1));
    QCOMPARE(bool(sizeState & (MF_DISABLED | MF_GRAYED)), fixedAxes == 3);
    const auto moveState = GetMenuState(menu, SC_MOVE, MF_BYCOMMAND);
    QVERIFY(moveState != UINT(-1)); QVERIFY(!(moveState & (MF_DISABLED | MF_GRAYED)));
    QCOMPARE(DWORD(GetWindowLongPtrW(handle, GWL_STYLE)) & DWORD(WS_SYSMENU), DWORD(0));
    QVERIFY(!window->isVisible());
}

void NativeFrameTest::customSystemMenuFollowsWindowLifetime()
{
    QFETCH(bool, floating);
    HMENU menu = nullptr;
    HWND handle = nullptr;
    {
        WindowManager manager; const auto id = createWindow(manager, floating);
        auto* window = manager.windowWidget(id); QVERIFY(window);
        handle = reinterpret_cast<HWND>(window->effectiveWinId());
        menu = reinterpret_cast<HMENU>(nativeSystemMenuHandle(window)); QVERIFY(menu && IsMenu(menu));
        const auto initialGeometry = window->geometry();
        for (int index = 0; index < 4; ++index) {
            applyNativeFrame(window);
            QCOMPARE(reinterpret_cast<HMENU>(nativeSystemMenuHandle(window)), menu);
            QCOMPARE(window->geometry(), initialGeometry); QVERIFY(!window->isVisible());
            verifyNativeStyle(window);
        }
        window->setWindowFlag(Qt::WindowStaysOnTopHint, true);
        // Changing Qt flags may destroy the native window and defer creating
        // its replacement. Explicitly request it, as the existing flag-change
        // fixture does, before checking that the replacement was prepared.
        handle = reinterpret_cast<HWND>(window->winId());
        QVERIFY(IsWindow(handle));
        QVERIFY(!window->isVisible()); QVERIFY(!IsWindowVisible(handle));
        QVERIFY(!GetSystemMenu(handle, FALSE));
        const auto currentMenu = reinterpret_cast<HMENU>(nativeSystemMenuHandle(window));
        QVERIFY(currentMenu && IsMenu(currentMenu));
        menu = currentMenu;
        QCOMPARE(GetMenuItemCount(menu), 7);
        verifyNativeStyle(window);
    }
    QVERIFY(!IsWindow(handle)); QVERIFY(!IsMenu(menu));
}

void NativeFrameTest::preparationIsIdempotentAndDoesNotShowOrMoveTheWindow()
{
    WindowManager manager;
    const auto id = createWindow(manager, false);
    auto* window = manager.windowWidget(id);
    QVERIFY(window);
    const auto handle = window->effectiveWinId();
    const auto geometry = window->geometry();
    for (int index = 0; index < 4; ++index) {
        applyNativeFrame(window);
        QCOMPARE(window->effectiveWinId(), handle);
        QCOMPARE(window->geometry(), geometry);
        QVERIFY(!window->isVisible());
        QVERIFY(!IsWindowVisible(reinterpret_cast<HWND>(handle)));
        verifyNativeStyle(window);
    }
}

void NativeFrameTest::nativeCaptionDoesNotConsumeClientSpace_data() { addWindowKinds(); }
void NativeFrameTest::nativeCaptionDoesNotConsumeClientSpace()
{
    QFETCH(bool, floating);
    WindowManager manager;
    const auto id = createWindow(manager, floating);
    auto* window = manager.windowWidget(id);
    QVERIFY(window);
    const auto handle = reinterpret_cast<HWND>(window->winId());
    RECT outer{}, client{};
    QVERIFY(GetWindowRect(handle, &outer));
    QVERIFY(GetClientRect(handle, &client));
    POINT origin{};
    QVERIFY(ClientToScreen(handle, &origin));
    // No caption above the client. Windows' sizing border stays outside the
    // left, right and bottom, as in every Win32 window.
    const UINT dpi = GetDpiForWindow(handle); QVERIFY(dpi != 0);
    const int border = GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
    QCOMPARE(origin.x, outer.left + border);
    QCOMPARE(origin.y, outer.top);
    QCOMPARE(client.right - client.left, outer.right - outer.left - 2 * border);
    QCOMPARE(client.bottom - client.top, outer.bottom - outer.top - border);
    // Qt's geometry is that client, so placement and size apply to it.
    const qreal ratio = window->devicePixelRatioF();
    QCOMPARE(window->geometry().size(), QSize(qRound(client.right / ratio), qRound(client.bottom / ratio)));
    verifyNativeStyle(window);
}

void NativeFrameTest::qtFlagChangesKeepTheNativeFrame_data() { addWindowKinds(); }
void NativeFrameTest::qtFlagChangesKeepTheNativeFrame()
{
    QFETCH(bool, floating);
    WindowManager manager;
    const auto id = createWindow(manager, floating);
    auto* window = manager.windowWidget(id);
    QVERIFY(window);
    window->setWindowFlag(Qt::WindowStaysOnTopHint, true);
    verifyNativeStyle(window);
    QVERIFY(!window->isVisible());
    window->setWindowFlag(Qt::WindowStaysOnTopHint, false);
    verifyNativeStyle(window);
    QVERIFY(!window->isVisible());
}

void NativeFrameTest::qtStateChangesKeepNativeStylesAndRestoreGeometry_data() { addWindowKinds(); }
void NativeFrameTest::qtStateChangesKeepNativeStylesAndRestoreGeometry()
{
    QFETCH(bool, floating);
    WindowManager manager;
    const auto id = createWindow(manager, floating);
    QVERIFY(!manager.addTab(id, new QLabel("Content"), {"Content", "test.label", {}}).isNull());
    auto* window = manager.windowWidget(id);
    QVERIFY(window);
    window->move(160, 160);
    manager.showWindow(id);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    const auto normal = window->geometry();
    const auto handle = reinterpret_cast<HWND>(window->winId());
    window->showMinimized();
    QTRY_VERIFY(window->isMinimized());
    QVERIFY(IsIconic(handle));
    verifyNativeStyle(window);
    window->showNormal();
    QTRY_VERIFY(!window->isMinimized());
    QVERIFY(!IsIconic(handle));
    QCOMPARE(window->geometry(), normal);
    window->showMaximized();
    QTRY_VERIFY(window->isMaximized());
    QVERIFY(IsZoomed(handle));
    verifyMaximizedWorkArea(window);
    verifyNativeStyle(window);
    window->showNormal();
    QTRY_VERIFY(!window->isMaximized());
    QVERIFY(!IsZoomed(handle));
    QCOMPARE(window->geometry(), normal);
    verifyNativeStyle(window);
    QVERIFY(manager.requestCloseWindow(id));
    QVERIFY(!manager.model().window(id));
    QVERIFY(!IsWindowVisible(handle));
}

void NativeFrameTest::expandedModesFillNativeClientWithoutFrameOrRoundCorners_data()
{
    QTest::addColumn<bool>("floating"); QTest::addColumn<int>("mode");
    for (const bool floating : {false, true})
        for (const auto mode : {ViewMode::WindowFullscreen, ViewMode::GroupFullscreen, ViewMode::PaneFullscreen})
            QTest::newRow(qPrintable(QStringLiteral("%1-mode-%2").arg(floating ? "floating" : "workbench").arg(int(mode))))
                << floating << int(mode);
}

void NativeFrameTest::expandedModesFillNativeClientWithoutFrameOrRoundCorners()
{
    QFETCH(bool, floating); QFETCH(int, mode);
    WindowManager manager; const auto id = createWindow(manager, floating);
    const auto tab = manager.addTab(id, new QLabel("Live content"), {"Live", "test.label", {}});
    const auto pane = manager.model().tab(tab)->activePane;
    const QPointer<QWidget> content(manager.paneWidget(pane));
    auto* window = manager.windowWidget(id); QVERIFY(window);
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(window));
    const auto handle = reinterpret_cast<HWND>(window->effectiveWinId());
    const auto normal = window->geometry();
    const auto verifyExpandedFrame = [window] {
        const auto configuration = nativeFrameConfiguration(window);
        QVERIFY(configuration.attempted); QVERIFY(configuration.expanded);
        QCOMPARE(configuration.cornerPreference, quint32(1));
        QCOMPARE(configuration.borderColor, quint32(0xfffffffe));
        QCOMPARE(window->layout()->contentsMargins(), QMargins(0, 0, 0, 0));
        QVERIFY(window->property("windowSystemExpanded").toBool());
        // Microsoft documents these attributes for Set, not Get. Some DWM
        // versions accept a corner query but reject a border-color query;
        // neither getter is an acceptance contract. Check the actual writes
        // against the independent documented Windows build requirement.
        const bool supported = QOperatingSystemVersion::current().microVersion() >= 22000;
        QCOMPARE(configuration.cornerResult, qint32(supported ? S_OK : E_INVALIDARG));
        QCOMPARE(configuration.borderResult, qint32(supported ? S_OK : E_INVALIDARG));
    };
    window->showMaximized(); QTRY_VERIFY(window->isMaximized());
    verifyExpandedFrame(); verifyMaximizedWorkArea(window);
    if (auto* status = window->findChild<QLabel*>("StatusBar")) {
        QTRY_COMPARE(status->mapTo(window, QPoint()).x(), 0);
        QCOMPARE(status->width(), window->width());
        QCOMPARE(status->mapTo(window, status->rect().bottomRight()).y(), window->height() - 1);
    }
    window->showNormal(); QTRY_COMPARE(window->geometry(), normal);
    QVERIFY(manager.setViewMode(id, ViewMode(mode)));
    QTRY_VERIFY(window->isFullScreen());
    QVERIFY(window->windowHandle()->windowStates().testFlag(Qt::WindowFullScreen));
    QVERIFY(!IsZoomed(handle));
    verifyExpandedFrame(); QVERIFY(nativeFrameConfiguration(window).fullscreen);
    MONITORINFO monitor{sizeof(MONITORINFO)};
    QVERIFY(GetMonitorInfoW(MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST), &monitor));
    RECT native{}, client{}; QVERIFY(GetWindowRect(handle, &native)); QVERIFY(GetClientRect(handle, &client));
    QCOMPARE(native.left, monitor.rcMonitor.left); QCOMPARE(native.top, monitor.rcMonitor.top);
    QCOMPARE(native.right, monitor.rcMonitor.right); QCOMPARE(native.bottom, monitor.rcMonitor.bottom);
    QCOMPARE(client.right - client.left, monitor.rcMonitor.right - monitor.rcMonitor.left);
    QCOMPARE(client.bottom - client.top, monitor.rcMonitor.bottom - monitor.rcMonitor.top);
    QCOMPARE(window->effectiveWinId(), WId(handle)); QCOMPARE(manager.paneWidget(pane), content.data());
    QVERIFY(manager.setViewMode(id, ViewMode::Normal)); QTRY_COMPARE(window->geometry(), normal);
    const auto restored = nativeFrameConfiguration(window);
    QVERIFY(!restored.expanded); QVERIFY(!restored.fullscreen);
    QCOMPARE(restored.cornerPreference, quint32(2));
    QCOMPARE(restored.borderColor, quint32(0xffffffff));
    // DWM draws the left, right and bottom border outside the client.
    QCOMPARE(window->layout()->contentsMargins(), QMargins(0, 1, 0, 0));
    QCOMPARE(manager.paneWidget(pane), content.data()); QVERIFY(manager.model().validate().ok);
}

void NativeFrameTest::preparationLeavesSystemAnimationPreferencesUntouched()
{
    ANIMATIONINFO original{sizeof(ANIMATIONINFO), 0};
    BOOL originalClientAnimations = FALSE;
    QVERIFY(SystemParametersInfoW(SPI_GETANIMATION, sizeof(original), &original, 0));
    QVERIFY(SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &originalClientAnimations, 0));
    WindowManager manager;
    const auto id = createWindow(manager, false);
    applyNativeFrame(manager.windowWidget(id));
    ANIMATIONINFO after{sizeof(ANIMATIONINFO), 0};
    BOOL afterClientAnimations = FALSE;
    QVERIFY(SystemParametersInfoW(SPI_GETANIMATION, sizeof(after), &after, 0));
    QVERIFY(SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &afterClientAnimations, 0));
    QCOMPARE(after.iMinAnimate, original.iMinAnimate);
    QCOMPARE(afterClientAnimations, originalClientAnimations);
}

void NativeFrameTest::initiallyMaximizedWindowPreservesNormalGeometry_data()
{
    QTest::addColumn<bool>("floating"); QTest::addColumn<bool>("throughModule");
    QTest::newRow("workbench-module") << false << true;
    QTest::newRow("floating-module") << true << true;
    QTest::newRow("workbench-hidden-widget") << false << false;
    QTest::newRow("floating-hidden-widget") << true << false;
}
void NativeFrameTest::initiallyMaximizedWindowPreservesNormalGeometry()
{
    QFETCH(bool, floating);
    QFETCH(bool, throughModule);
    WindowManager manager;
    const auto id = createWindow(manager, floating);
    auto* window = manager.windowWidget(id);
    QVERIFY(window);
    const QRect normal(180, 170, 900, 650);
    QVERIFY(!window->isVisible());
    const auto handle = reinterpret_cast<HWND>(window->winId());
    auto* workspace = static_cast<WorkspaceWindow*>(window);
    if (throughModule) workspace->restorePlacement(normal, true);
    else { window->setGeometry(normal); window->showMaximized(); }
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QVERIFY(window->isMaximized());
    verifyMaximizedWorkArea(window);
    QCOMPARE(workspace->normalPlacementGeometry(), normal);
    verifyNativeStyle(window);
    WINDOWPLACEMENT maximizedPlacement{}; maximizedPlacement.length = sizeof(maximizedPlacement);
    QVERIFY(GetWindowPlacement(handle, &maximizedPlacement));
    window->showNormal();
    // Verify the native restore immediately, then wait for Qt's queued geometry
    // notification. QWidget's state flag is synchronous and alone cannot prove
    // that its cached rectangle has received that later native notification.
    WINDOWPLACEMENT restoredPlacement{}; restoredPlacement.length = sizeof(restoredPlacement);
    QVERIFY(GetWindowPlacement(handle, &restoredPlacement));
    QCOMPARE(restoredPlacement.rcNormalPosition.left, maximizedPlacement.rcNormalPosition.left);
    QCOMPARE(restoredPlacement.rcNormalPosition.top, maximizedPlacement.rcNormalPosition.top);
    QCOMPARE(restoredPlacement.rcNormalPosition.right, maximizedPlacement.rcNormalPosition.right);
    QCOMPARE(restoredPlacement.rcNormalPosition.bottom, maximizedPlacement.rcNormalPosition.bottom);
    QTRY_VERIFY(!window->isMaximized());
    QTRY_COMPARE(window->geometry(), normal);
}

void NativeFrameTest::detachedWindowRemainsOpaqueAndKeepsItsContentWhileMoving()
{
    WindowManager manager;
    const auto owner = createWindow(manager, false);
    const auto tab = manager.addTab(owner, new QLabel("Live content"), {"Live", "test.label", {}});
    const auto pane = manager.model().tab(tab)->activePane;
    const QPointer<QWidget> content = manager.paneWidget(pane);
    const auto floating = manager.detachTab(tab);
    QVERIFY(!floating.isNull());
    auto* window = manager.windowWidget(floating);
    QVERIFY(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    const auto handle = window->effectiveWinId();
    for (int index = 0; index < 4; ++index) {
        window->move(160 + 20 * index, 160 + 10 * index);
        QCoreApplication::processEvents();
        QCOMPARE(window->effectiveWinId(), handle);
        QCOMPARE(manager.paneWidget(pane), content.data());
        verifyNativeStyle(window);
    }
}

void NativeFrameTest::publicSizeLimitsRemainEffective()
{
    QFETCH(bool, floating);
    WindowManager manager; const auto id = createWindow(manager, floating);
    auto* window = manager.windowWidget(id); QVERIFY(window);
    window->setMinimumSize(650, 480); window->setMaximumSize(900, 680);
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(window));
    const auto handle = reinterpret_cast<HWND>(window->winId());
    const qreal ratio = window->devicePixelRatioF();
    MINMAXINFO hints{};
    SendMessageW(handle, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&hints));
    // Tracking sizes are outer sizes; the client limits hold inside the
    // invisible sizing border.
    RECT outer{}, inner{}; QVERIFY(GetWindowRect(handle, &outer)); QVERIFY(GetClientRect(handle, &inner));
    const LONG frameX = (outer.right - outer.left) - inner.right, frameY = (outer.bottom - outer.top) - inner.bottom;
    QVERIFY(frameX > 0); QVERIFY(frameY > 0);
    QCOMPARE(hints.ptMinTrackSize.x, LONG(std::ceil(650 * ratio)) + frameX);
    QCOMPARE(hints.ptMinTrackSize.y, LONG(std::ceil(480 * ratio)) + frameY);
    QCOMPARE(hints.ptMaxTrackSize.x, LONG(std::floor(900 * ratio)) + frameX);
    QCOMPARE(hints.ptMaxTrackSize.y, LONG(std::floor(680 * ratio)) + frameY);
    MONITORINFO monitor{sizeof(MONITORINFO)};
    QVERIFY(GetMonitorInfoW(MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST), &monitor));
    // Maximization is bounded by the monitor's physical work area as well as
    // the public size limit. Tracking limits remain independent and exact.
    const LONG maximumWidth = std::min(LONG(std::floor(900 * ratio)), monitor.rcWork.right - monitor.rcWork.left);
    const LONG maximumHeight = std::min(LONG(std::floor(680 * ratio)), monitor.rcWork.bottom - monitor.rcWork.top);
    const UINT dpi = GetDpiForWindow(handle); QVERIFY(dpi != 0);
    RECT maximumOuter{0, 0, maximumWidth, maximumHeight};
    // Derive the invisible outer frame independently from Win32. WS_BORDER
    // retains the ordinary border pixel; without WS_DLGFRAME this style has
    // no caption, so all four insets describe only the maximized resize frame.
    QVERIFY(AdjustWindowRectExForDpi(&maximumOuter, WS_THICKFRAME | WS_BORDER, FALSE, 0, dpi));
    QCOMPARE(hints.ptMaxSize.x, maximumOuter.right - maximumOuter.left);
    QCOMPARE(hints.ptMaxSize.y, maximumOuter.bottom - maximumOuter.top);
    const auto normal = window->geometry();
    window->showMaximized(); QTRY_VERIFY(window->isMaximized());
    RECT client{}; QVERIFY(GetClientRect(handle, &client));
    QCOMPARE(client.right - client.left, maximumWidth);
    QCOMPARE(client.bottom - client.top, maximumHeight);
    QCOMPARE(static_cast<WorkspaceWindow*>(window)->normalPlacementGeometry(), normal);
    window->showNormal(); QTRY_COMPARE(window->geometry(), normal);
    MINMAXINFO restoredHints{};
    SendMessageW(handle, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&restoredHints));
    QCOMPARE(restoredHints.ptMinTrackSize.x, LONG(std::ceil(650 * ratio)) + frameX);
    QCOMPARE(restoredHints.ptMinTrackSize.y, LONG(std::ceil(480 * ratio)) + frameY);
    QCOMPARE(restoredHints.ptMaxTrackSize.x, LONG(std::floor(900 * ratio)) + frameX);
    QCOMPARE(restoredHints.ptMaxTrackSize.y, LONG(std::floor(680 * ratio)) + frameY);

    // The native system-menu path starts before Qt requests a maximized state.
    // It must preserve the same exact client limits and restore geometry.
    SendMessageW(handle, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
    QTRY_VERIFY(window->isMaximized()); QVERIFY(IsZoomed(handle));
    QVERIFY(GetClientRect(handle, &client));
    QCOMPARE(client.right - client.left, maximumWidth);
    QCOMPARE(client.bottom - client.top, maximumHeight);
    QCOMPARE(static_cast<WorkspaceWindow*>(window)->normalPlacementGeometry(), normal);
    QCOMPARE(reinterpret_cast<HWND>(window->effectiveWinId()), handle);
    SendMessageW(handle, WM_SYSCOMMAND, SC_RESTORE, 0);
    QTRY_VERIFY(!window->isMaximized()); QVERIFY(!IsZoomed(handle));
    QTRY_COMPARE(window->geometry(), normal);
    restoredHints = {};
    SendMessageW(handle, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&restoredHints));
    QCOMPARE(restoredHints.ptMinTrackSize.x, LONG(std::ceil(650 * ratio)) + frameX);
    QCOMPARE(restoredHints.ptMinTrackSize.y, LONG(std::ceil(480 * ratio)) + frameY);
    QCOMPARE(restoredHints.ptMaxTrackSize.x, LONG(std::floor(900 * ratio)) + frameX);
    QCOMPARE(restoredHints.ptMaxTrackSize.y, LONG(std::floor(680 * ratio)) + frameY);
    QCOMPARE(reinterpret_cast<HWND>(window->effectiveWinId()), handle);
}

void NativeFrameTest::nativeCorrectionPreservesOtherPlacementRequests_data()
{
    QTest::addColumn<bool>("floating"); QTest::addColumn<bool>("maximized");
    for (const bool floating : {false, true})
        for (const bool maximized : {false, true})
            QTest::newRow(qPrintable(QString("%1-%2").arg(floating ? "floating" : "workbench")
                .arg(maximized ? "maximized" : "normal"))) << floating << maximized;
}

void NativeFrameTest::nativeCorrectionPreservesOtherPlacementRequests()
{
    QFETCH(bool, floating); QFETCH(bool, maximized);
    WindowManager manager; const auto id = createWindow(manager, floating);
    auto* window = manager.windowWidget(id); QVERIFY(window);
    window->setMinimumSize(650, 480); window->setMaximumSize(900, 680);
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(window));
    const auto handle = reinterpret_cast<HWND>(window->effectiveWinId());
    if (maximized) { window->showMaximized(); QTRY_VERIFY(window->isMaximized()); }
    QCOMPARE(bool(IsZoomed(handle)), maximized);
    const auto geometry = window->geometry();
    MONITORINFO monitor{sizeof(MONITORINFO)};
    QVERIFY(GetMonitorInfoW(MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST), &monitor));
    WINDOWPOS workRequest{};
    workRequest.hwnd = handle; workRequest.hwndInsertAfter = HWND_BOTTOM;
    workRequest.x = int(monitor.rcMonitor.left); workRequest.y = int(monitor.rcMonitor.top);
    workRequest.cx = int(monitor.rcWork.right - monitor.rcWork.left);
    workRequest.cy = int(monitor.rcWork.bottom - monitor.rcWork.top);
    workRequest.flags = SWP_NOZORDER | SWP_NOACTIVATE;
    QVector<WINDOWPOS> requests;
    auto rightScreen = workRequest;
    rightScreen.x += int(monitor.rcMonitor.right - monitor.rcMonitor.left);
    requests.push_back(rightScreen);
    auto lowerScreen = workRequest;
    lowerScreen.y += int(monitor.rcMonitor.bottom - monitor.rcMonitor.top);
    requests.push_back(lowerScreen);
    auto otherWidth = workRequest; --otherWidth.cx; requests.push_back(otherWidth);
    auto otherHeight = workRequest; --otherHeight.cy; requests.push_back(otherHeight);
    auto noMove = workRequest; noMove.flags |= SWP_NOMOVE; requests.push_back(noMove);
    auto noSize = workRequest; noSize.flags |= SWP_NOSIZE; requests.push_back(noSize);
    auto neither = workRequest; neither.flags |= SWP_NOMOVE | SWP_NOSIZE; requests.push_back(neither);
    if (!maximized) requests.push_back(workRequest);
    // These direct dispatches verify preservation of proposed message fields.
    // They do not move the HWND or claim real mixed-DPI/multi-monitor coverage.
    for (auto position : requests) {
        const auto expected = position;
        MSG message{}; message.hwnd = handle; message.message = WM_WINDOWPOSCHANGING;
        message.lParam = reinterpret_cast<LPARAM>(&position);
        qintptr result = 123;
        QVERIFY(!handleNativeFrame(window, &message, &result));
        QCOMPARE(result, qintptr(123));
        QCOMPARE(position.hwnd, expected.hwnd);
        QCOMPARE(position.hwndInsertAfter, expected.hwndInsertAfter);
        QCOMPARE(position.x, expected.x); QCOMPARE(position.y, expected.y);
        QCOMPARE(position.cx, expected.cx); QCOMPARE(position.cy, expected.cy);
        QCOMPARE(position.flags, expected.flags);
    }
    QCOMPARE(reinterpret_cast<HWND>(window->effectiveWinId()), handle);
    QCOMPARE(window->geometry(), geometry);
    QCOMPARE(bool(IsZoomed(handle)), maximized);
}

void NativeFrameTest::compactFirstShowAlreadyHasThePreparedNativeFrame()
{
    QFETCH(bool, floating);
    WindowManager manager;
    const auto id = createWindow(manager, floating);
    const auto tab = manager.addTab(id, new QLabel("Prepared live content"), {"Prepared", "test.label", {}});
    QVERIFY(!tab.isNull());
    auto* window = dynamic_cast<WorkspaceWindow*>(manager.windowWidget(id)); QVERIFY(window);
    QVERIFY(!window->isVisible());
    const auto handle = reinterpret_cast<HWND>(window->effectiveWinId()); QVERIFY(IsWindow(handle));
    window->setDragPresentation(WorkspaceWindow::DragPresentation::Tab);
    QCOMPARE(reinterpret_cast<HWND>(window->effectiveWinId()), handle);
    const auto configuration = nativeFrameConfiguration(window);
    QVERIFY(configuration.attempted && configuration.compact && configuration.nonClientEnabled);
    QCOMPARE(configuration.nonClientResult, qint32(S_OK)); QCOMPARE(configuration.marginsResult, qint32(S_OK));
    verifyNativeStyle(window);
    FirstShowProbe probe(handle);
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(window));
    QVERIFY(!probe.samples.isEmpty());
    for (const auto& sample : probe.samples) {
        QCOMPARE(sample.result, S_OK); QVERIFY(sample.enabled);
        QCOMPARE(sample.style & DWORD(WS_OVERLAPPEDWINDOW), DWORD(WS_OVERLAPPEDWINDOW) & ~DWORD(WS_DLGFRAME | WS_SYSMENU));
    }
    QCOMPARE(reinterpret_cast<HWND>(window->effectiveWinId()), handle);
}

void NativeFrameTest::presentationChangesRetainHandleCaptureAndCompositing_data()
{
    QTest::addColumn<bool>("floating"); QTest::addColumn<int>("presentation");
    for (const bool floating : {false, true})
        for (const auto mode : {WorkspaceWindow::DragPresentation::Tab, WorkspaceWindow::DragPresentation::Left,
                               WorkspaceWindow::DragPresentation::Right, WorkspaceWindow::DragPresentation::Top,
                               WorkspaceWindow::DragPresentation::Bottom})
            QTest::newRow(qPrintable(QString("%1-presentation-%2").arg(floating ? "floating" : "workbench").arg(int(mode)))) << floating << int(mode);
}

void NativeFrameTest::presentationChangesRetainHandleCaptureAndCompositing()
{
    QFETCH(bool, floating); QFETCH(int, presentation);
    WindowManager manager;
    const auto id = createWindow(manager, floating);
    QPointer<QWidget> content = new QLabel("Same live content");
    const auto tab = manager.addTab(id, content, {"Same live tab", "test.label", {}});
    const auto pane = manager.model().tab(tab)->activePane;
    auto* window = dynamic_cast<WorkspaceWindow*>(manager.windowWidget(id)); QVERIFY(window);
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(window));
    const auto handle = reinterpret_cast<HWND>(window->effectiveWinId());
    const auto geometry = window->geometry();
    window->grabMouse(Qt::ClosedHandCursor);
    QCOMPARE(QWidget::mouseGrabber(), window); QCOMPARE(GetCapture(), handle);
    for (int index = 0; index < 3; ++index) {
        window->setDragPresentation(WorkspaceWindow::DragPresentation(presentation));
        QVERIFY(window->isVisible()); QCOMPARE(reinterpret_cast<HWND>(window->effectiveWinId()), handle);
        QCOMPARE(GetCapture(), handle); QCOMPARE(QWidget::mouseGrabber(), window);
        QCOMPARE(window->geometry(), geometry); QCOMPARE(manager.paneWidget(pane), content.data());
        QVERIFY(!window->mask().isEmpty()); QVERIFY(hasNativeRegion(handle));
        verifyNativeStyle(window);
        const auto compact = nativeFrameConfiguration(window);
        QVERIFY(compact.compact); QCOMPARE(compact.nonClientResult, qint32(S_OK));
        window->setDragPresentation(WorkspaceWindow::DragPresentation::Window);
        QVERIFY(window->isVisible()); QCOMPARE(reinterpret_cast<HWND>(window->effectiveWinId()), handle);
        QCOMPARE(GetCapture(), handle); QCOMPARE(QWidget::mouseGrabber(), window);
        QCOMPARE(window->geometry(), geometry); QCOMPARE(manager.paneWidget(pane), content.data());
        QVERIFY(window->mask().isEmpty()); QVERIFY(!hasNativeRegion(handle));
        verifyNativeStyle(window); QVERIFY(!nativeFrameConfiguration(window).compact);
    }
    window->releaseMouse();
    QVERIFY(!QWidget::mouseGrabber());
}

void NativeFrameTest::compactMaskCommitSurvivesPreviousCacheReplay()
{
    QFETCH(bool, floating); QFETCH(int, presentation);
    WindowManager manager;
    const auto id = createWindow(manager, floating);
    QPointer<QWidget> content = new QLabel("Same content after native mask replay");
    const auto tab = manager.addTab(id, content, {"Same live tab", "test.label", {}});
    const auto pane = manager.model().tab(tab)->activePane;
    auto* window = dynamic_cast<WorkspaceWindow*>(manager.windowWidget(id)); QVERIFY(window);
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(window));
    const auto handle = reinterpret_cast<HWND>(window->effectiveWinId());
    const auto geometry = window->geometry();
    window->grabMouse(Qt::ClosedHandCursor);
    const auto release = qScopeGuard([window] { window->releaseMouse(); });
    PreviousMaskReplay replay(window);
    window->setDragPresentation(WorkspaceWindow::DragPresentation(presentation));
    QCOMPARE(replay.replays, 1); QVERIFY(replay.shapedBeforeReplay); QVERIFY(replay.cleared);
    QVERIFY(window->isVisible()); QCOMPARE(reinterpret_cast<HWND>(window->effectiveWinId()), handle);
    QCOMPARE(GetCapture(), handle); QCOMPARE(QWidget::mouseGrabber(), window);
    QCOMPARE(window->geometry(), geometry); QCOMPARE(manager.paneWidget(pane), content.data());
    QVERIFY(!window->mask().isEmpty()); QCOMPARE(window->windowHandle()->mask(), window->mask());
    QVERIFY(hasNativeRegion(handle)); verifyNativeMask(window); verifyNativeStyle(window);
    window->setDragPresentation(WorkspaceWindow::DragPresentation::Window);
    QVERIFY(window->mask().isEmpty()); QVERIFY(window->windowHandle()->mask().isEmpty());
    QVERIFY(!hasNativeRegion(handle)); verifyNativeStyle(window);
}

void NativeFrameTest::cachedPreparationAndCompositionRefreshKeepTheNativeFrame()
{
    QFETCH(bool, floating);
    WindowManager manager;
    const auto id = createWindow(manager, floating);
    auto* window = dynamic_cast<WorkspaceWindow*>(manager.windowWidget(id)); QVERIFY(window);
    const auto handle = reinterpret_cast<HWND>(window->effectiveWinId());
    const auto geometry = window->geometry();
    for (const bool compact : {false, true}) {
        window->setDragPresentation(compact ? WorkspaceWindow::DragPresentation::Tab : WorkspaceWindow::DragPresentation::Window);
        const auto before = nativeFrameConfiguration(window); QVERIFY(before.attempted);
        const auto recalculations = nativePaintGuardActivity(window).clientRecalculations;
        for (int index = 0; index < 4; ++index) {
            applyNativeFrame(window);
            QCOMPARE(nativeFrameConfiguration(window).revision, before.revision);
            QCOMPARE(nativePaintGuardActivity(window).clientRecalculations, recalculations);
            QCOMPARE(reinterpret_cast<HWND>(window->effectiveWinId()), handle);
            QCOMPARE(window->geometry(), geometry); QVERIFY(!window->isVisible());
            verifyNativeStyle(window);
        }
        SendMessageW(handle, WM_DWMCOMPOSITIONCHANGED, 0, 0);
        const auto refreshed = nativeFrameConfiguration(window);
        QVERIFY(refreshed.revision > before.revision); QCOMPARE(refreshed.compact, compact);
        QCOMPARE(refreshed.nonClientResult, qint32(S_OK)); QCOMPARE(refreshed.marginsResult, qint32(S_OK));
        verifyNativeStyle(window); QCOMPARE(reinterpret_cast<HWND>(window->effectiveWinId()), handle);
    }
}

void NativeFrameTest::nativePaintMessagesCannotReplaceTheCustomClient_data()
{
    QTest::addColumn<bool>("floating"); QTest::addColumn<int>("presentation");
    for (const bool floating : {false, true})
        for (const auto mode : {WorkspaceWindow::DragPresentation::Window, WorkspaceWindow::DragPresentation::Tab,
                               WorkspaceWindow::DragPresentation::Left, WorkspaceWindow::DragPresentation::Right,
                               WorkspaceWindow::DragPresentation::Top, WorkspaceWindow::DragPresentation::Bottom})
            QTest::newRow(qPrintable(QString("%1-presentation-%2").arg(floating ? "floating" : "workbench").arg(int(mode)))) << floating << int(mode);
}

void NativeFrameTest::nativePaintMessagesCannotReplaceTheCustomClient()
{
    QFETCH(bool, floating); QFETCH(int, presentation);
    WindowManager manager; const auto id = createWindow(manager, floating);
    auto* content = new QWidget; content->setAutoFillBackground(true);
    auto palette = content->palette(); palette.setColor(QPalette::Window, QColor(24, 89, 161)); content->setPalette(palette);
    const auto tab = manager.addTab(id, content, {"Native paint guard", "test.content", {}});
    QVERIFY(!tab.isNull()); const auto pane = manager.model().tab(tab)->activePane;
    auto* window = dynamic_cast<WorkspaceWindow*>(manager.windowWidget(id)); QVERIFY(window);
    // Screen pixels must come from this fixture, never an unrelated foreground
    // application. This topmost window belongs only to the test process.
    window->setWindowFlag(Qt::WindowStaysOnTopHint, true);
    const auto available = qApp->primaryScreen()->availableGeometry();
    window->resize(QSize(qMin(740, available.width() - 48), qMin(520, available.height() - 48)));
    window->move(available.topLeft() + QPoint(24, 24));
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(window));
    QVERIFY(available.contains(window->geometry()));
    window->setDragPresentation(WorkspaceWindow::DragPresentation(presentation));
    window->raise(); window->repaint(); QCoreApplication::processEvents(); QCOMPARE(DwmFlush(), S_OK);
    QVERIFY(available.contains(window->geometry()));
    const auto handle = reinterpret_cast<HWND>(window->effectiveWinId());
    const auto geometry = window->geometry(); const auto mask = window->mask();
    const bool compact = presentation != int(WorkspaceWindow::DragPresentation::Window);
    QVector<QPoint> samples;
    if (!compact) {
        // Normal DWM activation is allowed to recolor its outer border. Check
        // the actual client content, with an independently specified RGB value.
        for (const auto point : {QPoint(8, 8), content->rect().center(), content->rect().bottomRight() - QPoint(8, 8)})
            samples.append(content->mapTo(window, point));
    } else {
        // A drag presentation may mask the window down to the lifted label:
        // sample along the top and bottom of what is still shown.
        const QRegion shown = mask.isEmpty() ? QRegion(window->rect()) : mask;
        const auto bounds = shown.boundingRect();
        for (const int y : {bounds.top() + 2, bounds.bottom() - 2})
            for (const int x : {bounds.left() + 3, bounds.left() + bounds.width() / 4, bounds.center().x(), bounds.right() - 3}) {
                const QPoint point(x, y);
                if (shown.contains(point)) samples.append(point);
            }
    }
    QCOMPARE(samples.size() >= (compact ? 4 : 3), true);
    QVector<COLORREF> beforePixels, afterPixels;
    // A screen-pixel oracle includes the OS cursor and any pointer highlight.
    // Keep those away from every original sample without changing the oracle.
    QCursor::setPos(available.bottomRight() - QPoint(16, 16));
    QApplication::processEvents();
    const auto rendered = window->grab().toImage();
    QVector<COLORREF> expectedPixels;
    for (const auto sample : samples) {
        const auto color = rendered.pixelColor(qRound(sample.x() * rendered.devicePixelRatio()),
                                               qRound(sample.y() * rendered.devicePixelRatio()));
        expectedPixels.append(RGB(color.red(), color.green(), color.blue()));
    }
    if (!compact) QCOMPARE(expectedPixels, QVector<COLORREF>(samples.size(), RGB(24, 89, 161)));
    QStringList misses;
    const auto captureExact = [&] {
        QString reason;
        const bool read = readScreenPixels(window, samples, beforePixels, &reason);
        const bool matches = read && beforePixels == expectedPixels;
        if (!matches && misses.size() < 20) {
            QStringList values;
            for (const auto color : beforePixels) values.append(QString::number(color));
            misses.append(reason + " pixels=" + values.join(','));
        }
        return matches;
    };
    const auto failurePixels = qScopeGuard([&] {
        if (!QTest::currentTestFailed()) return;
        qInfo() << "Misses" << misses;
        qInfo() << "Pixel diagnostic" << window->geometry() << window->mask().boundingRect()
                << window->devicePixelRatioF() << QCursor::pos() << QToolTip::isVisible()
                << expectedPixels << beforePixels << renderedPixels(window, samples);
        if (auto* label = window->findChild<QWidget*>("DragEdgeLabel"))
            qInfo() << "Label" << label->geometry() << label->underMouse() << label->hasFocus();
        const auto dc = GetDC(nullptr);
        for (int i = 0; i < samples.size(); ++i) {
            POINT point{qRound(samples[i].x() * window->devicePixelRatioF()), qRound(samples[i].y() * window->devicePixelRatioF())};
            ClientToScreen(handle, &point); const auto root = GetAncestor(WindowFromPoint(point), GA_ROOT);
            wchar_t name[128]{}; GetClassNameW(root, name, 128);
            qInfo() << i << samples[i] << point.x << point.y << quintptr(handle) << quintptr(root)
                    << QString::fromWCharArray(name) << (dc ? GetPixel(dc, point.x, point.y) : CLR_INVALID);
        }
        if (dc) ReleaseDC(nullptr, dc);
    });
    // Exposed does not mean DWM's creation transition has completed. Require
    // three separate exact samples, resetting on any mismatch. QTRY must read
    // only the settled flag: it evaluates its expression repeatedly, which is
    // unsuitable for a non-monotonic framebuffer read during animation.
    int exactSamples = 0;
    bool settled = false;
    QTimer sampler;
    sampler.setInterval(50);
    QObject::connect(&sampler, &QTimer::timeout, &sampler, [&] {
        exactSamples = captureExact() ? exactSamples + 1 : 0;
        if (exactSamples == 3) { sampler.stop(); settled = true; }
    });
    sampler.start();
    QTRY_VERIFY(settled);
    QVERIFY(!sampler.isActive());
    QCOMPARE(beforePixels, expectedPixels);
    window->grabMouse(Qt::ClosedHandCursor); QCOMPARE(GetCapture(), handle);
    const auto before = nativePaintGuardActivity(window);
    QCOMPARE(SendMessageW(handle, WM_NCPAINT, WPARAM(1), 0), LRESULT(0));
    QCOMPARE(nativePaintGuardActivity(window).paintsSuppressed, before.paintsSuppressed + (compact ? 1 : 0));
    QCOMPARE(SendMessageW(handle, WM_NCACTIVATE, FALSE, 0), LRESULT(TRUE));
    SendMessageW(handle, WM_NCACTIVATE, TRUE, 0);
    QCOMPARE(nativePaintGuardActivity(window).activationsSuppressed, before.activationsSuppressed + 2);
    QCOMPARE(DwmFlush(), S_OK);
    QVERIFY(readScreenPixels(window, samples, afterPixels)); QCOMPARE(afterPixels, beforePixels);
    QCOMPARE(reinterpret_cast<HWND>(window->effectiveWinId()), handle); QCOMPARE(GetCapture(), handle);
    QCOMPARE(QWidget::mouseGrabber(), window); QCOMPARE(window->geometry(), geometry); QCOMPARE(window->mask(), mask);
    QCOMPARE(manager.paneWidget(pane), content);
    if (presentation == int(WorkspaceWindow::DragPresentation::Window)) {
        QVERIFY(mask.isEmpty()); QVERIFY(!hasNativeRegion(handle)); verifyNativeStyle(window);
    } else {
        QVERIFY(!mask.isEmpty()); QVERIFY(hasNativeRegion(handle)); verifyNativeStyle(window);
    }
    const auto configuration = nativeFrameConfiguration(window);
    QVERIFY(configuration.nonClientEnabled); QCOMPARE(configuration.nonClientResult, qint32(S_OK));
    window->releaseMouse();
}

void NativeFrameTest::compactPreviewHasNoFrameAcrossItsTransparentGap_data()
{
    QTest::addColumn<int>("presentation"); QTest::addColumn<bool>("bottomTabs");
    for (const auto mode : {WorkspaceWindow::DragPresentation::Tab, WorkspaceWindow::DragPresentation::Left,
                           WorkspaceWindow::DragPresentation::Right, WorkspaceWindow::DragPresentation::Top,
                           WorkspaceWindow::DragPresentation::Bottom})
        QTest::newRow(qPrintable(QString("presentation-%1").arg(int(mode)))) << int(mode) << false;
    QTest::newRow("bottom-tabs") << int(WorkspaceWindow::DragPresentation::Tab) << true;
}

void NativeFrameTest::compactPreviewHasNoFrameAcrossItsTransparentGap()
{
    QFETCH(int, presentation); QFETCH(bool, bottomTabs);
    QWidget backdrop(nullptr, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    backdrop.setAutoFillBackground(true);
    auto palette = backdrop.palette();
    const QColor background(31, 73, 43);
    palette.setColor(QPalette::Window, background); backdrop.setPalette(palette);
    const auto available = qApp->primaryScreen()->availableGeometry();
    backdrop.setGeometry(available.adjusted(12, 12, -12, -12));
    const BOOL disableBackdropTransitions = TRUE;
    QCOMPARE(DwmSetWindowAttribute(reinterpret_cast<HWND>(backdrop.winId()), DWMWA_TRANSITIONS_FORCEDISABLED,
        &disableBackdropTransitions, sizeof(disableBackdropTransitions)), S_OK);
    backdrop.show(); QVERIFY(QTest::qWaitForWindowExposed(&backdrop));
    test::authoredMouseInput().takePriority();
    native_input::mouse(&backdrop, backdrop.rect().bottomRight() - QPoint(4, 4));
    QVector<COLORREF> pixels;
    const QVector<QPoint> backdropSamples{backdrop.rect().center()};
    QTRY_VERIFY(readScreenPixels(&backdrop, backdropSamples, pixels) && pixels == QVector<COLORREF>{RGB(31, 73, 43)});

    WindowManager manager;
    auto appearance = manager.tabAppearance();
    appearance.centralPlacement = bottomTabs ? TabBarPlacement::Bottom : TabBarPlacement::Top;
    QVERIFY(manager.setTabAppearance(appearance));
    const auto owner = manager.createWindow();
    const auto moving = manager.createWindow(WindowKind::Floating, {"Preview gap", {500, 320}}, owner);
    manager.addTab(moving, new QLabel("Live content"), {"Tab", {}, {}});
    auto* carrier = static_cast<WorkspaceWindow*>(manager.windowWidget(moving));
    carrier->setWindowFlag(Qt::WindowStaysOnTopHint, true);
    // This oracle checks the preview's stationary outline. Prepare a stable
    // starting image; the existing whole-window drag test keeps and checks
    // the carrier's opening animation during presentation changes.
    const BOOL disableInitialTransition = TRUE;
    QCOMPARE(DwmSetWindowAttribute(reinterpret_cast<HWND>(carrier->winId()), DWMWA_TRANSITIONS_FORCEDISABLED,
        &disableInitialTransition, sizeof(disableInitialTransition)), S_OK);
    carrier->move(backdrop.geometry().topLeft() + QPoint(40, 40));
    manager.showWindow(moving); QVERIFY(QTest::qWaitForWindowExposed(carrier));
    const auto geometry = carrier->geometry();
    carrier->setDragPresentation(WorkspaceWindow::DragPresentation(presentation));
    QCOMPARE(carrier->geometry(), geometry);
    carrier->raise(); carrier->repaint(); QApplication::processEvents();
    QCOMPARE(DwmFlush(), S_OK);
    const auto handle = reinterpret_cast<HWND>(carrier->effectiveWinId());
    RECT outer{}, client{};
    QVERIFY(GetWindowRect(handle, &outer)); QVERIFY(GetClientRect(handle, &client));
    POINT origin{}; QVERIFY(ClientToScreen(handle, &origin));
    QVERIFY(OffsetRect(&client, origin.x, origin.y));
    QCOMPARE(client.left, outer.left); QCOMPARE(client.top, outer.top);
    QCOMPARE(client.right, outer.right); QCOMPARE(client.bottom, outer.bottom);
    const auto ratio = carrier->devicePixelRatioF();
    const QRegion gaps = QRegion(carrier->rect()).subtracted(carrier->mask());
    QVERIFY(!gaps.isEmpty());

    // Copy a complete frame once per sample. Repeated GetPixel calls would
    // combine different compositor frames and are much slower for this area.
    const auto screen = GetDC(nullptr); QVERIFY(screen);
    const auto releaseScreen = qScopeGuard([&] { ReleaseDC(nullptr, screen); });
    const auto memory = CreateCompatibleDC(screen); QVERIFY(memory);
    const auto releaseMemory = qScopeGuard([&] { DeleteDC(memory); });
    const int width = client.right - client.left, height = client.bottom - client.top;
    BITMAPINFO bitmap{}; bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap.bmiHeader.biWidth = width; bitmap.bmiHeader.biHeight = -height;
    bitmap.bmiHeader.biPlanes = 1; bitmap.bmiHeader.biBitCount = 32; bitmap.bmiHeader.biCompression = BI_RGB;
    void* bits{};
    const auto image = CreateDIBSection(memory, &bitmap, DIB_RGB_COLORS, &bits, nullptr, 0); QVERIFY(image && bits);
    const auto previous = SelectObject(memory, image);
    const auto releaseImage = qScopeGuard([&] { SelectObject(memory, previous); DeleteObject(image); });
    qsizetype samples = 0;
    for (int frame = 0; frame < 25; ++frame) {
        QVERIFY(BitBlt(memory, 0, 0, width, height, screen, client.left, client.top, SRCCOPY));
        QVERIFY(GdiFlush());
        const QImage captured(static_cast<const uchar*>(bits), width, height, QImage::Format_RGB32);
        for (const auto& gap : gaps) for (int y = gap.top(); y <= gap.bottom(); ++y)
            for (int x = gap.left(); x <= gap.right(); ++x) {
                const auto color = captured.pixelColor(qRound(x * ratio), qRound(y * ratio));
                ++samples;
                // DWM's shadow may darken the helper. A bright frame in an
                // otherwise transparent cutout cannot come from that shadow.
                if (color.red() > background.red() || color.green() > background.green() || color.blue() > background.blue())
                    QFAIL(qPrintable(QString("Preview outline in frame %1 at %2,%3: %4").arg(frame).arg(x).arg(y).arg(color.name())));
            }
        QTest::qWait(16);
    }
    QVERIFY(samples > 100);
    verifyNativeMask(carrier);
}

void NativeFrameTest::reorderedWholeWindowDragCannotRestoreTheStockCaption_data()
{
    QTest::addColumn<int>("placement"); QTest::addColumn<bool>("cancel");
    for (int placement = 0; placement < 3; ++placement) for (const bool cancel : {false, true})
        QTest::newRow(qPrintable(QString("layout-%1-%2").arg(placement).arg(cancel ? "cancel" : "release"))) << placement << cancel;
}

void NativeFrameTest::reorderedWholeWindowDragCannotRestoreTheStockCaption()
{
    QFETCH(int, placement); QFETCH(bool, cancel);
    const auto available = qApp->primaryScreen()->availableGeometry();
    const QSize size(qMin(640, available.width() - 48), qMin(420, available.height() - 48));
    WindowManager manager; const auto owner = manager.createWindow();
    const auto target = manager.createWindow(WindowKind::Workbench, {"Target", size});
    const auto targetTab = manager.addTab(target, new QLabel("Target content"), {"Target", {}, {}});
    QVERIFY(!targetTab.isNull());
    const auto moving = manager.createWindow(WindowKind::Floating, {"Customized carrier", size}, owner);
    QVector<PaneId> panes; QVector<QPointer<QWidget>> contents;
    for (const auto& title : {QStringLiteral("Alpha"), QStringLiteral("Beta")}) {
        auto* content = new QWidget; content->setAutoFillBackground(true);
        auto palette = content->palette(); palette.setColor(QPalette::Window, QColor(24, 89, 161)); content->setPalette(palette);
        const auto tab = manager.addTab(moving, content, {title, {}, {}}); QVERIFY(!tab.isNull());
        panes.append(manager.model().tab(tab)->activePane); contents.append(content);
    }
    auto* carrier = static_cast<WorkspaceWindow*>(manager.windowWidget(moving));
    auto* destination = static_cast<WorkspaceWindow*>(manager.windowWidget(target)); QVERIFY(carrier && destination);
    auto* strips = carrier->strips(); QVERIFY(strips);
    if (placement == 0) {
        QVERIFY(strips->moveStrip("caption-controls", 0, 1));
        QCOMPARE(strips->stripOrder(0), (QStringList{"floating-menu", "caption-controls", "title"}));
    } else if (placement == 1) {
        QVERIFY(strips->moveStrip("caption-controls", 1, 0));
        QVERIFY(strips->moveStrip("floating-menu", 1, 1));
        QCOMPARE(strips->stripOrder(1), (QStringList{"caption-controls", "floating-menu"}));
    } else {
        for (const auto& strip : {QStringLiteral("caption-controls"), QStringLiteral("floating-menu"), QStringLiteral("title")})
            QVERIFY(strips->moveStrip(strip, CommandStripHost::Area::Bottom, 0, 99));
        QCOMPARE(strips->stripOrder(CommandStripHost::Area::Bottom, 0),
                 (QStringList{"caption-controls", "floating-menu", "title"}));
    }
    for (auto* window : {destination, carrier}) window->setWindowFlag(Qt::WindowStaysOnTopHint, true);
    // Only the carrier's native transitions are under test. DWM can keep
    // the helper's opening animation above a newer HWND even while native
    // hit testing correctly reports the carrier. Its caption then pollutes
    // the screen-pixel oracle; no carrier assertion or animation is changed.
    const BOOL disableHelperTransitions = TRUE;
    QCOMPARE(DwmSetWindowAttribute(reinterpret_cast<HWND>(destination->winId()),
        DWMWA_TRANSITIONS_FORCEDISABLED, &disableHelperTransitions, sizeof(disableHelperTransitions)), S_OK);
    destination->move(available.topLeft() + QPoint(12, 12)); manager.showWindow(target);
    carrier->move(available.topLeft() + QPoint(36, 36)); manager.showWindow(moving);
    QVERIFY(QTest::qWaitForWindowExposed(carrier));
    const auto original = carrier->geometry(); QVERIFY(available.contains(original));
    const auto originalStrips = carrier->saveStripLayout();
    const auto originalModel = manager.model().toJson();
    const auto handle = reinterpret_cast<HWND>(carrier->effectiveWinId());
    auto* title = carrier->findChild<QLabel*>("CaptionTitle"); QVERIFY(title && title->isVisible());
    const QPoint titlePress(qMin(10, title->width() / 2), title->rect().center().y());
    QVERIFY(title->rect().contains(titlePress));
    const auto press = title->mapToGlobal(titlePress);
    // Keep the desktop pointer away from all screen samples. The component
    // fixture provides its authored drag stream independently of that pointer.
    test::authoredMouseInput().takePriority();
    QCursor::setPos(available.bottomRight() - QPoint(16, 16));
    QApplication::processEvents();
    const auto anchor = title->mapTo(carrier, titlePress);
    for (const int y : {6, 16, 26}) for (const int x : {carrier->width() - 110, carrier->width() - 70, carrier->width() - 25}) {
        const auto distance = anchor - QPoint(x, y);
        QVERIFY(distance.x() * distance.x() + distance.y() * distance.y() >= 64 * 64);
    }
    const auto offset = press - carrier->pos();
    sendDragMouse(title, QEvent::MouseButtonPress, press, Qt::LeftButton);
    sendDragMouse(title, QEvent::MouseMove, press + QPoint(24, 0));
    QCOMPARE(QWidget::mouseGrabber(), carrier); QCOMPARE(GetCapture(), handle);
    const auto targetGroup = manager.model().groupForTab(targetTab);
    const auto targetBar = destination->surface()->tabBarRects().value(targetGroup); QVERIFY(!targetBar.isEmpty());
    const auto onStrip = targetBar.center();
    const auto outside = available.topLeft() + QPoint(24, 24) + offset;
    const QRect restoredMovingGeometry(available.topLeft() + QPoint(24, 24), original.size());
    QVERIFY(available.contains(restoredMovingGeometry));
    for (int cycle = 0; cycle < 2; ++cycle) {
        const auto before = nativePaintGuardActivity(carrier);
        sendDragMouse(carrier, QEvent::MouseMove, onStrip);
        QCOMPARE(carrier->dragPresentation(), WorkspaceWindow::DragPresentation::Tab);
        QCOMPARE(reinterpret_cast<HWND>(carrier->effectiveWinId()), handle); QCOMPARE(GetCapture(), handle);
        QVERIFY(hasNativeRegion(handle)); verifyNativeStyle(carrier);
        sendDragMouse(carrier, QEvent::MouseMove, outside);
        QCOMPARE(carrier->dragPresentation(), WorkspaceWindow::DragPresentation::Window);
        QCOMPARE(carrier->geometry(), restoredMovingGeometry);
        QCOMPARE(reinterpret_cast<HWND>(carrier->effectiveWinId()), handle); QCOMPARE(GetCapture(), handle);
        QVERIFY(carrier->mask().isEmpty()); QVERIFY(!hasNativeRegion(handle)); verifyNativeStyle(carrier);
        QVERIFY(nativePaintGuardActivity(carrier).clientRecalculations >= before.clientRecalculations + 2);
        QCOMPARE(carrier->saveStripLayout(), originalStrips); QCOMPARE(manager.model().toJson(), originalModel);
        for (int index = 0; index < panes.size(); ++index) QCOMPARE(manager.paneWidget(panes[index]), contents[index].data());
        // The native stock buttons paint these upper-right client pixels. Keep
        // them in the oracle even when every custom caption strip is elsewhere.
        QVector<QPoint> samples;
        for (const int y : {6, 16, 26}) for (const int x : {carrier->width() - 110, carrier->width() - 70, carrier->width() - 25})
            samples.append(QPoint(x, y));
        carrier->repaint(); QApplication::processEvents();
        const auto expected = renderedPixels(carrier, samples);
        QVector<COLORREF> actual;
        QTRY_VERIFY(readScreenPixels(carrier, samples, actual) && actual == expected);
        const auto guards = nativePaintGuardActivity(carrier);
        QCOMPARE(SendMessageW(handle, WM_NCPAINT, WPARAM(1), 0), LRESULT(0));
        QCOMPARE(nativePaintGuardActivity(carrier).paintsSuppressed, guards.paintsSuppressed); // Normal DWM remains eligible.
        QCOMPARE(SendMessageW(handle, WM_NCACTIVATE, FALSE, 0), LRESULT(TRUE));
        SendMessageW(handle, WM_NCACTIVATE, TRUE, 0);
        QCOMPARE(nativePaintGuardActivity(carrier).activationsSuppressed, guards.activationsSuppressed + 2);
        QCOMPARE(SendMessageW(handle, 0x00ae, 0, 0), LRESULT(0));
        QCOMPARE(SendMessageW(handle, 0x00af, 0, 0), LRESULT(0));
        QCOMPARE(nativePaintGuardActivity(carrier).themedPaintsSuppressed, guards.themedPaintsSuppressed + 2);
        QCOMPARE(DwmFlush(), S_OK);
        QVERIFY(readScreenPixels(carrier, samples, actual)); QCOMPARE(actual, expected);
        QCOMPARE(carrier->geometry(), restoredMovingGeometry); verifyNativeStyle(carrier);
        QCOMPARE(GetCapture(), handle); QCOMPARE(QWidget::mouseGrabber(), carrier);
    }
    if (cancel) QTest::keyClick(carrier, Qt::Key_Escape);
    else sendDragMouse(carrier, QEvent::MouseButtonRelease, outside, Qt::LeftButton);
    QApplication::processEvents(); QVERIFY(!QWidget::mouseGrabber());
    QCOMPARE(carrier->geometry(), cancel ? original : restoredMovingGeometry);
    QCOMPARE(carrier->saveStripLayout(), originalStrips); QCOMPARE(manager.model().toJson(), originalModel);
    QCOMPARE(reinterpret_cast<HWND>(carrier->effectiveWinId()), handle); verifyNativeStyle(carrier);
    for (int index = 0; index < panes.size(); ++index) QCOMPARE(manager.paneWidget(panes[index]), contents[index].data());
}

QTEST_MAIN(NativeFrameTest)
#include "native_frame_test.moc"
