// SPDX-License-Identifier: MIT
#include <window_system/layout_model.hpp>

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QTest>
#include <algorithm>

using namespace window_system;

namespace {

const QVector<Edge> edges{Edge::Left, Edge::Right, Edge::Top, Edge::Bottom};
const QVector<Placement> directions{Placement::Left, Placement::Right, Placement::Top, Placement::Bottom};

QVector<GroupId> leaves(LayoutTree tree)
{
    if (!tree) return {};
    if (tree->isLeaf()) return {tree->itemId};
    return leaves(tree->first) + leaves(tree->second);
}

LayoutTree nodeFor(LayoutTree tree, QUuid id)
{
    if (!tree || tree->id == id) return tree;
    if (auto found = nodeFor(tree->first, id)) return found;
    return nodeFor(tree->second, id);
}

LayoutTree branchFor(LayoutTree tree, GroupId first, GroupId second)
{
    if (!tree || tree->isLeaf()) return {};
    if (auto found = branchFor(tree->first, first, second)) return found;
    if (auto found = branchFor(tree->second, first, second)) return found;
    const auto items = leaves(tree);
    return items.contains(first) && items.contains(second) ? tree : LayoutTree{};
}

void comparePinnedMembership(const LayoutModel& model)
{
    QSet<GroupId> seen;
    for (const auto window : model.windowOrder()) {
        for (const auto edge : edges) {
            const auto items = leaves(model.pinnedLayout(window, edge));
            for (const auto group : items) {
                QVERIFY(!seen.contains(group)); seen.insert(group);
                const auto* record = model.group(group); QVERIFY(record);
                QCOMPARE(record->windowId, window); QCOMPARE(record->location, GroupLocation::Pinned);
                QCOMPARE(record->edge, edge); QVERIFY(model.window(window)->edgeGroups.contains(group));
            }
        }
    }
    for (auto iterator = model.groups().cbegin(); iterator != model.groups().cend(); ++iterator)
        QCOMPARE(seen.contains(iterator.key()), iterator->location == GroupLocation::Pinned);
    QVERIFY(model.validate());
}

void addEdgeDirectionRows()
{
    QTest::addColumn<int>("edge"); QTest::addColumn<int>("direction");
    for (const auto edge : edges) for (const auto direction : directions)
        QTest::newRow(qPrintable(QString("edge-%1-direction-%2").arg(int(edge)).arg(int(direction))))
            << int(edge) << int(direction);
}

QString idText(QUuid id) { return id.toString(QUuid::WithoutBraces); }
QJsonObject leafJson(GroupId id)
{
    return {{"id", idText(QUuid::createUuid())}, {"itemId", idText(id)}};
}

} // namespace

class PinnedLayoutTest final : public QObject {
    Q_OBJECT
private slots:
    void directionalOperationsRetainThePinnedRegion_data();
    void directionalOperationsRetainThePinnedRegion();
    void nestedRatiosAreIndependentOfOtherRegions_data() { addEdgeDirectionRows(); }
    void nestedRatiosAreIndependentOfOtherRegions();
    void everyMutationKeepsPinnedTreesFreeOfOrphans_data();
    void everyMutationKeepsPinnedTreesFreeOfOrphans();
    void explicitTreesRoundTripAndLegacyOrdersMigrate_data();
    void explicitTreesRoundTripAndLegacyOrdersMigrate();
    void malformedExplicitTreesAreRejectedAtomically_data();
    void malformedExplicitTreesAreRejectedAtomically();
};

void PinnedLayoutTest::directionalOperationsRetainThePinnedRegion_data()
{
    QTest::addColumn<int>("edge"); QTest::addColumn<int>("direction"); QTest::addColumn<QString>("operation");
    for (const auto edge : edges) for (const auto direction : directions)
        for (const auto* operation : {"tab", "new-group", "group", "whole-window"})
            QTest::newRow(qPrintable(QString("edge-%1-direction-%2-%3").arg(int(edge)).arg(int(direction)).arg(operation)))
                << int(edge) << int(direction) << QString(operation);
}

void PinnedLayoutTest::directionalOperationsRetainThePinnedRegion()
{
    QFETCH(int, edge); QFETCH(int, direction); QFETCH(QString, operation);
    const auto side = Edge(edge); const auto placement = Placement(direction);
    LayoutModel model; const auto root = model.createWindow();
    const auto central = model.addTab(root, "Central");
    const auto target = model.addTab(root, "Pinned target");
    const auto targetPane = model.tab(target)->activePane;
    QVERIFY(model.moveTabToEdge(target, side, true));
    const auto targetGroup = model.groupForTab(target);
    QCOMPARE(leaves(model.pinnedLayout(root, side)), QVector<GroupId>{targetGroup});
    QVector<TabId> carried;
    QHash<TabId, LayoutTree> paneTrees;
    QVector<GroupId> originalGroups;
    WindowId sourceWindow;
    if (operation != "new-group") {
        sourceWindow = operation == "whole-window" ? model.createWindow(WindowKind::Floating, root) : root;
        const auto tab = model.addTab(sourceWindow, "Carried"); carried.append(tab);
        const auto extraPane = model.splitPane(tab, model.tab(tab)->activePane, Placement::Bottom);
        QVERIFY(!extraPane.isNull());
        if (operation == "group") {
            QVERIFY(model.moveTab(tab, model.groupForTab(central), Placement::Right));
            const auto peer = model.addTab(root, "Carried peer", model.groupForTab(tab)); carried.append(peer);
        } else if (operation == "whole-window") {
            const auto peer = model.addTab(sourceWindow, "Carried adjacent"); carried.append(peer);
            QVERIFY(model.moveTab(peer, model.groupForTab(tab), Placement::Bottom));
        }
        for (const auto id : carried) paneTrees.insert(id, model.tab(id)->panes);
        for (const auto id : carried) {
            const auto group = model.groupForTab(id);
            if (!originalGroups.contains(group)) originalGroups.append(group);
        }
    }
    if (operation == "tab") QVERIFY(model.moveTab(carried.first(), targetGroup, placement));
    else if (operation == "group") QVERIFY(model.moveGroup(originalGroups.first(), targetGroup, placement));
    else if (operation == "whole-window") QVERIFY(model.moveAllTabs(sourceWindow, targetGroup, placement));
    else {
        const auto tab = model.addTabToNewGroup(root, "New pinned", targetGroup, placement);
        QVERIFY(!tab.isNull()); carried.append(tab); paneTrees.insert(tab, model.tab(tab)->panes);
    }
    const auto tree = model.pinnedLayout(root, side); QVERIFY(tree); QVERIFY(!tree->isLeaf());
    const bool horizontal = placement == Placement::Left || placement == Placement::Right;
    const bool before = placement == Placement::Left || placement == Placement::Top;
    QCOMPARE(tree->orientation, horizontal ? Qt::Horizontal : Qt::Vertical);
    const auto targetLeaf = before ? tree->second : tree->first;
    QVERIFY(targetLeaf->isLeaf()); QCOMPARE(targetLeaf->itemId, targetGroup);
    QVector<GroupId> movedGroups;
    for (const auto tab : carried) {
        const auto group = model.groupForTab(tab);
        if (!movedGroups.contains(group)) movedGroups.append(group);
        QCOMPARE(model.group(group)->location, GroupLocation::Pinned);
        QCOMPARE(model.group(group)->edge, side); QCOMPARE(model.group(group)->windowId, root);
        QCOMPARE(model.tab(tab)->panes, paneTrees.value(tab));
    }
    QCOMPARE(leaves(before ? tree->first : tree->second), movedGroups);
    if (operation == "group" || operation == "whole-window") QCOMPARE(movedGroups, originalGroups);
    if (operation == "whole-window") QVERIFY(!model.window(sourceWindow));
    QCOMPARE(model.tab(target)->activePane, targetPane);
    QCOMPARE(model.centralGroups(root), QVector<GroupId>{model.groupForTab(central)});
    comparePinnedMembership(model);
    LayoutModel restored; QVERIFY(restored.fromJson(model.toJson()));
    QCOMPARE(restored.toJson(), model.toJson()); comparePinnedMembership(restored);
}

void PinnedLayoutTest::nestedRatiosAreIndependentOfOtherRegions()
{
    QFETCH(int, edge); QFETCH(int, direction);
    const auto side = Edge(edge); const auto placement = Placement(direction);
    LayoutModel model; const auto root = model.createWindow();
    const auto central = model.addTab(root, "Central");
    const auto secondCentral = model.addTabToNewGroup(root, "Central split", model.groupForTab(central), Placement::Right);
    QVERIFY(!secondCentral.isNull());
    const auto target = model.addTab(root, "Target"); QVERIFY(model.moveTabToEdge(target, side, true));
    const auto targetGroup = model.groupForTab(target);
    const auto first = model.addTabToNewGroup(root, "First", targetGroup, placement); QVERIFY(!first.isNull());
    const auto outer = model.pinnedLayout(root, side);
    QVERIFY(model.setSplitRatio(outer->id, 0.64));
    const auto nested = model.addTabToNewGroup(root, "Nested", targetGroup, placement); QVERIFY(!nested.isNull());
    const auto nestedGroup = model.groupForTab(nested);
    const auto otherEdge = side == Edge::Bottom ? Edge::Top : Edge::Bottom;
    const auto other = model.addTab(root, "Other side"); QVERIFY(model.moveTabToEdge(other, otherEdge, true));
    const auto secondOther = model.addTabToNewGroup(root, "Other split", model.groupForTab(other), Placement::Right);
    QVERIFY(!secondOther.isNull());
    const auto before = model;
    const auto centralTree = model.window(root)->central;
    const auto otherTree = model.pinnedLayout(root, otherEdge);
    const auto pinnedTree = model.pinnedLayout(root, side);
    const auto branch = branchFor(pinnedTree, targetGroup, nestedGroup); QVERIFY(branch); QVERIFY(branch != pinnedTree);
    QVERIFY(model.setSplitRatio(branch->id, 0.31));
    QCOMPARE(model.window(root)->central, centralTree); QCOMPARE(model.pinnedLayout(root, otherEdge), otherTree);
    QCOMPARE(model.pinnedLayout(root, side)->id, pinnedTree->id);
    QCOMPARE(model.pinnedLayout(root, side)->ratio, 0.64);
    QCOMPARE(nodeFor(model.pinnedLayout(root, side), branch->id)->ratio, 0.31);
    QCOMPARE(nodeFor(before.pinnedLayout(root, side), branch->id)->ratio, 0.5);
    QCOMPARE(leaves(model.pinnedLayout(root, side)), leaves(pinnedTree));
    comparePinnedMembership(model);
}

void PinnedLayoutTest::everyMutationKeepsPinnedTreesFreeOfOrphans_data()
{
    QTest::addColumn<int>("edge");
    for (const auto edge : edges) QTest::newRow(qPrintable(QString::number(int(edge)))) << int(edge);
}

void PinnedLayoutTest::everyMutationKeepsPinnedTreesFreeOfOrphans()
{
    QFETCH(int, edge); const auto side = Edge(edge);
    LayoutModel model; const auto firstWindow = model.createWindow(); const auto secondWindow = model.createWindow();
    const auto central = model.addTab(firstWindow, "Central");
    const auto otherCentral = model.addTab(secondWindow, "Other central");
    const auto a = model.addTab(firstWindow, "A"); QVERIFY(model.moveTabToEdge(a, side, true));
    const auto b = model.addTabToNewGroup(firstWindow, "B", model.groupForTab(a), Placement::Bottom); QVERIFY(!b.isNull());
    const auto c = model.addTabToNewGroup(firstWindow, "C", model.groupForTab(a), Placement::Right); QVERIFY(!c.isNull());
    const auto originalPane = model.tab(a)->activePane;
    comparePinnedMembership(model);
    QVERIFY(model.setTabPinned(b, false));
    QCOMPARE(model.group(model.groupForTab(b))->location, GroupLocation::AutoHide); comparePinnedMembership(model);
    QVERIFY(model.setTabPinned(b, true));
    QCOMPARE(model.group(model.groupForTab(b))->location, GroupLocation::Pinned); comparePinnedMembership(model);
    QVERIFY(model.moveTab(c, model.groupForTab(central)));
    QCOMPARE(leaves(model.pinnedLayout(firstWindow, side)).size(), 2); comparePinnedMembership(model);
    QVERIFY(model.removeTab(b));
    QCOMPARE(leaves(model.pinnedLayout(firstWindow, side)), QVector<GroupId>{model.groupForTab(a)});
    comparePinnedMembership(model);
    const auto secondPinned = model.addTab(secondWindow, "Second pinned");
    const auto opposite = side == Edge::Left ? Edge::Right : side == Edge::Right ? Edge::Left
        : side == Edge::Top ? Edge::Bottom : Edge::Top;
    QVERIFY(model.moveTabToEdge(secondPinned, opposite, true));
    const auto aGroup = model.groupForTab(a), secondGroup = model.groupForTab(secondPinned);
    QVERIFY(model.swapGroups(aGroup, secondGroup));
    QCOMPARE(model.group(aGroup)->windowId, secondWindow); QCOMPARE(model.group(aGroup)->edge, opposite);
    QCOMPARE(model.group(secondGroup)->windowId, firstWindow); QCOMPARE(model.group(secondGroup)->edge, side);
    QCOMPARE(model.tab(a)->activePane, originalPane); comparePinnedMembership(model);
    QVERIFY(model.moveGroup(aGroup, model.groupForTab(otherCentral), Placement::Left));
    QCOMPARE(model.group(aGroup)->location, GroupLocation::Central);
    QVERIFY(!model.pinnedLayout(secondWindow, opposite)); comparePinnedMembership(model);
    QVERIFY(model.setEdgePinned(firstWindow, side, false));
    QVERIFY(!model.pinnedLayout(firstWindow, side)); comparePinnedMembership(model);
    QVERIFY(model.setEdgePinned(firstWindow, side, true)); comparePinnedMembership(model);
    QVERIFY(model.closeWindow(firstWindow)); comparePinnedMembership(model);
    QCOMPARE(model.tab(a)->activePane, originalPane);
}

void PinnedLayoutTest::explicitTreesRoundTripAndLegacyOrdersMigrate_data()
{
    QTest::addColumn<int>("edge");
    for (const auto edge : edges) QTest::newRow(qPrintable(QString::number(int(edge)))) << int(edge);
}

void PinnedLayoutTest::explicitTreesRoundTripAndLegacyOrdersMigrate()
{
    QFETCH(int, edge); const auto side = Edge(edge);
    LayoutModel model; const auto root = model.createWindow(); model.addTab(root, "Central");
    const auto a = model.addTab(root, "A"); QVERIFY(model.moveTabToEdge(a, side, true));
    const auto b = model.addTabToNewGroup(root, "B", model.groupForTab(a), Placement::Bottom); QVERIFY(!b.isNull());
    const auto c = model.addTabToNewGroup(root, "C", model.groupForTab(a), Placement::Right); QVERIFY(!c.isNull());
    const auto hidden = model.addTab(root, "Hidden"); QVERIFY(model.moveTabToEdge(hidden, side));
    QVERIFY(model.setSplitRatio(model.pinnedLayout(root, side)->id, 0.37));
    LayoutModel restored; QVERIFY(restored.fromJson(model.toJson()));
    QCOMPARE(restored.toJson(), model.toJson()); comparePinnedMembership(restored);
    auto legacy = model.toJson(); auto windows = legacy.value("windows").toArray();
    auto window = windows.first().toObject(); window.remove("pinnedLayouts"); windows[0] = window; legacy.insert("windows", windows);
    LayoutModel migrated; QVERIFY(migrated.fromJson(legacy)); comparePinnedMembership(migrated);
    QVector<GroupId> expected;
    for (const auto id : model.window(root)->edgeGroups)
        if (model.group(id)->location == GroupLocation::Pinned && model.group(id)->edge == side) expected.append(id);
    if (side == Edge::Right || side == Edge::Bottom) std::reverse(expected.begin(), expected.end());
    const auto tree = migrated.pinnedLayout(root, side); QVERIFY(tree);
    QCOMPARE(leaves(tree), expected);
    const auto expectedAxis = side == Edge::Left || side == Edge::Right ? Qt::Horizontal : Qt::Vertical;
    const auto checkAxis = [&](const auto& recurse, LayoutTree node) -> void {
        if (!node || node->isLeaf()) return;
        QCOMPARE(node->orientation, expectedAxis); recurse(recurse, node->first); recurse(recurse, node->second);
    };
    checkAxis(checkAxis, tree);
    QVERIFY(!leaves(tree).contains(model.groupForTab(hidden)));
    LayoutModel secondRestore; QVERIFY(secondRestore.fromJson(migrated.toJson()));
    QCOMPARE(secondRestore.toJson(), migrated.toJson());
}

void PinnedLayoutTest::malformedExplicitTreesAreRejectedAtomically_data()
{
    QTest::addColumn<QString>("problem");
    for (const auto* problem : {"duplicate-leaf", "wrong-edge", "missing-leaf", "auto-hide-leaf",
                                "central-leaf", "unknown-leaf", "wrong-count", "not-array"})
        QTest::newRow(problem) << QString(problem);
}

void PinnedLayoutTest::malformedExplicitTreesAreRejectedAtomically()
{
    QFETCH(QString, problem);
    LayoutModel model; const auto root = model.createWindow(); const auto central = model.addTab(root, "Central");
    const auto a = model.addTab(root, "Pinned A"); QVERIFY(model.moveTabToEdge(a, Edge::Left, true));
    const auto b = model.addTabToNewGroup(root, "Pinned B", model.groupForTab(a), Placement::Bottom); QVERIFY(!b.isNull());
    const auto hidden = model.addTab(root, "Hidden"); QVERIFY(model.moveTabToEdge(hidden, Edge::Left));
    const auto before = model.toJson(); const auto revision = model.revision();
    auto malformed = before; auto windows = malformed.value("windows").toArray(); auto window = windows[0].toObject();
    auto layouts = window.value("pinnedLayouts").toArray(); QCOMPARE(layouts.size(), 4);
    if (problem == "duplicate-leaf") {
        auto branch = layouts[int(Edge::Left)].toObject();
        branch.insert("second", leafJson(model.groupForTab(a))); layouts[int(Edge::Left)] = branch;
    } else if (problem == "wrong-edge") {
        layouts[int(Edge::Right)] = layouts[int(Edge::Left)]; layouts[int(Edge::Left)] = QJsonValue(QJsonValue::Null);
    } else if (problem == "missing-leaf") layouts[int(Edge::Left)] = leafJson(model.groupForTab(a));
    else if (problem == "auto-hide-leaf") layouts[int(Edge::Left)] = leafJson(model.groupForTab(hidden));
    else if (problem == "central-leaf") layouts[int(Edge::Left)] = leafJson(model.groupForTab(central));
    else if (problem == "unknown-leaf") layouts[int(Edge::Left)] = leafJson(QUuid::createUuid());
    else if (problem == "wrong-count") layouts.removeLast();
    if (problem == "not-array") window.insert("pinnedLayouts", QJsonObject{});
    else window.insert("pinnedLayouts", layouts);
    windows[0] = window; malformed.insert("windows", windows);
    const auto result = model.fromJson(malformed); QVERIFY(!result); QVERIFY(!result.error.isEmpty());
    QCOMPARE(model.toJson(), before); QCOMPARE(model.revision(), revision); comparePinnedMembership(model);
}

QTEST_APPLESS_MAIN(PinnedLayoutTest)
#include "pinned_layout_test.moc"
