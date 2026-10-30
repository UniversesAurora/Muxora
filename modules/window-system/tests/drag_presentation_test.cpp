// SPDX-License-Identifier: MIT
#include "chrome/workspace_window.hpp"
#include "docking/dock_surface.hpp"
#include "docking/edge_strip.hpp"
#include "docking/dock_guides.hpp"
#include "docking/tab_strip.hpp"
#include "docking/tab_bar.hpp"
#include "platform/windows/window_frame.hpp"
#include "authored_mouse_input.hpp"
#include <window_system/window_manager.hpp>
#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPointer>
#include <QScrollBar>
#include <QScreen>
#include <QStackedWidget>
#include <QTest>
#include <QDebug>

using namespace window_system;
namespace {
// Over a strip the carrier window is its labels alone. Since the one-piece
// surface (surface elevation design) their rounded outlines cut the
// corners, so the mask lies within the labels and reaches each one.
void verifyLabelMask(const QRegion& mask, const QVector<QRect>& labels)
{
    QRegion area;
    for (const auto& label : labels) { area += label; QVERIFY(mask.contains(label.center())); }
    QVERIFY((mask - area).isEmpty());
    QCOMPARE(mask.boundingRect(), area.boundingRect());
}
class CarrierCommitObserver final : public QObject {
public:
    explicit CarrierCommitObserver(WorkspaceWindow* carrier) : carrier_(carrier)
    {
        carrier->installEventFilter(this);
        chrome_ = carrier->findChild<QWidget*>("WindowHeader");
        if (chrome_) chrome_->installEventFilter(this);
    }
    bool hidden = false;
    bool compactAtHide = false;
    bool chromeRevealed = false;
    NativeTransitionConfiguration removal;
protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == chrome_ && event->type() == QEvent::Show) chromeRevealed = true;
        if (watched == carrier_ && event->type() == QEvent::Hide) {
            hidden = true;
            compactAtHide = carrier_->dragPresentation() == WorkspaceWindow::DragPresentation::Tab;
            removal = nativeTransitionConfiguration(carrier_);
        }
        return false;
    }
private:
    QPointer<WorkspaceWindow> carrier_;
    QPointer<QWidget> chrome_;
};

void saveRequestedScreenshot(QWidget* widget, const QString& name)
{
    const auto directory = qEnvironmentVariable("WINDOW_SYSTEM_SCREENSHOT_DIR");
    if (directory.isEmpty()) return;
    QVERIFY(QDir().mkpath(directory));
    QVERIFY(widget->grab().save(QDir(directory).filePath(name)));
}
void moveMouse(QWidget* capture, QPoint global)
{
    test::authoredMouseInput().takePriority();
    QMouseEvent event(QEvent::MouseMove, capture->mapFromGlobal(global), global,
        Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(capture, &event);
    test::authoredMouseInput().takePriority();
    QApplication::processEvents();
}
void showFixture(WorkspaceWindow* window, QPoint position)
{
    window->setWindowFlag(Qt::WindowStaysOnTopHint, true);
    window->move(position); window->show(); window->raise();
}
GroupTabs* groupView(WorkspaceWindow* window, GroupId id)
{
    for (auto* widget : window->findChildren<QWidget*>())
        if (auto* group = dynamic_cast<GroupTabs*>(widget); group && group->isVisible() && group->property("groupId").toUuid() == id) return group;
    return nullptr;
}
WorkspaceWindow* beginDrag(WindowManager& manager, WindowId source, TabId tab)
{
    auto* window = static_cast<WorkspaceWindow*>(manager.windowWidget(source));
    auto* group = groupView(window, manager.model().groupForTab(tab));
    if (!group) return nullptr;
    auto* bar = group->tabBar();
    const int index = manager.model().group(manager.model().groupForTab(tab))->tabs.indexOf(tab);
    // This fixture requests an already detached drag; it does not simulate
    // a press followed by threshold crossing inside the source strip.
    const auto global = bar->mapToGlobal(bar->tabRect(index).center()) + QPoint(0, 35);
    const auto callback = bar->drag;
    callback(index, DragStart{global, global, {}});
    return dynamic_cast<WorkspaceWindow*>(QWidget::mouseGrabber());
}
} // namespace
class DragPresentationTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); }
    void cleanup() { test::authoredMouseInput().finish(); }
    void gapDisplacesOnlyTheVisualTabs();
    void guidesAvoidVisibleStripsAndShortGroupHeaders_data();
    void guidesAvoidVisibleStripsAndShortGroupHeaders();
    void edgeGuidesStayInTheirOwnSideBand_data();
    void edgeGuidesStayInTheirOwnSideBand();
    void narrowGroupCrossStaysBesideItsWindowEdgePair_data();
    void narrowGroupCrossStaysBesideItsWindowEdgePair();
    void displacedGroupCrossAlignsWithItsEdgePair_data();
    void displacedGroupCrossAlignsWithItsEdgePair();
    void displacedCrossKeepsItsTargetAcrossNeighborAndGaps_data();
    void displacedCrossKeepsItsTargetAcrossNeighborAndGaps();
    void hiddenCrossDoesNotStealNeighboringContent_data();
    void hiddenCrossDoesNotStealNeighboringContent();
    void edgeGuidesAcceptDropsBesideStackedGroups_data();
    void edgeGuidesAcceptDropsBesideStackedGroups();
    void guidesTrackWindowGeometryWithoutMouseMovement_data();
    void guidesTrackWindowGeometryWithoutMouseMovement();
    void tabTargetKeepsCarrierAndLiveContent_data();
    void tabTargetKeepsCarrierAndLiveContent();
    void stickyRowPreviewKeepsTheLabelAgainstItsContent_data();
    void stickyRowPreviewKeepsTheLabelAgainstItsContent();
    void compactPreviewMaskFollowsTheDisplayedRows_data();
    void compactPreviewMaskFollowsTheDisplayedRows();
    void edgeTargetMirrorsTheLabelAndClearsOnCancel_data();
    void edgeTargetMirrorsTheLabelAndClearsOnCancel();
    void stationaryDragScrollsAndStops_data();
    void stationaryDragScrollsAndStops();
    void wholeWindowPresentationRestoresKeyboardFocus_data();
    void wholeWindowPresentationRestoresKeyboardFocus();
    void directSaveOmitsIntermediateCarrier_data();
    void directSaveOmitsIntermediateCarrier();
};
void DragPresentationTest::gapDisplacesOnlyTheVisualTabs()
{
    GroupTabs tabs(defaultTheme(), nullptr); QVector<TabId> ids;
    for (const auto* title : {"First", "Second", "Third"}) {
        ids << QUuid::createUuid(); tabs.addTab(new QLabel(title), {}, title, ids.back());
    }
    tabs.resize(600, 200); tabs.show(); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    const auto first = tabs.tabBar()->tabRect(0); const auto second = tabs.tabBar()->tabRect(1);
    const int current = tabs.currentIndex();
    // A carried tab opens a gap as wide as it will be here.
    const CarriedLabel carried{QStringLiteral("Carried tab"), false};
    const int width = tabs.tabBar()->carriedExtent(carried, false);
    const auto probe = tabs.tabBar()->mapToGlobal(QPoint(second.left() + 2, second.center().y()));
    QCOMPARE(tabs.insertionIndex(probe), 1);
    tabs.setInsertionPreview(1, {carried});
    QCOMPARE(tabs.tabBar()->displayTabRect(0), first);
    QCOMPARE(tabs.tabBar()->displayTabRect(1), second.translated(width, 0));
    QCOMPARE(tabs.count(), 3); QCOMPARE(tabs.currentIndex(), current);
    for (int i = 0; i < ids.size(); ++i) QCOMPARE(tabs.tabBar()->tabData(i).toUuid(), ids[i]);
    const auto gap = tabs.insertionPreviewRectGlobal(); QVERIFY(!gap.isEmpty()); QCOMPARE(gap.width(), width);
    // The decision does not move with the gap it opened.
    QCOMPARE(tabs.insertionIndex(probe), 1);
    tabs.setInsertionPreview(-1, {});
    QVERIFY(tabs.insertionPreviewRectGlobal().isEmpty()); QCOMPARE(tabs.tabBar()->displayTabRect(1), second);
}
void DragPresentationTest::guidesAvoidVisibleStripsAndShortGroupHeaders_data()
{
    QTest::addColumn<int>("height"); QTest::newRow("short") << 90; QTest::newRow("normal") << 350;
}
void DragPresentationTest::guidesAvoidVisibleStripsAndShortGroupHeaders()
{
    QFETCH(int, height);
    const QRect bounds(0, 0, 740, 450); const QRect group(0, 0, 120, height);
    const QRect header(0, 0, 740, 27);
    const auto guides = dockGuides(bounds, group, true, {{header, Edge::Top}});
    QCOMPARE(guides.size(), 13);
    for (const auto& guide : guides) {
        QVERIFY(!guide.rect.intersects(header));
        QVERIFY(bounds.contains(guide.rect));
        if (!guide.toEdge) {
            for (const auto& other : guides) if (other.toEdge) QVERIFY(!guide.rect.intersects(other.rect));
        }
    }
}
void DragPresentationTest::narrowGroupCrossStaysBesideItsWindowEdgePair_data()
{
    QTest::addColumn<int>("edge");
    for (const auto edge : {Edge::Left, Edge::Right, Edge::Top, Edge::Bottom})
        QTest::newRow(qPrintable(QString::number(int(edge)))) << int(edge);
}
void DragPresentationTest::narrowGroupCrossStaysBesideItsWindowEdgePair()
{
    QFETCH(int, edge); const auto direction = Edge(edge);
    const QRect bounds(100, 80, 800, 600);
    QRect group;
    switch (direction) {
    case Edge::Left: group = QRect(bounds.left(), bounds.top(), 100, bounds.height()); break;
    case Edge::Right: group = QRect(bounds.right() - 99, bounds.top(), 100, bounds.height()); break;
    case Edge::Top: group = QRect(bounds.left(), bounds.top(), bounds.width(), 100); break;
    case Edge::Bottom: group = QRect(bounds.left(), bounds.bottom() - 99, bounds.width(), 100); break;
    }
    const QVector<DockStrip> strips{{QRect(bounds.left(), bounds.top(), bounds.width(), 27), Edge::Top}};
    const auto guides = dockGuides(bounds, group, true, strips);
    QCOMPARE(guides.size(), 13);
    QRect cross, pair; QHash<Placement, QRect> cells;
    for (const auto& guide : guides) {
        QVERIFY(bounds.contains(guide.rect));
        for (const auto& strip : strips) QVERIFY(!guide.rect.intersects(strip.rect));
        if (!guide.toEdge) { cross = cross.united(guide.rect); cells.insert(guide.placement, guide.rect); }
        else if (guide.edge == direction) pair = pair.united(guide.rect);
    }
    QCOMPARE(cells.size(), 5); QVERIFY(!pair.isEmpty());
    for (const auto& guide : guides) if (guide.toEdge)
        for (const auto& cell : cells) QVERIFY(!cell.intersects(guide.rect));
    if (direction == Edge::Left) QCOMPARE(cross.left(), pair.right() + 3);
    if (direction == Edge::Right) QCOMPARE(cross.right(), pair.left() - 3);
    if (direction == Edge::Top) QCOMPARE(cross.top(), pair.bottom() + 3);
    if (direction == Edge::Bottom) QCOMPARE(cross.bottom(), pair.top() - 3);
    if (direction == Edge::Left || direction == Edge::Right) QCOMPARE(cross.center().y(), pair.center().y());
    else QCOMPARE(cross.center().x(), pair.center().x());
    const auto center = cells.value(Placement::Center);
    const int step = center.width() + 4;
    QCOMPARE(cells.value(Placement::Left), center.translated(-step, 0));
    QCOMPARE(cells.value(Placement::Right), center.translated(step, 0));
    QCOMPARE(cells.value(Placement::Top), center.translated(0, -step));
    QCOMPARE(cells.value(Placement::Bottom), center.translated(0, step));
    QCOMPARE(dockGuides(bounds, group, true, strips).size(), guides.size());
    const auto repeated = dockGuides(bounds, group, true, strips);
    for (int index = 0; index < guides.size(); ++index) QCOMPARE(repeated[index].rect, guides[index].rect);
    if (direction == Edge::Top || direction == Edge::Bottom) {
        // A full-width neighbouring header leaves no edge-parallel route.
        // Keep all five targets and place the complete cross beyond it.
        const QRect blocker(bounds.left(), direction == Edge::Top ? group.bottom() + 3 : group.top(), bounds.width(), 27);
        auto blockedStrips = strips; blockedStrips.append({blocker, Edge::Top});
        const auto shifted = dockGuides(bounds, group, true, blockedStrips);
        QCOMPARE(shifted.size(), 13);
        QRect shiftedCross; int targets = 0;
        for (const auto& guide : shifted) if (!guide.toEdge) {
            ++targets; shiftedCross = shiftedCross.united(guide.rect);
            QVERIFY(bounds.contains(guide.rect));
            for (const auto& strip : blockedStrips) QVERIFY(!guide.rect.intersects(strip.rect));
            for (const auto& other : shifted) if (other.toEdge) QVERIFY(!guide.rect.intersects(other.rect));
        }
        QCOMPARE(targets, 5);
        if (direction == Edge::Top) QCOMPARE(shiftedCross.top(), blocker.bottom() + 3);
        else QCOMPARE(shiftedCross.bottom(), blocker.top() - 3);
    }
}
void DragPresentationTest::displacedGroupCrossAlignsWithItsEdgePair_data()
{
    QTest::addColumn<int>("edge"); QTest::addColumn<int>("offset");
    for (const auto edge : {Edge::Left, Edge::Right, Edge::Top, Edge::Bottom})
        for (const int offset : {-24, 24})
            QTest::newRow(qPrintable(QString("edge-%1-offset-%2").arg(int(edge)).arg(offset))) << int(edge) << offset;
}
void DragPresentationTest::displacedGroupCrossAlignsWithItsEdgePair()
{
    QFETCH(int, edge); QFETCH(int, offset); const auto side = Edge(edge);
    // Non-zero, negative coordinates also exercise logical desktop geometry.
    const QRect bounds(-200, 80, 800, 600);
    const bool vertical = side == Edge::Left || side == Edge::Right;
    QRect group;
    if (vertical) group = QRect(side == Edge::Left ? bounds.left() : bounds.right() - 99,
        bounds.center().y() - 139 + offset, 100, 280);
    else group = QRect(bounds.center().x() - 139 + offset,
        side == Edge::Top ? bounds.top() : bounds.bottom() - 99, 280, 100);
    const auto guides = dockGuides(bounds, group);
    QCOMPARE(guides.size(), 13);
    QRect cross, pair;
    for (const auto& guide : guides) {
        QVERIFY(bounds.contains(guide.rect));
        if (!guide.toEdge) cross = cross.united(guide.rect);
        else if (guide.edge == side) pair = pair.united(guide.rect);
    }
    QVERIFY(!cross.isEmpty()); QVERIFY(!pair.isEmpty());
    if (vertical) QCOMPARE(cross.center().y(), pair.center().y());
    else QCOMPARE(cross.center().x(), pair.center().x());
    if (side == Edge::Left) QCOMPARE(cross.left() - pair.right() - 1, 2);
    if (side == Edge::Right) QCOMPARE(pair.left() - cross.right() - 1, 2);
    if (side == Edge::Top) QCOMPARE(cross.top() - pair.bottom() - 1, 2);
    if (side == Edge::Bottom) QCOMPARE(pair.top() - cross.bottom() - 1, 2);
    for (const auto& guide : guides) if (!guide.toEdge)
        for (const auto& other : guides) if (other.toEdge) QVERIFY(!guide.rect.intersects(other.rect));
}
void DragPresentationTest::displacedCrossKeepsItsTargetAcrossNeighborAndGaps_data()
{
    QTest::addColumn<int>("edge"); QTest::addColumn<bool>("leaveCross");
    for (const auto edge : {Edge::Left, Edge::Right, Edge::Top, Edge::Bottom})
        for (const bool leave : {false, true})
            QTest::newRow(qPrintable(QString("edge-%1-leave-%2").arg(int(edge)).arg(leave))) << int(edge) << leave;
}
void DragPresentationTest::displacedCrossKeepsItsTargetAcrossNeighborAndGaps()
{
    QFETCH(int, edge); QFETCH(bool, leaveCross); const auto side = Edge(edge);
    WindowManager manager;
    auto behavior = manager.behavior(); behavior.revealDuringDrag = false; manager.setBehavior(behavior);
    const auto source = manager.createWindow();
    auto* content = new QLabel("Live"); const auto moving = manager.addTab(source, content, {"Moving", "label", {}});
    const auto pane = manager.model().tab(moving)->activePane;
    const auto target = manager.createWindow(WindowKind::Workbench, {"Target", {780, 560}});
    const auto central = manager.addTab(target, new QLabel("Central"), {"Central", "label", {}});
    const auto pinned = manager.addTab(target, new QLabel("Pinned"), {"Pinned", "label", {}});
    QVERIFY(manager.moveTabToEdge(pinned, side, true));
    const auto pinnedGroup = manager.model().groupForTab(pinned);
    const auto centralGroup = manager.model().groupForTab(central);
    QVERIFY(manager.execute("layout.setEdgeExtent", {{"group", pinnedGroup}, {"extent", 100}}, target));
    auto* sourceWindow = static_cast<WorkspaceWindow*>(manager.windowWidget(source));
    auto* targetWindow = static_cast<WorkspaceWindow*>(manager.windowWidget(target));
    showFixture(sourceWindow, {20, 20}); showFixture(targetWindow, {70, 70});
    QVERIFY(QTest::qWaitForWindowExposed(targetWindow));
    QPointer<WorkspaceWindow> carrier = beginDrag(manager, source, moving); QVERIFY(carrier);
    auto* surface = targetWindow->surface();
    const auto groupRect = surface->groupRects().value(pinnedGroup); QVERIFY(!groupRect.isEmpty());
    moveMouse(carrier, groupRect.center());
    const QRect bounds(surface->mapToGlobal(QPoint()), surface->size());
    const auto guides = dockGuides(bounds, groupRect, true, surface->dockStrips(), surface->edgeBarRects());
    const auto placement = side == Edge::Left ? Placement::Right : side == Edge::Right ? Placement::Left
        : side == Edge::Top ? Placement::Bottom : Placement::Top;
    QRect center, arm, cross;
    for (const auto& guide : guides) if (!guide.toEdge) {
        cross = cross.united(guide.rect);
        if (guide.placement == Placement::Center) center = guide.rect;
        if (guide.placement == placement) arm = guide.rect;
    }
    QVERIFY(!center.isEmpty()); QVERIFY(!arm.isEmpty());
    QVERIFY2(!groupRect.contains(arm.center()), "The inward arm must actually overlap the neighboring group");
    QCOMPARE(surface->groupAt(arm.center()), centralGroup);
    QPoint gap;
    if (side == Edge::Left) gap = {center.right() + 2, center.center().y()};
    if (side == Edge::Right) gap = {center.left() - 2, center.center().y()};
    if (side == Edge::Top) gap = {center.center().x(), center.bottom() + 2};
    if (side == Edge::Bottom) gap = {center.center().x(), center.top() - 2};
    QVERIFY(cross.contains(gap));
    for (const auto& guide : guides) if (!guide.toEdge) QVERIFY(!guide.rect.contains(gap));
    // Crossing the blank spacing must not discard the old cross before the
    // adjacent arm can be reached, even though another group lies underneath.
    moveMouse(carrier, center.center()); moveMouse(carrier, gap); moveMouse(carrier, arm.center());
    QPoint destination = arm.center();
    if (leaveCross) {
        const auto centralRect = surface->groupRects().value(centralGroup);
        const auto hover = centralRect.center(); QVERIFY(!cross.contains(hover));
        moveMouse(carrier, hover);
        const auto centralGuides = dockGuides(bounds, centralRect, true,
            surface->dockStrips(), surface->edgeBarRects());
        for (const auto& guide : centralGuides) if (!guide.toEdge && guide.placement == Placement::Center)
            destination = guide.rect.center();
        moveMouse(carrier, destination);
    }
    QMouseEvent release(QEvent::MouseButtonRelease, carrier->mapFromGlobal(destination), destination,
        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(carrier, &release); QApplication::processEvents();
    const auto resultId = manager.model().groupForTab(moving);
    const auto* result = manager.model().group(resultId); QVERIFY(result);
    QCOMPARE(result->windowId, target);
    if (leaveCross) QCOMPARE(resultId, centralGroup);
    else {
        QCOMPARE(result->location, GroupLocation::Pinned); QCOMPARE(result->edge, side);
        QVERIFY(resultId != pinnedGroup); QCOMPARE(manager.model().groupForTab(pinned), pinnedGroup);
    }
    QCOMPARE(manager.paneWidget(pane), content); QCOMPARE(content->window(), targetWindow);
    QVERIFY(!QWidget::mouseGrabber()); QVERIFY(manager.model().validate());
}
void DragPresentationTest::hiddenCrossDoesNotStealNeighboringContent_data()
{
    QTest::addColumn<int>("edge");
    for (const auto edge : {Edge::Left, Edge::Right, Edge::Top, Edge::Bottom})
        QTest::newRow(qPrintable(QString("edge-%1").arg(int(edge)))) << int(edge);
}
void DragPresentationTest::hiddenCrossDoesNotStealNeighboringContent()
{
    QFETCH(int, edge); const auto side = Edge(edge);
    WindowManager manager;
    auto behavior = manager.behavior(); behavior.revealDuringDrag = false; manager.setBehavior(behavior);
    const auto source = manager.createWindow(); auto* content = new QLabel("Live");
    const auto moving = manager.addTab(source, content, {"Moving", "label", {}});
    const auto pane = manager.model().tab(moving)->activePane;
    const auto target = manager.createWindow(WindowKind::Workbench, {"Target", {780, 560}});
    const auto central = manager.addTab(target, new QLabel("Central"), {"Central", "label", {}});
    const auto pinned = manager.addTab(target, new QLabel("Pinned"), {"Pinned", "label", {}});
    QVERIFY(manager.moveTabToEdge(pinned, side, true));
    const auto pinnedGroup = manager.model().groupForTab(pinned), centralGroup = manager.model().groupForTab(central);
    QVERIFY(manager.execute("layout.setEdgeExtent", {{"group", pinnedGroup}, {"extent", 100}}, target));
    auto* sourceWindow = static_cast<WorkspaceWindow*>(manager.windowWidget(source));
    auto* targetWindow = static_cast<WorkspaceWindow*>(manager.windowWidget(target));
    showFixture(sourceWindow, {20, 20}); showFixture(targetWindow, {70, 70});
    QVERIFY(QTest::qWaitForWindowExposed(targetWindow));
    QPointer<WorkspaceWindow> carrier = beginDrag(manager, source, moving); QVERIFY(carrier);
    const auto carrierId = carrier->id();
    auto* surface = targetWindow->surface();
    const auto strip = surface->tabBarRects().value(pinnedGroup); QVERIFY(!strip.isEmpty());
    // A strip insertion hides the controls. The displaced cross coordinates
    // still exist in the hit result, but they must not capture a later move.
    moveMouse(carrier, strip.center());
    QCOMPARE(carrier->dragPresentation(), WorkspaceWindow::DragPresentation::Tab);
    for (auto* widget : QApplication::topLevelWidgets()) if (widget->objectName() == "DockOverlay")
        QVERIFY(!widget->isVisible());
    const QRect bounds(surface->mapToGlobal(QPoint()), surface->size());
    const auto guides = dockGuides(bounds, surface->groupRects().value(pinnedGroup), true,
        surface->dockStrips(), surface->edgeBarRects());
    const auto inward = side == Edge::Left ? Placement::Right : side == Edge::Right ? Placement::Left
        : side == Edge::Top ? Placement::Bottom : Placement::Top;
    QPoint staleArm;
    for (const auto& guide : guides) if (!guide.toEdge && guide.placement == inward)
        staleArm = guide.rect.center();
    QVERIFY(!staleArm.isNull()); QCOMPARE(surface->groupAt(staleArm), centralGroup);
    const auto centralGuides = dockGuides(bounds, surface->groupRects().value(centralGroup), true,
        surface->dockStrips(), surface->edgeBarRects());
    for (const auto& guide : centralGuides) QVERIFY(!guide.rect.contains(staleArm));
    moveMouse(carrier, staleArm);
    QMouseEvent release(QEvent::MouseButtonRelease, carrier->mapFromGlobal(staleArm), staleArm,
        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(carrier, &release); QApplication::processEvents();
    QVERIFY(carrier);
    const auto* result = manager.model().group(manager.model().groupForTab(moving)); QVERIFY(result);
    QCOMPARE(result->windowId, carrierId); QCOMPARE(manager.model().window(carrierId)->kind, WindowKind::Floating);
    QCOMPARE(manager.model().group(pinnedGroup)->tabs, QVector<TabId>{pinned});
    QCOMPARE(manager.model().group(centralGroup)->tabs, QVector<TabId>{central});
    QCOMPARE(manager.paneWidget(pane), content); QCOMPARE(content->window(), carrier.data());
    QVERIFY(!QWidget::mouseGrabber()); QVERIFY(manager.model().validate());
}
void DragPresentationTest::tabTargetKeepsCarrierAndLiveContent_data()
{
    QTest::addColumn<bool>("floating");
    QTest::newRow("workbench") << false; QTest::newRow("floating") << true;
}
void DragPresentationTest::edgeGuidesStayInTheirOwnSideBand_data()
{
    QTest::addColumn<int>("edge"); QTest::addColumn<bool>("rail");
    for (int edge : {int(Edge::Left), int(Edge::Right)}) for (bool rail : {false, true})
        QTest::newRow(qPrintable(QString("side-%1-rail-%2").arg(edge).arg(rail))) << edge << rail;
}
void DragPresentationTest::edgeGuidesStayInTheirOwnSideBand()
{
    QFETCH(int, edge); QFETCH(bool, rail);
    const QRect bounds(120, 70, 1000, 600);
    QHash<Edge, QRect> rails;
    if (rail) rails.insert(Edge(edge), edge == int(Edge::Left)
        ? QRect(bounds.left(), bounds.top(), 22, bounds.height())
        : QRect(bounds.right() - 21, bounds.top(), 22, bounds.height()));
    const auto middle = QRect(edge == int(Edge::Left) ? bounds.left() : bounds.center().x(),
        bounds.center().y() - 12, bounds.width() / 2 + 1, 27);
    const QVector<DockStrip> tabs{{QRect(bounds.left(), bounds.top(), bounds.width(), 27), Edge::Top}, {middle, Edge::Top}};
    const auto guides = dockGuides(bounds, bounds, true, tabs, rails);
    QCOMPARE(guides.size(), 13);
    int count = 0;
    for (const auto& guide : guides) if (guide.toEdge && guide.edge == Edge(edge)) {
        ++count; QVERIFY(bounds.contains(guide.rect));
        for (const auto& strip : tabs) QVERIFY(!guide.rect.intersects(strip.rect));
        for (const auto& railRect : rails) QVERIFY(!guide.rect.intersects(railRect));
        const int offset = 6 + (rail ? 22 : 0) + (guide.pinned ? 28 : 0);
        if (edge == int(Edge::Left)) QCOMPARE(guide.rect.left(), bounds.left() + offset);
        else QCOMPARE(bounds.right() - guide.rect.right(), offset);
    }
    QCOMPARE(count, 2);
}
void DragPresentationTest::edgeGuidesAcceptDropsBesideStackedGroups_data()
{
    QTest::addColumn<int>("edge"); QTest::addColumn<bool>("pinned");
    for (int edge : {int(Edge::Left), int(Edge::Right)}) for (bool pinned : {false, true})
        QTest::newRow(qPrintable(QString("side-%1-pinned-%2").arg(edge).arg(pinned))) << edge << pinned;
}
void DragPresentationTest::edgeGuidesAcceptDropsBesideStackedGroups()
{
    QFETCH(int, edge); QFETCH(bool, pinned);
    WindowManager manager;
    const auto source = manager.createWindow(); auto* content = new QLabel("Live content");
    const auto moving = manager.addTab(source, content, {"Moving", "label", {}});
    const auto pane = manager.model().tab(moving)->activePane;
    const auto target = manager.createWindow(WindowKind::Workbench, {"Target", {700, 450}});
    const auto top = manager.addTab(target, new QLabel("Top"), {"Top", "label", {}});
    const auto bottom = manager.addTab(target, new QLabel("Bottom"), {"Bottom", "label", {}});
    QVERIFY(manager.moveTab(bottom, manager.model().groupForTab(top), Placement::Bottom));
    auto* sourceWindow = static_cast<WorkspaceWindow*>(manager.windowWidget(source));
    auto* targetWindow = static_cast<WorkspaceWindow*>(manager.windowWidget(target));
    showFixture(sourceWindow, {20, 20}); showFixture(targetWindow, {70, 70});
    QVERIFY(QTest::qWaitForWindowExposed(targetWindow));
    QPointer<WorkspaceWindow> carrier = beginDrag(manager, source, moving); QVERIFY(carrier);
    auto* surface = targetWindow->surface();
    const QRect bounds(surface->mapToGlobal(QPoint()), surface->size());
    const auto guides = dockGuides(bounds, surface->groupRects().value(manager.model().groupForTab(top)),
        true, surface->dockStrips(), surface->edgeBarRects());
    QPoint targetPoint;
    for (const auto& guide : guides) if (guide.toEdge && guide.edge == Edge(edge) && guide.pinned == pinned) {
        targetPoint = guide.rect.center();
        if (edge == int(Edge::Left)) QVERIFY(guide.rect.right() < bounds.left() + 70);
        else QVERIFY(guide.rect.left() > bounds.right() - 70);
        for (const auto& strip : surface->dockStrips()) QVERIFY(!guide.rect.intersects(strip.rect));
    }
    QVERIFY(!targetPoint.isNull()); moveMouse(carrier, targetPoint);
    QMouseEvent release(QEvent::MouseButtonRelease, carrier->mapFromGlobal(targetPoint), targetPoint,
        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(carrier, &release); QApplication::processEvents();
    const auto* group = manager.model().group(manager.model().groupForTab(moving)); QVERIFY(group);
    QCOMPARE(group->windowId, target); QCOMPARE(group->edge, Edge(edge));
    QCOMPARE(group->location, pinned ? GroupLocation::Pinned : GroupLocation::AutoHide);
    QCOMPARE(manager.paneWidget(pane), content); QVERIFY(!QWidget::mouseGrabber()); QVERIFY(manager.model().validate());
}
void DragPresentationTest::guidesTrackWindowGeometryWithoutMouseMovement_data()
{
    QTest::addColumn<bool>("floating"); QTest::addColumn<QString>("change");
    for (bool floating : {false, true}) for (const auto* change : {"move", "resize", "maximize", "restore"})
        QTest::newRow(qPrintable(QString("floating-%1-%2").arg(floating).arg(change))) << floating << QString::fromLatin1(change);
}
void DragPresentationTest::guidesTrackWindowGeometryWithoutMouseMovement()
{
    QFETCH(bool, floating); QFETCH(QString, change);
    WindowManager manager;
    auto behavior = manager.behavior(); behavior.revealDuringDrag = false; manager.setBehavior(behavior);
    const auto source = manager.createWindow(); auto* content = new QLabel("Live content");
    const auto moving = manager.addTab(source, content, {"Moving", "label", {}});
    const auto pane = manager.model().tab(moving)->activePane;
    const auto target = manager.createWindow(floating ? WindowKind::Floating : WindowKind::Workbench,
        {"Target", {700, 450}}, source);
    manager.addTab(target, new QLabel("Target"), {"Target", "label", {}});
    auto* sourceWindow = static_cast<WorkspaceWindow*>(manager.windowWidget(source));
    auto* targetWindow = static_cast<WorkspaceWindow*>(manager.windowWidget(target));
    showFixture(sourceWindow, {20, 20}); showFixture(targetWindow, {90, 90});
    QVERIFY(QTest::qWaitForWindowExposed(targetWindow));
    if (change == "restore") { targetWindow->showMaximized(); QTRY_VERIFY(targetWindow->isMaximized()); }
    QPointer<WorkspaceWindow> carrier = beginDrag(manager, source, moving); QVERIFY(carrier);
    const auto hover = targetWindow->surface()->centralRectGlobal().center() + QPoint(50, 0);
    moveMouse(carrier, hover);
    QPointer<QWidget> overlay;
    for (auto* widget : QApplication::topLevelWidgets()) if (widget->objectName() == "DockOverlay") overlay = widget;
    QVERIFY(overlay); QTRY_VERIFY(overlay->isVisible());
    const auto original = overlay->geometry();
    if (change == "move") targetWindow->move(targetWindow->pos() + QPoint(60, 40));
    else if (change == "resize") targetWindow->resize(840, 520);
    else if (change == "maximize") targetWindow->showMaximized();
    else targetWindow->showNormal();
    // No mouse event follows the window change. Stationary drag guides must
    // refresh their target coordinates as soon as the actual layout settles.
    QTRY_COMPARE(overlay->geometry(), QRect(targetWindow->mapToGlobal(QPoint()), targetWindow->size()));
    QVERIFY(overlay->geometry() != original); QVERIFY(overlay->isVisible());
    auto* surface = targetWindow->surface();
    const QRect bounds(surface->mapToGlobal(QPoint()), surface->size());
    const auto group = surface->groupAt(hover);
    const auto guides = dockGuides(bounds, surface->groupRects().value(group, surface->centralRectGlobal()),
        true, surface->dockStrips(), surface->edgeBarRects());
    const auto image = overlay->grab().toImage();
    QPoint drop;
    for (const auto& guide : guides) if (guide.toEdge) {
        QVERIFY(bounds.contains(guide.rect));
        const auto local = guide.rect.translated(-overlay->mapToGlobal(QPoint()));
        QVERIFY(overlay->rect().contains(local));
        int ink = 0;
        const auto pixels = QRect(qRound(local.x() * image.devicePixelRatio()), qRound(local.y() * image.devicePixelRatio()),
            qRound(local.width() * image.devicePixelRatio()), qRound(local.height() * image.devicePixelRatio()));
        for (int y = pixels.top(); y <= pixels.bottom(); ++y) for (int x = pixels.left(); x <= pixels.right(); ++x)
            if (image.pixelColor(x, y) == QColor("#8A52C7")) ++ink;
        QVERIFY2(ink > 0, "The visible guide must be painted at its current hit rectangle");
        if (!guide.pinned && guide.edge == (floating ? Edge::Right : Edge::Left)) drop = guide.rect.center();
    }
    QVERIFY(!drop.isNull()); moveMouse(carrier, drop);
    QMouseEvent release(QEvent::MouseButtonRelease, carrier->mapFromGlobal(drop), drop,
        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(carrier, &release); QApplication::processEvents();
    const auto* result = manager.model().group(manager.model().groupForTab(moving)); QVERIFY(result);
    QCOMPARE(result->windowId, target); QCOMPARE(result->edge, floating ? Edge::Right : Edge::Left);
    QCOMPARE(result->location, GroupLocation::AutoHide); QCOMPARE(manager.paneWidget(pane), content);
    QVERIFY(!QWidget::mouseGrabber()); QVERIFY(!overlay); QVERIFY(manager.model().validate());
}
void DragPresentationTest::tabTargetKeepsCarrierAndLiveContent()
{
    QFETCH(bool, floating);
    WindowManager manager; const auto source = manager.createWindow();
    auto* content = new QLabel("Live drag content"); const QPointer<QLabel> observed(content);
    const auto tab = manager.addTab(source, content, {"Moving", "label", {}});
    const auto pane = manager.model().tab(tab)->activePane;
    const auto target = manager.createWindow(floating ? WindowKind::Floating : WindowKind::Workbench, {"Target", {700, 450}}, source);
    const auto a = manager.addTab(target, new QLabel("A"), {"First", "label", {}});
    const auto b = manager.addTab(target, new QLabel("B"), {"Second", "label", {}});
    auto* sourceWindow = static_cast<WorkspaceWindow*>(manager.windowWidget(source));
    auto* targetWindow = static_cast<WorkspaceWindow*>(manager.windowWidget(target));
    showFixture(sourceWindow, {20, 20}); showFixture(targetWindow, {70, 70});
    QVERIFY(QTest::qWaitForWindowExposed(targetWindow));
    const auto before = manager.model().toJson();
    const auto committedLayout = manager.saveLayout();
    QVector<QJsonObject> preparingSnapshots;
    const auto saveConnection = QObject::connect(&manager, &WindowManager::layoutChanged, &manager,
        [&] { preparingSnapshots.append(manager.saveLayout()); }, Qt::DirectConnection);
    QPointer<WorkspaceWindow> carrier = beginDrag(manager, source, tab); QVERIFY(carrier);
    QObject::disconnect(saveConnection);
    QVERIFY(!preparingSnapshots.isEmpty());
    for (const auto& snapshot : preparingSnapshots) QCOMPARE(snapshot, committedLayout);
    const auto hwnd = carrier->winId(); const auto carrierId = carrier->id();
    auto* group = groupView(targetWindow, manager.model().groupForTab(a)); QVERIFY(group);
    const auto second = group->tabBar()->tabRect(1);
    const auto point = group->tabBar()->mapToGlobal(QPoint(second.left() + 2, second.center().y()));
    const auto modelDuringDrag = manager.model().toJson();
    moveMouse(carrier, point);
    QCOMPARE(carrier->dragPresentation(), WorkspaceWindow::DragPresentation::Tab);
    QCOMPARE(carrier->winId(), hwnd); QCOMPARE(QWidget::mouseGrabber(), carrier.data());
    QCOMPARE(carrier->windowOpacity(), qreal(0));
    QVERIFY(!group->tabBar()->incomingRect().isEmpty());
    QVERIFY(carrier->findChild<QWidget*>("WindowHeader")->isHidden()); QVERIFY(!carrier->mask().isEmpty());
    QVERIFY(observed && observed->isVisible()); QCOMPARE(manager.paneWidget(pane), content);
    QCOMPARE(manager.model().toJson(), modelDuringDrag); QCOMPARE(group->count(), 2);
    QCOMPARE(manager.saveLayout(), committedLayout);
    QCOMPARE(group->tabBar()->displayTabRect(1).left(), second.left() + group->tabBar()->dropPreviewRect().width());
    QVERIFY(!group->insertionPreviewRectGlobal().isEmpty());
    QCOMPARE(group->insertionPreviewRectGlobal().width(), carrier->surface()->dragLabelExtent(tab, false));
    saveRequestedScreenshot(carrier, QString("drag-tab-carrier-%1.png").arg(floating));
    saveRequestedScreenshot(targetWindow, QString("drag-tab-target-%1.png").arg(floating));
    moveMouse(carrier, targetWindow->mapToGlobal(QPoint(targetWindow->width() + 200, 100)));
    QCOMPARE(carrier->dragPresentation(), WorkspaceWindow::DragPresentation::Window);
    QCOMPARE(carrier->windowOpacity(), qreal(1));
    QVERIFY(group->tabBar()->incomingRect().isEmpty());
    QVERIFY(carrier->mask().isEmpty()); QVERIFY(carrier->findChild<QWidget*>("WindowHeader")->isVisible());
    QVERIFY(group->insertionPreviewRectGlobal().isEmpty()); QCOMPARE(carrier->winId(), hwnd);
    moveMouse(carrier, point);
    CarrierCommitObserver commit(carrier);
    QMouseEvent release(QEvent::MouseButtonRelease, carrier->mapFromGlobal(point), point, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(carrier, &release); QApplication::processEvents();
    QCOMPARE(manager.model().group(manager.model().groupForTab(a))->tabs, (QVector<TabId>{a, tab, b}));
    QCOMPARE(manager.paneWidget(pane), content); QVERIFY(!manager.model().window(carrierId));
    QVERIFY(commit.hidden); QVERIFY(commit.compactAtHide); QVERIFY(!commit.chromeRevealed);
#ifdef Q_OS_WIN
    QVERIFY(commit.removal.attempted); QCOMPARE(commit.removal.result, qint32(0)); QVERIFY(commit.removal.forcedDisabled);
#endif
    QVERIFY(groupView(targetWindow, manager.model().groupForTab(a))->insertionPreviewRectGlobal().isEmpty());
    QVERIFY(groupView(targetWindow, manager.model().groupForTab(a))->tabBar()->incomingRect().isEmpty());
    QVERIFY(!QWidget::mouseGrabber()); QVERIFY(manager.model().validate()); Q_UNUSED(before)
}
void DragPresentationTest::stickyRowPreviewKeepsTheLabelAgainstItsContent_data()
{
    QTest::addColumn<bool>("bottom"); QTest::addColumn<int>("rows"); QTest::addColumn<int>("sizing");
    for (const bool bottom : {false, true}) for (int rows = 0; rows < 3; ++rows)
        for (const auto sizing : {StickyTabSizing::Normal, StickyTabSizing::Compact, StickyTabSizing::Shrink})
            QTest::newRow(qPrintable(QString("%1-rows-%2-size-%3").arg(bottom ? "bottom" : "top").arg(rows).arg(int(sizing))))
                << bottom << rows << int(sizing);
}
void DragPresentationTest::stickyRowPreviewKeepsTheLabelAgainstItsContent()
{
    QFETCH(bool, bottom); QFETCH(int, rows); QFETCH(int, sizing);
    WindowManager manager;
    auto appearance = manager.tabAppearance();
    appearance.stickyTabsOnSeparateRow = true; appearance.stickySizing = StickyTabSizing(sizing);
    appearance.centralPlacement = bottom ? TabBarPlacement::Bottom : TabBarPlacement::Top;
    appearance.multiline = rows != 0; appearance.wheelExpandRows = rows == 2;
    QVERIFY(manager.setTabAppearance(appearance));
    const auto source = manager.createWindow(WindowKind::Workbench, {"Source", {600, 300}});
    auto* content = new QLabel("Sticky content");
    const auto tab = manager.addTab(source, content, {"Sticky", "label", {}});
    const auto pane = manager.model().tab(tab)->activePane;
    manager.addTab(source, new QLabel("Other"), {"Other", "label", {}});
    QVERIFY(manager.setTabSticky(tab, true));
    const auto target = manager.createWindow(WindowKind::Workbench, {"Target", {600, 300}});
    const auto targetTab = manager.addTab(target, new QLabel("Target"), {"Target", "label", {}});
    auto* sourceWindow = static_cast<WorkspaceWindow*>(manager.windowWidget(source));
    auto* targetWindow = static_cast<WorkspaceWindow*>(manager.windowWidget(target));
    const auto origin = qApp->primaryScreen()->availableGeometry().topLeft();
    showFixture(sourceWindow, origin + QPoint(12, 12)); showFixture(targetWindow, origin + QPoint(100, 140));
    QVERIFY(QTest::qWaitForWindowExposed(sourceWindow)); QVERIFY(QTest::qWaitForWindowExposed(targetWindow));
    auto* sourceGroup = groupView(sourceWindow, manager.model().groupForTab(tab)); QVERIFY(sourceGroup);
    auto* sourceBar = sourceGroup->tabBar();
    const auto start = sourceBar->mapToGlobal(sourceBar->displayTabRect(0).center());
    test::authoredMouseInput().takePriority();
    QMouseEvent press(QEvent::MouseButtonPress, sourceBar->mapFromGlobal(start), start,
        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(sourceBar, &press);
    // Pressing activates the tab before the drag transaction begins.
    const auto committed = manager.saveLayout();
    moveMouse(sourceBar, start + QPoint(QApplication::startDragDistance() + 3, 0));
    QCOMPARE(QWidget::mouseGrabber(), sourceWindow);
    // Leaving the entire two-row source creates the real carrier, rather
    // than mistaking a move onto the other row for a detach. A lifted tab
    // stays in its strip up to a tab height past it (tab strip design 6.2).
    const int beyond = 2 * sourceBar->metrics().rowHeight;
    const auto outside = QPoint(sourceWindow->mapToGlobal(QPoint(8, 0)).x(), bottom
        ? sourceBar->mapToGlobal(QPoint()).y() - beyond : sourceBar->mapToGlobal(QPoint(0, sourceBar->height())).y() + beyond);
    moveMouse(sourceWindow, outside);
    QPointer<WorkspaceWindow> carrier = dynamic_cast<WorkspaceWindow*>(QWidget::mouseGrabber());
    QVERIFY(carrier && carrier != sourceWindow);
    const auto hwnd = carrier->winId();
    auto* targetGroup = groupView(targetWindow, manager.model().groupForTab(targetTab)); QVERIFY(targetGroup);
    const auto point = targetGroup->tabBar()->mapToGlobal(targetGroup->tabBar()->displayTabRect(0).center());
    for (int visit = 0; visit < 2; ++visit) {
        moveMouse(carrier, point);
        QCOMPARE(carrier->dragPresentation(), WorkspaceWindow::DragPresentation::Tab);
        auto* preview = groupView(carrier, manager.model().groupForTab(tab)); QVERIFY(preview);
        auto* bar = preview->tabBar();
        auto* stack = preview->findChild<QStackedWidget*>(QString(), Qt::FindDirectChildrenOnly); QVERIFY(stack);
        const QRect label(bar->mapTo(carrier, bar->displayTabRect(0).topLeft()), bar->displayTabRect(0).size());
        const QRect body(stack->mapTo(carrier, QPoint()), stack->size());
        // Over a strip the carrier is its tab alone; the target shows the
        // content (tab strip design 6.2).
        QVERIFY(label.height() == bar->metrics().rowHeight);
        if (bottom) QVERIFY(label.top() >= body.bottom());
        else QVERIFY(body.top() >= label.bottom());
        verifyLabelMask(carrier->mask(), {label}); if (QTest::currentTestFailed()) return;
        QCOMPARE(carrier->dragLabelAnchor(), label.center());
        QCOMPARE(carrier->winId(), hwnd); QCOMPARE(QWidget::mouseGrabber(), carrier.data());
        QCOMPARE(manager.paneWidget(pane), content); QVERIFY(content->isVisible());
        QCOMPARE(manager.saveLayout(), committed); QVERIFY(manager.isTabSticky(tab));
        moveMouse(carrier, outside);
        QCOMPARE(carrier->dragPresentation(), WorkspaceWindow::DragPresentation::Window);
        QVERIFY(carrier->mask().isEmpty());
    }
    QTest::keyClick(carrier, Qt::Key_Escape); QApplication::processEvents();
    QVERIFY(!QWidget::mouseGrabber()); QCOMPARE(manager.saveLayout(), committed);
    QCOMPARE(manager.paneWidget(pane), content); QVERIFY(manager.isTabSticky(tab)); QVERIFY(manager.model().validate());
}
void DragPresentationTest::compactPreviewMaskFollowsTheDisplayedRows_data()
{
    QTest::addColumn<bool>("bottom"); QTest::addColumn<bool>("collapsed"); QTest::addColumn<bool>("sticky");
    for (const bool bottom : {false, true}) for (const bool collapsed : {false, true}) for (const bool sticky : {false, true})
        QTest::newRow(qPrintable(QString("bottom-%1-collapsed-%2-sticky-%3").arg(bottom).arg(collapsed).arg(sticky)))
            << bottom << collapsed << sticky;
}
void DragPresentationTest::compactPreviewMaskFollowsTheDisplayedRows()
{
    QFETCH(bool, bottom); QFETCH(bool, collapsed); QFETCH(bool, sticky);
    WindowManager manager;
    auto appearance = manager.tabAppearance();
    appearance.centralPlacement = bottom ? TabBarPlacement::Bottom : TabBarPlacement::Top;
    appearance.multiline = true; appearance.stickyMultiline = true; appearance.stickyTabsOnSeparateRow = true;
    appearance.showAllRows = !collapsed; appearance.stickyShowAllRows = !collapsed; appearance.rowExpansion = TabRowExpansion::Push;
    QVERIFY(manager.setTabAppearance(appearance));
    const auto window = manager.createWindow(WindowKind::Workbench, {"Rows", {400, 350}});
    QVector<TabId> tabs;
    for (int i = 0; i < 10; ++i) {
        tabs.append(manager.addTab(window, new QLabel("Content"), {QString("Preview tab %1").arg(i), "label", {}}));
        if (sticky && i < 3) QVERIFY(manager.setTabSticky(tabs.back(), true));
    }
    auto* carrier = static_cast<WorkspaceWindow*>(manager.windowWidget(window));
    showFixture(carrier, qApp->primaryScreen()->availableGeometry().topLeft() + QPoint(20, 20));
    QVERIFY(QTest::qWaitForWindowExposed(carrier));
    auto* group = groupView(carrier, manager.model().groupForTab(tabs.back())); QVERIFY(group);
    auto* bar = group->tabBar();
    const auto normalHeight = bar->height();
    const auto hwnd = carrier->winId();
    carrier->setDragPresentation(WorkspaceWindow::DragPresentation::Tab);
    // Over a strip only the tabs remain; the target shows the content
    // (tab strip design 6.2).
    QVector<QRect> labels; QRect first;
    const auto viewport = group->viewportRectGlobal();
    for (int i = 0; i < bar->count(); ++i) {
        const auto shown = bar->displayTabRect(i);
        if (shown.isEmpty()) continue;
        const QRect label(bar->mapTo(carrier, shown.topLeft()), shown.size());
        QVERIFY(viewport.contains(QRect(bar->mapToGlobal(shown.topLeft()), shown.size())));
        if (first.isEmpty()) first = label;
        labels.append(label);
    }
    QVERIFY(!first.isEmpty());
    verifyLabelMask(carrier->mask(), labels); if (QTest::currentTestFailed()) return;
    QCOMPARE(carrier->dragLabelAnchor(), first.center());
    if (collapsed && !sticky) QVERIFY(bar->displayTabRect(0).isEmpty());
    carrier->setDragPresentation(WorkspaceWindow::DragPresentation::Window);
    QCOMPARE(bar->height(), normalHeight); QVERIFY(carrier->mask().isEmpty()); QCOMPARE(carrier->winId(), hwnd);
}
void DragPresentationTest::edgeTargetMirrorsTheLabelAndClearsOnCancel_data()
{
    QTest::addColumn<int>("edge");
    for (int edge = 0; edge < 4; ++edge) QTest::newRow(qPrintable(QString::number(edge))) << edge;
}
void DragPresentationTest::edgeTargetMirrorsTheLabelAndClearsOnCancel()
{
    QFETCH(int, edge);
    WindowManager manager; const auto source = manager.createWindow();
    auto* content = new QLineEdit("Live edge content");
    const auto tab = manager.addTab(source, content, {"Moving", "label", {}}); const auto pane = manager.model().tab(tab)->activePane;
    const auto target = manager.createWindow(WindowKind::Workbench, {"Target", {700, 450}});
    const auto a = manager.addTab(target, new QLabel("A"), {"Moving", "label", {}});
    const auto b = manager.addTab(target, new QLabel("B"), {"Second", "label", {}});
    const auto central = manager.addTab(target, new QLabel("Central"), {"Central", "label", {}});
    QVERIFY(manager.moveTabToEdge(a, Edge(edge))); QVERIFY(manager.moveTabToEdge(b, Edge(edge)));
    QCOMPARE(manager.model().group(manager.model().groupForTab(central))->location, GroupLocation::Central);
    auto* sourceWindow = static_cast<WorkspaceWindow*>(manager.windowWidget(source));
    auto* targetWindow = static_cast<WorkspaceWindow*>(manager.windowWidget(target));
    showFixture(sourceWindow, {20, 20}); showFixture(targetWindow, {70, 70}); QVERIFY(QTest::qWaitForWindowExposed(targetWindow));
    const auto before = manager.model().toJson();
    const auto committedLayout = manager.saveLayout();
    QPointer<WorkspaceWindow> carrier = beginDrag(manager, source, tab); QVERIFY(carrier);
    const auto hwnd = carrier->winId();
    carrier->activateWindow(); content->setFocus(); QTRY_COMPARE(QApplication::focusWidget(), content);
    const auto area = targetWindow->surface()->edgeBarRects().value(Edge(edge));
    const bool vertical = edge < 2;
    const QPoint point = vertical ? QPoint(area.center().x(), area.top() + 8) : QPoint(area.left() + 8, area.center().y());
    moveMouse(carrier, point);
    QCOMPARE(carrier->dragPresentation(), WorkspaceWindow::DragPresentation(int(WorkspaceWindow::DragPresentation::Left) + edge));
    QCOMPARE(carrier->winId(), hwnd); QCOMPARE(QWidget::mouseGrabber(), carrier.data());
    auto* label = carrier->findChild<QWidget*>("DragEdgeLabel"); auto* panel = carrier->findChild<QWidget*>("DragEdgeContent");
    QVERIFY(label && label->isVisible()); QVERIFY(panel && panel->isVisible());
    auto* title = carrier->findChild<QWidget*>("DragPeekTitle"); QVERIFY(title);
    QCOMPARE(title->height(), 20);
    QWidget* railLabel = nullptr;
    for (auto* candidate : targetWindow->surface()->findChildren<QWidget*>("EdgeButton"))
        if (candidate->property("tabId").toUuid() == a) railLabel = candidate;
    QVERIFY(railLabel);
    QCOMPARE(label->size(), railLabel->size()); QCOMPARE(label->font(), railLabel->font());
    label->setAttribute(Qt::WA_UnderMouse, false); railLabel->setAttribute(Qt::WA_UnderMouse, false);
    QVERIFY(!label->hasFocus()); QVERIFY(!railLabel->hasFocus());
    QCOMPARE(label->grab().toImage(), railLabel->grab().toImage());
    const QRect labelRect(label->mapTo(carrier, QPoint()), label->size());
    const QRect panelRect(panel->mapTo(carrier, QPoint()), panel->size());
    if (Edge(edge) == Edge::Left) QVERIFY(labelRect.right() < panelRect.left());
    if (Edge(edge) == Edge::Right) QVERIFY(labelRect.left() > panelRect.right());
    if (Edge(edge) == Edge::Top) QVERIFY(labelRect.bottom() < panelRect.top());
    if (Edge(edge) == Edge::Bottom) QVERIFY(labelRect.top() > panelRect.bottom());
    QVERIFY(content->isVisible()); QCOMPARE(manager.paneWidget(pane), content);
    QCOMPARE(QApplication::focusWidget(), content);
    QVERIFY(!targetWindow->surface()->edgeInsertionPreviewRect(Edge(edge)).isEmpty());
    const auto ghost = targetWindow->surface()->edgeInsertionPreviewRect(Edge(edge));
    QCOMPARE(edge < 2 ? ghost.height() : ghost.width(), edge < 2 ? label->height() : label->width());
    QCOMPARE(manager.saveLayout(), committedLayout);
    saveRequestedScreenshot(carrier, QString("drag-edge-carrier-%1.png").arg(edge));
    saveRequestedScreenshot(targetWindow, QString("drag-edge-target-%1.png").arg(edge));
    moveMouse(carrier, targetWindow->mapToGlobal(QPoint(targetWindow->width() + 200, 100)));
    QCOMPARE(carrier->dragPresentation(), WorkspaceWindow::DragPresentation::Window);
    QVERIFY(content->isVisible()); QVERIFY(carrier->surface()->isVisible());
    QCOMPARE(QApplication::focusWidget(), content);
    QVERIFY(carrier->mask().isEmpty()); QCOMPARE(carrier->winId(), hwnd);
    auto* centralView = groupView(targetWindow, manager.model().groupForTab(central)); QVERIFY(centralView);
    moveMouse(carrier, centralView->viewportRectGlobal().center());
    QCOMPARE(carrier->dragPresentation(), WorkspaceWindow::DragPresentation::Tab);
    QVERIFY(content->isVisible()); QCOMPARE(carrier->winId(), hwnd);
    QCOMPARE(QApplication::focusWidget(), content); QVERIFY(!carrier->mask().isEmpty());
    moveMouse(carrier, point);
    QCOMPARE(carrier->dragPresentation(), WorkspaceWindow::DragPresentation(int(WorkspaceWindow::DragPresentation::Left) + edge));
    QVERIFY(content->isVisible());
    QTest::keyClick(carrier, Qt::Key_Escape); QApplication::processEvents();
    QCOMPARE(manager.model().toJson(), before); QCOMPARE(manager.paneWidget(pane), content);
    QVERIFY(targetWindow->surface()->edgeInsertionPreviewRect(Edge(edge)).isEmpty()); QVERIFY(!QWidget::mouseGrabber());
}
void DragPresentationTest::wholeWindowPresentationRestoresKeyboardFocus_data()
{
    QTest::addColumn<bool>("cancel"); QTest::newRow("cancel") << true; QTest::newRow("release") << false;
}
void DragPresentationTest::directSaveOmitsIntermediateCarrier_data()
{
    QTest::addColumn<QString>("action");
    QTest::newRow("cancel") << QString("cancel");
    QTest::newRow("commit") << QString("commit");
    QTest::newRow("api-mutation") << QString("api-mutation");
}
void DragPresentationTest::directSaveOmitsIntermediateCarrier()
{
    QFETCH(QString, action);
    WindowManager manager;
    const auto source = manager.createWindow();
    auto* content = new QLineEdit("Live transaction");
    const auto tab = manager.addTab(source, content, {"Moving", "edit", {}});
    const auto pane = manager.model().tab(tab)->activePane;
    const auto target = manager.createWindow(WindowKind::Workbench, {"Target", {700, 450}});
    const auto central = manager.addTab(target, new QLabel("Central"), {"Central", "label", {}});
    const auto side = manager.addTab(target, new QLabel("Side"), {"Side", "label", {}});
    QVERIFY(manager.moveTabToEdge(side, Edge::Left));
    auto* sourceWindow = static_cast<WorkspaceWindow*>(manager.windowWidget(source));
    auto* targetWindow = static_cast<WorkspaceWindow*>(manager.windowWidget(target));
    showFixture(sourceWindow, {20, 20}); showFixture(targetWindow, {70, 70});
    QVERIFY(QTest::qWaitForWindowExposed(targetWindow));
    const auto committed = manager.saveLayout();
    QVector<QJsonObject> snapshots;
    QObject::connect(&manager, &WindowManager::layoutChanged, &manager,
        [&] { snapshots.append(manager.saveLayout()); }, Qt::DirectConnection);
    QPointer<WorkspaceWindow> carrier = beginDrag(manager, source, tab); QVERIFY(carrier);
    const auto carrierId = carrier->id();
    carrier->activateWindow(); content->setFocus(); QTRY_COMPARE(QApplication::focusWidget(), content);
    const auto area = targetWindow->surface()->edgeBarRects().value(Edge::Left);
    const QPoint point(area.center().x(), area.top() + 8);
    moveMouse(carrier, point);
    QCOMPARE(carrier->dragPresentation(), WorkspaceWindow::DragPresentation::Left);
    QVERIFY(!snapshots.isEmpty());
    for (const auto& snapshot : snapshots) QCOMPARE(snapshot, committed);
    if (action == "cancel") QTest::keyClick(carrier, Qt::Key_Escape);
    else if (action == "api-mutation") QVERIFY(manager.moveTab(tab, manager.model().groupForTab(central)));
    else {
        QMouseEvent release(QEvent::MouseButtonRelease, carrier->mapFromGlobal(point), point,
            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(carrier, &release);
    }
    const auto completed = manager.saveLayout();
    if (action == "cancel") QCOMPARE(completed.value("model"), committed.value("model"));
    else {
        const auto* group = manager.model().group(manager.model().groupForTab(tab)); QVERIFY(group);
        QCOMPARE(group->windowId, target);
        QCOMPARE(group->location, action == "commit" ? GroupLocation::AutoHide : GroupLocation::Central);
        QVERIFY(completed.value("model") != committed.value("model"));
    }
    QCOMPARE(snapshots.back(), completed);
    for (const auto& snapshot : snapshots) QVERIFY(snapshot == committed || snapshot == completed);
    QVERIFY(!manager.model().window(carrierId)); QCOMPARE(manager.paneWidget(pane), content);
    QVERIFY(manager.model().tab(central)); QVERIFY(!QWidget::mouseGrabber()); QVERIFY(manager.model().validate());
}
void DragPresentationTest::wholeWindowPresentationRestoresKeyboardFocus()
{
    QFETCH(bool, cancel);
    WindowManager manager; const auto owner = manager.createWindow();
    const auto source = manager.createWindow(WindowKind::Floating, {"Carrier", {700, 450}}, owner);
    auto* content = new QLineEdit("Keyboard"); const auto tab = manager.addTab(source, content, {"Moving", "edit", {}});
    const auto pane = manager.model().tab(tab)->activePane;
    const auto target = manager.createWindow(WindowKind::Workbench, {"Target", {700, 450}});
    const auto side = manager.addTab(target, new QLabel("Side"), {"Side", "label", {}});
    QVERIFY(manager.moveTabToEdge(side, Edge::Left));
    auto* carrier = static_cast<WorkspaceWindow*>(manager.windowWidget(source));
    auto* targetWindow = static_cast<WorkspaceWindow*>(manager.windowWidget(target));
    showFixture(targetWindow, {70, 70}); showFixture(carrier, {20, 20});
    QVERIFY(QTest::qWaitForWindowExposed(carrier)); carrier->activateWindow(); content->setFocus();
    QTRY_COMPARE(QApplication::focusWidget(), content);
    const auto originalGeometry = carrier->normalPlacementGeometry(); const auto hwnd = carrier->winId();
    const auto model = manager.model().toJson(); auto* header = carrier->findChild<QWidget*>("WindowHeader"); QVERIFY(header);
    const auto pressPoint = header->mapToGlobal(header->rect().center());
    QMouseEvent press(QEvent::MouseButtonPress, header->mapFromGlobal(pressPoint), pressPoint, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(header, &press); moveMouse(header, pressPoint + QPoint(60, 40));
    QCOMPARE(QWidget::mouseGrabber(), carrier);
    const auto edge = targetWindow->surface()->edgeBarRects().value(Edge::Left);
    moveMouse(carrier, {edge.center().x(), edge.top() + 8});
    QCOMPARE(carrier->dragPresentation(), WorkspaceWindow::DragPresentation::Left);
    QCOMPARE(QApplication::focusWidget(), content); QCOMPARE(carrier->winId(), hwnd);
    const auto outside = targetWindow->mapToGlobal(QPoint(targetWindow->width() + 200, 100));
    moveMouse(carrier, outside); QCOMPARE(QApplication::focusWidget(), content);
    if (cancel) QTest::keyClick(carrier, Qt::Key_Escape);
    else {
        QMouseEvent release(QEvent::MouseButtonRelease, carrier->mapFromGlobal(outside), outside, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(carrier, &release);
    }
    QApplication::processEvents(); QVERIFY(!QWidget::mouseGrabber());
    QCOMPARE(carrier->dragPresentation(), WorkspaceWindow::DragPresentation::Window);
    QVERIFY(carrier->mask().isEmpty()); QVERIFY(content->isVisible()); QCOMPARE(QApplication::focusWidget(), content);
    QCOMPARE(manager.paneWidget(pane), content); QCOMPARE(manager.model().toJson(), model); QCOMPARE(carrier->winId(), hwnd);
    if (cancel) QCOMPARE(carrier->normalPlacementGeometry(), originalGeometry);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_X); QVERIFY(content->text().contains('x'));
}
void DragPresentationTest::stationaryDragScrollsAndStops_data()
{
    QTest::addColumn<bool>("edge"); QTest::newRow("tabs") << false; QTest::newRow("side") << true;
}
void DragPresentationTest::stationaryDragScrollsAndStops()
{
    QFETCH(bool, edge);
    WindowManager manager; const auto source = manager.createWindow();
    const auto tab = manager.addTab(source, new QLabel("Moving"), {"Moving", "label", {}});
    const auto target = manager.createWindow(WindowKind::Workbench, {"Target", {700, 450}});
    TabId first;
    for (int i = 0; i < 25; ++i) {
        const auto id = manager.addTab(target, new QLabel("target"), {QString("Long target %1").arg(i), "label", {}});
        if (i == 0) first = id;
        if (edge) QVERIFY(manager.moveTabToEdge(id, Edge::Top));
    }
    auto* sourceWindow = static_cast<WorkspaceWindow*>(manager.windowWidget(source));
    auto* targetWindow = static_cast<WorkspaceWindow*>(manager.windowWidget(target));
    showFixture(sourceWindow, {20, 20}); showFixture(targetWindow, {70, 70}); QVERIFY(QTest::qWaitForWindowExposed(targetWindow));
    QVERIFY(manager.activateTab(first));
    // The strip's axis: a rail's scroll bar or the tab strip's lane.
    struct Axis {
        QPointer<QScrollBar> scroll; QPointer<TabBar> bar;
        explicit operator bool() const { return scroll || bar; }
        const Axis* operator->() const { return this; }
        [[nodiscard]] int value() const { return scroll ? scroll->value() : bar->scrollOffset(); }
        [[nodiscard]] int maximum() const { return scroll ? scroll->maximum() : bar->maximumScrollOffset(); }
        void setValue(int value) const { if (scroll) scroll->setValue(value); else bar->setScrollOffset(value); }
    };
    auto currentScrollBar = [&]() -> Axis {
        if (!edge) {
            auto* group = groupView(targetWindow, manager.model().groupForTab(first));
            return {nullptr, group ? group->tabBar() : nullptr};
        }
        for (auto* widget : targetWindow->surface()->findChildren<QWidget*>())
            if (auto* strip = dynamic_cast<EdgeStrip*>(widget); strip && strip->isVisible() && strip->property("edge").toInt() == int(Edge::Top))
                return {strip->findChild<QScrollBar*>("EdgeScrollBar"), nullptr};
        return {};
    };
    auto scrolling = currentScrollBar(); QVERIFY(scrolling);
    QTRY_VERIFY(scrolling->maximum() > 0); scrolling->setValue(0);
    QPointer<WorkspaceWindow> carrier = beginDrag(manager, source, tab); QVERIFY(carrier);
    scrolling = currentScrollBar(); QVERIFY(scrolling);
    QTRY_VERIFY(scrolling->maximum() > 0); scrolling->setValue(0);
    QRect area;
    if (edge) {
        for (auto* widget : targetWindow->surface()->findChildren<QWidget*>())
            if (auto* strip = dynamic_cast<EdgeStrip*>(widget); strip && strip->isVisible()
                && strip->property("edge").toInt() == int(Edge::Top)) area = strip->viewportRectGlobal();
    } else area = groupView(targetWindow, manager.model().groupForTab(first))->viewportRectGlobal();
    QVERIFY(!area.isEmpty());
    moveMouse(carrier, QPoint(area.right() - 6, area.center().y()));
    QTRY_VERIFY_WITH_TIMEOUT(scrolling->value() > 20, 1000);
    const auto beforeMove = scrolling->value();
    moveMouse(carrier, targetWindow->mapToGlobal(QPoint(targetWindow->width() + 200, 100)));
    const auto stopped = scrolling->value(); QTest::qWait(150); QCOMPARE(scrolling->value(), stopped);
    QVERIFY(beforeMove > 0); QTest::keyClick(carrier, Qt::Key_Escape);
}
QTEST_MAIN(DragPresentationTest)
#include "drag_presentation_test.moc"
