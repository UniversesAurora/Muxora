#include <window_system/layout_model.hpp>

#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>

#include <limits>

using namespace window_system;

namespace window_system {
struct LayoutModelTestAccess {
    static bool indexValid(const LayoutModel& model) { return model.indexValid_; }
    static void corruptTabGroup(LayoutModel& model, TabId tab, GroupId group) { model.tabGroup_.insert(tab, group); }
    // "<kind>:<owner>[:<edge>]", or empty when the index has no entry.
    static QString owner(const LayoutModel& model, QUuid node)
    {
        const auto owner = model.ownerOfNode(node);
        if (!owner) return {};
        switch (owner->kind) {
        case LayoutModel::NodeOwner::Kind::Central: return "central:" + owner->owner.toString();
        case LayoutModel::NodeOwner::Kind::Pinned:
            return "pinned:" + owner->owner.toString() + ":" + QString::number(int(owner->edge));
        case LayoutModel::NodeOwner::Kind::Tab: return "tab:" + owner->owner.toString();
        }
        return {};
    }
};
} // namespace window_system

namespace {

void expectedOwners(const LayoutTree& tree, const QString& owner, QHash<QUuid, QString>& owners)
{
    if (!tree || tree->isLeaf()) return;
    owners.insert(tree->id, owner);
    expectedOwners(tree->first, owner, owners);
    expectedOwners(tree->second, owner, owners);
}

// Compares every indexed lookup with a scan of the public records. Seen IDs
// include removed tabs and panes, which must no longer resolve.
QString indexMismatch(const LayoutModel& model, const QSet<TabId>& seenTabs, const QSet<PaneId>& seenPanes,
                      const QSet<QUuid>& seenNodes)
{
    if (!LayoutModelTestAccess::indexValid(model)) return "index invalid";
    QHash<TabId, GroupId> groups;
    QHash<PaneId, TabId> panes;
    QHash<QUuid, QString> owners;
    for (const auto& group : model.groups())
        for (const auto tab : group.tabs) groups.insert(tab, group.id);
    for (const auto& tab : model.tabs()) {
        for (const auto pane : model.panes(tab.id)) panes.insert(pane, tab.id);
        expectedOwners(tab.panes, "tab:" + tab.id.toString(), owners);
    }
    for (const auto& window : model.windows()) {
        expectedOwners(window.central, "central:" + window.id.toString(), owners);
        for (const auto edge : {Edge::Left, Edge::Right, Edge::Top, Edge::Bottom})
            expectedOwners(window.pinnedLayouts[size_t(edge)],
                           "pinned:" + window.id.toString() + ":" + QString::number(int(edge)), owners);
    }
    for (const auto tab : seenTabs)
        if (model.groupForTab(tab) != groups.value(tab)) return "groupForTab " + tab.toString();
    for (const auto pane : seenPanes)
        if (model.tabForPane(pane) != panes.value(pane)) return "tabForPane " + pane.toString();
    for (const auto node : seenNodes)
        if (LayoutModelTestAccess::owner(model, node) != owners.value(node)) return "ownerOfNode " + node.toString();
    if (const auto result = model.validate(); !result) return result.error;
    return {};
}

} // namespace

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
    void individualPinningRestoresGroupAndTabPositionsInAnyOrder();
    void pinningEveryEdgeTabPreservesMixedGroupsAndSnapshotPositions();
    void explicitPinnedDropsCreateAnOuterPartition();
    void wholeWindowCenterInsertionUsesTheDropIndex();
    void wholeWindowEdgeMovementWorksWithoutCentralGroups();
    void autoHideInsertionUsesTheWholeVisibleSide_data();
    void autoHideInsertionUsesTheWholeVisibleSide();
    void edgeBarTransfersAreAtomicAndKeepThePayloadOrder();
    void peekSizesBelongToIndividualTabsAndResetIndependently();
    void peekSizesSurvivePinningAndCrossWindowMoves_data();
    void peekSizesSurvivePinningAndCrossWindowMoves();
    void peekSizeBoundariesRoundTrip_data();
    void peekSizeBoundariesRoundTrip();
    void invalidPeekSizeOperationsAreAtomic_data();
    void invalidPeekSizeOperationsAreAtomic();
    void peekSizeOperationsRequireAnExistingTab();
    void oldSnapshotsLeavePeekSizesAbsent();
    void malformedPeekSizesAreRejectedAtomically_data();
    void malformedPeekSizesAreRejectedAtomically();
    void extractedPanesReceiveTheirOwnDefaultPeekSize();
    void directionalPeekPoliciesShareOppositeSidesAndKeepOtherTabsIndependent();
    void directionalPeekPoliciesKeepLegacyStretchAndResetCompatibility();
    void invalidDirectionalPeekPoliciesAreAtomic_data();
    void invalidDirectionalPeekPoliciesAreAtomic();
    void malformedDirectionalPeekPoliciesAreAtomic_data();
    void malformedDirectionalPeekPoliciesAreAtomic();
    void directionalPeekPolicyBoundariesRoundTrip_data();
    void directionalPeekPolicyBoundariesRoundTrip();
    void indexFollowsEveryMutation();
    void failedTransactionKeepsIndex();
    void validateReportsAStaleIndex();
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
    QVERIFY(!model.moveTabToEdge(a, static_cast<Edge>(99)).ok);
    QVERIFY(!model.setEdgePinned(root, static_cast<Edge>(99), true).ok);
    QVERIFY(model.createWindow(static_cast<WindowKind>(99)).isNull());
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

void LayoutModelTest::individualPinningRestoresGroupAndTabPositionsInAnyOrder()
{
    LayoutModel model;
    const auto window = model.createWindow();
    const auto a = model.addTab(window, "A");
    const auto b = model.addTab(window, "B");
    const auto c = model.addTab(window, "C");
    const auto original = model.groupForTab(a);
    QVERIFY(model.moveGroupToEdge(original, Edge::Left, true).ok);
    QVERIFY(model.setEdgeGroupExtent(original, 347).ok);
    QVERIFY(model.setTabPinned(a, false).ok);
    QCOMPARE(model.group(original)->tabs, (QVector<TabId>{b, c}));
    QCOMPARE(model.group(model.groupForTab(a))->location, GroupLocation::AutoHide);
    QVERIFY(model.setTabPinned(b, false).ok);
    QVERIFY(model.setTabPinned(c, false).ok);
    QVERIFY(!model.group(original));
    QVERIFY(model.setTabPinned(c, true).ok);
    QVERIFY(model.setTabPinned(b, true).ok);
    QVERIFY(model.setTabPinned(a, true).ok);
    QCOMPARE(model.groupForTab(a), original);
    QCOMPARE(model.group(original)->tabs, (QVector<TabId>{a, b, c}));
    QCOMPARE(model.group(original)->edgeExtent, 347);
    QCOMPARE(model.window(window)->edgeGroups, (QVector<GroupId>{original}));
    QVERIFY(model.validate().ok);
}
void LayoutModelTest::pinningEveryEdgeTabPreservesMixedGroupsAndSnapshotPositions()
{
    LayoutModel model;
    const auto window = model.createWindow();
    const auto a = model.addTab(window, "A");
    const auto b = model.addTab(window, "B");
    const auto other = model.addTab(window, "Other");
    QVERIFY(model.moveTabToEdge(other, Edge::Right, true).ok);
    const auto original = model.groupForTab(a);
    QVERIFY(model.moveGroupToEdge(original, Edge::Left, true).ok);
    QVERIFY(model.setTabPinned(a, false).ok);
    LayoutModel restored;
    QVERIFY(restored.fromJson(model.toJson()).ok);
    QCOMPARE(restored.toJson(), model.toJson());
    QVERIFY(restored.setEdgePinned(window, Edge::Left, false).ok);
    QVERIFY(!restored.group(original));
    QCOMPARE(restored.group(restored.groupForTab(other))->location, GroupLocation::Pinned);
    QVERIFY(restored.setEdgePinned(window, Edge::Left, true).ok);
    QCOMPARE(restored.group(original)->tabs, (QVector<TabId>{a, b}));
    QVERIFY(restored.validate().ok);
    const auto before = restored.toJson();
    QVERIFY(!restored.setTabPinned(QUuid::createUuid(), true).ok);
    QVERIFY(!restored.setEdgeGroupExtent(original, 1).ok);
    QCOMPARE(restored.toJson(), before);
    const auto untouchedWindow = restored.createWindow();
    const auto c = restored.addTab(untouchedWindow, "C");
    const auto d = restored.addTab(untouchedWindow, "D");
    const auto untouched = restored.groupForTab(c);
    QVERIFY(restored.moveGroupToEdge(untouched, Edge::Top).ok);
    QVERIFY(restored.setEdgePinned(untouchedWindow, Edge::Top, true).ok);
    QCOMPARE(restored.group(untouched)->tabs, (QVector<TabId>{c, d}));
    QCOMPARE(restored.group(untouched)->location, GroupLocation::Pinned);
}
void LayoutModelTest::explicitPinnedDropsCreateAnOuterPartition()
{
    LayoutModel model;
    const auto window = model.createWindow();
    const auto inner = model.addTab(window, "Inner");
    const auto outer = model.addTab(window, "Outer");
    QVERIFY(model.moveTabToEdge(inner, Edge::Left, true).ok);
    QVERIFY(model.moveTabToEdge(outer, Edge::Left, true).ok);
    QCOMPARE(model.window(window)->edgeGroups,
             (QVector<GroupId>{model.groupForTab(outer), model.groupForTab(inner)}));
    QCOMPARE(model.group(model.groupForTab(inner))->tabs, (QVector<TabId>{inner}));
    const auto ordered = model.window(window)->edgeGroups;
    QVERIFY(model.setTabPinned(outer, false).ok);
    QVERIFY(model.setTabPinned(inner, false).ok);
    QVERIFY(model.setTabPinned(outer, true).ok);
    QVERIFY(model.setTabPinned(inner, true).ok);
    QCOMPARE(model.window(window)->edgeGroups, ordered);
    QVERIFY(model.validate().ok);
}
void LayoutModelTest::wholeWindowCenterInsertionUsesTheDropIndex()
{
    LayoutModel model;
    const auto destination = model.createWindow();
    const auto a = model.addTab(destination, "A");
    const auto b = model.addTab(destination, "B");
    const auto source = model.createWindow(WindowKind::Floating, destination);
    const auto c = model.addTab(source, "C");
    const auto d = model.addTab(source, "D");
    QVERIFY(model.moveAllTabs(source, model.groupForTab(a), Placement::Center, 1).ok);
    QCOMPARE(model.group(model.groupForTab(a))->tabs, (QVector<TabId>{a, c, d, b}));
    QVERIFY(!model.window(source));
    QVERIFY(model.validate().ok);
}

void LayoutModelTest::wholeWindowEdgeMovementWorksWithoutCentralGroups()
{
    LayoutModel model;
    const auto destination = model.createWindow();
    const auto existing = model.addTab(destination, "Existing");
    QVERIFY(model.moveTabToEdge(existing, Edge::Left, true).ok);
    const auto previousGroup = model.groupForTab(existing);
    const auto source = model.createWindow(WindowKind::Floating, destination);
    const auto first = model.addTab(source, "First");
    const auto second = model.addTab(source, "Second");
    const auto sourcePane = model.tab(first)->activePane;
    const auto extraPane = model.splitPane(first, sourcePane, Placement::Right);
    const auto tree = model.tab(first)->panes;
    QVERIFY(model.moveTabToEdge(second, Edge::Bottom).ok);
    const auto movingOrder = model.orderedTabs(source);
    QVERIFY(model.centralGroups(destination).isEmpty());
    QVERIFY(model.moveAllTabsToEdge(source, destination, Edge::Left, true).ok);
    const auto inserted = model.groupForTab(first);
    QCOMPARE(model.group(inserted)->tabs, movingOrder);
    QCOMPARE(model.group(inserted)->location, GroupLocation::Pinned);
    QCOMPARE(model.window(destination)->edgeGroups, (QVector<GroupId>{inserted, previousGroup}));
    QCOMPARE(model.group(previousGroup)->tabs, (QVector<TabId>{existing}));
    QCOMPARE(model.tab(first)->panes, tree);
    QCOMPARE(model.panes(first), (QVector<PaneId>{sourcePane, extraPane}));
    QVERIFY(!model.window(source));
    QVERIFY(model.centralGroups(destination).isEmpty());
    QVERIFY(model.validate().ok);
    const auto before = model.toJson();
    QVERIFY(!model.moveAllTabsToEdge(destination, destination, Edge::Right).ok);
    QVERIFY(!model.moveAllTabsToEdge(QUuid::createUuid(), destination, Edge::Right).ok);
    QCOMPARE(model.toJson(), before);
    const auto empty = model.createWindow();
    QVERIFY(!model.moveAllTabsToEdge(empty, destination, Edge::Right).ok);
}

void LayoutModelTest::autoHideInsertionUsesTheWholeVisibleSide_data()
{
    QTest::addColumn<int>("edge");
    QTest::newRow("left") << int(Edge::Left);
    QTest::newRow("right") << int(Edge::Right);
    QTest::newRow("top") << int(Edge::Top);
    QTest::newRow("bottom") << int(Edge::Bottom);
}
void LayoutModelTest::autoHideInsertionUsesTheWholeVisibleSide()
{
    QFETCH(int, edge);
    LayoutModel model;
    const auto window = model.createWindow();
    const auto a = model.addTab(window, "A");
    const auto b = model.addTab(window, "B");
    const auto c = model.addTab(window, "C");
    const auto pinned = model.addTab(window, "Pinned");
    const auto paneA = model.tab(a)->activePane;
    QVERIFY(model.moveTabToEdge(a, Edge(edge)).ok);
    QVERIFY(model.moveTab(b, model.groupForTab(a)).ok);
    QVERIFY(model.moveTabToEdge(c, Edge(edge)).ok);
    QVERIFY(model.moveTabToEdge(pinned, Edge(edge), true).ok);
    QCOMPARE(model.autoHiddenTabs(window, Edge(edge)), (QVector<TabId>{a, b, c}));
    QVERIFY(model.moveTabToEdgeBar(b, window, Edge(edge), 0).ok);
    QCOMPARE(model.autoHiddenTabs(window, Edge(edge)), (QVector<TabId>{b, a, c}));
    QVERIFY(model.moveTabToEdgeBar(a, window, Edge(edge), 2).ok);
    QCOMPARE(model.autoHiddenTabs(window, Edge(edge)), (QVector<TabId>{b, c, a}));
    QCOMPARE(model.tab(a)->activePane, paneA);
    QCOMPARE(model.group(model.groupForTab(pinned))->location, GroupLocation::Pinned);
    LayoutModel restored;
    QVERIFY(restored.fromJson(model.toJson()).ok);
    QCOMPARE(restored.autoHiddenTabs(window, Edge(edge)), (QVector<TabId>{b, c, a}));
    QCOMPARE(restored.tab(a)->activePane, paneA);
    QVERIFY(restored.validate().ok);
}
void LayoutModelTest::edgeBarTransfersAreAtomicAndKeepThePayloadOrder()
{
    LayoutModel model;
    const auto destination = model.createWindow();
    const auto first = model.addTab(destination, "First");
    const auto last = model.addTab(destination, "Last");
    QVERIFY(model.moveTabToEdge(first, Edge::Left).ok);
    QVERIFY(model.moveTabToEdge(last, Edge::Left).ok);
    const auto source = model.createWindow(WindowKind::Floating, destination);
    const auto a = model.addTab(source, "A");
    const auto b = model.addTab(source, "B");
    const auto c = model.addTab(source, "C");
    const auto panes = model.panes(a);
    QVERIFY(model.moveTabToEdgeBar(a, destination, Edge::Left, 1).ok);
    QCOMPARE(model.autoHiddenTabs(destination, Edge::Left), (QVector<TabId>{first, a, last}));
    QCOMPARE(model.panes(a), panes);
    const auto before = model.toJson();
    QVERIFY(!model.moveTabToEdgeBar(b, destination, Edge(99), 0).ok);
    QVERIFY(!model.moveAllTabsToEdgeBar(source, destination, Edge::Left, -2).ok);
    QCOMPARE(model.toJson(), before);
    QVERIFY(model.moveAllTabsToEdgeBar(source, destination, Edge::Left, 1).ok);
    QCOMPARE(model.autoHiddenTabs(destination, Edge::Left), (QVector<TabId>{first, b, c, a, last}));
    QVERIFY(!model.window(source));
    const auto appendedSource = model.createWindow(WindowKind::Floating, destination);
    const auto d = model.addTab(appendedSource, "D");
    const auto e = model.addTab(appendedSource, "E");
    QVERIFY(model.moveAllTabsToEdgeBar(appendedSource, destination, Edge::Left, std::numeric_limits<int>::max()).ok);
    QCOMPARE(model.autoHiddenTabs(destination, Edge::Left), (QVector<TabId>{first, b, c, a, last, d, e}));
    QVERIFY(!model.window(appendedSource));
    const auto edgeOnly = model.createWindow();
    const auto soloWindow = model.detachTab(c);
    QVERIFY(model.moveTabToEdgeBar(c, edgeOnly, Edge::Bottom).ok);
    QVERIFY(!model.window(soloWindow));
    QVERIFY(model.centralGroups(edgeOnly).isEmpty());
    QCOMPARE(model.autoHiddenTabs(edgeOnly, Edge::Bottom), (QVector<TabId>{c}));
    QVERIFY(model.validate().ok);
}

void LayoutModelTest::peekSizesBelongToIndividualTabsAndResetIndependently()
{
    LayoutModel model;
    const auto window = model.createWindow();
    const auto first = model.addTab(window, "First");
    const auto second = model.addTab(window, "Second");
    const auto group = model.groupForTab(first);
    const auto paneTree = model.tab(first)->panes;
    QVERIFY(!model.tab(first)->peekSize);
    QVERIFY(!model.tab(second)->peekSize);
    QVERIFY(model.setPeekSize(first, QSize(430, 210)).ok);
    QCOMPARE(*model.tab(first)->peekSize, QSize(430, 210));
    QVERIFY(!model.tab(second)->peekSize);
    QVERIFY(model.moveGroupToEdge(group, Edge::Left).ok);
    QVERIFY(model.setEdgeGroupExtent(group, 379).ok);
    QVERIFY(model.setPeekSize(second, QSize(610, 360)).ok);
    QCOMPARE(*model.tab(first)->peekSize, QSize(430, 210));
    QCOMPARE(*model.tab(second)->peekSize, QSize(610, 360));
    QCOMPARE(model.group(group)->tabs, (QVector<TabId>{first, second}));
    QCOMPARE(model.group(group)->edgeExtent, 379);
    QCOMPARE(model.tab(first)->panes, paneTree);
    QVERIFY(model.setPeekSize(second, std::nullopt).ok);
    QVERIFY(!model.tab(second)->peekSize);
    QCOMPARE(*model.tab(first)->peekSize, QSize(430, 210));
    QCOMPARE(model.group(group)->edgeExtent, 379);
    const auto tabs = model.toJson().value("tabs").toArray();
    QVERIFY(tabs[0].toObject().contains("peekSize"));
    QVERIFY(!tabs[1].toObject().contains("peekSize"));
    QVERIFY(model.validate().ok);
}

void LayoutModelTest::peekSizesSurvivePinningAndCrossWindowMoves_data()
{
    QTest::addColumn<int>("edge");
    QTest::newRow("left") << int(Edge::Left);
    QTest::newRow("right") << int(Edge::Right);
    QTest::newRow("top") << int(Edge::Top);
    QTest::newRow("bottom") << int(Edge::Bottom);
}

void LayoutModelTest::peekSizesSurvivePinningAndCrossWindowMoves()
{
    QFETCH(int, edge);
    LayoutModel model;
    const auto source = model.createWindow();
    const auto tab = model.addTab(source, "Moved");
    const auto pane = model.tab(tab)->activePane;
    const auto paneTree = model.tab(tab)->panes;
    QVERIFY(model.setPeekSize(tab, QSize(483, 297)).ok);
    QVERIFY(model.moveTabToEdge(tab, Edge(edge), true).ok);
    const auto pinnedGroup = model.groupForTab(tab);
    QVERIFY(model.setEdgeGroupExtent(pinnedGroup, 387).ok);
    QVERIFY(model.setTabPinned(tab, false).ok);
    QCOMPARE(*model.tab(tab)->peekSize, QSize(483, 297));
    QVERIFY(model.setTabPinned(tab, true).ok);
    QCOMPARE(model.groupForTab(tab), pinnedGroup);
    QCOMPARE(model.group(pinnedGroup)->edgeExtent, 387);
    QCOMPARE(*model.tab(tab)->peekSize, QSize(483, 297));
    const auto floating = model.detachTab(tab);
    QVERIFY(!floating.isNull());
    QCOMPARE(*model.tab(tab)->peekSize, QSize(483, 297));
    const auto destination = model.createWindow();
    QVERIFY(model.moveTabToEdgeBar(tab, destination, Edge(edge), 0).ok);
    QVERIFY(!model.window(floating));
    QCOMPARE(*model.tab(tab)->peekSize, QSize(483, 297));
    QCOMPARE(model.tab(tab)->activePane, pane);
    QCOMPARE(model.tab(tab)->panes, paneTree);
    LayoutModel restored;
    const auto snapshot = model.toJson();
    QVERIFY(restored.fromJson(snapshot).ok);
    QCOMPARE(restored.toJson(), snapshot);
    QCOMPARE(*restored.tab(tab)->peekSize, QSize(483, 297));
    QCOMPARE(restored.tab(tab)->activePane, pane);
    QVERIFY(restored.validate().ok);
}

void LayoutModelTest::peekSizeBoundariesRoundTrip_data()
{
    QTest::addColumn<QSize>("size");
    QTest::newRow("minimum") << QSize(140, 100);
    QTest::newRow("ordinary") << QSize(613, 271);
    QTest::newRow("maximum") << QSize(16777215, 16777215);
}

void LayoutModelTest::peekSizeBoundariesRoundTrip()
{
    QFETCH(QSize, size);
    LayoutModel original;
    const auto window = original.createWindow();
    const auto tab = original.addTab(window, "Sized");
    const auto revision = original.revision();
    QVERIFY(original.setPeekSize(tab, size).ok);
    QCOMPARE(original.revision(), revision + 1);
    QCOMPARE(*original.tab(tab)->peekSize, size);
    const auto snapshot = original.toJson();
    const auto storedSize = snapshot.value("tabs").toArray()[0].toObject().value("peekSize").toObject();
    QCOMPARE(storedSize, (QJsonObject{{"width", size.width()}, {"height", size.height()}}));
    LayoutModel restored;
    QVERIFY(restored.fromJson(QJsonDocument::fromJson(QJsonDocument(snapshot).toJson()).object()).ok);
    QCOMPARE(*restored.tab(tab)->peekSize, size);
    QCOMPARE(restored.toJson(), snapshot);
    QVERIFY(restored.validate().ok);
}

void LayoutModelTest::invalidPeekSizeOperationsAreAtomic_data()
{
    QTest::addColumn<QSize>("size");
    QTest::newRow("width-below-minimum") << QSize(139, 100);
    QTest::newRow("height-below-minimum") << QSize(140, 99);
    QTest::newRow("negative-width") << QSize(-1, 170);
    QTest::newRow("negative-height") << QSize(250, -1);
    QTest::newRow("zero") << QSize(0, 0);
    QTest::newRow("width-above-maximum") << QSize(16777216, 170);
    QTest::newRow("height-above-maximum") << QSize(250, 16777216);
    QTest::newRow("integer-maximum") << QSize(std::numeric_limits<int>::max(), std::numeric_limits<int>::max());
}

void LayoutModelTest::invalidPeekSizeOperationsAreAtomic()
{
    QFETCH(QSize, size);
    LayoutModel model;
    const auto window = model.createWindow();
    const auto tab = model.addTab(window, "Retained");
    QVERIFY(model.setPeekSize(tab, QSize(430, 280)).ok);
    const auto snapshot = model.toJson();
    const auto revision = model.revision();
    const auto result = model.setPeekSize(tab, size);
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());
    QCOMPARE(model.toJson(), snapshot);
    QCOMPARE(model.revision(), revision);
    QCOMPARE(*model.tab(tab)->peekSize, QSize(430, 280));
}

void LayoutModelTest::peekSizeOperationsRequireAnExistingTab()
{
    LayoutModel model;
    const auto window = model.createWindow();
    const auto tab = model.addTab(window, "Retained");
    QVERIFY(model.setPeekSize(tab, QSize(430, 280)).ok);
    const auto snapshot = model.toJson();
    const auto revision = model.revision();
    QVERIFY(!model.setPeekSize(TabId{}, QSize(250, 170)).ok);
    QVERIFY(!model.setPeekSize(QUuid::createUuid(), QSize(250, 170)).ok);
    QVERIFY(!model.setPeekSize(QUuid::createUuid(), std::nullopt).ok);
    QCOMPARE(model.toJson(), snapshot);
    QCOMPARE(model.revision(), revision);
}

void LayoutModelTest::oldSnapshotsLeavePeekSizesAbsent()
{
    LayoutModel original;
    const auto window = original.createWindow();
    const auto central = original.addTab(window, "Central");
    const auto hidden = original.addTab(window, "Hidden");
    const auto pinned = original.addTab(window, "Pinned");
    QVERIFY(original.moveTabToEdge(hidden, Edge::Left).ok);
    QVERIFY(original.moveTabToEdge(pinned, Edge::Bottom, true).ok);
    const auto snapshot = original.toJson();
    for (const auto& tab : snapshot.value("tabs").toArray()) QVERIFY(!tab.toObject().contains("peekSize"));
    LayoutModel restored;
    QVERIFY(restored.fromJson(snapshot).ok);
    for (const auto tab : {central, hidden, pinned}) QVERIFY(!restored.tab(tab)->peekSize);
    QCOMPARE(restored.toJson(), snapshot);
    QVERIFY(restored.validate().ok);
}

void LayoutModelTest::malformedPeekSizesAreRejectedAtomically_data()
{
    QTest::addColumn<QJsonValue>("payload");
    QTest::newRow("null") << QJsonValue(QJsonValue::Null);
    QTest::newRow("array") << QJsonValue(QJsonArray{250, 170});
    QTest::newRow("string") << QJsonValue("250x170");
    QTest::newRow("empty-object") << QJsonValue(QJsonObject{});
    QTest::newRow("missing-width") << QJsonValue(QJsonObject{{"height", 170}});
    QTest::newRow("missing-height") << QJsonValue(QJsonObject{{"width", 250}});
    QTest::newRow("string-width") << QJsonValue(QJsonObject{{"width", "250"}, {"height", 170}});
    QTest::newRow("string-height") << QJsonValue(QJsonObject{{"width", 250}, {"height", "170"}});
    QTest::newRow("boolean-width") << QJsonValue(QJsonObject{{"width", true}, {"height", 170}});
    QTest::newRow("boolean-height") << QJsonValue(QJsonObject{{"width", 250}, {"height", true}});
    QTest::newRow("fractional-width") << QJsonValue(QJsonObject{{"width", 250.25}, {"height", 170}});
    QTest::newRow("fractional-height") << QJsonValue(QJsonObject{{"width", 250}, {"height", 170.75}});
    QTest::newRow("width-below-minimum") << QJsonValue(QJsonObject{{"width", 139}, {"height", 170}});
    QTest::newRow("height-below-minimum") << QJsonValue(QJsonObject{{"width", 250}, {"height", 99}});
    QTest::newRow("width-above-maximum") << QJsonValue(QJsonObject{{"width", 16777216}, {"height", 170}});
    QTest::newRow("height-above-maximum") << QJsonValue(QJsonObject{{"width", 250}, {"height", 16777216}});
    QTest::newRow("beyond-integer-range") << QJsonValue(QJsonObject{{"width", 4294967296.0}, {"height", 170}});
    QTest::newRow("non-finite-width") << QJsonValue(QJsonObject{{"width", std::numeric_limits<double>::infinity()}, {"height", 170}});
    QTest::newRow("non-finite-height") << QJsonValue(QJsonObject{{"width", 250}, {"height", std::numeric_limits<double>::quiet_NaN()}});
}

void LayoutModelTest::malformedPeekSizesAreRejectedAtomically()
{
    QFETCH(QJsonValue, payload);
    LayoutModel model;
    const auto window = model.createWindow();
    const auto tab = model.addTab(window, "Retained");
    QVERIFY(model.setPeekSize(tab, QSize(430, 280)).ok);
    const auto before = model.toJson();
    const auto revision = model.revision();
    auto malformed = before;
    auto tabs = malformed.value("tabs").toArray();
    auto record = tabs[0].toObject();
    record.insert("peekSize", payload);
    tabs[0] = record;
    malformed.insert("tabs", tabs);
    const auto result = model.fromJson(malformed);
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());
    QCOMPARE(model.toJson(), before);
    QCOMPARE(model.revision(), revision);
    QCOMPARE(*model.tab(tab)->peekSize, QSize(430, 280));
}

void LayoutModelTest::extractedPanesReceiveTheirOwnDefaultPeekSize()
{
    LayoutModel model;
    const auto window = model.createWindow();
    const auto source = model.addTab(window, "Original");
    const auto firstPane = model.tab(source)->activePane;
    const auto extractedPane = model.splitPane(source, firstPane, Placement::Right);
    QVERIFY(!extractedPane.isNull());
    QVERIFY(model.setPeekSize(source, QSize(480, 310)).ok);
    const auto extracted = model.extractPane(source, extractedPane, model.groupForTab(source));
    QVERIFY(!extracted.isNull());
    QCOMPARE(*model.tab(source)->peekSize, QSize(480, 310));
    QVERIFY(!model.tab(extracted)->peekSize);
    QCOMPARE(model.panes(source), (QVector<PaneId>{firstPane}));
    QCOMPARE(model.panes(extracted), (QVector<PaneId>{extractedPane}));
    QVERIFY(model.validate().ok);
}

void LayoutModelTest::directionalPeekPoliciesShareOppositeSidesAndKeepOtherTabsIndependent()
{
    LayoutModel model;
    const auto window = model.createWindow();
    const auto first = model.addTab(window, "First");
    const auto second = model.addTab(window, "Second");
    const auto pane = model.tab(first)->activePane;
    const auto tree = model.tab(first)->panes;
    const PeekSizePolicy vertical{410, 275}, horizontal{720, 155};
    QVERIFY(model.setPeekSizePolicy(first, Edge::Left, vertical));
    QVERIFY(model.setPeekSizePolicy(first, Edge::Bottom, horizontal));
    QCOMPARE(model.peekSizePolicy(first, Edge::Right), vertical);
    QCOMPARE(model.peekSizePolicy(first, Edge::Top), horizontal);
    QCOMPARE(model.peekSizePolicy(second, Edge::Left), PeekSizePolicy{});
    QCOMPARE(model.peekSizePolicy(second, Edge::Top), PeekSizePolicy{});
    QVERIFY(model.moveTabToEdge(first, Edge::Right, true));
    const auto pinned = model.groupForTab(first);
    QVERIFY(model.setEdgeGroupExtent(pinned, 365));
    QVERIFY(model.setTabPinned(first, false));
    QCOMPARE(model.peekSizePolicy(first, Edge::Left), vertical);
    const auto floating = model.detachTab(first);
    QVERIFY(!floating.isNull());
    QVERIFY(model.moveTabToEdgeBar(first, window, Edge::Top));
    QCOMPARE(model.peekSizePolicy(first, Edge::Bottom), horizontal);
    QCOMPARE(model.tab(first)->activePane, pane);
    QCOMPARE(model.tab(first)->panes, tree);
    const auto snapshot = model.toJson();
    LayoutModel restored; QVERIFY(restored.fromJson(snapshot));
    QCOMPARE(restored.toJson(), snapshot);
    QCOMPARE(restored.peekSizePolicy(first, Edge::Right), vertical);
    QCOMPARE(restored.peekSizePolicy(first, Edge::Top), horizontal);
    QVERIFY(restored.setPeekSizePolicy(first, Edge::Top, {}));
    QCOMPARE(restored.peekSizePolicy(first, Edge::Bottom), PeekSizePolicy{});
    QCOMPARE(restored.peekSizePolicy(first, Edge::Left), vertical);
    QCOMPARE(restored.tab(first)->activePane, pane);
    QVERIFY(restored.validate());
}

void LayoutModelTest::directionalPeekPoliciesKeepLegacyStretchAndResetCompatibility()
{
    LayoutModel model; const auto window = model.createWindow();
    const auto tab = model.addTab(window, "Legacy");
    QVERIFY(model.setPeekSize(tab, QSize(340, 205)));
    QCOMPARE(model.peekSizePolicy(tab, Edge::Left), (PeekSizePolicy{340, std::nullopt}));
    QCOMPARE(model.peekSizePolicy(tab, Edge::Bottom), (PeekSizePolicy{std::nullopt, 205}));
    LayoutModel restored; QVERIFY(restored.fromJson(model.toJson()));
    QCOMPARE(restored.peekSizePolicy(tab, Edge::Right), (PeekSizePolicy{340, std::nullopt}));
    QVERIFY(restored.setPeekSizePolicy(tab, Edge::Left, {430, 285}));
    QVERIFY(restored.setPeekSizePolicy(tab, Edge::Top, {730, 190}));
    QVERIFY(restored.setPeekSize(tab, QSize(350, 215)));
    QVERIFY(!restored.tab(tab)->verticalPeekSize); QVERIFY(!restored.tab(tab)->horizontalPeekSize);
    QCOMPARE(restored.peekSizePolicy(tab, Edge::Right), (PeekSizePolicy{350, std::nullopt}));
    QCOMPARE(restored.peekSizePolicy(tab, Edge::Top), (PeekSizePolicy{std::nullopt, 215}));
    QVERIFY(restored.setPeekSizePolicy(tab, Edge::Left, {}));
    QCOMPARE(restored.peekSizePolicy(tab, Edge::Right), PeekSizePolicy{});
    QCOMPARE(restored.peekSizePolicy(tab, Edge::Top), (PeekSizePolicy{std::nullopt, 215}));
    QVERIFY(restored.setPeekSize(tab, std::nullopt));
    const auto record = restored.toJson().value("tabs").toArray().first().toObject();
    QVERIFY(!record.contains("peekSize")); QVERIFY(!record.contains("verticalPeekSize"));
    QVERIFY(!record.contains("horizontalPeekSize"));
}

void LayoutModelTest::invalidDirectionalPeekPoliciesAreAtomic_data()
{
    QTest::addColumn<int>("edge"); QTest::addColumn<int>("width"); QTest::addColumn<int>("height");
    QTest::newRow("small-width") << int(Edge::Left) << 139 << 170;
    QTest::newRow("small-height") << int(Edge::Right) << 250 << 99;
    QTest::newRow("negative") << int(Edge::Top) << -1 << 170;
    QTest::newRow("zero-is-not-an-explicit-size") << int(Edge::Top) << 0 << 170;
    QTest::newRow("too-large") << int(Edge::Bottom) << 250 << 16777216;
    QTest::newRow("invalid-edge") << -1 << 250 << 170;
}

void LayoutModelTest::invalidDirectionalPeekPoliciesAreAtomic()
{
    QFETCH(int, edge); QFETCH(int, width); QFETCH(int, height);
    LayoutModel model; const auto window = model.createWindow();
    const auto tab = model.addTab(window, "Retained");
    QVERIFY(model.setPeekSizePolicy(tab, Edge::Left, {430, std::nullopt}));
    const auto snapshot = model.toJson(); const auto revision = model.revision();
    QVERIFY(!model.setPeekSizePolicy(tab, Edge(edge), {width, height}));
    QVERIFY(!model.setPeekSizePolicy(QUuid::createUuid(), Edge::Left, {}));
    QCOMPARE(model.toJson(), snapshot); QCOMPARE(model.revision(), revision);
}

void LayoutModelTest::malformedDirectionalPeekPoliciesAreAtomic_data()
{
    QTest::addColumn<QString>("key"); QTest::addColumn<QJsonValue>("payload");
    for (const auto* key : {"verticalPeekSize", "horizontalPeekSize"}) {
        const auto row = [key](const char* name) { return QByteArray(key) + '-' + name; };
        QTest::newRow(row("null").constData()) << QString::fromLatin1(key) << QJsonValue(QJsonValue::Null);
        QTest::newRow(row("array").constData()) << QString::fromLatin1(key) << QJsonValue(QJsonArray{250, 170});
        QTest::newRow(row("text").constData()) << QString::fromLatin1(key) << QJsonValue(QJsonObject{{"width", "250"}});
        QTest::newRow(row("boolean").constData()) << QString::fromLatin1(key) << QJsonValue(QJsonObject{{"height", true}});
        QTest::newRow(row("fraction").constData()) << QString::fromLatin1(key) << QJsonValue(QJsonObject{{"height", 170.25}});
        QTest::newRow(row("small").constData()) << QString::fromLatin1(key) << QJsonValue(QJsonObject{{"width", 139}});
        QTest::newRow(row("large").constData()) << QString::fromLatin1(key) << QJsonValue(QJsonObject{{"height", 16777216}});
        QTest::newRow(row("unknown-axis").constData()) << QString::fromLatin1(key) << QJsonValue(QJsonObject{{"depth", 20}});
    }
}

void LayoutModelTest::malformedDirectionalPeekPoliciesAreAtomic()
{
    QFETCH(QString, key); QFETCH(QJsonValue, payload);
    LayoutModel model; const auto window = model.createWindow();
    const auto tab = model.addTab(window, "Retained");
    QVERIFY(model.setPeekSizePolicy(tab, Edge::Left, {430, 280}));
    const auto snapshot = model.toJson(); const auto revision = model.revision();
    auto malformed = snapshot; auto tabs = malformed.value("tabs").toArray();
    auto record = tabs.first().toObject(); record.insert(key, payload); tabs[0] = record;
    malformed.insert("tabs", tabs);
    QVERIFY(!model.fromJson(malformed));
    QCOMPARE(model.toJson(), snapshot); QCOMPARE(model.revision(), revision);
    QCOMPARE(model.peekSizePolicy(tab, Edge::Right), (PeekSizePolicy{430, 280}));
}

void LayoutModelTest::directionalPeekPolicyBoundariesRoundTrip_data()
{
    QTest::addColumn<int>("width"); QTest::addColumn<int>("height");
    QTest::newRow("automatic") << 0 << 0;
    QTest::newRow("minimum-width-only") << 140 << 0;
    QTest::newRow("minimum-height-only") << 0 << 100;
    QTest::newRow("maximum-width-only") << 16777215 << 0;
    QTest::newRow("maximum-height-only") << 0 << 16777215;
    QTest::newRow("minimum-both") << 140 << 100;
    QTest::newRow("maximum-both") << 16777215 << 16777215;
}

void LayoutModelTest::directionalPeekPolicyBoundariesRoundTrip()
{
    QFETCH(int, width); QFETCH(int, height);
    LayoutModel model; const auto window = model.createWindow(); const auto tab = model.addTab(window, "Boundary");
    const PeekSizePolicy policy{width == 0 ? std::optional<int>{} : std::optional<int>{width},
        height == 0 ? std::optional<int>{} : std::optional<int>{height}};
    QVERIFY(model.setPeekSizePolicy(tab, Edge::Left, policy));
    QVERIFY(model.setPeekSizePolicy(tab, Edge::Top, policy));
    const auto snapshot = model.toJson(); const auto record = snapshot.value("tabs").toArray().first().toObject();
    const auto expected = [&] {
        QJsonObject object; if (width) object.insert("width", width); if (height) object.insert("height", height); return object;
    }();
    QCOMPARE(record.value("verticalPeekSize").toObject(), expected);
    QCOMPARE(record.value("horizontalPeekSize").toObject(), expected);
    LayoutModel restored; QVERIFY(restored.fromJson(snapshot));
    QCOMPARE(restored.toJson(), snapshot); QCOMPARE(restored.peekSizePolicy(tab, Edge::Right), policy);
    QCOMPARE(restored.peekSizePolicy(tab, Edge::Bottom), policy); QVERIFY(restored.validate());
}

namespace {
void collectSplits(const LayoutTree& tree, QSet<QUuid>& nodes)
{
    if (!tree || tree->isLeaf()) return;
    nodes.insert(tree->id);
    collectSplits(tree->first, nodes);
    collectSplits(tree->second, nodes);
}
} // namespace

void LayoutModelTest::indexFollowsEveryMutation()
{
    LayoutModel model;
    QSet<TabId> seenTabs;
    QSet<PaneId> seenPanes;
    QSet<QUuid> seenNodes;
    const auto check = [&](LayoutModel& checked, const char* step) {
        for (const auto& tab : checked.tabs()) {
            seenTabs.insert(tab.id);
            for (const auto pane : checked.panes(tab.id)) seenPanes.insert(pane);
            collectSplits(tab.panes, seenNodes);
        }
        for (const auto& window : checked.windows()) {
            collectSplits(window.central, seenNodes);
            for (const auto& tree : window.pinnedLayouts) collectSplits(tree, seenNodes);
            // Restore skeletons have their own node identities and no owner.
            for (const auto& tree : window.pinnedRestoreLayouts) collectSplits(tree, seenNodes);
        }
        const auto mismatch = indexMismatch(checked, seenTabs, seenPanes, seenNodes);
        QVERIFY2(mismatch.isEmpty(), qPrintable(QStringLiteral("%1: %2").arg(QLatin1String(step), mismatch)));
    };
#define CHECK_INDEX(step) do { check(model, step); if (QTest::currentTestFailed()) return; } while (false)
    const auto root = model.createWindow();
    CHECK_INDEX("createWindow");
    const auto a = model.addTab(root, "A");
    const auto b = model.addTab(root, "B");
    const auto c = model.addTab(root, "C");
    const auto d = model.addTab(root, "D");
    const auto e = model.addTab(root, "E");
    CHECK_INDEX("addTab");
    QVERIFY(model.moveTab(b, model.groupForTab(a), Placement::Right));
    CHECK_INDEX("moveTab");
    const auto split = model.splitPane(a, model.tab(a)->activePane, Placement::Bottom);
    QVERIFY(!split.isNull());
    CHECK_INDEX("splitPane");
    QVERIFY(model.moveTabToEdge(c, Edge::Left, false));
    CHECK_INDEX("moveTabToEdge auto-hide");
    QVERIFY(model.moveTabToEdge(d, Edge::Right, true));
    QVERIFY(model.moveTabToEdge(e, Edge::Right, true));
    CHECK_INDEX("moveTabToEdge pinned");
    QVERIFY(model.setTabPinned(d, false));
    QVERIFY(model.setTabPinned(d, true));
    CHECK_INDEX("setTabPinned");
    QVERIFY(model.setSplitRatio(model.window(root)->central->id, 0.3));
    CHECK_INDEX("setSplitRatio");
    const auto other = model.createWindow();
    const auto f = model.addTab(other, "F");
    CHECK_INDEX("second window");
    QVERIFY(model.movePane(a, split, f, {}, Placement::Right));
    CHECK_INDEX("movePane");
    const auto extracted = model.extractPane(f, split, model.groupForTab(f), Placement::Bottom);
    QVERIFY(!extracted.isNull());
    CHECK_INDEX("extractPane");
    QVERIFY(model.mergeGroups(model.groupForTab(extracted), model.groupForTab(f)));
    CHECK_INDEX("mergeGroups");
    const auto retained = model.convertGroupToTab(model.groupForTab(f), Placement::Right);
    QVERIFY(!retained.isNull());
    CHECK_INDEX("convertGroupToTab");
    QVERIFY(!model.convertTabToGroup(retained, Placement::Bottom).isNull());
    CHECK_INDEX("convertTabToGroup");
    QVERIFY(model.activatePane(a, model.panes(a).first()));
    QVERIFY(model.activateTab(b));
    CHECK_INDEX("activate");
    const auto floating = model.detachTab(b);
    QVERIFY(!floating.isNull());
    CHECK_INDEX("detachTab");
    QVERIFY(model.closeWindow(other));
    CHECK_INDEX("closeWindow");
    QVERIFY(model.removeTab(a));
    CHECK_INDEX("removeTab");
    const auto snapshot = model.toJson();
    QVERIFY(model.fromJson(snapshot));
    CHECK_INDEX("fromJson(toJson())");
    LayoutModel restored;
    QVERIFY(restored.fromJson(snapshot));
    check(restored, "fromJson into a new model");
#undef CHECK_INDEX
}

void LayoutModelTest::failedTransactionKeepsIndex()
{
    LayoutModel model;
    const auto root = model.createWindow();
    const auto a = model.addTab(root, "A");
    const auto b = model.addTab(root, "B");
    QVERIFY(model.moveTab(b, model.groupForTab(a), Placement::Right));
    const auto pane = model.tab(a)->activePane;
    const auto node = model.window(root)->central->id;
    const auto groupA = model.groupForTab(a);
    const auto groupB = model.groupForTab(b);
    const auto owner = LayoutModelTestAccess::owner(model, node);
    QVERIFY(!owner.isEmpty());
    QVERIFY(!model.moveTab(a, QUuid::createUuid()));
    QVERIFY(!model.movePane(a, pane, QUuid::createUuid()));
    QVERIFY(!model.setSplitRatio(node, 2.0));
    QVERIFY(!model.removeTab(QUuid::createUuid()));
    QVERIFY(LayoutModelTestAccess::indexValid(model));
    QCOMPARE(model.groupForTab(a), groupA);
    QCOMPARE(model.groupForTab(b), groupB);
    QCOMPARE(model.tabForPane(pane), a);
    QCOMPARE(LayoutModelTestAccess::owner(model, node), owner);
    QVERIFY(model.validate());
}

void LayoutModelTest::validateReportsAStaleIndex()
{
    LayoutModel model;
    const auto root = model.createWindow();
    const auto a = model.addTab(root, "A");
    QVERIFY(model.validate());
    LayoutModelTestAccess::corruptTabGroup(model, a, QUuid::createUuid());
    QCOMPARE(model.validate().error, QStringLiteral("The lookup index is stale."));
}

QTEST_GUILESS_MAIN(LayoutModelTest)
#include "layout_model_test.moc"
