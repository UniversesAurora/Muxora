// SPDX-License-Identifier: MIT
#include "platform/windows/window_frame.hpp"
#include "chrome/workspace_window.hpp"
#include "chrome/command_strip.hpp"
#include "docking/dock_surface.hpp"

#include <window_system/window_manager.hpp>

#include <QApplication>
#include <QAbstractNativeEventFilter>
#include <QLabel>
#include <QImage>
#include <QPalette>
#include <QPointer>
#include <QMouseEvent>
#include <QScreen>
#include <QScopeGuard>
#include <QVector>
#include <QCursor>
#include <QToolTip>
#include <QTimer>
#include <QTest>
#include <cmath>

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
    const auto style = DWORD(GetWindowLongPtrW(handle, GWL_STYLE));
    const bool compact = window->property("windowSystemDragPresentation").toBool();
    const auto expectedStyle = compact ? DWORD(WS_OVERLAPPEDWINDOW) & ~DWORD(WS_CAPTION) : DWORD(WS_OVERLAPPEDWINDOW);
    QCOMPARE(style & DWORD(WS_OVERLAPPEDWINDOW), expectedStyle);
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
    QMouseEvent event(type, receiver->mapFromGlobal(global), global, button,
                      type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(receiver, &event);
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
    void nativeRegionsDisableNcRenderingDespiteTheEnabledPolicy();
    void captionlessRegionKeepsCompositingAfterNativeShowAndRecalculation_data();
    void captionlessRegionKeepsCompositingAfterNativeShowAndRecalculation();
    void frameIsPreparedBeforeTheFirstShow_data();
    void frameIsPreparedBeforeTheFirstShow();
    void preparationIsIdempotentAndDoesNotShowOrMoveTheWindow();
    void nativeCaptionDoesNotConsumeClientSpace_data();
    void nativeCaptionDoesNotConsumeClientSpace();
    void qtFlagChangesKeepTheNativeFrame_data();
    void qtFlagChangesKeepTheNativeFrame();
    void qtStateChangesKeepNativeStylesAndRestoreGeometry_data();
    void qtStateChangesKeepNativeStylesAndRestoreGeometry();
    void initiallyMaximizedWindowPreservesNormalGeometry_data();
    void initiallyMaximizedWindowPreservesNormalGeometry();
    void preparationLeavesSystemAnimationPreferencesUntouched();
    void detachedWindowRemainsOpaqueAndKeepsItsContentWhileMoving();
    void publicSizeLimitsRemainEffective_data() { addWindowKinds(); }
    void publicSizeLimitsRemainEffective();
    void compactFirstShowAlreadyHasThePreparedNativeFrame_data() { addWindowKinds(); }
    void compactFirstShowAlreadyHasThePreparedNativeFrame();
    void presentationChangesRetainHandleCaptureAndCompositing_data();
    void presentationChangesRetainHandleCaptureAndCompositing();
    void cachedPreparationAndCompositionRefreshKeepTheNativeFrame_data() { addWindowKinds(); }
    void cachedPreparationAndCompositionRefreshKeepTheNativeFrame();
    void nativePaintMessagesCannotReplaceTheCustomClient_data();
    void nativePaintMessagesCannotReplaceTheCustomClient();
    void reorderedWholeWindowDragCannotRestoreTheStockCaption_data();
    void reorderedWholeWindowDragCannotRestoreTheStockCaption();
};

void NativeFrameTest::initTestCase()
{
    qApp->setQuitOnLastWindowClosed(false);
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
    QTest::newRow("margins-after-style") << false;
    QTest::newRow("production-margins-before-style") << true;
}
void NativeFrameTest::captionlessRegionKeepsCompositingAfterNativeShowAndRecalculation()
{
    // No Qt window or WindowSystem function participates. Initial full-caption
    // composition, followed by the captionless/full-client transition, matters:
    // creating a captionless HWND directly does not establish the same state.
    QFETCH(bool, marginsBeforeStyle);
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
    const DWORD style = DWORD(WS_OVERLAPPEDWINDOW) & ~DWORD(WS_CAPTION);
    const auto handle = CreateWindowExW(0, type.lpszClassName, L"Captionless", WS_OVERLAPPEDWINDOW, 100, 100, 600, 400, nullptr, nullptr, instance, nullptr);
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
    QCOMPARE(origin.x, outer.left);
    QCOMPARE(origin.y, outer.top);
    QCOMPARE(client.right - client.left, outer.right - outer.left);
    QCOMPARE(client.bottom - client.top, outer.bottom - outer.top);
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
    QCOMPARE(hints.ptMinTrackSize.x, LONG(std::ceil(650 * ratio)));
    QCOMPARE(hints.ptMinTrackSize.y, LONG(std::ceil(480 * ratio)));
    QCOMPARE(hints.ptMaxTrackSize.x, LONG(std::floor(900 * ratio)));
    QCOMPARE(hints.ptMaxTrackSize.y, LONG(std::floor(680 * ratio)));
    MONITORINFO monitor{sizeof(MONITORINFO)};
    QVERIFY(GetMonitorInfoW(MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST), &monitor));
    // Maximization is bounded by the monitor's physical work area as well as
    // the public size limit. Tracking limits remain independent and exact.
    const LONG maximumWidth = std::min(LONG(std::floor(900 * ratio)), monitor.rcWork.right - monitor.rcWork.left);
    const LONG maximumHeight = std::min(LONG(std::floor(680 * ratio)), monitor.rcWork.bottom - monitor.rcWork.top);
    QCOMPARE(hints.ptMaxSize.x, maximumWidth); QCOMPARE(hints.ptMaxSize.y, maximumHeight);
    const auto normal = window->geometry();
    window->showMaximized(); QTRY_VERIFY(window->isMaximized());
    RECT client{}; QVERIFY(GetClientRect(handle, &client));
    QCOMPARE(client.right - client.left, maximumWidth);
    QCOMPARE(client.bottom - client.top, maximumHeight);
    QCOMPARE(static_cast<WorkspaceWindow*>(window)->normalPlacementGeometry(), normal);
    window->showNormal(); QTRY_COMPARE(window->geometry(), normal);
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
        QCOMPARE(sample.style & DWORD(WS_OVERLAPPEDWINDOW), DWORD(WS_OVERLAPPEDWINDOW) & ~DWORD(WS_CAPTION));
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
    } else for (const int y : {2, window->height() - 3})
        for (const int x : {3, window->width() / 4, window->width() / 2, window->width() - 4}) {
            const QPoint point(x, y);
            if (mask.isEmpty() || mask.contains(point)) samples.append(point);
        }
    QCOMPARE(samples.size() >= (compact ? 4 : 3), true);
    QVector<COLORREF> beforePixels, afterPixels;
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
    destination->move(available.topLeft() + QPoint(12, 12)); manager.showWindow(target);
    carrier->move(available.topLeft() + QPoint(36, 36)); manager.showWindow(moving);
    QVERIFY(QTest::qWaitForWindowExposed(carrier));
    const auto original = carrier->geometry(); QVERIFY(available.contains(original));
    const auto originalStrips = carrier->saveStripLayout();
    const auto originalModel = manager.model().toJson();
    const auto handle = reinterpret_cast<HWND>(carrier->effectiveWinId());
    auto* title = carrier->findChild<QLabel*>("CaptionTitle"); QVERIFY(title && title->isVisible());
    const auto press = title->mapToGlobal(title->rect().center());
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
