// SPDX-License-Identifier: MIT
#include <window_system/layout_model.hpp>

#include <QTest>

using namespace window_system;

namespace {
LayoutTree leafFor(LayoutTree tree, PaneId pane)
{
    if (!tree || tree->isLeaf()) return tree && tree->itemId == pane ? tree : LayoutTree{};
    if (auto found = leafFor(tree->first, pane)) return found;
    return leafFor(tree->second, pane);
}
void compareSlots(LayoutTree before, LayoutTree after)
{
    QVERIFY(before && after);
    QCOMPARE(before->id, after->id);
    QCOMPARE(before->orientation, after->orientation);
    QCOMPARE(before->ratio, after->ratio);
    QCOMPARE(before->isLeaf(), after->isLeaf());
    if (!before->isLeaf()) { compareSlots(before->first, after->first); compareSlots(before->second, after->second); }
}
}

class MovementModelTest final : public QObject {
    Q_OBJECT
private slots:
    void paneTransferRetainsLeafAndCollapsesSource_data();
    void paneTransferRetainsLeafAndCollapsesSource();
    void paneTransferToExistingAndEmptyWindows_data();
    void paneTransferToExistingAndEmptyWindows();
    void swapPreservesSplitSlotsRatiosAndSnapshot();
    void invalidTransfersAreAtomic();
};

void MovementModelTest::paneTransferRetainsLeafAndCollapsesSource_data()
{
    QTest::addColumn<int>("direction");
    QTest::addColumn<bool>("last");
    for (const auto direction : {Placement::Left, Placement::Right, Placement::Top, Placement::Bottom})
        for (const bool last : {false, true})
            QTest::newRow(qPrintable(QStringLiteral("direction-%1-last-%2").arg(int(direction)).arg(last))) << int(direction) << last;
}
void MovementModelTest::paneTransferRetainsLeafAndCollapsesSource()
{
    QFETCH(int, direction);
    QFETCH(bool, last);
    LayoutModel model;
    const auto sourceWindow = model.createWindow();
    const auto source = model.addTab(sourceWindow, "source");
    const auto pane = model.tab(source)->activePane;
    const auto retained = last ? PaneId{} : model.splitPane(source, pane, Placement::Bottom);
    const auto target = model.addTab(sourceWindow, "target");
    QVERIFY(model.moveTab(target, model.groupForTab(source), Placement::Right));
    const auto sourceGroup = model.groupForTab(source);
    const auto targetGroup = model.groupForTab(target);
    const auto targetPane = model.tab(target)->activePane;
    const auto carried = leafFor(model.tab(source)->panes, pane);
    const auto frozen = model;
    QVERIFY(model.movePane(source, pane, target, targetPane, static_cast<Placement>(direction)));
    QCOMPARE(leafFor(model.tab(target)->panes, pane), carried);
    QCOMPARE(model.tabForPane(pane), target);
    QCOMPARE(model.tab(target)->activePane, pane);
    QCOMPARE(model.group(targetGroup)->activeTab, target);
    QCOMPARE(model.window(sourceWindow)->activeGroup, targetGroup);
    const auto tree = model.tab(target)->panes;
    QCOMPARE(tree->orientation, direction == int(Placement::Left) || direction == int(Placement::Right) ? Qt::Horizontal : Qt::Vertical);
    const bool before = direction == int(Placement::Left) || direction == int(Placement::Top);
    QCOMPARE((before ? tree->first : tree->second)->itemId, pane);
    QCOMPARE((before ? tree->second : tree->first)->itemId, targetPane);
    if (last) {
        QVERIFY(!model.tab(source));
        QVERIFY(!model.group(sourceGroup));
        QCOMPARE(model.centralGroups(sourceWindow), QVector<GroupId>{targetGroup});
        QVERIFY(model.window(sourceWindow)->central->isLeaf());
    } else {
        QCOMPARE(model.panes(source), QVector<PaneId>{retained});
        QCOMPARE(model.tab(source)->activePane, retained);
    }
    QCOMPARE(frozen.tabForPane(pane), source);
    QCOMPARE(leafFor(frozen.tab(source)->panes, pane), carried);
    QVERIFY(model.validate());
}

void MovementModelTest::paneTransferToExistingAndEmptyWindows_data()
{
    QTest::addColumn<bool>("empty");
    QTest::addColumn<bool>("floating");
    QTest::addColumn<bool>("last");
    for (const bool empty : {false, true}) for (const bool floating : {false, true}) for (const bool last : {false, true})
        QTest::newRow(qPrintable(QStringLiteral("empty-%1-floating-%2-last-%3").arg(empty).arg(floating).arg(last))) << empty << floating << last;
}
void MovementModelTest::paneTransferToExistingAndEmptyWindows()
{
    QFETCH(bool, empty);
    QFETCH(bool, floating);
    QFETCH(bool, last);
    LayoutModel model;
    const auto root = model.createWindow();
    const auto sourceWindow = model.createWindow(WindowKind::Floating, root);
    const auto source = model.addTab(sourceWindow, "carried title");
    const auto pane = model.tab(source)->activePane;
    const auto retained = last ? PaneId{} : model.splitPane(source, pane, Placement::Bottom);
    const auto targetWindow = model.createWindow(floating ? WindowKind::Floating : WindowKind::Workbench, root);
    const auto existing = empty ? TabId{} : model.addTab(targetWindow, "target");
    const auto carried = leafFor(model.tab(source)->panes, pane);
    const auto destination = model.movePaneToWindow(source, pane, targetWindow, Placement::Bottom);
    QVERIFY(!destination.isNull());
    QCOMPARE(model.tabForPane(pane), destination);
    QCOMPARE(leafFor(model.tab(destination)->panes, pane), carried);
    QCOMPARE(model.group(model.groupForTab(destination))->windowId, targetWindow);
    QCOMPARE(model.tab(destination)->activePane, pane);
    if (empty) {
        QVERIFY(destination != source);
        QCOMPARE(model.tab(destination)->title, QStringLiteral("carried title"));
        QCOMPARE(model.panes(destination), QVector<PaneId>{pane});
    } else {
        QCOMPARE(destination, existing);
        QCOMPARE(model.panes(destination).size(), 2);
    }
    if (last) { QVERIFY(!model.tab(source)); QVERIFY(!model.window(sourceWindow)); }
    else { QCOMPARE(model.panes(source), QVector<PaneId>{retained}); QVERIFY(model.window(sourceWindow)); }
    QVERIFY(model.validate());
}

void MovementModelTest::swapPreservesSplitSlotsRatiosAndSnapshot()
{
    LayoutModel model;
    const auto root = model.createWindow();
    const auto tab = model.addTab(root, "swap");
    const auto first = model.tab(tab)->activePane;
    const auto right = model.splitPane(tab, first, Placement::Right);
    const auto bottom = model.splitPane(tab, first, Placement::Bottom);
    QVERIFY(model.setSplitRatio(model.tab(tab)->panes->id, .63));
    QVERIFY(model.setSplitRatio(model.tab(tab)->panes->first->id, .27));
    QVERIFY(model.activatePane(tab, first));
    const auto frozen = model;
    const auto before = model.tab(tab)->panes;
    QVERIFY(model.swapPanes(tab, first, right));
    compareSlots(before, model.tab(tab)->panes);
    QCOMPARE(model.panes(tab), (QVector<PaneId>{right, bottom, first}));
    QCOMPARE(model.tab(tab)->activePane, first);
    QCOMPARE(frozen.panes(tab), (QVector<PaneId>{first, bottom, right}));
    QCOMPARE(frozen.tab(tab)->panes, before);
    LayoutModel restored;
    QVERIFY(restored.fromJson(model.toJson()));
    QCOMPARE(restored.toJson(), model.toJson());
    QVERIFY(model.validate());
}

void MovementModelTest::invalidTransfersAreAtomic()
{
    LayoutModel model;
    const auto root = model.createWindow();
    const auto source = model.addTab(root, "source");
    const auto first = model.tab(source)->activePane;
    const auto target = model.addTab(root, "target");
    const auto targetPane = model.tab(target)->activePane;
    const auto emptyWindow = model.createWindow();
    const auto invalid = QUuid::createUuid();
    const auto before = model.toJson();
    for (const auto result : {
        model.movePane(source, first, source), model.movePane(source, first, target, first),
        model.movePane(source, invalid, target), model.movePane(source, first, invalid),
        model.movePane(source, first, target, targetPane, Placement::Center),
        model.movePane(source, first, target, targetPane, static_cast<Placement>(99)),
        model.swapPanes(source, first, first), model.swapPanes(source, first, targetPane), model.swapPanes(invalid, first, targetPane)}) {
        QVERIFY(!result);
        QCOMPARE(model.toJson(), before);
    }
    QVERIFY(model.movePaneToWindow(source, first, root).isNull());
    QCOMPARE(model.toJson(), before);
    QVERIFY(model.movePaneToWindow(source, first, invalid).isNull());
    QCOMPARE(model.toJson(), before);
    QVERIFY(model.movePaneToWindow(source, first, emptyWindow, Placement::Center).isNull());
    QCOMPARE(model.toJson(), before);
    QVERIFY(model.tabForPane(invalid).isNull());
    QVERIFY(model.tabForPane({}).isNull());
}

QTEST_APPLESS_MAIN(MovementModelTest)
#include "movement_model_test.moc"
