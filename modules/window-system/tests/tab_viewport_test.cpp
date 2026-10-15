// SPDX-License-Identifier: MIT
#include "chrome/workspace_window.hpp"
#include "docking/dock_surface.hpp"
#include "docking/tab_strip.hpp"
#include <window_system/window_manager.hpp>

#include <QApplication>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QTest>
#include <QVBoxLayout>
#include <algorithm>

using namespace window_system;

namespace {
GroupTabs* groupView(WindowManager& manager, WindowId window, GroupId id)
{
    for (auto* widget : manager.windowWidget(window)->findChildren<QWidget*>())
        if (auto* group = dynamic_cast<GroupTabs*>(widget);
            group && group->isVisible() && group->property("groupId").toUuid() == id) return group;
    return nullptr;
}
QScrollBar* axisScroll(GroupTabs* tabs)
{
    return tabs->findChild<QScrollArea*>("TabScrollViewport")->horizontalScrollBar();
}
void populate(GroupTabs& tabs, int count = 22)
{
    for (int index = 0; index < count; ++index)
        tabs.addTab(new QLabel(QString("Content %1").arg(index)), {},
            QString("Workspace tab %1").arg(index), QUuid::createUuid());
}
void settle() { QApplication::processEvents(); QApplication::processEvents(); }
}

class TabViewportTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); }
    void initialWindowWithShortTabsDoesNotRetainPrelayoutOffsets_data();
    void initialWindowWithShortTabsDoesNotRetainPrelayoutOffsets();
    void restoringAnUnshownViewportClampsToNaturalContent();
    void localResizePreservesTheVisibleAnchor_data();
    void localResizePreservesTheVisibleAnchor();
    void reconstructedGroupsPreserveTheirAnchor_data();
    void reconstructedGroupsPreserveTheirAnchor();
    void pinnedEdgesPreserveTheCentralAnchor_data();
    void pinnedEdgesPreserveTheCentralAnchor();
    void lastActiveTabNeverManufacturesTrailingSpace();
    void underfilledStripTracksBothDirectionsWithoutOverscroll();
    void activeTabMovesOnlyAfterTheShrinkingEdgeReachesIt();
    void explicitScrollingCancelsQueuedFocusProtection();
    void movingAWindowOrVerticallyResizingNeverScrollsItsStrip();
    void realWindowResizePreservesTheUnchangedSide_data();
    void realWindowResizePreservesTheUnchangedSide();
};

void TabViewportTest::initialWindowWithShortTabsDoesNotRetainPrelayoutOffsets_data()
{
    QTest::addColumn<bool>("floating");
    QTest::newRow("workbench") << false;
    QTest::newRow("floating") << true;
}
void TabViewportTest::initialWindowWithShortTabsDoesNotRetainPrelayoutOffsets()
{
    QFETCH(bool, floating);
    WindowManager manager;
    const auto owner = manager.createWindow();
    const auto window = floating
        ? manager.createWindow(WindowKind::Floating, {"Initial fit", {920, 600}}, owner)
        : manager.createWindow(WindowKind::Workbench, {"Initial fit", {920, 600}});
    QVector<TabId> ids;
    for (const auto* title : {"A", "B", "C"})
        ids.append(manager.addTab(window, new QLabel(title), {title, "label", {}}));
    QVERIFY(manager.activateTab(ids.last()));
    manager.showWindow(window); QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window))); settle();
    auto* group = groupView(manager, window, manager.model().groupForTab(ids.first())); QVERIFY(group);
    auto* scroll = group->findChild<QScrollArea*>("TabScrollViewport"); QVERIFY(scroll);
    QCOMPARE(group->scrollOffset(), 0); QCOMPARE(axisScroll(group)->minimum(), 0); QCOMPARE(axisScroll(group)->maximum(), 0);
    QCOMPARE(scroll->widget()->width(), group->tabBar()->width() + 39);
    QVERIFY(scroll->widget()->width() < scroll->viewport()->width());
    const auto origin = group->tabBar()->mapTo(scroll->viewport(), QPoint());
    for (int index = 0; index < group->count(); ++index)
        QVERIFY(scroll->viewport()->rect().contains(group->tabBar()->displayTabRect(index).translated(origin)));
    QVERIFY(manager.model().validate());
}

void TabViewportTest::restoringAnUnshownViewportClampsToNaturalContent()
{
    QWidget root; GroupTabs tabs(defaultTheme(), &root);
    for (const auto* title : {"A", "B", "C"}) tabs.addTab(new QLabel(title), {}, title, QUuid::createUuid());
    tabs.resize(120, 240); tabs.setCurrentIndex(2);
    const auto unshown = tabs.captureViewportState(); QVERIFY(unshown.bounds.isEmpty());
    root.resize(920, 600); tabs.setGeometry(0, 0, 920, 600);
    tabs.restoreViewportState(unshown); applyTheme(&root); root.show();
    QVERIFY(QTest::qWaitForWindowExposed(&root)); settle();
    auto* scroll = tabs.findChild<QScrollArea*>("TabScrollViewport"); QVERIFY(scroll);
    QCOMPARE(tabs.scrollOffset(), 0); QCOMPARE(axisScroll(&tabs)->maximum(), 0);
    QCOMPARE(scroll->widget()->width(), tabs.tabBar()->width() + 39);
    const auto origin = tabs.tabBar()->mapTo(scroll->viewport(), QPoint());
    for (int index = 0; index < tabs.count(); ++index)
        QVERIFY(scroll->viewport()->rect().contains(tabs.tabBar()->displayTabRect(index).translated(origin)));
}

void TabViewportTest::localResizePreservesTheVisibleAnchor_data()
{
    QTest::addColumn<int>("direction"); QTest::addColumn<bool>("visible");
    for (int direction = 0; direction < 4; ++direction) for (bool visible : {false, true})
        QTest::newRow(qPrintable(QString("direction-%1-active-%2").arg(direction).arg(visible))) << direction << visible;
}
void TabViewportTest::localResizePreservesTheVisibleAnchor()
{
    QFETCH(int, direction); QFETCH(bool, visible);
    QWidget root; GroupTabs tabs(defaultTheme(), &root); populate(tabs);
    root.resize(760, 380); tabs.setGeometry(20, 20, 680, 300); applyTheme(&root); root.show();
    QVERIFY(QTest::qWaitForWindowExposed(&root)); settle();
    const int active = visible ? 8 : 0; tabs.setCurrentIndex(active, false);
    const auto area = tabs.tabBar()->tabRect(active);
    const int viewportWidth = tabs.captureViewportState().bounds.width();
    const int offset = !visible ? 500 : direction == 1
        ? area.right() - viewportWidth + 21 : area.left() - 20;
    axisScroll(&tabs)->setValue(offset); settle();
    const auto old = tabs.captureViewportState(); QCOMPARE(old.activeFullyVisible, visible);
    if (direction == 0) tabs.setGeometry(361, 20, 339, 300);       // new group on the left
    else if (direction == 1) tabs.setGeometry(20, 20, 339, 300);  // new group on the right
    else if (direction == 2) tabs.setGeometry(20, 171, 680, 149);
    else tabs.setGeometry(20, 20, 680, 149);
    settle(); const auto current = tabs.captureViewportState();
    if (direction >= 2) QCOMPARE(current.offset, old.offset);
    else if (visible) {
        const int candidate=old.offset + current.bounds.left()-old.bounds.left();
        QCOMPARE(current.offset,qBound(area.right()-current.bounds.width()+1,candidate,area.left()));
        QVERIFY(current.activeFullyVisible);
    } else {
        QCOMPARE(current.bounds.left() - current.offset, old.bounds.left() - old.offset);
        QVERIFY(!current.activeFullyVisible);
    }
    const int stable = current.offset; settle(); QCOMPARE(tabs.scrollOffset(), stable);
    QCOMPARE(tabs.count(), 22);
}

void TabViewportTest::reconstructedGroupsPreserveTheirAnchor_data()
{
    QTest::addColumn<int>("placement");
    for (const auto placement : {Placement::Left, Placement::Right, Placement::Top, Placement::Bottom})
        QTest::newRow(qPrintable(QString("placement-%1").arg(int(placement)))) << int(placement);
}
void TabViewportTest::reconstructedGroupsPreserveTheirAnchor()
{
    QFETCH(int, placement);
    WindowManager manager; const auto window = manager.createWindow(WindowKind::Workbench, {"Anchor", {1100, 680}});
    QVector<TabId> ids;
    for (int index = 0; index < 22; ++index)
        ids.append(manager.addTab(window, new QLabel(QString("Content %1").arg(index)),
            {QString("Workspace tab %1").arg(index), "label", {}}));
    QVERIFY(manager.activateTab(ids.first())); manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window))); settle();
    const auto id = manager.model().groupForTab(ids.first()); auto* oldView = groupView(manager, window, id); QVERIFY(oldView);
    axisScroll(oldView)->setValue(500); settle();
    const auto old = oldView->captureViewportState(); QVERIFY(!old.activeFullyVisible);
    auto* content = manager.paneWidget(manager.model().tab(ids.first())->activePane);
    QVERIFY(manager.moveTab(ids.last(), id, Placement(placement))); settle();
    auto* currentView = groupView(manager, window, id); QVERIFY(currentView);
    const auto current = currentView->captureViewportState();
    QCOMPARE(current.bounds.left() - current.offset, old.bounds.left() - old.offset);
    QCOMPARE(manager.paneWidget(manager.model().tab(ids.first())->activePane), content);
    const int offset = currentView->scrollOffset(); settle(); QCOMPARE(currentView->scrollOffset(), offset);
    QVERIFY(manager.model().validate());
}

void TabViewportTest::pinnedEdgesPreserveTheCentralAnchor_data()
{
    QTest::addColumn<int>("edge");
    for (const auto edge : {Edge::Left, Edge::Right, Edge::Top, Edge::Bottom})
        QTest::newRow(qPrintable(QString("edge-%1").arg(int(edge)))) << int(edge);
}
void TabViewportTest::pinnedEdgesPreserveTheCentralAnchor()
{
    QFETCH(int, edge);
    WindowManager manager; const auto window = manager.createWindow(WindowKind::Workbench, {"Anchor", {1100, 680}});
    QVector<TabId> ids;
    for (int index = 0; index < 22; ++index)
        ids.append(manager.addTab(window, new QLabel(QString("Content %1").arg(index)),
            {QString("Workspace tab %1").arg(index), "label", {}}));
    QVERIFY(manager.activateTab(ids.first())); manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window))); settle();
    const auto group = manager.model().groupForTab(ids.first()); auto* view = groupView(manager, window, group); QVERIFY(view);
    axisScroll(view)->setValue(500); settle(); const auto old = view->captureViewportState();
    QVERIFY(manager.moveTabToEdge(ids.last(), Edge(edge), true)); settle();
    view = groupView(manager, window, group); QVERIFY(view); const auto current = view->captureViewportState();
    QCOMPARE(current.bounds.left() - current.offset, old.bounds.left() - old.offset);
    QVERIFY(manager.pinTab(ids.last(), false)); settle();
    view = groupView(manager, window, group); QVERIFY(view);
    // Unpinning introduces the auto-hide rail, so the final viewport edge need
    // not equal its initial edge. The same content keeps its screen position.
    const auto unpinned=view->captureViewportState();
    QCOMPARE(unpinned.bounds.left()-unpinned.offset,old.bounds.left()-old.offset);
    QVERIFY(manager.model().validate());
}

void TabViewportTest::lastActiveTabNeverManufacturesTrailingSpace()
{
    QWidget root; auto* row = new QVBoxLayout(&root); row->setContentsMargins(0, 0, 0, 0);
    GroupTabs tabs(defaultTheme(), &root); row->addWidget(&tabs); populate(tabs, 12);
    root.resize(700, 320); applyTheme(&root); root.show();
    QVERIFY(QTest::qWaitForWindowExposed(&root)); settle();
    tabs.setCurrentIndex(11); settle(); const auto old = tabs.captureViewportState(); QVERIFY(old.activeFullyVisible);
    const auto active=tabs.tabBar()->tabRect(11); const int naturalWidth = tabs.tabBar()->width() + 39;
    root.resize(340, 320); settle();
    auto* scroll = tabs.findChild<QScrollArea*>("TabScrollViewport"); QVERIFY(scroll);
    QCOMPARE(tabs.scrollOffset(),active.right()-scroll->viewport()->width()+1);
    QCOMPARE(scroll->widget()->width(),naturalWidth);
    for (int width : {540, 380, 620, 340}) {
        root.resize(width, 320); settle();
        QVERIFY(tabs.captureViewportState().activeFullyVisible);
        QCOMPARE(scroll->widget()->width(),naturalWidth);
        QCOMPARE(axisScroll(&tabs)->maximum(),std::max(0,naturalWidth-scroll->viewport()->width()));
        QVERIFY(tabs.scrollOffset()<=axisScroll(&tabs)->maximum());
        QCOMPARE(tabs.count(), 12); QCOMPARE(tabs.tabBar()->width(), naturalWidth - 39);
    }
    const int before=tabs.scrollOffset();
    tabs.scrollByWheel(QPoint(0, 300)); settle(); QCOMPARE(tabs.scrollOffset(),std::max(0,before-300));
    const int offset = tabs.scrollOffset(); settle(); QCOMPARE(tabs.scrollOffset(), offset);
}

void TabViewportTest::underfilledStripTracksBothDirectionsWithoutOverscroll()
{
    QWidget root; GroupTabs tabs(defaultTheme(),&root);
    for(const auto* title:{"A","B","C"}) tabs.addTab(new QLabel(title),{},title,QUuid::createUuid());
    root.resize(940,400); tabs.setGeometry(450,20,460,300); applyTheme(&root); root.show();
    QVERIFY(QTest::qWaitForWindowExposed(&root)); settle();
    for(const QRect area:{QRect(300,20,610,300),QRect(500,20,410,300),QRect(350,20,560,300),QRect(550,20,360,300)}) {
        tabs.setGeometry(area); settle();
        auto* scroll=tabs.findChild<QScrollArea*>("TabScrollViewport"); QVERIFY(scroll);
        QCOMPARE(axisScroll(&tabs)->maximum(),0); QCOMPARE(tabs.scrollOffset(),0);
        QCOMPARE(tabs.tabBar()->mapTo(scroll->viewport(),QPoint()).x(),0);
        for(int i=0;i<tabs.count();++i) QVERIFY(scroll->viewport()->rect().contains(tabs.tabBar()->displayTabRect(i)));
        axisScroll(&tabs)->setValue(999999); QCOMPARE(tabs.scrollOffset(),0);
    }
}
void TabViewportTest::activeTabMovesOnlyAfterTheShrinkingEdgeReachesIt()
{
    QWidget root; GroupTabs tabs(defaultTheme(),&root); populate(tabs,18);
    root.resize(940,400); tabs.setGeometry(20,20,780,300); applyTheme(&root); root.show();
    QVERIFY(QTest::qWaitForWindowExposed(&root)); settle(); tabs.setCurrentIndex(5,false);
    const auto active=tabs.tabBar()->displayTabRect(5);
    constexpr int clearance=40;
    auto* scroll=tabs.findChild<QScrollArea*>("TabScrollViewport"); QVERIFY(scroll);
    axisScroll(&tabs)->setValue(active.right()-scroll->viewport()->width()+clearance+1); settle();
    const auto initial=tabs.captureViewportState(); QVERIFY(initial.activeFullyVisible);
    const int startWidth=tabs.width();
    for(int shrink:{10,25,40,45,60,90}) {
        tabs.resize(startWidth-shrink,300); settle();
        QCOMPARE(tabs.scrollOffset(),initial.offset+std::max(0,shrink-clearance));
        QVERIFY(tabs.captureViewportState().activeFullyVisible);
        QCOMPARE(scroll->widget()->width(),tabs.tabBar()->width()+39);
        QCOMPARE(axisScroll(&tabs)->maximum(),std::max(0,scroll->widget()->width()-scroll->viewport()->width()));
    }
}
void TabViewportTest::explicitScrollingCancelsQueuedFocusProtection()
{
    QWidget root; GroupTabs tabs(defaultTheme(), &root); populate(tabs);
    root.resize(760, 380); tabs.setGeometry(20, 20, 680, 300); applyTheme(&root); root.show();
    QVERIFY(QTest::qWaitForWindowExposed(&root)); settle();
    tabs.setCurrentIndex(8); settle(); QVERIFY(tabs.captureViewportState().activeFullyVisible);
    tabs.resize(340, 300);
    tabs.scrollByWheel(QPoint(0, -200)); const int authoredOffset = tabs.scrollOffset();
    settle(); QCOMPARE(tabs.scrollOffset(), authoredOffset);
    tabs.restoreViewportState(tabs.captureViewportState());
    tabs.scrollByWheel(QPoint(0, 80)); const int newerOffset = tabs.scrollOffset();
    settle(); QCOMPARE(tabs.scrollOffset(), newerOffset);
}

void TabViewportTest::movingAWindowOrVerticallyResizingNeverScrollsItsStrip()
{
    QWidget root; auto* row = new QVBoxLayout(&root); GroupTabs tabs(defaultTheme(), &root); row->addWidget(&tabs); populate(tabs);
    root.resize(760, 380); applyTheme(&root); root.show();
    QVERIFY(QTest::qWaitForWindowExposed(&root)); settle();
    axisScroll(&tabs)->setValue(500); settle();
    const auto old = tabs.captureViewportState(); root.move(root.pos() + QPoint(70, 30)); settle();
    QCOMPARE(tabs.scrollOffset(), old.offset);
    root.resize(root.width(), 280); settle(); QCOMPARE(tabs.scrollOffset(), old.offset);
    root.resize(600, 280); settle(); QCOMPARE(tabs.scrollOffset(), old.offset);
    root.resize(820, 400); settle(); QCOMPARE(tabs.scrollOffset(), old.offset);
}

void TabViewportTest::realWindowResizePreservesTheUnchangedSide_data()
{
    QTest::addColumn<bool>("left"); QTest::addColumn<bool>("visible");
    for (bool left : {false, true}) for (bool visible : {false, true})
        QTest::newRow(qPrintable(QString("resize-%1-active-%2").arg(left ? "left" : "right").arg(visible))) << left << visible;
}
void TabViewportTest::realWindowResizePreservesTheUnchangedSide()
{
    QFETCH(bool, left); QFETCH(bool, visible);
    WindowManager manager;
    const auto id = manager.createWindow(WindowKind::Workbench, {"Real window resize", {1100, 680}});
    QVector<TabId> ids;
    for (int index = 0; index < 22; ++index)
        ids.append(manager.addTab(id, new QLabel(QString("Content %1").arg(index)),
            {QString("Workspace tab %1").arg(index), "label", {}}));
    const int active = visible ? 8 : 0; QVERIFY(manager.activateTab(ids[active])); manager.showWindow(id);
    auto* window = manager.windowWidget(id); QVERIFY(QTest::qWaitForWindowExposed(window)); settle();
    const auto groupId = manager.model().groupForTab(ids[active]); auto* group = groupView(manager, id, groupId); QVERIFY(group);
    const auto area = group->tabBar()->tabRect(active); const int viewportWidth = group->captureViewportState().bounds.width();
    axisScroll(group)->setValue(!visible ? 500 : left ? area.left() - 20 : area.right() - viewportWidth + 21);
    settle(); const auto old = group->captureViewportState(); QCOMPARE(old.activeFullyVisible, visible);
    const auto oldGeometry = window->geometry(); constexpr int shrink = 330;
    window->setGeometry(oldGeometry.x() + (left ? shrink : 0), oldGeometry.y(),
                        oldGeometry.width() - shrink, oldGeometry.height());
    settle(); const auto current = group->captureViewportState();
    QCOMPARE(window->width(), oldGeometry.width() - shrink);
    if (visible) {
        const int candidate=old.offset+current.bounds.left()-old.bounds.left()+window->geometry().left()-oldGeometry.left();
        QCOMPARE(current.offset,qBound(area.right()-current.bounds.width()+1,candidate,area.left()));
        QVERIFY(current.activeFullyVisible);
    } else {
        QCOMPARE(current.windowClientBounds.left() + current.bounds.left() - current.offset,
                 old.windowClientBounds.left() + old.bounds.left() - old.offset);
        QCOMPARE(current.offset, old.offset + (left ? shrink : 0));
        QVERIFY(!current.activeFullyVisible);
    }
    const int preservedOffset = current.offset;
    window->move(window->pos() + QPoint(40, 25)); settle(); QCOMPARE(group->scrollOffset(), preservedOffset);
    const auto expanded = window->geometry(); window->resize(oldGeometry.width(), expanded.height()); settle();
    QCOMPARE(group->scrollOffset(), preservedOffset);
    // A later right-edge shrink must not count the previous independent move
    // as a left resize. Current off-screen focus cannot trigger protection.
    QVERIFY(manager.activateTab(ids.first())); group = groupView(manager, id, groupId); QVERIFY(group);
    axisScroll(group)->setValue(500); settle();
    window->resize(window->width() - 120, window->height());
    group->scrollByWheel(QPoint(0, -60)); const int newerOffset = group->scrollOffset(); settle();
    QCOMPARE(group->scrollOffset(), newerOffset); QCOMPARE(newerOffset, 560);
    QCOMPARE(manager.model().group(groupId)->tabs.size(), 22); QVERIFY(manager.model().validate());
}

QTEST_MAIN(TabViewportTest)
#include "tab_viewport_test.moc"
