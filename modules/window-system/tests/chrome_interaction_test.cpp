// SPDX-License-Identifier: MIT
#include "chrome/command_strip.hpp"
#include "chrome/workspace_window.hpp"

#include <window_system/theme.hpp>
#include <window_system/window_manager.hpp>

#include <QAction>
#include <QApplication>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>

#include <algorithm>
#include <memory>

using namespace window_system;

namespace {

QList<QAction*> actions(QWidget* owner, int count)
{
    QList<QAction*> result;
    for (int index = 0; index < count; ++index) {
        auto* action = new QAction(iconFor("settings"), QStringLiteral("Action %1").arg(index), owner);
        action->setObjectName(QStringLiteral("command.%1").arg(index));
        result.append(action);
    }
    return result;
}

void mouse(QWidget* target, QEvent::Type type, QPoint global, Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, QPointF(target->mapFromGlobal(global)), QPointF(global), button, buttons, Qt::NoModifier);
    QCoreApplication::sendEvent(target, &event);
    QCoreApplication::processEvents();
}

void startDrag(CommandStripHost& host, QWidget* handle, QPoint destination)
{
    const auto start = handle->mapToGlobal(handle->rect().center());
    mouse(handle, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    mouse(handle, QEvent::MouseMove, destination, Qt::NoButton, Qt::LeftButton);
    QVERIFY(host.isDragging());
}

} // namespace

class ChromeInteractionTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void narrowStripsKeepTheTailAndExposeOverflowActions();
    void hiddenActionsDoNotReserveButtonsOrOverflowEntries();
    void visibilityCustomizationAndWidthsSurviveRestore();
    void restoringLockedMinimumWidthDoesNotUseTheCurrentUnlockedMinimum();
    void titleFirstFillsTheRowAndIsAWindowDragArea();
    void toolbarDragImmediatelyMovesThePresentationAndCancelsCleanly();
    void slidingTheFollowingHandleCompressesThePreviousStrip();
    void contextMenuListsEveryStripAndOmitsUnrelatedWindowKinds();
    void stripCustomizationsSurviveWindowKindConversions();
};

void ChromeInteractionTest::initTestCase() { qApp->setQuitOnLastWindowClosed(false); }

void ChromeInteractionTest::narrowStripsKeepTheTailAndExposeOverflowActions()
{
    CommandStripHost host;
    applyTheme(&host, defaultTheme());
    const auto commands = actions(&host, 8);
    host.addActionStrip("tools", "Tools", commands, 0);
    host.resize(110, host.height()); host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    const auto overflow = host.overflowItems("tools");
    QVERIFY(!overflow.isEmpty());
    QCOMPARE(host.width(), 110);
    auto* strip = host.stripWidget("tools");
    auto* tail = strip->findChild<QWidget*>("StripOverflowButton");
    QVERIFY(tail); QVERIFY(tail->isVisible());
    QVERIFY(strip->rect().contains(tail->geometry()));
    QVERIFY(tail->property("hasOverflow").toBool());
    std::unique_ptr<QMenu> menu(host.createOverflowMenu("tools", &host));
    QVERIFY(menu);
    const auto id = overflow.first();
    const auto commandIterator = std::find_if(commands.begin(), commands.end(), [&id](QAction* action) { return action->objectName() == id; });
    QVERIFY(commandIterator != commands.end());
    auto* command = *commandIterator;
    QVERIFY(menu->actions().contains(command));
    QSignalSpy executed(command, &QAction::triggered); command->trigger(); QCOMPARE(executed.count(), 1);
    QVERIFY(menu->findChild<QMenu*>("StripCustomizeMenu"));
    host.resize(800, host.height()); QCoreApplication::processEvents();
    QVERIFY(host.overflowItems("tools").isEmpty());
    QVERIFY(tail->isVisible());
    QVERIFY(!tail->property("hasOverflow").toBool());
    std::unique_ptr<QMenu> withoutOverflow(host.createOverflowMenu("tools", &host));
    QCOMPARE(withoutOverflow->actions().size(), 1);
    QVERIFY(withoutOverflow->actions().first()->menu());
}

void ChromeInteractionTest::visibilityCustomizationAndWidthsSurviveRestore()
{
    CommandStripHost original;
    original.addActionStrip("tools", "Tools", actions(&original, 6), 0);
    original.addStrip("title", "Title", new QLabel("Workspace"), 0, true);
    original.setLocked(false);
    QVERIFY(original.setStripItemVisible("tools", "command.1", false));
    QVERIFY(original.setStripWidth("tools", 90));
    QVERIFY(original.setStripVisible("title", false));
    QVERIFY(original.moveStrip("tools", 1, 0));
    const auto snapshot = original.saveLayout();
    CommandStripHost restored;
    restored.addActionStrip("tools", "Tools", actions(&restored, 6), 0);
    restored.addStrip("title", "Title", new QLabel("Workspace"), 0, true);
    restored.restoreLayout(snapshot);
    QCOMPARE(restored.saveLayout(), snapshot);
    QVERIFY(!restored.isLocked());
    QVERIFY(!restored.isStripVisible("title"));
    QVERIFY(!restored.isStripItemVisible("tools", "command.1"));
    std::unique_ptr<QMenu> menu(restored.createOverflowMenu("tools", &restored));
    auto* customization = menu->findChild<QMenu*>("StripCustomizeMenu"); QVERIFY(customization);
    const auto customizedActions = customization->actions();
    const auto itemIterator = std::find_if(customizedActions.begin(), customizedActions.end(), [](QAction* action) {
        return action->data().toString() == "command.1";
    });
    QVERIFY(itemIterator != customizedActions.end());
    auto* item = *itemIterator;
    QVERIFY(item->isCheckable()); QVERIFY(!item->isChecked()); item->trigger();
    QVERIFY(restored.isStripItemVisible("tools", "command.1"));
}

void ChromeInteractionTest::hiddenActionsDoNotReserveButtonsOrOverflowEntries()
{
    CommandStripHost host;
    const auto commands = actions(&host, 5);
    host.addActionStrip("tools", "Tools", commands, 0);
    host.resize(80, host.height()); host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    const auto id = commands.first()->objectName();
    auto* strip = host.stripWidget("tools");
    QToolButton* button = nullptr;
    for (auto* candidate : strip->findChildren<QToolButton*>()) if (candidate->property("commandId").toString() == id) button = candidate;
    QVERIFY(button);
    QVERIFY(button->isVisible());
    commands.first()->setVisible(false); host.refreshLayout();
    QVERIFY(!button->isVisible()); QVERIFY(!host.overflowItems("tools").contains(id));
    commands.first()->setVisible(true); host.refreshLayout();
    QVERIFY(button->isVisible());
    commands.last()->setVisible(false); host.refreshLayout();
    QVERIFY(!host.overflowItems("tools").contains(commands.last()->objectName()));
}

void ChromeInteractionTest::titleFirstFillsTheRowAndIsAWindowDragArea()
{
    CommandStripHost host;
    auto* title = new QLabel("Workspace");
    host.addStrip("title", "Title", title, 0, true);
    host.addActionStrip("tools", "Tools", actions(&host, 2), 0);
    host.resize(640, host.height()); host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    const auto titleWidth = host.stripWidget("title")->width();
    QVERIFY(titleWidth > 400);
    QVERIFY(host.windowDragAreas().contains(title));
    QCOMPARE(title->alignment() & Qt::AlignHorizontal_Mask, Qt::AlignLeft);
    QVERIFY(host.moveStrip("title", 0, 1));
    QVERIFY(host.stripWidget("title")->width() < titleWidth);
    host.setLocked(false);
    auto* handle = host.stripWidget("title")->findChild<QWidget*>("StripHandle");
    QVERIFY(handle); QCOMPARE(handle->cursor().shape(), Qt::SizeAllCursor);
    QVERIFY(!qobject_cast<QToolButton*>(handle));
}

void ChromeInteractionTest::restoringLockedMinimumWidthDoesNotUseTheCurrentUnlockedMinimum()
{
    CommandStripHost original;
    original.addActionStrip("tools", "Tools", actions(&original, 4), 0);
    QVERIFY(original.isLocked()); QVERIFY(original.setStripWidth("tools", 24));
    QVERIFY(original.moveStrip("tools", 1, 0));
    const auto snapshot = original.saveLayout();
    CommandStripHost restored;
    restored.addActionStrip("tools", "Tools", actions(&restored, 4), 0);
    restored.setLocked(false);
    restored.restoreLayout(snapshot);
    QVERIFY(restored.isLocked()); QCOMPARE(restored.saveLayout(), snapshot);
}

void ChromeInteractionTest::toolbarDragImmediatelyMovesThePresentationAndCancelsCleanly()
{
    CommandStripHost host;
    host.addActionStrip("tools", "Tools", actions(&host, 6), 0);
    host.addStrip("title", "Title", new QLabel("Workspace"), 1, true);
    host.setLocked(false); host.resize(650, host.height()); host.move(150, 150); host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    const auto snapshot = host.saveLayout();
    auto* strip = host.stripWidget("tools");
    auto* handle = strip->findChild<QWidget*>("StripHandle");
    const auto destination = host.mapToGlobal(QPoint(100, 48));
    startDrag(host, handle, destination);
    QVERIFY(!strip->isVisible());
    auto* preview = host.findChild<QLabel*>("StripDragPreview");
    QVERIFY(preview); QVERIFY(preview->isVisible()); QVERIFY(preview->isWindow());
    QCOMPARE(host.saveLayout(), snapshot);
    const auto firstPosition = preview->pos();
    mouse(&host, QEvent::MouseMove, destination + QPoint(40, 0), Qt::NoButton, Qt::LeftButton);
    QCOMPARE(preview->pos(), firstPosition + QPoint(40, 0));
    QPointer<QLabel> observed = preview;
    QTest::keyClick(&host, Qt::Key_Escape);
    QVERIFY(!host.isDragging()); QVERIFY(!observed);
    QVERIFY(strip->isVisible()); QCOMPARE(host.saveLayout(), snapshot);
}

void ChromeInteractionTest::slidingTheFollowingHandleCompressesThePreviousStrip()
{
    CommandStripHost host;
    host.addActionStrip("tools", "Tools", actions(&host, 12), 0);
    host.addStrip("title", "Title", new QLabel("Workspace"), 0, true);
    host.setLocked(false); host.resize(800, host.height()); host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    auto* first = host.stripWidget("tools");
    const int originalWidth = first->width();
    auto* handle = host.stripWidget("title")->findChild<QWidget*>("StripHandle");
    const auto destination = host.mapToGlobal(QPoint(100, 12));
    startDrag(host, handle, destination);
    mouse(&host, QEvent::MouseButtonRelease, destination, Qt::LeftButton, Qt::NoButton);
    QVERIFY(!host.isDragging());
    QCOMPARE(host.stripOrder(0), QStringList({"tools", "title"}));
    QVERIFY(first->width() < originalWidth);
    QVERIFY(!host.overflowItems("tools").isEmpty());
    const int firstRight = first->geometry().right();
    QVERIFY(host.stripWidget("title")->geometry().left() > firstRight);
}

void ChromeInteractionTest::contextMenuListsEveryStripAndOmitsUnrelatedWindowKinds()
{
    WindowManager manager;
    const auto windowId = manager.createWindow();
    auto* window = dynamic_cast<WorkspaceWindow*>(manager.windowWidget(windowId));
    QVERIFY(window); QVERIFY(window->strips());
    std::unique_ptr<QMenu> menu(window->strips()->createContextMenu(window));
    QStringList listed;
    for (auto* action : menu->actions()) {
        if (action->objectName() == "StripVisibilityAction") listed.append(action->data().toString());
        QVERIFY(action->data().toString() != "window.promote");
        QVERIFY(action->data().toString() != "window.redock");
        QVERIFY(action->data().toString() != "window.demote");
    }
    QCOMPARE(listed, QStringList({"menu", "title", "workspace"}));
    const auto menuActions = menu->actions();
    const auto visibilityIterator = std::find_if(menuActions.begin(), menuActions.end(), [](QAction* action) {
        return action->objectName() == "StripVisibilityAction" && action->data().toString() == "workspace";
    });
    QVERIFY(visibilityIterator != menuActions.end());
    auto* visibility = *visibilityIterator;
    QVERIFY(visibility->isChecked()); visibility->trigger();
    QVERIFY(!window->strips()->isStripVisible("workspace"));
}

void ChromeInteractionTest::stripCustomizationsSurviveWindowKindConversions()
{
    WindowManager manager;
    const auto windowId = manager.createWindow();
    const auto otherId = manager.createWindow();
    QVERIFY(!manager.addTab(windowId, new QLabel("Content"), {"Content", "test.label", {}}).isNull());
    auto* window = dynamic_cast<WorkspaceWindow*>(manager.windowWidget(windowId)); QVERIFY(window);
    auto* strips = window->strips(); QVERIFY(strips);
    strips->setLocked(false);
    QVERIFY(strips->moveStrip("title", 1, 0));
    QVERIFY(strips->setStripVisible("workspace", false));
    QVERIFY(strips->setStripItemVisible("workspace", "pane.split.right", false));
    const auto snapshot = strips->saveLayout();
    QVERIFY(manager.demoteWindow(windowId, otherId));
    QVERIFY(!window->strips());
    QCOMPARE(window->saveStripLayout(), snapshot);
    QVERIFY(manager.promoteWindow(windowId));
    QVERIFY(window->strips());
    QCOMPARE(window->strips()->saveLayout(), snapshot);
}

QTEST_MAIN(ChromeInteractionTest)
#include "chrome_interaction_test.moc"
