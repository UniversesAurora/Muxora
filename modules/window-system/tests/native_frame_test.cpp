// SPDX-License-Identifier: MIT
#include "platform/windows/window_frame.hpp"
#include "chrome/workspace_window.hpp"

#include <window_system/window_manager.hpp>

#include <QApplication>
#include <QAbstractNativeEventFilter>
#include <QLabel>
#include <QImage>
#include <QPalette>
#include <QPointer>
#include <QScopeGuard>
#include <QVector>
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

void verifyNativeStyle(QWidget* window, bool expectedNonClientRendering = true)
{
    const auto handle = reinterpret_cast<HWND>(window->winId());
    QVERIFY(IsWindow(handle));
    const auto style = DWORD(GetWindowLongPtrW(handle, GWL_STYLE));
    QCOMPARE(style & DWORD(WS_OVERLAPPEDWINDOW), DWORD(WS_OVERLAPPEDWINDOW));
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
    // An independent Win32 fixture below establishes the native region rule:
    // shaped HWNDs cannot retain DWM NC rendering, even with a successful
    // enabled-policy write. Normal windows must still be truly composited.
    QCOMPARE(nonClientEnabled != FALSE, expectedNonClientRendering);
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

bool readScreenPixels(QWidget* window, const QVector<QPoint>& positions, QVector<COLORREF>& pixels)
{
    const auto dc = GetDC(nullptr);
    if (!dc) return false;
    const auto release = qScopeGuard([dc] { ReleaseDC(nullptr, dc); });
    const auto handle = reinterpret_cast<HWND>(window->effectiveWinId());
    const qreal ratio = window->devicePixelRatioF();
    pixels.clear();
    for (const auto logical : positions) {
        POINT native{qRound(logical.x() * ratio), qRound(logical.y() * ratio)};
        if (!ClientToScreen(handle, &native)) return false;
        if (GetAncestor(WindowFromPoint(native), GA_ROOT) != handle) return false;
        const auto color = GetPixel(dc, native.x, native.y);
        if (color == CLR_INVALID) return false;
        pixels.append(color);
    }
    return true;
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
    const auto normal = window->geometry();
    window->showMaximized(); QTRY_VERIFY(window->isMaximized());
    RECT client{}; QVERIFY(GetClientRect(handle, &client));
    QCOMPARE(client.right - client.left, qRound(900 * ratio));
    QCOMPARE(client.bottom - client.top, qRound(680 * ratio));
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
    verifyNativeStyle(window, !hasNativeRegion(handle));
    FirstShowProbe probe(handle);
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(window));
    QVERIFY(!probe.samples.isEmpty());
    for (const auto& sample : probe.samples) {
        QCOMPARE(sample.result, S_OK); QCOMPARE(sample.enabled, !sample.shaped);
        QCOMPARE(sample.style & DWORD(WS_OVERLAPPEDWINDOW), DWORD(WS_OVERLAPPEDWINDOW));
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
        verifyNativeStyle(window, false);
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
        for (int index = 0; index < 4; ++index) {
            applyNativeFrame(window);
            QCOMPARE(nativeFrameConfiguration(window).revision, before.revision);
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
    window->move(140, 140); manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(window));
    window->setDragPresentation(WorkspaceWindow::DragPresentation(presentation));
    window->raise(); window->repaint(); QCoreApplication::processEvents(); QCOMPARE(DwmFlush(), S_OK);
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
    // Exposed means visible, not that the compositor's creation transition has
    // completed. Wait for exact client pixels; keep system animations enabled.
    QTRY_VERIFY(readScreenPixels(window, samples, beforePixels) && beforePixels == expectedPixels);
    QCOMPARE(beforePixels, expectedPixels);
    window->grabMouse(Qt::ClosedHandCursor); QCOMPARE(GetCapture(), handle);
    const auto before = nativePaintGuardActivity(window);
    QCOMPARE(SendMessageW(handle, WM_NCPAINT, WPARAM(1), 0), LRESULT(0));
    QCOMPARE(nativePaintGuardActivity(window).paintsSuppressed, before.paintsSuppressed + (compact ? 1 : 0));
    QCOMPARE(SendMessageW(handle, WM_NCACTIVATE, FALSE, 0), LRESULT(TRUE));
    SendMessageW(handle, WM_NCACTIVATE, TRUE, 0);
    QCOMPARE(nativePaintGuardActivity(window).activationsSuppressed, before.activationsSuppressed + (compact ? 2 : 0));
    QCOMPARE(DwmFlush(), S_OK);
    QVERIFY(readScreenPixels(window, samples, afterPixels)); QCOMPARE(afterPixels, beforePixels);
    QCOMPARE(reinterpret_cast<HWND>(window->effectiveWinId()), handle); QCOMPARE(GetCapture(), handle);
    QCOMPARE(QWidget::mouseGrabber(), window); QCOMPARE(window->geometry(), geometry); QCOMPARE(window->mask(), mask);
    QCOMPARE(manager.paneWidget(pane), content);
    if (presentation == int(WorkspaceWindow::DragPresentation::Window)) {
        QVERIFY(mask.isEmpty()); QVERIFY(!hasNativeRegion(handle)); verifyNativeStyle(window);
    } else {
        QVERIFY(!mask.isEmpty()); QVERIFY(hasNativeRegion(handle)); verifyNativeStyle(window, false);
    }
    const auto configuration = nativeFrameConfiguration(window);
    QVERIFY(configuration.nonClientEnabled); QCOMPARE(configuration.nonClientResult, qint32(S_OK));
    window->releaseMouse();
}

QTEST_MAIN(NativeFrameTest)
#include "native_frame_test.moc"
