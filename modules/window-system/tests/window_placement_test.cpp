// SPDX-License-Identifier: MIT
#include <window_system/window_manager.hpp>
#include "chrome/workspace_window.hpp"
#include "platform/window_placement.hpp"

#include <QApplication>
#include <QJsonArray>
#include <QLabel>
#include <QPointer>
#include <QScreen>
#include <QSignalSpy>
#include <QTest>

using namespace window_system;

namespace {

void addWindowKinds()
{
    QTest::addColumn<bool>("floating");
    QTest::newRow("workbench") << false;
    QTest::newRow("floating") << true;
}

WindowId createFixture(WindowManager& manager, bool floating)
{
    const auto root = manager.createWindow();
    const auto target = floating ? manager.createWindow(WindowKind::Floating, {}, root) : root;
    manager.addTab(target, new QLabel("Content"), {"Placement", "test.label", {}});
    return target;
}

void useContentFactory(WindowManager& manager)
{
    manager.setContentFactory([](const TabOptions&, PaneId) { return new QLabel("Restored content"); });
}

QJsonObject windowRecord(const QJsonObject& snapshot, WindowId id)
{
    for (const auto& entry : snapshot.value("windows").toArray()) {
        const auto object = entry.toObject();
        if (WindowId(object.value("id").toString()) == id) return object;
    }
    return {};
}

QRect recordGeometry(const QJsonObject& record)
{
    return {record.value("x").toInt(), record.value("y").toInt(),
            record.value("width").toInt(), record.value("height").toInt()};
}

void replaceRecordGeometry(QJsonObject& snapshot, WindowId id, const QRect& geometry, bool maximized)
{
    auto windows = snapshot.value("windows").toArray();
    for (qsizetype index = 0; index < windows.size(); ++index) {
        auto record = windows.at(index).toObject();
        if (WindowId(record.value("id").toString()) != id) continue;
        record.insert("x", geometry.x()); record.insert("y", geometry.y());
        record.insert("width", geometry.width()); record.insert("height", geometry.height());
        record.insert("maximized", maximized); windows[index] = record;
    }
    snapshot.insert("windows", windows);
}

QRect visibleFixtureGeometry(QScreen* screen, QPoint offset = {})
{
    const auto available = screen->availableGeometry();
    const QSize size(std::min(900, available.width()), std::min(650, available.height()));
    QRect geometry(available.topLeft(), size);
    geometry.moveCenter(available.center() + offset);
    return geometry.intersected(available);
}

} // namespace

class WindowPlacementTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); }
    void cleanup() { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }
    void normalizationUsesAllLogicalScreenAreas_data();
    void normalizationUsesAllLogicalScreenAreas();
    void movingAndResizingNotifyTheHost_data() { addWindowKinds(); }
    void movingAndResizingNotifyTheHost();
    void freshManagerRestoresNormalPlacement_data() { addWindowKinds(); }
    void freshManagerRestoresNormalPlacement();
    void maximizeStateChangeNotifiesTheHost_data() { addWindowKinds(); }
    void maximizeStateChangeNotifiesTheHost();
    void maximizedAndMinimizedPreserveNormalPlacement_data();
    void maximizedAndMinimizedPreserveNormalPlacement();
    void partiallyOffscreenSnapshotsBecomeReachable_data() { addWindowKinds(); }
    void partiallyOffscreenSnapshotsBecomeReachable();
    void restoringOverAnExistingMaximizedWindowHonorsNormalState_data() { addWindowKinds(); }
    void restoringOverAnExistingMaximizedWindowHonorsNormalState();
    void queuedNotificationDoesNotOutliveAClosedWindow();
};

void WindowPlacementTest::normalizationUsesAllLogicalScreenAreas_data()
{
    QTest::addColumn<QRect>("desired");
    QTest::addColumn<QList<QRect>>("screens");
    QTest::addColumn<QSize>("minimum");
    QTest::addColumn<QRect>("expected");
    const QList<QRect> adjacent{{-1600, 0, 1600, 1000}, {0, 0, 1920, 1080}};
    const QSize minimum(480, 320);
    QTest::newRow("visible-negative-monitor") << QRect(-1500, 50, 900, 650) << adjacent << minimum
        << QRect(-1500, 50, 900, 650);
    QTest::newRow("visible-across-contiguous-monitors") << QRect(-300, 100, 900, 650) << adjacent << minimum
        << QRect(-300, 100, 900, 650);
    QTest::newRow("partly-outside-left-screen") << QRect(-1750, 100, 900, 650) << adjacent << minimum
        << QRect(-1600, 100, 900, 650);
    QTest::newRow("lost-screen-nearest-negative-monitor") << QRect(-5000, 100, 900, 650) << adjacent << minimum
        << QRect(-1600, 100, 900, 650);
    QTest::newRow("oversized-fits-selected-screen") << QRect(100, 100, 8000, 6000) << adjacent << minimum
        << QRect(0, 0, 1920, 1080);
    QTest::newRow("taskbar-area-is-unavailable") << QRect(100, 700, 900, 650)
        << QList<QRect>{{0, 0, 1920, 1040}} << minimum << QRect(100, 390, 900, 650);
    QTest::newRow("gap-is-not-a-monitor") << QRect(1400, 100, 800, 650)
        << QList<QRect>{{0, 0, 1600, 1000}, {2000, 0, 1920, 1080}} << minimum
        << QRect(800, 100, 800, 650);
    QTest::newRow("small-screen-keeps-caption-reachable") << QRect(1200, 900, 800, 650)
        << QList<QRect>{{0, 0, 400, 250}} << minimum << QRect(0, 0, 480, 320);
    QTest::newRow("missing-screen-information-keeps-geometry") << QRect(-1200, 80, 800, 650)
        << QList<QRect>{} << minimum << QRect(-1200, 80, 800, 650);
}

void WindowPlacementTest::normalizationUsesAllLogicalScreenAreas()
{
    QFETCH(QRect, desired); QFETCH(QList<QRect>, screens); QFETCH(QSize, minimum); QFETCH(QRect, expected);
    QCOMPARE(normalizeWindowPlacement(desired, screens, minimum), expected);
}

void WindowPlacementTest::movingAndResizingNotifyTheHost()
{
    QFETCH(bool, floating);
    WindowManager manager;
    const auto id = createFixture(manager, floating);
    auto* window = manager.windowWidget(id); QVERIFY(window);
    const auto first = visibleFixtureGeometry(qApp->primaryScreen());
    window->setGeometry(first); manager.showWindow(id);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QCoreApplication::processEvents();
    QSignalSpy changed(&manager, &WindowManager::layoutChanged);
    window->move(first.topLeft() + QPoint(21, 17));
    QTRY_VERIFY(!changed.isEmpty());
    QCOMPARE(recordGeometry(windowRecord(manager.saveLayout(), id)), window->geometry());
    changed.clear();
    window->resize(first.size() - QSize(31, 23));
    QTRY_VERIFY(!changed.isEmpty());
    QCOMPARE(recordGeometry(windowRecord(manager.saveLayout(), id)), window->geometry());
    changed.clear();
    // Qt may emit synthetic events with unchanged coordinates. They must not
    // restart the host's persistence debounce indefinitely.
    window->setGeometry(window->geometry());
    QCoreApplication::processEvents();
    QVERIFY(changed.isEmpty());
}

void WindowPlacementTest::freshManagerRestoresNormalPlacement()
{
    QFETCH(bool, floating);
    WindowManager original;
    const auto id = createFixture(original, floating);
    auto* window = original.windowWidget(id); QVERIFY(window);
    const auto expected = visibleFixtureGeometry(qApp->primaryScreen());
    window->setGeometry(expected); original.showWindow(id);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QTRY_COMPARE(window->geometry(), expected);
    const auto snapshot = original.saveLayout();
    original.windowWidget(id)->hide();
    WindowManager restored;
    useContentFactory(restored);
    QVERIFY(restored.restoreLayout(snapshot));
    auto* result = restored.windowWidget(id); QVERIFY(result);
    QVERIFY(QTest::qWaitForWindowExposed(result));
    QTRY_COMPARE(result->geometry(), expected);
    QVERIFY(!result->isMaximized());
    QCOMPARE(restored.model().window(id)->kind, original.model().window(id)->kind);
    QCOMPARE(recordGeometry(windowRecord(restored.saveLayout(), id)), expected);
}

void WindowPlacementTest::maximizedAndMinimizedPreserveNormalPlacement_data()
{
    QTest::addColumn<bool>("floating");
    QTest::addColumn<bool>("maximized");
    QTest::newRow("normal-workbench-minimized") << false << false;
    QTest::newRow("normal-floating-minimized") << true << false;
    QTest::newRow("maximized-workbench-minimized") << false << true;
    QTest::newRow("maximized-floating-minimized") << true << true;
}

void WindowPlacementTest::maximizeStateChangeNotifiesTheHost()
{
    QFETCH(bool, floating);
    WindowManager manager;
    const auto id = createFixture(manager, floating);
    auto* window = manager.windowWidget(id); QVERIFY(window);
    window->setGeometry(visibleFixtureGeometry(qApp->primaryScreen()));
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(window));
    QCoreApplication::processEvents();
    QSignalSpy changed(&manager, &WindowManager::layoutChanged);
    window->showMaximized(); QTRY_VERIFY(window->isMaximized());
    QTRY_VERIFY(!changed.isEmpty());
    QVERIFY(windowRecord(manager.saveLayout(), id).value("maximized").toBool());
    changed.clear();
    window->showNormal(); QTRY_VERIFY(!window->isMaximized());
    QTRY_VERIFY(!changed.isEmpty());
    QVERIFY(!windowRecord(manager.saveLayout(), id).value("maximized").toBool());
}

void WindowPlacementTest::maximizedAndMinimizedPreserveNormalPlacement()
{
    QFETCH(bool, floating); QFETCH(bool, maximized);
    WindowManager original;
    const auto id = createFixture(original, floating);
    auto* window = original.windowWidget(id); QVERIFY(window);
    const auto expected = visibleFixtureGeometry(qApp->primaryScreen());
    window->setGeometry(expected); original.showWindow(id);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QTRY_COMPARE(window->geometry(), expected);
    if (maximized) {
        window->showMaximized(); QTRY_VERIFY(window->isMaximized());
        QTRY_COMPARE(recordGeometry(windowRecord(original.saveLayout(), id)), expected);
        QVERIFY(windowRecord(original.saveLayout(), id).value("maximized").toBool());
    }
    window->showMinimized(); QTRY_VERIFY(window->isMinimized());
    const auto snapshot = original.saveLayout();
    QCOMPARE(recordGeometry(windowRecord(snapshot, id)), expected);
    QCOMPARE(windowRecord(snapshot, id).value("maximized").toBool(), maximized);
    window->hide();
    WindowManager restored; useContentFactory(restored);
    QVERIFY(restored.restoreLayout(snapshot));
    auto* result = restored.windowWidget(id); QVERIFY(result);
    QVERIFY(QTest::qWaitForWindowExposed(result));
    QTRY_COMPARE(result->isMaximized(), maximized);
    QVERIFY(!result->isMinimized());
    QCOMPARE(recordGeometry(windowRecord(restored.saveLayout(), id)), expected);
    result->showNormal();
    QTRY_COMPARE(result->geometry(), expected);
}

void WindowPlacementTest::partiallyOffscreenSnapshotsBecomeReachable()
{
    QFETCH(bool, floating);
    WindowManager manager; useContentFactory(manager);
    const auto id = createFixture(manager, floating);
    auto snapshot = manager.saveLayout();
    const auto available = qApp->primaryScreen()->availableGeometry();
    const QRect outside(available.right() - 20, available.bottom() - 20, 900, 650);
    replaceRecordGeometry(snapshot, id, outside, false);
    QVERIFY(manager.restoreLayout(snapshot));
    auto* window = manager.windowWidget(id); QVERIFY(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QRegion desktop;
    for (const auto* screen : qApp->screens()) desktop += screen->availableGeometry();
    QTRY_VERIFY(QRegion(window->geometry()).subtracted(desktop).isEmpty());
    QVERIFY(window->geometry() != outside);
    QCOMPARE(recordGeometry(windowRecord(manager.saveLayout(), id)), window->geometry());
}

void WindowPlacementTest::restoringOverAnExistingMaximizedWindowHonorsNormalState()
{
    QFETCH(bool, floating);
    WindowManager manager; useContentFactory(manager);
    const auto id = createFixture(manager, floating);
    auto* window = manager.windowWidget(id); QVERIFY(window);
    const auto expected = visibleFixtureGeometry(qApp->primaryScreen());
    window->setGeometry(expected); manager.showWindow(id);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    const auto normal = manager.saveLayout();
    window->showMaximized(); QTRY_VERIFY(window->isMaximized());
    QVERIFY(manager.restoreLayout(normal));
    QCOMPARE(manager.windowWidget(id), window);
    QTRY_VERIFY(!window->isMaximized());
    QTRY_COMPARE(window->geometry(), expected);
    QCOMPARE(recordGeometry(windowRecord(manager.saveLayout(), id)), expected);
}

void WindowPlacementTest::queuedNotificationDoesNotOutliveAClosedWindow()
{
    WindowManager manager;
    const auto root = manager.createWindow();
    const auto id = manager.createWindow(WindowKind::Floating, {}, root);
    QPointer<QWidget> window = manager.windowWidget(id);
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(window));
    window->move(window->pos() + QPoint(20, 20));
    QVERIFY(manager.requestCloseWindow(id));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
    QVERIFY(!window);
    QVERIFY(manager.model().window(root));
}

QTEST_MAIN(WindowPlacementTest)
#include "window_placement_test.moc"
