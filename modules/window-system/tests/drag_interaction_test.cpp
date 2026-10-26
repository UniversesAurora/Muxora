// SPDX-License-Identifier: MIT
#include <window_system/window_manager.hpp>
#include "chrome/workspace_window.hpp"
#include "chrome/command_strip.hpp"
#include "docking/dock_surface.hpp"
#include "docking/dock_guides.hpp"
#include <QAbstractButton>
#include <QApplication>
#include <QCursor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMouseEvent>
#include <QPointer>
#include <QProcess>
#include <QScreen>
#include "docking/tab_bar.hpp"
#include <QStackedWidget>
#include <QTest>
#include <QTemporaryDir>

using namespace window_system;
namespace {
void mouse(QWidget* widget, QEvent::Type type, QPoint global,
           Qt::MouseButton button, Qt::MouseButtons buttons)
{
    // This fixture injects Qt mouse events. It intentionally leaves the live OS
    // cursor elsewhere: queued events must be hit-tested at their own position.
    QMouseEvent event(type, widget->mapFromGlobal(global), global, button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(widget, &event);
    QCoreApplication::processEvents();
}
TabBar* tabBar(QWidget* window, TabId tab)
{
    for (auto* bar : window->findChildren<TabBar*>()) if (bar->isVisible())
        for (int i = 0; i < bar->count(); ++i)
            if (bar->tabData(i).toUuid() == tab) return bar;
    return nullptr;
}
QPoint tabPoint(TabBar* bar, TabId tab)
{
    for (int i = 0; i < bar->count(); ++i)
        if (bar->tabData(i).toUuid() == tab) return bar->mapToGlobal(bar->tabRect(i).center());
    return {};
}
void startTabDrag(QWidget* window, TabBar* bar, TabId tab)
{
    const auto point = tabPoint(bar, tab);
    mouse(bar, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
    mouse(bar, QEvent::MouseMove, point + QPoint(20, 20), Qt::NoButton, Qt::LeftButton);
    Q_UNUSED(window)
}
QAbstractButton* edgeTabButton(QWidget* window, TabId tab)
{
    for (auto* button : window->findChildren<QAbstractButton*>())
        if (button->isVisible() && button->objectName() == "EdgeButton" && button->property("tabId").toUuid() == tab)
            return button;
    return nullptr;
}
void startEdgeTabDrag(QAbstractButton* button)
{
    const auto position = button->mapToGlobal(button->rect().center());
    mouse(button, QEvent::MouseButtonPress, position, Qt::LeftButton, Qt::LeftButton);
    mouse(button, QEvent::MouseMove, position + QPoint(20, 20), Qt::NoButton, Qt::LeftButton);
}
// The center of a group's content, below its strip, where its drop guides
// are centered.
QPoint contentCenter(QWidget* window)
{
    for (auto* frame : window->findChildren<QWidget*>())
        if (frame->objectName() == "TabGroup" && frame->isVisible())
            if (auto* stack = frame->findChild<QStackedWidget*>(QString(), Qt::FindDirectChildrenOnly))
                return stack->mapToGlobal(stack->rect().center());
    return window->mapToGlobal(window->rect().center());
}
QRect groupRect(QWidget* window, GroupId group)
{
    for (auto* widget : window->findChildren<QWidget*>())
        if (widget->objectName() == "TabGroup" && widget->property("groupId").toUuid() == group && widget->isVisible())
            return QRect(widget->mapToGlobal(QPoint()), widget->size());
    return {};
}
void flushDeletes() { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }
void prepareDesktopFixture(QWidget* window)
{
    // These windows belong only to the test process. Keep the controlled test
    // area above unrelated applications; the foreign opaque/transparent fixtures
    // still sit above it and verify the actual occlusion rules independently.
    window->setWindowFlag(Qt::WindowStaysOnTopHint, true);
}

class OverlayProcess final {
public:
    ~OverlayProcess()
    {
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(2000);
        }
    }

    bool start(QRect geometry, const QString& readyPath, bool opaque)
    {
        QString program = QCoreApplication::applicationDirPath() + "/window-system-overlay-fixture";
#ifdef Q_OS_WIN
        program += ".exe";
#endif
        if (!QFileInfo::exists(program)) return false;
        QStringList arguments{"--x", QString::number(geometry.x()), "--y", QString::number(geometry.y()),
                              "--width", QString::number(geometry.width()), "--height", QString::number(geometry.height()),
                              "--ready", readyPath};
        if (opaque) arguments.append("--opaque");
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start(program, arguments);
        return process.waitForStarted(2000);
    }

    QProcess process;
};

QJsonObject overlayReport(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
}
class DragInteractionTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); }
    void cleanup() { flushDeletes(); }
    void tearOffUsesTheDefaultLightweightWindow();
    void escapeCancelsBeforeAnyContentTransfer();
    void actualTabBarDragMergesAcrossWindows();
    void floatingTitleDragMovesAllTabsTogether();
    void foreignClickThroughOverlayDoesNotBlockTabDrop();
    void foreignOpaqueOverlayBlocksTabDropWithoutLosingContent();
    void tearOffIsVisibleBeforeReleaseAndCollapsesTheEmptyGroup();
    void contentBodyDoesNotAcceptAnImplicitMerge();
    void narrowGroupRecognizesDirectionalGuides_data();
    void narrowGroupRecognizesDirectionalGuides();
    void edgeGuidesSeparateHiddenAndPinnedDrops_data();
    void edgeGuidesSeparateHiddenAndPinnedDrops();
    void tabStripDropInsertsAtTheReleasedPosition();
    void wholeWindowDropInsertsAllTabsAtTheReleasedPosition();
    void draggingThePeekTitleImmediatelyRemovesThePeek();
    void pinnedGroupAcceptsDirectionalDrops_data();
    void pinnedGroupAcceptsDirectionalDrops();
    void cancelFromAnEmptiedFloatingWindowRestoresItsToolbarPreferences();
    void sideBarDragReordersAcrossInternalGroups_data();
    void sideBarDragReordersAcrossInternalGroups();
    void sideBarDragAcrossWindowsKeepsTheOriginalContent();
    void sideBarDragEscapeRestoresAnEmptiedSourceGroup();
    void cancelFromMaximizedWindowPreservesItsNormalPlacement_data();
    void cancelFromMaximizedWindowPreservesItsNormalPlacement();
};
void DragInteractionTest::cancelFromMaximizedWindowPreservesItsNormalPlacement_data()
{
    QTest::addColumn<bool>("whole");
    QTest::newRow("workbench-tab") << false;
    QTest::newRow("floating-title") << true;
}
void DragInteractionTest::cancelFromMaximizedWindowPreservesItsNormalPlacement()
{
    QFETCH(bool, whole);
    WindowManager manager; const auto root = manager.createWindow();
    const auto source = whole ? manager.createWindow(WindowKind::Floating, {}, root) : root;
    const auto kept = manager.addTab(source, new QLabel("Kept"), {"Kept", "label", {}});
    auto* content = new QLabel("Live content");
    const auto tab = manager.addTab(source, content, {"Live", "label", {}});
    const auto pane = manager.model().tab(tab)->activePane;
    auto* window = static_cast<WorkspaceWindow*>(manager.windowWidget(source));
    prepareDesktopFixture(window);
    const QRect normal(150, 140, 900, 650);
    window->restorePlacement(normal, true); QVERIFY(QTest::qWaitForWindowExposed(window));
    QCOMPARE(window->normalPlacementGeometry(), normal);
    const auto model = manager.model().toJson();
    if (whole) {
        auto* title = window->findChild<QLabel*>("CaptionTitle"); QVERIFY(title);
        const auto point = title->mapToGlobal(title->rect().center());
        mouse(title, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
        mouse(title, QEvent::MouseMove, point + QPoint(32, 20), Qt::NoButton, Qt::LeftButton);
    } else {
        auto* bar = tabBar(window, tab); QVERIFY(bar); startTabDrag(window, bar, tab);
    }
    auto* actor = QWidget::mouseGrabber(); QVERIFY(actor);
    QTest::keyClick(actor, Qt::Key_Escape);
    QCOMPARE(manager.model().toJson(), model);
    QCOMPARE(manager.paneWidget(pane), content); QVERIFY(manager.model().tab(kept));
    QTRY_VERIFY(window->isMaximized()); QCOMPARE(window->normalPlacementGeometry(), normal);
    window->showNormal(); QTRY_COMPARE(window->geometry(), normal);
}

void DragInteractionTest::tearOffUsesTheDefaultLightweightWindow()
{
    WindowManager manager;
    const auto root = manager.createWindow(WindowKind::Workbench, {"Drag source", QSize(640, 430)});
    auto* content = new QLabel("Stable live content"); QPointer<QWidget> pointer(content);
    const auto tab = manager.addTab(root, content, {"Tab", "label", {}});
    auto* window = manager.windowWidget(root); prepareDesktopFixture(window);
    window->move(25, 25); manager.showWindow(root);
    QTRY_VERIFY(window->isVisible());
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto* bar = tabBar(window, tab); QVERIFY(bar);
    startTabDrag(window, bar, tab);
    const auto area = qApp->primaryScreen()->availableGeometry();
    const auto outside = area.bottomRight() - QPoint(40, 40);
    mouse(window, QEvent::MouseMove, outside, Qt::NoButton, Qt::LeftButton);
    mouse(window, QEvent::MouseButtonRelease, outside, Qt::LeftButton, Qt::NoButton);
    QCOMPARE(manager.model().windows().size(), 2);
    const auto group = manager.model().group(manager.model().groupForTab(tab)); QVERIFY(group);
    const auto* floating = manager.model().window(group->windowId);
    QCOMPARE(floating->kind, WindowKind::Floating);
    QCOMPARE(floating->familyId, manager.model().window(root)->familyId);
    QVERIFY(pointer); QCOMPARE(manager.paneWidget(manager.model().tab(tab)->activePane), content);
}
void DragInteractionTest::escapeCancelsBeforeAnyContentTransfer()
{
    WindowManager manager; const auto root = manager.createWindow();
    const auto tab = manager.addTab(root, new QLabel("Cancel retains me"), {"Cancel", "label", {}});
    auto* window = manager.windowWidget(root); prepareDesktopFixture(window); manager.showWindow(root);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto* bar = tabBar(window, tab); QVERIFY(bar);
    const auto group = manager.model().groupForTab(tab);
    startTabDrag(window, bar, tab);
    QCOMPARE(manager.model().windows().size(), 2);
    QVERIFY(manager.model().groupForTab(tab) != group);
    QTest::keyClick(window, Qt::Key_Escape);
    QCOMPARE(manager.model().windows().size(), 1);
    QCOMPARE(manager.model().groupForTab(tab), group);
    QVERIFY(QWidget::mouseGrabber() != window);
}
void DragInteractionTest::tearOffIsVisibleBeforeReleaseAndCollapsesTheEmptyGroup()
{
    WindowManager manager;
    const auto root = manager.createWindow(WindowKind::Workbench, {"Immediate", QSize(920, 600)});
    const auto a = manager.addTab(root, new QLabel("A"), {"A", "label", {}});
    auto* live = new QLabel("Live B"); QPointer<QWidget> preserved(live);
    const auto b = manager.addTab(root, live, {"B", "label", {}});
    QVERIFY(manager.moveTab(b, manager.model().groupForTab(a), Placement::Right));
    const auto sourceGroup = manager.model().groupForTab(b);
    const auto pane = manager.model().tab(b)->activePane;
    auto* rootWindow = manager.windowWidget(root); prepareDesktopFixture(rootWindow);
    rootWindow->move(20, 30); manager.showWindow(root);
    QVERIFY(QTest::qWaitForWindowExposed(rootWindow));
    auto* bar = tabBar(rootWindow, b); QVERIFY(bar);
    const auto start = tabPoint(bar, b) + QPoint(20, 20);
    startTabDrag(rootWindow, bar, b);
    QCOMPARE(manager.model().windows().size(), 2);
    QVERIFY(!manager.model().group(sourceGroup));
    QCOMPARE(manager.model().centralGroups(root).size(), 1);
    const auto moving = manager.model().group(manager.model().groupForTab(b))->windowId;
    QPointer<QWidget> floating(manager.windowWidget(moving)); QVERIFY(floating && floating->isVisible());
    QCOMPARE(manager.model().window(moving)->kind, WindowKind::Floating);
    QCOMPARE(live->window(), floating.data());
    QCOMPARE(manager.paneWidget(pane), live);
    const auto before = floating->pos();
    mouse(floating, QEvent::MouseMove, start + QPoint(91, 43), Qt::NoButton, Qt::LeftButton);
    QCOMPARE(floating->pos(), before + QPoint(91, 43));
    QTest::keyClick(floating, Qt::Key_Escape);
    QCOMPARE(manager.model().windows().size(), 1);
    QCOMPARE(manager.model().groupForTab(b), sourceGroup);
    QCOMPARE(manager.paneWidget(pane), live);
    QVERIFY(preserved);
    flushDeletes(); QVERIFY(floating.isNull());
}

void DragInteractionTest::contentBodyDoesNotAcceptAnImplicitMerge()
{
    WindowManager manager;
    const auto root = manager.createWindow(WindowKind::Workbench, {"Body", QSize(920, 600)});
    const auto a = manager.addTab(root, new QLabel("A"), {"A", "label", {}});
    const auto b = manager.addTab(root, new QLabel("B"), {"B", "label", {}});
    auto* window = manager.windowWidget(root); prepareDesktopFixture(window); window->move(20, 30); manager.showWindow(root);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto* bar = tabBar(window, b); QVERIFY(bar); startTabDrag(window, bar, b);
    const auto moving = manager.model().group(manager.model().groupForTab(b))->windowId;
    auto* actor = manager.windowWidget(moving); QVERIFY(actor);
    const auto group = manager.model().groupForTab(a);
    const auto point = groupRect(window, group).center() + QPoint(80, 80);
    mouse(actor, QEvent::MouseMove, point, Qt::NoButton, Qt::LeftButton);
    mouse(actor, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
    QCOMPARE(manager.model().group(manager.model().groupForTab(b))->windowId, moving);
    QCOMPARE(manager.model().orderedTabs(root), (QVector<TabId>{a}));
    QCOMPARE(manager.model().windows().size(), 2);
}

void DragInteractionTest::narrowGroupRecognizesDirectionalGuides_data()
{
    QTest::addColumn<bool>("right");
    QTest::newRow("left") << false; QTest::newRow("right") << true;
}
void DragInteractionTest::narrowGroupRecognizesDirectionalGuides()
{
    QFETCH(bool, right);
    WindowManager manager;
    const auto source = manager.createWindow(WindowKind::Workbench, {"Source", QSize(600, 500)});
    const auto target = manager.createWindow(WindowKind::Workbench, {"Narrow", QSize(560, 500)});
    const auto a = manager.addTab(source, new QLabel("A"), {"A", "label", {}});
    const auto b = manager.addTab(target, new QLabel("B"), {"B", "label", {}});
    const auto c = manager.addTab(target, new QLabel("C"), {"C", "label", {}});
    QVERIFY(manager.moveTab(c, manager.model().groupForTab(b), Placement::Right));
    auto* first = manager.windowWidget(source); auto* second = manager.windowWidget(target);
    prepareDesktopFixture(first); prepareDesktopFixture(second); first->move(20, 30); second->move(660, 30);
    manager.showWindow(target); manager.showWindow(source);
    QVERIFY(QTest::qWaitForWindowExposed(first)); QVERIFY(QTest::qWaitForWindowExposed(second));
    const auto destination = manager.model().groupForTab(b);
    const auto area = groupRect(second, destination); QVERIFY(area.width() < 340 && area.width() >= 120);
    auto* bar = tabBar(first, a); QVERIFY(bar); startTabDrag(first, bar, a);
    auto* actor = QWidget::mouseGrabber(); QVERIFY(actor);
    // The group's own side arm, as the drag shows it.
    auto* surface = dynamic_cast<DockSurface*>(second->findChild<QWidget*>("DockSurface")); QVERIFY(surface);
    const auto guides = dockGuides(QRect(surface->mapToGlobal(QPoint()), surface->size()), area, true,
                                   surface->dockStrips(), surface->edgeBarRects());
    QPoint point;
    for (const auto& guide : guides) if (!guide.toEdge && guide.placement == (right ? Placement::Right : Placement::Left))
        point = guide.rect.center();
    QVERIFY(!point.isNull());
    mouse(actor, QEvent::MouseMove, point, Qt::NoButton, Qt::LeftButton);
    mouse(actor, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
    QCOMPARE(manager.model().centralGroups(target).size(), 3);
    QVERIFY(manager.model().groupForTab(a) != destination);
    QCOMPARE(manager.model().group(manager.model().groupForTab(a))->windowId, target);
    QVERIFY(manager.model().validate().ok);
}
void DragInteractionTest::edgeGuidesSeparateHiddenAndPinnedDrops_data()
{
    QTest::addColumn<int>("side"); QTest::addColumn<bool>("pinned");
    for (int side = 0; side < 4; ++side) for (const bool pinned : {false, true})
        QTest::newRow(qPrintable(QStringLiteral("edge-%1-%2").arg(side).arg(pinned ? "pinned" : "hidden"))) << side << pinned;
}
void DragInteractionTest::edgeGuidesSeparateHiddenAndPinnedDrops()
{
    QFETCH(int, side); QFETCH(bool, pinned);
    WindowManager manager;
    const auto source = manager.createWindow(WindowKind::Workbench, {"Source", QSize(600, 500)});
    const auto target = manager.createWindow(WindowKind::Workbench, {"Edges", QSize(740, 600)});
    const auto a = manager.addTab(source, new QLabel("A"), {"A", "label", {}});
    manager.addTab(target, new QLabel("B"), {"B", "label", {}});
    auto* first = manager.windowWidget(source); auto* second = manager.windowWidget(target);
    prepareDesktopFixture(first); prepareDesktopFixture(second); first->move(20, 30); second->move(660, 30);
    manager.showWindow(target); manager.showWindow(source);
    QVERIFY(QTest::qWaitForWindowExposed(first)); QVERIFY(QTest::qWaitForWindowExposed(second));
    auto* surface = dynamic_cast<DockSurface*>(second->findChild<QWidget*>("DockSurface")); QVERIFY(surface);
    const QRect bounds(surface->mapToGlobal(QPoint()), surface->size());
    const auto strips = surface->tabBarRects().values() + surface->edgeBarRects().values();
    const auto guides = dockGuides(bounds, surface->centralRectGlobal(), true, surface->dockStrips(), surface->edgeBarRects());
    QPoint point;
    for (const auto& guide : guides) if (guide.toEdge && guide.edge == Edge(side) && guide.pinned == pinned) {
        for (const auto& strip : strips) QVERIFY(!guide.rect.intersects(strip));
        point = guide.rect.center();
    }
    QVERIFY(!point.isNull());
    auto* bar = tabBar(first, a); QVERIFY(bar); startTabDrag(first, bar, a);
    auto* actor = QWidget::mouseGrabber(); QVERIFY(actor);
    mouse(actor, QEvent::MouseMove, point, Qt::NoButton, Qt::LeftButton);
    mouse(actor, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
    const auto* group = manager.model().group(manager.model().groupForTab(a)); QVERIFY(group);
    QCOMPARE(group->windowId, target); QCOMPARE(group->edge, static_cast<Edge>(side));
    QCOMPARE(group->location, pinned ? GroupLocation::Pinned : GroupLocation::AutoHide);
    QVERIFY(manager.model().validate().ok);
}
void DragInteractionTest::actualTabBarDragMergesAcrossWindows()
{
    WindowManager manager;
    const auto source = manager.createWindow(WindowKind::Workbench, {"Source", QSize(600, 430)});
    const auto target = manager.createWindow(WindowKind::Workbench, {"Target", QSize(600, 430)});
    const auto a = manager.addTab(source, new QLabel("A"), {"A", "label", {}});
    const auto b = manager.addTab(target, new QLabel("B"), {"B", "label", {}});
    auto* sourceWindow = manager.windowWidget(source); auto* targetWindow = manager.windowWidget(target);
    prepareDesktopFixture(sourceWindow); prepareDesktopFixture(targetWindow);
    sourceWindow->move(20, 30); targetWindow->move(660, 30);
    manager.showWindow(source); manager.showWindow(target); QCoreApplication::processEvents();
    QVERIFY(QTest::qWaitForWindowExposed(sourceWindow));
    QVERIFY(QTest::qWaitForWindowExposed(targetWindow));
    auto* bar = tabBar(sourceWindow, a); QVERIFY(bar);
    const auto targetGroup = manager.model().groupForTab(b);
    startTabDrag(sourceWindow, bar, a);
    const auto point = contentCenter(targetWindow);
    mouse(sourceWindow, QEvent::MouseMove, point, Qt::NoButton, Qt::LeftButton);
    mouse(sourceWindow, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
    QCOMPARE(manager.model().groupForTab(a), targetGroup);
    QCOMPARE(manager.model().group(targetGroup)->tabs, QVector<TabId>({b, a}));
}
void DragInteractionTest::floatingTitleDragMovesAllTabsTogether()
{
    WindowManager manager;
    const auto root = manager.createWindow(WindowKind::Workbench, {"Root", QSize(600, 430)});
    const auto a = manager.addTab(root, new QLabel("A"), {"A", "label", {}});
    const auto floating = manager.detachTab(a);
    const auto b = manager.addTab(floating, new QLabel("B"), {"B", "label", {}});
    const auto target = manager.createWindow(WindowKind::Workbench, {"Target", QSize(600, 430)});
    const auto c = manager.addTab(target, new QLabel("C"), {"C", "label", {}});
    auto* sourceWindow = manager.windowWidget(floating); auto* targetWindow = manager.windowWidget(target);
    prepareDesktopFixture(sourceWindow); prepareDesktopFixture(targetWindow);
    sourceWindow->move(20, 30); targetWindow->move(720, 30);
    manager.showWindow(target); manager.showWindow(floating); QCoreApplication::processEvents();
    QVERIFY(QTest::qWaitForWindowExposed(sourceWindow));
    QVERIFY(QTest::qWaitForWindowExposed(targetWindow));
    auto* caption = sourceWindow->findChild<QLabel*>("CaptionTitle"); QVERIFY(caption);
    const auto start = caption->mapToGlobal(QPoint(35, 15));
    mouse(caption, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    mouse(caption, QEvent::MouseMove, start + QPoint(25, 0), Qt::NoButton, Qt::LeftButton);
    const auto point = contentCenter(targetWindow);
    mouse(sourceWindow, QEvent::MouseMove, point, Qt::NoButton, Qt::LeftButton);
    mouse(sourceWindow, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
    QVERIFY(!manager.model().window(floating));
    const auto group = manager.model().groupForTab(c);
    QCOMPARE(manager.model().groupForTab(a), group); QCOMPARE(manager.model().groupForTab(b), group);
    QCOMPARE(manager.model().group(group)->tabs.size(), 3);
}

void DragInteractionTest::foreignClickThroughOverlayDoesNotBlockTabDrop()
{
    WindowManager manager;
    const auto source = manager.createWindow(WindowKind::Workbench, {"Overlay source", QSize(600, 430)});
    const auto target = manager.createWindow(WindowKind::Workbench, {"Overlay target", QSize(600, 430)});
    auto* movedContent = new QLabel("Live content behind click-through layer");
    QPointer<QWidget> stableContent = movedContent;
    const auto a = manager.addTab(source, movedContent, {"A", "label", {}});
    const auto b = manager.addTab(target, new QLabel("B"), {"B", "label", {}});
    const auto pane = manager.model().tab(a)->activePane;
    const auto targetGroup = manager.model().groupForTab(b);
    auto* sourceWindow = manager.windowWidget(source);
    auto* targetWindow = manager.windowWidget(target);
    prepareDesktopFixture(sourceWindow); prepareDesktopFixture(targetWindow);
    sourceWindow->move(20, 30); targetWindow->move(660, 30);
    manager.showWindow(source); manager.showWindow(target);
    QVERIFY(QTest::qWaitForWindowExposed(sourceWindow));
    QVERIFY(QTest::qWaitForWindowExposed(targetWindow));
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto ready = temporary.filePath("transparent-ready.json");
    OverlayProcess fixture;
    QVERIFY(fixture.start(QRect(targetWindow->mapToGlobal(QPoint()), targetWindow->size()), ready, false));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(ready), 4000);
    QCOMPARE(fixture.process.state(), QProcess::Running);
    const auto report = overlayReport(ready);
    QVERIFY(report.value("pid").toVariant().toLongLong() != QCoreApplication::applicationPid());
    QVERIFY(!report.value("opaque").toBool());
#ifdef Q_OS_WIN
    QVERIFY(report.value("layered").toBool());
    QVERIFY(report.value("transparentForInput").toBool());
    QVERIFY(report.value("topmost").toBool());
#endif
    auto* bar = tabBar(sourceWindow, a); QVERIFY(bar);
    startTabDrag(sourceWindow, bar, a);
    const auto point = contentCenter(targetWindow);
    mouse(sourceWindow, QEvent::MouseMove, point, Qt::NoButton, Qt::LeftButton);
    mouse(sourceWindow, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
    QCOMPARE(manager.model().groupForTab(a), targetGroup);
    QCOMPARE(manager.model().group(targetGroup)->tabs, QVector<TabId>({b, a}));
    QVERIFY(stableContent);
    QCOMPARE(manager.paneWidget(pane), movedContent);
    QCOMPARE(movedContent->window(), targetWindow);
    QCOMPARE(movedContent->text(), QStringLiteral("Live content behind click-through layer"));
    QVERIFY(manager.model().validate().ok);
}

void DragInteractionTest::foreignOpaqueOverlayBlocksTabDropWithoutLosingContent()
{
    WindowManager manager;
    const auto source = manager.createWindow(WindowKind::Workbench, {"Opaque source", QSize(600, 430)});
    const auto target = manager.createWindow(WindowKind::Workbench, {"Opaque target", QSize(600, 430)});
    auto* movedContent = new QLabel("Live content must survive an opaque blocker");
    QPointer<QWidget> stableContent = movedContent;
    const auto a = manager.addTab(source, movedContent, {"A", "label", {}});
    const auto b = manager.addTab(target, new QLabel("B"), {"B", "label", {}});
    const auto pane = manager.model().tab(a)->activePane;
    const auto targetGroup = manager.model().groupForTab(b);
    auto* sourceWindow = manager.windowWidget(source);
    auto* targetWindow = manager.windowWidget(target);
    prepareDesktopFixture(sourceWindow); prepareDesktopFixture(targetWindow);
    sourceWindow->move(20, 30); targetWindow->move(660, 30);
    manager.showWindow(source); manager.showWindow(target);
    QVERIFY(QTest::qWaitForWindowExposed(sourceWindow));
    QVERIFY(QTest::qWaitForWindowExposed(targetWindow));
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto ready = temporary.filePath("opaque-ready.json");
    OverlayProcess fixture;
    QVERIFY(fixture.start(QRect(targetWindow->mapToGlobal(QPoint()), targetWindow->size()), ready, true));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(ready), 4000);
    QCOMPARE(fixture.process.state(), QProcess::Running);
    const auto report = overlayReport(ready);
    QVERIFY(report.value("pid").toVariant().toLongLong() != QCoreApplication::applicationPid());
    QVERIFY(report.value("opaque").toBool());
#ifdef Q_OS_WIN
    QVERIFY(!report.value("transparentForInput").toBool());
    QVERIFY(report.value("topmost").toBool());
#endif
    auto* bar = tabBar(sourceWindow, a); QVERIFY(bar);
    startTabDrag(sourceWindow, bar, a);
    const auto point = contentCenter(targetWindow);
    mouse(sourceWindow, QEvent::MouseMove, point, Qt::NoButton, Qt::LeftButton);
    mouse(sourceWindow, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
    QVERIFY(manager.model().groupForTab(a) != targetGroup);
    QCOMPARE(manager.model().group(targetGroup)->tabs, QVector<TabId>({b}));
    QVERIFY(manager.model().tab(a));
    QVERIFY(stableContent);
    QCOMPARE(manager.paneWidget(pane), movedContent);
    QCOMPARE(movedContent->text(), QStringLiteral("Live content must survive an opaque blocker"));
    QVERIFY(manager.model().validate().ok);
}
void DragInteractionTest::tabStripDropInsertsAtTheReleasedPosition()
{
    WindowManager manager;
    const auto root = manager.createWindow(WindowKind::Workbench, {"Insertion", QSize(920, 600)});
    const auto a = manager.addTab(root, new QLabel("A"), {"A", "label", {}});
    const auto b = manager.addTab(root, new QLabel("B"), {"B", "label", {}});
    const auto c = manager.addTab(root, new QLabel("C"), {"C", "label", {}});
    auto* window = manager.windowWidget(root); prepareDesktopFixture(window); window->move(20, 30); manager.showWindow(root);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto* bar = tabBar(window, c); QVERIFY(bar); startTabDrag(window, bar, c);
    auto* destination = tabBar(window, a); QVERIFY(destination);
    const auto first = destination->tabRect(0);
    const auto point = destination->mapToGlobal(QPoint(first.left() + 5, first.center().y()));
    auto* actor = QWidget::mouseGrabber(); QVERIFY(actor);
    mouse(actor, QEvent::MouseMove, point, Qt::NoButton, Qt::LeftButton);
    mouse(actor, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
    const auto group = manager.model().groupForTab(a);
    QCOMPARE(manager.model().group(group)->tabs, (QVector<TabId>{c, a, b}));
    QCOMPARE(manager.model().windows().size(), 1);
}
void DragInteractionTest::wholeWindowDropInsertsAllTabsAtTheReleasedPosition()
{
    WindowManager manager;
    const auto root = manager.createWindow(WindowKind::Workbench, {"Root", QSize(600, 500)});
    const auto a = manager.addTab(root, new QLabel("A"), {"A", "label", {}});
    const auto floating = manager.detachTab(a);
    const auto b = manager.addTab(floating, new QLabel("B"), {"B", "label", {}});
    const auto target = manager.createWindow(WindowKind::Workbench, {"Target", QSize(740, 600)});
    const auto c = manager.addTab(target, new QLabel("C"), {"C", "label", {}});
    const auto d = manager.addTab(target, new QLabel("D"), {"D", "label", {}});
    auto* first = manager.windowWidget(floating); auto* second = manager.windowWidget(target);
    prepareDesktopFixture(first); prepareDesktopFixture(second); first->move(20, 30); second->move(1000, 30);
    manager.showWindow(target); manager.showWindow(floating);
    QVERIFY(QTest::qWaitForWindowExposed(first)); QVERIFY(QTest::qWaitForWindowExposed(second));
    auto* caption = first->findChild<QLabel*>("CaptionTitle"); QVERIFY(caption);
    const auto start = caption->mapToGlobal(QPoint(40, 15));
    mouse(caption, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    mouse(caption, QEvent::MouseMove, start + QPoint(25, 0), Qt::NoButton, Qt::LeftButton);
    auto* bar = tabBar(second, d); QVERIFY(bar);
    const auto item = bar->tabRect(1);
    const auto point = bar->mapToGlobal(QPoint(item.left() + 5, item.center().y()));
    mouse(first, QEvent::MouseMove, point, Qt::NoButton, Qt::LeftButton);
    mouse(first, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
    QCOMPARE(manager.model().group(manager.model().groupForTab(c))->tabs, (QVector<TabId>{c, a, b, d}));
    QVERIFY(!manager.model().window(floating));
    QVERIFY(manager.model().validate().ok);
}
void DragInteractionTest::draggingThePeekTitleImmediatelyRemovesThePeek()
{
    WindowManager manager;
    const auto root = manager.createWindow(WindowKind::Workbench, {"Peek", QSize(920, 600)});
    auto* content = new QLabel("Peek content"); QPointer<QWidget> preserved(content);
    const auto a = manager.addTab(root, content, {"A", "label", {}});
    QVERIFY(manager.moveTabToEdge(a, Edge::Left));
    auto* window = manager.windowWidget(root); prepareDesktopFixture(window); window->move(20, 30); manager.showWindow(root);
    QVERIFY(QTest::qWaitForWindowExposed(window)); QVERIFY(manager.activateTab(a));
    QPointer<QWidget> peek = window->findChild<QWidget*>("PeekPanel"); QVERIFY(peek);
    auto* header = peek->findChild<QWidget*>("PeekTitleBar"); QVERIFY(header);
    const auto start = header->mapToGlobal(QPoint(40, 15));
    mouse(header, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    mouse(header, QEvent::MouseMove, start + QPoint(25, 20), Qt::NoButton, Qt::LeftButton);
    QVERIFY(peek.isNull()); QCOMPARE(manager.model().windows().size(), 2);
    const auto moving = manager.model().group(manager.model().groupForTab(a))->windowId;
    QVERIFY(moving != root); QCOMPARE(content->window(), manager.windowWidget(moving)); QVERIFY(preserved);
    QTest::keyClick(manager.windowWidget(moving), Qt::Key_Escape);
    QCOMPARE(manager.model().windows().size(), 1);
    QCOMPARE(manager.model().group(manager.model().groupForTab(a))->location, GroupLocation::AutoHide);
    QVERIFY(preserved); QVERIFY(manager.model().validate().ok);
}
void DragInteractionTest::pinnedGroupAcceptsDirectionalDrops_data()
{
    QTest::addColumn<int>("direction");
    QTest::newRow("left") << int(Placement::Left);
    QTest::newRow("right") << int(Placement::Right);
    QTest::newRow("top") << int(Placement::Top);
    QTest::newRow("bottom") << int(Placement::Bottom);
}
void DragInteractionTest::pinnedGroupAcceptsDirectionalDrops()
{
    QFETCH(int, direction); const auto placement = Placement(direction);
    WindowManager manager;
    const auto source = manager.createWindow(WindowKind::Workbench, {"Source", QSize(600, 500)});
    const auto target = manager.createWindow(WindowKind::Workbench, {"Pinned", QSize(740, 600)});
    auto* content = new QLabel("Stable dragged content"); QPointer<QWidget> stableContent(content);
    const auto a = manager.addTab(source, content, {"A", "label", {}});
    const auto pane = manager.model().tab(a)->activePane;
    const auto b = manager.addTab(target, new QLabel("B"), {"B", "label", {}});
    const auto central = manager.addTab(target, new QLabel("Central"), {"Central", "label", {}});
    const auto centralGroup = manager.model().groupForTab(central);
    QVERIFY(manager.moveTabToEdge(b, Edge::Left, true));
    const auto targetGroup = manager.model().groupForTab(b);
    auto* first = manager.windowWidget(source); auto* second = manager.windowWidget(target);
    prepareDesktopFixture(first); prepareDesktopFixture(second); first->move(20, 30); second->move(660, 30);
    manager.showWindow(target); manager.showWindow(source);
    QVERIFY(QTest::qWaitForWindowExposed(first)); QVERIFY(QTest::qWaitForWindowExposed(second));
    auto* surface = dynamic_cast<DockSurface*>(second->findChild<QWidget*>("DockSurface")); QVERIFY(surface);
    const auto area = groupRect(second, targetGroup); QVERIFY(!area.isEmpty());
    auto* bar = tabBar(first, a); QVERIFY(bar); startTabDrag(first, bar, a);
    const auto moving = manager.model().group(manager.model().groupForTab(a))->windowId;
    auto* actor = QWidget::mouseGrabber(); QVERIFY(actor);
    mouse(actor, QEvent::MouseMove, area.center(), Qt::NoButton, Qt::LeftButton);
    const QRect bounds(surface->mapToGlobal(QPoint()), surface->size());
    const auto guides = dockGuides(bounds, area, true, surface->dockStrips(), surface->edgeBarRects());
    QPoint point; int directionalGuides = 0;
    for (const auto& guide : guides) if (!guide.toEdge && guide.placement != Placement::Center) {
        ++directionalGuides;
        if (guide.placement == placement) point = guide.rect.center();
    }
    QCOMPARE(directionalGuides, 4); QVERIFY(!point.isNull());
    mouse(actor, QEvent::MouseMove, point, Qt::NoButton, Qt::LeftButton);
    mouse(actor, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
    const auto createdGroup = manager.model().groupForTab(a); QVERIFY(createdGroup != targetGroup);
    const auto* created = manager.model().group(createdGroup); QVERIFY(created);
    QCOMPARE(created->windowId, target); QCOMPARE(created->location, GroupLocation::Pinned);
    QCOMPARE(created->edge, Edge::Left); QCOMPARE(created->tabs, QVector<TabId>{a});
    QCOMPARE(manager.model().groupForTab(b), targetGroup);
    QCOMPARE(manager.model().group(targetGroup)->tabs, QVector<TabId>{b});
    QCOMPARE(manager.model().centralGroups(target), QVector<GroupId>{centralGroup});
    QVERIFY(!manager.model().window(moving));
    QTRY_VERIFY(!groupRect(second, createdGroup).isEmpty());
    const auto movedBounds = groupRect(second, createdGroup), retainedBounds = groupRect(second, targetGroup);
    if (placement == Placement::Left || placement == Placement::Right) {
        QCOMPARE(movedBounds.top(), retainedBounds.top()); QCOMPARE(movedBounds.bottom(), retainedBounds.bottom());
        if (placement == Placement::Left) QCOMPARE(retainedBounds.left() - movedBounds.right() - 1, 2);
        else QCOMPARE(movedBounds.left() - retainedBounds.right() - 1, 2);
    } else {
        QCOMPARE(movedBounds.left(), retainedBounds.left()); QCOMPARE(movedBounds.right(), retainedBounds.right());
        if (placement == Placement::Top) QCOMPARE(retainedBounds.top() - movedBounds.bottom() - 1, 2);
        else QCOMPARE(movedBounds.top() - retainedBounds.bottom() - 1, 2);
    }
    QVERIFY(stableContent); QCOMPARE(manager.paneWidget(pane), content);
    QCOMPARE(manager.model().tab(a)->activePane, pane); QCOMPARE(content->window(), second);
    QVERIFY(content->isVisible()); QCOMPARE(content->text(), QStringLiteral("Stable dragged content"));
    QVERIFY(!QWidget::mouseGrabber());
    QVERIFY(manager.model().validate().ok);
}
void DragInteractionTest::cancelFromAnEmptiedFloatingWindowRestoresItsToolbarPreferences()
{
    WindowManager manager;
    // Exercise tab drag cancellation with a visible source strip. The new
    // single-tab defaults are covered independently by presentation tests.
    auto behavior = manager.behavior(); behavior.hideSingleTabFloating = false; manager.setBehavior(behavior);
    const auto owner = manager.createWindow(WindowKind::Workbench, {"Owner", QSize(600, 500)});
    const auto source = manager.createWindow(WindowKind::Workbench, {"Customized", QSize(740, 600)});
    const auto tab = manager.addTab(source, new QLabel("Live content"), {"Live", "label", {}});
    const auto pane = manager.model().tab(tab)->activePane;
    QPointer<QWidget> live = manager.paneWidget(pane);
    auto* window = static_cast<WorkspaceWindow*>(manager.windowWidget(source));
    auto* strips = window->strips(); QVERIFY(strips);
    strips->setLocked(false); QVERIFY(strips->moveStrip("title", 1, 0));
    QVERIFY(strips->setStripVisible("workspace", false));
    const auto before = strips->saveLayout();
    QVERIFY(manager.setSideBarsVisible(source, false));
    QVERIFY(manager.demoteWindow(source, owner));
    QVERIFY(manager.setStatusBarVisible(source, false));
    const auto floatingBefore = window->saveStripLayout();
    const auto savedBars = [&manager, source] {
        for (const auto& item : manager.saveLayout().value("windows").toArray()) {
            const auto record = item.toObject();
            if (record.value("id").toString() == source.toString(QUuid::WithoutBraces)) return record.value("barLayouts").toObject();
        }
        return QJsonObject{};
    };
    const auto barsBefore = savedBars(); QCOMPARE(barsBefore.size(), 2);
    prepareDesktopFixture(window); manager.showWindow(source);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto* bar = tabBar(window, tab); QVERIFY(bar); startTabDrag(window, bar, tab);
    QVERIFY(!manager.model().window(source));
    auto* actor = QWidget::mouseGrabber(); QVERIFY(actor); QTest::keyClick(actor, Qt::Key_Escape);
    auto* restored = static_cast<WorkspaceWindow*>(manager.windowWidget(source)); QVERIFY(restored);
    QCOMPARE(restored->saveStripLayout(), floatingBefore);
    QCOMPARE(savedBars(), barsBefore);
    QCOMPARE(manager.paneWidget(pane), live.data());
    QVERIFY(manager.promoteWindow(source));
    QCOMPARE(restored->strips()->saveLayout(), before);
    QVERIFY(manager.model().validate().ok);
}
void DragInteractionTest::sideBarDragReordersAcrossInternalGroups_data()
{
    QTest::addColumn<int>("edge");
    QTest::newRow("left") << int(Edge::Left);
    QTest::newRow("right") << int(Edge::Right);
    QTest::newRow("top") << int(Edge::Top);
    QTest::newRow("bottom") << int(Edge::Bottom);
}
void DragInteractionTest::sideBarDragReordersAcrossInternalGroups()
{
    QFETCH(int, edge);
    WindowManager manager;
    const auto root = manager.createWindow(WindowKind::Workbench, {"Side reorder", QSize(900, 620)});
    auto* live = new QLabel("Stable side content");
    const auto a = manager.addTab(root, live, {"A", "label", {}});
    const auto b = manager.addTab(root, new QLabel("B"), {"B", "label", {}});
    const auto c = manager.addTab(root, new QLabel("C"), {"C", "label", {}});
    QVERIFY(manager.moveTabToEdge(a, Edge(edge)));
    QVERIFY(manager.moveTab(b, manager.model().groupForTab(a)));
    QVERIFY(manager.moveTabToEdge(c, Edge(edge)));
    const auto pane = manager.model().tab(a)->activePane;
    auto* window = manager.windowWidget(root); prepareDesktopFixture(window);
    window->move(30, 40); manager.showWindow(root);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto* source = edgeTabButton(window, a); QVERIFY(source);
    startEdgeTabDrag(source);
    QCOMPARE(manager.model().windows().size(), 2);
    QCOMPARE(manager.model().autoHiddenTabs(root, Edge(edge)), (QVector<TabId>{b, c}));
    QVERIFY(!edgeTabButton(window, a));
    const auto actorId = manager.model().group(manager.model().groupForTab(a))->windowId;
    auto* actor = manager.windowWidget(actorId); QVERIFY(actor); QVERIFY(actor->isVisible());
    auto* target = edgeTabButton(window, c); QVERIFY(target);
    const auto offset = edge == int(Edge::Left) || edge == int(Edge::Right) ? QPoint(0, 8) : QPoint(8, 0);
    const auto point = target->mapToGlobal(target->rect().center() + offset);
    mouse(actor, QEvent::MouseMove, point, Qt::NoButton, Qt::LeftButton);
    mouse(actor, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
    QCOMPARE(manager.model().autoHiddenTabs(root, Edge(edge)), (QVector<TabId>{b, c, a}));
    QCOMPARE(manager.model().windows().size(), 1);
    QCOMPARE(manager.paneWidget(pane), live);
    QVERIFY(manager.model().validate().ok);
}
void DragInteractionTest::sideBarDragAcrossWindowsKeepsTheOriginalContent()
{
    WindowManager manager;
    const auto sourceId = manager.createWindow(WindowKind::Workbench, {"Source", QSize(470, 450)});
    const auto targetId = manager.createWindow(WindowKind::Workbench, {"Target", QSize(470, 450)});
    auto* live = new QLabel("Cross window side content");
    const auto a = manager.addTab(sourceId, live, {"A", "label", {}});
    const auto b = manager.addTab(targetId, new QLabel("B"), {"B", "label", {}});
    const auto c = manager.addTab(targetId, new QLabel("C"), {"C", "label", {}});
    QVERIFY(manager.moveTabToEdge(a, Edge::Left));
    QVERIFY(manager.moveTabToEdge(b, Edge::Top));
    QVERIFY(manager.moveTabToEdge(c, Edge::Top));
    const auto pane = manager.model().tab(a)->activePane;
    const auto group = manager.model().groupForTab(a);
    auto* source = manager.windowWidget(sourceId); auto* target = manager.windowWidget(targetId);
    prepareDesktopFixture(source); prepareDesktopFixture(target);
    source->move(20, 30); target->move(530, 30);
    manager.showWindow(sourceId); manager.showWindow(targetId);
    QVERIFY(QTest::qWaitForWindowExposed(source)); QVERIFY(QTest::qWaitForWindowExposed(target));
    auto* button = edgeTabButton(source, a); QVERIFY(button);
    startEdgeTabDrag(button);
    QVERIFY(!manager.model().group(group));
    auto* actor = manager.windowWidget(manager.model().group(manager.model().groupForTab(a))->windowId); QVERIFY(actor);
    auto* destination = edgeTabButton(target, c); QVERIFY(destination);
    const auto point = destination->mapToGlobal(destination->rect().center() - QPoint(8, 0));
    mouse(actor, QEvent::MouseMove, point, Qt::NoButton, Qt::LeftButton);
    mouse(actor, QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
    QCOMPARE(manager.model().autoHiddenTabs(targetId, Edge::Top), (QVector<TabId>{b, a, c}));
    QCOMPARE(manager.model().group(manager.model().groupForTab(a))->windowId, targetId);
    QCOMPARE(manager.paneWidget(pane), live);
    QCOMPARE(manager.model().windows().size(), 2);
    QVERIFY(manager.model().validate().ok);
}
void DragInteractionTest::sideBarDragEscapeRestoresAnEmptiedSourceGroup()
{
    WindowManager manager;
    const auto root = manager.createWindow();
    auto* live = new QLabel("Cancelable side content");
    const auto a = manager.addTab(root, live, {"A", "label", {}});
    const auto b = manager.addTab(root, new QLabel("B"), {"B", "label", {}});
    QVERIFY(manager.moveTabToEdge(a, Edge::Right));
    QVERIFY(manager.moveTabToEdge(b, Edge::Right));
    QVERIFY(manager.activateTab(a));
    const auto before = manager.model().toJson();
    const auto sourceGroup = manager.model().groupForTab(a);
    const auto pane = manager.model().tab(a)->activePane;
    auto* window = manager.windowWidget(root); prepareDesktopFixture(window); manager.showWindow(root);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto* button = edgeTabButton(window, a); QVERIFY(button);
    startEdgeTabDrag(button);
    QVERIFY(!manager.model().group(sourceGroup));
    QTest::keyClick(manager.windowWidget(manager.model().group(manager.model().groupForTab(a))->windowId), Qt::Key_Escape);
    QCOMPARE(manager.model().toJson(), before);
    QCOMPARE(manager.paneWidget(pane), live);
    QCOMPARE(manager.model().groupForTab(a), sourceGroup);
    QVERIFY(edgeTabButton(window, a));
    QCOMPARE(manager.model().windows().size(), 1);
}

QTEST_MAIN(DragInteractionTest)
#include "drag_interaction_test.moc"
