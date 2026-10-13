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
