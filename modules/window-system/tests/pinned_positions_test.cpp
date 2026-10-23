// SPDX-License-Identifier: MIT
#include <window_system/layout_model.hpp>

#include <QJsonArray>
#include <QJsonObject>
#include <QTest>
#include <algorithm>

using namespace window_system;

namespace {
const QVector<Edge> sides{Edge::Left, Edge::Right, Edge::Top, Edge::Bottom};

void edgeRows() {
    QTest::addColumn<int>("edge");
    for (const auto edge : sides)
        QTest::newRow(qPrintable(QString::number(int(edge)))) << int(edge);
}

QJsonObject shape(LayoutTree tree) {
    if (!tree)
        return {};
    if (tree->isLeaf())
        return {{"group", tree->itemId.toString()}};
    return {{"axis", int(tree->orientation)},
            {"ratio", tree->ratio},
            {"first", shape(tree->first)},
            {"second", shape(tree->second)}};
}

LayoutTree containing(LayoutTree tree, GroupId group) {
    if (!tree)
        return {};
    if (tree->isLeaf())
        return tree->itemId == group ? tree : LayoutTree{};
    if (auto first = containing(tree->first, group))
        return first;
    return containing(tree->second, group);
}

LayoutTree splitFor(LayoutTree tree, GroupId first, GroupId second) {
    if (!tree || tree->isLeaf())
        return {};
    if (auto inner = splitFor(tree->first, first, second))
        return inner;
    if (auto inner = splitFor(tree->second, first, second))
        return inner;
    return containing(tree, first) && containing(tree, second) ? tree : LayoutTree{};
}

struct Fixture {
    WindowId window;
    TabId a, b, c;
    GroupId aGroup, bGroup, cGroup;
};

Fixture create(LayoutModel &model, Edge edge) {
    Fixture f;
    f.window = model.createWindow();
    model.addTab(f.window, "Central");
    f.a = model.addTab(f.window, "A");
    if (!model.moveTabToEdge(f.a, edge, true))
        return {};
    f.aGroup = model.groupForTab(f.a);
    f.b = model.addTabToNewGroup(f.window, "B", f.aGroup, Placement::Top);
    f.c = model.addTabToNewGroup(f.window, "C", f.aGroup, Placement::Right);
    f.bGroup = model.groupForTab(f.b);
    f.cGroup = model.groupForTab(f.c);
    const auto tree = model.pinnedLayout(f.window, edge);
    if (!tree || !model.setSplitRatio(tree->id, .62))
        return {};
    const auto inner = splitFor(model.pinnedLayout(f.window, edge), f.aGroup, f.cGroup);
    if (!inner || !model.setSplitRatio(inner->id, .31))
        return {};
    return f;
}

QJsonObject record(const LayoutModel &model, WindowId window) {
    for (const auto &entry : model.toJson().value("windows").toArray())
        if (QUuid(entry.toObject().value("id").toString()) == window)
            return entry.toObject();
    return {};
}

} // namespace

class PinnedPositionsTest final : public QObject {
    Q_OBJECT
  private slots:
    void tabGroupAndSidePinningRestoreTwoDimensionalSlots_data();
    void tabGroupAndSidePinningRestoreTwoDimensionalSlots();
    void differentRemovalAndPinOrdersKeepTheOriginalTopology_data();
    void differentRemovalAndPinOrdersKeepTheOriginalTopology();
    void aReturningSlotDoesNotReplaceNewLiveEdits_data() { edgeRows(); }
    void aReturningSlotDoesNotReplaceNewLiveEdits();
    void aNewSlotCanBeRememberedWhileAnotherSlotIsDormant_data() { edgeRows(); }
    void aNewSlotCanBeRememberedWhileAnotherSlotIsDormant();
    void laterLiveRatiosSurviveWhenAllGroupsBecomeDormant_data() { edgeRows(); }
    void laterLiveRatiosSurviveWhenAllGroupsBecomeDormant();
    void everyRemovalAndRestorePermutationPreservesTheTree_data() { edgeRows(); }
    void everyRemovalAndRestorePermutationPreservesTheTree();
    void dormantTreesRoundTrip_data() { edgeRows(); }
    void dormantTreesRoundTrip();
    void unusedDormantNodesAreReleased_data() { edgeRows(); }
    void unusedDormantNodesAreReleased();
    void malformedDormantTreesAreRejectedAtomically_data();
    void malformedDormantTreesAreRejectedAtomically();
};

void PinnedPositionsTest::tabGroupAndSidePinningRestoreTwoDimensionalSlots_data() {
    QTest::addColumn<int>("edge");
    QTest::addColumn<QString>("operation");
    for (const auto edge : sides)
        for (const auto *operation : {"tab", "group", "side"})
            QTest::newRow(qPrintable(QString("%1-%2").arg(int(edge)).arg(operation)))
                << int(edge) << QString(operation);
}

void PinnedPositionsTest::tabGroupAndSidePinningRestoreTwoDimensionalSlots() {
    QFETCH(int, edge);
    QFETCH(QString, operation);
    const auto side = Edge(edge);
    LayoutModel model;
    const auto f = create(model, side);
    QVERIFY(!f.window.isNull());
    const auto before = shape(model.pinnedLayout(f.window, side));
    const auto panes = model.panes(f.b);
    const LayoutModel retained = model;
    if (operation == "tab")
        QVERIFY(model.setTabPinned(f.b, false));
    else if (operation == "group")
        QVERIFY(model.setGroupPinned(f.bGroup, false));
    else
        QVERIFY(model.setEdgePinned(f.window, side, false));
    QVERIFY(model.window(f.window)->pinnedRestoreLayouts[size_t(side)]);
    QVERIFY(model.validate());
    QCOMPARE(shape(retained.pinnedLayout(f.window, side)), before);
    QVERIFY(!retained.window(f.window)->pinnedRestoreLayouts[size_t(side)]);
    if (operation == "tab")
        QVERIFY(model.setTabPinned(f.b, true));
    else if (operation == "group")
        QVERIFY(model.setGroupPinned(f.bGroup, true));
    else
        QVERIFY(model.setEdgePinned(f.window, side, true));
    QCOMPARE(shape(model.pinnedLayout(f.window, side)), before);
    QCOMPARE(model.groupForTab(f.b), f.bGroup);
    QCOMPARE(model.panes(f.b), panes);
    QVERIFY(!model.window(f.window)->pinnedRestoreLayouts[size_t(side)]);
    QVERIFY(model.validate());
}

void PinnedPositionsTest::differentRemovalAndPinOrdersKeepTheOriginalTopology_data() {
    QTest::addColumn<int>("edge");
    QTest::addColumn<bool>("wholeGroup");
    QTest::addColumn<bool>("reverse");
    for (const auto edge : sides)
        for (const bool whole : {false, true})
            for (const bool reverse : {false, true})
                QTest::newRow(
                    qPrintable(QString("%1-group-%2-reverse-%3").arg(int(edge)).arg(whole).arg(reverse)))
                    << int(edge) << whole << reverse;
}

void PinnedPositionsTest::differentRemovalAndPinOrdersKeepTheOriginalTopology() {
    QFETCH(int, edge);
    QFETCH(bool, wholeGroup);
    QFETCH(bool, reverse);
    const auto side = Edge(edge);
    LayoutModel model;
    const auto f = create(model, side);
    QVERIFY(!f.window.isNull());
    const auto before = shape(model.pinnedLayout(f.window, side));
    const QVector<TabId> tabs{f.b, f.c, f.a};
    const QVector<GroupId> groups{f.bGroup, f.cGroup, f.aGroup};
    for (int index = 0; index < tabs.size(); ++index)
        QVERIFY(wholeGroup ? model.setGroupPinned(groups[index], false)
                           : model.setTabPinned(tabs[index], false));
    QVERIFY(!model.pinnedLayout(f.window, side));
    for (int count = 0; count < tabs.size(); ++count) {
        const int index = reverse ? int(tabs.size()) - count - 1 : count;
        QVERIFY(wholeGroup ? model.setGroupPinned(groups[index], true)
                           : model.setTabPinned(tabs[index], true));
        QVERIFY(model.validate());
    }
    QCOMPARE(shape(model.pinnedLayout(f.window, side)), before);
    QVERIFY(!model.window(f.window)->pinnedRestoreLayouts[size_t(side)]);
}

void PinnedPositionsTest::aReturningSlotDoesNotReplaceNewLiveEdits() {
    QFETCH(int, edge);
    const auto side = Edge(edge);
    LayoutModel model;
    const auto f = create(model, side);
    QVERIFY(!f.window.isNull());
    QVERIFY(model.setTabPinned(f.b, false));
    const auto live = model.pinnedLayout(f.window, side);
    QVERIFY(live && !live->isLeaf());
    QVERIFY(model.setSplitRatio(live->id, .76));
    const auto d = model.addTabToNewGroup(f.window, "D", f.aGroup, Placement::Bottom);
    QVERIFY(!d.isNull());
    const auto dGroup = model.groupForTab(d);
    const auto newBranch = splitFor(model.pinnedLayout(f.window, side), f.aGroup, dGroup);
    QVERIFY(newBranch);
    QVERIFY(model.setSplitRatio(newBranch->id, .22));
    const auto preserved = shape(model.pinnedLayout(f.window, side));
    const auto cLeaf = containing(model.pinnedLayout(f.window, side), f.cGroup);
    QVERIFY(model.setTabPinned(f.b, true));
    const auto current = model.pinnedLayout(f.window, side);
    QVERIFY(current);
    // B used to be the root's first child. Its return must add that missing
    // ancestor, while retaining the complete edited A/C subtree below it.
    QCOMPARE(current->orientation, Qt::Vertical);
    QCOMPARE(current->ratio, .62);
    QCOMPARE(current->first->itemId, f.bGroup);
    const auto retainedLive = splitFor(current, f.aGroup, f.cGroup);
    QVERIFY(retainedLive);
    QCOMPARE(retainedLive->id, live->id);
    QCOMPARE(retainedLive->ratio, .76);
    QCOMPARE(containing(current, f.cGroup), cLeaf);
    const auto edited = splitFor(current, f.aGroup, dGroup);
    QVERIFY(edited);
    QCOMPARE(edited->id, newBranch->id);
    QCOMPARE(edited->ratio, .22);
    QVERIFY(containing(current, f.bGroup));
    QVERIFY(model.validate());
    // Removing the returning group leaves every later live edit intact.
    QVERIFY(model.setTabPinned(f.b, false));
    QCOMPARE(shape(model.pinnedLayout(f.window, side)), preserved);
}

void PinnedPositionsTest::aNewSlotCanBeRememberedWhileAnotherSlotIsDormant() {
    QFETCH(int, edge);
    const auto side = Edge(edge);
    LayoutModel model;
    const auto f = create(model, side);
    QVERIFY(!f.window.isNull());
    QVERIFY(model.setTabPinned(f.b, false));
    const auto d = model.addTabToNewGroup(f.window, "D", f.aGroup, Placement::Bottom);
    QVERIFY(!d.isNull());
    const auto dGroup = model.groupForTab(d);
    const auto edited = splitFor(model.pinnedLayout(f.window, side), f.aGroup, dGroup);
    QVERIFY(edited);
    QVERIFY(model.setSplitRatio(edited->id, .27));
    QVERIFY(model.setTabPinned(d, false));
    QVERIFY(model.setTabPinned(d, true));
    QVERIFY(model.setTabPinned(f.b, true));
    const auto restored = splitFor(model.pinnedLayout(f.window, side), f.aGroup, dGroup);
    QVERIFY(restored);
    QCOMPARE(restored->orientation, Qt::Vertical);
    QCOMPARE(restored->ratio, .27);
    QVERIFY(containing(model.pinnedLayout(f.window, side), f.bGroup));
    QVERIFY(!model.window(f.window)->pinnedRestoreLayouts[size_t(side)]);
    QVERIFY(model.validate());
}

void PinnedPositionsTest::laterLiveRatiosSurviveWhenAllGroupsBecomeDormant() {
    QFETCH(int, edge);
    const auto side = Edge(edge);
    LayoutModel model;
    const auto f = create(model, side);
    QVERIFY(!f.window.isNull());
    QVERIFY(model.setTabPinned(f.b, false));
    const auto surviving = model.pinnedLayout(f.window, side);
    QVERIFY(surviving && !surviving->isLeaf());
    QVERIFY(model.setSplitRatio(surviving->id, .79));
    QVERIFY(model.setTabPinned(f.a, false));
    QVERIFY(model.setTabPinned(f.c, false));
    QVERIFY(!model.pinnedLayout(f.window, side));
    QVERIFY(model.setTabPinned(f.c, true));
    QVERIFY(model.setTabPinned(f.b, true));
    QVERIFY(model.setTabPinned(f.a, true));
    const auto restored = model.pinnedLayout(f.window, side);
    QVERIFY(restored);
    QCOMPARE(restored->orientation, Qt::Vertical);
    QCOMPARE(restored->ratio, .62);
    const auto edited = splitFor(restored, f.aGroup, f.cGroup);
    QVERIFY(edited);
    QCOMPARE(edited->orientation, Qt::Horizontal);
    QCOMPARE(edited->ratio, .79);
    QCOMPARE(edited->first->itemId, f.aGroup);
    QCOMPARE(edited->second->itemId, f.cGroup);
    QVERIFY(model.validate());
}

void PinnedPositionsTest::everyRemovalAndRestorePermutationPreservesTheTree() {
    QFETCH(int, edge);
    const auto side = Edge(edge);
    QVector<int> removal{0, 1, 2};
    do {
        QVector<int> restore{0, 1, 2};
        do {
            LayoutModel model;
            const auto f = create(model, side);
            QVERIFY(!f.window.isNull());
            const auto expected = shape(model.pinnedLayout(f.window, side));
            const QVector<TabId> tabs{f.a, f.b, f.c};
            for (const auto index : removal)
                QVERIFY(model.setTabPinned(tabs[index], false));
            for (const auto index : restore)
                QVERIFY(model.setTabPinned(tabs[index], true));
            QCOMPARE(shape(model.pinnedLayout(f.window, side)), expected);
            QVERIFY(model.validate());
            QVERIFY(!model.window(f.window)->pinnedRestoreLayouts[size_t(side)]);
        } while (std::next_permutation(restore.begin(), restore.end()));
    } while (std::next_permutation(removal.begin(), removal.end()));
}

void PinnedPositionsTest::dormantTreesRoundTrip() {
    QFETCH(int, edge);
    const auto side = Edge(edge);
    LayoutModel model;
    const auto f = create(model, side);
    QVERIFY(!f.window.isNull());
    const auto before = shape(model.pinnedLayout(f.window, side));
    QVERIFY(model.setEdgePinned(f.window, side, false));
    const auto snapshot = model.toJson();
    QVERIFY(record(model, f.window).contains("pinnedRestoreLayouts"));
    LayoutModel restored;
    QVERIFY(restored.fromJson(snapshot));
    QCOMPARE(restored.toJson(), snapshot);
    QVERIFY(restored.setEdgePinned(f.window, side, true));
    QCOMPARE(shape(restored.pinnedLayout(f.window, side)), before);
    QVERIFY(!record(restored, f.window).contains("pinnedRestoreLayouts"));
    auto withoutRememberedSlots = snapshot;
    auto windows = withoutRememberedSlots.value("windows").toArray();
    auto window = windows.first().toObject();
    window.remove("pinnedRestoreLayouts");
    windows[0] = window;
    withoutRememberedSlots.insert("windows", windows);
    LayoutModel noRememberedSlots;
    QVERIFY(noRememberedSlots.fromJson(withoutRememberedSlots));
    QVERIFY(noRememberedSlots.setEdgePinned(f.window, side, true));
    QCOMPARE(noRememberedSlots.groupForTab(f.a), f.aGroup);
    QCOMPARE(noRememberedSlots.groupForTab(f.b), f.bGroup);
    QCOMPARE(noRememberedSlots.groupForTab(f.c), f.cGroup);
    QVERIFY(noRememberedSlots.validate());
}

void PinnedPositionsTest::unusedDormantNodesAreReleased() {
    QFETCH(int, edge);
    const auto side = Edge(edge);
    LayoutModel model;
    const auto f = create(model, side);
    QVERIFY(!f.window.isNull());
    for (int iteration = 0; iteration < 40; ++iteration) {
        const auto added = model.addTabToNewGroup(f.window, "Temporary", f.aGroup, Placement::Top);
        QVERIFY(!added.isNull());
        QVERIFY(model.setTabPinned(added, false));
        std::weak_ptr<const LayoutNode> observer = model.window(f.window)->pinnedRestoreLayouts[size_t(side)];
        QVERIFY(!observer.expired());
        QVERIFY(model.removeTab(added));
        QVERIFY(!model.window(f.window)->pinnedRestoreLayouts[size_t(side)]);
        QVERIFY(observer.expired());
        QVERIFY(model.validate());
    }
    QVERIFY(model.setTabPinned(f.b, false));
    const auto destination = model.createWindow();
    model.addTab(destination, "Other window");
    QVERIFY(model.moveTabToWindow(f.b, destination));
    QVERIFY(!model.window(f.window)->pinnedRestoreLayouts[size_t(side)]);
    QVERIFY(!record(model, f.window).contains("pinnedRestoreLayouts"));
    QVERIFY(model.validate());
}

void PinnedPositionsTest::malformedDormantTreesAreRejectedAtomically_data() {
    QTest::addColumn<QString>("problem");
    for (const auto *problem :
         {"not-array", "wrong-count", "unknown-slot", "duplicate-slot", "wrong-side", "live-node-id",
          "slot-node-id", "no-hidden-reference", "axis", "ratio", "depth"})
        QTest::newRow(problem) << QString(problem);
}

void PinnedPositionsTest::malformedDormantTreesAreRejectedAtomically() {
    QFETCH(QString, problem);
    LayoutModel model;
    const auto f = create(model, Edge::Left);
    QVERIFY(!f.window.isNull());
    QVERIFY(model.setTabPinned(f.b, false));
    const auto before = model.toJson();
    const auto revision = model.revision();
    auto broken = before;
    auto windows = broken.value("windows").toArray();
    auto window = windows.first().toObject();
    auto layouts = window.value("pinnedRestoreLayouts").toArray();
    QVERIFY(layouts[0].isObject());
    auto tree = layouts[0].toObject();
    auto first = tree.value("first").toObject();
    QVERIFY(first.contains("itemId"));
    if (problem == "unknown-slot")
        first.insert("itemId", QUuid::createUuid().toString());
    if (problem == "duplicate-slot")
        first.insert("itemId", f.aGroup.toString());
    tree.insert("first", first);
    if (problem == "live-node-id")
        tree.insert("id", model.pinnedLayout(f.window, Edge::Left)->id.toString());
    if (problem == "slot-node-id")
        tree.insert("id", f.bGroup.toString());
    if (problem == "axis")
        tree.insert("orientation", "diagonal");
    if (problem == "ratio")
        tree.insert("ratio", 0.99);
    if (problem == "no-hidden-reference")
        tree = {{"id", QUuid::createUuid().toString()}, {"itemId", f.aGroup.toString()}};
    if (problem == "depth")
        for (int depth = 0; depth < 66; ++depth)
            tree = {{"id", QUuid::createUuid().toString()},
                    {"orientation", "vertical"},
                    {"ratio", 0.5},
                    {"first", tree},
                    {"second",
                     QJsonObject{{"id", QUuid::createUuid().toString()}, {"itemId", f.aGroup.toString()}}}};
    layouts[0] = tree;
    if (problem == "wrong-side") {
        layouts[1] = layouts[0];
        layouts[0] = QJsonValue(QJsonValue::Null);
    }
    if (problem == "wrong-count")
        layouts.removeLast();
    window.insert("pinnedRestoreLayouts",
                  problem == "not-array" ? QJsonValue(QJsonObject{}) : QJsonValue(layouts));
    windows[0] = window;
    broken.insert("windows", windows);
    QVERIFY(!model.fromJson(broken));
    QCOMPARE(model.toJson(), before);
    QCOMPARE(model.revision(), revision);
    QVERIFY(model.validate());
}

QTEST_GUILESS_MAIN(PinnedPositionsTest)
#include "pinned_positions_test.moc"
