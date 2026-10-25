// SPDX-License-Identifier: MIT
#include <window_system/window_manager.hpp>
#include "chrome/workspace_window.hpp"
#include "docking/dock_surface.hpp"
#include <QApplication>
#include <QDynamicPropertyChangeEvent>
#include <QLabel>
#include <QMenu>
#include "docking/tab_bar.hpp"
#include <QMouseEvent>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <memory>
#include <functional>
using namespace window_system;

namespace {
class ContentPropertyHook final : public QObject {
public:
    QByteArray property;
    std::function<void(QObject*)> callback;
    int calls = 0;
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (calls || event->type() != QEvent::DynamicPropertyChange
            || static_cast<QDynamicPropertyChangeEvent*>(event)->propertyName() != property) return false;
        ++calls;
        const QPointer<QObject> observed(watched);
        callback(watched);
        return observed.isNull();
    }
};
}

class TabGroupTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); }
    void newCompleteWindowStartsItsHostTabBeforeCloseEmptyPolicy()
    {
        WindowManager manager; const auto original=manager.createWindow(); manager.addTab(original,new QLabel("old"),{"old","notes",{}});
        auto behavior=manager.behavior();behavior.closeEmptyWorkbench=true;manager.setBehavior(behavior);
        int created=0; QVERIFY(manager.commands().add({"host.new","new","file",{},{},[&](const CommandContext& c){++created;return !manager.addTab(c.window,new QLabel("new"),{"new","notes",{}},c.group).isNull();}}));
        QVERIFY(manager.setTabCreationCommands("host.new",{"host.new"})); QVERIFY(manager.execute("window.new",{},original));
        QCoreApplication::processEvents(); QCOMPARE(created,1); QCOMPARE(manager.model().windows().size(),2);
        const auto fresh=manager.model().windowOrder().last(); QVERIFY(fresh!=original);
        QCOMPARE(manager.model().orderedTabs(fresh).size(),1); QVERIFY(manager.windowWidget(fresh)->isVisible());
        QVERIFY(manager.model().tab(manager.model().orderedTabs(original).first()));
    }
    void newWindowCreationChecksFactoryLifetime()
    {
        auto manager=std::make_unique<WindowManager>(); const auto w=manager->createWindow(); manager->addTab(w,new QLabel("a"),{"a","notes",{}});
        QPointer<WindowManager> observed(manager.get()); QVERIFY(manager->commands().add({"host.new","new","file",{},{},[&](const CommandContext&){manager.reset();return true;}}));
        QVERIFY(manager->setTabCreationCommands("host.new",{"host.new"})); QVERIFY(!manager->execute("window.new",{},w)); QVERIFY(!observed);
    }
    void adjacentPreparationHonorsParticipantLifetimes_data()
    {
        QTest::addColumn<QByteArray>("property"); QTest::addColumn<bool>("destroyManager");
        for (const auto property : {QByteArray("windowSystemManaged"), QByteArray("paneId")})
            for (const bool manager : {false, true})
                QTest::newRow(qPrintable(QString::fromLatin1(property) + (manager ? "-manager" : "-content"))) << property << manager;
    }
    void adjacentPreparationHonorsParticipantLifetimes()
    {
        QFETCH(QByteArray, property); QFETCH(bool, destroyManager);
        auto manager = std::make_unique<WindowManager>(); const auto window = manager->createWindow();
        const auto original = manager->addTab(window, new QLabel("original"), {"original", "label", {}});
        const auto group = manager->model().groupForTab(original);
        const auto before = manager->saveLayout(); const auto revision = manager->model().revision();
        QPointer<WindowManager> owner(manager.get()); QWidget callerParent;
        QPointer<QWidget> content = new QLabel("caller content", &callerParent);
        const auto oldPane = QUuid::createUuid(); content->setProperty("paneId", oldPane);
        content->setProperty("windowSystemManaged", false); content->setFocusPolicy(Qt::NoFocus);
        int destroyed = 0; connect(content.data(), &QObject::destroyed, qApp, [&] { ++destroyed; });
        ContentPropertyHook hook; hook.property = property;
        hook.callback = [&](QObject* watched) { if (destroyManager) manager.reset(); else delete watched; };
        content->installEventFilter(&hook);
        auto* raw = manager.get();
        QVERIFY(raw->addTabToNewGroup(window, content.data(), {"new", "label", {}}, group, Placement::Right).isNull());
        QCOMPARE(hook.calls, 1);
        if (destroyManager) {
            QVERIFY(!owner); QVERIFY(content); QCOMPARE(destroyed, 0);
            QCOMPARE(content->parentWidget(), &callerParent);
            QCOMPARE(content->property("paneId").toUuid(), oldPane);
            QCOMPARE(content->property("windowSystemManaged"), QVariant(false));
            QCOMPARE(content->focusPolicy(), Qt::NoFocus);
            delete content.data(); QCOMPARE(destroyed, 1);
        } else {
            QVERIFY(owner); QVERIFY(!content); QCOMPARE(destroyed, 1);
            QCOMPARE(manager->saveLayout(), before); QCOMPARE(manager->model().revision(), revision);
        }
    }
    void adjacentPreparationPreservesReentrantModelChanges()
    {
        WindowManager manager; const auto window = manager.createWindow();
        const auto original = manager.addTab(window, new QLabel("original"), {"original", "label", {}});
        const auto group = manager.model().groupForTab(original);
        QWidget callerParent; QLabel content("caller content", &callerParent);
        const auto oldPane = QUuid::createUuid(); content.setProperty("paneId", oldPane);
        content.setProperty("windowSystemManaged", false); content.setFocusPolicy(Qt::NoFocus);
        QJsonObject afterCallback; bool edited = false;
        ContentPropertyHook hook; hook.property = "windowSystemManaged";
        hook.callback = [&](QObject*) { edited = manager.setTabColor(original, "red"); afterCallback = manager.model().toJson(); };
        content.installEventFilter(&hook);
        QVERIFY(manager.addTabToNewGroup(window, &content, {"new", "label", {}}, group, Placement::Right).isNull());
        QVERIFY(edited); QCOMPARE(hook.calls, 1); QCOMPARE(manager.model().toJson(), afterCallback);
        QCOMPARE(manager.model().centralGroups(window), QVector<GroupId>({group}));
        QCOMPARE(content.parentWidget(), &callerParent); QCOMPARE(content.focusPolicy(), Qt::NoFocus);
        QCOMPARE(content.property("paneId").toUuid(), oldPane);
        QCOMPARE(content.property("windowSystemManaged"), QVariant(false));
    }
    void adjacentCommitReturnsItsStableIdAfterNotification_data()
    {
        QTest::addColumn<bool>("destroyManager");
        QTest::newRow("close-new-tab") << false; QTest::newRow("destroy-manager") << true;
    }
    void adjacentPreparationPreservesARestoredModelWithTheSameRevision()
    {
        WindowManager manager; const auto window = manager.createWindow();
        const auto original = manager.addTab(window, new QLabel("original"), {"original", "label", {}});
        manager.setContentFactory([](const TabOptions&, PaneId) { return new QLabel("restored"); });
        QVERIFY(manager.restoreLayout(manager.saveLayout()));
        const auto group = manager.model().groupForTab(original); const auto revision = manager.model().revision();
        QVERIFY(!group.isNull()); QVERIFY(manager.model().group(group));
        QCOMPARE(manager.model().group(group)->windowId, window);
        QCOMPARE(manager.model().group(group)->location, GroupLocation::Central);
        QCOMPARE(revision, quint64(1));
        const auto before = manager.model().toJson();
        WindowManager replacement; const auto otherWindow = replacement.createWindow();
        const auto snapshot = replacement.saveLayout();
        QWidget callerParent; QLabel content("caller content", &callerParent);
        bool restored = false; QString restoreError; QJsonObject afterCallback;
        ContentPropertyHook hook; hook.property = "windowSystemManaged";
        hook.callback = [&](QObject*) {
            const auto result = manager.restoreLayout(snapshot); restored = result.ok; restoreError = result.error;
            afterCallback = manager.model().toJson();
        };
        content.installEventFilter(&hook);
        QVERIFY(manager.addTabToNewGroup(window, &content, {"new", "label", {}}, group, Placement::Right).isNull());
        QCOMPARE(hook.calls, 1); QVERIFY2(restored, qPrintable(restoreError));
        QVERIFY(afterCallback != before); QCOMPARE(manager.model().revision(), revision);
        QCOMPARE(manager.model().toJson(), afterCallback); QVERIFY(manager.model().window(otherWindow));
        QVERIFY(!manager.model().window(window)); QCOMPARE(content.parentWidget(), &callerParent);
        QVERIFY(!content.property("windowSystemManaged").isValid()); QVERIFY(!content.property("paneId").isValid());
        QCOMPARE(content.focusPolicy(), Qt::NoFocus);
    }
    void adjacentCommitReturnsItsStableIdAfterNotification()
    {
        QFETCH(bool, destroyManager);
        auto manager = std::make_unique<WindowManager>(); const auto window = manager->createWindow();
        const auto original = manager->addTab(window, new QLabel("original"), {"original", "label", {}});
        const auto group = manager->model().groupForTab(original);
        QPointer<WindowManager> owner(manager.get()); QPointer<QWidget> content = new QLabel("accepted content");
        int destroyed = 0; connect(content.data(), &QObject::destroyed, qApp, [&] { ++destroyed; });
        TabId committed; bool handled = false, closed = false;
        connect(manager.get(), &WindowManager::layoutChanged, qApp, [&] {
            if (handled) return;
            handled = true; committed = manager->model().tabForPane(content->property("paneId").toUuid());
            if (destroyManager) manager.reset(); else closed = manager->closeTab(committed);
        });
        const auto result = manager->addTabToNewGroup(window, content.data(), {"new", "label", {}}, group, Placement::Right);
        QVERIFY(handled); QVERIFY(!result.isNull()); QCOMPARE(result, committed);
        if (destroyManager) { QVERIFY(!owner); QVERIFY(!content); QCOMPARE(destroyed, 1); }
        else {
            QVERIFY(owner); QVERIFY(closed); QVERIFY(!manager->model().tab(result));
            QVERIFY(manager->model().tab(original)); QVERIFY(manager->model().validate());
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QVERIFY(!content); QCOMPARE(destroyed, 1);
        }
    }
    void splitFactoriesCanDestroyTheirManagerBeforeReturning_data()
    {
        QTest::addColumn<QString>("command");
        QTest::newRow("adjacent-group") << QString("workspace.splitGroup.right");
        QTest::newRow("pane") << QString("pane.split.right");
    }
    void splitFactoriesCanDestroyTheirManagerBeforeReturning()
    {
        QFETCH(QString, command);
        auto manager = std::make_unique<WindowManager>(); const auto window = manager->createWindow();
        manager->addTab(window, new QLabel("original"), {"original", "label", {}});
        QPointer<WindowManager> owner(manager.get()); QPointer<QWidget> created; int destroyed = 0;
        manager->setContentFactory([&](const TabOptions&, PaneId) -> QWidget* {
            auto* result = new QLabel("factory child", manager->windowWidget(window)); created = result;
            connect(result, &QObject::destroyed, qApp, [&] { ++destroyed; });
            manager.reset(); return result;
        });
        auto* raw = manager.get(); QVERIFY(!raw->execute(command, {}, window));
        QVERIFY(!owner); QVERIFY(!created); QCOMPARE(destroyed, 1);
    }
    void adjacentCommandDoesNotDeleteContentTwice()
    {
        WindowManager manager; const auto window = manager.createWindow();
        manager.addTab(window, new QLabel("original"), {"original", "label", {}});
        const auto before = manager.saveLayout(); const auto revision = manager.model().revision();
        ContentPropertyHook hook; hook.property = "windowSystemManaged";
        hook.callback = [](QObject* content) { delete content; };
        QPointer<QWidget> created; int destroyed = 0;
        manager.setContentFactory([&](const TabOptions&, PaneId) {
            auto* result = new QLabel("self-closing content"); created = result;
            connect(result, &QObject::destroyed, qApp, [&] { ++destroyed; });
            result->installEventFilter(&hook); return result;
        });
        QVERIFY(!manager.execute("workspace.splitGroup.right", {}, window));
        QCOMPARE(hook.calls, 1); QVERIFY(!created); QCOMPARE(destroyed, 1);
        QCOMPARE(manager.saveLayout(), before); QCOMPARE(manager.model().revision(), revision);
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
    void wholeGroupExchangeIncludesPinnedNeighbors_data()
    {
        QTest::addColumn<int>("direction");
        for (int i = 1; i < 5; ++i) QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void wholeGroupExchangeIncludesPinnedNeighbors()
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
        QVERIFY(manager.execute("group.swap."+command,{},window));
        QCOMPARE(manager.model().centralGroups(window), direction == int(Placement::Left) || direction == int(Placement::Top)
            ? QVector<GroupId>({pinned,target}) : QVector<GroupId>({target,pinned}));
        QCOMPARE(manager.model().group(pinned)->location,GroupLocation::Central);
        QCOMPARE(manager.model().group(group)->location,GroupLocation::Pinned);
        QCOMPARE(manager.model().group(group)->edge,side);
        QCOMPARE(manager.model().groupForTab(source),group); QCOMPARE(manager.model().groupForTab(other),target);
        QCOMPARE(manager.model().groupForTab(sidebar),pinned); QCOMPARE(manager.paneWidget(pane),content.data()); QVERIFY(content);
        QVERIFY(manager.model().validate());
    }
    void exchangeAndMergeCommandsUseExplicitTargetWithoutRecreatingContent()
    {
        WindowManager manager; const auto first = manager.createWindow(), second = manager.createWindow();
        const auto a = manager.addTab(first,new QLabel("a"),{"a","notes",{}}), b = manager.addTab(first,new QLabel("b"),{"b","notes",{}});
        const auto t = manager.addTab(second,new QLabel("t"),{"t","notes",{}});
        QVERIFY(manager.moveTabToEdge(t,Edge::Right,true)); const auto source = manager.model().groupForTab(a), target = manager.model().groupForTab(t);
        const auto paneA = manager.model().tab(a)->activePane, paneB = manager.model().tab(b)->activePane;
        QPointer<QWidget> widgetA(manager.paneWidget(paneA)), widgetB(manager.paneWidget(paneB));
        auto captured = manager.context(first); captured.group = source; captured.tab = b; captured.pane = paneB;
        QVERIFY(manager.executeInContext("group.swap",CommandContext{captured.window,captured.group,captured.tab,captured.pane,{{"targetGroup",target}}}));
        QCOMPARE(manager.model().group(source)->windowId,second); QCOMPARE(manager.model().group(source)->location,GroupLocation::Pinned);
        QCOMPARE(manager.model().group(target)->windowId,first); QCOMPARE(manager.model().group(target)->location,GroupLocation::Central);
        captured = manager.context(second); captured.group = source; captured.tab = b; captured.pane = paneB; captured.args = {{"targetGroup",target},{"insertionIndex",0}};
        QVERIFY(manager.executeInContext("group.merge",captured)); QVERIFY(!manager.model().group(source));
        QCOMPARE(manager.model().group(target)->tabs,QVector<TabId>({a,b,t})); QCOMPARE(manager.model().group(target)->activeTab,b);
        QCOMPARE(manager.paneWidget(paneA),widgetA.data()); QCOMPARE(manager.paneWidget(paneB),widgetB.data()); QVERIFY(widgetA); QVERIFY(widgetB);
        QVERIFY(manager.model().validate());
    }
    void directionalMergeIncludesPinnedGroupsAndAppendsInStableOrder_data()
    {
        QTest::addColumn<int>("direction");
        for (int i = 1; i < 5; ++i) QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void directionalMergeIncludesPinnedGroupsAndAppendsInStableOrder()
    {
        QFETCH(int,direction); WindowManager manager; const auto window = manager.createWindow();
        const auto a = manager.addTab(window,new QLabel("a"),{"a","notes",{}}), b = manager.addTab(window,new QLabel("b"),{"b","notes",{}});
        const auto source = manager.model().groupForTab(a);
        const auto t = manager.addTab(window,new QLabel("target"),{"target","notes",{}});
        const auto edge = direction == int(Placement::Left) ? Edge::Left : direction == int(Placement::Right) ? Edge::Right
            : direction == int(Placement::Top) ? Edge::Top : Edge::Bottom;
        QVERIFY(manager.moveTabToEdge(t,edge,true)); const auto target = manager.model().groupForTab(t);
        const auto pane = manager.model().tab(a)->activePane; QPointer<QWidget> content(manager.paneWidget(pane));
        auto* view = static_cast<WorkspaceWindow*>(manager.windowWidget(window)); view->resize(1100,740); manager.showWindow(window);
        QTRY_COMPARE(view->surface()->groupRects().size(),2); QVERIFY(manager.activateTab(b));
        const QString command = direction == int(Placement::Left) ? "left" : direction == int(Placement::Right) ? "right"
            : direction == int(Placement::Top) ? "up" : "down";
        QVERIFY(manager.execute("group.merge."+command,{},window));
        QVERIFY(!manager.model().group(source)); QCOMPARE(manager.model().group(target)->tabs,QVector<TabId>({t,a,b}));
        QCOMPARE(manager.model().group(target)->activeTab,b); QCOMPARE(manager.model().group(target)->location,GroupLocation::Pinned);
        QCOMPARE(manager.paneWidget(pane),content.data()); QVERIFY(content); QVERIFY(manager.model().validate());
    }
    void groupTargetPickerCancellationDoesNotChangeTheLayout_data()
    {
        QTest::addColumn<QString>("command");
        for (const auto id : {"group.swap","group.merge","group.moveToGroup"}) QTest::newRow(id) << QString::fromLatin1(id);
    }
    void groupTargetPickerCancellationDoesNotChangeTheLayout()
    {
        QFETCH(QString,command); WindowManager manager; const auto window = manager.createWindow();
        const auto a = manager.addTab(window,new QLabel("a"),{"a","notes",{}}), b = manager.addTab(window,new QLabel("b"),{"b","notes",{}});
        QVERIFY(manager.moveTab(b,manager.model().groupForTab(a),Placement::Right)); const auto original = manager.saveLayout();
        bool sawPicker = false;
        QTimer::singleShot(0,&manager,[&] {
            for (auto* widget : QApplication::topLevelWidgets()) if (widget->objectName() == "GroupTargetPicker") {
                auto* menu = qobject_cast<QMenu*>(widget); sawPicker = menu && menu->isVisible(); if (menu) menu->close();
            }
        });
        QVERIFY(!manager.execute(command,{},window)); QVERIFY(sawPicker); QCOMPARE(manager.saveLayout(),original);
    }
    void fixedGroupsParticipateInExplicitReparentAndDetachCommands()
    {
        WindowManager manager; const auto root = manager.createWindow(), targetWindow = manager.createWindow();
        const auto tab = manager.addTab(root,new QLabel("moved"),{"moved","notes",{}});
        const auto target = manager.addTab(targetWindow,new QLabel("target"),{"target","notes",{}});
        const auto group = manager.model().groupForTab(tab), targetGroup = manager.model().groupForTab(target), pane = manager.model().tab(tab)->activePane;
        QPointer<QWidget> content(manager.paneWidget(pane));
        QVERIFY(manager.execute("group.moveEdge.left",{},root)); QCOMPARE(manager.model().group(group)->location,GroupLocation::Pinned);
        QVERIFY(manager.execute("group.moveToGroup",{{"targetGroup",targetGroup},{"placement",int(Placement::Bottom)}},root));
        QCOMPARE(manager.model().group(group)->location,GroupLocation::Central); QCOMPARE(manager.model().group(group)->windowId,targetWindow);
        QCOMPARE(manager.context(targetWindow).group, group);
        QSignalSpy commandErrors(&manager, &WindowManager::commandFailed);
        const bool movedBottom = manager.execute("group.moveEdge.down",{},targetWindow);
        QVERIFY2(movedBottom, commandErrors.isEmpty() ? "No command diagnostic" : qPrintable(commandErrors.first().first().toString()));
        QCOMPARE(manager.model().group(group)->edge,Edge::Bottom);
        QVERIFY(manager.execute("group.detachFloating",{},targetWindow)); const auto floating = manager.model().group(group)->windowId;
        QCOMPARE(manager.model().window(floating)->kind,WindowKind::Floating); QCOMPARE(manager.model().group(group)->location,GroupLocation::Central);
        QVERIFY(manager.execute("group.moveWindow",{{"targetWindow",root}},floating)); QVERIFY(!manager.model().window(floating));
        QVERIFY(manager.execute("group.detachWorkbench",{},root)); const auto complete = manager.model().group(group)->windowId;
        QCOMPARE(manager.model().window(complete)->kind,WindowKind::Workbench);
        QVERIFY(manager.execute("group.hideEdge.right",{},complete)); QCOMPARE(manager.model().group(group)->location,GroupLocation::AutoHide);
        const auto before = manager.saveLayout(); QVERIFY(!manager.execute("group.swap",{{"targetGroup",targetGroup}},complete)); QCOMPARE(manager.saveLayout(),before);
        QCOMPARE(manager.paneWidget(pane),content.data()); QVERIFY(content); QVERIFY(manager.model().validate());
    }
    void directionalGroupExchangeWrapsWithinTheVisibleGroupLayer_data()
    {
        QTest::addColumn<int>("direction");
        for (int i = 1; i < 5; ++i) QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void detachedGroupsInheritTheDestinationKindVisibilityDefaults_data()
    {
        QTest::addColumn<bool>("floating");
        QTest::newRow("floating") << true; QTest::newRow("workbench") << false;
    }
    void collectingGroupsLeavesThePeekClosed_data()
    {
        QTest::addColumn<int>("edge"); QTest::addColumn<bool>("floating"); QTest::addColumn<bool>("pinned");
        for (int edge = 0; edge < 4; ++edge) for (const bool floating : {false, true}) for (const bool pinned : {false, true})
            QTest::newRow(qPrintable(QString("edge-%1-floating-%2-pinned-%3").arg(edge).arg(floating).arg(pinned)))
                << edge << floating << pinned;
    }
    void collectingGroupsLeavesThePeekClosed()
    {
        QFETCH(int, edge); QFETCH(bool, floating); QFETCH(bool, pinned);
        WindowManager manager; const auto owner = manager.createWindow();
        const auto window = floating ? manager.createWindow(WindowKind::Floating, {}, owner) : owner;
        const auto kept = manager.addTab(window, new QLabel("kept"), {"kept", "label", {}});
        const auto a = manager.addTab(window, new QLabel("a"), {"a", "label", {}});
        const auto b = manager.addTab(window, new QLabel("b"), {"b", "label", {}});
        QVERIFY(manager.moveTab(a, manager.model().groupForTab(kept), Placement::Right));
        const auto group = manager.model().groupForTab(a); QVERIFY(manager.moveTab(b, group));
        if (pinned) QVERIFY(manager.moveGroupToEdge(group, window, Edge((edge + 1) % 4), true));
        const auto pane = manager.model().tab(a)->activePane; QPointer<QWidget> content(manager.paneWidget(pane));
        manager.showWindow(window); auto* view = manager.windowWidget(window); QVERIFY(QTest::qWaitForWindowExposed(view));
        QVERIFY(manager.activateTab(a));
        const QStringList suffixes{"left", "right", "up", "down"};
        QVERIFY(manager.execute("group.hideEdge." + suffixes[edge], {}, window));
        QCOMPARE(manager.model().group(group)->location, GroupLocation::AutoHide);
        QCOMPARE(manager.model().group(group)->windowId, window); QCOMPARE(manager.model().group(group)->edge, Edge(edge));
        QCOMPARE(manager.model().group(group)->tabs, QVector<TabId>({a, b}));
        QVERIFY(!view->findChild<QWidget*>("PeekPanel")); QVERIFY(!content->isVisible());
        QCOMPARE(manager.model().tab(a)->activePane, pane); QCOMPARE(manager.paneWidget(pane), content.data()); QVERIFY(content);
        // Explicitly opening it still works; collecting did not destroy the
        // contents, rail interaction, or the group's remembered identity.
        QVERIFY(manager.activateTab(a)); QVERIFY(view->findChild<QWidget*>("PeekPanel")); QVERIFY(content->isVisible());
        QVERIFY(manager.model().validate());
    }
    void detachedGroupsInheritTheDestinationKindVisibilityDefaults()
    {
        QFETCH(bool, floating);
        WindowManager manager; const auto source = manager.createWindow();
        const auto first = manager.addTab(source, new QLabel("first"), {"first", "label", {}});
        const auto second = manager.addTab(source, new QLabel("second"), {"second", "label", {}});
        const auto group = manager.model().groupForTab(first);
        const auto firstPane = manager.model().tab(first)->activePane, secondPane = manager.model().tab(second)->activePane;
        QPointer<QWidget> firstContent(manager.paneWidget(firstPane)), secondContent(manager.paneWidget(secondPane));
        const auto otherWorkbench = manager.createWindow();
        manager.addTab(otherWorkbench, new QLabel("workbench"), {"workbench", "label", {}});
        const auto otherFloating = manager.createWindow(WindowKind::Floating, {}, source);
        manager.addTab(otherFloating, new QLabel("floating"), {"floating", "label", {}});
        const BarVisibility workbenchDefault{false, true, false, true};
        const BarVisibility floatingDefault{true, false, true, false};
        const auto setVisibility = [&manager](WindowId window, const BarVisibility& visibility) {
            return manager.setToolbarsVisible(window, visibility.toolbars) && manager.setStatusBarVisible(window, visibility.statusBar)
                && manager.setTabBarsVisible(window, visibility.tabBars) && manager.setSideBarsVisible(window, visibility.sideBars);
        };
        QVERIFY(setVisibility(otherWorkbench, workbenchDefault)); QVERIFY(setVisibility(otherFloating, floatingDefault));
        QCOMPARE(manager.preferredBarVisibility(source), BarVisibility{});
        const auto created = manager.detachGroup(group, floating ? WindowKind::Floating : WindowKind::Workbench);
        QVERIFY(!created.isNull()); QVERIFY(created != source);
        QCOMPARE(manager.model().window(created)->kind, floating ? WindowKind::Floating : WindowKind::Workbench);
        QCOMPARE(manager.preferredBarVisibility(created), floating ? floatingDefault : workbenchDefault);
        QCOMPARE(manager.barVisibility(created), floating ? floatingDefault : workbenchDefault);
        QCOMPARE(manager.model().group(group)->windowId, created); QCOMPARE(manager.model().groupForTab(first), group);
        QCOMPARE(manager.model().groupForTab(second), group); QCOMPARE(manager.model().group(group)->location, GroupLocation::Central);
        QCOMPARE(manager.model().tab(first)->activePane, firstPane); QCOMPARE(manager.model().tab(second)->activePane, secondPane);
        QCOMPARE(manager.paneWidget(firstPane), firstContent.data()); QCOMPARE(manager.paneWidget(secondPane), secondContent.data());
        QVERIFY(firstContent); QVERIFY(secondContent); QVERIFY(manager.model().validate());
    }
    void directionalGroupExchangeWrapsWithinTheVisibleGroupLayer()
    {
        QFETCH(int,direction); WindowManager manager; const auto window = manager.createWindow();
        const auto first = manager.addTab(window,new QLabel("first"),{"first","notes",{}}), second = manager.addTab(window,new QLabel("second"),{"second","notes",{}});
        const bool horizontal = direction == int(Placement::Left) || direction == int(Placement::Right);
        const auto firstGroup = manager.model().groupForTab(first);
        QVERIFY(manager.moveTab(second,firstGroup,horizontal ? Placement::Right : Placement::Bottom));
        const auto secondGroup = manager.model().groupForTab(second);
        auto* windowView = static_cast<WorkspaceWindow*>(manager.windowWidget(window)); windowView->resize(1000,700); manager.showWindow(window);
        QTRY_COMPARE(windowView->surface()->groupRects().size(),2);
        const auto source = direction == int(Placement::Left) || direction == int(Placement::Top) ? firstGroup : secondGroup;
        QVERIFY(manager.swapGroupDirection(source,Placement(direction)));
        QCOMPARE(manager.model().centralGroups(window),QVector<GroupId>({secondGroup,firstGroup})); QVERIFY(manager.model().validate());
    }
    void canceledNamedCollectionLayerRegistersNoCommandsOrStorage()
    {
        WindowManager manager; const auto window = manager.createWindow(); manager.addTab(window,new QLabel("a"),{"a","notes",{}});
        for (const auto& command : manager.commands().commands()) QVERIFY(!command.id.startsWith("collection."));
        const auto snapshot = manager.saveLayout(); QVERIFY(!snapshot.contains("savedCollections"));
        QVERIFY(!snapshot.value("model").toObject().contains("collections"));
    }
    void groupTargetArgumentsRejectInvalidNumbersAtomically_data()
    {
        QTest::addColumn<QString>("command"); QTest::addColumn<QString>("argument");
        QTest::addColumn<QVariant>("invalid");
        for (const auto& pair : {qMakePair(QString("group.merge"), QString("insertionIndex")),
                                qMakePair(QString("group.moveToGroup"), QString("placement"))}) {
            int row = 0;
            for (const auto& value : {QVariant("not-a-number"), QVariant("1"), QVariant(true),
                                      QVariant(1.5), QVariant(1e30), QVariant(-2)})
                QTest::newRow(qPrintable(pair.first + QString::number(row++))) << pair.first << pair.second << value;
        }
    }
    void groupTargetArgumentsRejectInvalidNumbersAtomically()
    {
        QFETCH(QString, command); QFETCH(QString, argument); QFETCH(QVariant, invalid);
        WindowManager manager; const auto window = manager.createWindow();
        const auto a = manager.addTab(window, new QLabel("a"), {"a", "label", {}});
        const auto b = manager.addTab(window, new QLabel("b"), {"b", "label", {}});
        const auto source = manager.model().groupForTab(a);
        QVERIFY(manager.moveTab(b, source, Placement::Right));
        const auto target = manager.model().groupForTab(b);
        auto captured = manager.context(window); captured.group = source; captured.tab = a;
        captured.pane = manager.model().tab(a)->activePane;
        captured.args = {{"targetGroup", target}, {argument, invalid}};
        const auto before = manager.saveLayout();
        QSignalSpy changes(&manager, &WindowManager::layoutChanged);
        QVERIFY(!manager.executeInContext(command, captured));
        QCOMPARE(manager.saveLayout(), before); QCOMPARE(changes.count(), 0);
    }
    void stickyTabsLeadTheirStripAndSurviveRangeCloses()
    {
        WindowManager manager; auto behavior = manager.behavior();
        behavior.newTabPosition = NewTabPosition::Beginning; manager.setBehavior(behavior);
        const auto window = manager.createWindow();
        QVector<TabId> tabs;
        for (const auto* name : {"A", "B", "C", "D"}) tabs << manager.addTab(window, new QLabel(name), {name, "test", {}});
        const auto group = manager.model().groupForTab(tabs[0]);
        const auto contextFor = [&](TabId tab) {
            auto result = manager.context(window);
            result.tab = tab; result.group = manager.model().groupForTab(tab); result.pane = manager.model().tab(tab)->activePane;
            return result;
        };
        auto context = contextFor(tabs[2]);
        QVERIFY(manager.commands().find("tab.stick")->enabled(context));
        QVERIFY(!manager.commands().find("tab.unstick")->enabled(context));
        QVERIFY(manager.executeInContext("tab.stick", context));
        QVERIFY(manager.isTabSticky(tabs[2])); QCOMPARE(manager.model().group(group)->tabs.first(), tabs[2]);
        QVERIFY(!manager.commands().find("tab.stick")->enabled(context));
        QVERIFY(manager.commands().find("tab.unstick")->enabled(context));
        // A new tab at the "beginning" opens after the sticky run.
        const auto created = manager.addTab(window, new QLabel("E"), {"E", "test", {}});
        QCOMPARE(manager.model().group(group)->tabs.indexOf(created), 1);
        // Range closes never take sticky tabs; a direct close does.
        context = contextFor(tabs[0]);
        QVERIFY(manager.executeInContext("workspace.closeTabs.others", context));
        QCOMPARE(manager.model().group(group)->tabs, QVector<TabId>({tabs[2], tabs[0]}));
        QVERIFY(manager.executeInContext("workspace.closeTabs.all", context));
        QCOMPARE(manager.model().group(group)->tabs, QVector<TabId>({tabs[2]}));
        context = contextFor(tabs[2]);
        QVERIFY(!manager.commands().find("workspace.closeTabs.all")->enabled(context));
        // Saved layouts keep the run.
        const auto restoredTab = manager.addTab(window, new QLabel("F"), {"F", "test", {}});
        WindowManager restored; restored.setContentFactory([](const TabOptions&, PaneId) { return new QLabel("restored"); });
        QVERIFY(restored.restoreLayout(manager.saveLayout()));
        QVERIFY(restored.isTabSticky(tabs[2])); QVERIFY(!restored.isTabSticky(restoredTab));
        // The strip's pin unsticks the tab.
        auto* host = manager.windowWidget(window); host->show(); QVERIFY(QTest::qWaitForWindowExposed(host));
        TabBar* bar = nullptr;
        for (auto* candidate : host->findChildren<TabBar*>())
            if (candidate->isVisible() && candidate->count() == 2) bar = candidate;
        QVERIFY(bar);
        QCOMPARE(bar->tabData(0).toUuid(), tabs[2]);
        const QPoint pin = bar->actionRect(0).center();
        for (const auto type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
            QMouseEvent event(type, pin, bar->mapToGlobal(pin), Qt::LeftButton,
                type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(bar, &event);
        }
        QTRY_VERIFY(!manager.isTabSticky(tabs[2]));
        QVERIFY(manager.model().tab(tabs[2]));
        QVERIFY(manager.model().validate());
    }

};
QTEST_MAIN(TabGroupTest)
#include "tab_group_test.moc"
