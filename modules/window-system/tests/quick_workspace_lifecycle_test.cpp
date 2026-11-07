// SPDX-License-Identifier: MIT
// Content lifetime during reentrant layout and repeated window transitions.
// Dimensions: ratio command/surface resize, tab/group removal.
#include "quick/dock_surface_item.hpp"
#include "quick/tab_bar_item.hpp"
#include <window_system/quick_workspace.hpp>
#include <window_system/window_manager.hpp>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <utility>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

using namespace window_system;

class QuickWorkspaceLifecycleTest final : public QObject {
    Q_OBJECT
private slots:
    void repeatedMovesReleaseContentBarsAndNativeWindows()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQuick; TextEdit {}", QUrl());
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        int creations = 0;
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([&](const TabOptions& options, PaneId) {
            ++creations;
            auto* item = qobject_cast<QQuickItem*>(component.create());
            if (item) item->setProperty("text", options.title == QStringLiteral("Temporary")
                ? QString(16384, QLatin1Char('x')) : options.title);
            return item;
        });
        const auto project = [&] {
            QCoreApplication::sendPostedEvents(&workspace, QEvent::MetaCall);
            for (const auto id : manager.model().windowOrder()) if (auto* view = workspace.window(id))
                for (auto* bar : view->contentItem()->findChildren<quick::TabBarItem*>()) bar->setAnimationPolicy(false);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        };
        const auto root = manager.createWindow();
        const auto anchor = manager.addTab(root, {QStringLiteral("Anchor")});
        const auto anchorPane = manager.model().tab(anchor)->activePane;
        const auto anchorGroup = manager.model().groupForTab(anchor);
        QTRY_VERIFY(workspace.content(anchorPane));
        QPointer<QQuickItem> anchorContent = workspace.content(anchorPane);
        QPointer<QQuickWindow> rootView = workspace.window(root);
        QVERIFY(QTest::qWaitForWindowExposed(rootView));
        project();
        qsizetype baselineObjects = 0;
        for (int iteration = 0; iteration < 28; ++iteration) {
            const auto tab = manager.addTab(root, {QStringLiteral("Temporary")}, anchorGroup);
            const auto pane = manager.model().tab(tab)->activePane;
            const auto split = manager.splitPane(tab, pane, Placement::Right);
            QVERIFY(!split.isNull());
            project();
            QPointer<QQuickItem> content = workspace.content(pane), second = workspace.content(split);
            QVERIFY(content && second);
            const auto factoryCalls = creations;
            QPointer<QQuickItem> top = new QQuickItem, bottom = new QQuickItem;
            top->setImplicitHeight(20); bottom->setImplicitHeight(20);
            QVERIFY(workspace.setPaneBar(pane, top, true));
            QVERIFY(workspace.setPaneBar(pane, bottom, false));
            project();
            QVERIFY(manager.moveTab(tab, anchorGroup, Placement::Right)); project();
            QVERIFY(manager.moveTabToEdge(tab, Edge(iteration % 4))); project();
            auto* dock = rootView->contentItem()->findChild<quick::DockSurfaceItem*>();
            QVERIFY(dock);
            dock->showPeek(manager.model().groupForTab(tab), tab); project();
            QVERIFY(manager.pinTab(tab, true)); project();
            QVERIFY(manager.pinTab(tab, false)); project();
            QVERIFY(manager.moveTab(tab, anchorGroup)); project();
            const auto floating = manager.detachTab(tab); project();
            QVERIFY(!floating.isNull());
            QPointer<QQuickWindow> retired = workspace.window(floating);
            QVERIFY(retired);
            QVERIFY(QTest::qWaitForWindowExposed(retired));
            QCOMPARE(content->window(), retired.data());
#ifdef Q_OS_WIN
            const auto handle = reinterpret_cast<HWND>(retired->winId());
            QVERIFY(IsWindow(handle));
#endif
            QVERIFY(manager.promoteWindow(floating)); project();
            QCOMPARE(workspace.window(floating), retired.data());
            QVERIFY(manager.demoteWindow(floating, root)); project();
            QCOMPARE(workspace.window(floating), retired.data());
            QVERIFY(manager.moveAllTabs(floating, anchorGroup)); project();
            QCOMPARE(workspace.content(pane), content.data());
            QCOMPARE(content->property("text").toString().size(), 16384);
            QCOMPARE(creations, factoryCalls);
            QVERIFY(manager.closeTab(tab)); project();
            QVERIFY(manager.activateTab(anchor)); project();
            QTRY_VERIFY(!content && !second && !top && !bottom && !retired);
#ifdef Q_OS_WIN
            QVERIFY(!IsWindow(handle));
#endif
            QCOMPARE(workspace.content(anchorPane), anchorContent.data());
            QCOMPARE(manager.model().windows().size(), 1);
            QCOMPARE(manager.model().groups().size(), 1);
            QCOMPARE(manager.model().tabs().size(), 1);
            const auto objects = rootView->findChildren<QObject*>().size();
            if (iteration == 3) baselineObjects = objects;
            else if (iteration > 3) QCOMPARE(objects, baselineObjects);
        }
        QVERIFY(manager.requestCloseAllWindows()); project();
        QTRY_VERIFY(!rootView && !anchorContent);
        QVERIFY(manager.model().windows().isEmpty());
        QVERIFY(manager.model().tabs().isEmpty());
    }

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
