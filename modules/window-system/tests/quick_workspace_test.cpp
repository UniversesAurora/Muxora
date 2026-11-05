// SPDX-License-Identifier: MIT
// Phase 10.3: the Quick workspace builds its windows from an External
// manager's notifications. Every model window gets a Quick window, the
// active tab's panes show their content, and pane content keeps its identity
// while its tab moves between windows.
#include "docking/drop_target.hpp"
#include "quick/button_item.hpp"
#include "quick/command_strip_host_item.hpp"
#include "quick/status_bar_item.hpp"
#include "quick/dock_guide_item.hpp"
#include "quick/dock_surface_item.hpp"
#include "quick/group_item.hpp"
#include "quick/peek_item.hpp"
#include "quick/picture_item.hpp"
#include "quick/rail_button_item.hpp"
#include "quick/rail_item.hpp"
#include "quick/tab_bar_item.hpp"
#include "quick/window_chrome_item.hpp"
#include "platform/window_order.hpp"
#include "style/frame_motion.hpp"
#include <window_system/quick_workspace.hpp>
#include <window_system/window_manager.hpp>

#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTest>
#include <QtMath>
#include <QWheelEvent>

using namespace window_system;

namespace {

TabOptions named(const QString& title) { return {title, QStringLiteral("test"), {}}; }

// Content shows when it sits in a window, visible, with a real size.
bool shownIn(const QQuickItem* item, const QQuickWindow* window)
{
    if (!item || item->window() != window || item->width() <= 0 || item->height() <= 0) return false;
    for (auto* ancestor = item; ancestor; ancestor = ancestor->parentItem())
        if (!ancestor->isVisible()) return false;
    return true;
}

quick::GroupItem* groupOf(const QQuickWindow* window, int index = 0)
{
    const auto groups = window->contentItem()->findChildren<quick::GroupItem*>();
    return index < groups.size() ? groups.at(index) : nullptr;
}

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

class QuickWorkspaceTest final : public QObject {
    Q_OBJECT
private slots:
    void activeTabsShowTheirPanes()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions& options, PaneId) {
            auto* item = new QQuickItem; item->setObjectName(options.title); return item;
        });
        const auto window = manager.createWindow();
        const auto shell = manager.addTab(window, named(QStringLiteral("Shell")));
        const auto files = manager.addTab(window, named(QStringLiteral("Files")));
        QTRY_VERIFY(workspace.window(window));
        auto* quickWindow = workspace.window(window);
        QVERIFY(QTest::qWaitForWindowExposed(quickWindow));
        const auto shellPane = manager.model().tab(shell)->activePane;
        const auto filesPane = manager.model().tab(files)->activePane;
        QPointer<QQuickItem> shellContent = workspace.content(shellPane);
        QPointer<QQuickItem> filesContent = workspace.content(filesPane);
        QVERIFY(shellContent && filesContent);
        QCOMPARE(filesContent->objectName(), QStringLiteral("Files"));
        QTRY_VERIFY(shownIn(filesContent, quickWindow));
        QVERIFY(!shownIn(shellContent, quickWindow));
        auto* group = groupOf(quickWindow);
        QVERIFY(group); QCOMPARE(group->count(), 2); QCOMPARE(group->currentIndex(), 1);
        // The pane fills the group's content area.
        const auto content = group->contentRect();
        QCOMPARE(filesContent->mapRectToItem(group, filesContent->boundingRect()).toRect(), content);

        manager.activateTab(shell);
        QTRY_VERIFY(shownIn(shellContent, quickWindow));
        QVERIFY(!shownIn(filesContent, quickWindow));
        QCOMPARE(group->currentIndex(), 0);

        // A split shows both panes side by side within the same area.
        const auto split = manager.splitPane(shell, shellPane, Placement::Right);
        QTRY_VERIFY(shownIn(workspace.content(split), quickWindow));
        QVERIFY(shownIn(shellContent, quickWindow));
        const auto left = shellContent->mapRectToItem(group, shellContent->boundingRect());
        const auto right = workspace.content(split)->mapRectToItem(group, workspace.content(split)->boundingRect());
        QVERIFY(left.right() <= right.left()); QVERIFY(left.width() > 0); QVERIFY(right.width() > 0);
        QCOMPARE(left.top(), right.top()); QCOMPARE(left.height(), right.height());

        // Closing a tab removes its label and releases its content.
        manager.closeTab(files);
        QTRY_VERIFY(!filesContent);
        QCOMPARE(group->count(), 1);
        QVERIFY(shownIn(shellContent, quickWindow));
    }

    void contentKeepsItsIdentityAcrossWindows()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto first = manager.createWindow();
        const auto moving = manager.addTab(first, named(QStringLiteral("Moving")));
        manager.addTab(first, named(QStringLiteral("Staying")));
        const auto second = manager.createWindow();
        manager.addTab(second, named(QStringLiteral("Other")));
        QTRY_VERIFY(workspace.window(first) && workspace.window(second));
        const auto pane = manager.model().tab(moving)->activePane;
        QPointer<QQuickItem> content = workspace.content(pane);
        QVERIFY(content);

        QVERIFY(manager.moveTabToWindow(moving, second));
        manager.activateTab(moving);
        QTRY_VERIFY(shownIn(content, workspace.window(second)));
        QCOMPARE(workspace.content(pane), content.data());
        QCOMPARE(groupOf(workspace.window(first))->count(), 1);
        QCOMPARE(groupOf(workspace.window(second))->count(), 2);
    }

    void pinnedRegionsSitAroundTheCenterAndKeepTheirExtent()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto window = manager.createWindow();
        const auto center = manager.addTab(window, named(QStringLiteral("Center")));
        const auto left = manager.addTab(window, named(QStringLiteral("Left")));
        const auto bottom = manager.addTab(window, named(QStringLiteral("Bottom")));
        QVERIFY(manager.moveTabToEdge(left, Edge::Left, true));
        QVERIFY(manager.moveTabToEdge(bottom, Edge::Bottom, true));
        QTRY_VERIFY(workspace.window(window));
        auto* quickWindow = workspace.window(window);
        QVERIFY(QTest::qWaitForWindowExposed(quickWindow));
        auto* dock = quickWindow->contentItem()->findChild<quick::DockSurfaceItem*>();
        QVERIFY(dock);
        const auto& model = manager.model();
        const auto leftGroup = model.groupForTab(left), bottomGroup = model.groupForTab(bottom);
        const auto centerGroup = model.groupForTab(center);
        QTRY_VERIFY(!dock->groupRect(leftGroup).isEmpty());
        const auto leftRect = dock->groupRect(leftGroup);
        const auto centerRect = dock->groupRect(centerGroup);
        const auto bottomRect = dock->groupRect(bottomGroup);
        // The bottom region spans the window; the left one sits beside the center.
        QCOMPARE(leftRect.width(), model.group(leftGroup)->edgeExtent);
        QCOMPARE(bottomRect.height(), model.group(bottomGroup)->edgeExtent);
        QVERIFY(leftRect.right() < centerRect.left());
        QCOMPARE(leftRect.top(), centerRect.top());
        QVERIFY(bottomRect.top() > centerRect.bottom());
        QCOMPARE(bottomRect.left(), leftRect.left());
        QVERIFY(bottomRect.right() >= centerRect.right());

        // A wider window gives every extra pixel to the center.
        quickWindow->resize(quickWindow->width() + 120, quickWindow->height());
        QTRY_COMPARE(dock->groupRect(centerGroup).width(), centerRect.width() + 120);
        QCOMPARE(dock->groupRect(leftGroup).width(), leftRect.width());

        // Dragging the divider saves the region's new extent.
        const QPoint grab((leftRect.right() + dock->groupRect(centerGroup).left()) / 2, centerRect.center().y());
        QTest::mousePress(quickWindow, Qt::LeftButton, Qt::NoModifier, grab);
        QTest::mouseMove(quickWindow, grab + QPoint(60, 0));
        QTest::mouseRelease(quickWindow, Qt::LeftButton, Qt::NoModifier, grab + QPoint(60, 0));
        QTRY_COMPARE(model.group(leftGroup)->edgeExtent, leftRect.width() + 60);
        QTRY_COMPARE(dock->groupRect(leftGroup).width(), leftRect.width() + 60);
    }

    void sidebarTabsOpenInAPanelOverTheWindow()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto window = manager.createWindow();
        const auto center = manager.addTab(window, named(QStringLiteral("Center")));
        const auto first = manager.addTab(window, named(QStringLiteral("First")));
        const auto second = manager.addTab(window, named(QStringLiteral("Second")));
        QVERIFY(manager.moveTabToEdge(first, Edge::Bottom));
        QVERIFY(manager.moveTabToEdge(second, Edge::Bottom));
        manager.activateTab(center);
        QTRY_VERIFY(workspace.window(window));
        auto* quickWindow = workspace.window(window);
        QVERIFY(QTest::qWaitForWindowExposed(quickWindow));
        auto* dock = quickWindow->contentItem()->findChild<quick::DockSurfaceItem*>();
        QVERIFY(dock);
        QTRY_VERIFY(dock->rail(Edge::Bottom));
        auto* rail = dock->rail(Edge::Bottom);
        QCOMPARE(rail->buttons().size(), 2);
        QVERIFY(!dock->rail(Edge::Left));
        // The rail spans the bottom; the groups end above it.
        QCOMPARE(int(rail->y() + rail->height()), int(dock->height()));
        QCOMPARE(int(rail->width()), int(dock->width()));
        QVERIFY(dock->groupRect(manager.model().groupForTab(center)).bottom() < rail->y());

        const auto click = [&](const QQuickItem* item) {
            QTest::mouseClick(quickWindow, Qt::LeftButton, Qt::NoModifier,
                              item->mapToScene(item->boundingRect().center()).toPoint());
        };
        auto* button = rail->buttons().at(1);
        QCOMPARE(button->tab(), second);
        click(button);
        QTRY_VERIFY(dock->peek());
        QCOMPARE(dock->peekTab(), second);
        QVERIFY(button->isChecked());
        QTRY_COMPARE(manager.model().group(manager.model().groupForTab(second))->activeTab, second);
        // The panel rests on the rail, across the window, with the tab's content.
        auto* peek = dock->peek();
        QCOMPARE(int(peek->y() + peek->height()), int(rail->y()));
        QCOMPARE(int(peek->width()), int(dock->width()));
        QPointer<QQuickItem> content = workspace.content(manager.model().tab(second)->activePane);
        QTRY_VERIFY(shownIn(content, quickWindow));
        QVERIFY(content->mapRectToItem(dock, content->boundingRect()).bottom() <= rail->y());

        QTest::keyClick(quickWindow, Qt::Key_Escape);
        QVERIFY(!dock->peek());
        QVERIFY(!button->isChecked());
        QVERIFY(!shownIn(content, quickWindow));

        // A click elsewhere closes the panel; clicking its button toggles it.
        click(button);
        QTRY_VERIFY(dock->peek());
        click(dock->group(manager.model().groupForTab(center)));
        QVERIFY(!dock->peek());
        click(button);
        QTRY_VERIFY(dock->peek());
        click(button);
        QVERIFY(!dock->peek());

        // The handle on its free side resizes the panel, and the tab keeps
        // that size.
        click(button);
        QTRY_VERIFY(dock->peek());
        peek = dock->peek();
        auto* handle = peek->findChild<QQuickItem*>(QStringLiteral("PeekResizeHandle"));
        QVERIFY(handle);
        const int height = int(peek->height());
        const auto grip = handle->mapToScene(handle->boundingRect().center()).toPoint();
        QTest::mousePress(quickWindow, Qt::LeftButton, {}, grip);
        moveHeld(quickWindow, grip, grip - QPoint(0, 40));
        QTest::mouseRelease(quickWindow, Qt::LeftButton, {}, grip - QPoint(0, 40));
        QTRY_COMPARE(int(dock->peek()->height()), height + 40);
        QCOMPARE(int(dock->peek()->y() + dock->peek()->height()), int(rail->y()));
        QCOMPARE(manager.peekSizePolicy(second, Edge::Bottom).height, std::optional<int>(height + 40));

        // Its title drags the tab out as the rail button does; Escape puts
        // it back on the rail.
        const auto title = peek->mapToScene(QPointF(peek->width() / 4, 6)).toPoint();
        QTest::mousePress(quickWindow, Qt::LeftButton, {}, title);
        moveHeld(quickWindow, title, title - QPoint(0, 120));
        QVERIFY(!dock->peek());
        QVERIFY(manager.model().group(manager.model().groupForTab(second))->windowId != window);
        QTest::keyClick(quickWindow, Qt::Key_Escape);
        QTest::mouseRelease(quickWindow, Qt::LeftButton, {}, title - QPoint(0, 120));
        QTRY_COMPARE(manager.model().autoHiddenTabs(window, Edge::Bottom), (QVector<TabId>{first, second}));
        QTRY_VERIFY(dock->rail(Edge::Bottom) && dock->rail(Edge::Bottom)->buttons().size() == 2);

        // Pinning the tab takes it off the rail; the last one removes the rail.
        QVERIFY(manager.pinTab(second, true));
        QTRY_COMPARE(dock->rail(Edge::Bottom)->buttons().size(), 1);
        QVERIFY(manager.pinTab(first, true));
        QTRY_VERIFY(!dock->rail(Edge::Bottom));
    }

    void theHeaderRowControlsItsWindow()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto kept = manager.createWindow(WindowKind::Workbench, {QStringLiteral("Kept"), QSize(640, 420)});
        manager.addTab(kept, named(QStringLiteral("Kept")));
        const auto window = manager.createWindow(WindowKind::Workbench, {QStringLiteral("Header"), QSize(640, 420)});
        manager.addTab(window, named(QStringLiteral("Only")));
        QTRY_VERIFY(workspace.window(window));
        QPointer<QQuickWindow> quickWindow = workspace.window(window);
        QVERIFY(QTest::qWaitForWindowExposed(quickWindow.data()));
        // As the Widgets window: the system title leads with the active tab.
        QTRY_COMPARE(quickWindow->title(), QStringLiteral("Only — Header"));
        // Joined tab shapes are drawn aliased and rely on multisampling.
        QCOMPARE(quickWindow->format().samples(), 4);
        auto* chrome = quickWindow->contentItem()->findChild<quick::WindowChromeItem*>();
        auto* dock = quickWindow->contentItem()->findChild<quick::DockSurfaceItem*>();
        auto* toolbars = quickWindow->contentItem()->findChild<quick::CommandStripHostItem*>();
        QVERIFY(chrome && dock && toolbars);
        // The dock fills the window between the top toolbar area and the
        // bottom toolbars and status bar, also after the window changes state.
        auto* status = quickWindow->contentItem()->findChild<quick::StatusBarItem*>();
        QVERIFY(status);
        const auto dockFillsTheBody = [&] {
            const auto* bottom = toolbars->bottomArea();
            const int below = (bottom->isVisible() ? qCeil(bottom->implicitHeight()) : 0)
                + (status->isVisible() ? qCeil(status->implicitHeight()) : 0);
            return toolbars->height() > 0 && dock->y() == toolbars->y() + toolbars->height()
                && dock->y() + dock->height() == quickWindow->height() - below;
        };
        QTRY_VERIFY(dockFillsTheBody());
        // The buttons sit at the right edge: minimize, maximize, close.
        const QPointF row(quickWindow->width() - 1, chrome->height() / 2);
        auto* close = chrome->captionButtonAt(row - QPointF(4, 0));
        auto* maximize = chrome->captionButtonAt(row - QPointF(4 + close->width(), 0));
        QVERIFY(close && maximize && close != maximize);
        QVERIFY(!chrome->captionButtonAt(QPointF(quickWindow->width() / 2, row.y())));
        const auto click = [&](const QQuickItem* item) {
            QTest::mouseClick(quickWindow, Qt::LeftButton, Qt::NoModifier, item->mapToScene(item->boundingRect().center()).toPoint());
        };
        click(maximize);
        QTRY_VERIFY(quickWindow->windowStates() & Qt::WindowMaximized);
        QTRY_VERIFY(dockFillsTheBody());
        click(maximize);
        QTRY_VERIFY(!(quickWindow->windowStates() & Qt::WindowMaximized));
        click(close);
        QTRY_VERIFY(!manager.model().window(window));
        QTRY_VERIFY(!quickWindow);
        QVERIFY(workspace.window(kept));
    }

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

    void windowRequestsReachTheQuickWindows()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto first = manager.createWindow(WindowKind::Workbench, {QStringLiteral("First"), QSize(420, 300)});
        const auto firstTab = manager.addTab(first, named(QStringLiteral("One")));
        const auto second = manager.createWindow(WindowKind::Workbench, {QStringLiteral("Second"), QSize(420, 300)});
        const auto secondTab = manager.addTab(second, named(QStringLiteral("Two")));
        QTRY_VERIFY(workspace.window(first) && workspace.window(second));
        auto* one = workspace.window(first);
        auto* two = workspace.window(second);
        two->setPosition(one->frameGeometry().topRight() + QPoint(40, 0));
        QVERIFY(QTest::qWaitForWindowExposed(one) && QTest::qWaitForWindowExposed(two));

        // Showing a minimized window brings it back in front and active.
        one->showMinimized();
        QTRY_VERIFY(one->windowStates() & Qt::WindowMinimized);
        QVERIFY(manager.activateWindow(first));
        QTRY_VERIFY(!(one->windowStates() & Qt::WindowMinimized));
        QTRY_VERIFY(one->isActive());
        QCOMPARE(manager.context().window, first);
        QCOMPARE(manager.context().tab, firstTab);

        // The window cycle goes to the next shown window and back.
        QVERIFY(manager.execute(QStringLiteral("window.focusNext.all")));
        QTRY_VERIFY(two->isActive());
        QCOMPARE(manager.context().window, second);
        QCOMPARE(manager.context().tab, secondTab);
        QVERIFY(manager.execute(QStringLiteral("window.focusNext.all")));
        QTRY_VERIFY(one->isActive());
        QCOMPARE(manager.context().window, first);

        // A minimized window is left out, and a window the user turns to
        // becomes the current one.
        one->showMinimized();
        QTRY_VERIFY(one->windowStates() & Qt::WindowMinimized);
        two->requestActivate();
        QTRY_COMPARE(manager.context().window, second);
        QVERIFY(manager.execute(QStringLiteral("window.focusNext.all")));
        QCOMPARE(manager.context().window, second);
        QVERIFY(one->windowStates() & Qt::WindowMinimized);
    }

    void viewModesShapeTheQuickWindow()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto window = manager.createWindow(WindowKind::Workbench, {QStringLiteral("View"), QSize(640, 420)});
        const auto a = manager.addTab(window, named(QStringLiteral("A")));
        const auto b = manager.addTab(window, named(QStringLiteral("B")));
        QVERIFY(manager.moveTab(b, manager.model().groupForTab(a), Placement::Right));
        const auto left = manager.model().groupForTab(a), right = manager.model().groupForTab(b);
        const auto top = manager.model().tab(a)->activePane;
        const auto bottom = manager.splitPane(a, top, Placement::Bottom);
        QVERIFY(!bottom.isNull());
        QTRY_VERIFY(workspace.window(window));
        auto* quickWindow = workspace.window(window);
        QVERIFY(QTest::qWaitForWindowExposed(quickWindow));
        auto* dock = quickWindow->contentItem()->findChild<quick::DockSurfaceItem*>();
        auto* chrome = quickWindow->contentItem()->findChild<quick::WindowChromeItem*>();
        QTRY_VERIFY(dock->group(left) && dock->group(right));
        const auto shown = [&](PaneId pane) { return shownIn(workspace.content(pane), quickWindow); };

        // Tab bars follow their setting.
        QVERIFY(!dock->group(left)->stripRect().isEmpty());
        QVERIFY(manager.setTabBarsVisible(window, false));
        QTRY_VERIFY(dock->group(left)->stripRect().isEmpty() && dock->group(right)->stripRect().isEmpty());
        QVERIFY(manager.setTabBarsVisible(window, true));
        QTRY_VERIFY(!dock->group(left)->stripRect().isEmpty());

        // Window fullscreen keeps the layout.
        QVERIFY(manager.setViewMode(window, ViewMode::WindowFullscreen));
        QTRY_VERIFY(quickWindow->windowStates() & Qt::WindowFullScreen);
        QVERIFY(dock->group(left) && dock->group(right));

        // Group fullscreen shows only the active group.
        QVERIFY(manager.activateTab(b));
        QVERIFY(manager.setViewMode(window, ViewMode::GroupFullscreen));
        QTRY_VERIFY(!dock->group(left));
        QVERIFY(dock->group(right));
        QVERIFY(!shown(top));

        // Pane fullscreen shows only the active pane of the active tab.
        QVERIFY(manager.focusPane(a, bottom));
        QVERIFY(manager.setViewMode(window, ViewMode::PaneFullscreen));
        QTRY_VERIFY(shown(bottom));
        QVERIFY(!shown(top));
        QVERIFY(!dock->group(right));

        // Zen hides the header row and the strips.
        QVERIFY(manager.setZenMode(window, true));
        QTRY_VERIFY(!chrome->isVisible());
        QTRY_VERIFY(dock->group(left)->stripRect().isEmpty());

        // Back to normal: the window, both groups and both panes return.
        QVERIFY(manager.setViewMode(window, ViewMode::Normal));
        QTRY_VERIFY(!(quickWindow->windowStates() & Qt::WindowFullScreen));
        QTRY_VERIFY(chrome->isVisible() && dock->group(left) && dock->group(right));
        QTRY_VERIFY(shown(top) && shown(bottom));
        QVERIFY(!dock->group(left)->stripRect().isEmpty());

        // Leaving full screen some other way ends the view mode.
        QVERIFY(manager.setViewMode(window, ViewMode::WindowFullscreen));
        QTRY_VERIFY(quickWindow->windowStates() & Qt::WindowFullScreen);
        quickWindow->showNormal();
        QTRY_COMPARE(manager.viewMode(window), ViewMode::Normal);

        // A window with one tab hides its strip when the behavior says so.
        auto behavior = manager.behavior(); behavior.hideSingleTabFloating = true; manager.setBehavior(behavior);
        const auto floating = manager.createWindow(WindowKind::Floating, {QStringLiteral("Single"), QSize(320, 240)}, window);
        const auto single = manager.addTab(floating, named(QStringLiteral("Single")));
        QTRY_VERIFY(workspace.window(floating));
        auto* floatingDock = workspace.window(floating)->contentItem()->findChild<quick::DockSurfaceItem*>();
        QTRY_VERIFY(floatingDock->group(manager.model().groupForTab(single)));
        QTRY_VERIFY(floatingDock->group(manager.model().groupForTab(single))->stripRect().isEmpty());

        // Titles name the active tab, and a floating window says what it is.
        QTRY_COMPARE(workspace.window(floating)->title(), QStringLiteral("Single — Single · Floating window"));
        QVERIFY(manager.renameTab(single, QStringLiteral("Renamed")));
        QTRY_COMPARE(workspace.window(floating)->title(), QStringLiteral("Renamed — Single · Floating window"));
        QVERIFY(manager.activateTab(a));
        QTRY_COMPARE(quickWindow->title(), QStringLiteral("A — View"));
    }

    void theLayoutSurvivesSavingAndRestoring()
    {
        QJsonObject saved;
        WindowId window;
        TabId first, second;
        QRect placed;
        {
            WindowManager manager(WindowManager::Presentation::External);
            QuickWorkspace workspace(manager);
            workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
            window = manager.createWindow(WindowKind::Workbench, {QStringLiteral("Saved"), QSize(560, 380)});
            first = manager.addTab(window, named(QStringLiteral("First")));
            second = manager.addTab(window, named(QStringLiteral("Second")));
            QTRY_VERIFY(workspace.window(window));
            auto* quickWindow = workspace.window(window);
            QVERIFY(QTest::qWaitForWindowExposed(quickWindow));
            quickWindow->setGeometry(QRect(quickWindow->position() + QPoint(30, 20), QSize(600, 400)));
            QTRY_COMPARE(quickWindow->size(), QSize(600, 400));
            placed = quickWindow->geometry();
            saved = manager.saveLayout();
        }
        const auto windows = saved.value("windows").toArray();
        QCOMPARE(windows.size(), 1);
        QCOMPARE(windows.first().toObject().value("width").toInt(), 600);
        // A toolbar layout the Quick windows do not show yet is kept.
        auto record = windows.first().toObject();
        const QJsonObject strips{{"locked", true},
            {"rows", QJsonArray{QJsonValue(QJsonArray{QStringLiteral("menu"), QStringLiteral("workspace"), QStringLiteral("title"), QStringLiteral("caption-controls")})}},
            {"strips", QJsonObject{{"menu", QJsonObject{}}, {"workspace", QJsonObject{}}, {"title", QJsonObject{}}, {"caption-controls", QJsonObject{}}}}};
        QVERIFY(!record.contains("strips"));
        record.insert("strips", strips);
        saved.insert("windows", QJsonArray{record});

        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        int created = 0;
        workspace.setContentFactory([&created](const TabOptions&, PaneId) { ++created; return new QQuickItem; });
        const auto restored = manager.restoreLayout(saved);
        QVERIFY2(restored.ok, qPrintable(restored.error));
        QCOMPARE(manager.model().orderedTabs(window), (QVector<TabId>{first, second}));
        QTRY_VERIFY(workspace.window(window));
        auto* quickWindow = workspace.window(window);
        QVERIFY(QTest::qWaitForWindowExposed(quickWindow));
        QTRY_COMPARE(quickWindow->geometry(), placed);
        QTRY_COMPARE(created, 2);
        QCOMPARE(manager.saveLayout().value("windows").toArray().first().toObject().value("strips").toObject(), strips);
    }

    void shortcutsRunInTheirQuickWindow()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto window = manager.createWindow();
        manager.addTab(window, named(QStringLiteral("Only")));
        WindowId ran;
        manager.commands().add({QStringLiteral("test.shortcut"), QStringLiteral("Run"), QStringLiteral("test"),
            QKeySequence(QStringLiteral("Ctrl+Shift+F7")), {},
            [&ran](const CommandContext& context) { ran = context.window; return true; }});
        QTRY_VERIFY(workspace.window(window));
        auto* quickWindow = workspace.window(window);
        quickWindow->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(quickWindow));
        QTest::keyClick(quickWindow, Qt::Key_F7, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(ran, window);
    }

    void closingAWindowAsksTheManager()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto kept = manager.createWindow();
        manager.addTab(kept, named(QStringLiteral("Kept")));
        const auto closed = manager.createWindow();
        manager.addTab(closed, named(QStringLiteral("Closed")));
        QTRY_VERIFY(workspace.window(closed));
        QPointer<QQuickWindow> window = workspace.window(closed);
        QVERIFY(QTest::qWaitForWindowExposed(window.data()));

        window->close();
        QVERIFY(!manager.model().window(closed));
        QTRY_VERIFY(!window);
        QVERIFY(workspace.window(kept));
    }
};

QTEST_MAIN(QuickWorkspaceTest)
#include "quick_workspace_test.moc"
