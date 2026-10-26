// SPDX-License-Identifier: MIT
#pragma once

// Real Windows input for tests that need native popup routing, capture and
// focus restoration. SendInput goes to whatever window has the foreground or
// lies under the pointer, which may belong to another application (an Escape
// would then cancel its work). Every helper therefore refuses, and fails the
// test, unless that window belongs to this test process.

#include <QMenu>
#include <QTest>
#include <QWidget>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace native_input {

#ifdef Q_OS_WIN
inline bool ownsWindow(HWND handle)
{
    DWORD process = 0;
    return handle && GetWindowThreadProcessId(handle, &process) && process == GetCurrentProcessId();
}

// Names a window that refused input by its class and program, so a failure
// says which application took the foreground or covered the point.
inline QByteArray describeWindow(HWND handle)
{
    if (!handle) return "no window";
    wchar_t name[256]{};
    GetClassNameW(handle, name, 256);
    QString program = QStringLiteral("?");
    DWORD process = 0;
    GetWindowThreadProcessId(handle, &process);
    if (HANDLE query = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process)) {
        wchar_t path[MAX_PATH]{};
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(query, 0, path, &size))
            program = QString::fromWCharArray(path, int(size)).section(u'\\', -1);
        CloseHandle(query);
    }
    return QStringLiteral("class %1 of %2 (process %3)").arg(QString::fromWCharArray(name), program).arg(process).toUtf8();
}
#endif

// Hold or release the real left button for native drag loops. A release is
// still allowed after an assertion failed, so cleanup cannot leave it held.
inline void leftButton(QWidget* target, bool pressed)
{
    if (pressed && QTest::currentTestFailed()) return;
#ifdef Q_OS_WIN
    Q_UNUSED(target);
    POINT point{}; QVERIFY(GetCursorPos(&point));
    const HWND recipient = GetCapture() ? GetCapture() : GetAncestor(WindowFromPoint(point), GA_ROOT);
    QVERIFY2(ownsWindow(recipient), (QByteArray("Refusing a button event outside the test process: ")
        + describeWindow(recipient)).constData());
    INPUT event{}; event.type = INPUT_MOUSE;
    event.mi.dwFlags = pressed ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    QCOMPARE(SendInput(1, &event, sizeof(INPUT)), UINT(1));
#else
    if (pressed) QTest::mousePress(target, Qt::LeftButton);
    else QTest::mouseRelease(target, Qt::LeftButton);
#endif
}

// Moves the real pointer to a point of the target and optionally clicks it.
inline void mouse(QWidget* target, QPoint local, bool click = false)
{
    if (QTest::currentTestFailed()) return;
#ifdef Q_OS_WIN
    auto* window = target->window();
    const QPoint client = target->mapTo(window, local);
    const qreal ratio = window->devicePixelRatioF();
    POINT point{qRound(client.x() * ratio), qRound(client.y() * ratio)};
    QVERIFY(ClientToScreen(reinterpret_cast<HWND>(window->winId()), &point));
    QVERIFY(SetCursorPos(point.x, point.y));
    if (click) {
        const HWND under = GetAncestor(WindowFromPoint(point), GA_ROOT);
        QVERIFY2(ownsWindow(under), (QByteArray("Another application's window lies under the click point; refusing to click it: ")
            + describeWindow(under)).constData());
        INPUT events[2]{};
        events[0].type = events[1].type = INPUT_MOUSE;
        events[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        events[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
        QCOMPARE(SendInput(2, events, sizeof(INPUT)), UINT(2));
    }
#else
    if (click) QTest::mouseClick(target, Qt::LeftButton, Qt::NoModifier, local);
    else QTest::mouseMove(target, local);
#endif
}

// Presses and releases Escape, Tab or Space in the foreground window.
inline void key(QWidget* target, Qt::Key key)
{
    if (QTest::currentTestFailed()) return;
#ifdef Q_OS_WIN
    Q_UNUSED(target);
    const HWND foreground = GetForegroundWindow();
    QVERIFY2(ownsWindow(foreground), (QByteArray("Another application has the foreground; refusing to send it a key: ")
        + describeWindow(foreground)).constData());
    const WORD virtualKey = key == Qt::Key_Escape ? VK_ESCAPE : key == Qt::Key_Tab ? VK_TAB : VK_SPACE;
    INPUT events[2]{};
    events[0].type = events[1].type = INPUT_KEYBOARD;
    events[0].ki.wVk = events[1].ki.wVk = virtualKey;
    events[1].ki.dwFlags = KEYEVENTF_KEYUP;
    QCOMPARE(SendInput(2, events, sizeof(INPUT)), UINT(2));
#else
    QTest::keyClick(target, key);
#endif
}

} // namespace native_input
