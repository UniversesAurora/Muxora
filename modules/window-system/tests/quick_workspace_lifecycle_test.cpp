// SPDX-License-Identifier: MIT
// Content lifetime during reentrant layout and repeated window transitions.
// Dimensions: ratio command/surface resize, tab/group removal.
#include "quick/dock_surface_item.hpp"
#include <window_system/quick_workspace.hpp>
#include <window_system/window_manager.hpp>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <utility>

using namespace window_system;

class QuickWorkspaceLifecycleTest final : public QObject {
    Q_OBJECT
private slots:
    void contentResizeMayRemoveAnotherTabOrGroup_data()
    {
        QTest::addColumn<bool>("command");
        QTest::addColumn<bool>("removeGroup");
        for (const bool command : {true, false}) for (const bool group : {false, true})
            QTest::newRow(qPrintable(QStringLiteral("%1-%2").arg(command ? "ratio-command" : "surface-resize",
                group ? "remove-group" : "close-tab"))) << command << group;
    }

    void contentResizeMayRemoveAnotherTabOrGroup()
    {
        QFETCH(bool, command);
        QFETCH(bool, removeGroup);
        bool fired = false;
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto window = manager.createWindow();
        const auto left = manager.addTab(window, {QStringLiteral("Left")});
        const auto right = manager.addTab(window, {QStringLiteral("Right")});
        QVERIFY(manager.moveTab(right, manager.model().groupForTab(left), Placement::Right));
        const auto extra = manager.addTab(window, {QStringLiteral("Extra")}, manager.model().groupForTab(right));
        if (removeGroup) QVERIFY(manager.closeTab(right));
        QVERIFY(manager.activateTab(left));
        const auto pane = manager.model().tab(left)->activePane;
        QTRY_VERIFY(workspace.content(pane));
        QPointer<QQuickItem> content = workspace.content(pane);
        auto* view = workspace.window(window);
        QVERIFY(QTest::qWaitForWindowExposed(view));
        auto* dock = view->contentItem()->findChild<quick::DockSurfaceItem*>();
        QVERIFY(dock);
        connect(content, &QQuickItem::widthChanged, &workspace, [&] {
            if (std::exchange(fired, true)) return;
            QVERIFY(manager.closeTab(extra));
        });
        const auto node = manager.model().window(window)->central->id;
        if (command) QVERIFY(manager.execute("layout.setRatio", {{"node", node}, {"ratio", .3}}, window));
        else dock->setWidth(dock->width() - 80);
        QTRY_VERIFY(fired);
        QVERIFY(!manager.model().tab(extra));
        QCOMPARE(manager.model().window(window)->central->isLeaf(), removeGroup);
        QTRY_VERIFY(content && content->isVisible());
        QCOMPARE(workspace.content(pane), content.data());
        QCOMPARE(content->window(), view);
        QVERIFY(manager.focusPane(left, pane));
        QTRY_VERIFY(content->hasFocus());
        QVERIFY(manager.model().validate());
    }
};

QTEST_MAIN(QuickWorkspaceLifecycleTest)
#include "quick_workspace_lifecycle_test.moc"
