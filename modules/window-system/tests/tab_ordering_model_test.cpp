// SPDX-License-Identifier: MIT
#include <window_system/layout_model.hpp>
#include <QJsonArray>
#include <QTest>
using namespace window_system;

class TabOrderingModelTest final : public QObject {
    Q_OBJECT
private slots:
    void adjacentGroupCreationKeepsSourceFocusOrderAndVisitHistory_data()
    {
        QTest::addColumn<int>("direction"); QTest::addColumn<int>("activeIndex");
        for (int direction = 1; direction < 5; ++direction) for (int active = 0; active < 3; ++active)
            QTest::newRow(qPrintable(QString("direction-%1-active-%2").arg(direction).arg(active))) << direction << active;
    }
    void adjacentGroupCreationKeepsSourceFocusOrderAndVisitHistory()
    {
        QFETCH(int,direction); QFETCH(int,activeIndex); LayoutModel model; const auto window = model.createWindow();
        const QVector<TabId> original{model.addTab(window,"a"),model.addTab(window,"b"),model.addTab(window,"c")};
        const auto source = model.groupForTab(original.first()); QVERIFY(model.activateTab(original[activeIndex]));
        QHash<TabId,TabRecord> previous; for (const auto tab : original) previous.insert(tab,*model.tab(tab));
        const auto revision = model.revision();
        const auto created = model.addTabToNewGroup(window,"new",source,Placement(direction)); QVERIFY(!created.isNull());
        const auto destination = model.groupForTab(created); QVERIFY(destination != source);
        QCOMPARE(model.revision(),revision + 1); QCOMPARE(model.group(source)->tabs,original); QCOMPARE(model.group(source)->activeTab,original[activeIndex]);
        for (const auto tab : original) {
            QCOMPARE(model.tab(tab)->openedOrder,previous[tab].openedOrder); QCOMPARE(model.tab(tab)->visitedOrder,previous[tab].visitedOrder);
            QCOMPARE(model.tab(tab)->panes,previous[tab].panes); QCOMPARE(model.tab(tab)->activePane,previous[tab].activePane);
        }
        QCOMPARE(model.group(destination)->tabs,QVector<TabId>({created})); QCOMPARE(model.group(destination)->activeTab,created);
        QCOMPARE(model.window(window)->activeGroup,destination);
        QCOMPARE(model.centralGroups(window),direction == int(Placement::Left) || direction == int(Placement::Top)
            ? QVector<GroupId>({destination,source}) : QVector<GroupId>({source,destination}));
        QVERIFY(model.validate());
    }
    void invalidAdjacentGroupCreationIsAtomic()
    {
        LayoutModel model; const auto window = model.createWindow(), other = model.createWindow();
        const auto tab = model.addTab(window,"a"), group = model.groupForTab(tab); const auto before = model.toJson();
        QVERIFY(model.addTabToNewGroup({},"new",group,Placement::Left).isNull()); QCOMPARE(model.toJson(),before);
        QVERIFY(model.addTabToNewGroup(other,"new",group,Placement::Left).isNull()); QCOMPARE(model.toJson(),before);
        QVERIFY(model.addTabToNewGroup(window,"new",{},Placement::Left).isNull()); QCOMPARE(model.toJson(),before);
        QVERIFY(model.addTabToNewGroup(window,"new",group,Placement::Center).isNull()); QCOMPARE(model.toJson(),before);
        QVERIFY(model.addTabToNewGroup(window,"new",group,Placement(99)).isNull()); QCOMPARE(model.toJson(),before);
        QVERIFY(model.moveGroupToEdge(group,Edge::Left,true)); const auto pinned = model.toJson();
        const auto pinnedRevision = model.revision();
        QVERIFY(model.addTabToNewGroup(window,"new",group,Placement::Center).isNull()); QCOMPARE(model.toJson(),pinned);
        QCOMPARE(model.revision(),pinnedRevision);
        QVERIFY(model.addTabToNewGroup(window,"new",group,Placement(99)).isNull()); QCOMPARE(model.toJson(),pinned);
        QCOMPARE(model.revision(),pinnedRevision);
        QVERIFY(model.setGroupPinned(group,false)); const auto hidden = model.toJson(); const auto hiddenRevision = model.revision();
        QVERIFY(model.addTabToNewGroup(window,"new",group,Placement::Right).isNull()); QCOMPARE(model.toJson(),hidden);
        QCOMPARE(model.revision(),hiddenRevision); QVERIFY(model.validate());
    }
    void pinnedGroupCreationAndMovementSupportBothAxes()
    {
        LayoutModel model; const auto window = model.createWindow();
        const auto moved = model.addTab(window,"moved"), pinned = model.addTab(window,"pinned");
        const auto source = model.groupForTab(moved);
        const auto movedPanes = model.panes(moved), pinnedPanes = model.panes(pinned);
        QVERIFY(model.moveTabToEdge(pinned,Edge::Left,true)); const auto target = model.groupForTab(pinned);
        const auto created = model.addTabToNewGroup(window,"new",target,Placement::Right); QVERIFY(!created.isNull());
        const auto adjacent = model.groupForTab(created); QVERIFY(adjacent != target);
        const auto initial = model.pinnedLayout(window,Edge::Left); QVERIFY(initial && !initial->isLeaf());
        QCOMPARE(initial->orientation,Qt::Horizontal); QCOMPARE(initial->first->itemId,target);
        QCOMPARE(initial->second->itemId,adjacent); QCOMPARE(model.group(adjacent)->tabs,QVector<TabId>{created});
        QCOMPARE(model.group(adjacent)->windowId,window); QCOMPARE(model.group(adjacent)->location,GroupLocation::Pinned);
        QCOMPARE(model.group(adjacent)->edge,Edge::Left); QCOMPARE(model.centralGroups(window),QVector<GroupId>{source});
        const auto revision = model.revision();
        QVERIFY(model.moveGroup(source,target,Placement::Top)); QCOMPARE(model.revision(),revision + 1);
        const auto current = model.pinnedLayout(window,Edge::Left); QVERIFY(current && !current->isLeaf());
        QCOMPARE(current->id,initial->id); QCOMPARE(current->orientation,Qt::Horizontal); QCOMPARE(current->ratio,initial->ratio);
        QCOMPARE(current->second,initial->second); QVERIFY(current->first && !current->first->isLeaf());
        QCOMPARE(current->first->orientation,Qt::Vertical); QCOMPARE(current->first->first->itemId,source);
        QCOMPARE(current->first->second->itemId,target); QCOMPARE(initial->first->itemId,target);
        QCOMPARE(model.groupForTab(moved),source); QCOMPARE(model.group(source)->tabs,QVector<TabId>{moved});
        QCOMPARE(model.group(source)->windowId,window); QCOMPARE(model.group(source)->location,GroupLocation::Pinned);
        QCOMPARE(model.group(source)->edge,Edge::Left); QCOMPARE(model.group(target)->tabs,QVector<TabId>{pinned});
        QVERIFY(model.centralGroups(window).isEmpty()); QCOMPARE(model.panes(moved),movedPanes);
        QCOMPARE(model.panes(pinned),pinnedPanes); QVERIFY(model.validate());
    }
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
        for (auto field : {"colorType", "colorLength", "orderType", "orderNegative", "orderFraction", "orderOverflow", "counterType", "missingOpened", "missingVisited", "missingCounter", "missingOrders", "counterBehindOrders"})
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
        if (field == "missingCounter") json.remove("orderCounter");
        if (field == "missingOrders") { tab.remove("openedOrder"); tab.remove("visitedOrder"); }
        if (field == "counterBehindOrders") json.insert("orderCounter", 0);
        tabs[0] = tab; json.insert("tabs", tabs);
        const auto revision = model.revision(); const auto state = model.stateId();
        QVERIFY(!model.fromJson(json)); QCOMPARE(model.toJson(), original);
        QCOMPARE(model.revision(), revision); QCOMPARE(model.stateId(), state); QVERIFY(model.validate());
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
    void orderingFieldsAreRequiredEvenWhenTheArraysAreEmpty()
    {
        LayoutModel model;
        const auto snapshot = model.toJson();
        QCOMPARE(snapshot.value("orderCounter").toDouble(), 0.0);
        LayoutModel restored; QVERIFY(restored.fromJson(snapshot)); QCOMPARE(restored.toJson(), snapshot);
        auto missing = snapshot; missing.remove("orderCounter");
        const auto revision = restored.revision(); const auto state = restored.stateId();
        QVERIFY(!restored.fromJson(missing)); QCOMPARE(restored.toJson(), snapshot);
        QCOMPARE(restored.revision(), revision); QCOMPARE(restored.stateId(), state);
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
    void exchangePreservesVisibleSlotsAndContents_data()
    {
        QTest::addColumn<int>("sourcePlacement"); QTest::addColumn<int>("targetPlacement"); QTest::addColumn<bool>("crossWindow");
        for (int source = 0; source < 5; ++source) for (int target = 0; target < 5; ++target) for (bool cross : {false, true})
            QTest::newRow(qPrintable(QString("%1-%2-cross-%3").arg(source).arg(target).arg(cross))) << source << target << cross;
    }
    void exchangeKeepsSplitIdentitiesAndRatios()
    {
        LayoutModel model; const auto window = model.createWindow();
        const auto a = model.addTab(window,"a"), b = model.addTab(window,"b"), c = model.addTab(window,"c");
        const auto first = model.groupForTab(a);
        QVERIFY(model.moveTab(b,first,Placement::Right)); const auto second = model.groupForTab(b);
        QVERIFY(model.moveTab(c,second,Placement::Bottom)); const auto third = model.groupForTab(c);
        const auto tree = model.window(window)->central;
        QVERIFY(model.setSplitRatio(tree->id,0.31)); QVERIFY(model.setSplitRatio(tree->second->id,0.67));
        const auto previous = model.toJson().value("windows").toArray().first().toObject().value("central").toObject();
        std::function<QJsonObject(QJsonObject)> exchange = [&](QJsonObject node) {
            if (node.contains("itemId")) {
                const auto id = QUuid(node.value("itemId").toString());
                if (id == first || id == third) node.insert("itemId",(id == first ? third : first).toString(QUuid::WithoutBraces));
            } else {
                node.insert("first",exchange(node.value("first").toObject())); node.insert("second",exchange(node.value("second").toObject()));
            }
            return node;
        };
        QVERIFY(model.swapGroups(first,third));
        QCOMPARE(model.toJson().value("windows").toArray().first().toObject().value("central").toObject(),exchange(previous));
        QVERIFY(model.validate());
    }
    void exchangePreservesVisibleSlotsAndContents()
    {
        QFETCH(int, sourcePlacement); QFETCH(int, targetPlacement); QFETCH(bool, crossWindow);
        LayoutModel model; const auto root = model.createWindow();
        const auto destination = crossWindow ? model.createWindow(WindowKind::Floating, root) : root;
        const auto a = model.addTab(root,"a"), b = model.addTab(root,"b");
        const auto source = model.groupForTab(a);
        const auto t = model.addTab(destination,"target");
        if (!crossWindow) QVERIFY(model.moveTab(t,source,Placement::Right));
        const auto target = model.groupForTab(t);
        if (sourcePlacement) QVERIFY(model.moveGroupToEdge(source,Edge(sourcePlacement-1),true));
        if (targetPlacement) QVERIFY(model.moveGroupToEdge(target,Edge(targetPlacement-1),true));
        if (sourcePlacement) QVERIFY(model.setEdgeGroupExtent(source,191));
        if (targetPlacement) QVERIFY(model.setEdgeGroupExtent(target,237));
        QVERIFY(model.setTabColor(a,"red")); const auto panes = model.panes(a);
        const auto first = *model.group(source), second = *model.group(target);
        const auto beforeSourceOrder = model.centralGroups(root), beforeTargetOrder = model.centralGroups(destination);
        const auto beforeSourceEdges = model.window(root)->edgeGroups, beforeTargetEdges = model.window(destination)->edgeGroups;
        QVERIFY(model.swapGroups(source,target));
        const auto* moved = model.group(source); const auto* displaced = model.group(target);
        QCOMPARE(moved->windowId,second.windowId); QCOMPARE(moved->location,second.location); QCOMPARE(moved->edge,second.edge);
        QCOMPARE(moved->edgeExtent,second.edgeExtent); QCOMPARE(moved->tabs,QVector<TabId>({a,b})); QCOMPARE(moved->activeTab,b);
        QCOMPARE(displaced->windowId,first.windowId); QCOMPARE(displaced->location,first.location); QCOMPARE(displaced->edge,first.edge);
        QCOMPARE(displaced->edgeExtent,first.edgeExtent); QCOMPARE(model.panes(a),panes); QCOMPARE(model.tab(a)->colorId,QString("red"));
        const auto exchanged = [source,target](QVector<GroupId> ids) {
            for (auto& id : ids) { if (id == source) id = target; else if (id == target) id = source; }
            return ids;
        };
        QCOMPARE(model.centralGroups(root),exchanged(beforeSourceOrder)); QCOMPARE(model.window(root)->edgeGroups,exchanged(beforeSourceEdges));
        QCOMPARE(model.centralGroups(destination),exchanged(beforeTargetOrder)); QCOMPARE(model.window(destination)->edgeGroups,exchanged(beforeTargetEdges));
        QVERIFY(model.validate()); LayoutModel restored; QVERIFY(restored.fromJson(model.toJson())); QCOMPARE(restored.toJson(),model.toJson());
        QVERIFY(model.swapGroups(source,target)); QCOMPARE(model.centralGroups(root),beforeSourceOrder); QCOMPARE(model.window(root)->edgeGroups,beforeSourceEdges);
        QCOMPARE(model.group(source)->tabs,first.tabs); QCOMPARE(model.group(target)->tabs,second.tabs); QVERIFY(model.validate());
    }
    void mergePreservesTargetSlotStableOrderAndTabState_data()
    {
        QTest::addColumn<int>("sourcePlacement"); QTest::addColumn<int>("targetPlacement"); QTest::addColumn<bool>("crossWindow");
        for (int source = 0; source < 5; ++source) for (int target = 0; target < 5; ++target) for (bool cross : {false, true})
            QTest::newRow(qPrintable(QString("%1-%2-cross-%3").arg(source).arg(target).arg(cross))) << source << target << cross;
    }
    void mergePreservesTargetSlotStableOrderAndTabState()
    {
        QFETCH(int, sourcePlacement); QFETCH(int, targetPlacement); QFETCH(bool, crossWindow);
        LayoutModel model; const auto root = model.createWindow();
        const auto origin = crossWindow ? model.createWindow(WindowKind::Floating,root) : root;
        const auto a = model.addTab(origin,"a"), b = model.addTab(origin,"b"), source = model.groupForTab(a);
        const auto t = model.addTab(root,"t"); if (!crossWindow) QVERIFY(model.moveTab(t,source,Placement::Right));
        const auto target = model.groupForTab(t), u = model.addTab(root,"u",target);
        if (sourcePlacement) QVERIFY(model.moveGroupToEdge(source,Edge(sourcePlacement-1),true));
        if (targetPlacement) { QVERIFY(model.moveGroupToEdge(target,Edge(targetPlacement-1),true)); QVERIFY(model.setEdgeGroupExtent(target,223)); }
        QVERIFY(model.setTabColor(a,"red")); QVERIFY(model.setPeekSizePolicy(a,Edge::Left,{311,417}));
        const auto previous = *model.tab(a);
        const auto targetSlot = *model.group(target);
        const auto panes = model.panes(a);
        QVERIFY(model.mergeGroups(source,target,1));
        QVERIFY(!model.group(source)); QCOMPARE(model.group(target)->tabs,QVector<TabId>({t,a,b,u})); QCOMPARE(model.group(target)->activeTab,b);
        QCOMPARE(model.group(target)->location,targetSlot.location); QCOMPARE(model.group(target)->edge,targetSlot.edge);
        QCOMPARE(model.group(target)->edgeExtent,targetSlot.edgeExtent); QCOMPARE(model.groupForTab(a),target);
        QCOMPARE(model.panes(a),panes); QCOMPARE(model.tab(a)->colorId,previous.colorId);
        QCOMPARE(model.tab(a)->openedOrder,previous.openedOrder); QCOMPARE(model.tab(a)->visitedOrder,previous.visitedOrder);
        QCOMPARE(model.peekSizePolicy(a,Edge::Left),PeekSizePolicy({311,417}));
        if (crossWindow) QVERIFY(!model.window(origin));
        QVERIFY(model.validate()); LayoutModel restored; QVERIFY(restored.fromJson(model.toJson())); QCOMPARE(restored.toJson(),model.toJson());
    }
    void directionalMovesAcceptFixedGroupsOnTheirPartitionAxis_data()
    {
        QTest::addColumn<int>("edge"); QTest::addColumn<bool>("outward"); QTest::addColumn<bool>("sourcePinned");
        for (int edge = 0; edge < 4; ++edge) for (bool outward : {false,true}) for (bool pinned : {false,true})
            QTest::newRow(qPrintable(QString("edge-%1-outward-%2-pinned-%3").arg(edge).arg(outward).arg(pinned))) << edge << outward << pinned;
    }
    void mergeEstablishesNewPinnedOrderWhileAnotherPartitionIsDormant()
    {
        LayoutModel model; const auto window = model.createWindow();
        const auto a = model.addTab(window,"a"), b = model.addTab(window,"b"), source = model.groupForTab(a);
        const auto t = model.addTab(window,"t"); QVERIFY(model.moveTab(t,source,Placement::Right)); const auto target = model.groupForTab(t);
        const auto u = model.addTab(window,"u",target), hidden = model.addTab(window,"hidden",target);
        QVERIFY(model.moveTabToEdge(hidden,Edge::Left,true)); QVERIFY(model.moveGroupToEdge(source,Edge::Left,true));
        QVERIFY(model.moveGroupToEdge(target,Edge::Left,true)); QVERIFY(model.setTabPinned(hidden,false));
        QVERIFY(model.mergeGroups(source,target,0)); QCOMPARE(model.group(target)->tabs,QVector<TabId>({a,b,t,u}));
        QVERIFY(model.setTabPinned(u,false)); QVERIFY(model.setTabPinned(u,true));
        QCOMPARE(model.group(target)->tabs,QVector<TabId>({a,b,t,u})); QCOMPARE(model.groupForTab(u),target); QVERIFY(model.validate());
    }
    void directionalMovesAcceptFixedGroupsOnTheirPartitionAxis()
    {
        QFETCH(int,edge); QFETCH(bool,outward); QFETCH(bool,sourcePinned);
        LayoutModel model; const auto root = model.createWindow(); const auto sourceWindow = model.createWindow(WindowKind::Floating,root);
        const auto a = model.addTab(sourceWindow,"a"), b = model.addTab(sourceWindow,"b"), source = model.groupForTab(a);
        const auto t = model.addTab(root,"t"), target = model.groupForTab(t);
        if (sourcePinned) QVERIFY(model.moveGroupToEdge(source,Edge((edge+1)%4),true));
        QVERIFY(model.moveGroupToEdge(target,Edge(edge),true));
        const auto panes = model.panes(a);
        const auto outside = edge == int(Edge::Left) ? Placement::Left : edge == int(Edge::Right) ? Placement::Right
            : edge == int(Edge::Top) ? Placement::Top : Placement::Bottom;
        const auto inside = edge == int(Edge::Left) ? Placement::Right : edge == int(Edge::Right) ? Placement::Left
            : edge == int(Edge::Top) ? Placement::Bottom : Placement::Top;
        QVERIFY(model.moveGroup(source,target,outward ? outside : inside));
        QCOMPARE(model.window(root)->edgeGroups,outward ? QVector<GroupId>({source,target}) : QVector<GroupId>({target,source}));
        QCOMPARE(model.group(source)->location,GroupLocation::Pinned); QCOMPARE(model.group(source)->edge,Edge(edge));
        QCOMPARE(model.group(source)->windowId,root); QCOMPARE(model.group(source)->tabs,QVector<TabId>({a,b}));
        QCOMPARE(model.panes(a),panes); QVERIFY(!model.window(sourceWindow)); QVERIFY(model.validate());
    }
    void explicitGroupParentChangesAndDetachAreAtomic_data()
    {
        QTest::addColumn<int>("edge"); QTest::addColumn<bool>("pinned");
        for (int edge = 0; edge < 4; ++edge) for (bool pinned : {false,true})
            QTest::newRow(qPrintable(QString("edge-%1-pinned-%2").arg(edge).arg(pinned))) << edge << pinned;
    }
    void explicitGroupParentChangesAndDetachAreAtomic()
    {
        QFETCH(int,edge); QFETCH(bool,pinned); LayoutModel model;
        const auto root = model.createWindow(), target = model.createWindow();
        const auto a = model.addTab(root,"a"), b = model.addTab(root,"b"), source = model.groupForTab(a);
        const auto panes = model.panes(a);
        QVERIFY(model.moveGroupToEdge(source,target,Edge(edge),pinned));
        QCOMPARE(model.group(source)->windowId,target); QCOMPARE(model.group(source)->location,pinned ? GroupLocation::Pinned : GroupLocation::AutoHide);
        QCOMPARE(model.group(source)->tabs,QVector<TabId>({a,b})); QCOMPARE(model.panes(a),panes);
        if (!pinned) QVERIFY(model.setGroupPinned(source,true));
        const auto floating = model.detachGroup(source); QVERIFY(!floating.isNull());
        QCOMPARE(model.group(source)->location,GroupLocation::Central); QCOMPARE(model.group(source)->windowId,floating);
        QCOMPARE(model.window(floating)->familyId,model.window(target)->familyId); QCOMPARE(model.window(floating)->kind,WindowKind::Floating);
        QVERIFY(model.moveGroupToWindow(source,target)); QVERIFY(!model.window(floating));
        const auto complete = model.detachGroup(source,WindowKind::Workbench); QVERIFY(!complete.isNull());
        QCOMPARE(model.window(complete)->kind,WindowKind::Workbench); QVERIFY(model.window(complete)->familyId != model.window(target)->familyId);
        QCOMPARE(model.panes(a),panes); QCOMPARE(model.group(source)->tabs,QVector<TabId>({a,b})); QVERIFY(model.validate());
    }
    void invalidGroupRelationshipsNeverPartiallyMutate()
    {
        LayoutModel model; const auto root = model.createWindow(); const auto a = model.addTab(root,"a"), b = model.addTab(root,"b");
        const auto first = model.groupForTab(a); QVERIFY(model.moveTab(b,first,Placement::Right)); const auto second = model.groupForTab(b);
        QVERIFY(model.moveGroupToEdge(second,Edge::Left,true)); const auto before = model.toJson();
        QVERIFY(!model.swapGroups(first,first)); QCOMPARE(model.toJson(),before);
        QVERIFY(!model.swapGroups(first,{})); QCOMPARE(model.toJson(),before);
        QVERIFY(!model.mergeGroups(first,first)); QCOMPARE(model.toJson(),before);
        QVERIFY(!model.mergeGroups(first,second,2)); QCOMPARE(model.toJson(),before);
        QVERIFY(!model.mergeGroups(first,second,-2)); QCOMPARE(model.toJson(),before);
        const auto revision = model.revision();
        QVERIFY(!model.moveGroup(first,second,Placement::Center)); QCOMPARE(model.toJson(),before); QCOMPARE(model.revision(),revision);
        QVERIFY(!model.moveGroup(first,second,Placement(99))); QCOMPARE(model.toJson(),before); QCOMPARE(model.revision(),revision);
        QVERIFY(!model.moveGroup(first,first,Placement::Top)); QCOMPARE(model.toJson(),before); QCOMPARE(model.revision(),revision);
        QVERIFY(!model.moveGroup({},second,Placement::Top)); QCOMPARE(model.toJson(),before); QCOMPARE(model.revision(),revision);
        QVERIFY(!model.moveGroupToEdge(first,{},Edge::Left,true)); QCOMPARE(model.toJson(),before);
        QVERIFY(!model.moveGroupToEdge(first,root,Edge(99),true)); QCOMPARE(model.toJson(),before);
        QVERIFY(model.detachGroup(first,WindowKind(99)).isNull()); QCOMPARE(model.toJson(),before);
        QVERIFY(model.setGroupPinned(second,false)); const auto hidden = model.toJson(); const auto hiddenRevision = model.revision();
        QVERIFY(!model.swapGroups(first,second)); QCOMPARE(model.toJson(),hidden);
        QVERIFY(!model.mergeGroups(second,first)); QCOMPARE(model.toJson(),hidden);
        QVERIFY(!model.moveGroup(first,second,Placement::Top)); QCOMPARE(model.toJson(),hidden); QCOMPARE(model.revision(),hiddenRevision);
        QVERIFY(!model.moveGroup(second,first,Placement::Top)); QCOMPARE(model.toJson(),hidden); QCOMPARE(model.revision(),hiddenRevision);
        const auto other = model.createWindow(); const auto final = model.toJson();
        QVERIFY(!model.moveGroupToWindow(second,other)); QCOMPARE(model.toJson(),final);
        QVERIFY(model.detachGroup(second).isNull()); QCOMPARE(model.toJson(),final); QVERIFY(model.validate());
    }
};
QTEST_GUILESS_MAIN(TabOrderingModelTest)
#include "tab_ordering_model_test.moc"
