// SPDX-License-Identifier: MIT
#include "chrome/command_strip.hpp"
#include "commands/popup_menu.hpp"

#include <window_system/theme.hpp>

#include <QAction>
#include <QApplication>
#include <QImage>
#include <QLabel>
#include <QMenuBar>
#include <QMouseEvent>
#include <QMenu>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <memory>
#include <QToolButton>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

using namespace window_system;

namespace {

QList<QAction*> commands(QWidget* owner)
{
    QList<QAction*> result;
    for (int index = 0; index < 6; ++index) {
        auto* command = new QAction(iconFor("settings"), QStringLiteral("Command %1").arg(index), owner);
        command->setObjectName(QStringLiteral("command.%1").arg(index));
        result.append(command);
    }
    return result;
}

// Use the target window's HWND and scale. Direct QWidget notification bypasses
// native popup routing and does not reproduce focus restoration on dismissal.
void nativeMouse(QWidget* target, QPoint local, bool click = false)
{
#ifdef Q_OS_WIN
    auto* window = target->window();
    const QPoint client = target->mapTo(window, local);
    const qreal ratio = window->devicePixelRatioF();
    POINT point{qRound(client.x() * ratio), qRound(client.y() * ratio)};
    QVERIFY(ClientToScreen(reinterpret_cast<HWND>(window->winId()), &point));
    QVERIFY(SetCursorPos(point.x, point.y));
    if (click) {
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

void nativeKey(QWidget* target, Qt::Key key)
{
#ifdef Q_OS_WIN
    Q_UNUSED(target);
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

int pixelsOfColor(const QImage& image, QColor color)
{
    int count = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (image.pixelColor(x, y).rgb() == color.rgb()) ++count;
    return count;
}

} // namespace

class ToolbarPopupTest final : public QObject {
    Q_OBJECT
private slots:
    void mouseOpenedMenuDoesNotLeaveAnOutline_data();
    void mouseOpenedMenuDoesNotLeaveAnOutline();
    void keyboardFocusRemainsVisibleAfterMenuDismissal();
    void clippedMenusDoNotOpenFromThePointer();
    void clippedMenusOpenBesideTheOverflowMenu();
};

namespace {
// A compressed menu strip: the last menus are clipped and listed in the
// strip's overflow menu instead.
struct CompressedMenuStrip {
    CommandStripHost host;
    QMenuBar* bar = new QMenuBar;
    QList<QMenu*> menus;
    CompressedMenuStrip()
    {
        applyTheme(&host, defaultTheme());
        bar->setNativeMenuBar(false);
        for (int index = 0; index < 6; ++index) {
            auto* menu = new DismissOnlyMenu(QStringLiteral("Menu %1").arg(index), bar);
            menu->addAction(QStringLiteral("Item %1").arg(index));
            bar->addMenu(menu); menus.append(menu);
        }
        host.addStrip("menu", "Menu", bar, 0);
        host.addStrip("title", "Title", new QLabel("Title"), 0, CommandStripHost::StripSizing::Elastic);
    }
    bool show()
    {
        if (!host.setStripWidth("menu", 150)) return false;
        host.move(160, 160); host.resize(600, host.height()); host.show();
        if (!QTest::qWaitForWindowExposed(&host)) return false;
        host.raise(); host.activateWindow();
        return QTest::qWaitForWindowActive(&host) && !host.overflowItems("menu").isEmpty();
    }
    bool visible(QAction* action) const { return bar->visibleRegion().contains(bar->actionGeometry(action).center()); }
    void pointerTo(QAction* action) const
    {
        const QPointF local(bar->actionGeometry(action).center());
        QMouseEvent move(QEvent::MouseMove, local, bar->mapToGlobal(local), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(bar, &move);
    }
};
} // namespace

void ToolbarPopupTest::mouseOpenedMenuDoesNotLeaveAnOutline_data()
{
    QTest::addColumn<QString>("dismissal");
    QTest::newRow("outside") << QStringLiteral("outside");
    QTest::newRow("escape") << QStringLiteral("escape");
    QTest::newRow("toggle") << QStringLiteral("toggle");
    QTest::newRow("command") << QStringLiteral("command");
}

void ToolbarPopupTest::mouseOpenedMenuDoesNotLeaveAnOutline()
{
    QFETCH(QString, dismissal);
    const auto theme = defaultTheme();
    CommandStripHost host;
    host.setWindowFlag(Qt::WindowStaysOnTopHint);
    host.move(160, 160);
    applyTheme(&host, theme);
    host.addActionStrip("tools", "Tools", commands(&host), 0);
    QVERIFY(host.setStripWidth("tools", 70));
    host.resize(500, host.height()); host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    host.raise(); host.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&host));
    auto* tail = host.stripWidget("tools")->findChild<QToolButton*>("StripOverflowButton");
    QVERIFY(tail); QVERIFY(tail->menu());
    host.setFocus(Qt::OtherFocusReason);
    const QPoint blank(host.width() - 25, host.height() / 2);
    nativeMouse(&host, blank);
    QTRY_VERIFY(!tail->underMouse());
    QVERIFY(!tail->hasFocus());
    const auto baseline = tail->grab().toImage();
    QCOMPARE(pixelsOfColor(baseline, theme.focus), 0);
    QCOMPARE(pixelsOfColor(baseline, theme.accent), 0);

    QSignalSpy triggered(tail->menu(), &QMenu::triggered);
    QSignalSpy shown(tail->menu(), &QMenu::aboutToShow);
    bool visibleBeforeClose = false;
    bool watchdogFired = false;
    QTimer::singleShot(100, &host, [&] {
        visibleBeforeClose = tail->menu()->isVisible();
        if (dismissal == "toggle") nativeMouse(tail, tail->rect().center(), true);
        else if (dismissal == "command") {
            nativeMouse(tail->menu(), tail->menu()->actionGeometry(tail->menu()->actions().first()).center(), true);
        } else if (dismissal == "escape") {
            nativeMouse(&host, blank);
            nativeKey(tail->menu(), Qt::Key_Escape);
        } else nativeMouse(&host, blank, true);
    });
    QTimer::singleShot(900, &host, [&] {
        if (tail->menu()->isVisible()) { watchdogFired = true; tail->menu()->close(); }
    });
    nativeMouse(tail, tail->rect().center(), true);
    QTRY_VERIFY(visibleBeforeClose);
    QTRY_VERIFY(!tail->menu()->isVisible());
    QVERIFY(!watchdogFired);
    QCOMPARE(triggered.count(), dismissal == "command" ? 1 : 0);
    QVERIFY(!QApplication::activePopupWidget());
    QVERIFY(!QWidget::mouseGrabber());
    QTRY_COMPARE(QApplication::mouseButtons(), Qt::MouseButtons(Qt::NoButton));
    QVERIFY(!tail->isDown());

    // Moving away is sufficient: no extra click or focus reassignment is used
    // to clear the outline left by the mouse-opened menu.
    nativeMouse(&host, blank);
    QTRY_VERIFY(!tail->underMouse());
    QTRY_COMPARE(tail->grab().toImage(), baseline);
    QCOMPARE(shown.count(), 1);
    QVERIFY(!tail->menu()->isVisible());
}

void ToolbarPopupTest::keyboardFocusRemainsVisibleAfterMenuDismissal()
{
    const auto theme = defaultTheme();
    CommandStripHost host;
    host.setWindowFlag(Qt::WindowStaysOnTopHint);
    host.move(160, 160);
    applyTheme(&host, theme);
    host.addActionStrip("tools", "Tools", commands(&host), 0);
    host.resize(500, host.height()); host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    host.raise(); host.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&host));
    auto* tail = host.stripWidget("tools")->findChild<QToolButton*>("StripOverflowButton");
    QVERIFY(tail); QVERIFY(tail->menu());
    host.setFocus(Qt::OtherFocusReason);
    nativeMouse(&host, QPoint(host.width() - 25, host.height() / 2));
    QTRY_VERIFY(!tail->underMouse());
    // Reach the button using the actual Tab chain rather than forcing its
    // focus, so removing keyboard access cannot satisfy the visual regression.
    for (int index = 0; index < 12 && !tail->hasFocus(); ++index) {
        auto* previous = QApplication::focusWidget();
        nativeKey(&host, Qt::Key_Tab);
        QTRY_VERIFY(QApplication::focusWidget() != previous);
    }
    QVERIFY(tail->hasFocus());
    const auto focused = tail->grab().toImage();
    QVERIFY(pixelsOfColor(focused, theme.focus) > 0);

    bool visibleBeforeClose = false;
    bool watchdogFired = false;
    QTimer::singleShot(100, &host, [&] {
        visibleBeforeClose = tail->menu()->isVisible();
        nativeKey(tail->menu(), Qt::Key_Escape);
    });
    QTimer::singleShot(900, &host, [&] {
        if (tail->menu()->isVisible()) { watchdogFired = true; tail->menu()->close(); }
    });
    nativeKey(tail, Qt::Key_Space);
    QTRY_VERIFY(visibleBeforeClose);
    QTRY_VERIFY(!tail->menu()->isVisible());
    QVERIFY(!watchdogFired);
    QTRY_VERIFY(tail->hasFocus());
    QVERIFY(!tail->underMouse());
    QVERIFY(!tail->isDown());
    QVERIFY(!QApplication::activePopupWidget());
    QTRY_COMPARE(tail->grab().toImage(), focused);
}

void ToolbarPopupTest::clippedMenusDoNotOpenFromThePointer()
{
    CompressedMenuStrip strip; QVERIFY(strip.show());
    const auto actions = strip.bar->actions();
    QVERIFY(strip.visible(actions[0]) && strip.visible(actions[1]));
    QVERIFY(!strip.visible(actions.last()));
    // With a menu open, Qt forwards pointer moves to the bar: moving over the
    // clipped part must not open the menu hidden there...
    strip.bar->setActiveAction(actions[0]);
    QTRY_VERIFY(strip.menus[0]->isVisible());
    strip.pointerTo(actions.last());
    QCoreApplication::processEvents();
    QVERIFY(!strip.menus.last()->isVisible());
    QCOMPARE(strip.bar->activeAction(), actions[0]);
    QVERIFY(strip.menus[0]->isVisible());
    // ...while a visible neighbour still takes over as usual.
    strip.pointerTo(actions[1]);
    QTRY_VERIFY(strip.menus[1]->isVisible());
    strip.menus[1]->close();
    // Keyboard navigation can still reach a clipped item; it then opens at
    // the visible end of the bar instead of out of sight.
    strip.bar->setActiveAction(actions.last());
    QTRY_VERIFY(strip.menus.last()->isVisible());
    const QRect visible = strip.bar->visibleRegion().boundingRect();
    const int visibleRight = strip.bar->mapToGlobal(visible.topRight()).x();
    QVERIFY2(strip.menus.last()->x() <= visibleRight,
        qPrintable(QStringLiteral("menu x %1, visible bar ends at %2").arg(strip.menus.last()->x()).arg(visibleRight)));
    strip.menus.last()->close();
}

void ToolbarPopupTest::clippedMenusOpenBesideTheOverflowMenu()
{
    CompressedMenuStrip strip; QVERIFY(strip.show());
    auto* hidden = strip.bar->actions().last();
    QVERIFY(!strip.visible(hidden));
    std::unique_ptr<QMenu> overflow(strip.host.createOverflowMenu("menu", &strip.host)); QVERIFY(overflow);
    QVERIFY(overflow->actions().contains(hidden));
    overflow->popup(strip.host.mapToGlobal(QPoint(300, strip.host.height())));
    QTRY_VERIFY(overflow->isVisible());
    overflow->setActiveAction(hidden);
    QTest::keyClick(overflow.get(), Qt::Key_Right);
    auto* submenu = strip.menus.last();
    QTRY_VERIFY(submenu->isVisible());
    // Beside the overflow menu at the item's row, like any submenu, not under
    // the clipped position in the menu bar.
    const QRect outer = overflow->frameGeometry();
    const QRect sub = submenu->frameGeometry();
    QVERIFY2(sub.left() == outer.right() + 1 || sub.right() + 1 == outer.left(),
        qPrintable(QStringLiteral("submenu %1,%2 overflow %3..%4").arg(sub.left()).arg(sub.top()).arg(outer.left()).arg(outer.right())));
    QCOMPARE(sub.top(), overflow->mapToGlobal(overflow->actionGeometry(hidden).topLeft()).y());
    submenu->close(); overflow->close();
}

QTEST_MAIN(ToolbarPopupTest)
#include "toolbar_popup_test.moc"
