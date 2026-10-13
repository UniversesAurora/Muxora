// SPDX-License-Identifier: MIT
#include "chrome/workspace_window.hpp"
#include "docking/dock_surface.hpp"
#include "docking/edge_strip.hpp"
#include "docking/dock_guides.hpp"
#include "docking/tab_strip.hpp"
#include <window_system/window_manager.hpp>
#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPointer>
#include <QScrollBar>
#include <QScreen>
#include <QTest>

using namespace window_system;
namespace {
void saveRequestedScreenshot(QWidget* widget, const QString& name)
{
    const auto directory = qEnvironmentVariable("WINDOW_SYSTEM_SCREENSHOT_DIR");
    if (directory.isEmpty()) return;
    QVERIFY(QDir().mkpath(directory));
    QVERIFY(widget->grab().save(QDir(directory).filePath(name)));
}
void moveMouse(QWidget* capture, QPoint global)
{
    QMouseEvent event(QEvent::MouseMove, capture->mapFromGlobal(global), global,
        Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(capture, &event); QApplication::processEvents();
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
    const auto callback = bar->drag;
    callback(index, bar->mapToGlobal(bar->tabRect(index).center()) + QPoint(0, 35));
    return dynamic_cast<WorkspaceWindow*>(QWidget::mouseGrabber());
}
} // namespace
class DragPresentationTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); }
    void gapDisplacesOnlyTheVisualTabs();
    void guidesAvoidVisibleStripsAndShortGroupHeaders_data();
    void guidesAvoidVisibleStripsAndShortGroupHeaders();
    void edgeGuidesStayInTheirOwnSideBand_data();
    void edgeGuidesStayInTheirOwnSideBand();
    void edgeGuidesAcceptDropsBesideStackedGroups_data();
    void edgeGuidesAcceptDropsBesideStackedGroups();
    void guidesTrackWindowGeometryWithoutMouseMovement_data();
    void guidesTrackWindowGeometryWithoutMouseMovement();
    void tabTargetKeepsCarrierAndLiveContent_data();
    void tabTargetKeepsCarrierAndLiveContent();
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
    tabs.setInsertionPreview(1, 96);
    QCOMPARE(tabs.tabBar()->displayTabRect(0), first);
    QCOMPARE(tabs.tabBar()->displayTabRect(1), second.translated(96, 0));
    QCOMPARE(tabs.count(), 3); QCOMPARE(tabs.currentIndex(), current);
    for (int i = 0; i < ids.size(); ++i) QCOMPARE(tabs.tabBar()->tabData(i).toUuid(), ids[i]);
    const auto gap = tabs.insertionPreviewRectGlobal(); QVERIFY(!gap.isEmpty()); QCOMPARE(gap.width(), 96);
    QCOMPARE(tabs.insertionIndex(gap.center()), 1);
    QCOMPARE(tabs.insertionIndex(gap.center()), 1);
    const auto pixel = tabs.tabBar()->grab().toImage();
    const auto point = tabs.tabBar()->insertionPreviewRect().center();
    QCOMPARE(pixel.pixelColor(qRound(point.x() * pixel.devicePixelRatio()), qRound(point.y() * pixel.devicePixelRatio())), defaultTheme().accentSoft);
    tabs.setInsertionPreview(-1, 0);
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
    const auto guides = dockGuides(bounds, group, true, {header});
    QCOMPARE(guides.size(), 13);
    for (const auto& guide : guides) {
        QVERIFY(!guide.rect.intersects(header));
        QVERIFY(bounds.contains(guide.rect));
        if (!guide.toEdge) QVERIFY(group.adjusted(0, 27, 0, 0).contains(guide.rect));
    }
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
    const QList<QRect> tabs{QRect(bounds.left(), bounds.top(), bounds.width(), 27), middle};
    const auto guides = dockGuides(bounds, bounds, true, tabs, rails);
    QCOMPARE(guides.size(), 13);
    int count = 0;
    for (const auto& guide : guides) if (guide.toEdge && guide.edge == Edge(edge)) {
        ++count; QVERIFY(bounds.contains(guide.rect));
        for (const auto& strip : tabs + rails.values()) QVERIFY(!guide.rect.intersects(strip));
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
        true, surface->tabBarRects().values(), surface->edgeBarRects());
    QPoint targetPoint;
    for (const auto& guide : guides) if (guide.toEdge && guide.edge == Edge(edge) && guide.pinned == pinned) {
        targetPoint = guide.rect.center();
        if (edge == int(Edge::Left)) QVERIFY(guide.rect.right() < bounds.left() + 70);
        else QVERIFY(guide.rect.left() > bounds.right() - 70);
        for (const auto& strip : surface->tabBarRects()) QVERIFY(!guide.rect.intersects(strip));
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
        true, surface->tabBarRects().values(), surface->edgeBarRects());
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
    QVERIFY(carrier->findChild<QWidget*>("WindowHeader")->isHidden()); QVERIFY(!carrier->mask().isEmpty());
    QVERIFY(observed && observed->isVisible()); QCOMPARE(manager.paneWidget(pane), content);
    QCOMPARE(manager.model().toJson(), modelDuringDrag); QCOMPARE(group->count(), 2);
    QCOMPARE(manager.saveLayout(), committedLayout);
    QCOMPARE(group->tabBar()->displayTabRect(1).left(), second.left() + group->tabBar()->insertionExtent());
    QVERIFY(!group->insertionPreviewRectGlobal().isEmpty());
    QCOMPARE(group->insertionPreviewRectGlobal().width(), carrier->surface()->dragLabelExtent(tab, false));
    saveRequestedScreenshot(carrier, QString("drag-tab-carrier-%1.png").arg(floating));
    saveRequestedScreenshot(targetWindow, QString("drag-tab-target-%1.png").arg(floating));
    moveMouse(carrier, targetWindow->mapToGlobal(QPoint(targetWindow->width() + 200, 100)));
    QCOMPARE(carrier->dragPresentation(), WorkspaceWindow::DragPresentation::Window);
    QVERIFY(carrier->mask().isEmpty()); QVERIFY(carrier->findChild<QWidget*>("WindowHeader")->isVisible());
    QVERIFY(group->insertionPreviewRectGlobal().isEmpty()); QCOMPARE(carrier->winId(), hwnd);
    moveMouse(carrier, point);
    QMouseEvent release(QEvent::MouseButtonRelease, carrier->mapFromGlobal(point), point, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(carrier, &release); QApplication::processEvents();
    QCOMPARE(manager.model().group(manager.model().groupForTab(a))->tabs, (QVector<TabId>{a, tab, b}));
    QCOMPARE(manager.paneWidget(pane), content); QVERIFY(!manager.model().window(carrierId));
    QVERIFY(groupView(targetWindow, manager.model().groupForTab(a))->insertionPreviewRectGlobal().isEmpty());
    QVERIFY(!QWidget::mouseGrabber()); QVERIFY(manager.model().validate()); Q_UNUSED(before)
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
    auto currentScrollBar = [&]() -> QScrollBar* {
        if (!edge) {
            const auto* group = groupView(targetWindow, manager.model().groupForTab(first));
            return group ? group->findChild<QScrollBar*>("TabScrollBar") : nullptr;
        }
        for (auto* widget : targetWindow->surface()->findChildren<QWidget*>())
            if (auto* strip = dynamic_cast<EdgeStrip*>(widget); strip && strip->isVisible() && strip->property("edge").toInt() == int(Edge::Top))
                return strip->findChild<QScrollBar*>("EdgeScrollBar");
        return nullptr;
    };
    QPointer<QScrollBar> scrolling = currentScrollBar(); QVERIFY(scrolling);
    QTRY_VERIFY(scrolling->maximum() > 0); scrolling->setValue(0);
    QPointer<WorkspaceWindow> carrier = beginDrag(manager, source, tab); QVERIFY(carrier);
    const auto oldScrollBar = scrolling;
    scrolling = currentScrollBar(); QVERIFY(scrolling); QVERIFY(scrolling != oldScrollBar);
    QTRY_VERIFY(scrolling->maximum() > 0); scrolling->setValue(0);
    const auto area = edge ? targetWindow->surface()->edgeBarRects().value(Edge::Top)
        : groupView(targetWindow, manager.model().groupForTab(first))->viewportRectGlobal();
    moveMouse(carrier, QPoint(area.right() - 6, area.center().y()));
    QTRY_VERIFY_WITH_TIMEOUT(scrolling->value() > 20, 1000);
    const auto beforeMove = scrolling->value();
    moveMouse(carrier, targetWindow->mapToGlobal(QPoint(targetWindow->width() + 200, 100)));
    const auto stopped = scrolling->value(); QTest::qWait(150); QCOMPARE(scrolling->value(), stopped);
    QVERIFY(beforeMove > 0); QTest::keyClick(carrier, Qt::Key_Escape);
}
QTEST_MAIN(DragPresentationTest)
#include "drag_presentation_test.moc"
