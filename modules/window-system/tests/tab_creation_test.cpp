// SPDX-License-Identifier: MIT
#include <window_system/window_manager.hpp>
#include "docking/tab_strip.hpp"

#include <QAction>
#include <QApplication>
#include <QJsonArray>
#include <QLabel>
#include <QMenu>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>
#include <memory>

using namespace window_system;

namespace {
GroupTabs* groupView(WindowManager& manager, WindowId window, GroupId group)
{
    for (auto* widget : manager.windowWidget(window)->findChildren<QWidget*>("TabGroup"))
        if (widget->property("groupId").toUuid() == group) return dynamic_cast<GroupTabs*>(widget);
    return nullptr;
}
TabId add(WindowManager& manager, WindowId window, const QString& title, GroupId group = {}, int index = -1)
{
    auto* content = new QLabel(title);
    const auto tab = manager.addTab(window, content, {title, "test.label", {}}, group, index);
    if (tab.isNull()) delete content;
    return tab;
}
void registerCreation(WindowManager& manager, const QString& id, QVector<CommandContext>& received)
{
    manager.commands().add({id, id, "Test", {}, {}, [&manager, &received](const CommandContext& ctx) {
        received.append(ctx);
        return !add(manager, ctx.window, "Created", ctx.group).isNull();
    }});
}
} // namespace

class TabCreationTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); }
    void cleanup() { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }
    void positionFollowsActiveTab_data();
    void positionFollowsActiveTab();
    void explicitIndexOverridesBehaviorAndModelStillAppends();
    void invalidInsertionPreservesOwnershipAndSnapshot();
    void settingRoundTripsAndOmissionDefaultsToAfterActive();
    void invalidSettingRejectedBeforeFactory_data();
    void invalidSettingRejectedBeforeFactory();
    void creationUsesClickedGroupInsteadOfFocusedGroup();
    void creationCanStartInAnEmptyWindow();
    void catalogValidationIsAtomicAndDisabledCommandsDoNotCreate();
    void staleTargetFromEnabledCallbackCannotCreateElsewhere();
    void enabledCallbackMayDestroyManagerAndPopupParent();
    void executionMayDestroyManager();
    void explicitContextRejectsStaleTargetsAndDeletionDuringQuery();
};

void TabCreationTest::positionFollowsActiveTab_data()
{
    QTest::addColumn<int>("position"); QTest::addColumn<int>("index");
    QTest::newRow("beginning") << int(NewTabPosition::Beginning) << 0;
    QTest::newRow("before-active") << int(NewTabPosition::BeforeActive) << 1;
    QTest::newRow("after-active-default") << int(NewTabPosition::AfterActive) << 2;
    QTest::newRow("end") << int(NewTabPosition::End) << 3;
}
void TabCreationTest::positionFollowsActiveTab()
{
    QFETCH(int, position); QFETCH(int, index);
    WindowManager manager; const auto window = manager.createWindow();
    const auto first = add(manager, window, "First");
    const auto second = add(manager, window, "Second");
    const auto third = add(manager, window, "Third");
    const auto group = manager.model().groupForTab(first);
    QVector<TabId> expected{first, second, third};
    QVERIFY(manager.activateTab(second));
    auto behavior = manager.behavior(); behavior.newTabPosition = NewTabPosition(position); manager.setBehavior(behavior);
    const auto created = add(manager, window, "New", group); QVERIFY(!created.isNull());
    expected.insert(index, created);
    QCOMPARE(manager.model().group(group)->tabs, expected);
    QCOMPARE(manager.model().group(group)->activeTab, created);
    QVERIFY(manager.model().validate());
}
void TabCreationTest::explicitIndexOverridesBehaviorAndModelStillAppends()
{
    WindowManager manager; const auto window = manager.createWindow();
    const auto first = add(manager, window, "First"); const auto second = add(manager, window, "Second");
    const auto group = manager.model().groupForTab(first); QVERIFY(manager.activateTab(first));
    auto behavior = manager.behavior(); behavior.newTabPosition = NewTabPosition::Beginning; manager.setBehavior(behavior);
    const auto end = add(manager, window, "Explicit end", group, 2);
    QCOMPARE(manager.model().group(group)->tabs, (QVector<TabId>{first, second, end}));
    const auto front = add(manager, window, "Explicit front", group, 0);
    QCOMPARE(manager.model().group(group)->tabs, (QVector<TabId>{front, first, second, end}));
    LayoutModel model; const auto root = model.createWindow();
    const auto a = model.addTab(root, "A"); const auto b = model.addTab(root, "B");
    QVERIFY(model.activateTab(a)); const auto c = model.addTab(root, "C");
    QCOMPARE(model.group(model.groupForTab(a))->tabs, (QVector<TabId>{a, b, c}));
    const auto inserted = model.addTab(root, "Inserted", model.groupForTab(a), 1);
    QCOMPARE(model.group(model.groupForTab(a))->tabs, (QVector<TabId>{a, inserted, b, c}));
    const auto before = model.toJson(); QVERIFY(model.addTab(root, "Invalid", {}, 999).isNull()); QCOMPARE(model.toJson(), before);
}
void TabCreationTest::invalidInsertionPreservesOwnershipAndSnapshot()
{
    WindowManager manager; const auto window = manager.createWindow(); add(manager, window, "First");
    const auto other = manager.createWindow(); const auto foreign = add(manager, other, "Foreign");
    QLabel content; const auto before = manager.saveLayout();
    for (const int index : {-2, 999}) {
        QVERIFY(manager.addTab(window, &content, {"Invalid", {}, {}}, {}, index).isNull());
        QVERIFY(!content.parentWidget()); QVERIFY(!content.property("windowSystemManaged").toBool());
        QCOMPARE(manager.saveLayout(), before);
    }
    QVERIFY(manager.addTab(window, &content, {"Foreign", {}, {}}, manager.model().groupForTab(foreign)).isNull());
    QVERIFY(!content.parentWidget()); QCOMPARE(manager.saveLayout(), before);
    auto behavior = manager.behavior(); behavior.newTabPosition = NewTabPosition(99); manager.setBehavior(behavior);
    QCOMPARE(manager.behavior().newTabPosition, NewTabPosition::AfterActive);
}
void TabCreationTest::settingRoundTripsAndOmissionDefaultsToAfterActive()
{
    WindowManager manager; const auto window = manager.createWindow(); add(manager, window, "Saved");
    auto behavior = manager.behavior(); behavior.newTabPosition = NewTabPosition::BeforeActive; manager.setBehavior(behavior);
    auto snapshot = manager.saveLayout();
    WindowManager restored; restored.setContentFactory([](const TabOptions&, PaneId) { return new QLabel; });
    QVERIFY(restored.restoreLayout(snapshot)); QCOMPARE(restored.behavior().newTabPosition, NewTabPosition::BeforeActive);
    auto settings = snapshot.value("behavior").toObject(); settings.remove("newTabPosition"); snapshot.insert("behavior", settings);
    QVERIFY(restored.restoreLayout(snapshot)); QCOMPARE(restored.behavior().newTabPosition, NewTabPosition::AfterActive);
    const auto first = restored.context(window).tab; const auto second = add(restored, window, "Second");
    QVERIFY(restored.activateTab(first)); const auto inserted = add(restored, window, "After first");
    QCOMPARE(restored.model().group(restored.model().groupForTab(first))->tabs, (QVector<TabId>{first, inserted, second}));
}
void TabCreationTest::invalidSettingRejectedBeforeFactory_data()
{
    QTest::addColumn<QJsonValue>("value");
    QTest::newRow("negative") << QJsonValue(-1);
    QTest::newRow("out-of-range") << QJsonValue(4);
    QTest::newRow("fraction") << QJsonValue(1.5);
    QTest::newRow("bool") << QJsonValue(true);
    QTest::newRow("string") << QJsonValue("after");
    QTest::newRow("null") << QJsonValue(QJsonValue::Null);
}
void TabCreationTest::invalidSettingRejectedBeforeFactory()
{
    QFETCH(QJsonValue, value);
    WindowManager manager; const auto window = manager.createWindow(); add(manager, window, "Original");
    const auto before = manager.saveLayout(); auto malformed = before;
    auto settings = malformed.value("behavior").toObject(); settings.insert("newTabPosition", value); malformed.insert("behavior", settings);
    int factoryCalls = 0; manager.setContentFactory([&](const TabOptions&, PaneId) { ++factoryCalls; return new QLabel; });
    QVERIFY(!manager.restoreLayout(malformed)); QCOMPARE(factoryCalls, 0); QCOMPARE(manager.saveLayout(), before);
}
void TabCreationTest::creationUsesClickedGroupInsteadOfFocusedGroup()
{
    WindowManager manager; const auto window = manager.createWindow();
    const auto left = add(manager, window, "Left"); const auto right = add(manager, window, "Right");
    QVERIFY(manager.moveTab(right, manager.model().groupForTab(left), Placement::Right));
    const auto leftGroup = manager.model().groupForTab(left), rightGroup = manager.model().groupForTab(right);
    QVector<CommandContext> received;
    registerCreation(manager, "test.new", received); registerCreation(manager, "test.files", received);
    QVERIFY(manager.setTabCreationCommands("test.new", {"test.files"}));
    manager.showWindow(window); QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    QVERIFY(manager.activateTab(right));
    auto* view = groupView(manager, window, leftGroup); QVERIFY(view);
    auto* button = view->findChild<QToolButton*>("NewTabButton"); QVERIFY(button);
    QTest::mouseClick(button, Qt::LeftButton);
    QCOMPARE(received.size(), 1); QCOMPARE(received.last().group, leftGroup); QCOMPARE(received.last().tab, left);
    QCOMPARE(manager.model().group(leftGroup)->tabs.size(), 2); QCOMPARE(manager.model().group(rightGroup)->tabs.size(), 1);
    QVERIFY(manager.activateTab(right)); view = groupView(manager, window, leftGroup); QVERIFY(view);
    auto* types = view->findChild<QToolButton*>("NewTabTypeButton"); QVERIFY(types); QTest::mouseClick(types, Qt::LeftButton);
    QPointer<QMenu> menu = view->findChild<QMenu*>("TabCreationMenu"); QVERIFY(menu); QTRY_VERIFY(menu->isVisible());
    QCOMPARE(menu->actions().size(), 2); QCOMPARE(menu->actions().first()->objectName(), QStringLiteral("test.new"));
    auto* action = menu->findChild<QAction*>("test.files"); QVERIFY(action); action->trigger();
    QCOMPARE(received.size(), 2); QCOMPARE(received.last().group, leftGroup);
    QCOMPARE(manager.model().group(leftGroup)->tabs.size(), 3); QCOMPARE(manager.model().group(rightGroup)->tabs.size(), 1);
    QVERIFY(manager.model().validate());
}
void TabCreationTest::catalogValidationIsAtomicAndDisabledCommandsDoNotCreate()
{
    WindowManager manager; const auto window = manager.createWindow(); const auto tab = add(manager, window, "Original");
    const auto group = manager.model().groupForTab(tab); QVector<CommandContext> received;
    registerCreation(manager, "test.new", received);
    int blockedExecutions = 0;
    manager.commands().add({"test.blocked", "Blocked", "Test", {}, [](const CommandContext&) { return false; },
        [&](const CommandContext&) { ++blockedExecutions; return true; }});
    QVERIFY(manager.setTabCreationCommands("test.new", {"test.new", "test.blocked"}));
    QVERIFY(!manager.setTabCreationCommands("missing", {})); QVERIFY(!manager.setTabCreationCommands("test.new", {"test.new", "test.new"}));
    QVERIFY(!manager.setTabCreationCommands("test.new", {"missing"}));
    QVERIFY(manager.requestNewTab(window, group)); QCOMPARE(received.size(), 1);
    QVERIFY(!manager.requestNewTab(window, group, "test.blocked")); QCOMPARE(blockedExecutions, 0);
    QVERIFY(!manager.requestNewTab({}, group)); QVERIFY(!manager.requestNewTab(window, QUuid::createUuid()));
    QVERIFY(manager.setTabCreationCommands({}, {})); QVERIFY(!manager.requestNewTab(window, group));
}
void TabCreationTest::creationCanStartInAnEmptyWindow()
{
    WindowManager manager; const auto window = manager.createWindow(); QVector<CommandContext> received;
    registerCreation(manager, "test.new", received); QVERIFY(manager.setTabCreationCommands("test.new", {"test.new"}));
    QVERIFY(manager.model().orderedTabs(window).isEmpty()); QVERIFY(manager.requestNewTab(window));
    QCOMPARE(received.size(), 1); QCOMPARE(received.first().window, window);
    QVERIFY(received.first().group.isNull()); QVERIFY(received.first().tab.isNull()); QVERIFY(received.first().pane.isNull());
    const auto first = manager.context(window).tab; const auto group = manager.model().groupForTab(first);
    QVERIFY(!first.isNull()); QVERIFY(manager.requestNewTab(window));
    QCOMPARE(received.size(), 2); QCOMPARE(received.last().group, group); QCOMPARE(received.last().tab, first);
    QCOMPARE(manager.model().group(group)->tabs.size(), 2); QVERIFY(manager.model().validate());
}
void TabCreationTest::staleTargetFromEnabledCallbackCannotCreateElsewhere()
{
    WindowManager manager; const auto window = manager.createWindow();
    const auto original = add(manager, window, "Original"); const auto removed = add(manager, window, "Remove");
    QVERIFY(manager.moveTab(removed, manager.model().groupForTab(original), Placement::Right));
    const auto target = manager.model().groupForTab(removed); int executions = 0;
    manager.commands().add({"test.new", "New", "Test", {}, [&](const CommandContext&) { manager.closeTab(removed); return true; },
        [&](const CommandContext&) { ++executions; return true; }});
    QVERIFY(manager.setTabCreationCommands("test.new", {"test.new"}));
    QVERIFY(!manager.requestNewTab(window, target)); QCOMPARE(executions, 0);
    QVERIFY(!manager.model().group(target)); QVERIFY(manager.model().tab(original));
}
void TabCreationTest::enabledCallbackMayDestroyManagerAndPopupParent()
{
    auto manager = std::make_unique<WindowManager>(); const auto window = manager->createWindow();
    const auto tab = add(*manager, window, "Original"); const auto group = manager->model().groupForTab(tab);
    manager->commands().add({"test.destroy", "Destroy", "Test", {}, [&](const CommandContext&) { manager.reset(); return true; },
        [](const CommandContext&) { return true; }});
    QVERIFY(manager->setTabCreationCommands("test.destroy", {"test.destroy"}));
    auto* view = groupView(*manager, window, group); QVERIFY(view);
    QPointer<WindowManager> guard = manager.get();
    const auto callback = view->newTabMenuRequested;
    QVERIFY(!callback(view)); QVERIFY(!guard);
}
void TabCreationTest::executionMayDestroyManager()
{
    auto manager = std::make_unique<WindowManager>(); const auto window = manager->createWindow();
    const auto tab = add(*manager, window, "Original"); const auto group = manager->model().groupForTab(tab);
    manager->commands().add({"test.destroy", "Destroy", "Test", {}, {},
        [&](const CommandContext&) { manager.reset(); return true; }});
    QVERIFY(manager->setTabCreationCommands("test.destroy", {"test.destroy"}));
    QPointer<WindowManager> guard = manager.get(); auto* raw = manager.get();
    QVERIFY(!raw->requestNewTab(window, group)); QVERIFY(!guard);
}
void TabCreationTest::explicitContextRejectsStaleTargetsAndDeletionDuringQuery()
{
    auto manager = std::make_unique<WindowManager>(); const auto window = manager->createWindow();
    add(*manager, window, "Original"); const auto target = manager->context(window); int executions = 0;
    manager->commands().add({"test.context", "Context", "Test", {}, {}, [&](const CommandContext&) { ++executions; return true; }});
    auto invalid = target; invalid.pane = QUuid::createUuid(); QVERIFY(!manager->executeInContext("test.context", invalid));
    invalid = target; invalid.window = manager->createWindow(); QVERIFY(!manager->executeInContext("test.context", invalid));
    invalid.group = GroupId{}; QVERIFY(!manager->executeInContext("test.context", invalid)); QCOMPARE(executions, 0);
    QVERIFY(manager->executeInContext("test.context", target)); QCOMPARE(executions, 1);
    manager->commands().add({"test.destroyQuery", "Destroy query", "Test", {},
        [&](const CommandContext&) { manager.reset(); return true; }, [&](const CommandContext&) { ++executions; return true; }});
    auto* raw = manager.get(); QVERIFY(!raw->executeInContext("test.destroyQuery", target));
    QVERIFY(!manager); QCOMPARE(executions, 1);
}

QTEST_MAIN(TabCreationTest)
#include "tab_creation_test.moc"
