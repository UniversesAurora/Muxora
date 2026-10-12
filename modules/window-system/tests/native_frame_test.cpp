// SPDX-License-Identifier: MIT
#include "platform/windows/window_frame.hpp"
#include "chrome/workspace_window.hpp"

#include <window_system/window_manager.hpp>

#include <QApplication>
#include <QLabel>
#include <QPointer>
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

void verifyNativeStyle(QWidget* window)
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

} // namespace

// Windows-only target. These contracts verify the native compositor's eligible
// frame and real state transitions, not the temporal pixels of DWM's animation.
// Visual animation and accessibility-setting acceptance remain desktop checks.
class NativeFrameTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
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
};

void NativeFrameTest::initTestCase()
{
    qApp->setQuitOnLastWindowClosed(false);
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

QTEST_MAIN(NativeFrameTest)
#include "native_frame_test.moc"
