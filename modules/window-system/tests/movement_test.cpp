// SPDX-License-Identifier: MIT
#include <window_system/window_manager.hpp>
#include "docking/dock_surface.hpp"
#include "chrome/workspace_window.hpp"
#include "panes/tab_content.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QFocusEvent>
#include <QLabel>
#include <QPointer>
#include <QSplitter>
#include <QTest>
#include <cmath>

using namespace window_system;

namespace {
TabId add(WindowManager& manager, WindowId window, const QString& title, GroupId group = {})
{
    return manager.addTab(window, new QLabel(title), {title, "test.text", {}}, group);
}
struct Grid {
    TabId topLeft, topRight, bottomLeft, bottomRight;
};
Grid createGrid(WindowManager& manager, WindowId window)
{
    Grid result;
    result.topLeft = add(manager, window, "top-left");
    result.topRight = add(manager, window, "top-right");
    manager.moveTab(result.topRight, manager.model().groupForTab(result.topLeft), Placement::Right);
    result.bottomLeft = add(manager, window, "bottom-left", manager.model().groupForTab(result.topLeft));
    manager.moveTab(result.bottomLeft, manager.model().groupForTab(result.topLeft), Placement::Bottom);
    result.bottomRight = add(manager, window, "bottom-right", manager.model().groupForTab(result.topRight));
    manager.moveTab(result.bottomRight, manager.model().groupForTab(result.topRight), Placement::Bottom);
    return result;
}
QPair<TabId, TabId> wrapPair(const Grid& grid, Placement direction)
{
    if (direction == Placement::Left) return {grid.topLeft, grid.topRight};
    if (direction == Placement::Top) return {grid.topLeft, grid.bottomLeft};
    if (direction == Placement::Right) return {grid.bottomRight, grid.bottomLeft};
    return {grid.bottomRight, grid.topRight};
}
QString directionName(Placement direction)
{
    if (direction == Placement::Left) return "left";
    if (direction == Placement::Right) return "right";
    return direction == Placement::Top ? "up" : "down";
}
QWidget* frameFor(QWidget* content)
{
    for (auto* widget = content; widget; widget = widget->parentWidget())
        if (widget->objectName() == QStringLiteral("PaneHost")) return widget;
    return nullptr;
}
void settle()
{
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}
}

class MovementTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); }
    void cleanup() { settle(); }
    void reorderingWrapsWithinItsOwnSequence_data();
    void reorderingWrapsWithinItsOwnSequence();
    void paneTransfersToAdjacentTabKeepBarsAndAdoptTargetPolicy_data();
    void paneTransfersToAdjacentTabKeepBarsAndAdoptTargetPolicy();
    void directionalGroupTransfersWrapByVisibleGeometry_data();
    void directionalGroupTransfersWrapByVisibleGeometry();
    void paneSwapAndFocusWrapWithinCurrentTab_data();
    void paneSwapAndFocusWrapWithinCurrentTab();
    void groupFocusWrapsWithoutEnteringHiddenRail_data();
    void groupFocusWrapsWithoutEnteringHiddenRail();
    void existingWindowMovementIncludesBothKindsAndWraps_data();
    void existingWindowMovementIncludesBothKindsAndWraps();
    void emptyWindowReceivesSamePaneAndSourceSharing();
    void commandParametersAndInvalidCallsAreAtomic();
    void tLayoutNavigationRetainsOrthogonalAnchor_data();
    void tLayoutNavigationRetainsOrthogonalAnchor();
    void directionalTiesFollowStableModelOrder();
    void ratioCommandUpdatesExistingSplitterWithoutRebuilding_data();
    void ratioCommandUpdatesExistingSplitterWithoutRebuilding();
    void ratioCommandUpdatesOnlyTheOwningSplitter_data();
    void ratioCommandUpdatesOnlyTheOwningSplitter();
    void splitterGapSelectsNearestProjection_data();
    void splitterGapSelectsNearestProjection();
};

void MovementTest::reorderingWrapsWithinItsOwnSequence_data()
{
    QTest::addColumn<int>("location");
    QTest::addColumn<bool>("forward");
    for (const auto location : {GroupLocation::Central, GroupLocation::Pinned, GroupLocation::AutoHide})
        for (const bool forward : {false, true})
            QTest::newRow(qPrintable(QStringLiteral("location-%1-forward-%2").arg(int(location)).arg(forward))) << int(location) << forward;
}
void MovementTest::reorderingWrapsWithinItsOwnSequence()
{
    QFETCH(int, location);
    QFETCH(bool, forward);
    WindowManager manager;
    const auto window = manager.createWindow();
    const auto a = add(manager, window, "a");
    const auto b = add(manager, window, "b");
    const auto c = add(manager, window, "c");
    if (location != int(GroupLocation::Central)) {
        QVERIFY(manager.moveTabToEdge(a, Edge::Left, location == int(GroupLocation::Pinned)));
        const auto group = manager.model().groupForTab(a);
        QVERIFY(manager.moveTab(b, group));
        QVERIFY(manager.moveTab(c, group));
    }
    const auto source = forward ? c : a;
    const auto pane = manager.model().tab(source)->activePane;
    const auto content = manager.paneWidget(pane);
    const auto group = manager.model().groupForTab(source);
    QVERIFY(manager.activateTab(source));
    const auto command = forward ? "workspace.reorderTab.next" : "workspace.reorderTab.previous";
    QVERIFY(manager.execute(command, {}, window));
    const auto expected = forward ? QVector<TabId>{c, a, b} : QVector<TabId>{b, c, a};
    QCOMPARE(manager.model().group(group)->tabs, expected);
    QCOMPARE(manager.model().tab(source)->activePane, pane);
    QCOMPARE(manager.paneWidget(pane), content);
    QVERIFY(manager.model().validate());
}

void MovementTest::paneTransfersToAdjacentTabKeepBarsAndAdoptTargetPolicy_data()
{
    QTest::addColumn<bool>("forward");
    QTest::newRow("left-wrap") << false;
    QTest::newRow("right-wrap") << true;
}
void MovementTest::paneTransfersToAdjacentTabKeepBarsAndAdoptTargetPolicy()
{
    QFETCH(bool, forward);
    WindowManager manager;
    const auto window = manager.createWindow();
    const auto a = add(manager, window, "a");
    const auto b = add(manager, window, "b");
    const auto c = add(manager, window, "c");
    const auto source = forward ? c : a;
    const auto target = forward ? a : c;
    const auto pane = manager.model().tab(source)->activePane;
    QPointer<QWidget> content = manager.paneWidget(pane);
    const auto retained = manager.splitPane(source, pane, new QLabel("retained"), Placement::Right);
    QPointer<QLabel> top = new QLabel("pane-top");
    QPointer<QLabel> bottom = new QLabel("pane-bottom");
    QPointer<QLabel> tabTop = new QLabel("source-tab-top");
    QVERIFY(manager.setPaneTopBar(pane, top));
    QVERIFY(manager.setPaneBottomBar(pane, bottom));
    QVERIFY(manager.setTabTopBar(source, tabTop));
    QVERIFY(manager.setTabTopBarSharing(target, PaneBarSharing::PerPane));
    QVERIFY(manager.setTabBottomBarSharing(target, PaneBarSharing::Shared));
    manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    QVERIFY(manager.activateTab(source));
    QVERIFY(manager.focusPane(source, pane));
    QVERIFY(manager.execute(forward ? "pane.moveToTab.next" : "pane.moveToTab.previous", {}, window));
    settle();
    QCOMPARE(manager.model().tabForPane(pane), target);
    QCOMPARE(manager.model().panes(source), QVector<PaneId>{retained});
    QCOMPARE(manager.model().tab(target)->activePane, pane);
    QCOMPARE(manager.paneWidget(pane), content.data());
    QCOMPARE(manager.tabOptions(target).topBarSharing, PaneBarSharing::PerPane);
    QCOMPARE(manager.tabOptions(target).bottomBarSharing, PaneBarSharing::Shared);
    QVERIFY(top && bottom && tabTop);
    QCOMPARE(top->parentWidget(), frameFor(content));
    QVERIFY(top->isVisible());
    QVERIFY(bottom->isVisible());
    QVERIFY(tabTop->parentWidget() != top->parentWidget());
    QVERIFY(!tabTop->isVisible());
    QVERIFY(manager.activateTab(source));
    QVERIFY(tabTop->isVisible());
    QCOMPARE(manager.model().group(manager.model().groupForTab(source))->tabs, (QVector<TabId>{a, b, c}));
}

void MovementTest::directionalGroupTransfersWrapByVisibleGeometry_data()
{
    QTest::addColumn<int>("direction");
    QTest::addColumn<bool>("paneTransfer");
    for (const auto direction : {Placement::Left, Placement::Right, Placement::Top, Placement::Bottom})
        for (const bool pane : {false, true})
            QTest::newRow(qPrintable(directionName(direction) + (pane ? "-pane" : "-tab"))) << int(direction) << pane;
}
void MovementTest::directionalGroupTransfersWrapByVisibleGeometry()
{
    QFETCH(int, direction);
    QFETCH(bool, paneTransfer);
    WindowManager manager;
    const auto window = manager.createWindow();
    const auto grid = createGrid(manager, window);
    const auto [source, target] = wrapPair(grid, static_cast<Placement>(direction));
    const auto sourceGroup = manager.model().groupForTab(source);
    const auto targetGroup = manager.model().groupForTab(target);
    const auto keepTab = add(manager, window, "keep-source-group", sourceGroup);
    const auto pane = manager.model().tab(source)->activePane;
    const auto content = manager.paneWidget(pane);
    PaneId keptPane;
    if (paneTransfer) keptPane = manager.splitPane(source, pane, new QLabel("kept-pane"), Placement::Right);
    const auto hidden = add(manager, window, "hidden");
    QVERIFY(manager.moveTabToEdge(hidden, Edge::Left));
    const auto hiddenGroup = manager.model().groupForTab(hidden);
    manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    settle();
    QVERIFY(manager.activateTab(source));
    QVERIFY(manager.focusPane(source, pane));
    QVERIFY(manager.execute((paneTransfer ? "pane.moveToGroup." : "workspace.moveTabToGroup.") + directionName(static_cast<Placement>(direction)), {}, window));
    if (paneTransfer) {
        QCOMPARE(manager.model().tabForPane(pane), target);
        QCOMPARE(manager.model().panes(source), QVector<PaneId>{keptPane});
    } else {
        QCOMPARE(manager.model().groupForTab(source), targetGroup);
        QCOMPARE(manager.model().group(sourceGroup)->tabs, QVector<TabId>{keepTab});
    }
    QCOMPARE(manager.paneWidget(pane), content);
    QCOMPARE(manager.model().groupForTab(hidden), hiddenGroup);
    QCOMPARE(manager.model().group(manager.model().groupForTab(hidden))->location, GroupLocation::AutoHide);
    QVERIFY(manager.model().validate());
}

void MovementTest::paneSwapAndFocusWrapWithinCurrentTab_data()
{
    QTest::addColumn<int>("direction");
    for (const auto direction : {Placement::Left, Placement::Right, Placement::Top, Placement::Bottom})
        QTest::newRow(qPrintable(directionName(direction))) << int(direction);
}
void MovementTest::paneSwapAndFocusWrapWithinCurrentTab()
{
    QFETCH(int, direction);
    const auto placement = static_cast<Placement>(direction);
    WindowManager manager;
    const auto window = manager.createWindow();
    const auto tab = add(manager, window, "pane-grid");
    const auto tl = manager.model().tab(tab)->activePane;
    const auto tr = manager.splitPane(tab, tl, new QLabel("tr"), Placement::Right);
    const auto bl = manager.splitPane(tab, tl, new QLabel("bl"), Placement::Bottom);
    const auto br = manager.splitPane(tab, tr, new QLabel("br"), Placement::Bottom);
    const auto [source, target] = wrapPair({tl, tr, bl, br}, placement);
    const auto sourceContent = manager.paneWidget(source);
    const auto targetContent = manager.paneWidget(target);
    QPointer<QLabel> sourceBar = new QLabel("source-bar");
    QPointer<QLabel> targetBar = new QLabel("target-bar");
    QVERIFY(manager.setPaneTopBar(source, sourceBar));
    QVERIFY(manager.setPaneTopBar(target, targetBar));
    QVERIFY(manager.setTabTopBarSharing(tab, PaneBarSharing::PerPane));
    manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    settle();
    const auto sourceRect = frameFor(sourceContent)->geometry();
    const auto sourceGlobal = frameFor(sourceContent)->mapToGlobal(QPoint());
    const auto targetGlobal = frameFor(targetContent)->mapToGlobal(QPoint());
    QVERIFY(manager.focusPane(tab, source));
    QVERIFY(manager.execute("pane.focus." + directionName(placement), {}, window));
    QCOMPARE(manager.model().tab(tab)->activePane, target);
    QVERIFY(manager.focusPane(tab, source));
    QVERIFY(manager.execute("pane.swap." + directionName(placement), {}, window));
    settle();
    QCOMPARE(manager.model().tab(tab)->activePane, source);
    QCOMPARE(frameFor(sourceContent)->mapToGlobal(QPoint()), targetGlobal);
    QCOMPARE(frameFor(targetContent)->mapToGlobal(QPoint()), sourceGlobal);
    QCOMPARE(frameFor(targetContent)->geometry().size(), sourceRect.size());
    QCOMPARE(sourceBar->parentWidget(), frameFor(sourceContent));
    QCOMPARE(targetBar->parentWidget(), frameFor(targetContent));
    QCOMPARE(manager.paneWidget(source), sourceContent);
    QCOMPARE(manager.paneWidget(target), targetContent);
    QVERIFY(manager.model().validate());
}

void MovementTest::groupFocusWrapsWithoutEnteringHiddenRail_data()
{
    QTest::addColumn<int>("direction");
    for (const auto direction : {Placement::Left, Placement::Right, Placement::Top, Placement::Bottom})
        QTest::newRow(qPrintable(directionName(direction))) << int(direction);
}
void MovementTest::groupFocusWrapsWithoutEnteringHiddenRail()
{
    QFETCH(int, direction);
    WindowManager manager;
    const auto window = manager.createWindow();
    const auto grid = createGrid(manager, window);
    const auto [source, target] = wrapPair(grid, static_cast<Placement>(direction));
    const auto hidden = add(manager, window, "hidden");
    QVERIFY(manager.moveTabToEdge(hidden, Edge::Left));
    manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    settle();
    QVERIFY(manager.activateTab(source));
    QVERIFY(manager.execute("workspace.focusGroup." + directionName(static_cast<Placement>(direction)), {}, window));
    QCOMPARE(manager.context(window).tab, target);
    QVERIFY(manager.context(window).tab != hidden);
}

void MovementTest::existingWindowMovementIncludesBothKindsAndWraps_data()
{
    QTest::addColumn<bool>("paneTransfer");
    QTest::addColumn<bool>("floating");
    QTest::addColumn<bool>("forward");
    for (const bool pane : {false, true}) for (const bool floating : {false, true}) for (const bool forward : {false, true})
        QTest::newRow(qPrintable(QStringLiteral("pane-%1-floating-%2-forward-%3").arg(pane).arg(floating).arg(forward))) << pane << floating << forward;
}
void MovementTest::existingWindowMovementIncludesBothKindsAndWraps()
{
    QFETCH(bool, paneTransfer);
    QFETCH(bool, floating);
    QFETCH(bool, forward);
    WindowManager manager;
    const auto first = manager.createWindow();
    const auto middle = manager.createWindow();
    const auto last = manager.createWindow(floating ? WindowKind::Floating : WindowKind::Workbench, {}, first);
    const auto sourceWindow = forward ? last : first;
    const auto targetWindow = forward ? first : last;
    add(manager, middle, "middle");
    const auto targetTab = add(manager, targetWindow, "target");
    const auto source = add(manager, sourceWindow, "source");
    const auto pane = manager.model().tab(source)->activePane;
    QPointer<QWidget> content = manager.paneWidget(pane);
    QPointer<QLabel> bar = new QLabel("carried-pane-bar");
    QVERIFY(manager.setPaneTopBar(pane, bar));
    if (paneTransfer) QVERIFY(!manager.splitPane(source, pane, new QLabel("retained"), Placement::Right).isNull());
    manager.showWindow(sourceWindow);
    QVERIFY(manager.activateTab(source));
    QVERIFY(manager.focusPane(source, pane));
    const auto command = QString(paneTransfer ? "pane.moveToWindow." : "workspace.moveTabToWindow.") + (forward ? "next" : "previous");
    QVERIFY(manager.execute(command, {}, sourceWindow));
    settle();
    const auto destination = manager.model().tabForPane(pane);
    QCOMPARE(manager.model().group(manager.model().groupForTab(destination))->windowId, targetWindow);
    QCOMPARE(destination, paneTransfer ? targetTab : source);
    QCOMPARE(manager.paneWidget(pane), content.data());
    QVERIFY(bar);
    QCOMPARE(bar->window(), manager.windowWidget(targetWindow));
    QCOMPARE(manager.model().tab(destination)->activePane, pane);
    if (floating && forward && !paneTransfer) QVERIFY(!manager.model().window(sourceWindow));
    QVERIFY(manager.model().validate());
}

void MovementTest::emptyWindowReceivesSamePaneAndSourceSharing()
{
    WindowManager manager;
    const auto root = manager.createWindow();
    const auto sourceWindow = manager.createWindow(WindowKind::Floating, {}, root);
    const auto source = add(manager, sourceWindow, "only-pane");
    const auto pane = manager.model().tab(source)->activePane;
    QPointer<QWidget> content = manager.paneWidget(pane);
    QPointer<QLabel> paneBar = new QLabel("pane-bar");
    QVERIFY(manager.setPaneTopBar(pane, paneBar));
    QVERIFY(manager.setTabTopBarSharing(source, PaneBarSharing::PerPane));
    QVERIFY(manager.movePaneToWindow(source, pane, root));
    settle();
    const auto destination = manager.model().tabForPane(pane);
    QVERIFY(destination != source);
    QVERIFY(!manager.model().window(sourceWindow));
    QVERIFY(!manager.model().tab(source));
    QCOMPARE(manager.paneWidget(pane), content.data());
    QCOMPARE(manager.tabOptions(destination).topBarSharing, PaneBarSharing::PerPane);
    QCOMPARE(paneBar->parentWidget(), frameFor(content));
    QCOMPARE(paneBar->window(), manager.windowWidget(root));
}

void MovementTest::commandParametersAndInvalidCallsAreAtomic()
{
    WindowManager manager;
    const auto root = manager.createWindow();
    const auto source = add(manager, root, "source");
    const auto pane = manager.model().tab(source)->activePane;
    const auto retained = manager.splitPane(source, pane, new QLabel("retained"), Placement::Right);
    const auto targetWindow = manager.createWindow(WindowKind::Floating, {}, root);
    const auto target = add(manager, targetWindow, "target");
    const auto targetPane = manager.model().tab(target)->activePane;
    int closeCalls = 0;
    manager.setCloseGuard([&](const QVector<TabId>&) { ++closeCalls; return false; });
    QVERIFY(manager.activateTab(source));
    QVERIFY(manager.focusPane(source, pane));
    QVERIFY(manager.execute("pane.moveToTab", {{"targetTab", target}, {"targetPane", targetPane}, {"placement", int(Placement::Bottom)}}, root));
    QCOMPARE(manager.model().tabForPane(pane), target);
    QCOMPARE(manager.model().panes(source), QVector<PaneId>{retained});
    QCOMPARE(closeCalls, 0);
    const auto snapshot = manager.saveLayout();
    QVERIFY(!manager.movePane(source, retained, source));
    QVERIFY(!manager.movePaneToWindow(source, retained, root));
    QVERIFY(!manager.moveTabToAdjacentGroup(source, Placement::Center));
    QVERIFY(!manager.swapPanes(target, pane, retained));
    QVERIFY(!manager.movePane(source, retained, target, {}, static_cast<Placement>(99)));
    QCOMPARE(manager.saveLayout(), snapshot);
    QVERIFY(manager.execute("pane.moveToWindow", {{"tab", source}, {"pane", retained}, {"targetWindow", targetWindow}}, root));
    QCOMPARE(manager.model().tabForPane(retained), target);
    QVERIFY(!manager.model().tab(source));
    QCOMPARE(closeCalls, 0);
    QVERIFY(manager.execute("workspace.moveTabToWindow", {{"tab", target}, {"targetWindow", root}}, targetWindow));
    QCOMPARE(manager.model().group(manager.model().groupForTab(target))->windowId, root);
    QVERIFY(manager.model().validate());
}

void MovementTest::tLayoutNavigationRetainsOrthogonalAnchor_data()
{
    QTest::addColumn<bool>("panes");
    QTest::newRow("group-layer") << false;
    QTest::newRow("pane-layer") << true;
}
void MovementTest::tLayoutNavigationRetainsOrthogonalAnchor()
{
    QFETCH(bool, panes);
    WindowManager manager;
    const auto window = manager.createWindow();
    const auto firstTab = add(manager, window, "top-left");
    const auto topPane = manager.model().tab(firstTab)->activePane;
    PaneId rightPane, bottomPane;
    TabId rightTab = firstTab, bottomTab = firstTab;
    QUuid verticalSplit;
    if (panes) {
        rightPane = manager.splitPane(firstTab, topPane, new QLabel("full-height-right"), Placement::Right);
        bottomPane = manager.splitPane(firstTab, topPane, new QLabel("bottom-left"), Placement::Bottom);
        verticalSplit = manager.model().tab(firstTab)->panes->first->id;
    } else {
        rightTab = add(manager, window, "full-height-right");
        QVERIFY(manager.moveTab(rightTab, manager.model().groupForTab(firstTab), Placement::Right));
        bottomTab = add(manager, window, "bottom-left", manager.model().groupForTab(firstTab));
        QVERIFY(manager.moveTab(bottomTab, manager.model().groupForTab(firstTab), Placement::Bottom));
        rightPane = manager.model().tab(rightTab)->activePane;
        bottomPane = manager.model().tab(bottomTab)->activePane;
        verticalSplit = manager.model().window(window)->central->first->id;
    }
    QVERIFY(manager.execute("layout.setRatio", {{"node", verticalSplit}, {"ratio", .3}}, window));
    manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    settle();
    auto focus = [&](TabId tab, PaneId pane) { QVERIFY(manager.activateTab(tab)); QVERIFY(manager.focusPane(tab, pane)); };
    auto active = [&] { return panes ? manager.context(window).pane : manager.context(window).tab; };
    const auto prefix = panes ? QStringLiteral("pane.focus.") : QStringLiteral("workspace.focusGroup.");
    focus(firstTab, topPane);
    QVERIFY(manager.execute(prefix + "right", {}, window));
    QCOMPARE(active(), panes ? rightPane : rightTab);
    QVERIFY(manager.execute(prefix + "left", {}, window));
    QCOMPARE(active(), panes ? topPane : firstTab);
    // The same full-height target must return to the originating row.
    focus(bottomTab, bottomPane);
    QVERIFY(manager.execute(prefix + "right", {}, window));
    QVERIFY(manager.execute(prefix + "left", {}, window));
    QCOMPARE(active(), panes ? bottomPane : bottomTab);
    // Axis change starts a fresh channel at the newly focused rectangle.
    focus(firstTab, topPane);
    QVERIFY(manager.execute(prefix + "down", {}, window));
    QCOMPARE(active(), panes ? bottomPane : bottomTab);
    QVERIFY(manager.execute(prefix + "right", {}, window));
    QVERIFY(manager.execute(prefix + "left", {}, window));
    QCOMPARE(active(), panes ? bottomPane : bottomTab);
    // A mouse focus on the tall column discards the old top-row anchor.
    focus(firstTab, topPane);
    QVERIFY(manager.execute(prefix + "right", {}, window));
    QFocusEvent mouseFocus(QEvent::FocusIn, Qt::MouseFocusReason);
    QApplication::sendEvent(manager.paneWidget(rightPane), &mouseFocus);
    QVERIFY(manager.execute(prefix + "left", {}, window));
    QCOMPARE(active(), panes ? bottomPane : bottomTab);
}

void MovementTest::directionalTiesFollowStableModelOrder()
{
    // Use an exact geometric tie so random UUID/QHash ordering cannot choose
    // the target. The layout's top group precedes its bottom group.
    for (int attempt = 0; attempt < 8; ++attempt) {
        WindowManager manager;
        const auto window = manager.createWindow();
        const auto top = add(manager, window, "top");
        const auto right = add(manager, window, "right");
        QVERIFY(manager.moveTab(right, manager.model().groupForTab(top), Placement::Right));
        const auto bottom = add(manager, window, "bottom", manager.model().groupForTab(top));
        QVERIFY(manager.moveTab(bottom, manager.model().groupForTab(top), Placement::Bottom));
        const auto pane = manager.model().tab(right)->activePane;
        QVERIFY(!manager.splitPane(right, pane, new QLabel("kept"), Placement::Right).isNull());
        manager.showWindow(window);
        QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
        settle();
        const auto topGroup = manager.model().groupForTab(top);
        const auto bottomGroup = manager.model().groupForTab(bottom);
        const auto rightGroup = manager.model().groupForTab(right);
        auto position = [&](GroupId id, QRect rect) {
            for (auto* widget : manager.windowWidget(window)->findChildren<QWidget*>())
                if (widget->property("groupId").toUuid() == id)
                    widget->setGeometry(QRect(widget->parentWidget()->mapFromGlobal(rect.topLeft()), rect.size()));
        };
        position(topGroup, QRect(100, 100, 200, 200));
        position(bottomGroup, QRect(100, 300, 200, 200));
        position(rightGroup, QRect(400, 200, 200, 200));
        auto* surface = static_cast<WorkspaceWindow*>(manager.windowWidget(window))->surface();
        QCOMPARE(surface->groupRects().value(topGroup), QRect(100, 100, 200, 200));
        QCOMPARE(surface->groupRects().value(bottomGroup), QRect(100, 300, 200, 200));
        QVERIFY(manager.movePaneToAdjacentGroup(right, pane, Placement::Left));
        QCOMPARE(manager.model().tabForPane(pane), top);
        QVERIFY(manager.model().validate());
    }
}

void MovementTest::ratioCommandUpdatesExistingSplitterWithoutRebuilding_data()
{
    QTest::addColumn<bool>("panes");
    QTest::newRow("group-splitter") << false;
    QTest::newRow("pane-splitter") << true;
}
void MovementTest::ratioCommandUpdatesExistingSplitterWithoutRebuilding()
{
    QFETCH(bool, panes);
    WindowManager manager;
    const auto window = manager.createWindow();
    const auto tab = add(manager, window, "first");
    const auto first = manager.model().tab(tab)->activePane;
    PaneId second;
    if (panes) second = manager.splitPane(tab, first, new QLabel("second"), Placement::Right);
    else {
        const auto secondTab = add(manager, window, "second");
        second = manager.model().tab(secondTab)->activePane;
        QVERIFY(manager.moveTab(secondTab, manager.model().groupForTab(tab), Placement::Right));
    }
    const auto node = panes ? manager.model().tab(tab)->panes->id : manager.model().window(window)->central->id;
    const auto firstContent = manager.paneWidget(first);
    const auto secondContent = manager.paneWidget(second);
    manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    settle();
    QPointer<QSplitter> splitter;
    for (auto* candidate : manager.windowWidget(window)->findChildren<QSplitter*>())
        if (candidate->property("splitNodeId").toUuid() == node) splitter = candidate;
    QVERIFY(splitter);
    const auto firstFrame = frameFor(firstContent);
    const auto secondFrame = frameFor(secondContent);
    for (const double ratio : {.3, .73}) {
        QVERIFY(manager.execute("layout.setRatio", {{"node", node}, {"ratio", ratio}}, window));
        QVERIFY(splitter);
        const auto sizes = splitter->sizes();
        const int available = sizes[0] + sizes[1];
        QVERIFY(available > 0);
        QVERIFY(std::abs(double(sizes[0]) / available - ratio) <= 2. / available);
        QCOMPARE(panes ? manager.model().tab(tab)->panes->ratio : manager.model().window(window)->central->ratio, ratio);
        QCOMPARE(frameFor(firstContent), firstFrame);
        QCOMPARE(frameFor(secondContent), secondFrame);
        QCOMPARE(manager.paneWidget(first), firstContent);
        QCOMPARE(manager.paneWidget(second), secondContent);
    }
    const auto snapshot = manager.saveLayout();
    const auto sizes = splitter->sizes();
    QVERIFY(!manager.execute("layout.setRatio", {{"node", node}, {"ratio", 1.2}}, window));
    QCOMPARE(splitter->sizes(), sizes);
    QCOMPARE(manager.saveLayout(), snapshot);
}

void MovementTest::ratioCommandUpdatesOnlyTheOwningSplitter_data()
{
    QTest::addColumn<bool>("central");
    QTest::newRow("tab-node") << false;
    QTest::newRow("central-node") << true;
}
void MovementTest::ratioCommandUpdatesOnlyTheOwningSplitter()
{
    QFETCH(bool, central);
    WindowManager manager;
    const auto window = manager.createWindow();
    const auto left = add(manager, window, "left");
    const auto right = add(manager, window, "right");
    QVERIFY(manager.moveTab(right, manager.model().groupForTab(left), Placement::Right));
    for (const auto tab : {left, right})
        QVERIFY(!manager.splitPane(tab, manager.model().tab(tab)->activePane, new QLabel("split"), Placement::Bottom).isNull());
    manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    settle();
    const auto splitterFor = [&](QUuid node) {
        QPointer<QSplitter> result;
        for (auto* candidate : manager.windowWidget(window)->findChildren<QSplitter*>())
            if (candidate->property("splitNodeId").toUuid() == node) result = candidate;
        return result;
    };
    const auto leftNode = manager.model().tab(left)->panes->id;
    const auto rightNode = manager.model().tab(right)->panes->id;
    const auto centralNode = manager.model().window(window)->central->id;
    const auto leftSplitter = splitterFor(leftNode);
    const auto rightSplitter = splitterFor(rightNode);
    const auto centralSplitter = splitterFor(centralNode);
    QVERIFY(leftSplitter && rightSplitter && centralSplitter);
    QVERIFY(leftSplitter->isVisible() && rightSplitter->isVisible());
    const auto target = central ? centralSplitter : leftSplitter;
    const auto node = central ? centralNode : leftNode;
    QVector<QPointer<QSplitter>> others{rightSplitter};
    others << (central ? leftSplitter : centralSplitter);
    QVector<QList<int>> before;
    for (const auto& other : others) before << other->sizes();
    const auto rightRatio = manager.model().tab(right)->panes->ratio;
    for (const double ratio : {.3, .72}) {
        QVERIFY(manager.execute("layout.setRatio", {{"node", node}, {"ratio", ratio}}, window));
        QVERIFY(target);
        const auto sizes = target->sizes();
        const int available = sizes.value(0) + sizes.value(1);
        QVERIFY(available > 0);
        QVERIFY(std::abs(double(sizes.value(0)) / available - ratio) <= 2. / available);
        // The command adjusts the existing widget instead of rebuilding.
        QCOMPARE(splitterFor(node), target);
        for (int i = 0; i < others.size(); ++i) {
            QVERIFY(others[i]);
            QCOMPARE(others[i]->sizes(), before[i]);
        }
        QCOMPARE(manager.model().tab(right)->panes->ratio, rightRatio);
    }
}

void MovementTest::splitterGapSelectsNearestProjection_data()
{
    QTest::addColumn<bool>("panes");
    QTest::addColumn<bool>("bottom");
    for (const bool panes : {false, true}) for (const bool bottom : {false, true})
        QTest::newRow(qPrintable(QStringLiteral("panes-%1-nearest-bottom-%2").arg(panes).arg(bottom))) << panes << bottom;
}
void MovementTest::splitterGapSelectsNearestProjection()
{
    QFETCH(bool, panes);
    QFETCH(bool, bottom);
    WindowManager manager;
    const auto window = manager.createWindow();
    const auto topTab = add(manager, window, "top");
    const auto topPane = manager.model().tab(topTab)->activePane;
    TabId rightTab = topTab, bottomTab = topTab;
    PaneId rightPane, bottomPane;
    if (panes) {
        rightPane = manager.splitPane(topTab, topPane, new QLabel("right"), Placement::Right);
        bottomPane = manager.splitPane(topTab, topPane, new QLabel("bottom"), Placement::Bottom);
    } else {
        rightTab = add(manager, window, "right");
        QVERIFY(manager.moveTab(rightTab, manager.model().groupForTab(topTab), Placement::Right));
        bottomTab = add(manager, window, "bottom", manager.model().groupForTab(topTab));
        QVERIFY(manager.moveTab(bottomTab, manager.model().groupForTab(topTab), Placement::Bottom));
        rightPane = manager.model().tab(rightTab)->activePane;
        bottomPane = manager.model().tab(bottomTab)->activePane;
    }
    manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    settle();
    auto groupWidget = [&](TabId tab) -> QWidget* {
        const auto group = manager.model().groupForTab(tab);
        for (auto* widget : manager.windowWidget(window)->findChildren<QWidget*>())
            if (widget->property("groupId").toUuid() == group) return widget;
        return nullptr;
    };
    auto position = [](QWidget* widget, QRect rect) {
        QVERIFY(widget);
        widget->setGeometry(QRect(widget->parentWidget()->mapFromGlobal(rect.topLeft()), rect.size()));
    };
    position(panes ? frameFor(manager.paneWidget(topPane)) : groupWidget(topTab), QRect(100, 100, 200, 200));
    position(panes ? frameFor(manager.paneWidget(bottomPane)) : groupWidget(bottomTab), QRect(100, 304, 200, 200));
    position(panes ? frameFor(manager.paneWidget(rightPane)) : groupWidget(rightTab), QRect(400, bottom ? 204 : 202, 200, 200));
    QVERIFY(manager.activateTab(rightTab));
    QVERIFY(manager.focusPane(rightTab, rightPane));
    QVERIFY(manager.execute(panes ? "pane.focus.left" : "workspace.focusGroup.left", {}, window));
    QCOMPARE(panes ? manager.context(window).pane : manager.context(window).tab,
        panes ? (bottom ? bottomPane : topPane) : (bottom ? bottomTab : topTab));
}

QTEST_MAIN(MovementTest)
#include "movement_test.moc"
