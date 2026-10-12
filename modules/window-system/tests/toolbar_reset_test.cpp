// SPDX-License-Identifier: MIT
#include "chrome/command_strip.hpp"
#include "chrome/workspace_window.hpp"

#include <window_system/window_manager.hpp>

#include <QAction>
#include <QApplication>
#include <QJsonObject>
#include <QMenu>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

#include <memory>

using namespace window_system;

namespace {

WindowId createFixture(WindowManager& manager, bool floating)
{
    const auto root = manager.createWindow();
    return floating ? manager.createWindow(WindowKind::Floating, {}, root) : root;
}

void windowKinds()
{
    QTest::addColumn<bool>("floating");
    QTest::newRow("workbench") << false;
    QTest::newRow("floating") << true;
}

QJsonObject stripState(WorkspaceWindow* window, const QString& id)
{
    return window->saveStripLayout().value("strips").toObject().value(id).toObject();
}

} // namespace

class ToolbarResetTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); }
    void publicApiAndExplicitCommandRestoreNaturalWidth_data() { windowKinds(); }
    void publicApiAndExplicitCommandRestoreNaturalWidth();
    void optionsMenuDispatchesTheRegisteredCommand_data() { windowKinds(); }
    void optionsMenuDispatchesTheRegisteredCommand();
    void pickerSupportsKeyboardSelectionAndCancellation_data();
    void pickerSupportsKeyboardSelectionAndCancellation();
    void invalidRequestsLeaveTheLayoutUnchanged();
    void closingTheWindowDismissesItsPicker();
};

void ToolbarResetTest::publicApiAndExplicitCommandRestoreNaturalWidth()
{
    QFETCH(bool, floating);
    WindowManager manager;
    const auto id = createFixture(manager, floating);
    auto* window = static_cast<WorkspaceWindow*>(manager.windowWidget(id)); QVERIFY(window);
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(window));
    auto* strips = window->strips(); QVERIFY(strips);
    auto* controls = strips->stripWidget("caption-controls"); QVERIFY(controls);
    const int natural = controls->width();
    const auto order = strips->stripOrder(0);
    QVERIFY(strips->setStripWidth("caption-controls", natural + 70));
    QVERIFY(manager.resetToolbarWidth(id, "caption-controls"));
    QCOMPARE(controls->width(), natural);
    QCOMPARE(strips->stripOrder(0), order);
    QCOMPARE(stripState(window, "caption-controls").value("width").toInt(), -1);
    QVERIFY(strips->setStripWidth("caption-controls", natural + 90));
    QVERIFY(manager.execute("window.toolbar.resetSize", {{"strip", "caption-controls"}}, id));
    QCOMPARE(controls->width(), natural);
    QCOMPARE(strips->stripOrder(0), order);
    QCOMPARE(strips->stripSizing("title"), CommandStripHost::StripSizing::Elastic);
    QVERIFY(!strips->isStripOptionsEnabled("caption-controls"));
}

void ToolbarResetTest::optionsMenuDispatchesTheRegisteredCommand()
{
    QFETCH(bool, floating);
    WindowManager manager;
    const auto id = createFixture(manager, floating);
    auto* window = static_cast<WorkspaceWindow*>(manager.windowWidget(id)); QVERIFY(window);
    auto* strips = window->strips(); QVERIFY(strips);
    auto* action = new QAction("Custom command", strips);
    action->setObjectName("custom.command");
    strips->addActionStrip("custom", "Custom strip", {action}, 1);
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(window));
    auto* custom = strips->stripWidget("custom"); QVERIFY(custom);
    const int natural = custom->width();
    QVERIFY(strips->setStripWidth("custom", natural + 100));
    std::unique_ptr<QMenu> menu(strips->createOverflowMenu("custom", window)); QVERIFY(menu);
    auto* reset = menu->findChild<QAction*>("StripResetWidthAction"); QVERIFY(reset);
    QVERIFY(manager.commands().find("window.toolbar.resetSize"));
    QSignalSpy failures(&manager, &WindowManager::commandFailed);
    QSignalSpy changes(&manager, &WindowManager::layoutChanged);
    reset->trigger();
    QCOMPARE(failures.count(), 0);
    QVERIFY(changes.count() > 0);
    QCOMPARE(custom->width(), natural);
    QCOMPARE(stripState(window, "custom").value("width").toInt(), -1);
}

void ToolbarResetTest::pickerSupportsKeyboardSelectionAndCancellation_data()
{
    QTest::addColumn<bool>("cancel");
    QTest::newRow("choose") << false;
    QTest::newRow("cancel") << true;
}

void ToolbarResetTest::pickerSupportsKeyboardSelectionAndCancellation()
{
    QFETCH(bool, cancel);
    WindowManager manager;
    const auto id = manager.createWindow();
    auto* window = static_cast<WorkspaceWindow*>(manager.windowWidget(id)); QVERIFY(window);
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(window));
    auto* strips = window->strips();
    const int natural = strips->stripWidget("workspace")->width();
    QVERIFY(strips->setStripWidth("workspace", natural + 80));
    const auto before = window->saveStripLayout();
    bool interacted = false;
    bool watchdogFired = false;
    QTimer::singleShot(30, &manager, [&] {
        auto* picker = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        QVERIFY(picker); QCOMPARE(picker->objectName(), QStringLiteral("ToolbarResetPicker"));
        if (cancel) QTest::keyClick(picker, Qt::Key_Escape);
        else {
            QTest::keyClick(picker, Qt::Key_End);
            QVERIFY(picker->activeAction());
            QCOMPARE(picker->activeAction()->data().toString(), QStringLiteral("workspace"));
            QTest::keyClick(picker, Qt::Key_Return);
        }
        interacted = true;
    });
    QTimer::singleShot(900, &manager, [&] {
        if (auto* picker = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
            watchdogFired = true; picker->close();
        }
    });
    QVERIFY(manager.execute("window.toolbar.resetSize", {}, id));
    QVERIFY(interacted);
    QVERIFY(!watchdogFired);
    if (cancel) QCOMPARE(window->saveStripLayout(), before);
    else QCOMPARE(strips->stripWidget("workspace")->width(), natural);
    QVERIFY(!QApplication::activePopupWidget());
}

void ToolbarResetTest::invalidRequestsLeaveTheLayoutUnchanged()
{
    WindowManager manager;
    const auto id = manager.createWindow();
    const auto before = manager.saveLayout();
    QVERIFY(!manager.resetToolbarWidth({}, "title"));
    QVERIFY(!manager.resetToolbarWidth(id, ""));
    QVERIFY(!manager.resetToolbarWidth(id, "missing"));
    QVERIFY(!manager.execute("window.toolbar.resetSize", {{"strip", "missing"}}, id));
    QCOMPARE(manager.saveLayout(), before);
}

void ToolbarResetTest::closingTheWindowDismissesItsPicker()
{
    WindowManager manager;
    const auto id = manager.createWindow();
    manager.showWindow(id);
    bool closed = false;
    bool watchdogFired = false;
    QTimer::singleShot(30, &manager, [&] { closed = manager.requestCloseWindow(id); });
    QTimer::singleShot(900, &manager, [&] {
        if (auto* picker = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
            watchdogFired = true; picker->close();
        }
    });
    QVERIFY(!manager.execute("window.toolbar.resetSize", {}, id));
    QVERIFY(closed);
    QVERIFY(!watchdogFired);
    QVERIFY(!manager.model().window(id));
    QVERIFY(!QApplication::activePopupWidget());
}

QTEST_MAIN(ToolbarResetTest)
#include "toolbar_reset_test.moc"
