// SPDX-License-Identifier: MIT
#include "translation_checks.hpp"
#include "chrome/status_bar.hpp"
#include "chrome/workspace_window.hpp"
#include <window_system/window_manager.hpp>

#include <QAction>
#include <QApplication>
#include <QJsonArray>
#include <QLabel>
#include <QMenu>
#include <QPointer>
#include <QTest>
#include <QToolButton>
#include <QDir>
#include <algorithm>
#include <functional>
#include <memory>

using namespace window_system;

namespace {
WorkspaceStatusBar* status(WindowManager& manager, WindowId window)
{
    return manager.windowWidget(window)->findChild<WorkspaceStatusBar*>("StatusBar");
}
QAction* itemAction(QMenu* menu, const QString& id)
{
    for (auto* action : menu->actions()) if (action->data().toString() == id) return action;
    return nullptr;
}
class DeferDeletionOnHide final : public QObject {
public:
    std::function<void()> removeOwner;
    bool armed = false;
protected:
    bool eventFilter(QObject*, QEvent* event) override
    {
        if (!armed || event->type() != QEvent::Hide) return false;
        armed = false;
        removeOwner();
        return false;
    }
};
} // namespace

class StatusBarTest final : public QObject {
    Q_OBJECT
    translation_test::ChineseUiCatalog chineseUi_;
private slots:
    void initTestCase() { QVERIFY(chineseUi_.install()); qApp->setQuitOnLastWindowClosed(false); }
    void cleanup() { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }
    void contextMenuReflectsEachControlAndCommandsUpdateIt();
    void controlsRemainIndependentOfWholeBarVisibility();
    void optionsAndWindowKindChangesKeepTheSamePreference();
    void preferencesRoundTripWithoutChangingOtherWindows();
    void invalidIdsAndTypesDoNotMutatePreferences();
    void statusContentReflectsMessagesAndCurrentTabs();
    void menuActionsDoNotOutliveTheManager();
    void controlVisibilityCallbacksMayDeferManagerDestruction();
    void heightFollowsThemeMetricAndFont();
    void commandItemsUseTheirOwnWindowAndSupportKeyboard();
    void commandItemsUseTheirOwnWindowAndSupportKeyboard_data();
    void removedCommandItemsCancelQueuedClicks();
    void emptyCommandItemsPreserveVisibilityPreferences();
};

void StatusBarTest::contextMenuReflectsEachControlAndCommandsUpdateIt()
{
    WindowManager manager; const auto window = manager.createWindow();
    manager.addTab(window, new QLabel("Content"), {"First", {}, {}});
    auto* bar = status(manager, window); QVERIFY(bar);
    std::unique_ptr<QMenu> menu(bar->createContextMenu());
    QCOMPARE(menu->objectName(), QStringLiteral("StatusBarMenu"));
    QCOMPARE(menu->actions().size(), 2);
    for (const auto& id : WorkspaceStatusBar::itemIds()) {
        auto* action = itemAction(menu.get(), id); QVERIFY(action);
        QVERIFY(action->isCheckable()); QVERIFY(action->isChecked()); action->trigger();
        QTRY_VERIFY(!manager.statusItemVisible(window, id));
        auto* content = bar->findChild<QLabel*>(id == "message" ? "StatusMessage" : "StatusWorkspace");
        QVERIFY(content); QVERIFY(content->isHidden());
        std::unique_ptr<QMenu> updated(bar->createContextMenu()); QVERIFY(!itemAction(updated.get(), id)->isChecked());
        itemAction(updated.get(), id)->trigger(); QTRY_VERIFY(manager.statusItemVisible(window, id));
        QVERIFY(!content->isHidden());
    }
}

void StatusBarTest::controlsRemainIndependentOfWholeBarVisibility()
{
    WindowManager manager; const auto window = manager.createWindow();
    manager.addTab(window, new QLabel("Content"), {"First", {}, {}});
    QVERIFY(manager.setStatusItemVisible(window, "message", false));
    QVERIFY(manager.setStatusBarVisible(window, false)); QVERIFY(!manager.statusItemVisible(window, "message"));
    QVERIFY(manager.statusItemVisible(window, "workspace"));
    QVERIFY(manager.setStatusBarVisible(window, true));
    auto* bar = status(manager, window); QVERIFY(bar);
    QVERIFY(bar->findChild<QLabel*>("StatusMessage")->isHidden());
    QVERIFY(!bar->findChild<QLabel*>("StatusWorkspace")->isHidden());
}

void StatusBarTest::optionsAndWindowKindChangesKeepTheSamePreference()
{
    WindowManager manager; WindowOptions options; options.hiddenStatusItems = {"workspace"};
    const auto owner = manager.createWindow();
    manager.addTab(owner, new QLabel("Owner"), {"Owner", {}, {}});
    const auto floating = manager.createWindow(WindowKind::Floating, options, owner);
    manager.addTab(floating, new QLabel("Movable"), {"Movable", {}, {}});
    QVERIFY(!status(manager, floating)); QVERIFY(!manager.statusItemVisible(floating, "workspace"));
    QVERIFY(manager.promoteWindow(floating)); auto* bar = status(manager, floating); QVERIFY(bar);
    QVERIFY(bar->findChild<QLabel*>("StatusWorkspace")->isHidden());
    QVERIFY(manager.setStatusItemVisible(floating, "message", false));
    QVERIFY(manager.demoteWindow(floating, owner)); QVERIFY(!status(manager, floating));
    QVERIFY(manager.promoteWindow(floating)); bar = status(manager, floating); QVERIFY(bar);
    QVERIFY(bar->findChild<QLabel*>("StatusMessage")->isHidden());
    QVERIFY(bar->findChild<QLabel*>("StatusWorkspace")->isHidden());
}

void StatusBarTest::preferencesRoundTripWithoutChangingOtherWindows()
{
    WindowManager manager; const auto first = manager.createWindow(); const auto second = manager.createWindow();
    manager.addTab(first, new QLabel("One"), {"One", "label", {}});
    manager.addTab(second, new QLabel("Two"), {"Two", "label", {}});
    QVERIFY(manager.setStatusItemVisible(first, "message", false));
    QVERIFY(manager.setStatusItemVisible(second, "workspace", false));
    const auto snapshot = manager.saveLayout(); WindowManager restored;
    restored.setContentFactory([](const TabOptions& options, PaneId) { return new QLabel(options.title); });
    QVERIFY(restored.restoreLayout(snapshot));
    QVERIFY(!restored.statusItemVisible(first, "message")); QVERIFY(restored.statusItemVisible(first, "workspace"));
    QVERIFY(restored.statusItemVisible(second, "message")); QVERIFY(!restored.statusItemVisible(second, "workspace"));
    QCOMPARE(restored.model().toJson(), manager.model().toJson());
}

void StatusBarTest::invalidIdsAndTypesDoNotMutatePreferences()
{
    WindowManager manager; const auto window = manager.createWindow();
    const auto before = manager.saveLayout();
    QVERIFY(!manager.setStatusItemVisible(window, "unknown", false));
    QVERIFY(!manager.setStatusItemVisible(QUuid::createUuid(), "message", false));
    QVERIFY(!manager.execute("window.statusItem.setVisible", {{"item", "message"}, {"visible", 1}}, window));
    QVERIFY(!manager.execute("window.statusItem.setVisible", {{"item", "unknown"}, {"visible", false}}, window));
    QCOMPARE(manager.saveLayout(), before);
}

void StatusBarTest::statusContentReflectsMessagesAndCurrentTabs()
{
    WindowManager manager; const auto window = manager.createWindow();
    const auto first = manager.addTab(window, new QLabel("One"), {"First", {}, {}});
    const auto second = manager.addTab(window, new QLabel("Two"), {"Second", {}, {}});
    auto* bar = status(manager, window); QVERIFY(bar);
    manager.setStatus(window, QStringLiteral("Saved"));
    QCOMPARE(bar->findChild<QLabel*>("StatusMessage")->text(), QStringLiteral("Saved"));
    QVERIFY(manager.activateTab(first));
    QCOMPARE(bar->findChild<QLabel*>("StatusWorkspace")->text(), QStringLiteral("First  ·  2 个标签"));
    QVERIFY(manager.activateTab(second));
    QCOMPARE(bar->findChild<QLabel*>("StatusWorkspace")->text(), QStringLiteral("Second  ·  2 个标签"));
}

void StatusBarTest::menuActionsDoNotOutliveTheManager()
{
    auto manager = std::make_unique<WindowManager>(); const auto window = manager->createWindow();
    auto* bar = status(*manager, window); QVERIFY(bar);
    std::unique_ptr<QMenu> menu(bar->createContextMenu());
    auto* action = itemAction(menu.get(), "message"); QVERIFY(action);
    action->trigger(); manager.reset(); QCoreApplication::processEvents();
    action->trigger(); QCoreApplication::processEvents(); QVERIFY(action);
}

void StatusBarTest::controlVisibilityCallbacksMayDeferManagerDestruction()
{
    auto manager = std::make_unique<WindowManager>(); const auto window = manager->createWindow();
    manager->addTab(window, new QLabel("Content"), {"First", {}, {}});
    manager->showWindow(window); QVERIFY(QTest::qWaitForWindowExposed(manager->windowWidget(window)));
    auto* bar = status(*manager, window); QVERIFY(bar);
    auto* message = bar->findChild<QLabel*>("StatusMessage"); QVERIFY(message); QVERIFY(message->isVisible());
    const QPointer<WindowManager> retained(manager.get());
    const QPointer<QWidget> retainedWindow(manager->windowWidget(window));
    const QPointer<WorkspaceStatusBar> retainedBar(bar);
    const QPointer<QLabel> retainedMessage(message);
    int managerDestroyed = 0, barDestroyed = 0, pendingCallbacks = 0;
    connect(manager.get(), &QObject::destroyed, this, [&] { ++managerDestroyed; });
    connect(bar, &QObject::destroyed, this, [&] { ++barDestroyed; });
    DeferDeletionOnHide observer; observer.armed = true;
    // Releasing unique ownership before deleteLater prevents double deletion.
    // Direct deletion here invalidates Qt's still-active hide_helper stack.
    observer.removeOwner = [&] { manager.release()->deleteLater(); };
    message->installEventFilter(&observer);
    QVERIFY(manager->setStatusItemVisible(window, "message", false));
    QVERIFY(!manager); QVERIFY(retained); QVERIFY(retainedBar); QVERIFY(retainedMessage);
    QVERIFY(!observer.armed); QVERIFY(retainedMessage->isHidden());
    QVERIFY(!retained->statusItemVisible(window, "message"));
    QVERIFY(retained->statusItemVisible(window, "workspace"));
    QCOMPARE(managerDestroyed, 0); QCOMPARE(barDestroyed, 0);
    QMetaObject::invokeMethod(retained, [&] { ++pendingCallbacks; }, Qt::QueuedConnection);
    QMetaObject::invokeMethod(retainedBar, [&] { ++pendingCallbacks; }, Qt::QueuedConnection);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(!retained); QVERIFY(!retainedWindow); QVERIFY(!retainedBar); QVERIFY(!retainedMessage);
    QCOMPARE(managerDestroyed, 1); QCOMPARE(barDestroyed, 1);
    QCoreApplication::processEvents(); QCOMPARE(pendingCallbacks, 0);
}

void StatusBarTest::heightFollowsThemeMetricAndFont()
{
    WindowManager manager; const auto window = manager.createWindow();
    manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    auto* bar = status(manager, window); QVERIFY(bar);
    // VS Code's 22 px bar is the default whenever the font fits inside it.
    QCOMPARE(ThemeMetrics{}.statusHeight, 22);
    QCOMPARE(bar->height(), std::max(22, bar->fontMetrics().height() + 6));
    auto theme = manager.theme(); theme.metrics.statusHeight = 30; manager.setTheme(theme);
    QTRY_COMPARE(bar->height(), 30);
    theme.metrics.statusHeight = 22; manager.setTheme(theme);
    QTRY_COMPARE(bar->height(), std::max(22, bar->fontMetrics().height() + 6));
    // A larger font expands the bar instead of clipping its text.
    auto font = bar->font(); font.setPointSizeF(std::max(qreal(24), font.pointSizeF() * 2));
    bar->setFont(font);
    QTRY_VERIFY(bar->fontMetrics().height() + 6 > 22);
    QTRY_COMPARE(bar->height(), bar->fontMetrics().height() + 6);
}

void StatusBarTest::commandItemsUseTheirOwnWindowAndSupportKeyboard_data()
{
    QTest::addColumn<QString>("appearance");
    for (const auto& name : {QString("light"), QString("dark"), QString("black")})
        QTest::newRow(qPrintable(name)) << name;
}

void StatusBarTest::commandItemsUseTheirOwnWindowAndSupportKeyboard()
{
    QFETCH(QString, appearance);
    WindowManager manager;
    auto theme = manager.theme();
    if (appearance != "light") {
        theme.statusBackground = QColor(appearance == "black" ? "#000000" : "#252526");
        theme.statusText = QColor("#f0f0f0"); theme.accentSoft = QColor("#393343");
        theme.text = QColor("#f0f0f0");
    }
    manager.setTheme(theme);
    const auto first = manager.createWindow(), second = manager.createWindow();
    const auto firstTab = manager.addTab(first, new QLabel("First"), {"First", {}, {}});
    manager.addTab(second, new QLabel("Second"), {"Second", {}, {}});
    QVector<CommandContext> calls;
    QVERIFY(manager.commands().add({"test.status", "Status", {}, {}, {},
        [&](const CommandContext& context) { calls.append(context); return true; }}));
    QVERIFY(!manager.registerStatusItem({"bad", "Bad", {}, " invalid "}));
    QVERIFY(manager.registerStatusItem({"running", "Lua 脚本运行中", "停止正在运行的 Lua 脚本", "test.status"}));
    auto* button = status(manager, first)->findChild<QToolButton*>("CustomStatusCommandItem"); QVERIFY(button);
    manager.showWindow(first); QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(first)));
    QVERIFY(!button->isHidden()); QVERIFY(button->focusPolicy() & Qt::TabFocus);
    QVERIFY(status(manager, first)->rect().contains(QRect(button->mapTo(status(manager, first), QPoint()), button->size())));
    QTest::mouseClick(button, Qt::LeftButton); QTRY_COMPARE(calls.size(), 1);
    QCOMPARE(calls.first().window, first); QCOMPARE(calls.first().tab, firstTab);
    button->setFocus(); QTest::keyClick(button, Qt::Key_Space); QTRY_COMPARE(calls.size(), 2);
    QCOMPARE(calls.last().window, first);
    const auto review = qEnvironmentVariable("MUXORA_UI_REVIEW_DIRECTORY");
    if (!review.isEmpty()) {
        QVERIFY(QDir().mkpath(review));
        QVERIFY(status(manager, first)->grab().save(QDir(review).filePath("status-" + appearance + ".png")));
    }
}

void StatusBarTest::removedCommandItemsCancelQueuedClicks()
{
    auto manager = std::make_unique<WindowManager>();
    const auto window = manager->createWindow();
    int calls = 0;
    QVERIFY(manager->commands().add({"test.status", "Status", {}, {}, {},
        [&](const CommandContext&) { ++calls; return true; }}));
    QVERIFY(manager->registerStatusItem({"running", "Running", {}, "test.status"}));
    auto* button = status(*manager, window)->findChild<QToolButton*>("CustomStatusCommandItem"); QVERIFY(button);
    button->click(); QVERIFY(manager->removeStatusItem("running"));
    // Reusing the same ID and command must not revive the old button's click.
    QVERIFY(manager->registerStatusItem({"running", "Replacement", {}, "test.status"}));
    QCoreApplication::processEvents(); QCOMPARE(calls, 0);
    button = status(*manager, window)->findChild<QToolButton*>("CustomStatusCommandItem"); QVERIFY(button);
    button->click(); QCoreApplication::processEvents(); QCOMPARE(calls, 1);
    calls = 0;
    button->click(); manager.reset(); QCoreApplication::processEvents(); QCOMPARE(calls, 0);
}

void StatusBarTest::emptyCommandItemsPreserveVisibilityPreferences()
{
    WindowManager manager; const auto window = manager.createWindow();
    QVERIFY(manager.commands().add({"test.status", "Status", {}, {}, {}, [](const CommandContext&) { return true; }}));
    QVERIFY(manager.registerStatusItem({"running", {}, {}, "test.status"}));
    auto* button = status(manager, window)->findChild<QToolButton*>("CustomStatusCommandItem"); QVERIFY(button);
    QVERIFY(button->isHidden());
    std::unique_ptr<QMenu> menu(status(manager, window)->createContextMenu());
    QVERIFY(itemAction(menu.get(), "running"));
    QCOMPARE(itemAction(menu.get(), "running")->text(), QString("Status"));
    QVERIFY(manager.setStatusItemText("running", "Running")); QVERIFY(!button->isHidden());
    QVERIFY(manager.setStatusItemVisible(window, "running", false)); QVERIFY(button->isHidden());
    QVERIFY(manager.setStatusItemText("running", {}));
    QVERIFY(manager.setStatusItemText("running", "Running again")); QVERIFY(button->isHidden());
    QVERIFY(manager.setStatusItemVisible(window, "running", true)); QVERIFY(!button->isHidden());
    QVERIFY(manager.setStatusItemText("running", {})); QVERIFY(button->isHidden());
}

QTEST_MAIN(StatusBarTest)
#include "status_bar_test.moc"
