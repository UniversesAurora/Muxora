// SPDX-License-Identifier: MIT
#include <window_system/window_manager.hpp>
#include <QApplication>
#include <QCursor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMouseEvent>
#include <QPointer>
#include <QProcess>
#include <QScreen>
#include <QTabBar>
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
QTabBar* tabBar(QWidget* window, TabId tab)
{
    for (auto* bar : window->findChildren<QTabBar*>())
        for (int i = 0; i < bar->count(); ++i)
            if (bar->tabData(i).toUuid() == tab) return bar;
    return nullptr;
}
QPoint tabPoint(QTabBar* bar, TabId tab)
{
    for (int i = 0; i < bar->count(); ++i)
        if (bar->tabData(i).toUuid() == tab) return bar->mapToGlobal(bar->tabRect(i).center());
    return {};
}
void startTabDrag(QWidget* window, QTabBar* bar, TabId tab)
{
    const auto point = tabPoint(bar, tab);
    mouse(bar, QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
    mouse(bar, QEvent::MouseMove, point + QPoint(20, 20), Qt::NoButton, Qt::LeftButton);
    Q_UNUSED(window)
}
QPoint contentCenter(QWidget* window)
{
    for (auto* frame : window->findChildren<QWidget*>())
        if (frame->objectName() == "TabGroup" && frame->isVisible()) return frame->mapToGlobal(frame->rect().center());
    return window->mapToGlobal(window->rect().center());
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
};
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
    QTest::keyClick(window, Qt::Key_Escape);
    QCOMPARE(manager.model().windows().size(), 1);
    QCOMPARE(manager.model().groupForTab(tab), group);
    QVERIFY(QWidget::mouseGrabber() != window);
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
QTEST_MAIN(DragInteractionTest)
#include "drag_interaction_test.moc"
