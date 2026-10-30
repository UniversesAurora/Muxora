// SPDX-License-Identifier: MIT
// Phase 10.3: the Quick workspace builds its windows from an External
// manager's notifications. Every model window gets a Quick window, the
// active tab's panes show their content, and pane content keeps its identity
// while its tab moves between windows.
#include "quick/button_item.hpp"
#include "quick/dock_surface_item.hpp"
#include "quick/group_item.hpp"
#include "quick/peek_item.hpp"
#include "quick/rail_button_item.hpp"
#include "quick/rail_item.hpp"
#include "quick/window_chrome_item.hpp"
#include <window_system/quick_workspace.hpp>
#include <window_system/window_manager.hpp>

#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

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
        QCOMPARE(quickWindow->title(), QStringLiteral("Header"));
        auto* chrome = quickWindow->contentItem()->findChild<quick::WindowChromeItem*>();
        auto* dock = quickWindow->contentItem()->findChild<quick::DockSurfaceItem*>();
        QVERIFY(chrome && dock);
        QCOMPARE(int(chrome->height()), quick::WindowChromeItem::rowHeight());
        QCOMPARE(int(dock->y()), quick::WindowChromeItem::rowHeight());
        QCOMPARE(int(dock->height()), quickWindow->height() - quick::WindowChromeItem::rowHeight());
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
        QTRY_COMPARE(int(dock->height()), quickWindow->height() - quick::WindowChromeItem::rowHeight());
        click(maximize);
        QTRY_VERIFY(!(quickWindow->windowStates() & Qt::WindowMaximized));
        click(close);
        QTRY_VERIFY(!manager.model().window(window));
        QTRY_VERIFY(!quickWindow);
        QVERIFY(workspace.window(kept));
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
