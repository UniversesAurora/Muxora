// SPDX-License-Identifier: MIT
// Real strip adapters, without exposed windows or native input. Thresholds
// depend on the displayed label, not its pointer grab or animated neighbors.
#include "docking/tab_strip.hpp"
#include <QTest>

using namespace window_system;

namespace {
void axesAndGrabs(bool includeSticky)
{
    QTest::addColumn<TabBarPlacement>("placement");
    QTest::addColumn<int>("grabPercent");
    QTest::addColumn<int>("sizing");
    const QList<int> sizes = includeSticky
        ? QList<int>{-1, int(StickyTabSizing::Normal), int(StickyTabSizing::Shrink), int(StickyTabSizing::Compact)}
        : QList<int>{-1};
    for (const auto placement : {TabBarPlacement::Top, TabBarPlacement::Bottom, TabBarPlacement::Left, TabBarPlacement::Right})
        for (const int grab : {10, 50, 90}) for (const int size : sizes)
            QTest::newRow(qPrintable(QStringLiteral("%1-grab-%2-size-%3").arg(int(placement)).arg(grab).arg(size)))
                << placement << grab << size;
}

void fill(GroupTabs& group)
{
    for (int index = 0; index < 4; ++index)
        group.addTab(new QWidget, {}, QStringLiteral("Workspace %1").arg(index), QUuid::createUuid());
}

QPoint grabAt(QSize size, int percent)
{
    return {(size.width() - 1) * percent / 100, (size.height() - 1) * percent / 100};
}
}

class TabReorderWiringTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { TabBar::setAnimationsEnabled(false); }

    void localEdgesIgnoreTheGrabPosition_data() { axesAndGrabs(true); }
    void localEdgesIgnoreTheGrabPosition()
    {
        QFETCH(TabBarPlacement, placement); QFETCH(int, grabPercent); QFETCH(int, sizing);
        GroupTabs group(defaultTheme(), nullptr); group.resize(900, 500); fill(group);
        TabAppearance policy; policy.centralPlacement = placement; policy.showNewTabButton = false;
        policy.stickyTabsOnSeparateRow = sizing >= 0;
        if (sizing >= 0) policy.stickySizing = StickyTabSizing(sizing);
        group.setAppearance(policy);
        if (sizing >= 0) group.setStickyCount(3);
        auto* bar = group.tabBar();
        const auto original = bar->tabRect(1);
        const auto grab = grabAt(original.size(), grabPercent);
        const DragAnchor anchor{grab, original.size()};
        const QVector<CarriedLabel> labels{{bar->tabText(1), false}};
        group.setDraggedTab(bar->tabData(1).toUuid());
        group.setLiftedTabWithAnchor(bar->mapToGlobal(original.topLeft() + grab), anchor);
        group.setInsertionPreview(1, labels, sizing >= 0);
        QCOMPARE(bar->dropIndex(), 1);
        const bool tile = bar->vertical() && sizing == int(StickyTabSizing::Compact);
        const bool alongY = bar->vertical() && !tile;
        const bool centered = sizing == int(StickyTabSizing::Compact);
        auto held = bar->liftedRect();
        const auto move = [&](QRect rect) {
            const auto point = rect.topLeft() + anchor.forSize(rect.size(), centered);
            const auto global = bar->mapToGlobal(point);
            group.setLiftedTabWithAnchor(global, anchor);
            return global;
        };
        auto neighbor = bar->tabLayout().tabs.at(2).translated(bar->tabOrigin());
        if (alongY) held.moveBottom(neighbor.center().y()); else held.moveRight(neighbor.center().x());
        auto global = move(held);
        QCOMPARE(group.insertionIndex(global), 1);
        held.translate(alongY ? QPoint(0, 1) : QPoint(1, 0)); global = move(held);
        QCOMPARE(group.insertionIndex(global), 2);
        group.setInsertionPreview(2, labels, sizing >= 0);
        QCOMPARE(bar->dropIndex(), 2);
        QCOMPARE(group.insertionIndex(global), 2);
        neighbor = bar->tabLayout().tabs.at(2).translated(bar->tabOrigin());
        if (alongY) held.moveTop(neighbor.center().y()); else held.moveLeft(neighbor.center().x());
        global = move(held);
        QCOMPARE(group.insertionIndex(global), 2);
        held.translate(alongY ? QPoint(0, -1) : QPoint(-1, 0)); global = move(held);
        QCOMPARE(group.insertionIndex(global), 1);
        group.setInsertionPreview(1, labels, sizing >= 0);
        QCOMPARE(group.insertionIndex(global), 1);
        QVERIFY(!group.isVisible());
    }

    void incomingEdgesUseTheLatestPointer_data() { axesAndGrabs(false); }
    void incomingEdgesUseTheLatestPointer()
    {
        QFETCH(TabBarPlacement, placement); QFETCH(int, grabPercent);
        GroupTabs group(defaultTheme(), nullptr); group.resize(900, 500); fill(group);
        TabAppearance policy; policy.centralPlacement = placement; policy.showNewTabButton = false;
        group.setAppearance(policy);
        auto* bar = group.tabBar();
        bar->setDropPreview(1, false, {bar->tabExtent(1, false)});
        const auto gap = bar->tabLayout().gap.first().translated(bar->tabOrigin());
        const auto grab = grabAt(gap.size(), grabPercent);
        TabDragVisual visual;
        visual.size = gap.size(); visual.anchor = grab;
        visual.active = QRect(QPoint(), gap.size()); visual.identity = QUuid::createUuid();
        visual.pieces.append(TabCarrierPiece{});
        bar->setIncomingVisual(visual, gap.topLeft() + grab);
        auto held = gap;
        const auto neighbor = bar->tabLayout().tabs.at(1).translated(bar->tabOrigin());
        if (bar->vertical()) held.moveBottom(neighbor.center().y()); else held.moveRight(neighbor.center().x());
        for (auto kind : {TabLayoutInput::Carried::Tab, TabLayoutInput::Carried::Tabs})
            QCOMPARE(bar->insertionAt(held.topLeft() + grab, 0, kind).index, 1);
        held.translate(bar->vertical() ? QPoint(0, 1) : QPoint(1, 0));
        // No setIncomingVisual between the two queries: a stored pointer from
        // the previous frame must not decide this new input event's slot.
        for (auto kind : {TabLayoutInput::Carried::Tab, TabLayoutInput::Carried::Tabs})
            QCOMPARE(bar->insertionAt(held.topLeft() + grab, 0, kind).index, 2);
        QCOMPARE(bar->dropIndex(), 1);
        QVERIFY(!group.isVisible());
    }

    void incomingOverlapWaitsForMotion_data() { axesAndGrabs(false); }
    void incomingOverlapWaitsForMotion()
    {
        QFETCH(TabBarPlacement, placement); QFETCH(int, grabPercent);
        GroupTabs group(defaultTheme(), nullptr); group.resize(900, 500); fill(group);
        TabAppearance policy; policy.centralPlacement = placement; policy.showNewTabButton = false;
        group.setAppearance(policy);
        auto* bar = group.tabBar();
        bar->setDropPreview(1, false, {bar->tabExtent(1, false)});
        const auto gap = bar->tabLayout().gap.first().translated(bar->tabOrigin());
        const auto grab = grabAt(gap.size(), grabPercent);
        TabDragVisual visual; visual.size = gap.size(); visual.anchor = grab;
        visual.active = QRect(QPoint(), gap.size()); visual.pieces.append(TabCarrierPiece{});
        const auto point = bar->vertical() ? QPoint(gap.center().x(), gap.top() + 1)
            : QPoint(gap.left() + 1, gap.center().y());
        bar->setIncomingVisual(visual, point);
        const auto step = bar->vertical() ? QPoint(0, 1) : QPoint(1, 0);
        // An initial off-center grab may overlap the previous tab. Publishing
        // or releasing that same input must preserve the accepted slot.
        QCOMPARE(bar->insertionAt(point).index, 1);
        QCOMPARE(bar->insertionAt(point + step).index, 1);
        if (grabPercent == 90) QCOMPARE(bar->insertionAt(point - step).index, 0);
        QVERIFY(!group.isVisible());
    }

    void wrappedGapFollowsTheCarriedRow_data()
    {
        QTest::addColumn<bool>("bottom");
        QTest::newRow("top") << false; QTest::newRow("bottom") << true;
    }
    void wrappedGapFollowsTheCarriedRow()
    {
        QFETCH(bool, bottom);
        GroupTabs group(defaultTheme(), nullptr); group.resize(350, 500);
        for (int index = 0; index < 8; ++index)
            group.addTab(new QWidget, {}, QStringLiteral("Workspace %1").arg(index), QUuid::createUuid());
        TabAppearance policy; policy.centralPlacement = bottom ? TabBarPlacement::Bottom : TabBarPlacement::Top;
        policy.multiline = true; policy.showAllRows = true; policy.showNewTabButton = false;
        group.setAppearance(policy);
        auto* bar = group.tabBar();
        int source = -1;
        const auto& initial = bar->tabLayout().tabs;
        for (int index = 0; index + 1 < initial.size(); ++index)
            if (initial[index].top() < initial[index + 1].top()) { source = index; break; }
        QVERIFY(source >= 0);
        const auto original = bar->tabRect(source);
        const auto grab = original.center() - original.topLeft();
        const DragAnchor anchor{grab, original.size()};
        const QVector<CarriedLabel> labels{{bar->tabText(source), false}};
        group.setDraggedTab(bar->tabData(source).toUuid());
        group.setLiftedTabWithAnchor(bar->mapToGlobal(original.center()), anchor);
        group.setInsertionPreview(source, labels);
        const auto neighbor = bar->tabLayout().tabs[source + 1].translated(bar->tabOrigin());
        auto held = bar->liftedRect(); held.moveTopLeft(neighbor.topLeft());
        const auto global = bar->mapToGlobal(held.topLeft() + anchor.forSize(held.size()));
        group.setLiftedTabWithAnchor(global, anchor);
        QCOMPARE(group.insertionIndex(global), source + 1);
        group.setInsertionPreview(source + 1, labels);
        QCOMPARE(group.insertionIndex(global), source + 1);
        QCOMPARE(bar->dropPreviewRect().top(), bar->liftedRect().top());
        QVERIFY(!group.isVisible());
    }
};

QTEST_MAIN(TabReorderWiringTest)
#include "tab_reorder_wiring_test.moc"
