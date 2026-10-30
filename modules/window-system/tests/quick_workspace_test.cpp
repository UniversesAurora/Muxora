// SPDX-License-Identifier: MIT
// Phase 10.3: the Quick workspace builds its windows from an External
// manager's notifications. Every model window gets a Quick window, the
// active tab's panes show their content, and pane content keeps its identity
// while its tab moves between windows.
#include "quick/group_item.hpp"
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
