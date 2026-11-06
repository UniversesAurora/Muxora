// SPDX-License-Identifier: MIT
// Phase 10.3: the Quick workspace builds its windows from an External
// manager's notifications. Every model window gets a Quick window, the
// active tab's panes show their content, and pane content keeps its identity
// while its tab moves between windows.
#include "quick/button_item.hpp"
#include "quick/command_strip_host_item.hpp"
#include "quick/status_bar_item.hpp"
#include "quick/dock_surface_item.hpp"
#include "quick/group_item.hpp"
#include "quick/peek_item.hpp"
#include "quick/rail_button_item.hpp"
#include "quick/rail_item.hpp"
#include "quick/tab_bar_item.hpp"
#include "quick/window_chrome_item.hpp"
#include <window_system/interface_appearance.hpp>
#include <window_system/quick_workspace.hpp>
#include <window_system/window_manager.hpp>

#include <QJsonArray>
#include <algorithm>
#include <QMenuBar>
#include <QWidget>
#include <QJsonObject>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QtMath>

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

// What a Quick window holds at the moment it is shown.
class ShowProbe final : public QObject {
public:
    struct Shown { int groups = 0; bool laidOut = false; };
    QVector<Shown> shown;
protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        auto* window = qobject_cast<QQuickWindow*>(watched);
        if (event->type() != QEvent::Show || !window) return false;
        const auto groups = window->contentItem()->findChildren<quick::GroupItem*>();
        Shown state{int(groups.size()), !groups.isEmpty()};
        for (auto* group : groups)
            state.laidOut = state.laidOut && group->width() > 0 && group->height() > 0
                && group->tabBar()->width() > 0 && group->tabBar()->height() > 0;
        shown.append(state);
        return false;
    }
};

// The first visible descendant whose class name contains `type`, with its
// rectangle in the window.
QRect widgetIn(QWidget* window, const char* type, const QString& name = {})
{
    for (auto* child : window->findChildren<QWidget*>())
        if (child->isVisible() && QByteArray(child->metaObject()->className()).contains(type) && (name.isEmpty() || child->objectName() == name))
            return QRect(child->mapTo(window, QPoint()), child->size());
    return {};
}
QRect itemIn(QQuickWindow* window, const char* type, const QString& name = {})
{
    QList<QQuickItem*> items{window->contentItem()};
    while (!items.isEmpty()) {
        auto* item = items.takeFirst();
        if (item->isVisible() && QByteArray(item->metaObject()->className()).contains(type) && (name.isEmpty() || item->objectName() == name))
            return QRectF(item->mapToScene(QPointF()), item->size()).toRect();
        items += item->childItems();
    }
    return {};
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
    // A new window shows its groups laid out, as a widget window shows its
    // laid-out children: its first frame is not a layout in progress whose
    // later steps (a selected tab joining its card) would then animate.
    void windowsShowWithTheirGroupsLaidOut()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        ShowProbe probe;
        qApp->installEventFilter(&probe);
        const auto window = manager.createWindow(WindowKind::Workbench, {QStringLiteral("Main"), QSize(900, 700)});
        const auto first = manager.addTab(window, named(QStringLiteral("First")));
        const auto second = manager.addTab(window, named(QStringLiteral("Second")));
        QVERIFY(manager.moveTab(second, manager.model().groupForTab(first), Placement::Right));
        QTRY_VERIFY(workspace.window(window) && workspace.window(window)->isVisible());
        qApp->removeEventFilter(&probe);
        QVERIFY(!probe.shown.isEmpty());
        QCOMPARE(probe.shown.first().groups, 2);
        QVERIFY(probe.shown.first().laidOut);
    }

    // The rows sit where the Widgets window puts them: below a one-pixel
    // border along the top of a normal window, none when maximized; menu
    // titles as high as QMenuBar makes them; toolbar buttons as wide as
    // QToolButton, with the menu strip keeping its options tail.
    void rowsMatchTheWidgetsWindow()
    {
        InterfaceAppearanceController appearance;
        WindowManager widgets;
        const auto widgetsWindow = widgets.createWindow(WindowKind::Workbench, {QStringLiteral("Widgets"), QSize(900, 700)});
        widgets.addTab(widgetsWindow, new QWidget, named(QStringLiteral("First")));
        widgets.showWindow(widgetsWindow);
        auto* top = widgets.windowWidget(widgetsWindow);
        QVERIFY(QTest::qWaitForWindowExposed(top));
        QTRY_VERIFY(widgetIn(top, "DockSurface").height() > 0 && widgetIn(top, "TabBar").height() > 0);
        auto* menuBar = top->findChild<QMenuBar*>(QStringLiteral("MenuStrip"));
        QVERIFY(menuBar && !menuBar->actions().isEmpty());

        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto window = manager.createWindow(WindowKind::Workbench, {QStringLiteral("Quick"), QSize(900, 700)});
        manager.addTab(window, named(QStringLiteral("First")));
        QTRY_VERIFY(workspace.window(window) && workspace.window(window)->isVisible());
        auto* view = workspace.window(window);
        QVERIFY(QTest::qWaitForWindowExposed(view));
        QTRY_VERIFY(itemIn(view, "TabBarItem").height() > 0);
        QCOMPARE(itemIn(view, "ApplicationMenuBar", QStringLiteral("MenuStrip")).topLeft(), widgetIn(top, "QMenuBar", QStringLiteral("MenuStrip")).topLeft());
        QCOMPARE(itemIn(view, "DockSurfaceItem").top(), widgetIn(top, "DockSurface").top());
        QCOMPARE(itemIn(view, "TabBarItem"), widgetIn(top, "TabBar"));
        QCOMPARE(itemIn(view, "MenuBarItem", QStringLiteral("file")).size(), menuBar->actionGeometry(menuBar->actions().first()).size());

        // Toolbar buttons and the strips' option tails, the menu strip's too.
        for (const auto* name : {"StripCommandButton", "StripOverflowButton"}) {
            QVector<QRect> widgetRects, quickRects;
            for (auto* child : top->findChildren<QWidget*>(QString::fromLatin1(name)))
                if (child->isVisible()) widgetRects.append(QRect(child->mapTo(top, QPoint()), child->size()));
            QList<QQuickItem*> items{view->contentItem()};
            while (!items.isEmpty()) {
                auto* item = items.takeFirst();
                if (item->isVisible() && item->objectName() == QLatin1String(name))
                    quickRects.append(QRectF(item->mapToScene(QPointF()), item->size()).toRect());
                items += item->childItems();
            }
            const auto byPosition = [](const QRect& a, const QRect& b) { return std::pair(a.y(), a.x()) < std::pair(b.y(), b.x()); };
            std::sort(widgetRects.begin(), widgetRects.end(), byPosition);
            std::sort(quickRects.begin(), quickRects.end(), byPosition);
            QVERIFY(!widgetRects.isEmpty());
            QCOMPARE(quickRects, widgetRects);
        }

        const int toolbars = itemIn(view, "CommandStripHostItem", QStringLiteral("CommandToolbars")).height();
        QCOMPARE(itemIn(view, "DockSurfaceItem").top(), 1 + toolbars);
        view->showMaximized();
        QTRY_COMPARE(itemIn(view, "DockSurfaceItem").top(), toolbars);
    }

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
        QVERIFY(manager.isWindowMinimized(first));

        // Scripts move, resize and read the state of a Quick window as they
        // did a Widgets one: the frame's position, the content's size.
        QVERIFY(manager.resizeWindow(second, QSize(500, 360)));
        QTRY_COMPARE(two->size(), QSize(500, 360));
        const QPoint place = two->frameGeometry().topLeft() + QPoint(30, 20);
        QVERIFY(manager.moveWindow(second, place));
        QTRY_COMPARE(two->frameGeometry().topLeft(), place);
        QVERIFY(!manager.isWindowMinimized(second) && !manager.isWindowMaximized(second));
        QVERIFY(!manager.moveWindow(WindowId::createUuid(), place));
        // The window state menu's commands reach the Quick window.
        QVERIFY(manager.execute(QStringLiteral("window.maximize"), {}, second));
        QTRY_VERIFY(manager.isWindowMaximized(second));
        QVERIFY(manager.execute(QStringLiteral("window.restore"), {}, second));
        QTRY_VERIFY(!manager.isWindowMaximized(second));
        QVERIFY(manager.execute(QStringLiteral("window.minimize"), {}, second));
        QTRY_VERIFY(manager.isWindowMinimized(second));
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

    // Directional commands pick neighbours from the Quick window's geometry.
    void directionalCommandsReachTheNeighbourOnScreen()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto window = manager.createWindow();
        const auto left = manager.addTab(window, named(QStringLiteral("Left")));
        const auto right = manager.addTab(window, named(QStringLiteral("Right")));
        QVERIFY(manager.moveTab(right, manager.model().groupForTab(left), Placement::Right));
        const auto leftGroup = manager.model().groupForTab(left), rightGroup = manager.model().groupForTab(right);
        const auto upper = manager.model().tab(left)->activePane;
        const auto lower = manager.splitPane(left, upper, Placement::Bottom);
        QVERIFY(manager.activateTab(left));
        QTRY_VERIFY(workspace.window(window));
        auto* quickWindow = workspace.window(window);
        quickWindow->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(quickWindow));
        const auto other = manager.model().tab(right)->activePane;
        QTRY_VERIFY(shownIn(workspace.content(lower), quickWindow) && shownIn(workspace.content(other), quickWindow));

        QVERIFY(manager.focusPane(left, lower));
        QTest::keyClick(quickWindow, Qt::Key_Up, Qt::AltModifier);
        QCOMPARE(manager.model().tab(left)->activePane, upper);
        QTest::keyClick(quickWindow, Qt::Key_Right, Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(manager.model().window(window)->activeGroup, rightGroup);
        QTest::keyClick(quickWindow, Qt::Key_Left, Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(manager.model().window(window)->activeGroup, leftGroup);

        QVERIFY(manager.execute(QStringLiteral("pane.swap.down"), {}, window));
        QCOMPARE(manager.model().panes(left), (QVector<PaneId>{lower, upper}));
        QVERIFY(manager.execute(QStringLiteral("group.swap.right"), {}, window));
        QCOMPARE(manager.model().centralGroups(window), (QVector<GroupId>{rightGroup, leftGroup}));
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
