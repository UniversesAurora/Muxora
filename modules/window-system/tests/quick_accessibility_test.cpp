// SPDX-License-Identifier: MIT
// Phase 10.4j: the accessibility tree, names, states, native accessible
// actions and ownership. No rendering or static appearance assertions.
#include "quick/button_item.hpp"
#include "quick/dock_surface_item.hpp"
#include "quick/group_item.hpp"
#include "quick/tab_bar_item.hpp"
#include "quick/rail_button_item.hpp"
#include "quick/rail_item.hpp"
#include "quick/window_chrome_item.hpp"
#include "quick_menu_test_support.hpp"
#include <window_system/theme.hpp>
#include <window_system/quick_workspace.hpp>
#include <window_system/window_manager.hpp>
#include <QAccessible>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

using namespace window_system;
using namespace quick_menu_test;

class QuickAccessibilityTest final : public QObject {
    Q_OBJECT
private slots:
    void init()
    {
        QTest::failOnWarning(QRegularExpression(".*(Unable to assign|TypeError|ReferenceError|Binding loop).*"));
    }

    void buttonActionsFollowStateAndReleaseWithTheirOwner()
    {
        QQuickWindow window;
        window.resize(500, 300);
        auto* button = new quick::ButtonItem(defaultTheme(), window.contentItem());
        button->setSize(QSizeF(80, 30));
        button->setToolTip(QStringLiteral("Toggle view\nCtrl+Alt+V"));
        button->setCheckable(true);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* accessible = QAccessible::queryAccessibleInterface(button);
        QVERIFY(accessible);
        QCOMPARE(accessible->role(), QAccessible::Button);
        QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Toggle view"));
        QCOMPARE(accessible->text(QAccessible::Description), button->toolTip());
        QVERIFY(accessible->state().checkable);
        QVERIFY(!accessible->state().checked);
        QVERIFY(accessible->state().focusable);
        button->setFocusPolicy(Qt::NoFocus);
        QVERIFY(!accessible->state().focusable);
        button->setActiveFocusOnTab(true);
        QVERIFY(accessible->state().focusable);
        // The native Quick tree must discover the C++ item, not just a
        // direct query by a test that already has its pointer.
        auto* parent = QAccessible::queryAccessibleInterface(&window);
        QVERIFY(parent && parent->indexOfChild(accessible) >= 0);
        auto* actions = accessible->actionInterface();
        QVERIFY(actions && actions->actionNames().contains(QAccessibleActionInterface::pressAction()));
        QSignalSpy clicked(button, &quick::ButtonItem::clicked);
        actions->doAction(QAccessibleActionInterface::pressAction());
        QCOMPARE(clicked.size(), 1);
        QVERIFY(button->isChecked() && accessible->state().checked);
        button->setEnabled(false);
        QVERIFY(accessible->state().disabled);
        actions->doAction(QAccessibleActionInterface::pressAction());
        QCOMPARE(clicked.size(), 1);
        button->setEnabled(true);
        button->setChecked(false);
        QVERIFY(!accessible->state().checked);
        button->setToolTip(QStringLiteral("Updated title"));
        QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Updated title"));
        const auto id = QAccessible::uniqueId(accessible);
        delete button;
        QVERIFY(!QAccessible::accessibleInterface(id));
    }

    void sidebarActionsAndKeyboardUseTheSameTarget()
    {
        QQuickWindow window;
        window.resize(500, 300);
        auto* rail = new quick::RailItem(Edge::Left, defaultTheme(), window.contentItem());
        rail->setSize(QSizeF(40, 300));
        QVector<quick::RailButtonItem*> buttons;
        int clicked = 0;
        for (int index = 0; index < 3; ++index) {
            auto* button = new quick::RailButtonItem(defaultTheme(), QUuid::createUuid(), QStringLiteral("Full sidebar title %1").arg(index), true, 24, rail);
            button->clicked = [button, &clicked] { ++clicked; button->setChecked(true); };
            rail->addButton(button);
            buttons.append(button);
        }
        window.show(); window.requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto* accessible = QAccessible::queryAccessibleInterface(buttons.first());
        QVERIFY(accessible);
        buttons.first()->setTitleClipping(true, 0);
        QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Full sidebar title 0"));
        QVERIFY(accessible->state().checkable && !accessible->state().checked);
        auto* actions = accessible->actionInterface();
        QVERIFY(actions);
        actions->doAction(QAccessibleActionInterface::pressAction());
        QCOMPARE(clicked, 1);
        QVERIFY(accessible->state().checked);
        buttons.first()->forceActiveFocus();
        QTest::keyClick(&window, Qt::Key_Space);
        QCOMPARE(clicked, 2);
        QTest::keyPress(&window, Qt::Key_Space);
        QTest::keyClick(&window, Qt::Key_Escape);
        QTest::keyRelease(&window, Qt::Key_Space);
        QCOMPARE(clicked, 2);
        buttons.at(1)->setEnabled(false);
        QTest::keyClick(&window, Qt::Key_Down);
        QTRY_VERIFY(buttons.at(2)->hasActiveFocus());
        QTest::keyClick(&window, Qt::Key_Up);
        QTRY_VERIFY(buttons.first()->hasActiveFocus());
    }

    void groupFocusVisitsControlsBeforeContentAndTitlesFollowState()
    {
        QQuickWindow window;
        window.resize(700, 400);
        auto page = std::make_unique<QQuickItem>();
        page->setFocusPolicy(Qt::TabFocus);
        auto* group = new quick::GroupItem(defaultTheme(), window.contentItem());
        group->setSize(window.size());
        group->addTab(page.get(), {}, QStringLiteral("Content"), QUuid::createUuid());
        group->setGroupName(QStringLiteral("Work"));
        auto* accessible = QAccessible::queryAccessibleInterface(group);
        QVERIFY(accessible);
        QCOMPARE(accessible->role(), QAccessible::Grouping);
        QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Work"));
        window.show(); window.requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        group->tabBar()->forceActiveFocus();
        for (const auto* name : {"NewTabButton", "NewTabTypeButton", "TabListButton"}) {
            auto* button = namedItem(group, QString::fromLatin1(name));
            QVERIFY(button && button->isVisible());
            QTest::keyClick(&window, Qt::Key_Tab);
            QTRY_VERIFY(button->hasActiveFocus());
        }
        QTest::keyClick(&window, Qt::Key_Tab);
        QTRY_VERIFY(page->hasActiveFocus());

        auto* title = new quick::WindowChromeItem(defaultTheme(), window.contentItem());
        title->setTitle(QStringLiteral("Complete window title"));
        auto* titleAccessible = QAccessible::queryAccessibleInterface(title);
        QVERIFY(titleAccessible);
        QCOMPARE(titleAccessible->role(), QAccessible::TitleBar);
        QCOMPARE(titleAccessible->text(QAccessible::Name), QStringLiteral("Complete window title"));
        QQuickItem* maximize = nullptr;
        for (auto* child : title->captionContent()->childItems())
            if (child->property("role").toString() == QStringLiteral("maximize")) maximize = child;
        QVERIFY(maximize);
        title->setExpanded(true);
        QCOMPARE(QAccessible::queryAccessibleInterface(maximize)->text(QAccessible::Name), QStringLiteral("Restore"));
        title->setExpanded(false);
        QCOMPARE(QAccessible::queryAccessibleInterface(maximize)->text(QAccessible::Name), QStringLiteral("Maximize"));
    }

    void tabAndFolderActionsFollowStableIdentities()
    {
        QAccessible::Id listId = 0, folderNodeId = 0;
        {
            WindowManager manager(WindowManager::Presentation::External);
            QuickWorkspace workspace(manager);
            workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
            const auto window = manager.createWindow();
            const auto first = manager.addTab(window, {QStringLiteral("First")});
            const auto second = manager.addTab(window, {QStringLiteral("Second")});
            const auto target = manager.addTab(window, {QStringLiteral("Full target title & text")});
            const auto folder = manager.createTabFolder({first, second}, QStringLiteral("Work"));
            QVERIFY(!folder.isNull());
            QVERIFY(manager.activateTab(first));
            QTRY_VERIFY(workspace.window(window));
            auto* view = workspace.window(window);
            QVERIFY(QTest::qWaitForWindowExposed(view));
            auto* dock = view->contentItem()->findChild<quick::DockSurfaceItem*>();
            QVERIFY(dock);
            const auto groupId = manager.model().groupForTab(first);
            auto* group = dock->group(groupId);
            QVERIFY(group);
            auto* bar = group->tabBar();
            auto* list = QAccessible::queryAccessibleInterface(bar);
            QVERIFY(list && list->parent() == QAccessible::queryAccessibleInterface(group));
            QCOMPARE(list->role(), QAccessible::PageTabList);
            QCOMPARE(list->childCount(), 4);
            auto* selection = list->selectionInterface();
            QVERIFY(selection && selection->selectedItemCount() == 1);
            auto* tab = list->child(2);
            auto* header = list->child(3);
            const auto targetId = QAccessible::uniqueId(tab);
            listId = QAccessible::uniqueId(list); folderNodeId = QAccessible::uniqueId(header);
            QCOMPARE(tab->text(QAccessible::Name), QStringLiteral("Full target title & text"));
            QVERIFY(selection->select(tab));
            QCOMPARE(manager.model().group(groupId)->activeTab, target);
            QVERIFY(tab->state().selected);
            QCOMPARE(selection->selectedItems(), QList<QAccessibleInterface*>{tab});
            QVERIFY(!selection->clear() && !selection->unselect(tab) && !selection->selectAll());
            QVERIFY(!selection->select(header));
            // Move past the folder as a whole; inserting into its run is a
            // different operation. Quick projects manager changes queued.
            QVERIFY(manager.reorderTab(target, true));
            QCOMPARE(manager.model().group(groupId)->tabs.first(), target);
            QTRY_COMPARE(bar->controller().indexOf(target), 0);
            QCOMPARE(list->child(0), tab);
            QCOMPARE(list->indexOfChild(tab), 0);
            QCOMPARE(QAccessible::uniqueId(tab), targetId);
            QVERIFY(manager.renameTab(target, QStringLiteral("Renamed target")));
            QTRY_COMPARE(tab->text(QAccessible::Name), QStringLiteral("Renamed target"));
            QCOMPARE(header->role(), QAccessible::Button);
            QVERIFY(header->text(QAccessible::Name).contains(QStringLiteral("Work")));
            QVERIFY(header->state().expandable && header->state().expanded);
            header->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
            QVERIFY(manager.model().folder(folder)->collapsed);
            QTRY_VERIFY(header->state().collapsed && !header->state().expanded);
            header->actionInterface()->doAction(QAccessibleActionInterface::toggleAction());
            QVERIFY(!manager.model().folder(folder)->collapsed);
            QTRY_VERIFY(header->state().expanded);
            QVERIFY(manager.closeTab(target));
            QTRY_VERIFY(!QAccessible::accessibleInterface(targetId));
            QCOMPARE(list->childCount(), 3);
            QCOMPARE(QAccessible::uniqueId(list->child(2)), folderNodeId);
        }
        QVERIFY(!QAccessible::accessibleInterface(listId));
        QVERIFY(!QAccessible::accessibleInterface(folderNodeId));
    }

    void tabActivationMayDestroyItsGroup()
    {
        QQuickWindow window;
        window.resize(500, 300);
        QPointer<quick::GroupItem> group = new quick::GroupItem(defaultTheme(), window.contentItem());
        group->setSize(window.size());
        group->addTab(nullptr, {}, QStringLiteral("First"), QUuid::createUuid());
        group->addTab(nullptr, {}, QStringLiteral("Second"), QUuid::createUuid());
        group->setCurrentIndex(0);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto* list = QAccessible::queryAccessibleInterface(group->tabBar());
        auto* tab = list->child(1);
        const auto id = QAccessible::uniqueId(tab);
        group->currentChanged = [&group](int) { delete group; };
        tab->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
        QVERIFY(!group);
        QVERIFY(!QAccessible::accessibleInterface(id));
    }
};

QTEST_MAIN(QuickAccessibilityTest)
#include "quick_accessibility_test.moc"
