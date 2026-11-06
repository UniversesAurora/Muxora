// SPDX-License-Identifier: MIT
// Phase 10.3: tabs dragged in Quick windows. A tab moves within its strip,
// to another group or window, out into a floating window and onto a rail; a
// folder tears out with its tabs, and a floating window joins a strip by its
// title. Split from quick_workspace_test.cpp to keep each suite in its limit.
#include "docking/drop_target.hpp"
#include "quick/dock_guide_item.hpp"
#include "quick/dock_surface_item.hpp"
#include "quick/group_item.hpp"
#include "quick/picture_item.hpp"
#include "quick/rail_button_item.hpp"
#include "quick/rail_item.hpp"
#include "quick/tab_bar_item.hpp"
#include "quick/window_chrome_item.hpp"
#include "platform/window_order.hpp"
#include "style/frame_motion.hpp"
#include <window_system/quick_workspace.hpp>
#include <window_system/window_manager.hpp>

#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTest>
#include <QWheelEvent>
#include <algorithm>

using namespace window_system;

namespace {

TabOptions named(const QString& title) { return {title, QStringLiteral("test"), {}}; }

// A tab's label center, in window coordinates.
QPoint tabPoint(quick::GroupItem* group, TabId tab)
{
    auto* bar = group->tabBar();
    const auto& strip = bar->controller();
    const auto label = strip.tabLayout().tabs.at(strip.indexOf(tab)).translated(strip.tabOrigin());
    return bar->mapToScene(QPointF(label.center())).toPoint();
}

// Moves the held mouse in steps, as a hand does.
void moveHeld(QWindow* window, QPoint from, QPoint to)
{
    for (int step = 1; step <= 8; ++step) QTest::mouseMove(window, from + (to - from) * step / 8);
}

} // namespace

class QuickWorkspaceDragTest final : public QObject {
    Q_OBJECT
private slots:
    void tabsDragWithinAndAcrossGroups()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto window = manager.createWindow(WindowKind::Workbench, {QStringLiteral("Drag"), QSize(900, 560)});
        const auto a = manager.addTab(window, named(QStringLiteral("Alpha")));
        const auto b = manager.addTab(window, named(QStringLiteral("Beta")));
        const auto c = manager.addTab(window, named(QStringLiteral("Gamma")));
        const auto first = manager.model().groupForTab(a);
        QTRY_VERIFY(workspace.window(window));
        auto* quickWindow = workspace.window(window);
        QVERIFY(QTest::qWaitForWindowExposed(quickWindow));
        auto* dock = quickWindow->contentItem()->findChild<quick::DockSurfaceItem*>();
        QVERIFY(dock);
        const auto tabs = [&](GroupId group) { return manager.model().group(group)->tabs; };

        // Within its strip the tab is lifted and dropped where the gap is.
        QTRY_VERIFY(dock->group(first) && dock->group(first)->count() == 3);
        auto from = tabPoint(dock->group(first), a);
        auto to = tabPoint(dock->group(first), c) + QPoint(20, 0);
        QTest::mousePress(quickWindow, Qt::LeftButton, {}, from);
        moveHeld(quickWindow, from, to);
        QTest::mouseRelease(quickWindow, Qt::LeftButton, {}, to);
        QTRY_COMPARE(tabs(first), (QVector<TabId>{b, c, a}));

        // Onto another group's strip: the tab joins it there.
        QVERIFY(manager.moveTab(c, first, Placement::Right));
        const auto second = manager.model().groupForTab(c);
        QVERIFY(second != first);
        QTRY_VERIFY(dock->group(second) && dock->group(second)->count() == 1 && dock->group(first)->count() == 2);
        // The strip paints the carried label at the pointer and places the gap
        // by the label's edges, as the Widgets strip does: a tab's width past
        // the last tab's middle puts it after that tab.
        from = tabPoint(dock->group(first), a);
        const auto& secondStrip = dock->group(second)->tabBar()->controller();
        to = tabPoint(dock->group(second), c) + QPoint(secondStrip.tabRect(secondStrip.indexOf(c)).width(), 0);
        QTest::mousePress(quickWindow, Qt::LeftButton, {}, from);
        moveHeld(quickWindow, from, to);
        QTest::mouseRelease(quickWindow, Qt::LeftButton, {}, to);
        QTRY_COMPARE(tabs(second), (QVector<TabId>{c, a}));
        QCOMPARE(tabs(first), QVector<TabId>{b});
        QCOMPARE(manager.model().window(window)->activeGroup, second);

        // Escape ends the drag where it is; nothing moves.
        QTRY_VERIFY(dock->group(second)->count() == 2);
        from = tabPoint(dock->group(second), a);
        to = tabPoint(dock->group(first), b) + QPoint(30, 0);
        QTest::mousePress(quickWindow, Qt::LeftButton, {}, from);
        moveHeld(quickWindow, from, to);
        QTest::keyClick(quickWindow, Qt::Key_Escape);
        QTest::mouseRelease(quickWindow, Qt::LeftButton, {}, to);
        QTest::qWait(50);
        QCOMPARE(tabs(second), (QVector<TabId>{c, a}));
        QCOMPARE(tabs(first), QVector<TabId>{b});

        // Torn out of its strip, the tab's window follows the pointer. Its
        // group was the only other one, so the remaining group fills the
        // window; over its content the guides show, and the lower one docks
        // the tab in a new group below it.
        const int leftEdge = dock->groupRect(first).left();
        from = tabPoint(dock->group(first), b);
        const auto lifted = from + QPoint(0, 120);
        QTest::mousePress(quickWindow, Qt::LeftButton, {}, from);
        moveHeld(quickWindow, from, lifted);
        QVERIFY(!manager.model().group(first));
        QTRY_COMPARE(dock->groupRect(second).left(), leftEdge);
        DropQuery query;
        query.global = dock->groupContentRect(second).center();
        const auto guides = dropTargetAt(*dock, manager.model(), query).guides;
        const auto below = std::find_if(guides.cbegin(), guides.cend(),
            [](const DockGuide& guide) { return !guide.toEdge && guide.placement == Placement::Bottom; });
        QVERIFY(below != guides.cend());
        to = quickWindow->mapFromGlobal(below->rect.center());
        moveHeld(quickWindow, lifted, to);
        auto* overlay = dock->findChild<quick::DockGuideItem*>();
        QVERIFY(overlay);
        const auto shown = overlay->childItems();
        QVERIFY(std::any_of(shown.cbegin(), shown.cend(), [](const QQuickItem* item) { return item->isVisible(); }));
        QTest::mouseRelease(quickWindow, Qt::LeftButton, {}, to);
        QTRY_COMPARE(manager.model().group(manager.model().groupForTab(b))->windowId, window);
        const auto third = manager.model().groupForTab(b);
        QVERIFY(third != second);
        QTRY_VERIFY(dock->group(third));
        QTRY_VERIFY(dock->groupRect(third).top() > dock->groupRect(second).top());
        QCOMPARE(dock->groupRect(third).left(), dock->groupRect(second).left());
        const auto after = overlay->childItems();
        QVERIFY(std::none_of(after.cbegin(), after.cend(), [](const QQuickItem* item) { return item->isVisible(); }));
    }

    void tabsDragIntoAnotherWindow()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto source = manager.createWindow(WindowKind::Workbench, {QStringLiteral("Source"), QSize(520, 380)});
        const auto moving = manager.addTab(source, named(QStringLiteral("Moving")));
        const auto staying = manager.addTab(source, named(QStringLiteral("Staying")));
        const auto target = manager.createWindow(WindowKind::Workbench, {QStringLiteral("Target"), QSize(520, 380)});
        const auto other = manager.addTab(target, named(QStringLiteral("Other")));
        QTRY_VERIFY(workspace.window(source) && workspace.window(target));
        auto* from = workspace.window(source);
        auto* to = workspace.window(target);
        // Side by side, so the pointer is over exactly one of them.
        to->setPosition(from->frameGeometry().topRight() + QPoint(40, 0));
        QVERIFY(QTest::qWaitForWindowExposed(from) && QTest::qWaitForWindowExposed(to));
        auto* sourceDock = from->contentItem()->findChild<quick::DockSurfaceItem*>();
        auto* targetDock = to->contentItem()->findChild<quick::DockSurfaceItem*>();
        const auto sourceGroup = manager.model().groupForTab(moving);
        const auto targetGroup = manager.model().groupForTab(other);
        QTRY_VERIFY(sourceDock->group(sourceGroup) && targetDock->group(targetGroup));
        QTRY_COMPARE(to->position(), from->frameGeometry().topRight() + QPoint(40, 0));
        // Another window covers the target's right half.
        auto behavior = manager.behavior(); behavior.revealDelayMs = 50; manager.setBehavior(behavior);
        QWindow cover;
        cover.setFlags(Qt::FramelessWindowHint);
        cover.setGeometry(QRect(to->geometry().center(), to->size() / 2));
        cover.show();
        QVERIFY(QTest::qWaitForWindowExposed(&cover));
        const auto covered = to->geometry().center() + QPoint(20, 20);
        const auto front = [&] { return stackedWindowAt(covered, [&](const QWindow* window) { return window == to || window == &cover; }); };
        QTRY_COMPARE(front(), &cover);

        // The source window holds the mouse, as under the system's capture,
        // while the pointer crosses into the target; QTest notes each such event.
        // Resting on the target brings it to the front.
        const QRegularExpression outside(QStringLiteral("occurs outside target window"));
        const auto start = tabPoint(sourceDock->group(sourceGroup), moving);
        const auto end = from->mapFromGlobal(to->mapToGlobal(tabPoint(targetDock->group(targetGroup), other) + QPoint(30, 0)));
        QTest::mousePress(from, Qt::LeftButton, {}, start);
        for (int step = 1; step <= 8; ++step)
            if (!from->geometry().contains(from->mapToGlobal(start + (end - start) * step / 8)))
                QTest::ignoreMessage(QtWarningMsg, outside);
        moveHeld(from, start, end);
        QTRY_COMPARE(front(), to);
        QTest::ignoreMessage(QtWarningMsg, outside);
        QTest::mouseRelease(from, Qt::LeftButton, {}, end);
        QTRY_COMPARE(manager.model().group(targetGroup)->tabs, (QVector<TabId>{other, moving}));
        QCOMPARE(manager.model().group(sourceGroup)->tabs, QVector<TabId>{staying});
        QTRY_COMPARE(targetDock->group(targetGroup)->count(), 2);
    }

    // Tab during a drag, as in the Widgets drag: the tab tears out, a list of
    // the other windows opens, and the chosen one comes back from the
    // taskbar to receive the drop. Esc then returns the tab.
    void tabDuringADragChoosesTheTargetWindow()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto source = manager.createWindow(WindowKind::Workbench, {QStringLiteral("Source"), QSize(520, 380)});
        const auto moving = manager.addTab(source, named(QStringLiteral("Moving")));
        manager.addTab(source, named(QStringLiteral("Staying")));
        const auto target = manager.createWindow(WindowKind::Workbench, {QStringLiteral("Target"), QSize(520, 380)});
        manager.addTab(target, named(QStringLiteral("Target tab")));
        QTRY_VERIFY(workspace.window(source) && workspace.window(target));
        auto* from = workspace.window(source);
        QPointer<QQuickWindow> to(workspace.window(target));
        QVERIFY(QTest::qWaitForWindowExposed(from));
        to->showMinimized();
        QTRY_VERIFY(to->windowStates() & Qt::WindowMinimized);
        from->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(from));
        auto* dock = from->contentItem()->findChild<quick::DockSurfaceItem*>();
        const auto sourceGroup = manager.model().groupForTab(moving);
        QTRY_VERIFY(dock->group(sourceGroup));
        const auto start = tabPoint(dock->group(sourceGroup), moving);
        QTest::mousePress(from, Qt::LeftButton, {}, start);
        moveHeld(from, start, start + QPoint(12, 0));
        QTest::keyClick(from, Qt::Key_Tab);
        // The tab is now in a floating window, which anchors the list.
        QTRY_VERIFY(manager.model().groupForTab(moving) != sourceGroup);
        QQuickWindow* list = nullptr;
        QTRY_VERIFY((list = [&]() -> QQuickWindow* {
            for (auto* window : QGuiApplication::topLevelWindows())
                if (window->type() == Qt::Popup && window->isVisible()) return qobject_cast<QQuickWindow*>(window);
            return nullptr;
        }()));
        QVERIFY(QTest::qWaitForWindowExposed(list));
        QQuickItem* row = nullptr;
        const auto targetRow = target.toString(QUuid::WithoutBraces);
        QTRY_VERIFY((row = [&]() -> QQuickItem* {
            QList<QQuickItem*> items{list->contentItem()};
            while (!items.isEmpty()) {
                auto* item = items.takeFirst();
                if (item->objectName() == targetRow) return item;
                items += item->childItems();
            }
            return nullptr;
        }()));
        QTest::mouseClick(list, Qt::LeftButton, Qt::NoModifier, row->mapToScene(row->boundingRect().center()).toPoint());
        QTRY_VERIFY(to && !(to->windowStates() & Qt::WindowMinimized));
        QTest::keyClick(from, Qt::Key_Escape);
        QTRY_COMPARE(manager.model().groupForTab(moving), sourceGroup);
        QTest::mouseRelease(from, Qt::LeftButton, {}, start);
    }
    void aDragScrollsTheStripUnderThePointer()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto source = manager.createWindow(WindowKind::Workbench, {QStringLiteral("Source"), QSize(520, 380)});
        const auto moving = manager.addTab(source, named(QStringLiteral("Moving")));
        manager.addTab(source, named(QStringLiteral("Staying")));
        const auto target = manager.createWindow(WindowKind::Workbench, {QStringLiteral("Target"), QSize(520, 380)});
        TabId first;
        for (int index = 0; index < 16; ++index) {
            const auto tab = manager.addTab(target, named(QStringLiteral("Long tab title %1").arg(index)));
            if (first.isNull()) first = tab;
        }
        QVERIFY(manager.activateTab(first));
        QTRY_VERIFY(workspace.window(source) && workspace.window(target));
        auto* from = workspace.window(source);
        auto* to = workspace.window(target);
        to->setPosition(from->frameGeometry().topRight() + QPoint(40, 0));
        QVERIFY(QTest::qWaitForWindowExposed(from) && QTest::qWaitForWindowExposed(to));
        QTRY_COMPARE(to->position(), from->frameGeometry().topRight() + QPoint(40, 0));
        auto* sourceDock = from->contentItem()->findChild<quick::DockSurfaceItem*>();
        auto* targetDock = to->contentItem()->findChild<quick::DockSurfaceItem*>();
        const auto targetGroup = manager.model().groupForTab(first);
        QTRY_VERIFY(sourceDock->group(manager.model().groupForTab(moving)) && targetDock->group(targetGroup));
        const auto& strip = targetDock->group(targetGroup)->tabBar()->controller();
        QTRY_VERIFY(strip.maximumScrollOffset() > 0);
        const int before = strip.scrollOffset();

        // Over the target's strip, a wheel step scrolls it, though the
        // source window holds the mouse.
        const QRegularExpression outside(QStringLiteral("occurs outside target window"));
        const auto start = tabPoint(sourceDock->group(manager.model().groupForTab(moving)), moving);
        const auto end = from->mapFromGlobal(to->mapToGlobal(tabPoint(targetDock->group(targetGroup), first) + QPoint(60, 0)));
        QTest::mousePress(from, Qt::LeftButton, {}, start);
        for (int step = 1; step <= 8; ++step)
            if (!from->geometry().contains(from->mapToGlobal(start + (end - start) * step / 8)))
                QTest::ignoreMessage(QtWarningMsg, outside);
        moveHeld(from, start, end);
        QVERIFY(!targetDock->group(targetGroup)->insertionPreviewRectGlobal().isEmpty());
        QWheelEvent wheel(QPointF(end), QPointF(from->mapToGlobal(end)), QPoint(0, -120), QPoint(0, -120),
                          Qt::LeftButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(from, &wheel);
        QVERIFY(wheel.isAccepted());
        QTRY_VERIFY(strip.scrollOffset() != before);
        QTest::keyClick(from, Qt::Key_Escape);
        QTest::ignoreMessage(QtWarningMsg, outside);
        QTest::mouseRelease(from, Qt::LeftButton, {}, end);
        QTRY_COMPARE(manager.model().group(manager.model().groupForTab(moving))->windowId, source);
    }

    void aTabDraggedOutBecomesAFloatingWindow()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto source = manager.createWindow(WindowKind::Workbench, {QStringLiteral("Source"), QSize(520, 380)});
        const auto moving = manager.addTab(source, named(QStringLiteral("Moving")));
        const auto staying = manager.addTab(source, named(QStringLiteral("Staying")));
        QTRY_VERIFY(workspace.window(source));
        auto* from = workspace.window(source);
        QVERIFY(QTest::qWaitForWindowExposed(from));
        auto* dock = from->contentItem()->findChild<quick::DockSurfaceItem*>();
        const auto group = manager.model().groupForTab(moving);
        QTRY_VERIFY(dock->group(group) && dock->group(group)->count() == 2);
        const auto windowsBefore = manager.model().windowOrder();
        const auto windowOf = [&](TabId tab) { return manager.model().group(manager.model().groupForTab(tab))->windowId; };

        // Past the strip the tab moves into a new window that follows the
        // pointer by its title; released beside the source window, it stays.
        const auto start = tabPoint(dock->group(group), moving);
        const auto end = from->mapFromGlobal(from->frameGeometry().topRight() + QPoint(300, 120));
        const QRegularExpression outside(QStringLiteral("occurs outside target window"));
        QTest::mousePress(from, Qt::LeftButton, {}, start);
        for (int step = 1; step <= 8; ++step)
            if (!from->geometry().contains(from->mapToGlobal(start + (end - start) * step / 8)))
                QTest::ignoreMessage(QtWarningMsg, outside);
        moveHeld(from, start, end);
        const auto created = windowOf(moving);
        QVERIFY(!windowsBefore.contains(created));
        QCOMPARE(manager.model().window(created)->kind, WindowKind::Floating);
        auto* torn = workspace.window(created);
        QVERIFY(torn && torn->isVisible());
        const QPoint title(torn->width() / 2, quick::WindowChromeItem::rowHeight() / 2);
        QCOMPARE(torn->mapToGlobal(title), from->mapToGlobal(end));
        QTest::ignoreMessage(QtWarningMsg, outside);
        QTest::mouseRelease(from, Qt::LeftButton, {}, end);
        QTest::qWait(50);
        QCOMPARE(windowOf(moving), created);
        QCOMPARE(manager.model().group(group)->tabs, QVector<TabId>{staying});
        QCOMPARE(torn->opacity(), 1.0);

        // Escape after tearing out puts the tab back in its window.
        QTRY_VERIFY(dock->group(group) && dock->group(group)->count() == 1);
        const auto again = tabPoint(dock->group(group), staying);
        const auto away = again + QPoint(0, 160);
        QTest::mousePress(from, Qt::LeftButton, {}, again);
        moveHeld(from, again, away);
        const auto second = windowOf(staying);
        QVERIFY(second != source && second != created);
        QTest::keyClick(from, Qt::Key_Escape);
        QTest::mouseRelease(from, Qt::LeftButton, {}, away);
        QTRY_COMPARE(windowOf(staying), source);
        QTRY_VERIFY(!manager.model().window(second));
        QTRY_VERIFY(!workspace.window(second));
    }

    void railsTakeAndGiveDraggedTabs()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto window = manager.createWindow(WindowKind::Workbench, {QStringLiteral("Rails"), QSize(560, 400)});
        const auto moving = manager.addTab(window, named(QStringLiteral("Moving")));
        manager.addTab(window, named(QStringLiteral("Staying")));
        const auto first = manager.addTab(window, named(QStringLiteral("First")));
        const auto second = manager.addTab(window, named(QStringLiteral("Second")));
        QVERIFY(manager.moveTabToEdge(first, Edge::Left));
        QVERIFY(manager.moveTabToEdge(second, Edge::Left));
        QTRY_VERIFY(workspace.window(window));
        auto* quickWindow = workspace.window(window);
        QVERIFY(QTest::qWaitForWindowExposed(quickWindow));
        auto* dock = quickWindow->contentItem()->findChild<quick::DockSurfaceItem*>();
        const auto central = manager.model().groupForTab(moving);
        QTRY_VERIFY(dock->rail(Edge::Left) && dock->rail(Edge::Left)->buttons().size() == 2 && dock->group(central));
        const auto rail = [&] { return manager.model().autoHiddenTabs(window, Edge::Left); };
        // A rail button's point a quarter along it, in window coordinates.
        const auto along = [&](TabId tab, qreal fraction) {
            for (auto* button : dock->rail(Edge::Left)->buttons()) if (button->tab() == tab)
                return button->mapToScene(QPointF(button->width() / 2, button->height() * fraction)).toPoint();
            return QPoint();
        };
        const QRegularExpression outside(QStringLiteral("occurs outside target window"));

        // A strip's tab dropped at the second button's leading quarter goes
        // between the two; the rail opens a gap while the tab is over it.
        // The torn-out window hides there and the tab shows under the
        // pointer as the checked rail button it will become.
        auto from = tabPoint(dock->group(central), moving);
        auto to = along(second, 0.25);
        QTest::mousePress(quickWindow, Qt::LeftButton, {}, from);
        moveHeld(quickWindow, from, to);
        QVERIFY(!dock->railInsertionRectGlobal(Edge::Left).isEmpty());
        const auto carrier = workspace.window(manager.model().group(manager.model().groupForTab(moving))->windowId);
        QVERIFY(carrier && carrier != quickWindow);
        QCOMPARE(carrier->opacity(), 0.0);
        const auto carried = [&] {
            const auto buttons = dock->findChildren<quick::RailButtonItem*>();
            const auto found = std::find_if(buttons.cbegin(), buttons.cend(), [&](const quick::RailButtonItem* button) {
                return button->tab() == moving && button->isVisible()
                    && button->mapRectToScene(button->boundingRect()).contains(QPointF(to));
            });
            return found == buttons.cend() ? nullptr : *found;
        };
        QVERIFY(carried() && carried()->isChecked());
        QTest::mouseRelease(quickWindow, Qt::LeftButton, {}, to);
        QTRY_COMPARE(rail(), (QVector<TabId>{first, moving, second}));
        QVERIFY(dock->railInsertionRectGlobal(Edge::Left).isEmpty());
        QVERIFY(!carried() || dock->rail(Edge::Left)->isAncestorOf(carried()));

        // Within its rail a tab moves among the buttons.
        QTRY_VERIFY(dock->rail(Edge::Left) && dock->rail(Edge::Left)->buttons().size() == 3);
        from = along(second, 0.5);
        to = along(first, 0.25);
        QTest::mousePress(quickWindow, Qt::LeftButton, {}, from);
        moveHeld(quickWindow, from, to);
        QTest::mouseRelease(quickWindow, Qt::LeftButton, {}, to);
        QTRY_COMPARE(rail(), (QVector<TabId>{second, first, moving}));

        // Off the rail it tears out; Escape puts it back on the rail.
        QTRY_VERIFY(dock->rail(Edge::Left) && dock->rail(Edge::Left)->buttons().first()->tab() == second);
        from = along(first, 0.5);
        to = quickWindow->mapFromGlobal(quickWindow->frameGeometry().topRight() + QPoint(300, 120));
        QTest::mousePress(quickWindow, Qt::LeftButton, {}, from);
        for (int step = 1; step <= 8; ++step)
            if (!quickWindow->geometry().contains(quickWindow->mapToGlobal(from + (to - from) * step / 8)))
                QTest::ignoreMessage(QtWarningMsg, outside);
        moveHeld(quickWindow, from, to);
        const auto torn = manager.model().group(manager.model().groupForTab(first))->windowId;
        QVERIFY(torn != window);
        QTest::keyClick(quickWindow, Qt::Key_Escape);
        QTest::ignoreMessage(QtWarningMsg, outside);
        QTest::mouseRelease(quickWindow, Qt::LeftButton, {}, to);
        QTRY_COMPARE(rail(), (QVector<TabId>{second, first, moving}));
        QTRY_VERIFY(!workspace.window(torn));
    }

    void aFolderTearsOutWithItsTabs()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto source = manager.createWindow(WindowKind::Workbench, {QStringLiteral("Source"), QSize(520, 380)});
        const auto kept = manager.addTab(source, named(QStringLiteral("Kept")));
        const auto first = manager.addTab(source, named(QStringLiteral("First")));
        const auto second = manager.addTab(source, named(QStringLiteral("Second")));
        const auto folder = manager.createTabFolder({first, second}, QStringLiteral("Pair"));
        QVERIFY(!folder.isNull());
        const auto railed = manager.addTab(source, named(QStringLiteral("Railed")));
        QVERIFY(manager.moveTabToEdge(railed, Edge::Left));
        QTRY_VERIFY(workspace.window(source));
        auto* from = workspace.window(source);
        QVERIFY(QTest::qWaitForWindowExposed(from));
        auto* dock = from->contentItem()->findChild<quick::DockSurfaceItem*>();
        const auto group = manager.model().groupForTab(kept);
        QTRY_VERIFY(dock->group(group) && dock->group(group)->count() == 3);
        const auto header = [&](GroupId holder) {
            auto* bar = dock->group(holder)->tabBar();
            return bar->mapToScene(QPointF(bar->controller().folderHeaderRect(0).center())).toPoint();
        };
        const auto windowOf = [&](TabId tab) { return manager.model().group(manager.model().groupForTab(tab))->windowId; };
        const QRegularExpression outside(QStringLiteral("occurs outside target window"));
        const auto moveFrom = [&](QPoint start, QPoint end) {
            for (int step = 1; step <= 8; ++step)
                if (!from->geometry().contains(from->mapToGlobal(start + (end - start) * step / 8)))
                    QTest::ignoreMessage(QtWarningMsg, outside);
            moveHeld(from, start, end);
        };
        const auto tearOut = [&](GroupId holder) {
            const auto start = header(holder);
            const auto end = from->mapFromGlobal(from->frameGeometry().topRight() + QPoint(300, 120));
            QTest::mousePress(from, Qt::LeftButton, {}, start);
            moveFrom(start, end);
            return end;
        };

        // Out of the strip the folder moves whole into a new window; Escape
        // puts it back where it was.
        auto end = tearOut(group);
        const auto created = windowOf(first);
        QVERIFY(created != source);
        QCOMPARE(windowOf(second), created);
        QCOMPARE(manager.model().tab(first)->folder, folder);
        QCOMPARE(manager.model().group(group)->tabs, QVector<TabId>{kept});
        QTest::keyClick(from, Qt::Key_Escape);
        QTest::ignoreMessage(QtWarningMsg, outside);
        QTest::mouseRelease(from, Qt::LeftButton, {}, end);
        QTRY_COMPARE(manager.model().group(group)->tabs, (QVector<TabId>{kept, first, second}));
        QCOMPARE(manager.model().tab(second)->folder, folder);
        QTRY_VERIFY(!workspace.window(created));

        // On a guide it docks whole as its own group, still a folder.
        QTRY_VERIFY(dock->group(group) && dock->group(group)->count() == 3);
        end = tearOut(group);
        const auto carrier = windowOf(first);
        QVERIFY(carrier != source);
        DropQuery query;
        query.global = dock->groupContentRect(group).center();
        const auto guides = dropTargetAt(*dock, manager.model(), query).guides;
        const auto below = std::find_if(guides.cbegin(), guides.cend(),
            [](const DockGuide& guide) { return !guide.toEdge && guide.placement == Placement::Bottom; });
        QVERIFY(below != guides.cend());
        const auto guide = from->mapFromGlobal(below->rect.center());
        moveFrom(end, guide);
        QTest::mouseRelease(from, Qt::LeftButton, {}, guide);
        QTRY_COMPARE(windowOf(first), source);
        const auto docked = manager.model().groupForTab(first);
        QVERIFY(docked != group);
        QCOMPARE(manager.model().group(docked)->tabs, (QVector<TabId>{first, second}));
        QCOMPARE(manager.model().tab(second)->folder, folder);
        QCOMPARE(manager.model().group(group)->tabs, QVector<TabId>{kept});
        QTRY_VERIFY(dock->group(docked) && dock->groupRect(docked).top() > dock->groupRect(group).top());
        QTRY_VERIFY(!workspace.window(carrier));

        // Released outside, it stays in its window, still a folder.
        end = tearOut(docked);
        QTest::ignoreMessage(QtWarningMsg, outside);
        QTest::mouseRelease(from, Qt::LeftButton, {}, end);
        QTest::qWait(50);
        const auto torn = windowOf(first);
        QVERIFY(torn != source);
        QCOMPARE(manager.model().tab(second)->folder, folder);
        QCOMPARE(manager.model().group(group)->tabs, QVector<TabId>{kept});

        // Onto a rail its tabs go one by one; a rail holds no folders.
        QTRY_VERIFY(workspace.window(torn));
        auto* tornWindow = workspace.window(torn);
        QVERIFY(QTest::qWaitForWindowExposed(tornWindow));
        auto* tornDock = tornWindow->contentItem()->findChild<quick::DockSurfaceItem*>();
        const auto holder = manager.model().groupForTab(first);
        QTRY_VERIFY(tornDock->group(holder) && tornDock->group(holder)->count() == 2);
        QTRY_VERIFY(dock->rail(Edge::Left) && dock->rail(Edge::Left)->buttons().size() == 1);
        auto* button = dock->rail(Edge::Left)->buttons().first();
        const auto start = [&] {
            auto* bar = tornDock->group(holder)->tabBar();
            return bar->mapToScene(QPointF(bar->controller().folderHeaderRect(0).center())).toPoint();
        }();
        const auto onRail = button->mapToScene(QPointF(button->width() / 2, button->height() * 0.75)).toPoint();
        // One move tears the folder out into a new window and the emptied
        // one closes; later events go to the window under the pointer.
        const auto tornOut = from->mapFromGlobal(tornWindow->mapToGlobal(start + QPoint(0, 150)));
        QTest::mousePress(tornWindow, Qt::LeftButton, {}, start);
        QTest::mouseMove(tornWindow, start + QPoint(0, 150));
        QVERIFY(windowOf(first) != torn);
        moveFrom(tornOut, onRail);
        QTest::mouseRelease(from, Qt::LeftButton, {}, onRail);
        QTRY_COMPARE(manager.model().autoHiddenTabs(source, Edge::Left), (QVector<TabId>{railed, first, second}));
        QVERIFY(!manager.model().folder(folder));
    }

    void aFloatingWindowDraggedByItsTitleJoinsAStrip()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto target = manager.createWindow(WindowKind::Workbench, {QStringLiteral("Target"), QSize(520, 380)});
        const auto other = manager.addTab(target, named(QStringLiteral("Other")));
        const auto floating = manager.createWindow(WindowKind::Floating, {QStringLiteral("Floating"), QSize(360, 260)});
        const auto first = manager.addTab(floating, named(QStringLiteral("First")));
        const auto second = manager.addTab(floating, named(QStringLiteral("Second")));
        QTRY_VERIFY(workspace.window(target) && workspace.window(floating));
        auto* to = workspace.window(target);
        auto* carried = workspace.window(floating);
        const auto beside = to->frameGeometry().topRight() + QPoint(40, 0);
        carried->setPosition(beside);
        QVERIFY(QTest::qWaitForWindowExposed(to) && QTest::qWaitForWindowExposed(carried));
        QTRY_COMPARE(carried->position(), beside);
        auto* targetDock = to->contentItem()->findChild<quick::DockSurfaceItem*>();
        const auto group = manager.model().groupForTab(other);
        QTRY_VERIFY(targetDock->group(group));
        auto* carriedDock = carried->contentItem()->findChild<quick::DockSurfaceItem*>();
        QTRY_VERIFY(carriedDock->group(manager.model().groupForTab(second)));
        const auto carriedContent = carriedDock->group(manager.model().groupForTab(second))->contentRect().size();
        const auto preview = [&] {
            const auto* picture = targetDock->group(group)->findChild<quick::PictureItem*>();
            return picture && picture->isVisible() ? picture->picture() : QImage{};
        };

        // The title starts the drag once the pointer has moved; the window
        // then follows it, here by events to the window under the pointer.
        const QPoint title(carried->width() / 2, quick::WindowChromeItem::rowHeight() / 2);
        const auto drop = tabPoint(targetDock->group(group), other) + QPoint(30, 0);
        const QRegularExpression outside(QStringLiteral("occurs outside target window"));
        const auto carry = [&] {
            QTest::mousePress(carried, Qt::LeftButton, {}, title);
            QTest::mouseMove(carried, title + QPoint(-30, 0));
            const auto start = to->mapFromGlobal(carried->mapToGlobal(title));
            for (int step = 1; step <= 8; ++step)
                if (!to->geometry().contains(to->mapToGlobal(start + (drop - start) * step / 8)))
                    QTest::ignoreMessage(QtWarningMsg, outside);
            moveHeld(to, start, drop);
        };

        // Escape puts the window back where it was.
        carry();
        QVERIFY(carried->position() != beside);
        QVERIFY(!targetDock->group(group)->insertionPreviewRectGlobal().isEmpty());
        // Over the strip the window hides and the strip paints its tabs at
        // the pointer; the group shows the carried content's picture at its
        // own size.
        QCOMPARE(carried->opacity(), 0.0);
        QVERIFY(targetDock->group(group)->incomingRectGlobal().contains(to->mapToGlobal(drop)));
        QCOMPARE(preview().deviceIndependentSize().toSize(), carriedContent);
        QTest::keyClick(to, Qt::Key_Escape);
        QTest::mouseRelease(to, Qt::LeftButton, {}, drop);
        QCOMPARE(carried->position(), beside);
        QCOMPARE(carried->opacity(), 1.0);
        QVERIFY(targetDock->group(group)->incomingRectGlobal().isEmpty());
        QVERIFY(preview().isNull());
        QCOMPARE(manager.model().group(group)->tabs, QVector<TabId>{other});

        // Dropped on the strip, all its tabs join it and the window closes.
        // They slide from where the strip painted them to their places.
        carry();
        const auto painted = targetDock->group(group)->incomingRectGlobal();
        QTest::mouseRelease(to, Qt::LeftButton, {}, drop);
        QTRY_COMPARE(manager.model().group(group)->tabs, (QVector<TabId>{other, first, second}));
        QTRY_VERIFY(preview().isNull());
        if (motionAnimationsEnabled()) {
            auto* bar = targetDock->group(group)->tabBar();
            auto& strip = bar->controller();
            const auto settling = [&] {
                const auto frame = strip.presentationFrame();
                return frame && frame->foreground ? frame->foreground->settling : QVector<TabStripLabelSample>{};
            };
            QTRY_VERIFY(!settling().isEmpty());
            const QRect from(bar->mapFromGlobal(QPointF(painted.topLeft())).toPoint(), painted.size());
            const auto final = strip.tabLayout().tabs.at(strip.indexOf(first)).translated(strip.tabOrigin());
            const auto samples = settling();
            const auto landing = std::find_if(samples.cbegin(), samples.cend(),
                [&](const TabStripLabelSample& sample) { return sample.id == first; });
            QVERIFY(landing != samples.cend());
            QVERIFY(landing->label.parts.tab != final);
            QVERIFY((from | final).contains(landing->label.parts.tab));
            // Frames come from the display's refresh; see the lesson on
            // measuring frames only while the screen is on.
            QTRY_VERIFY_WITH_TIMEOUT(settling().isEmpty(), 1000);
        }
        QVERIFY(!manager.model().window(floating));
        QTRY_VERIFY(!workspace.window(floating));
    }
};

QTEST_MAIN(QuickWorkspaceDragTest)
#include "quick_workspace_drag_test.moc"
