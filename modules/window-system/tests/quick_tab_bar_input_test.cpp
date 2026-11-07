// SPDX-License-Identifier: MIT
// Quick tab-strip adapter: actual input and frame clock use the shared state.
// Dimensions: horizontal/right placement x click/key/close x timed activation.
// No static appearance checks.
#include "quick/tab_bar_item.hpp"
#include "quick/dock_surface_item.hpp"
#include "quick/group_item.hpp"
#include <window_system/quick_workspace.hpp>
#include <window_system/window_manager.hpp>
#include <QContextMenuEvent>
#include <QPointer>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

using namespace window_system;
using namespace window_system::quick;

class QuickTabBarInputTest final : public QObject {
    Q_OBJECT
private slots:
    void stickyTabsSurviveRangeClosesAndUnpinFromTheirQuickAction()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        auto behavior = manager.behavior();
        behavior.newTabPosition = NewTabPosition::Beginning;
        manager.setBehavior(behavior);
        const auto window = manager.createWindow();
        QVector<TabId> tabs;
        for (const auto* name : {"A", "B", "C", "D"}) tabs << manager.addTab(window, {QString::fromLatin1(name)});
        const auto group = manager.model().groupForTab(tabs[0]);
        const auto contextFor = [&](TabId tab) {
            auto context = manager.context(window);
            context.tab = tab;
            context.group = manager.model().groupForTab(tab);
            context.pane = manager.model().tab(tab)->activePane;
            return context;
        };
        auto context = contextFor(tabs[2]);
        QVERIFY(manager.commands().find("tab.stick")->enabled(context));
        QVERIFY(!manager.commands().find("tab.unstick")->enabled(context));
        QVERIFY(manager.executeInContext("tab.stick", context));
        QVERIFY(manager.isTabSticky(tabs[2]));
        QCOMPARE(manager.model().group(group)->tabs.first(), tabs[2]);
        QVERIFY(!manager.commands().find("tab.stick")->enabled(context));
        QVERIFY(manager.commands().find("tab.unstick")->enabled(context));
        const auto created = manager.addTab(window, {QStringLiteral("E")});
        QCOMPARE(manager.model().group(group)->tabs.indexOf(created), 1);
        context = contextFor(tabs[0]);
        QVERIFY(manager.executeInContext("workspace.closeTabs.others", context));
        QCOMPARE(manager.model().group(group)->tabs, (QVector<TabId>{tabs[2], tabs[0]}));
        QVERIFY(manager.executeInContext("workspace.closeTabs.all", context));
        QCOMPARE(manager.model().group(group)->tabs, (QVector<TabId>{tabs[2]}));
        QVERIFY(!manager.commands().find("workspace.closeTabs.all")->enabled(contextFor(tabs[2])));
        const auto restoredTab = manager.addTab(window, {QStringLiteral("F")});
        QTRY_VERIFY(workspace.window(window));
        WindowManager restored(WindowManager::Presentation::External);
        QuickWorkspace restoredWorkspace(restored);
        restoredWorkspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        QVERIFY(restored.restoreLayout(manager.saveLayout()));
        QVERIFY(restored.isTabSticky(tabs[2]));
        QCOMPARE(restored.model().group(group)->tabs, (QVector<TabId>{tabs[2], restoredTab}));
        auto* view = workspace.window(window);
        auto* dock = view->contentItem()->findChild<DockSurfaceItem*>();
        QVERIFY(dock && dock->group(group));
        auto* bar = dock->group(group)->tabBar();
        auto& controller = bar->controller();
        QCOMPARE(controller.tabData(0).toUuid(), tabs[2]);
        const auto action = bar->mapToScene(controller.actionRect(0).center()).toPoint();
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, action);
        QTRY_VERIFY(!manager.isTabSticky(tabs[2]));
        QVERIFY(manager.model().tab(tabs[2]));
        QVERIFY(manager.model().validate());
    }

    void inputsAndFramesDriveTheSharedController_data()
    {
        QTest::addColumn<bool>("vertical");
        QTest::newRow("horizontal") << false;
        QTest::newRow("right") << true;
    }

    void inputsAndFramesDriveTheSharedController()
    {
        QFETCH(bool, vertical);
        QQuickWindow window;
        auto format = window.format(); format.setSamples(4); window.setFormat(format);
        window.resize(600, 420);
        auto* bar = new TabBarItem(defaultTheme(), window.contentItem());
        bar->setAnimationPolicy(false);
        auto& controller = bar->controller();
        controller.setAppearance({}, vertical ? TabBarPlacement::Right : TabBarPlacement::Top);
        controller.setAvailableSize(vertical ? QSize(200, 420) : QSize(600, 420));
        for (int index = 0; index < 3; ++index) {
            const auto tab = controller.addTab({}, QStringLiteral("Workspace %1").arg(index));
            controller.setTabData(tab, QUuid::createUuid());
        }
        bar->setSize(controller.displaySize(controller.availableSize()));
        bar->synchronize();
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(bar->renderingAccepted());
        QSignalSpy activated(bar, &TabBarItem::currentChanged);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, controller.titleRect(1).center());
        QCOMPARE(controller.currentIndex(), 1);
        QVERIFY(!activated.isEmpty());
        int menus = 0;
        int expectedMenuTab = 0;
        connect(bar, &TabBarItem::contextMenuRequested, &window, [&](int tab, int folder, QPoint) {
            QCOMPARE(tab, expectedMenuTab); QCOMPARE(folder, -1); ++menus;
        });
        bar->setPosition({45, 60});
        const QPoint point = controller.titleRect(0).center();
        QContextMenuEvent menu(QContextMenuEvent::Mouse, bar->mapToScene(point).toPoint(), bar->mapToGlobal(point).toPoint());
        QCoreApplication::sendEvent(&window, &menu);
        QVERIFY(menu.isAccepted());
        QCOMPARE(menus, 1);
        QCOMPARE(controller.currentIndex(), 1);
        bar->setPopupOpen(false);
        bar->forceActiveFocus();
        expectedMenuTab = 1;
        QContextMenuEvent keyboardMenu(QContextMenuEvent::Keyboard, {}, {});
        QCoreApplication::sendEvent(&window, &keyboardMenu);
        QCOMPARE(menus, 2);
        QCOMPARE(controller.currentIndex(), 1);
        bar->setPopupOpen(false);
        bar->setPosition({});
        bar->forceActiveFocus();
        QTest::keyClick(&window, vertical ? Qt::Key_Down : Qt::Key_Right);
        QCOMPARE(controller.currentIndex(), 2);
        connect(bar, &TabBarItem::actionRequested, &window, [&](TabPointerDecision decision) {
            if (decision.action != TabPointerDecision::Action::CloseTab) return;
            controller.removeTab(decision.index); bar->synchronize();
        });
        QTest::mouseClick(&window, Qt::MiddleButton, Qt::NoModifier, controller.titleRect(0).center());
        QCOMPARE(controller.count(), 2);
        bar->setAnimationPolicy(true);
        controller.setCurrentIndex(0);
        bar->synchronize();
        QTRY_VERIFY(!controller.needsFrame());
        QVERIFY(bar->renderingAccepted());
        QPointer<TabBarItem> lifetime = bar;
        connect(bar, &TabBarItem::currentChanged, &window, [bar] { delete bar; });
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, controller.titleRect(1).center());
        QVERIFY(lifetime.isNull());
    }
};

QTEST_MAIN(QuickTabBarInputTest)
#include "quick_tab_bar_input_test.moc"
