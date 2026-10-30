// SPDX-License-Identifier: MIT
// Quick/native-window contract (Quick rendering design sections 1-2): actual
// caption endpoints, native capture, surface recreation and per-window menus.
// Dimensions: caption phase x pointer inside/outside x callback destruction;
// native surface alive/destroyed/recreated x independent window ownership.
// No appearance or screenshot assertions.
#include "platform/windows/window_frame.hpp"
#include "platform/windows/window_menu.hpp"

#include <QGuiApplication>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTest>

#include <memory>
#include <qt_windows.h>
#include <windowsx.h>

using namespace window_system;

namespace {

class NativeWindow final : public QQuickWindow {
public:
    NativeWindow()
    {
        setTitle(QStringLiteral("Quick native frame test"));
        setFlags(Qt::Window | Qt::WindowMinMaxButtonsHint | Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint);
        resize(420, 260);
        setPersistentGraphics(false);
        setPersistentSceneGraph(false);
    }
protected:
    bool nativeEvent(const QByteArray& type, void* message, qintptr* result) override
    {
        if (handleNativeFrame(this, message, result)) return true;
        return QQuickWindow::nativeEvent(type, message, result);
    }
};

class CaptionEndpoint final : public QQuickItem {
public:
    explicit CaptionEndpoint(QQuickItem* parent) : QQuickItem(parent)
    {
        setPosition({24, 24});
        setSize({80, 32});
    }
    bool hovered = false;
    bool pressed = false;
    int activations = 0;
};

NativeFrameClient clientFor(CaptionEndpoint* control)
{
    const QPointer<CaptionEndpoint> endpoint(control);
    NativeFrameClient client;
    client.captionButtonAt = [endpoint](QPointF position) -> QObject* {
        return endpoint && endpoint->isVisible() && endpoint->isEnabled()
            && endpoint->contains(endpoint->mapFromScene(position)) ? endpoint.data() : nullptr;
    };
    client.setCaptionHovered = [endpoint](QObject* target, bool hovered, QPointF) {
        QCOMPARE(target, endpoint.data());
        endpoint->hovered = hovered;
    };
    client.setCaptionPressed = [endpoint](QObject* target, bool pressed) {
        QCOMPARE(target, endpoint.data());
        endpoint->pressed = pressed;
    };
    client.activateCaption = [endpoint](QObject* target) {
        QCOMPARE(target, endpoint.data());
        ++endpoint->activations;
    };
    return client;
}

LPARAM clientPosition(const QWindow& window, QPointF position)
{
    return MAKELPARAM(qRound(position.x() * window.devicePixelRatio()),
                      qRound(position.y() * window.devicePixelRatio()));
}

LPARAM screenPosition(const QWindow& window, QPointF position)
{
    POINT point{qRound(position.x() * window.devicePixelRatio()),
                qRound(position.y() * window.devicePixelRatio())};
    if (!ClientToScreen(reinterpret_cast<HWND>(window.winId()), &point)) return 0;
    return MAKELPARAM(point.x, point.y);
}

} // namespace

class QuickNativeFrameTest final : public QObject {
    Q_OBJECT
private slots:
    void captionEndpointsOwnNativeInputAndReleaseCapture()
    {
        NativeWindow window;
        auto* endpoint = new CaptionEndpoint(window.contentItem());
        setNativeFrameClient(&window, clientFor(endpoint));
        applyNativeFrame(&window);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto handle = reinterpret_cast<HWND>(window.winId());
        const auto inside = endpoint->mapToScene(endpoint->boundingRect().center());
        const QPointF outside(window.width() / 2.0, window.height() / 2.0);
        const auto insideScreen = screenPosition(window, inside);
        QCOMPARE(SendMessageW(handle, WM_NCHITTEST, 0, insideScreen), LRESULT(HTMAXBUTTON));
        SendMessageW(handle, WM_NCMOUSEMOVE, HTMAXBUTTON, insideScreen);
        QVERIFY(endpoint->hovered);
        SendMessageW(handle, WM_NCLBUTTONDOWN, HTMAXBUTTON, insideScreen);
        QVERIFY(endpoint->pressed);
        QCOMPARE(GetCapture(), handle);
        SendMessageW(handle, WM_MOUSEMOVE, MK_LBUTTON, clientPosition(window, outside));
        QVERIFY(!endpoint->pressed);
        SendMessageW(handle, WM_LBUTTONUP, 0, clientPosition(window, outside));
        QCOMPARE(endpoint->activations, 0);
        QVERIFY(GetCapture() != handle);

        SendMessageW(handle, WM_NCLBUTTONDOWN, HTMAXBUTTON, insideScreen);
        SendMessageW(handle, WM_LBUTTONUP, 0, clientPosition(window, inside));
        QCOMPARE(endpoint->activations, 1);
        QVERIFY(!endpoint->pressed);
        QVERIFY(GetCapture() != handle);
        SendMessageW(handle, WM_NCMOUSELEAVE, 0, 0);
        QVERIFY(!endpoint->hovered);
        endpoint->setEnabled(false);
        QCOMPARE(SendMessageW(handle, WM_NCHITTEST, 0, insideScreen), LRESULT(HTCLIENT));
        endpoint->setEnabled(true);
        SendMessageW(handle, WM_NCLBUTTONDOWN, HTMAXBUTTON, insideScreen);
        QCOMPARE(GetCapture(), handle);
        delete endpoint;
        QVERIFY(GetCapture() != handle);
    }

    void surfacesRecreateWithoutTransferringMenuOwnership()
    {
        auto first = std::make_unique<NativeWindow>();
        NativeWindow second;
        auto* endpoint = new CaptionEndpoint(first->contentItem());
        setNativeFrameClient(first.get(), clientFor(endpoint));
        applyNativeFrame(first.get());
        applyNativeFrame(&second);
        first->show();
        second.show();
        QVERIFY(QTest::qWaitForWindowExposed(first.get()));
        QVERIFY(QTest::qWaitForWindowExposed(&second));
        const auto geometry = first->geometry();
        const auto firstMenu = nativeSystemMenuHandle(first.get());
        const auto secondMenu = nativeSystemMenuHandle(&second);
        QVERIFY(firstMenu && secondMenu && firstMenu != secondMenu);
        QVERIFY(IsMenu(reinterpret_cast<HMENU>(firstMenu)));
        QVERIFY(hasNormalNativePlacement(first.get()));

        const auto handle = reinterpret_cast<HWND>(first->winId());
        const auto inside = endpoint->mapToScene(endpoint->boundingRect().center());
        SendMessageW(handle, WM_NCLBUTTONDOWN, HTMAXBUTTON, screenPosition(*first, inside));
        QVERIFY(endpoint->pressed);
        first->hide();
        QTRY_VERIFY(!first->isExposed());
        first->destroy();
        QVERIFY(!first->handle());
        QVERIFY(!endpoint->pressed);
        QVERIFY(GetCapture() != handle);
        QVERIFY(!nativeFrameConfiguration(first.get()).attempted);
        QCOMPARE(nativeSystemMenuHandle(first.get()), quintptr(0));
        QVERIFY(IsMenu(reinterpret_cast<HMENU>(firstMenu)));
        QVERIFY(IsMenu(reinterpret_cast<HMENU>(secondMenu)));

        first->create();
        applyNativeFrame(first.get());
        first->show();
        QVERIFY(QTest::qWaitForWindowExposed(first.get()));
        QVERIFY(nativeFrameConfiguration(first.get()).attempted);
        QVERIFY(hasNormalNativePlacement(first.get()));
        QCOMPARE(first->geometry(), geometry);
        QCOMPARE(nativeSystemMenuHandle(first.get()), firstMenu);
        QCOMPARE(nativeSystemMenuHandle(&second), secondMenu);
        first.reset();
        QVERIFY(!IsMenu(reinterpret_cast<HMENU>(firstMenu)));
        QVERIFY(IsMenu(reinterpret_cast<HMENU>(secondMenu)));
    }

    void callbackDestructionStopsNativeDispatch_data()
    {
        QTest::addColumn<int>("phase");
        QTest::newRow("hit") << 0;
        QTest::newRow("hover") << 1;
        QTest::newRow("press") << 2;
        QTest::newRow("activate") << 3;
    }

    void callbackDestructionStopsNativeDispatch()
    {
        QFETCH(int, phase);
        QPointer<NativeWindow> window(new NativeWindow);
        const auto cleanup = qScopeGuard([&] { delete window.data(); });
        QPointer<CaptionEndpoint> endpoint(new CaptionEndpoint(window->contentItem()));
        auto client = clientFor(endpoint);
        // Install the destructive endpoint only after exposure: incidental
        // activation and pointer messages during creation are not the action.
        setNativeFrameClient(window, client);
        applyNativeFrame(window);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        const auto handle = reinterpret_cast<HWND>(window->winId());
        const auto inside = endpoint->mapToScene(endpoint->boundingRect().center());
        const auto point = screenPosition(*window, inside);
        QSignalSpy destroyed(window, &QObject::destroyed);
        if (phase == 0)
            client.captionButtonAt = [&](QPointF) -> QObject* { delete window.data(); return nullptr; };
        else if (phase == 1)
            client.setCaptionHovered = [&](QObject*, bool hover, QPointF) { if (hover) delete window.data(); };
        else if (phase == 2)
            client.setCaptionPressed = [&](QObject*, bool press) { if (press) delete window.data(); };
        else client.activateCaption = [&](QObject*) { delete window.data(); };
        setNativeFrameClient(window, std::move(client));
        if (phase == 0) SendMessageW(handle, WM_NCHITTEST, 0, point);
        else if (phase == 1) SendMessageW(handle, WM_NCMOUSEMOVE, HTMAXBUTTON, point);
        else {
            SendMessageW(handle, WM_NCLBUTTONDOWN, HTMAXBUTTON, point);
            if (phase == 3) SendMessageW(handle, WM_NCLBUTTONUP, HTMAXBUTTON, point);
        }
        QVERIFY(window.isNull());
        QVERIFY(endpoint.isNull());
        QCOMPARE(destroyed.count(), 1);
        QVERIFY(GetCapture() != handle);
    }
};

QTEST_MAIN(QuickNativeFrameTest)
#include "quick_native_frame_test.moc"
