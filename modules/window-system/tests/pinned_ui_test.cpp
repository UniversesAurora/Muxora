// SPDX-License-Identifier: MIT
#include "chrome/workspace_window.hpp"
#include "docking/dock_guides.hpp"
#include "docking/dock_surface.hpp"
#include "docking/drag_controller.hpp"
#include "docking/tab_strip.hpp"
#include "fixtures/authored_mouse_input.hpp"
#include <window_system/window_manager.hpp>

#include <QApplication>
#include <QLabel>
#include <QMouseEvent>
#include <QPointer>
#include <QResizeEvent>
#include <QScreen>
#include <QSplitter>
#include <QTest>
#include <memory>
#include <functional>

using namespace window_system;

namespace {
const QVector<Edge> sides{Edge::Left, Edge::Right, Edge::Top, Edge::Bottom};
const QVector<Placement> directions{Placement::Left, Placement::Right, Placement::Top, Placement::Bottom};

QString suffix(Placement placement)
{
    if (placement == Placement::Left) return "left";
    if (placement == Placement::Right) return "right";
    return placement == Placement::Top ? "up" : "down";
}

void edgeDirectionRows()
{
    QTest::addColumn<int>("edge"); QTest::addColumn<int>("direction");
    for (const auto edge : sides) for (const auto placement : directions)
        QTest::newRow(qPrintable(QString("edge-%1-direction-%2").arg(int(edge)).arg(int(placement))))
            << int(edge) << int(placement);
}

QLabel* label(const QString& text)
{
    auto* result = new QLabel(text); result->setMinimumSize(16, 16);
    return result;
}

class ResizeCallbackLabel final : public QLabel {
public:
    explicit ResizeCallbackLabel(const QString& text) : QLabel(text) { setMinimumSize(16, 16); }
    std::function<void()> resized;
protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QLabel::resizeEvent(event);
        const auto callback = resized;
        if (callback) callback();
    }
};

GroupTabs* groupView(WorkspaceWindow* window, GroupId group)
{
    for (auto* widget : window->findChildren<QWidget*>("TabGroup"))
        if (auto* view = dynamic_cast<GroupTabs*>(widget); view && view->isVisible() && view->property("groupId").toUuid() == group)
            return view;
    return nullptr;
}

void showFixture(WindowManager& manager, WindowId id, bool topmost = false)
{
    auto* window = manager.windowWidget(id);
    if (topmost) window->setWindowFlag(Qt::WindowStaysOnTopHint, true);
    const auto available = window->screen()->availableGeometry();
    QRect geometry(QPoint(), QSize(qMin(1100, available.width()), qMin(740, available.height())));
    geometry.moveCenter(available.center()); window->setGeometry(geometry);
    manager.showWindow(id); window->raise();
}

void comparePosition(QRect moved, QRect target, Placement placement)
{
    QVERIFY(!moved.isEmpty()); QVERIFY(!target.isEmpty());
    switch (placement) {
    case Placement::Left: QVERIFY(moved.right() < target.left()); QCOMPARE(moved.top(), target.top()); QCOMPARE(moved.bottom(), target.bottom()); break;
    case Placement::Right: QVERIFY(moved.left() > target.right()); QCOMPARE(moved.top(), target.top()); QCOMPARE(moved.bottom(), target.bottom()); break;
    case Placement::Top: QVERIFY(moved.bottom() < target.top()); QCOMPARE(moved.left(), target.left()); QCOMPARE(moved.right(), target.right()); break;
    case Placement::Bottom: QVERIFY(moved.top() > target.bottom()); QCOMPARE(moved.left(), target.left()); QCOMPARE(moved.right(), target.right()); break;
    case Placement::Center: QFAIL("This fixture requires an explicit split direction.");
    }
}

void settle()
{
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}

void sendDragEvent(QWidget* capture, QEvent::Type type, QPoint global)
{
    test::authoredMouseInput().takePriority();
    const bool release = type == QEvent::MouseButtonRelease;
    QMouseEvent event(type, capture->mapFromGlobal(global), global,
        release ? Qt::LeftButton : Qt::NoButton, release ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(capture, &event);
    test::authoredMouseInput().takePriority(); QCoreApplication::processEvents();
}

DragController* dragController(WindowManager& manager)
{
    for (auto* child : manager.children()) if (auto* controller = dynamic_cast<DragController*>(child)) return controller;
    return nullptr;
}

QSplitter* splitterForNode(WorkspaceWindow* window, QUuid node)
{
    for (auto* split : window->findChildren<QSplitter*>())
        if (split->isVisible() && split->property("splitNodeId").toUuid() == node) return split;
    return nullptr;
}

} // namespace

class PinnedUiTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); }
    void cleanup() { test::authoredMouseInput().finish(); settle(); }
    void createCommandsSplitPinnedGroupsInEveryDirection_data() { edgeDirectionRows(); }
    void createCommandsSplitPinnedGroupsInEveryDirection();
    void dragGuidesSplitPinnedGroupsAndRemainReachableAfterAvoidance_data() { edgeDirectionRows(); }
    void dragGuidesSplitPinnedGroupsAndRemainReachableAfterAvoidance();
    void resizingANestedPinnedBoundaryLeavesOtherRegionsUnchanged_data();
    void resizingANestedPinnedBoundaryLeavesOtherRegionsUnchanged();
    void synchronousExtentSaversMayReplaceTheirOwningUi_data();
    void synchronousExtentSaversMayReplaceTheirOwningUi();
    void resizeCommandUsesCapturedPinnedGroup();
    void restoringPinnedExtentsMayReplaceUiFromContentResize_data();
    void restoringPinnedExtentsMayReplaceUiFromContentResize();
};

void PinnedUiTest::createCommandsSplitPinnedGroupsInEveryDirection()
{
    QFETCH(int, edge); QFETCH(int, direction);
    const auto side = Edge(edge); const auto placement = Placement(direction);
    WindowManager manager;
    manager.setContentFactory([](const TabOptions& options, PaneId) { return label(options.title); });
    const auto id = manager.createWindow();
    const auto central = manager.addTab(id, label("Central"), {"Central", "label", {}});
    auto* original = label("Pinned source");
    const auto target = manager.addTab(id, original, {"Pinned source", "label", {}});
    const auto pane = manager.model().tab(target)->activePane;
    QVERIFY(manager.moveTabToEdge(target, side, true));
    const auto group = manager.model().groupForTab(target);
    showFixture(manager, id); auto* window = static_cast<WorkspaceWindow*>(manager.windowWidget(id));
    QVERIFY(QTest::qWaitForWindowExposed(window)); QVERIFY(manager.activateTab(target));
    QVERIFY(manager.execute("workspace.splitGroup." + suffix(placement), {}, id));
    const auto tree = manager.model().pinnedLayout(id, side); QVERIFY(tree); QVERIFY(!tree->isLeaf());
    GroupId created;
    for (const auto candidate : manager.model().window(id)->edgeGroups)
        if (candidate != group && manager.model().group(candidate)->location == GroupLocation::Pinned) created = candidate;
    QVERIFY(!created.isNull()); QCOMPARE(manager.model().group(created)->edge, side);
    QCOMPARE(manager.model().centralGroups(id), QVector<GroupId>{manager.model().groupForTab(central)});
    QTRY_VERIFY(groupView(window, created)); QTRY_VERIFY(groupView(window, group));
    const auto bounds = window->surface()->groupRects();
    comparePosition(bounds.value(created), bounds.value(group), placement);
    QCOMPARE(manager.paneWidget(pane), original); QVERIFY(original->isVisible());
    QCOMPARE(manager.model().tab(target)->activePane, pane); QVERIFY(manager.model().validate());
}

void PinnedUiTest::dragGuidesSplitPinnedGroupsAndRemainReachableAfterAvoidance()
{
    QFETCH(int, edge); QFETCH(int, direction);
    const auto side = Edge(edge); const auto placement = Placement(direction);
    WindowManager manager; const auto source = manager.createWindow();
    manager.addTab(source, label("Kept"), {"Kept", "label", {}});
    auto* carried = label("Carried"); const auto moving = manager.addTab(source, carried, {"Carried", "label", {}});
    const auto pane = manager.model().tab(moving)->activePane;
    const auto targetWindow = manager.createWindow();
    const auto central = manager.addTab(targetWindow, label("Central"), {"Central", "label", {}});
    const auto pinned = manager.addTab(targetWindow, label("Pinned"), {"Pinned", "label", {}});
    QVERIFY(manager.moveTabToEdge(pinned, side, true)); const auto pinnedGroup = manager.model().groupForTab(pinned);
    QVERIFY(manager.execute("layout.setEdgeExtent", {{"group", pinnedGroup}, {"extent", 100}}, targetWindow));
    showFixture(manager, source); showFixture(manager, targetWindow, true);
    auto* sourceWidget = static_cast<WorkspaceWindow*>(manager.windowWidget(source));
    auto* target = static_cast<WorkspaceWindow*>(manager.windowWidget(targetWindow));
    QVERIFY(QTest::qWaitForWindowExposed(sourceWidget)); QVERIFY(QTest::qWaitForWindowExposed(target));
    auto* controller = dragController(manager); QVERIFY(controller);
    auto* sourceGroup = groupView(sourceWidget, manager.model().groupForTab(moving)); QVERIFY(sourceGroup);
    const auto sourcePoint = sourceGroup->tabBar()->mapToGlobal(sourceGroup->tabBar()->rect().center()) + QPoint(0, 48);
    test::authoredMouseInput().takePriority(); controller->beginTab(moving, sourcePoint);
    QPointer<QWidget> actor(QWidget::mouseGrabber()); QVERIFY(actor); QVERIFY(controller->isActive());
    auto* dock = target->surface(); const auto pinnedRect = dock->groupRects().value(pinnedGroup); QVERIFY(!pinnedRect.isEmpty());
    sendDragEvent(actor, QEvent::MouseMove, pinnedRect.center()); QVERIFY(actor);
    const QRect bounds(dock->mapToGlobal(QPoint()), dock->size());
    const auto guides = dockGuides(bounds, pinnedRect, true, dock->tabBarRects().values(), dock->edgeBarRects());
    QPoint destination; int groupGuideCount = 0;
    for (const auto& guide : guides) if (!guide.toEdge) {
        ++groupGuideCount;
        for (const auto& edgeGuide : guides) if (edgeGuide.toEdge) QVERIFY(!guide.rect.intersects(edgeGuide.rect));
        if (guide.placement == placement) destination = guide.rect.center();
    }
    QCOMPARE(groupGuideCount, 5); QVERIFY(!destination.isNull());
    // For an outer split the advertised target lies inside the displaced
    // cross, beyond the narrow source group. It must still split that group.
    sendDragEvent(actor, QEvent::MouseMove, destination); QVERIFY(actor);
    sendDragEvent(actor, QEvent::MouseButtonRelease, destination);
    const auto created = manager.model().groupForTab(moving);
    QVERIFY(created != pinnedGroup); QCOMPARE(manager.model().group(created)->windowId, targetWindow);
    QCOMPARE(manager.model().group(created)->location, GroupLocation::Pinned); QCOMPARE(manager.model().group(created)->edge, side);
    QCOMPARE(manager.model().centralGroups(targetWindow), QVector<GroupId>{manager.model().groupForTab(central)});
    QTRY_VERIFY(groupView(target, created)); QTRY_VERIFY(groupView(target, pinnedGroup));
    const auto after = dock->groupRects(); comparePosition(after.value(created), after.value(pinnedGroup), placement);
    QCOMPARE(manager.paneWidget(pane), carried); QCOMPARE(manager.model().tab(moving)->activePane, pane);
    QVERIFY(!controller->isActive()); QVERIFY(!QWidget::mouseGrabber()); QVERIFY(manager.model().validate());
}

void PinnedUiTest::resizingANestedPinnedBoundaryLeavesOtherRegionsUnchanged_data()
{
    QTest::addColumn<int>("edge");
    for (const auto side : sides) QTest::newRow(qPrintable(QString::number(int(side)))) << int(side);
}

void PinnedUiTest::resizingANestedPinnedBoundaryLeavesOtherRegionsUnchanged()
{
    QFETCH(int, edge); const auto side = Edge(edge);
    const bool verticalSide = side == Edge::Left || side == Edge::Right;
    WindowManager manager; const auto id = manager.createWindow();
    const auto central = manager.addTab(id, label("Central"), {"Central", "label", {}});
    const auto first = manager.addTab(id, label("First"), {"First", "label", {}});
    const auto second = manager.addTab(id, label("Second"), {"Second", "label", {}});
    QVERIFY(manager.moveTabToEdge(first, side, true));
    QVERIFY(manager.moveTab(second, manager.model().groupForTab(first), verticalSide ? Placement::Bottom : Placement::Right));
    const auto tree = manager.model().pinnedLayout(id, side); QVERIFY(tree); QVERIFY(!tree->isLeaf());
    const auto opposite = side == Edge::Left ? Edge::Right : side == Edge::Right ? Edge::Left
        : side == Edge::Top ? Edge::Bottom : Edge::Top;
    const auto other = manager.addTab(id, label("Other side"), {"Other side", "label", {}}, manager.model().groupForTab(central));
    QVERIFY(manager.moveTabToEdge(other, opposite, true));
    showFixture(manager, id); auto* window = static_cast<WorkspaceWindow*>(manager.windowWidget(id));
    QVERIFY(QTest::qWaitForWindowExposed(window)); settle();
    const auto initial = window->surface()->groupRects();
    const auto centralGroup = manager.model().groupForTab(central), otherGroup = manager.model().groupForTab(other);
    auto* splitter = splitterForNode(window, tree->id); QVERIFY(splitter); QCOMPARE(splitter->count(), 2);
    QPointer<QSplitter> retained(splitter); const auto before = splitter->sizes(); QVERIFY(before[0] > 0 && before[1] > 0);
    const int total = before[0] + before[1]; const int firstExtent = total * 3 / 5;
    splitter->setSizes({firstExtent, total - firstExtent}); const auto resized = splitter->sizes();
    QVERIFY(resized != before);
    QVERIFY(QMetaObject::invokeMethod(splitter, "splitterMoved", Qt::DirectConnection, Q_ARG(int, resized[0]), Q_ARG(int, 1)));
    QVERIFY(retained); QCOMPARE(splitter->sizes(), resized);
    QCOMPARE(window->surface()->groupRects().value(centralGroup), initial.value(centralGroup));
    QCOMPARE(window->surface()->groupRects().value(otherGroup), initial.value(otherGroup));
    const auto currentTree = manager.model().pinnedLayout(id, side);
    QCOMPARE(currentTree->ratio, double(resized[0]) / double(resized[0] + resized[1]));
    QVERIFY(manager.model().validate());
}

void PinnedUiTest::synchronousExtentSaversMayReplaceTheirOwningUi_data()
{
    QTest::addColumn<QString>("mutation");
    QTest::newRow("close-window") << QString("close-window");
    QTest::newRow("destroy-manager") << QString("destroy-manager");
    QTest::newRow("rebuild-surface") << QString("rebuild-surface");
}

void PinnedUiTest::synchronousExtentSaversMayReplaceTheirOwningUi()
{
    QFETCH(QString, mutation);
    auto manager = std::make_unique<WindowManager>(); QPointer<WindowManager> owner(manager.get());
    const auto id = manager->createWindow(), otherWindow = manager->createWindow();
    const auto other = manager->addTab(otherWindow, label("Other window"), {"Other window", "label", {}});
    const auto otherPane = manager->model().tab(other)->activePane; QPointer<QWidget> otherContent(manager->paneWidget(otherPane));
    const auto central = manager->addTab(id, label("Central"), {"Central", "label", {}});
    const auto first = manager->addTab(id, label("First"), {"First", "label", {}});
    const auto second = manager->addTab(id, label("Second"), {"Second", "label", {}});
    QVERIFY(manager->moveTabToEdge(first, Edge::Left, true));
    const auto targetGroup = manager->model().groupForTab(first);
    QVERIFY(manager->moveTab(second, targetGroup, Placement::Right));
    showFixture(*manager, id); QPointer<WorkspaceWindow> window(static_cast<WorkspaceWindow*>(manager->windowWidget(id)));
    QVERIFY(QTest::qWaitForWindowExposed(window)); settle();
    auto* pinned = window->findChild<QSplitter*>("PinnedRegion"); QVERIFY(pinned); QCOMPARE(pinned->count(), 2);
    QPointer<QSplitter> outer(qobject_cast<QSplitter*>(pinned->parentWidget())); QVERIFY(outer);
    QVERIFY(outer->property("splitNodeId").toUuid().isNull());
    const auto sizes = outer->sizes(); QVERIFY(sizes.size() >= 2);
    bool handled = false, changed = false; int callbacks = 0; TabId newlyCreated; QJsonObject afterCallback;
    connect(manager.get(), &WindowManager::layoutChanged, this, [&] {
        ++callbacks; if (handled) return; handled = true;
        if (mutation == "destroy-manager") { manager.reset(); return; }
        if (mutation == "close-window") changed = manager->requestCloseWindow(id);
        else {
            newlyCreated = manager->addTabToNewGroup(id, label("New after save"), {"New after save", "label", {}}, targetGroup, Placement::Bottom);
            changed = !newlyCreated.isNull();
        }
        afterCallback = manager->model().toJson();
    });
    const bool invoked = QMetaObject::invokeMethod(outer, "splitterMoved", Qt::DirectConnection,
        Q_ARG(int, sizes[0]), Q_ARG(int, 1));
    QVERIFY(invoked); QVERIFY(handled); QVERIFY(callbacks >= 1);
    if (mutation == "destroy-manager") {
        QVERIFY(!owner); settle(); QVERIFY(!window); QVERIFY(!outer); QVERIFY(!otherContent); return;
    }
    QVERIFY(owner); QVERIFY(changed); QCOMPARE(manager->model().toJson(), afterCallback);
    QCOMPARE(manager->paneWidget(otherPane), otherContent.data()); QVERIFY(otherContent);
    if (mutation == "close-window") {
        QVERIFY(!manager->model().window(id)); QVERIFY(manager->model().window(otherWindow));
        settle(); QVERIFY(!window); QVERIFY(!outer);
    } else {
        QVERIFY(manager->model().tab(newlyCreated));
        QCOMPARE(manager->model().group(manager->model().groupForTab(newlyCreated))->location, GroupLocation::Pinned);
        QCOMPARE(manager->model().centralGroups(id), QVector<GroupId>{manager->model().groupForTab(central)});
        settle(); QVERIFY(window); QVERIFY(!outer);
        QVERIFY(groupView(window, manager->model().groupForTab(newlyCreated)));
        QCOMPARE(manager->model().toJson(), afterCallback);
    }
    QVERIFY(manager->model().validate());
}

void PinnedUiTest::resizeCommandUsesCapturedPinnedGroup()
{
    WindowManager manager; const auto id = manager.createWindow();
    const auto central = manager.addTab(id, label("Central"), {"Central", "label", {}});
    const auto first = manager.addTab(id, label("First"), {"First", "label", {}});
    const auto second = manager.addTab(id, label("Second"), {"Second", "label", {}});
    QVERIFY(manager.moveTabToEdge(first, Edge::Left, true));
    QVERIFY(manager.moveTab(second, manager.model().groupForTab(first), Placement::Right));
    QVERIFY(manager.activateTab(first)); const auto captured = manager.context(id);
    const auto before = manager.model().pinnedLayout(id, Edge::Left); QVERIFY(before && !before->isLeaf());
    const auto centralTree = manager.model().window(id)->central;
    QVERIFY(manager.activateTab(central));
    QVERIFY(manager.executeInContext("workspace.resizeGroup.right", captured));
    const auto after = manager.model().pinnedLayout(id, Edge::Left);
    QCOMPARE(after->id, before->id); QCOMPARE(after->ratio, before->ratio + 0.05);
    QCOMPARE(manager.model().window(id)->central, centralTree);
    QCOMPARE(manager.context(id).tab, central); QVERIFY(manager.model().validate());
}

void PinnedUiTest::restoringPinnedExtentsMayReplaceUiFromContentResize_data()
{
    QTest::addColumn<int>("edge");
    QTest::addColumn<QString>("mutation");
    for (const auto edge : {Edge::Top, Edge::Left})
        for (const auto& mutation : {QString("destroy-manager"), QString("rebuild-surface"), QString("rebuild-tab"),
                                     QString("close-window")})
            QTest::newRow(qPrintable(QString("edge-%1-%2").arg(int(edge)).arg(mutation))) << int(edge) << mutation;
}

void PinnedUiTest::restoringPinnedExtentsMayReplaceUiFromContentResize()
{
    QFETCH(int, edge); QFETCH(QString, mutation);
    auto manager = std::make_unique<WindowManager>();
    QPointer<WindowManager> owner(manager.get());
    const auto id = manager->createWindow(), survivor = manager->createWindow();
    const auto kept = manager->addTab(survivor, label("Surviving content"), {"Surviving content", "label", {}});
    QPointer<QWidget> keptContent(manager->paneWidget(manager->model().tab(kept)->activePane));
    const auto central = manager->addTab(id, label("Central"), {"Central", "label", {}});
    auto* content = new ResizeCallbackLabel("Resize callback"); QPointer<QWidget> original(content);
    const auto tab = manager->addTab(id, content, {"Resize callback", "label", {}});
    const auto pane = manager->model().tab(tab)->activePane;
    QVERIFY(manager->moveTabToEdge(tab, Edge(edge), true));
    const auto group = manager->model().groupForTab(tab);
    showFixture(*manager, id);
    QPointer<WorkspaceWindow> window(static_cast<WorkspaceWindow*>(manager->windowWidget(id)));
    QVERIFY(QTest::qWaitForWindowExposed(window)); settle();
    QPointer<DockSurface> surface(window->surface());
    QPointer<QSplitter> region;
    auto* tabs = groupView(window, group); QVERIFY(tabs);
    for (auto* splitter : window->findChildren<QSplitter*>("PinnedRegion"))
        if (splitter->isAncestorOf(tabs)) { region = splitter; break; }
    QVERIFY(region);
    const int extent = Edge(edge) == Edge::Top ? region->height() : region->width();
    QVERIFY(extent > 16);
    region->setProperty("preferredExtent", extent + 23);
    QPointer<QWidget> originalHost(content->parentWidget());
    QVERIFY(originalHost); QCOMPARE(originalHost->objectName(), QStringLiteral("PaneHost"));
    bool handled = false, changed = false, uiAliveDuringCallback = false;
    TabId created; QJsonObject afterCallback;
    content->resized = [&] {
        if (handled) return;
        handled = true;
        if (mutation == "destroy-manager") manager.reset();
        else if (mutation == "close-window") changed = manager->requestCloseWindow(id);
        else if (mutation == "rebuild-tab") {
            // Changes the resized tab's own pane tree, so its hosts are rebuilt.
            created = manager->splitPane(tab, pane, label("Split during resize"), Placement::Bottom);
            changed = !created.isNull();
        } else {
            created = manager->addTabToNewGroup(id, label("Created during resize"),
                {"Created during resize", "label", {}}, group, Placement::Right);
            changed = !created.isNull();
        }
        // Deletion must wait until Qt returns from the active child layout.
        uiAliveDuringCallback = window && surface && region && original && originalHost;
        if (owner) afterCallback = manager->model().toJson();
    };
    // Trigger the same extent-restoration path used by a real surface resize,
    // without changing child geometry before its guarded resizeEvent starts.
    QResizeEvent event(surface->size(), surface->size());
    QApplication::sendEvent(surface, &event);
    QVERIFY(handled); QVERIFY(uiAliveDuringCallback);
    if (mutation == "destroy-manager") {
        QVERIFY(!owner); QVERIFY(window); QVERIFY(surface); QVERIFY(original);
        settle(); QVERIFY(!window); QVERIFY(!surface); QVERIFY(!region); QVERIFY(!original); QVERIFY(!originalHost); QVERIFY(!keptContent);
        return;
    }
    QVERIFY(owner); QVERIFY(changed); QCOMPARE(manager->model().toJson(), afterCallback);
    QCOMPARE(manager->paneWidget(manager->model().tab(kept)->activePane), keptContent.data());
    QVERIFY(keptContent);
    if (mutation == "close-window") {
        QVERIFY(!manager->model().window(id)); QVERIFY(manager->model().window(survivor));
        settle(); QVERIFY(!window); QVERIFY(!surface); QVERIFY(!region); QVERIFY(!original); QVERIFY(!originalHost);
    } else if (mutation == "rebuild-tab") {
        // The replaced host is deleted only after Qt leaves the active layout.
        // The surface's splitter structure is unchanged, so its pinned region
        // stays (phase 1.6b).
        QCOMPARE(manager->model().tabForPane(created), tab);
        QCOMPARE(manager->paneWidget(pane), original.data());
        QVERIFY(originalHost); QVERIFY(originalHost->isHidden());
        QVERIFY(original->parentWidget() != originalHost);
        settle(); QVERIFY(window); QVERIFY(surface); QVERIFY(region); QVERIFY(original); QVERIFY(!originalHost);
        QVERIFY(region->isAncestorOf(original));
        QCOMPARE(manager->model().toJson(), afterCallback);
    } else {
        QVERIFY(manager->model().tab(created)); QVERIFY(manager->model().tab(central));
        QCOMPARE(manager->paneWidget(pane), original.data());
        // A new neighbouring group leaves this tab's pane tree unchanged: its
        // host and mounted content are kept (phase 1.5); the surface's own
        // splitters are still replaced after the layout returns.
        QVERIFY(originalHost); QVERIFY(!originalHost->isHidden());
        QCOMPARE(original->parentWidget(), originalHost.data());
        settle(); QVERIFY(window); QVERIFY(surface); QVERIFY(!region); QVERIFY(original); QVERIFY(originalHost);
        QCOMPARE(original->parentWidget(), originalHost.data());
        QCOMPARE(manager->model().toJson(), afterCallback);
        QCOMPARE(manager->model().group(manager->model().groupForTab(created))->location, GroupLocation::Pinned);
    }
    QVERIFY(manager->model().validate());
}

QTEST_MAIN(PinnedUiTest)
#include "pinned_ui_test.moc"
