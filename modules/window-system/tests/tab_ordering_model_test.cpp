// SPDX-License-Identifier: MIT
#include <window_system/layout_model.hpp>
#include <QJsonArray>
#include <QTest>
using namespace window_system;

class TabOrderingModelTest final : public QObject {
    Q_OBJECT
private slots:
    void sortUsesRecordedEventsWithoutChangingContent_data()
    {
        QTest::addColumn<int>("sorting");
        for (int i = 0; i < 6; ++i) QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void sortUsesRecordedEventsWithoutChangingContent()
    {
        QFETCH(int, sorting);
        LayoutModel model; const auto window = model.createWindow();
        const auto z = model.addTab(window, "z"), a = model.addTab(window, "a"), m = model.addTab(window, "m"), n = model.addTab(window, "n");
        const auto group = model.groupForTab(z); const auto panes = model.panes(m);
        const auto opened = model.tab(m)->openedOrder;
        QVERIFY(model.activateTab(z)); QVERIFY(model.activateTab(a));
        const auto active = model.group(group)->activeTab;
        QVERIFY(model.sortTabs(group, TabSortOrder(sorting)));
        const QVector<QVector<TabId>> expected{{a,m,n,z}, {z,n,m,a}, {z,a,m,n}, {n,m,a,z}, {m,n,z,a}, {a,z,n,m}};
        QCOMPARE(model.group(group)->tabs, expected[sorting]);
        QCOMPARE(model.group(group)->activeTab, active); QCOMPARE(model.panes(m), panes);
        QCOMPARE(model.tab(m)->openedOrder, opened); QVERIFY(model.validate());
    }
    void stableAlphabeticalTiesPreserveExistingOrder()
    {
        LayoutModel model; const auto window = model.createWindow();
        const auto first = model.addTab(window, "Same"), second = model.addTab(window, "same"), last = model.addTab(window, "other");
        const auto group = model.groupForTab(first);
        QVERIFY(model.sortTabs(group, TabSortOrder::AlphabeticalAscending));
        QCOMPARE(model.group(group)->tabs, QVector<TabId>({last, first, second}));
        QVERIFY(model.sortTabs(group, TabSortOrder::AlphabeticalDescending));
        QCOMPARE(model.group(group)->tabs, QVector<TabId>({first, second, last}));
    }
    void groupMovementPreservesTheGroupAndContent_data()
    {
        QTest::addColumn<int>("direction");
        for (int i = 1; i < 5; ++i) QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void groupMovementPreservesTheGroupAndContent()
    {
        QFETCH(int, direction);
        LayoutModel model; const auto first = model.createWindow(), second = model.createWindow();
        const auto a = model.addTab(first, "a"), b = model.addTab(first, "b"), t = model.addTab(second, "t");
        const auto group = model.groupForTab(a), target = model.groupForTab(t); const auto panes = model.panes(a);
        QVERIFY(model.setTabColor(a, "red"));
        QVERIFY(model.moveGroup(group, target, Placement(direction)));
        QCOMPARE(model.groupForTab(a), group); QCOMPARE(model.group(group)->windowId, second); QCOMPARE(model.panes(a), panes);
        QCOMPARE(model.group(group)->tabs, QVector<TabId>({a,b})); QCOMPARE(model.tab(a)->colorId, QString("red"));
        const auto ordered = model.centralGroups(second);
        QCOMPARE(ordered, direction == int(Placement::Left) || direction == int(Placement::Top)
            ? QVector<GroupId>({group,target}) : QVector<GroupId>({target,group}));
        QVERIFY(model.moveGroupToWindow(group, first)); QCOMPARE(model.group(group)->windowId, first); QVERIFY(model.validate());
    }
    void moveToWindowUsesCentralGroupsWhilePinnedGroupIsActive()
    {
        LayoutModel model; const auto first = model.createWindow(), second = model.createWindow();
        const auto moved = model.addTab(first, "moved"), central = model.addTab(second, "central"), pinned = model.addTab(second, "pinned");
        QVERIFY(model.moveTabToEdge(pinned, Edge::Left, true)); QVERIFY(model.activateTab(pinned));
        const auto group = model.groupForTab(moved), target = model.groupForTab(central), side = model.groupForTab(pinned);
        QVERIFY(model.moveGroupToWindow(group, second, Placement::Left));
        QCOMPARE(model.centralGroups(second), QVector<GroupId>({group,target}));
        QCOMPARE(model.group(side)->location, GroupLocation::Pinned); QCOMPARE(model.groupForTab(pinned), side); QVERIFY(model.validate());
    }
    void moveToWindowWithoutCentralGroupPreservesSidebar()
    {
        LayoutModel model; const auto first = model.createWindow(), second = model.createWindow();
        const auto moved = model.addTab(first, "moved"), pinned = model.addTab(second, "pinned");
        QVERIFY(model.moveTabToEdge(pinned, Edge::Right, true)); const auto side = model.groupForTab(pinned), group = model.groupForTab(moved);
        QVERIFY(model.moveGroupToWindow(group, second)); QCOMPARE(model.centralGroups(second), QVector<GroupId>({group}));
        QCOMPARE(model.window(second)->edgeGroups, QVector<GroupId>({side})); QVERIFY(model.validate());
    }
    void wholeWindowMergePreservesColorAndRecordedOrder()
    {
        LayoutModel model; const auto root = model.createWindow(), target = model.createWindow();
        const auto a = model.addTab(root, "a"), b = model.addTab(root, "b"), t = model.addTab(target, "t");
        const auto opened = model.tab(a)->openedOrder; const auto visited = model.tab(a)->visitedOrder;
        const auto panes = model.panes(a); QVERIFY(model.setTabColor(a, "red"));
        QVERIFY(model.moveAllTabs(root, model.groupForTab(t)));
        QCOMPARE(model.group(model.groupForTab(t))->tabs, QVector<TabId>({t,a,b}));
        QCOMPARE(model.tab(a)->colorId, QString("red")); QCOMPARE(model.tab(a)->openedOrder, opened);
        QCOMPARE(model.tab(a)->visitedOrder, visited); QCOMPARE(model.panes(a), panes); QVERIFY(model.validate());
        LayoutModel restored; QVERIFY(restored.fromJson(model.toJson())); QCOMPARE(restored.toJson(), model.toJson());
    }
    void paneActivationRecordsTabVisitsOnlyWhenChangingTabs()
    {
        LayoutModel model; const auto window = model.createWindow(); const auto a = model.addTab(window, "a"), b = model.addTab(window, "b");
        const auto previous = model.tab(b)->visitedOrder;
        QVERIFY(model.activatePane(a, model.tab(a)->activePane)); QVERIFY(model.tab(a)->visitedOrder > previous);
        const auto visit = model.tab(a)->visitedOrder;
        QVERIFY(model.activatePane(a, model.tab(a)->activePane)); QCOMPARE(model.tab(a)->visitedOrder, visit);
    }
    void invalidOrderingJsonIsRejectedAtomically_data()
    {
        QTest::addColumn<QString>("field");
        for (auto field : {"colorType", "colorLength", "orderType", "orderNegative", "orderFraction", "orderOverflow", "counterType", "missingOpened", "missingVisited"})
            QTest::newRow(field) << QString(field);
    }
    void invalidOrderingJsonIsRejectedAtomically()
    {
        QFETCH(QString, field); LayoutModel model; const auto window = model.createWindow(); model.addTab(window, "a");
        const auto original = model.toJson(); auto json = original; auto tabs = json.value("tabs").toArray(); auto tab = tabs[0].toObject();
        if (field == "colorType") tab.insert("colorId", 3);
        if (field == "colorLength") tab.insert("colorId", QString(129, 'x'));
        if (field == "orderType") tab.insert("openedOrder", "1");
        if (field == "orderNegative") tab.insert("visitedOrder", -1);
        if (field == "orderFraction") tab.insert("visitedOrder", 1.5);
        if (field == "orderOverflow") tab.insert("openedOrder", 9007199254740992.0);
        if (field == "counterType") json.insert("orderCounter", "1");
        if (field == "missingOpened") tab.remove("openedOrder");
        if (field == "missingVisited") tab.remove("visitedOrder");
        tabs[0] = tab; json.insert("tabs", tabs);
        QVERIFY(!model.fromJson(json)); QCOMPARE(model.toJson(), original); QVERIFY(model.validate());
    }
    void nearLimitClockPreservesAllSixSorts_data()
    {
        QTest::addColumn<int>("action"); QTest::addColumn<int>("sorting");
        for (int action = 0; action < 4; ++action) for (int sorting = 0; sorting < 6; ++sorting)
            QTest::newRow(qPrintable(QString("action-%1-sort-%2").arg(action).arg(sorting))) << action << sorting;
    }
    void nearLimitClockPreservesAllSixSorts()
    {
        QFETCH(int, action); QFETCH(int, sorting);
        constexpr quint64 maximum = 9007199254740991ULL;
        LayoutModel model; const auto window = model.createWindow();
        const auto z = model.addTab(window, "z"), a = model.addTab(window, "a"), m = model.addTab(window, "m"), n = model.addTab(window, "n");
        const auto pane = model.tab(z)->activePane;
        const auto carried = model.splitPane(z, pane, Placement::Right); QVERIFY(!carried.isNull());
        auto snapshot = model.toJson(); snapshot.insert("orderCounter", double(maximum));
        auto tabs = snapshot.value("tabs").toArray(); const QVector<TabId> original{z,a,m,n};
        const QVector<quint64> opened{maximum-7,maximum-6,maximum-5,maximum-4};
        const QVector<quint64> visited{maximum-2,maximum-2,maximum-3,maximum-1};
        for (qsizetype i = 0; i < tabs.size(); ++i) {
            auto tab = tabs[i].toObject(); const auto id = QUuid(tab.value("id").toString()); const auto index = original.indexOf(id);
            QVERIFY(index >= 0); tab.insert("openedOrder", double(opened[index])); tab.insert("visitedOrder", double(visited[index])); tabs[i] = tab;
        }
        snapshot.insert("tabs", tabs); QVERIFY(model.fromJson(snapshot)); QCOMPARE(model.toJson(),snapshot);
        const auto group = model.groupForTab(z); TabId created;
        if (action == 0) created = model.addTab(window,"b",group);
        if (action == 1) QVERIFY(model.activateTab(z));
        if (action == 2) QVERIFY(model.activatePane(z,pane));
        if (action == 3) created = model.extractPane(z,carried,group);
        if (action == 0 || action == 3) QVERIFY(!created.isNull());
        QVERIFY(model.tab(z)->openedOrder < model.tab(a)->openedOrder);
        QVERIFY(model.tab(a)->openedOrder < model.tab(m)->openedOrder);
        QVERIFY(model.tab(m)->openedOrder < model.tab(n)->openedOrder);
        if (action == 0 || action == 3) QCOMPARE(model.tab(z)->visitedOrder,model.tab(a)->visitedOrder);
        const auto compacted = model.toJson(); QVERIFY(compacted.value("orderCounter").toDouble() < 20);
        for (const auto& value : compacted.value("tabs").toArray()) {
            const auto tab = value.toObject(); QVERIFY(tab.value("openedOrder").toDouble() <= double(maximum));
            QVERIFY(tab.value("visitedOrder").toDouble() <= double(maximum));
        }
        LayoutModel restored; QVERIFY(restored.fromJson(compacted)); QCOMPARE(restored.toJson(),compacted);
        const auto active = restored.group(group)->activeTab;
        QVERIFY(restored.sortTabs(group,TabSortOrder(sorting)));
        QVector<QVector<TabId>> expected;
        if (action == 0) expected = {{a,created,m,n,z},{z,n,m,created,a},{z,a,m,n,created},{created,n,m,a,z},{m,z,a,n,created},{created,n,z,a,m}};
        else if (action == 3) expected = {{a,m,n,z,created},{z,created,n,m,a},{z,a,m,n,created},{created,n,m,a,z},{m,z,a,n,created},{created,n,z,a,m}};
        else expected = {{a,m,n,z},{z,n,m,a},{z,a,m,n},{n,m,a,z},{m,a,n,z},{z,n,a,m}};
        QCOMPARE(restored.group(group)->tabs,expected[sorting]); QCOMPARE(restored.group(group)->activeTab,active);
        QCOMPARE(restored.tabForPane(pane),z); QCOMPARE(restored.tabForPane(carried),action == 3 ? created : z); QVERIFY(restored.validate());
    }
    void failedMutationAtClockLimitDoesNotRenormalizeLiveState()
    {
        LayoutModel model; const auto window = model.createWindow(); const auto tab = model.addTab(window,"a");
        auto snapshot = model.toJson(); snapshot.insert("orderCounter",9007199254740991.0); QVERIFY(model.fromJson(snapshot));
        const auto original = model.toJson();
        QVERIFY(model.addTab(window,"invalid",model.groupForTab(tab),99).isNull()); QCOMPARE(model.toJson(),original);
        LayoutModel restored; QVERIFY(restored.fromJson(original)); QCOMPARE(restored.toJson(),original);
    }
    void legacySnapshotsWithoutOrderingRemainDeterministic()
    {
        LayoutModel model; const auto window = model.createWindow();
        const auto a = model.addTab(window,"a"), b = model.addTab(window,"b"), c = model.addTab(window,"c");
        auto snapshot = model.toJson(); snapshot.remove("orderCounter"); auto tabs = snapshot.value("tabs").toArray();
        for (qsizetype i = 0; i < tabs.size(); ++i) { auto tab = tabs[i].toObject(); tab.remove("openedOrder"); tab.remove("visitedOrder"); tabs[i] = tab; }
        snapshot.insert("tabs",tabs); LayoutModel restored; QVERIFY(restored.fromJson(snapshot));
        const auto group = restored.groupForTab(a); QVERIFY(restored.sortTabs(group,TabSortOrder::OpenedAscending));
        QCOMPARE(restored.group(group)->tabs,QVector<TabId>({a,b,c}));
        for (const auto tab : {a,b,c}) QCOMPARE(restored.tab(tab)->openedOrder,restored.tab(tab)->visitedOrder);
        const auto normalized = restored.toJson(); LayoutModel next; QVERIFY(next.fromJson(normalized)); QCOMPARE(next.toJson(),normalized);
    }
    void invalidGroupMoveAndSortAreAtomic()
    {
        LayoutModel model; const auto window = model.createWindow(), other = model.createWindow();
        const auto a = model.addTab(window, "a"), b = model.addTab(other, "b"); const auto group = model.groupForTab(a), target = model.groupForTab(b);
        const auto original = model.toJson();
        QVERIFY(!model.moveGroup(group, target, Placement::Center)); QCOMPARE(model.toJson(), original);
        QVERIFY(!model.moveGroup(group, target, Placement(99))); QCOMPARE(model.toJson(), original);
        QVERIFY(!model.moveGroup(group, group, Placement::Left)); QCOMPARE(model.toJson(), original);
        QVERIFY(!model.moveGroupToWindow(group, window)); QCOMPARE(model.toJson(), original);
        QVERIFY(!model.sortTabs(group, TabSortOrder(99))); QCOMPARE(model.toJson(), original);
    }
};
QTEST_GUILESS_MAIN(TabOrderingModelTest)
#include "tab_ordering_model_test.moc"
