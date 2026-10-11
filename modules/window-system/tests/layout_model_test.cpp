#include <window_system/layout_model.hpp>

#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>

#include <limits>

using namespace window_system;

class LayoutModelTest final : public QObject {
    Q_OBJECT

private slots:
    void recursiveGroupsKeepTabsAndCollapseEmptyBranches();
    void edgeGroupsDoNotSplitTheCentralArea();
    void movingTheLastTabToAFloatingWindowKeepsTheRoot();
    void promotionAndDemotionKeepIdsAndTransferTheWholeFamily();
    void centerMergeUsesDocumentedOrderAndKeepsPaneIdentity();
    void directionalMergePreservesSourceGroupsAndNormalizesEdges();
    void paneExtractionMovesTheExistingPane();
    void failedOperationsAreAtomic();
    void snapshotsRoundTripAllLayers();
    void malformedSnapshotsAreRejectedWithoutChangingTheModel();
    void duplicateAndOrphanReferencesAreRejected();
    void closeWorkbenchClosesOnlyItsOwnFamily();
    void splitRatiosAreBoundedAndImmutableSnapshotsStayUnchanged();
    void directionalTabMovementIntoAnEmptyWindowCreatesOneGroup();
    void wholeWindowMovementIntoAnEmptyWindowSupportsBothPolicies();
    void movingOneTabFromAPinnedGroupKeepsTheOtherTabsPinned();
};

void LayoutModelTest::recursiveGroupsKeepTabsAndCollapseEmptyBranches()
{
    LayoutModel model;
    const auto window = model.createWindow();
    const auto a = model.addTab(window, "A");
    const auto b = model.addTab(window, "B");
    const auto c = model.addTab(window, "C");
    const auto originalGroup = model.groupForTab(a);
    QVERIFY(model.moveTab(b, originalGroup, Placement::Right).ok);
    const auto rightGroup = model.groupForTab(b);
    QVERIFY(model.moveTab(c, rightGroup, Placement::Bottom).ok);
    QCOMPARE(model.centralGroups(window), (QVector<GroupId>{originalGroup, rightGroup, model.groupForTab(c)}));
    QVERIFY(!model.window(window)->central->isLeaf());
    QCOMPARE(model.window(window)->central->orientation, Qt::Horizontal);
    QVERIFY(model.removeTab(b).ok);
    QCOMPARE(model.centralGroups(window).size(), 2);
    QVERIFY(!model.group(rightGroup));
    QVERIFY(model.moveTab(c, originalGroup).ok);
    QVERIFY(model.window(window)->central->isLeaf());
    QCOMPARE(model.group(originalGroup)->tabs, (QVector<TabId>{a, c}));
    QVERIFY(model.validate().ok);
}

void LayoutModelTest::edgeGroupsDoNotSplitTheCentralArea()
{
    LayoutModel model;
    const auto window = model.createWindow();
    const auto centralTab = model.addTab(window, "Terminal");
    const auto sideTab = model.addTab(window, "Sessions");
    QVERIFY(model.moveTab(sideTab, model.groupForTab(centralTab), Placement::Left).ok);
    const auto side = model.groupForTab(sideTab);
    QVERIFY(model.moveGroupToEdge(side, Edge::Right).ok);
    QCOMPARE(model.centralGroups(window), (QVector<GroupId>{model.groupForTab(centralTab)}));
    QCOMPARE(model.group(side)->location, GroupLocation::AutoHide);
    QCOMPARE(model.group(side)->edge, Edge::Right);
    QVERIFY(model.setGroupPinned(side, true).ok);
    QCOMPARE(model.group(side)->location, GroupLocation::Pinned);
    QVERIFY(model.setGroupPinned(side, false).ok);
    QVERIFY(model.moveGroupToCenter(side, model.groupForTab(centralTab), Placement::Bottom).ok);
    QVERIFY(model.window(window)->edgeGroups.isEmpty());
    QCOMPARE(model.window(window)->central->orientation, Qt::Vertical);
    QVERIFY(model.validate().ok);
}

void LayoutModelTest::movingTheLastTabToAFloatingWindowKeepsTheRoot()
{
    LayoutModel model;
    const auto root = model.createWindow();
    const auto tab = model.addTab(root, "Only tab");
    const auto pane = model.tab(tab)->activePane;
    const auto family = model.window(root)->familyId;
    const auto floating = model.detachTab(tab);
    QVERIFY(!floating.isNull());
    QCOMPARE(model.window(floating)->kind, WindowKind::Floating);
    QCOMPARE(model.window(floating)->familyId, family);
    QVERIFY(model.window(root));
    QVERIFY(model.orderedTabs(root).isEmpty());
    QVERIFY(model.familyHasTabs(family));
    QCOMPARE(model.tab(tab)->activePane, pane);
    QVERIFY(model.moveTabToWindow(tab, root).ok);
    QVERIFY(!model.window(floating));
    QCOMPARE(model.orderedTabs(root), (QVector<TabId>{tab}));
    QVERIFY(model.validate().ok);
}

void LayoutModelTest::promotionAndDemotionKeepIdsAndTransferTheWholeFamily()
{
    LayoutModel model;
    const auto root = model.createWindow();
    const auto a = model.addTab(root, "A");
    const auto b = model.addTab(root, "B");
    const auto floatingA = model.detachTab(a);
    const auto floatingB = model.detachTab(b);
    const auto originalFamily = model.window(root)->familyId;
    QVERIFY(model.promoteWindow(floatingA).ok);
    const auto newFamily = model.window(floatingA)->familyId;
    QVERIFY(newFamily != originalFamily);
    QCOMPARE(model.window(floatingB)->familyId, originalFamily);
    QVERIFY(model.demoteWindow(root, floatingA).ok);
    QCOMPARE(model.window(root)->kind, WindowKind::Floating);
    QCOMPARE(model.window(root)->familyId, newFamily);
    QCOMPARE(model.window(floatingB)->familyId, newFamily);
    QCOMPARE(model.familyRoot(newFamily), floatingA);
    QCOMPARE(model.orderedTabs(floatingA), (QVector<TabId>{a}));
    QCOMPARE(model.orderedTabs(floatingB), (QVector<TabId>{b}));
    const auto anotherRoot = model.createWindow();
    QVERIFY(model.reassignFamily(floatingB, anotherRoot).ok);
    QCOMPARE(model.window(floatingB)->familyId, model.window(anotherRoot)->familyId);
    QVERIFY(model.validate().ok);
}

void LayoutModelTest::centerMergeUsesDocumentedOrderAndKeepsPaneIdentity()
{
    LayoutModel model;
    const auto destination = model.createWindow();
    const auto existing = model.addTab(destination, "Existing");
    const auto source = model.createWindow(WindowKind::Floating, destination);
    const auto first = model.addTab(source, "First central");
    const auto second = model.addTab(source, "Second central");
    const auto top = model.addTab(source, "Top edge");
    const auto right = model.addTab(source, "Right edge");
    QVERIFY(model.moveTab(second, model.groupForTab(first), Placement::Right).ok);
    QVERIFY(model.moveTab(top, model.groupForTab(first), Placement::Top).ok);
    QVERIFY(model.moveGroupToEdge(model.groupForTab(top), Edge::Top, true).ok);
    QVERIFY(model.moveTab(right, model.groupForTab(first), Placement::Left).ok);
    QVERIFY(model.moveGroupToEdge(model.groupForTab(right), Edge::Right).ok);
    QVERIFY(model.activateTab(second).ok);
    const auto extraPane = model.splitPane(first, model.tab(first)->activePane, Placement::Bottom);
    const auto paneTree = model.tab(first)->panes;
    QVERIFY(model.moveAllTabs(source, model.groupForTab(existing)).ok);
    QCOMPARE(model.group(model.groupForTab(existing))->tabs,
             (QVector<TabId>{existing, first, second, top, right}));
    QCOMPARE(model.group(model.groupForTab(existing))->activeTab, second);
    QCOMPARE(model.tab(first)->panes, paneTree);
    QCOMPARE(model.tab(first)->activePane, extraPane);
    QVERIFY(!model.window(source));
    QVERIFY(model.validate().ok);
}

void LayoutModelTest::directionalMergePreservesSourceGroupsAndNormalizesEdges()
{
    LayoutModel model;
    const auto destination = model.createWindow();
    const auto existing = model.addTab(destination, "Destination");
    const auto source = model.createWindow();
    const auto a = model.addTab(source, "A");
    const auto b = model.addTab(source, "B");
    const auto hidden = model.addTab(source, "Hidden");
    const auto groupA = model.groupForTab(a);
    QVERIFY(model.moveTab(b, groupA, Placement::Bottom).ok);
    const auto groupB = model.groupForTab(b);
    const auto sourceCentral = model.window(source)->central;
    QVERIFY(model.setSplitRatio(sourceCentral->id, 0.35).ok);
    QVERIFY(model.moveTab(hidden, groupA, Placement::Right).ok);
    const auto hiddenGroup = model.groupForTab(hidden);
    QVERIFY(model.moveGroupToEdge(hiddenGroup, Edge::Left).ok);
    const auto preserved = model.window(source)->central;
    QVERIFY(model.moveAllTabs(source, model.groupForTab(existing), Placement::Right).ok);
    QVERIFY(model.window(source)); // Complete empty shells are a host policy.
    QVERIFY(model.orderedTabs(source).isEmpty());
    const auto inserted = model.window(destination)->central->second;
    QCOMPARE(inserted->second, preserved);
    QCOMPARE(inserted->second->ratio, 0.35);
    QCOMPARE(model.group(groupA)->windowId, destination);
    QCOMPARE(model.group(groupB)->windowId, destination);
    QCOMPARE(model.group(hiddenGroup)->location, GroupLocation::Central);
    QCOMPARE(model.group(hiddenGroup)->windowId, destination);
    QVERIFY(model.window(destination)->edgeGroups.isEmpty());
    QVERIFY(model.validate().ok);
}

void LayoutModelTest::paneExtractionMovesTheExistingPane()
{
    LayoutModel model;
    const auto window = model.createWindow();
    const auto tab = model.addTab(window, "Original");
    const auto originalPane = model.tab(tab)->activePane;
    const auto addedPane = model.splitPane(tab, originalPane, Placement::Right);
    const auto group = model.groupForTab(tab);
    const auto extracted = model.extractPane(tab, addedPane, group, Placement::Bottom);
    QVERIFY(!extracted.isNull());
    QCOMPARE(model.panes(tab), (QVector<PaneId>{originalPane}));
    QCOMPARE(model.panes(extracted), (QVector<PaneId>{addedPane}));
    QCOMPARE(model.tab(extracted)->activePane, addedPane);
    QVERIFY(model.removePane(tab, originalPane).ok);
    QVERIFY(!model.tab(tab));
    QCOMPARE(model.centralGroups(window).size(), 1);
    const auto lastExtraction = model.extractPane(extracted, addedPane, model.groupForTab(extracted));
    QVERIFY(!lastExtraction.isNull());
    QVERIFY(!model.tab(extracted));
    QCOMPARE(model.panes(lastExtraction), (QVector<PaneId>{addedPane}));
    QVERIFY(model.validate().ok);
}

void LayoutModelTest::failedOperationsAreAtomic()
{
    LayoutModel model;
    const auto root = model.createWindow();
    const auto a = model.addTab(root, "A");
    const auto before = model.toJson();
    const auto revision = model.revision();
    QVERIFY(!model.moveTab(a, QUuid::createUuid()).ok);
    QVERIFY(!model.demoteWindow(root, root).ok);
    QVERIFY(model.detachTab(QUuid::createUuid()).isNull());
    QVERIFY(model.splitPane(a, model.tab(a)->activePane, Placement::Center).isNull());
    QVERIFY(!model.moveAllTabs(root, model.groupForTab(a)).ok);
    QVERIFY(!model.setGroupPinned(model.groupForTab(a), true).ok);
    QCOMPARE(model.toJson(), before);
    QCOMPARE(model.revision(), revision);
}

void LayoutModelTest::snapshotsRoundTripAllLayers()
{
    LayoutModel original;
    const auto root = original.createWindow();
    const auto a = original.addTab(root, QString::fromUtf8("终端 A"));
    const auto b = original.addTab(root, "B");
    QVERIFY(!original.splitPane(a, original.tab(a)->activePane, Placement::Bottom).isNull());
    const auto floating = original.detachTab(b);
    QVERIFY(original.moveGroupToEdge(original.groupForTab(a), Edge::Top, true).ok);
    QVERIFY(!original.addTab(floating, "C").isNull());
    const auto snapshot = original.toJson();
    LayoutModel restored;
    QVERIFY(restored.fromJson(snapshot).ok);
    QCOMPARE(restored.toJson(), snapshot);
    QCOMPARE(restored.windowOrder(), original.windowOrder());
    QCOMPARE(restored.panes(a), original.panes(a));
    QVERIFY(restored.validate().ok);
}

void LayoutModelTest::malformedSnapshotsAreRejectedWithoutChangingTheModel()
{
    LayoutModel model;
    const auto window = model.createWindow();
    model.addTab(window, "Retained");
    const auto before = model.toJson();
    const auto revision = model.revision();
    auto bad = before;
    bad.insert("schemaVersion", 2);
    QVERIFY(!model.fromJson(bad).ok);
    bad = before;
    auto windows = bad.value("windows").toArray();
    auto record = windows[0].toObject();
    record.insert("familyId", "invalid");
    windows[0] = record;
    bad.insert("windows", windows);
    QVERIFY(!model.fromJson(bad).ok);
    bad = before;
    auto tabs = bad.value("tabs").toArray();
    auto tabRecord = tabs[0].toObject();
    tabRecord.insert("panes", QJsonValue(QJsonValue::Null));
    tabs[0] = tabRecord;
    bad.insert("tabs", tabs);
    QVERIFY(!model.fromJson(bad).ok);
    QCOMPARE(model.toJson(), before);
    QCOMPARE(model.revision(), revision);
}

void LayoutModelTest::duplicateAndOrphanReferencesAreRejected()
{
    LayoutModel model;
    const auto window = model.createWindow();
    model.addTab(window, "Retained");
    const auto before = model.toJson();
    auto bad = before;
    auto tabs = bad.value("tabs").toArray();
    tabs.append(tabs[0]);
    bad.insert("tabs", tabs);
    QVERIFY(!model.fromJson(bad).ok);
    bad = before;
    auto groups = bad.value("groups").toArray();
    auto group = groups[0].toObject();
    auto groupTabs = group.value("tabs").toArray();
    groupTabs.append(groupTabs[0]);
    group.insert("tabs", groupTabs);
    groups[0] = group;
    bad.insert("groups", groups);
    QVERIFY(!model.fromJson(bad).ok);
    bad = before;
    auto windows = bad.value("windows").toArray();
    auto record = windows[0].toObject();
    record.insert("central", QJsonValue(QJsonValue::Null));
    record.insert("activeGroup", "");
    windows[0] = record;
    bad.insert("windows", windows);
    QVERIFY(!model.fromJson(bad).ok);
    QCOMPARE(model.toJson(), before);
}

void LayoutModelTest::closeWorkbenchClosesOnlyItsOwnFamily()
{
    LayoutModel model;
    const auto root = model.createWindow();
    const auto tab = model.addTab(root, "A");
    const auto floating = model.detachTab(tab);
    const auto otherRoot = model.createWindow();
    const auto retained = model.addTab(otherRoot, "Retained");
    const auto promoted = model.detachTab(retained);
    QVERIFY(model.promoteWindow(promoted).ok);
    QVERIFY(model.closeWindow(root).ok);
    QVERIFY(!model.window(root));
    QVERIFY(!model.window(floating));
    QVERIFY(!model.tab(tab));
    QVERIFY(model.window(otherRoot));
    QVERIFY(model.window(promoted));
    QVERIFY(model.tab(retained));
    QVERIFY(model.validate().ok);
}

void LayoutModelTest::splitRatiosAreBoundedAndImmutableSnapshotsStayUnchanged()
{
    LayoutModel model;
    const auto window = model.createWindow();
    const auto a = model.addTab(window, "A");
    const auto b = model.addTab(window, "B");
    QVERIFY(model.moveTab(b, model.groupForTab(a), Placement::Right).ok);
    const auto snapshot = model.window(window)->central;
    QVERIFY(model.setSplitRatio(snapshot->id, 0.75).ok);
    QCOMPARE(model.window(window)->central->ratio, 0.75);
    QCOMPARE(snapshot->ratio, 0.5);
    const auto revision = model.revision();
    QVERIFY(!model.setSplitRatio(snapshot->id, 0.999).ok);
    QVERIFY(!model.setSplitRatio(snapshot->id, std::numeric_limits<double>::quiet_NaN()).ok);
    QCOMPARE(model.revision(), revision);
    QVERIFY(model.validate().ok);
}

void LayoutModelTest::directionalTabMovementIntoAnEmptyWindowCreatesOneGroup()
{
    for (auto direction : {Placement::Left, Placement::Right, Placement::Top, Placement::Bottom}) {
        LayoutModel model;
        const auto source = model.createWindow();
        const auto moved = model.addTab(source, "Moved");
        const auto destination = model.createWindow();
        const auto pane = model.tab(moved)->activePane;
        const auto result = model.moveTabToWindow(moved, destination, direction);
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(model.centralGroups(destination).size(), 1);
        QVERIFY(model.window(destination)->central->isLeaf());
        QCOMPARE(model.orderedTabs(destination), (QVector<TabId>{moved}));
        QCOMPARE(model.tab(moved)->activePane, pane);
        QVERIFY(model.orderedTabs(source).isEmpty());
        QVERIFY(model.window(source));
        QVERIFY(model.validate().ok);
    }
}

void LayoutModelTest::wholeWindowMovementIntoAnEmptyWindowSupportsBothPolicies()
{
    for (auto placement : {Placement::Center, Placement::Left, Placement::Right, Placement::Top, Placement::Bottom}) {
        LayoutModel model;
        const auto owner = model.createWindow();
        const auto source = model.createWindow(WindowKind::Floating, owner);
        const auto a = model.addTab(source, "A");
        const auto b = model.addTab(source, "B");
        QVERIFY(model.moveTab(b, model.groupForTab(a), Placement::Bottom).ok);
        const auto originalTree = model.window(source)->central;
        const auto groupA = model.groupForTab(a);
        const auto groupB = model.groupForTab(b);
        const auto paneA = model.tab(a)->activePane;
        const auto destination = model.createWindow();
        const auto result = model.moveAllTabsToWindow(source, destination, placement);
        QVERIFY2(result.ok, qPrintable(result.error));
        QVERIFY(!model.window(source));
        QCOMPARE(model.orderedTabs(destination), (QVector<TabId>{a, b}));
        QCOMPARE(model.tab(a)->activePane, paneA);
        if (placement == Placement::Center) {
            QCOMPARE(model.centralGroups(destination).size(), 1);
            QVERIFY(model.window(destination)->central->isLeaf());
            QCOMPARE(model.groupForTab(a), model.groupForTab(b));
        } else {
            QCOMPARE(model.centralGroups(destination).size(), 2);
            QCOMPARE(model.window(destination)->central, originalTree);
            QCOMPARE(model.groupForTab(a), groupA);
            QCOMPARE(model.groupForTab(b), groupB);
        }
        QVERIFY(model.validate().ok);
        const auto snapshot = model.toJson();
        const auto revision = model.revision();
        QVERIFY(!model.moveAllTabsToWindow(destination, destination, placement).ok);
        QVERIFY(!model.moveAllTabsToWindow(destination, QUuid::createUuid(), placement).ok);
        QCOMPARE(model.toJson(), snapshot);
        QCOMPARE(model.revision(), revision);
    }
}

void LayoutModelTest::movingOneTabFromAPinnedGroupKeepsTheOtherTabsPinned()
{
    LayoutModel model;
    const auto window = model.createWindow();
    const auto first = model.addTab(window, "Remain first");
    const auto moved = model.addTab(window, "Move only this tab");
    const auto last = model.addTab(window, "Remain last");
    const auto originalGroup = model.groupForTab(first);
    QVERIFY(model.moveGroupToEdge(originalGroup, Edge::Right, true).ok);
    const auto retainedPane = model.tab(moved)->activePane;
    const auto result = model.moveTabToEdge(moved, Edge::Left);
    QVERIFY2(result.ok, qPrintable(result.error));
    const auto newGroup = model.groupForTab(moved);
    QVERIFY(newGroup != originalGroup);
    QCOMPARE(model.group(originalGroup)->tabs, (QVector<TabId>{first, last}));
    QCOMPARE(model.group(originalGroup)->location, GroupLocation::Pinned);
    QCOMPARE(model.group(originalGroup)->edge, Edge::Right);
    QCOMPARE(model.group(newGroup)->tabs, (QVector<TabId>{moved}));
    QCOMPARE(model.group(newGroup)->location, GroupLocation::AutoHide);
    QCOMPARE(model.group(newGroup)->edge, Edge::Left);
    QCOMPARE(model.tab(moved)->activePane, retainedPane);
    QVERIFY(model.centralGroups(window).isEmpty());
    QCOMPARE(model.window(window)->edgeGroups.size(), 2);
    QVERIFY(model.validate().ok);
}

QTEST_GUILESS_MAIN(LayoutModelTest)
#include "layout_model_test.moc"
