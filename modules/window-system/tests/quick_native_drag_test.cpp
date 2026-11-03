// SPDX-License-Identifier: MIT
// Phase 10.3: real held input drives the Quick drag through Windows' modal
// move loop, which authored events cannot exercise. A torn-out tab's window
// and a floating window dragged by its title both move in the system's loop,
// keep the pointer where it took them, and drop into a strip.
#include "native_input.hpp"
#include "quick/dock_surface_item.hpp"
#include "quick/group_item.hpp"
#include "quick/tab_bar_item.hpp"
#include "quick/window_chrome_item.hpp"
#include <window_system/quick_workspace.hpp>
#include <window_system/window_manager.hpp>

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QScreen>
#include <QStyleHints>
#include <QTimer>
#include <functional>

using namespace window_system;

namespace {

TabOptions named(const QString& title) { return {title, QStringLiteral("test"), {}}; }

bool inSystemMove()
{
    GUITHREADINFO info{}; info.cbSize = sizeof(info);
    return GetGUIThreadInfo(GetCurrentThreadId(), &info) && (info.flags & GUI_INMOVESIZE);
}

// A tab's label center, in global coordinates.
QPoint tabPoint(quick::GroupItem* group, TabId tab)
{
    auto* bar = group->tabBar();
    const auto& strip = bar->controller();
    const auto label = strip.tabLayout().tabs.at(strip.indexOf(tab)).translated(strip.tabOrigin());
    return bar->mapToGlobal(QPointF(label.center())).toPoint();
}

// The title row's middle, where a torn-out window is held.
QPoint titlePoint(const QQuickWindow* window)
{
    return window->mapToGlobal(QPoint(window->width() / 2, quick::WindowChromeItem::rowHeight() / 2));
}

// Each step acts once its condition holds. The steps run from a timer,
// which Windows keeps firing inside its move loop.
struct Step {
    QString name;
    std::function<bool()> ready;
    std::function<void()> act;
};

QString drive(QWindow* host, const QVector<Step>& steps)
{
    int index = 0;
    QString error;
    bool held = false;
    QTimer timer;
    timer.setInterval(10);
    QElapsedTimer clock; clock.start();
    const auto release = [&] { if (std::exchange(held, false)) native_input::leftButton(host, false); };
    const auto cleanup = qScopeGuard(release);
    QObject::connect(&timer, &QTimer::timeout, [&] {
        if (index == steps.size() || !error.isEmpty()) return;
        if (clock.elapsed() > 6000) { error = QStringLiteral("Timed out before \"%1\"").arg(steps.at(index).name); release(); return; }
        if (!steps.at(index).ready()) return;
        steps.at(index).act();
        if (index == 0) held = true;
        if (++index == steps.size()) held = false;
    });
    timer.start();
    (void)QTest::qWaitFor([&] { return index == steps.size() || !error.isEmpty(); }, 8000);
    if (index < steps.size() && error.isEmpty()) error = QStringLiteral("Stopped before \"%1\"").arg(steps.at(index).name);
    return error;
}

void pointer(QQuickWindow* window, QPoint global) { native_input::mouse(window, window->mapFromGlobal(global)); }

} // namespace

class QuickNativeDragTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); }

    void carriedWindowsMoveInTheSystemLoopAndDropIntoStrips()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto available = qApp->primaryScreen()->availableGeometry();
        const auto window = manager.createWindow(WindowKind::Workbench,
            {QStringLiteral("Native drag"), QSize(std::min(900, available.width() - 24), std::min(560, available.height() - 24))});
        const auto torn = manager.addTab(window, named(QStringLiteral("Torn")));
        const auto floated = manager.addTab(window, named(QStringLiteral("Floated")));
        const auto kept = manager.addTab(window, named(QStringLiteral("Kept")));
        const auto left = manager.model().groupForTab(torn);
        QVERIFY(manager.moveTab(kept, left, Placement::Right));
        const auto right = manager.model().groupForTab(kept);
        QTRY_VERIFY(workspace.window(window));
        auto* host = workspace.window(window);
        host->setPosition(available.topLeft() + QPoint(12, 12));
        host->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(host));
        auto* dock = host->contentItem()->findChild<quick::DockSurfaceItem*>();
        QTRY_VERIFY(dock->group(left) && dock->group(right) && dock->group(left)->count() == 2);
        const auto windowOf = [&](TabId tab) { return manager.model().group(manager.model().groupForTab(tab))->windowId; };

        // Torn out of its strip, the tab's window moves in the system's loop
        // under the pointer; over the other strip the gap opens, and the tab
        // drops there.
        const auto start = tabPoint(dock->group(left), torn);
        const auto below = start + QPoint(40, 200);
        QQuickWindow* carrier = nullptr;
        auto error = drive(host, {
            {"press", [] { return true; }, [&] { pointer(host, start); native_input::leftButton(host, true); }},
            {"lift", [] { return QGuiApplication::mouseButtons().testFlag(Qt::LeftButton); },
             [&] { pointer(host, start + QPoint(QGuiApplication::styleHints()->startDragDistance() + 4, 0)); }},
            {"tear out", [] { return true; }, [&] { pointer(host, below); }},
            {"system loop", [&] { return windowOf(torn) != window && inSystemMove(); },
             [&] { carrier = workspace.window(windowOf(torn)); pointer(host, below + QPoint(60, 30)); }},
            {"follow", [&] { return carrier && (titlePoint(carrier) - QCursor::pos()).manhattanLength() <= 2; },
             [&] { pointer(host, tabPoint(dock->group(right), kept) + QPoint(30, 0)); }},
            {"gap", [&] { return !dock->group(right)->insertionPreviewRectGlobal().isEmpty(); },
             [&] { native_input::leftButton(host, false); }},
        });
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QTRY_COMPARE(manager.model().group(right)->tabs, (QVector<TabId>{kept, torn}));
        QTRY_COMPARE(manager.model().windowOrder().size(), 1);
        QVERIFY(!inSystemMove());

        // A floating window dragged by its title does the same with its tabs.
        const auto floating = manager.detachTab(floated, WindowKind::Floating);
        QTRY_VERIFY(workspace.window(floating));
        auto* carried = workspace.window(floating);
        carried->setPosition(host->position() + QPoint(80, 220));
        QVERIFY(QTest::qWaitForWindowExposed(carried));
        QTRY_VERIFY(dock->group(right) && dock->group(right)->count() == 2);
        const auto grip = titlePoint(carried) + QPoint(-40, 0);
        error = drive(carried, {
            {"press", [] { return true; }, [&] { pointer(carried, grip); native_input::leftButton(carried, true); }},
            {"carry", [] { return QGuiApplication::mouseButtons().testFlag(Qt::LeftButton); },
             [&] { pointer(carried, grip + QPoint(0, 40)); }},
            {"system loop", [] { return inSystemMove(); },
             [&] { pointer(host, tabPoint(dock->group(right), torn) + QPoint(30, 0)); }},
            {"gap", [&] { return !dock->group(right)->insertionPreviewRectGlobal().isEmpty(); },
             [&] { native_input::leftButton(carried, false); }},
        });
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QTRY_COMPARE(manager.model().group(right)->tabs, (QVector<TabId>{kept, torn, floated}));
        QTRY_VERIFY(!manager.model().window(floating));
        QVERIFY(!inSystemMove());
    }
};

QTEST_MAIN(QuickNativeDragTest)
#include "quick_native_drag_test.moc"
