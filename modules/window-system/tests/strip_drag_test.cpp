// SPDX-License-Identifier: MIT
#include "chrome/workspace_window.hpp"
#include "docking/dock_surface.hpp"
#include "docking/edge_strip.hpp"
#include "docking/tab_strip.hpp"
#include "authored_mouse_input.hpp"
#include <window_system/window_manager.hpp>

#include <QAbstractButton>
#include <QApplication>
#include <QBoxLayout>
#include <QDir>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <QWheelEvent>
#include <QWindow>
#include <algorithm>
#include <memory>

using namespace window_system;

namespace {
enum class StripKind { Central, Pinned, AutoHide };

void sendMouse(QWidget* receiver, QEvent::Type type, QPoint global,
               Qt::MouseButton button = Qt::NoButton, Qt::MouseButtons buttons = Qt::LeftButton)
{
    test::authoredMouseInput().takePriority();
    QMouseEvent event(type, receiver->mapFromGlobal(global), global, button, buttons, Qt::NoModifier);
    QApplication::sendEvent(receiver, &event);
    // Starting a drag installs another filter; keep the authored stream first.
    test::authoredMouseInput().takePriority();
    QApplication::processEvents();
}

QWidget* labelPreview()
{
    for (auto* widget : QApplication::topLevelWidgets())
        if (widget->objectName() == "DragLabelPreview") return widget;
    return nullptr;
}

GroupTabs* groupView(QWidget* window, GroupId id)
{
    for (auto* widget : window->findChildren<QWidget*>())
        if (auto* group = dynamic_cast<GroupTabs*>(widget);
            group && group->isVisible() && group->property("groupId").toUuid() == id) return group;
    return nullptr;
}

QAbstractButton* edgeButton(QWidget* window, TabId id)
{
    for (auto* button : window->findChildren<QAbstractButton*>())
        if (button->objectName() == "EdgeButton" && button->property("tabId").toUuid() == id) return button;
    return nullptr;
}

EdgeStrip* edgeView(QWidget* window, Edge edge)
{
    for (auto* widget : window->findChildren<QWidget*>())
        if (auto* strip = dynamic_cast<EdgeStrip*>(widget);
            strip && strip->isVisible() && strip->property("edge").toInt() == int(edge)) return strip;
    return nullptr;
}

QRect globalBounds(QWidget* widget) { return {widget->mapToGlobal(QPoint()), widget->size()}; }
bool vertical(Edge edge) { return edge == Edge::Left || edge == Edge::Right; }

WindowOptions lifecycleWindowOptions()
{
    const auto available = qApp->primaryScreen()->availableGeometry();
    return {"Drag lifecycle", {std::min(760, available.width() - 24), std::min(460, available.height() - 24)}};
}

void showLifecycleWindow(WindowManager& manager, WindowId id)
{
    auto* window = manager.windowWidget(id);
    window->setWindowFlag(Qt::WindowStaysOnTopHint, true);
    window->move(qApp->primaryScreen()->availableGeometry().topLeft() + QPoint(12, 12));
    manager.showWindow(id);
}

bool beginCentralLabelDrag(WorkspaceWindow* window, GroupId group, TabId tab)
{
    auto* view = groupView(window, group);
    if (!view) return false;
    auto* bar = view->tabBar();
    for (int index = 0; index < bar->count(); ++index) if (bar->tabData(index).toUuid() == tab) {
        const auto start = bar->mapToGlobal(bar->tabRect(index).center());
        const auto threshold = start + QPoint(20, 0);
        if (!view->viewportRectGlobal().contains(threshold)) return false;
        sendMouse(bar, QEvent::MouseButtonPress, start, Qt::LeftButton);
        sendMouse(bar, QEvent::MouseMove, threshold);
        return labelPreview() && QWidget::mouseGrabber() == window;
    }
    return false;
}

void saveLabelScreenshots(QWidget* source, QWidget* preview, const QString& name)
{
    const auto directory = qEnvironmentVariable("WINDOW_SYSTEM_SCREENSHOT_DIR");
    if (directory.isEmpty()) return;
    QVERIFY(QDir().mkpath(directory));
    QVERIFY(source->grab().save(QDir(directory).filePath(name + "-label-stage.png")));
    QVERIFY(preview->grab().save(QDir(directory).filePath(name + "-label-preview.png")));
}

void addStripRows()
{
    QTest::addColumn<int>("kindValue"); QTest::addColumn<int>("edgeValue");
    QTest::newRow("central") << int(StripKind::Central) << int(Edge::Top);
    for (int edge = 0; edge < 4; ++edge) {
        QTest::newRow(qPrintable(QString("pinned-%1").arg(edge))) << int(StripKind::Pinned) << edge;
        QTest::newRow(qPrintable(QString("auto-hide-%1").arg(edge))) << int(StripKind::AutoHide) << edge;
    }
}

struct Fixture {
    Fixture(StripKind kind, Edge edge, int count = 3) : kind(kind), edge(edge)
    {
        const auto available = qApp->primaryScreen()->availableGeometry();
        const QSize size(std::min(760, available.width() - 24), std::min(460, available.height() - 24));
        id = manager.createWindow(WindowKind::Workbench, {"Strip drag", size});
        if (kind != StripKind::Central) manager.addTab(id, new QLabel("Central"), {"Central", "label", {}});
        for (int index = 0; index < count; ++index) {
            auto* content = new QLabel(QString("Live content %1").arg(index)); contents.append(content);
            const auto title = count == 3 ? QString(QChar('A' + index)) : QString("Long label %1").arg(index);
            tabs.append(manager.addTab(id, content, {title, "label", {}}));
        }
        if (kind != StripKind::Central) {
            ready = manager.moveTabToEdge(tabs[0], edge, kind == StripKind::Pinned);
            for (int index = 1; index < tabs.size(); ++index)
                ready = ready && manager.moveTab(tabs[index], manager.model().groupForTab(tabs[0]));
        }
        sourceGroup = manager.model().groupForTab(tabs[0]);
        window = static_cast<WorkspaceWindow*>(manager.windowWidget(id));
        window->setWindowFlag(Qt::WindowStaysOnTopHint, true);
        window->move(available.topLeft() + QPoint(12, 12));
        manager.showWindow(id); window->raise();
    }
    QRect stripRect() const
    {
        return kind == StripKind::AutoHide ? window->surface()->edgeBarRects().value(edge)
            : window->surface()->tabBarRects().value(sourceGroup);
    }
    QRect viewportRect() const
    {
        if (kind == StripKind::AutoHide) {
            auto* strip = edgeView(window, edge); return strip ? strip->viewportRectGlobal() : QRect{};
        }
        auto* group = groupView(window, sourceGroup);
        return group ? group->viewportRectGlobal() : QRect{};
    }
    QRect labelRect(TabId tab) const
    {
        if (kind == StripKind::AutoHide) {
            auto* button = edgeButton(window, tab); return button ? globalBounds(button) : QRect{};
        }
        auto* group = groupView(window, manager.model().groupForTab(tab));
        if (!group) return {};
        auto* bar = group->tabBar();
        for (int index = 0; index < bar->count(); ++index)
            if (bar->tabData(index).toUuid() == tab) return bar->tabRect(index).translated(bar->mapToGlobal(QPoint()));
        return {};
    }
    QWidget* begin(TabId tab)
    {
        const auto bounds = labelRect(tab); const auto start = bounds.center();
        const auto delta = kind == StripKind::AutoHide && vertical(edge) ? QPoint(0, 20) : QPoint(20, 0);
        QWidget* receiver = kind == StripKind::AutoHide ? static_cast<QWidget*>(edgeButton(window, tab))
            : static_cast<QWidget*>(groupView(window, manager.model().groupForTab(tab))->tabBar());
        if (!receiver || !viewportRect().contains(start + delta)) return nullptr;
        sendMouse(receiver, QEvent::MouseButtonPress, start, Qt::LeftButton);
        sendMouse(receiver, QEvent::MouseMove, start + delta);
        return QWidget::mouseGrabber();
    }
    QPoint endpoint(bool last) const
    {
        const auto viewport = viewportRect();
        return kind == StripKind::AutoHide && vertical(edge)
            ? QPoint(viewport.center().x(), last ? viewport.bottom() - 3 : viewport.top() + 3)
            : QPoint(last ? viewport.right() - 3 : viewport.left() + 3, viewport.center().y());
    }
    QPoint outside() const
    {
        const auto bounds = stripRect();
        if (kind != StripKind::AutoHide) return QPoint(bounds.center().x(), bounds.bottom() + 40);
        switch (edge) {
        case Edge::Left: return QPoint(bounds.right() + 40, bounds.center().y());
        case Edge::Right: return QPoint(bounds.left() - 40, bounds.center().y());
        case Edge::Top: return QPoint(bounds.center().x(), bounds.bottom() + 40);
        case Edge::Bottom: return QPoint(bounds.center().x(), bounds.top() - 40);
        }
        return {};
    }
    QScrollBar* scrollBar() const
    {
        if (kind == StripKind::AutoHide) {
            auto* scroll = edgeView(window, edge)->findChild<QScrollArea*>("EdgeScrollViewport");
            return vertical(edge) ? scroll->verticalScrollBar() : scroll->horizontalScrollBar();
        }
        auto* scroll = groupView(window, sourceGroup)->findChild<QScrollArea*>("TabScrollViewport");
        return scroll->horizontalScrollBar();
    }
    QVector<TabId> order() const
    {
        return kind == StripKind::AutoHide ? manager.model().autoHiddenTabs(id, edge)
            : manager.model().group(manager.model().groupForTab(tabs[0]))->tabs;
    }
    WindowManager manager;
    StripKind kind;
    Edge edge;
    WindowId id;
    GroupId sourceGroup;
    QPointer<WorkspaceWindow> window;
    QVector<TabId> tabs;
    QVector<QPointer<QWidget>> contents;
    bool ready = true;
};
} // namespace

class StripDragTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); }
    void cleanup() {
        test::authoredMouseInput().finish();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    void authoredMouseStreamPreservesInputButtonsKeyboardAndTimers();
    void liveContentStaysInPlaceUntilFirstExit_data();
    void liveContentStaysInPlaceUntilFirstExit();
    void releasingWithinTheOriginalLabelKeepsItsOrderAndNeighbours_data();
    void releasingWithinTheOriginalLabelKeepsItsOrderAndNeighbours();
    void releaseInTheOriginalStripCommitsAnEndpointOrder_data();
    void releaseInTheOriginalStripCommitsAnEndpointOrder();
    void firstExitCreatesACarrierAndReentryKeepsIt_data();
    void firstExitCreatesACarrierAndReentryKeepsIt();
    void firstExitCollapsesTheEmptySourceGroup();
    void stationaryStripDragAutoScrolls_data();
    void stationaryStripDragAutoScrolls();
    void sourceGeometryChangesRefreshThePhaseWithoutMouseMovement_data();
    void sourceGeometryChangesRefreshThePhaseWithoutMouseMovement();
    void closingTheSourceTabFromPreviewDestructionDoesNotResurrectIt();
    void detachmentNotificationMayDestroyTheManager();
    void nestedTargetPickerMayLoseItsCarrierOrManager_data();
    void nestedTargetPickerMayLoseItsCarrierOrManager();
    void tearOutPreservesSourceScroll_data();
    void tearOutPreservesSourceScroll();
    void capturedDragWheelScrollsTheHoveredStrip_data();
    void capturedDragWheelScrollsTheHoveredStrip();
    void leavingAnAutoScrolledStripKeepsItsViewport_data();
    void leavingAnAutoScrolledStripKeepsItsViewport();
    void queuedScrollRestoreYieldsToKeyboard_data();
    void queuedScrollRestoreYieldsToKeyboard();
    void wheelOnAPeekDoesNotScrollTheCoveredStrip();
    void capturedWheelMayDestroyTheManager();
};

void StripDragTest::authoredMouseStreamPreservesInputButtonsKeyboardAndTimers()
{
    class Recorder final : public QObject {
    public:
        int moves = 0, presses = 0, releases = 0, keys = 0;
        bool eventFilter(QObject*, QEvent* event) override {
            if (event->type() == QEvent::MouseMove) ++moves;
            if (event->type() == QEvent::MouseButtonPress) ++presses;
            if (event->type() == QEvent::MouseButtonRelease) ++releases;
            if (event->type() == QEvent::KeyPress) ++keys;
            return false;
        }
    } recorder;
    QWidget widget;
    widget.setMouseTracking(true);
    widget.resize(180, 100);
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));
    widget.installEventFilter(&recorder);
    test::authoredMouseInput().takePriority();
    QTest::mouseMove(widget.windowHandle(), QPoint(30, 30));
    QCOMPARE(recorder.moves, 0);
    QMouseEvent authored(QEvent::MouseMove, QPointF(40, 40), QPointF(widget.mapToGlobal(QPoint(40, 40))),
        Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &authored);
    QCOMPARE(recorder.moves, 1); // Synchronous, before processing any queue.
    QTest::mousePress(&widget, Qt::LeftButton, Qt::NoModifier, QPoint(40, 40));
    QCOMPARE(recorder.presses, 1);
    QTest::mouseRelease(&widget, Qt::LeftButton, Qt::NoModifier, QPoint(40, 40));
    QCOMPARE(recorder.releases, 1);
    QTest::keyClick(&widget, Qt::Key_A);
    QCOMPARE(recorder.keys, 1);
    bool timerRan = false;
    QTimer::singleShot(0, &widget, [&] { timerRan = true; });
    QTRY_VERIFY(timerRan);
    QCOMPARE(recorder.moves, 1);
    test::authoredMouseInput().finish();
    QTest::mouseMove(widget.windowHandle(), QPoint(50, 50));
    QCOMPARE(recorder.moves, 2); // Cleanup restores ordinary QPA delivery.
}

void StripDragTest::queuedScrollRestoreYieldsToKeyboard_data()
{
    QTest::addColumn<int>("edgeValue"); QTest::addColumn<int>("key");
    for (int edge = -1; edge < 4; ++edge)
        for (auto key : {Qt::Key_Home, Qt::Key_End, Qt::Key_Right})
            QTest::newRow(qPrintable(QString("strip-%1-key-%2").arg(edge).arg(int(key)))) << edge << int(key);
}
void StripDragTest::queuedScrollRestoreYieldsToKeyboard()
{
    QFETCH(int, edgeValue); QFETCH(int, key);
    QWidget root; auto* layout = new QVBoxLayout(&root);
    GroupTabs* group = nullptr; EdgeStrip* edge = nullptr; QScrollBar* overlay = nullptr;
    if (edgeValue < 0) {
        group = new GroupTabs(defaultTheme(), &root); layout->addWidget(group);
        for (int index = 0; index < 18; ++index) group->addTab(new QLabel("Content"), {}, QString("Long title %1").arg(index), QUuid::createUuid());
        overlay = group->findChild<QScrollBar*>("TabScrollBar");
    } else {
        edge = new EdgeStrip(Edge(edgeValue), defaultTheme(), &root); layout->addWidget(edge);
        for (int index = 0; index < 18; ++index) edge->addButton(new QPushButton(QString("Long edge title %1").arg(index)));
        overlay = edge->findChild<QScrollBar*>("EdgeScrollBar");
    }
    root.resize(300, 180); root.show(); QVERIFY(QTest::qWaitForWindowExposed(&root));
    QVERIFY(overlay); QTRY_VERIFY(overlay->maximum() > 180);
    const auto effectiveKey = edgeValue >= 0 && vertical(Edge(edgeValue)) && key == Qt::Key_Right ? Qt::Key_Down : Qt::Key(key);
    QScrollBar reference(overlay->orientation());
    reference.setRange(overlay->minimum(), overlay->maximum()); reference.setSingleStep(overlay->singleStep());
    reference.setValue(120); QTest::keyClick(&reference, effectiveKey);
    const int expected = reference.value();
    QVERIFY(expected != 120);
    QSignalSpy action(overlay, &QScrollBar::actionTriggered);
    if (group) group->restoreScrollOffset(120); else edge->restoreScrollOffset(120);
    // Do not let Qt Test's configurable key delay consume the queued restore
    // before the keyboard action. Dispatch input now, then explicitly pump.
    QKeyEvent press(QEvent::KeyPress, effectiveKey, Qt::NoModifier);
    QKeyEvent release(QEvent::KeyRelease, effectiveKey, Qt::NoModifier);
    QApplication::sendEvent(overlay, &press); QApplication::sendEvent(overlay, &release);
    QCOMPARE(action.count(), 1);
    QCOMPARE(overlay->value(), expected);
    QCOMPARE(group ? group->scrollOffset() : edge->scrollOffset(), expected);
    QApplication::processEvents(); QCOMPARE(overlay->value(), expected);
    QCOMPARE(group ? group->scrollOffset() : edge->scrollOffset(), expected);
}

void StripDragTest::wheelOnAPeekDoesNotScrollTheCoveredStrip()
{
    Fixture fixture{StripKind::Central, Edge::Top, 18}; QVERIFY(fixture.ready);
    QVERIFY(QTest::qWaitForWindowExposed(fixture.window));
    const auto hidden = fixture.manager.addTab(fixture.id, new QLabel("Peek content"), {"Peek", {}, {}});
    QVERIFY(fixture.manager.moveTabToEdge(hidden, Edge::Left));
    QVERIFY(fixture.manager.setPeekSizePolicy(hidden, Edge::Left, {420, std::nullopt}));
    QVERIFY(fixture.manager.activateTab(hidden));
    auto* panel = fixture.window->findChild<QWidget*>("PeekPanel"); QVERIFY(panel); QVERIFY(panel->isVisible());
    const auto viewport = fixture.viewportRect();
    const auto global = QPoint(viewport.left() + 80, viewport.center().y());
    QVERIFY(globalBounds(panel).contains(global));
    const int before = fixture.scrollBar()->value();
    QVERIFY(!fixture.window->surface()->scrollStripAt(global, {0, -40}));
    QCOMPARE(fixture.scrollBar()->value(), before);
    const auto other = fixture.manager.addTab(fixture.id, new QLabel("Other"), {"Other", {}, {}});
    QVERIFY(fixture.manager.moveTab(other, fixture.sourceGroup, Placement::Right));
    auto* original = groupView(fixture.window, fixture.sourceGroup); QVERIFY(original);
    auto* sourceScroll = original->findChild<QScrollArea*>("TabScrollViewport")->horizontalScrollBar();
    sourceScroll->setValue(120); const int hiddenOffset = sourceScroll->value();
    QVERIFY(fixture.manager.activateTab(other));
    QVERIFY(fixture.manager.setViewMode(fixture.id, ViewMode::GroupFullscreen));
    QVERIFY(!original->isVisible());
    fixture.window->surface()->scrollStripAt(original->viewportRectGlobal().center(), {0, -40});
    QCOMPARE(sourceScroll->value(), hiddenOffset);
}

void StripDragTest::capturedWheelMayDestroyTheManager()
{
    auto manager = std::make_unique<WindowManager>(); const auto id = manager->createWindow();
    QVector<TabId> tabs;
    for (int index = 0; index < 18; ++index) tabs.append(manager->addTab(id, new QLabel("Content"), {QString("Long title %1").arg(index), {}, {}}));
    QPointer<WorkspaceWindow> window = static_cast<WorkspaceWindow*>(manager->windowWidget(id));
    manager->showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(window));
    auto* group = groupView(window, manager->model().groupForTab(tabs.first())); QVERIFY(group);
    auto* scroll = group->findChild<QScrollArea*>("TabScrollViewport")->horizontalScrollBar();
    scroll->setValue(0);
    QVERIFY(beginCentralLabelDrag(window, manager->model().groupForTab(tabs.first()), tabs.first()));
    QPointer<QWidget> actor = QWidget::mouseGrabber(); QVERIFY(actor);
    const auto global = group->viewportRectGlobal().center();
    QObject::connect(scroll, &QScrollBar::valueChanged, qApp, [&](int) { manager.reset(); });
    QWheelEvent event(actor->mapFromGlobal(global), global, {}, {0, -120}, Qt::LeftButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(actor, &event);
    QVERIFY(!manager); QVERIFY(!QWidget::mouseGrabber()); QVERIFY(!labelPreview());
}

void StripDragTest::tearOutPreservesSourceScroll_data() { addStripRows(); }
void StripDragTest::tearOutPreservesSourceScroll()
{
    QFETCH(int, kindValue); QFETCH(int, edgeValue);
    Fixture fixture{StripKind(kindValue), Edge(edgeValue), 18}; QVERIFY(fixture.ready);
    QVERIFY(QTest::qWaitForWindowExposed(fixture.window));
    auto* scroll = fixture.scrollBar(); QTRY_VERIFY(scroll->maximum() > 400);
    scroll->setValue(std::min(360, scroll->maximum() / 2));
    QApplication::processEvents();
    const int offset = scroll->value(); QVERIFY(offset > 0);
    TabId moving;
    for (const auto tab : fixture.tabs) {
        const auto label = fixture.labelRect(tab);
        const auto delta = fixture.kind == StripKind::AutoHide && vertical(fixture.edge) ? QPoint(0, 20) : QPoint(20, 0);
        if (fixture.viewportRect().contains(label.center() + delta) && fixture.viewportRect().contains(label.center())) {
            moving = tab; break;
        }
    }
    QVERIFY(!moving.isNull());
    const auto pane = fixture.manager.model().tab(moving)->activePane;
    const QPointer<QWidget> content = fixture.manager.paneWidget(pane);
    auto* actor = fixture.begin(moving); QVERIFY(actor);
    QCOMPARE(fixture.scrollBar()->value(), offset);
    sendMouse(actor, QEvent::MouseMove, fixture.outside());
    actor = QWidget::mouseGrabber(); QVERIFY(actor); QVERIFY(actor != fixture.window);
    QTRY_COMPARE(fixture.scrollBar()->value(), std::min(offset, fixture.scrollBar()->maximum()));
    const int detachedOffset = fixture.scrollBar()->value(); QVERIFY(detachedOffset > 0);
    QCOMPARE(fixture.manager.paneWidget(pane), content.data());
    QTest::keyClick(actor, Qt::Key_Escape);
    QTRY_COMPARE(fixture.manager.model().windows().size(), 1);
    QTRY_COMPARE(fixture.scrollBar()->value(), detachedOffset);
    QCOMPARE(fixture.manager.paneWidget(pane), content.data());
    // View reconstruction preserves scrolling; explicit navigation still
    // reveals the chosen tab rather than leaving keyboard selection offscreen.
    if (fixture.kind != StripKind::AutoHide) {
        QVERIFY(fixture.manager.activateTab(fixture.tabs.first()));
        QTRY_COMPARE(fixture.scrollBar()->value(), 0);
    }
}

void StripDragTest::capturedDragWheelScrollsTheHoveredStrip_data()
{
    QTest::addColumn<int>("kindValue"); QTest::addColumn<int>("edgeValue"); QTest::addColumn<bool>("detached");
    for (bool detached : {false, true}) {
        QTest::newRow(qPrintable(QString("central-detached-%1").arg(detached))) << int(StripKind::Central) << int(Edge::Top) << detached;
        for (int edge = 0; edge < 4; ++edge) {
            QTest::newRow(qPrintable(QString("pinned-%1-detached-%2").arg(edge).arg(detached))) << int(StripKind::Pinned) << edge << detached;
            QTest::newRow(qPrintable(QString("auto-hide-%1-detached-%2").arg(edge).arg(detached))) << int(StripKind::AutoHide) << edge << detached;
        }
    }
}
void StripDragTest::capturedDragWheelScrollsTheHoveredStrip()
{
    QFETCH(int, kindValue); QFETCH(int, edgeValue); QFETCH(bool, detached);
    Fixture fixture{StripKind(kindValue), Edge(edgeValue), 18}; QVERIFY(fixture.ready);
    QVERIFY(QTest::qWaitForWindowExposed(fixture.window));
    QTRY_VERIFY(fixture.scrollBar()->maximum() > 400);
    fixture.scrollBar()->setValue(0);
    auto* actor = fixture.begin(fixture.tabs[0]); QVERIFY(actor);
    if (detached) {
        sendMouse(actor, QEvent::MouseMove, fixture.outside());
        actor = QWidget::mouseGrabber(); QVERIFY(actor); QVERIFY(actor != fixture.window);
    }
    const auto global = fixture.viewportRect().center();
    sendMouse(actor, QEvent::MouseMove, global);
    const auto wheel = [&](QPoint pixel, QPoint angle) {
        QWheelEvent event(actor->mapFromGlobal(global), global, pixel, angle, Qt::LeftButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        event.ignore(); QApplication::sendEvent(actor, &event);
        QVERIFY(event.isAccepted()); QApplication::processEvents();
        QCOMPARE(QWidget::mouseGrabber(), actor);
    };
    const auto active = fixture.manager.context(fixture.id).tab;
    const int initial = fixture.scrollBar()->value();
    wheel({}, {0, -120}); QCOMPARE(fixture.scrollBar()->value(), initial + 40);
    const bool verticalStrip = fixture.kind == StripKind::AutoHide && vertical(fixture.edge);
    wheel(verticalStrip ? QPoint(0, -17) : QPoint(-17, 0), {});
    QCOMPARE(fixture.scrollBar()->value(), initial + 57);
    wheel({}, {0, 120}); QCOMPARE(fixture.scrollBar()->value(), initial + 17);
    QCOMPARE(fixture.manager.context(fixture.id).tab, active);
    sendMouse(actor, QEvent::MouseMove, fixture.outside());
    actor = QWidget::mouseGrabber(); QVERIFY(actor);
    QTRY_COMPARE(fixture.scrollBar()->value(), initial + 17);
    QTest::keyClick(actor, Qt::Key_Escape);
    QTRY_COMPARE(fixture.manager.model().windows().size(), 1);
    QTRY_COMPARE(fixture.scrollBar()->value(), initial + 17);
}

void StripDragTest::leavingAnAutoScrolledStripKeepsItsViewport_data() { addStripRows(); }
void StripDragTest::leavingAnAutoScrolledStripKeepsItsViewport()
{
    QFETCH(int, kindValue); QFETCH(int, edgeValue);
    Fixture fixture{StripKind(kindValue), Edge(edgeValue), 18}; QVERIFY(fixture.ready);
    QVERIFY(QTest::qWaitForWindowExposed(fixture.window));
    QTRY_VERIFY(fixture.scrollBar()->maximum() > 400);
    fixture.scrollBar()->setValue(0);
    auto* actor = fixture.begin(fixture.tabs[0]); QVERIFY(actor);
    sendMouse(actor, QEvent::MouseMove, fixture.endpoint(true));
    QTRY_VERIFY(fixture.scrollBar()->value() >= 80);
    const int offset = fixture.scrollBar()->value();
    sendMouse(actor, QEvent::MouseMove, fixture.outside());
    actor = QWidget::mouseGrabber(); QVERIFY(actor); QVERIFY(actor != fixture.window);
    QTRY_COMPARE(fixture.scrollBar()->value(), offset);
    QTest::qWait(100); QCOMPARE(fixture.scrollBar()->value(), offset);
    QTest::keyClick(actor, Qt::Key_Escape);
    QTRY_COMPARE(fixture.manager.model().windows().size(), 1);
    QTRY_COMPARE(fixture.scrollBar()->value(), offset);
}

void StripDragTest::liveContentStaysInPlaceUntilFirstExit_data()
{
    QTest::addColumn<int>("kindValue"); QTest::addColumn<int>("edgeValue"); QTest::addColumn<bool>("peek");
    QTest::newRow("central") << int(StripKind::Central) << int(Edge::Top) << true;
    for (int edge = 0; edge < 4; ++edge) {
        QTest::newRow(qPrintable(QString("pinned-%1").arg(edge))) << int(StripKind::Pinned) << edge << true;
        for (bool peek : {false, true})
            QTest::newRow(qPrintable(QString("auto-hide-%1-peek-%2").arg(edge).arg(peek))) << int(StripKind::AutoHide) << edge << peek;
    }
}

void StripDragTest::liveContentStaysInPlaceUntilFirstExit()
{
    QFETCH(int, kindValue); QFETCH(int, edgeValue); QFETCH(bool, peek);
    Fixture fixture{StripKind(kindValue), Edge(edgeValue)}; QVERIFY(fixture.ready);
    QVERIFY(QTest::qWaitForWindowExposed(fixture.window));
    QVERIFY(qApp->primaryScreen()->availableGeometry().contains(globalBounds(fixture.window)));
    const auto moving = fixture.tabs[1];
    if (fixture.kind != StripKind::AutoHide || peek) QVERIFY(fixture.manager.activateTab(moving));
    const auto pane = fixture.manager.model().tab(moving)->activePane;
    auto* content = fixture.contents[1].data();
    const QPointer<QWidget> parent = content->parentWidget();
    const auto contentBounds = globalBounds(content);
    const auto wasVisible = content->isVisible();
    QCOMPARE(wasVisible, fixture.kind != StripKind::AutoHide || peek);
    QPointer<QWidget> peekPanel = fixture.window->findChild<QWidget*>("PeekPanel");
    const auto before = fixture.manager.model().toJson();
    const auto groupId = fixture.manager.model().groupForTab(moving);
    const auto label = fixture.labelRect(moving);
    auto* actor = fixture.begin(moving); QVERIFY(actor); QCOMPARE(actor, fixture.window.data());
    QPointer<QWidget> preview = labelPreview(); QVERIFY(preview); QVERIFY(preview->isVisible());
    QVERIFY(preview->width() <= label.width() + 16); QVERIFY(preview->height() <= label.height() + 16);
    QVERIFY(!preview->findChild<QWidget*>("DockSurface"));
    bool shadow = false; const auto pixels = preview->grab().toImage(); const auto ratio = pixels.devicePixelRatio();
    for (int y = 0; y < pixels.height(); ++y) for (int x = 0; x < pixels.width(); ++x)
        if ((x < 4 * ratio || y < 4 * ratio) && pixels.pixelColor(x, y).alpha() > 0
            && pixels.pixelColor(x, y).alpha() < 255) shadow = true;
    QVERIFY2(shadow, "The small label preview needs a translucent shadow, without copying the content");
    if (fixture.kind == StripKind::Central)
        saveLabelScreenshots(fixture.window, preview, "central");
    else if (fixture.kind == StripKind::AutoHide && fixture.edge == Edge::Left && peek)
        saveLabelScreenshots(fixture.window, preview, "auto-hide-left-peek");
    const auto last = fixture.endpoint(true); sendMouse(actor, QEvent::MouseMove, last);
    QCOMPARE(fixture.manager.model().windows().size(), 1); QCOMPARE(fixture.manager.model().toJson(), before);
    QCOMPARE(fixture.manager.model().groupForTab(moving), groupId);
    QCOMPARE(fixture.manager.paneWidget(pane), content); QCOMPARE(content->parentWidget(), parent.data());
    QCOMPARE(globalBounds(content), contentBounds); QCOMPARE(content->isVisible(), wasVisible);
    if (peekPanel) { QVERIFY(peekPanel->isVisible()); QVERIFY(peekPanel->isAncestorOf(content)); }
    if (fixture.kind == StripKind::AutoHide) {
        auto* button = edgeButton(fixture.window, moving); QVERIFY(button); QVERIFY(button->isHidden());
        const auto gap = fixture.window->surface()->edgeInsertionPreviewRect(fixture.edge); QVERIFY(!gap.isEmpty());
        auto* third = edgeButton(fixture.window, fixture.tabs[2]); QVERIFY(third);
        if (vertical(fixture.edge)) QVERIFY(globalBounds(third).bottom() < gap.top());
        else QVERIFY(globalBounds(third).right() < gap.left());
    } else {
        auto* bar = groupView(fixture.window, groupId)->tabBar();
        QCOMPARE(bar->count(), 3); QVERIFY(bar->displayTabRect(1).isEmpty());
        const auto gap = bar->insertionPreviewRect(); QVERIFY(!gap.isEmpty());
        QVERIFY(bar->displayTabRect(2).right() < gap.left());
    }
    QTest::keyClick(actor, Qt::Key_Escape);
    QVERIFY(!preview); QVERIFY(!labelPreview()); QVERIFY(!QWidget::mouseGrabber());
    QCOMPARE(fixture.manager.model().toJson(), before); QCOMPARE(content->parentWidget(), parent.data());
    QCOMPARE(content->isVisible(), wasVisible);
    if (fixture.kind == StripKind::AutoHide) {
        QVERIFY(edgeButton(fixture.window, moving)->isVisible());
        QVERIFY(fixture.window->surface()->edgeInsertionPreviewRect(fixture.edge).isEmpty());
    } else QVERIFY(fixture.window->surface()->insertionPreviewRect(groupId).isEmpty());
    QVERIFY(fixture.manager.model().validate());
}

void StripDragTest::releasingWithinTheOriginalLabelKeepsItsOrderAndNeighbours_data() { addStripRows(); }

void StripDragTest::releasingWithinTheOriginalLabelKeepsItsOrderAndNeighbours()
{
    QFETCH(int, kindValue); QFETCH(int, edgeValue);
    Fixture fixture{StripKind(kindValue), Edge(edgeValue)}; QVERIFY(fixture.ready);
    QVERIFY(QTest::qWaitForWindowExposed(fixture.window));
    const auto moving = fixture.tabs[1]; QVERIFY(fixture.manager.activateTab(moving));
    const auto pane = fixture.manager.model().tab(moving)->activePane;
    auto* content = fixture.manager.paneWidget(pane); const QPointer<QWidget> parent = content->parentWidget();
    const auto contentBounds = globalBounds(content);
    const auto before = fixture.manager.model().toJson(); const auto originalOrder = fixture.order();
    const auto groupId = fixture.manager.model().groupForTab(moving);
    const auto firstLabel = fixture.labelRect(fixture.tabs[0]);
    const auto movingLabel = fixture.labelRect(moving);
    const auto lastLabel = fixture.labelRect(fixture.tabs[2]);
    const auto offset = fixture.kind == StripKind::AutoHide && vertical(fixture.edge) ? QPoint(0, 20) : QPoint(20, 0);
    const auto released = movingLabel.center() + offset;
    QVERIFY2(movingLabel.contains(released), "The drag threshold must be reached inside the original label bounds");
    auto* actor = fixture.begin(moving); QVERIFY(actor); QCOMPARE(actor, fixture.window.data()); QVERIFY(labelPreview());
    QCOMPARE(fixture.manager.model().windows().size(), 1); QCOMPARE(fixture.manager.model().toJson(), before);
    QCOMPARE(content->parentWidget(), parent.data()); QCOMPARE(globalBounds(content), contentBounds); QVERIFY(content->isVisible());
    if (fixture.kind == StripKind::AutoHide) {
        QCOMPARE(fixture.window->surface()->edgeInsertionPreviewRect(fixture.edge), movingLabel);
        QCOMPARE(globalBounds(edgeButton(fixture.window, fixture.tabs[0])), firstLabel);
        QCOMPARE(globalBounds(edgeButton(fixture.window, fixture.tabs[2])), lastLabel);
    } else {
        auto* bar = groupView(fixture.window, groupId)->tabBar();
        QCOMPARE(fixture.window->surface()->insertionPreviewRect(groupId), movingLabel);
        QCOMPARE(bar->displayTabRect(0).translated(bar->mapToGlobal(QPoint())), firstLabel);
        QCOMPARE(bar->displayTabRect(2).translated(bar->mapToGlobal(QPoint())), lastLabel);
    }
    // No move beyond B's original rectangle follows the threshold event. A
    // lifted label and replacement gap must retain this exact insertion slot.
    sendMouse(actor, QEvent::MouseButtonRelease, released, Qt::LeftButton, Qt::NoButton);
    QCOMPARE(fixture.order(), originalOrder); QCOMPARE(fixture.manager.model().toJson(), before);
    QCOMPARE(fixture.manager.model().windows().size(), 1); QCOMPARE(fixture.manager.paneWidget(pane), content);
    QVERIFY(content->isVisible()); QVERIFY(!labelPreview()); QVERIFY(!QWidget::mouseGrabber());
    QVERIFY(fixture.manager.model().validate());
}

void StripDragTest::releaseInTheOriginalStripCommitsAnEndpointOrder_data()
{
    QTest::addColumn<int>("kindValue"); QTest::addColumn<int>("edgeValue"); QTest::addColumn<bool>("last");
    for (bool last : {false, true}) {
        QTest::newRow(qPrintable(QString("central-last-%1").arg(last))) << int(StripKind::Central) << int(Edge::Top) << last;
        for (int edge = 0; edge < 4; ++edge) {
            QTest::newRow(qPrintable(QString("pinned-%1-last-%2").arg(edge).arg(last))) << int(StripKind::Pinned) << edge << last;
            QTest::newRow(qPrintable(QString("auto-hide-%1-last-%2").arg(edge).arg(last))) << int(StripKind::AutoHide) << edge << last;
        }
    }
}

void StripDragTest::releaseInTheOriginalStripCommitsAnEndpointOrder()
{
    QFETCH(int, kindValue); QFETCH(int, edgeValue); QFETCH(bool, last);
    Fixture fixture{StripKind(kindValue), Edge(edgeValue)}; QVERIFY(fixture.ready);
    QVERIFY(QTest::qWaitForWindowExposed(fixture.window));
    const auto moving = last ? fixture.tabs.front() : fixture.tabs.back();
    QVERIFY(fixture.manager.activateTab(moving));
    const auto pane = fixture.manager.model().tab(moving)->activePane;
    auto* content = fixture.manager.paneWidget(pane); const auto group = fixture.manager.model().groupForTab(moving);
    auto* actor = fixture.begin(moving); QVERIFY(actor); QCOMPARE(actor, fixture.window.data());
    QVERIFY(labelPreview()); const auto point = fixture.endpoint(last);
    sendMouse(actor, QEvent::MouseMove, point);
    QCOMPARE(fixture.manager.model().windows().size(), 1);
    sendMouse(actor, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
    const auto expected = last ? QVector<TabId>{fixture.tabs[1], fixture.tabs[2], fixture.tabs[0]}
        : QVector<TabId>{fixture.tabs[2], fixture.tabs[0], fixture.tabs[1]};
    QCOMPARE(fixture.order(), expected); QCOMPARE(fixture.manager.model().windows().size(), 1);
    QCOMPARE(fixture.manager.model().groupForTab(moving), group);
    QCOMPARE(fixture.manager.paneWidget(pane), content); QVERIFY(content->isVisible());
    QVERIFY(!labelPreview()); QVERIFY(!QWidget::mouseGrabber()); QVERIFY(fixture.manager.model().validate());
}

void StripDragTest::firstExitCreatesACarrierAndReentryKeepsIt_data() { addStripRows(); }

void StripDragTest::firstExitCreatesACarrierAndReentryKeepsIt()
{
    QFETCH(int, kindValue); QFETCH(int, edgeValue);
    Fixture fixture{StripKind(kindValue), Edge(edgeValue)}; QVERIFY(fixture.ready);
    QVERIFY(QTest::qWaitForWindowExposed(fixture.window)); const auto moving = fixture.tabs[1];
    QVERIFY(fixture.manager.activateTab(moving));
    const auto pane = fixture.manager.model().tab(moving)->activePane;
    auto* content = fixture.manager.paneWidget(pane); const auto before = fixture.manager.model().toJson();
    QPointer<QWidget> peek = fixture.window->findChild<QWidget*>("PeekPanel");
    const auto outside = fixture.outside();
    auto* actor = fixture.begin(moving); QVERIFY(actor); QCOMPARE(actor, fixture.window.data());
    sendMouse(actor, QEvent::MouseMove, outside);
    QCOMPARE(fixture.manager.model().windows().size(), 2); QVERIFY(!labelPreview());
    const auto carriedGroup = fixture.manager.model().group(fixture.manager.model().groupForTab(moving)); QVERIFY(carriedGroup);
    const auto carrierId = carriedGroup->windowId; QVERIFY(carrierId != fixture.id);
    QCOMPARE(fixture.manager.model().window(carrierId)->kind, WindowKind::Floating);
    QPointer<WorkspaceWindow> carrier = static_cast<WorkspaceWindow*>(fixture.manager.windowWidget(carrierId)); QVERIFY(carrier);
    QVERIFY(carrier->isVisible()); QCOMPARE(QWidget::mouseGrabber(), carrier.data());
    QCOMPARE(fixture.manager.paneWidget(pane), content); QVERIFY(carrier->isAncestorOf(content)); QVERIFY(content->isVisible());
    if (fixture.kind == StripKind::AutoHide) QVERIFY(!peek);
    const auto strip = fixture.stripRect(); QVERIFY(!strip.isEmpty()); const auto returning = fixture.endpoint(false);
    sendMouse(carrier, QEvent::MouseMove, returning);
    QCOMPARE(fixture.manager.model().windows().size(), 2); QVERIFY(!labelPreview());
    QCOMPARE(QWidget::mouseGrabber(), carrier.data()); QVERIFY(carrier->isAncestorOf(content));
    QCOMPARE(carrier->dragPresentation(), fixture.kind == StripKind::AutoHide
        ? WorkspaceWindow::DragPresentation(int(WorkspaceWindow::DragPresentation::Left) + int(fixture.edge))
        : WorkspaceWindow::DragPresentation::Tab);
    QTest::keyClick(carrier, Qt::Key_Escape);
    QCOMPARE(fixture.manager.model().toJson(), before); QCOMPARE(fixture.manager.paneWidget(pane), content);
    QVERIFY(!labelPreview()); QVERIFY(!QWidget::mouseGrabber()); QVERIFY(fixture.manager.model().validate());
}

void StripDragTest::firstExitCollapsesTheEmptySourceGroup()
{
    Fixture fixture{StripKind::Central, Edge::Top}; QVERIFY(fixture.ready);
    const auto moving = fixture.tabs.back(); const auto keeper = fixture.tabs.front();
    QVERIFY(fixture.manager.moveTab(moving, fixture.manager.model().groupForTab(keeper), Placement::Right));
    QVERIFY(QTest::qWaitForWindowExposed(fixture.window)); QVERIFY(fixture.manager.activateTab(moving));
    const auto sourceGroup = fixture.manager.model().groupForTab(moving);
    QPointer<GroupTabs> source = groupView(fixture.window, sourceGroup); QVERIFY(source);
    auto* bar = source->tabBar(); const auto start = fixture.labelRect(moving).center();
    sendMouse(bar, QEvent::MouseButtonPress, start, Qt::LeftButton);
    sendMouse(bar, QEvent::MouseMove, start + QPoint(20, 0));
    QCOMPARE(fixture.manager.model().windows().size(), 1); QVERIFY(fixture.manager.model().group(sourceGroup));
    QVERIFY(source->isVisible()); QVERIFY(labelPreview());
    const auto strip = fixture.window->surface()->tabBarRects().value(sourceGroup);
    const auto outside = QPoint(strip.center().x(), strip.bottom() + 40);
    sendMouse(fixture.window, QEvent::MouseMove, outside);
    QCOMPARE(fixture.manager.model().windows().size(), 2); QVERIFY(!fixture.manager.model().group(sourceGroup));
    QCOMPARE(fixture.manager.model().centralGroups(fixture.id).size(), 1);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QVERIFY(!source);
    auto* actor = QWidget::mouseGrabber(); QVERIFY(actor);
    QTest::keyClick(actor, Qt::Key_Escape);
    QCOMPARE(fixture.manager.model().groupForTab(moving), sourceGroup);
    QCOMPARE(fixture.manager.model().centralGroups(fixture.id).size(), 2);
    QVERIFY(fixture.manager.model().validate());
}

void StripDragTest::stationaryStripDragAutoScrolls_data()
{
    QTest::addColumn<int>("kindValue"); QTest::addColumn<int>("edgeValue");
    QTest::newRow("central") << int(StripKind::Central) << int(Edge::Top);
    for (int edge = 0; edge < 4; ++edge)
        QTest::newRow(qPrintable(QString("auto-hide-%1").arg(edge))) << int(StripKind::AutoHide) << edge;
}

void StripDragTest::stationaryStripDragAutoScrolls()
{
    QFETCH(int, kindValue); QFETCH(int, edgeValue);
    Fixture fixture{StripKind(kindValue), Edge(edgeValue), 25}; QVERIFY(fixture.ready);
    QVERIFY(QTest::qWaitForWindowExposed(fixture.window));
    QVERIFY(fixture.manager.activateTab(fixture.tabs.front()));
    auto* scrolling = fixture.scrollBar(); QVERIFY(scrolling); QTRY_VERIFY(scrolling->maximum() > 0); scrolling->setValue(0);
    const auto before = fixture.manager.model().toJson();
    auto* actor = fixture.begin(fixture.tabs.front()); QVERIFY(actor); QCOMPARE(actor, fixture.window.data());
    const auto point = fixture.endpoint(true); sendMouse(actor, QEvent::MouseMove, point);
    QTRY_VERIFY_WITH_TIMEOUT(scrolling->value() > 20, 1500);
    QCOMPARE(fixture.manager.model().windows().size(), 1); QCOMPARE(fixture.manager.model().toJson(), before);
    QVERIFY(labelPreview());
    const auto gap = fixture.kind == StripKind::AutoHide ? fixture.window->surface()->edgeInsertionPreviewRect(fixture.edge)
        : fixture.window->surface()->insertionPreviewRect(fixture.manager.model().groupForTab(fixture.tabs.front()));
    QVERIFY(!gap.isEmpty()); QVERIFY(fixture.stripRect().intersects(gap));
    QTest::keyClick(actor, Qt::Key_Escape); QVERIFY(!labelPreview()); QVERIFY(!QWidget::mouseGrabber());
    const auto stopped = scrolling->value(); QTest::qWait(150); QCOMPARE(scrolling->value(), stopped);
    QCOMPARE(fixture.manager.model().toJson(), before); QVERIFY(fixture.manager.model().validate());
}

void StripDragTest::sourceGeometryChangesRefreshThePhaseWithoutMouseMovement_data()
{
    QTest::addColumn<int>("kindValue"); QTest::addColumn<QString>("change");
    for (int kind : {int(StripKind::Central), int(StripKind::AutoHide)}) for (const auto* change : {"move", "resize"})
        QTest::newRow(qPrintable(QString("kind-%1-%2").arg(kind).arg(change))) << kind << QString(change);
}

void StripDragTest::sourceGeometryChangesRefreshThePhaseWithoutMouseMovement()
{
    QFETCH(int, kindValue); QFETCH(QString, change);
    Fixture fixture{StripKind(kindValue), Edge::Top}; QVERIFY(fixture.ready);
    QVERIFY(QTest::qWaitForWindowExposed(fixture.window));
    const auto moving = fixture.tabs[1]; QVERIFY(fixture.manager.activateTab(moving));
    const auto pane = fixture.manager.model().tab(moving)->activePane; auto* content = fixture.manager.paneWidget(pane);
    auto* actor = fixture.begin(moving); QVERIFY(actor); QCOMPARE(actor, fixture.window.data()); QVERIFY(labelPreview());
    const auto oldStrip = fixture.stripRect();
    if (change == "move") fixture.window->move(fixture.window->pos() + QPoint(24, 32));
    else {
        const auto point = fixture.endpoint(true); sendMouse(actor, QEvent::MouseMove, point);
        fixture.window->resize(fixture.window->width() - 120, fixture.window->height());
    }
    // Keep the last drag position stationary. Move/resize alone must refresh
    // the source strip and begin detachment once that position is outside it.
    QTRY_COMPARE(fixture.manager.model().windows().size(), 2); QVERIFY(!labelPreview());
    QVERIFY(fixture.stripRect() != oldStrip);
    const auto carried = fixture.manager.model().group(fixture.manager.model().groupForTab(moving)); QVERIFY(carried);
    auto* carrier = fixture.manager.windowWidget(carried->windowId); QVERIFY(carrier);
    QCOMPARE(QWidget::mouseGrabber(), carrier); QCOMPARE(fixture.manager.paneWidget(pane), content);
    QVERIFY(carrier->isAncestorOf(content));
    QTest::keyClick(carrier, Qt::Key_Escape); QVERIFY(!QWidget::mouseGrabber()); QVERIFY(fixture.manager.model().validate());
}

void StripDragTest::closingTheSourceTabFromPreviewDestructionDoesNotResurrectIt()
{
    Fixture fixture{StripKind::Central, Edge::Top}; QVERIFY(fixture.ready);
    QVERIFY(QTest::qWaitForWindowExposed(fixture.window));
    const auto moving = fixture.tabs[1]; QVERIFY(fixture.manager.activateTab(moving));
    const auto pane = fixture.manager.model().tab(moving)->activePane;
    QPointer<QWidget> content = fixture.manager.paneWidget(pane);
    const auto outside = fixture.outside();
    auto* actor = fixture.begin(moving); QVERIFY(actor); QCOMPARE(actor, fixture.window.data());
    QPointer<QWidget> preview = labelPreview(); QVERIFY(preview);
    bool closed = false; int destroyedCalls = 0;
    connect(preview, &QObject::destroyed, this, [&] {
        ++destroyedCalls;
        closed = fixture.manager.execute("workspace.closeTab", {}, fixture.id);
    });
    sendMouse(actor, QEvent::MouseMove, outside);
    QVERIFY(closed); QCOMPARE(destroyedCalls, 1); QVERIFY(!preview); QVERIFY(!labelPreview());
    QCOMPARE(fixture.manager.model().windows().size(), 1); QVERIFY(!fixture.manager.model().tab(moving));
    QCOMPARE(fixture.order(), (QVector<TabId>{fixture.tabs[0], fixture.tabs[2]}));
    QVERIFY(!fixture.manager.paneWidget(pane)); QVERIFY(!QWidget::mouseGrabber());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QVERIFY(!content);
    QVERIFY(fixture.manager.model().validate());
}

void StripDragTest::detachmentNotificationMayDestroyTheManager()
{
    auto manager = std::make_unique<WindowManager>();
    QPointer<WindowManager> weakManager = manager.get();
    const auto source = manager->createWindow(WindowKind::Workbench, lifecycleWindowOptions());
    manager->addTab(source, new QLabel("Kept"), {"Kept", "label", {}});
    QPointer<QWidget> content = new QLabel("Moving");
    const auto moving = manager->addTab(source, content, {"Moving", "label", {}});
    const auto group = manager->model().groupForTab(moving);
    QPointer<WorkspaceWindow> window = static_cast<WorkspaceWindow*>(manager->windowWidget(source));
    showLifecycleWindow(*manager, source); QVERIFY(QTest::qWaitForWindowExposed(window));
    QVERIFY(beginCentralLabelDrag(window, group, moving));
    QPointer<QWidget> preview = labelPreview(); QVERIFY(preview);
    const auto bounds = window->surface()->tabBarRects().value(group);
    const QPoint outside(bounds.center().x(), bounds.bottom() + 40);
    QPointer<QWidget> carrier; bool destroyedDuringDetachment = false;
    connect(manager.get(), &WindowManager::layoutChanged, this, [&] {
        if (!manager || manager->model().windows().size() != 2) return;
        const auto* moved = manager->model().group(manager->model().groupForTab(moving));
        if (!moved || moved->windowId == source) return;
        carrier = manager->windowWidget(moved->windowId);
        destroyedDuringDetachment = true;
        manager.reset();
    });
    sendMouse(window, QEvent::MouseMove, outside);
    QVERIFY(destroyedDuringDetachment); QVERIFY(!manager); QVERIFY(!weakManager);
    QVERIFY(!window); QVERIFY(!carrier); QVERIFY(!content); QVERIFY(!preview);
    QVERIFY(!labelPreview()); QVERIFY(!QWidget::mouseGrabber());
}

void StripDragTest::nestedTargetPickerMayLoseItsCarrierOrManager_data()
{
    QTest::addColumn<bool>("destroyManager");
    QTest::newRow("close-carrier") << false;
    QTest::newRow("destroy-manager") << true;
}

void StripDragTest::nestedTargetPickerMayLoseItsCarrierOrManager()
{
    QFETCH(bool, destroyManager);
    auto manager = std::make_unique<WindowManager>();
    QPointer<WindowManager> weakManager = manager.get();
    const auto source = manager->createWindow(WindowKind::Workbench, lifecycleWindowOptions());
    const auto kept = manager->addTab(source, new QLabel("Kept"), {"Kept", "label", {}});
    QPointer<QWidget> content = new QLabel("Moving");
    const auto moving = manager->addTab(source, content, {"Moving", "label", {}});
    const auto group = manager->model().groupForTab(moving);
    QPointer<WorkspaceWindow> sourceWindow = static_cast<WorkspaceWindow*>(manager->windowWidget(source));
    showLifecycleWindow(*manager, source); QVERIFY(QTest::qWaitForWindowExposed(sourceWindow));
    QVERIFY(beginCentralLabelDrag(sourceWindow, group, moving));
    const auto bounds = sourceWindow->surface()->tabBarRects().value(group);
    sendMouse(sourceWindow, QEvent::MouseMove, QPoint(bounds.center().x(), bounds.bottom() + 40));
    const auto carriedGroup = manager->model().group(manager->model().groupForTab(moving)); QVERIFY(carriedGroup);
    const auto carrierId = carriedGroup->windowId; QVERIFY(carrierId != source);
    QPointer<QWidget> carrier = manager->windowWidget(carrierId); QVERIFY(carrier);
    QCOMPARE(QWidget::mouseGrabber(), carrier.data());
    QPointer<QMenu> popup;
    bool mutationRan = false; bool pickerWasVisible = false; bool closeSucceeded = false;
    QTimer mutate; mutate.setSingleShot(true);
    connect(&mutate, &QTimer::timeout, this, [&] {
        popup = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        pickerWasVisible = popup && popup->isVisible();
        mutationRan = true;
        if (destroyManager) manager.reset();
        else closeSucceeded = manager->execute("window.close", {}, carrierId);
    });
    // A failing implementation must not hang the rest of the GUI suites. The
    // watchdog closes only this test's menu and records a failed assertion.
    bool watchdogRan = false;
    QTimer watchdog; watchdog.setSingleShot(true);
    connect(&watchdog, &QTimer::timeout, this, [&] {
        watchdogRan = true;
        if (popup) popup->close();
        else if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) menu->close();
    });
    mutate.start(0); watchdog.start(3000);
    const bool commandSucceeded = manager->execute("window.drag.selectTarget", {}, carrierId);
    watchdog.stop();
    QVERIFY(mutationRan); QVERIFY(pickerWasVisible);
    QVERIFY2(!watchdogRan, "Losing the carrier or manager must leave the nested picker without a manual close");
    QVERIFY(!popup); QVERIFY(!QApplication::activePopupWidget()); QVERIFY(!QWidget::mouseGrabber());
    QVERIFY(!labelPreview());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QVERIFY(!carrier); QVERIFY(!content);
    if (destroyManager) {
        QVERIFY(!manager); QVERIFY(!weakManager); QVERIFY(!sourceWindow); QVERIFY(!commandSucceeded);
    } else {
        QVERIFY(manager); QVERIFY(weakManager); QVERIFY(sourceWindow); QVERIFY(closeSucceeded); QVERIFY(commandSucceeded);
        QCOMPARE(manager->model().windows().size(), 1); QVERIFY(manager->model().tab(kept));
        QVERIFY(!manager->model().tab(moving)); QVERIFY(manager->model().validate());
    }
}

QTEST_MAIN(StripDragTest)
#include "strip_drag_test.moc"
