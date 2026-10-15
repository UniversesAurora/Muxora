// SPDX-License-Identifier: MIT
#include <window_system/window_manager.hpp>
#include "chrome/workspace_window.hpp"
#include "docking/dock_surface.hpp"
#include <QApplication>
#include <QLabel>
#include <QMenu>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <memory>
using namespace window_system;

class TabGroupTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); }
    void newCompleteWindowStartsItsHostTabBeforeCloseEmptyPolicy()
    {
        WindowManager manager; const auto original=manager.createWindow(); manager.addTab(original,new QLabel("old"),{"old","notes",{}});
        auto behavior=manager.behavior();behavior.closeEmptyWorkbench=true;manager.setBehavior(behavior);
        int created=0; QVERIFY(manager.commands().add({"host.new","new","文件",{},{},[&](const CommandContext& c){++created;return !manager.addTab(c.window,new QLabel("new"),{"new","notes",{}},c.group).isNull();}}));
        QVERIFY(manager.setTabCreationCommands("host.new",{"host.new"})); QVERIFY(manager.execute("window.new",{},original));
        QCoreApplication::processEvents(); QCOMPARE(created,1); QCOMPARE(manager.model().windows().size(),2);
        const auto fresh=manager.model().windowOrder().last(); QVERIFY(fresh!=original);
        QCOMPARE(manager.model().orderedTabs(fresh).size(),1); QVERIFY(manager.windowWidget(fresh)->isVisible());
        QVERIFY(manager.model().tab(manager.model().orderedTabs(original).first()));
    }
    void newWindowCreationChecksFactoryLifetime()
    {
        auto manager=std::make_unique<WindowManager>(); const auto w=manager->createWindow(); manager->addTab(w,new QLabel("a"),{"a","notes",{}});
        QPointer<WindowManager> observed(manager.get()); QVERIFY(manager->commands().add({"host.new","new","文件",{},{},[&](const CommandContext&){manager.reset();return true;}}));
        QVERIFY(manager->setTabCreationCommands("host.new",{"host.new"})); QVERIFY(!manager->execute("window.new",{},w)); QVERIFY(!observed);
    }
    void colorCommandsRejectUnknownPresetsAndPersistSelections()
    {
        WindowManager manager;const auto w=manager.createWindow();const auto a=manager.addTab(w,new QLabel("a"),{"a","notes",{}});
        QVERIFY(manager.execute("tab.color.set",{{"color","red"}},w)); QCOMPARE(manager.model().tab(a)->colorId,QString("red"));
        const auto snapshot=manager.saveLayout(); QVERIFY(!manager.setTabColor(a,"missing")); QCOMPARE(manager.saveLayout(),snapshot);
        QVERIFY(manager.setTabColor(a,{})); QVERIFY(manager.model().tab(a)->colorId.isEmpty());
    }
    void groupMovementAndCloseUseTheCapturedGroup()
    {
        WindowManager manager;const auto first=manager.createWindow(),second=manager.createWindow();
        const auto a=manager.addTab(first,new QLabel("a"),{"a","notes",{}}),b=manager.addTab(first,new QLabel("b"),{"b","notes",{}});
        manager.addTab(second,new QLabel("target"),{"target","notes",{}}); const auto group=manager.model().groupForTab(a);
        QVERIFY(manager.execute("group.moveWindow",{{"targetWindow",second}},first)); QCOMPARE(manager.model().groupForTab(a),group);
        QCOMPARE(manager.model().group(group)->windowId,second); QCOMPARE(manager.model().groupForTab(b),group);
        QVERIFY(manager.execute("group.close",{},second)); QVERIFY(!manager.model().group(group)); QVERIFY(!manager.model().tab(a)); QVERIFY(!manager.model().tab(b));
    }
    void sortCommandsUseAllSixRecordedOrders_data()
    {
        QTest::addColumn<QString>("command"); QTest::addColumn<int>("sorting");
        const QStringList names{"alphabeticalAscending", "alphabeticalDescending", "openedAscending", "openedDescending", "visitedAscending", "visitedDescending"};
        for (int i = 0; i < names.size(); ++i) QTest::newRow(qPrintable(names[i])) << "group.sort." + names[i] << i;
    }
    void sortCommandsUseAllSixRecordedOrders()
    {
        QFETCH(QString, command); QFETCH(int, sorting);
        WindowManager manager; const auto window = manager.createWindow();
        const auto z = manager.addTab(window, new QLabel("z"), {"z","notes",{}});
        const auto a = manager.addTab(window, new QLabel("a"), {"a","notes",{}});
        const auto m = manager.addTab(window, new QLabel("m"), {"m","notes",{}});
        const auto n = manager.addTab(window, new QLabel("n"), {"n","notes",{}});
        const auto group = manager.model().groupForTab(z), pane = manager.model().tab(m)->activePane;
        QPointer<QWidget> content(manager.paneWidget(pane));
        QVERIFY(manager.activateTab(z)); QVERIFY(manager.activateTab(a));
        QVERIFY(manager.execute(command, {}, window));
        const QVector<QVector<TabId>> expected{{a,m,n,z}, {z,n,m,a}, {z,a,m,n}, {n,m,a,z}, {m,n,z,a}, {a,z,n,m}};
        QCOMPARE(manager.model().group(group)->tabs, expected[sorting]);
        QCOMPARE(manager.model().group(group)->activeTab, a); QCOMPARE(manager.paneWidget(pane), content.data());
        QCOMPARE(manager.model().groupForTab(m), group); QVERIFY(manager.model().validate());
    }
    void groupMovesPreserveAllLiveWidgets()
    {
        WindowManager manager; const auto first = manager.createWindow(), second = manager.createWindow();
        const auto a = manager.addTab(first, new QLabel("a"), {"a","notes",{}});
        const auto b = manager.addTab(first, new QLabel("b"), {"b","notes",{}});
        const auto target = manager.addTab(second, new QLabel("target"), {"target","notes",{}});
        const auto group = manager.model().groupForTab(a), destination = manager.model().groupForTab(target);
        const auto paneA = manager.model().tab(a)->activePane, paneB = manager.model().tab(b)->activePane;
        QPointer<QWidget> contentA(manager.paneWidget(paneA)), contentB(manager.paneWidget(paneB));
        QVERIFY(manager.moveGroup(group, destination, Placement::Left));
        QCOMPARE(manager.model().centralGroups(second), QVector<GroupId>({group,destination}));
        QCOMPARE(manager.model().group(group)->tabs, QVector<TabId>({a,b}));
        QVERIFY(manager.moveGroupToWindow(group, first));
        QCOMPARE(manager.paneWidget(paneA), contentA.data()); QCOMPARE(manager.paneWidget(paneB), contentB.data());
        QCOMPARE(manager.model().group(group)->windowId, first); QVERIFY(manager.model().validate());
    }
    void groupCloseRespectsGuardAndCopiedMembership()
    {
        WindowManager manager; const auto window = manager.createWindow();
        const auto a = manager.addTab(window, new QLabel("a"), {"a","notes",{}}), b = manager.addTab(window, new QLabel("b"), {"b","notes",{}});
        const auto group = manager.model().groupForTab(a); const auto original = manager.saveLayout();
        QVector<TabId> requested;
        manager.setCloseGuard([&](const QVector<TabId>& tabs) { requested = tabs; return false; });
        QVERIFY(!manager.closeGroup(group)); QCOMPARE(requested, QVector<TabId>({a,b})); QCOMPARE(manager.saveLayout(), original);
        manager.setCloseGuard([&](const QVector<TabId>& tabs) { requested = tabs; return manager.setTabColor(a,"red"); });
        QVERIFY(manager.closeGroup(group)); QCOMPARE(requested, QVector<TabId>({a,b})); QVERIFY(!manager.model().group(group));
    }
    void colorSelectionSurvivesLayoutRestart()
    {
        WindowManager manager; const auto window = manager.createWindow();
        const auto tab = manager.addTab(window, new QLabel("content"), {"tab","notes",{}}); const auto pane = manager.model().tab(tab)->activePane;
        QVERIFY(manager.setTabColor(tab, "green")); const auto snapshot = manager.saveLayout();
        WindowManager restored; int calls = 0;
        restored.setContentFactory([&](const TabOptions&, PaneId) { ++calls; return new QLabel("restored"); });
        QVERIFY(restored.restoreLayout(snapshot)); QCOMPARE(calls,1); QCOMPARE(restored.model().tab(tab)->colorId,QString("green"));
        QVERIFY(restored.paneWidget(pane)); QCOMPARE(restored.model().groupForTab(tab),manager.model().groupForTab(tab));
    }
    void validColorIdMatchingOldCancelMarkerWorksThroughCommand()
    {
        WindowManager manager; const auto window = manager.createWindow(); const auto tab = manager.addTab(window,new QLabel("a"),{"a","notes",{}});
        auto appearance = manager.tabAppearance(); auto preset = appearance.colors.first(); preset.id = "__cancel__"; preset.name = "Custom";
        appearance.colors.append(preset); QVERIFY(manager.setTabAppearance(appearance));
        QVERIFY(manager.execute("tab.color.set",{{"color","__cancel__"}},window)); QCOMPARE(manager.model().tab(tab)->colorId,QString("__cancel__"));
        QVERIFY(manager.execute("tab.color.set",{{"color",QString()}},window)); QVERIFY(manager.model().tab(tab)->colorId.isEmpty());
    }
    void cancelColorPickerDoesNotChangeLayout()
    {
        WindowManager manager; const auto window = manager.createWindow(); const auto tab = manager.addTab(window,new QLabel("a"),{"a","notes",{}});
        QVERIFY(manager.setTabColor(tab,"red")); const auto before = manager.saveLayout(); QSignalSpy changed(&manager,&WindowManager::layoutChanged);
        bool sawPicker = false;
        QTimer::singleShot(0, &manager, [&] {
            for (auto* widget : QApplication::topLevelWidgets()) if (widget->objectName() == "TabColorPicker") {
                auto* menu = qobject_cast<QMenu*>(widget); sawPicker = menu && menu->isVisible(); if (menu) menu->close();
            }
        });
        QVERIFY(!manager.execute("tab.color.set",{},window)); QVERIFY(sawPicker);
        QCOMPARE(manager.model().tab(tab)->colorId,QString("red")); QCOMPARE(changed.size(),0); QCOMPARE(manager.saveLayout(),before);
    }
    void pinnedTabBarsAcceptAllSixSortCommands_data()
    {
        QTest::addColumn<int>("edge"); QTest::addColumn<int>("sorting");
        for (int edge = 0; edge < 4; ++edge) for (int sorting = 0; sorting < 6; ++sorting)
            QTest::newRow(qPrintable(QString("edge-%1-sort-%2").arg(edge).arg(sorting))) << edge << sorting;
    }
    void pinnedTabBarsAcceptAllSixSortCommands()
    {
        QFETCH(int,edge); QFETCH(int,sorting); WindowManager manager; const auto window = manager.createWindow();
        const auto z = manager.addTab(window,new QLabel("z"),{"z","notes",{}}); QVERIFY(manager.moveTabToEdge(z,Edge(edge),true));
        const auto group = manager.model().groupForTab(z);
        const auto a = manager.addTab(window,new QLabel("a"),{"a","notes",{}},group);
        const auto m = manager.addTab(window,new QLabel("m"),{"m","notes",{}},group);
        const auto n = manager.addTab(window,new QLabel("n"),{"n","notes",{}},group);
        const QVector<TabId> tabs{z,a,m,n}; QVector<PaneId> panes; QVector<QPointer<QWidget>> contents;
        for (const auto tab : tabs) { const auto pane = manager.model().tab(tab)->activePane; panes.append(pane); contents.append(manager.paneWidget(pane)); }
        QVERIFY(manager.activateTab(z)); QVERIFY(manager.activateTab(a));
        const QStringList suffixes{"alphabeticalAscending","alphabeticalDescending","openedAscending","openedDescending","visitedAscending","visitedDescending"};
        QVERIFY(manager.execute("group.sort."+suffixes[sorting],{},window));
        const QVector<QVector<TabId>> expected{{a,m,n,z},{z,n,m,a},{z,a,m,n},{n,m,a,z},{m,n,z,a},{a,z,n,m}};
        QCOMPARE(manager.model().group(group)->tabs,expected[sorting]); QCOMPARE(manager.model().group(group)->activeTab,a);
        QCOMPARE(manager.model().group(group)->location,GroupLocation::Pinned); QCOMPARE(manager.model().group(group)->edge,Edge(edge));
        for (qsizetype index = 0; index < tabs.size(); ++index) {
            QCOMPARE(manager.model().groupForTab(tabs[index]),group); QCOMPARE(manager.model().tab(tabs[index])->activePane,panes[index]);
            QVERIFY(contents[index]); QCOMPARE(manager.paneWidget(panes[index]),contents[index].data());
        }
        QVERIFY(manager.model().validate());
    }
    void autoHideRailDoesNotAdvertiseHiddenInternalGroupSort()
    {
        WindowManager manager; const auto window = manager.createWindow(); const auto tab = manager.addTab(window,new QLabel("a"),{"a","notes",{}});
        QVERIFY(manager.moveTabToEdge(tab,Edge::Left)); QVERIFY(manager.activateTab(tab)); const auto before = manager.saveLayout();
        QVERIFY(!manager.execute("group.sort.alphabeticalAscending",{},window)); QCOMPARE(manager.saveLayout(),before);
    }
    void wholeGroupMovementIgnoresPinnedNeighborsAndWraps_data()
    {
        QTest::addColumn<int>("direction");
        for (int i = 1; i < 5; ++i) QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void wholeGroupMovementIgnoresPinnedNeighborsAndWraps()
    {
        QFETCH(int,direction); WindowManager manager; const auto window = manager.createWindow();
        const auto first = manager.addTab(window,new QLabel("first"),{"first","notes",{}});
        const auto second = manager.addTab(window,new QLabel("second"),{"second","notes",{}});
        const bool horizontal = direction == int(Placement::Left) || direction == int(Placement::Right);
        QVERIFY(manager.moveTab(second,manager.model().groupForTab(first),horizontal ? Placement::Right : Placement::Bottom));
        const auto sidebar = manager.addTab(window,new QLabel("side"),{"side","notes",{}});
        const auto side = direction == int(Placement::Left) ? Edge::Left : direction == int(Placement::Right) ? Edge::Right
            : direction == int(Placement::Top) ? Edge::Top : Edge::Bottom;
        QVERIFY(manager.moveTabToEdge(sidebar,side,true));
        const auto source = direction == int(Placement::Left) || direction == int(Placement::Top) ? first : second;
        const auto other = source == first ? second : first;
        const auto group = manager.model().groupForTab(source), target = manager.model().groupForTab(other), pinned = manager.model().groupForTab(sidebar);
        const auto pane = manager.model().tab(source)->activePane; QPointer<QWidget> content(manager.paneWidget(pane));
        auto* view = static_cast<WorkspaceWindow*>(manager.windowWidget(window)); view->resize(1100,740); manager.showWindow(window);
        QTRY_COMPARE(view->surface()->groupRects().size(),3);
        const auto originalRects = view->surface()->groupRects();
        if (horizontal) QVERIFY(originalRects[pinned].center().x() != originalRects[group].center().x());
        else QVERIFY(originalRects[pinned].center().y() != originalRects[group].center().y());
        QVERIFY(manager.activateTab(source));
        const QString command = direction == int(Placement::Left) ? "left" : direction == int(Placement::Right) ? "right"
            : direction == int(Placement::Top) ? "up" : "down";
        QVERIFY(manager.execute("group.move."+command,{},window));
        QCOMPARE(manager.model().centralGroups(window), direction == int(Placement::Left) || direction == int(Placement::Top)
            ? QVector<GroupId>({target,group}) : QVector<GroupId>({group,target}));
        QCOMPARE(manager.model().group(pinned)->location,GroupLocation::Pinned);
        QCOMPARE(manager.model().groupForTab(source),group); QCOMPARE(manager.model().groupForTab(other),target);
        QCOMPARE(manager.model().groupForTab(sidebar),pinned); QCOMPARE(manager.paneWidget(pane),content.data()); QVERIFY(content);
        QVERIFY(manager.model().validate());
    }
    void canceledNamedCollectionLayerRegistersNoCommandsOrStorage()
    {
        WindowManager manager; const auto window = manager.createWindow(); manager.addTab(window,new QLabel("a"),{"a","notes",{}});
        for (const auto& command : manager.commands().commands()) QVERIFY(!command.id.startsWith("collection."));
        const auto snapshot = manager.saveLayout(); QVERIFY(!snapshot.contains("savedCollections"));
        QVERIFY(!snapshot.value("model").toObject().contains("collections"));
    }
};
QTEST_MAIN(TabGroupTest)
#include "tab_group_test.moc"
