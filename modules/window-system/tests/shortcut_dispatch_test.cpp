// SPDX-License-Identifier: MIT
#include <window_system/window_manager.hpp>
#include <QApplication>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QInputMethodEvent>
#include <QPlainTextEdit>
#include <QPointer>
#include <QShortcut>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <memory>

using namespace window_system;

namespace {
struct Fixture {
    WindowId window;
    TabId tab;
    QPlainTextEdit* editor = nullptr;
};
Fixture createFixture(WindowManager& manager)
{
    Fixture fixture;
    fixture.window = manager.createWindow();
    fixture.editor = new QPlainTextEdit;
    fixture.tab = manager.addTab(fixture.window, fixture.editor, {"Keyboard", "test.editor", {}});
    return fixture;
}
QVariantMap batch(QVariantList commands) { return {{"commands", std::move(commands)}}; }
QVariant step(const QString& id, QVariantMap args = {}) { return QVariantMap{{"command", id}, {"args", std::move(args)}}; }
}

class ShortcutDispatchTest : public QObject {
    Q_OBJECT
private slots:
    void multipleBindingsRetainArgumentsAndPrimaryCompatibility();
    void bindingValidationIsAtomicAndNotifiesOnlyChanges();
    void compositeCommandsRetainTargetsAndUseIndividualArguments();
    void malformedCompositeDescriptionsHaveNoSideEffects();
    void compositeFailureAndDisabledCommandsStopTheSequence();
    void compositeDepthCapacityAndReentrantCyclesAreBounded();
    void callbacksCanMutateOrDestroyTheRegistry();
    void chordLengths_data();
    void chordLengths();
    void overrideReservesWithoutAdvancingAndAvoidsNativeShortcut();
    void invalidEscapeTimeoutAndModifierInputDoNotLeak();
    void focusAndWindowChangesCancelPendingChords();
    void exactBindingWinsOverAnEarlierLongerPrefix();
    void additionalBindingArgumentsDriveCompositeCommands();
    void compositesUseNewActiveContentWithinTheirOriginalWindow();
    void compositesCloseTheNewlyFocusedGroup();
    void compositeCommandsAbortWhenTheirSourceWindowDisappears();
    void compositeFirstStepRespectsItsCapturedContext();
    void changedBindingsAndInputMethodsCancelTheChord();
    void disabledBindingsDoNotReserveNativeShortcutInput();
    void enabledCallbacksCanDestroyTheInputReceiver();
    void capturedContextIsRecheckedAfterEnabledCallbacks();
    void directTabCommandsKeepTheirOwnContextContract();
    void bindingSignalsCanDestroyTheManager();
};

void ShortcutDispatchTest::multipleBindingsRetainArgumentsAndPrimaryCompatibility()
{
    CommandRegistry registry;
    QVariantMap received;
    QVERIFY(registry.add({"host", "Host", "Test", QKeySequence("Ctrl+Q"), {},
                          [&](const CommandContext& context) { received = context.args; return true; }}));
    QCOMPARE(registry.bindings("host"), (QVector<CommandBinding>{{QKeySequence("Ctrl+Q"), {}}}));
    QVERIFY(registry.addBinding("host", {QKeySequence("Ctrl+K, A, 1"), {{"mode", "second"}}}));
    QCOMPARE(registry.bindings("host").size(), 2);
    QCOMPARE(registry.find("host")->shortcut, QKeySequence("Ctrl+Q"));
    QVERIFY(registry.removeBinding("host", 0));
    QCOMPARE(registry.find("host")->shortcut, QKeySequence("Ctrl+K, A, 1"));
    CommandContext context;
    context.args = registry.bindings("host").first().args;
    QVERIFY(registry.execute("host", context));
    QCOMPARE(received, (QVariantMap{{"mode", "second"}}));
    QVERIFY(registry.setShortcut("host", QKeySequence("Ctrl+W")));
    QCOMPARE(registry.bindings("host"), (QVector<CommandBinding>{{QKeySequence("Ctrl+W"), {}}}));
    QVERIFY(registry.setShortcut("host", {}));
    QVERIFY(registry.bindings("host").isEmpty());
    QVERIFY(registry.find("host")->shortcut.isEmpty());
}

void ShortcutDispatchTest::bindingValidationIsAtomicAndNotifiesOnlyChanges()
{
    CommandRegistry registry;
    QVERIFY(registry.add({"host", "Host", "Test", {}, {}, [](const CommandContext&) { return true; }}));
    int changes = 0;
    registry.setBindingsChangedHandler([&] { ++changes; });
    const CommandBinding binding{QKeySequence("Ctrl+K, 2"), {{"number", 2}}};
    QVERIFY(registry.setBindings("host", {binding, binding}));
    QCOMPARE(registry.bindings("host"), (QVector<CommandBinding>{binding}));
    QCOMPARE(changes, 1);
    QVERIFY(registry.setBindings("host", {binding}));
    QCOMPARE(changes, 1);
    QVERIFY(!registry.addBinding("host", {binding.sequence, {{"number", 3}}}));
    QVERIFY(!registry.addBinding("host", {{}, {}}));
    QVERIFY(!registry.setBindings("missing", {binding}));
    QVERIFY(!registry.removeBinding("host", 1));
    QCOMPARE(registry.bindings("host"), (QVector<CommandBinding>{binding}));
    QCOMPARE(changes, 1);
}

void ShortcutDispatchTest::compositeCommandsRetainTargetsAndUseIndividualArguments()
{
    CommandRegistry registry;
    CommandContext original{QUuid::createUuid(), QUuid::createUuid(), QUuid::createUuid(), QUuid::createUuid(), {}};
    QVector<CommandContext> received;
    for (const auto& id : {QStringLiteral("one"), QStringLiteral("two")})
        QVERIFY(registry.add({id, id, "Test", {}, {}, [&](const CommandContext& context) { received.append(context); return true; }}));
    original.args = batch({"one", step("runCommands", batch({step("two", {{"value", 42}})}))});
    QVERIFY(registry.execute("runCommands", original));
    QCOMPARE(received.size(), 2);
    for (const auto& context : received) {
        QCOMPARE(context.window, original.window); QCOMPARE(context.group, original.group);
        QCOMPARE(context.tab, original.tab); QCOMPARE(context.pane, original.pane);
    }
    QVERIFY(received[0].args.isEmpty());
    QCOMPARE(received[1].args, (QVariantMap{{"value", 42}}));
}

void ShortcutDispatchTest::malformedCompositeDescriptionsHaveNoSideEffects()
{
    CommandRegistry registry;
    int calls = 0;
    QVERIFY(registry.add({"one", "One", "Test", {}, {}, [&](const CommandContext&) { ++calls; return true; }}));
    for (const auto& args : {QVariantMap{}, batch({}), QVariantMap{{"commands", "one"}},
                            batch({"one", 42}), batch({"one", QVariantMap{{"command", "one"}, {"args", 42}}}),
                            batch({"one", QVariantMap{{"command", 42}}}), batch({"one", ""})}) {
        CommandContext context; context.args = args;
        QVERIFY(!registry.execute("runCommands", context));
        QCOMPARE(calls, 0);
    }
}

void ShortcutDispatchTest::compositeFailureAndDisabledCommandsStopTheSequence()
{
    CommandRegistry registry;
    QStringList seen;
    QVERIFY(registry.add({"one", "One", "Test", {}, {}, [&](const CommandContext&) { seen << "one"; return true; }}));
    QVERIFY(registry.add({"fail", "Fail", "Test", {}, {}, [&](const CommandContext&) { seen << "fail"; return false; }}));
    QVERIFY(registry.add({"disabled", "Disabled", "Test", {}, [](const CommandContext&) { return false; },
                          [&](const CommandContext&) { seen << "disabled"; return true; }}));
    for (const auto& middle : {QStringLiteral("fail"), QStringLiteral("disabled"), QStringLiteral("missing")}) {
        seen.clear(); CommandContext context; context.args = batch({"one", middle, "one"});
        QVERIFY(!registry.execute("runCommands", context));
        QCOMPARE(seen, middle == "fail" ? QStringList({"one", "fail"}) : QStringList({"one"}));
    }
}

void ShortcutDispatchTest::compositeDepthCapacityAndReentrantCyclesAreBounded()
{
    CommandRegistry registry;
    int calls = 0;
    QVERIFY(registry.add({"one", "One", "Test", {}, {}, [&](const CommandContext&) { ++calls; return true; }}));
    QVariantMap args = batch({"one"});
    for (int i = 0; i < 17; ++i) args = batch({step("runCommands", args)});
    CommandContext context; context.args = args;
    QVERIFY(!registry.execute("runCommands", context)); QCOMPARE(calls, 0);
    QVariantList bounded;
    for (int i = 0; i < 256; ++i) bounded.append("one");
    context.args = batch(bounded);
    QVERIFY(registry.execute("runCommands", context)); QCOMPARE(calls, 256);
    calls = 0;
    QVariantList oversized;
    for (int i = 0; i < 257; ++i) oversized.append("one");
    context.args = batch(oversized);
    QVERIFY(!registry.execute("runCommands", context)); QCOMPARE(calls, 0);
    QVERIFY(registry.add({"cycle", "Cycle", "Test", {}, {}, [&](const CommandContext& target) {
        ++calls; return registry.execute("cycle", target);
    }}));
    QVERIFY(!registry.execute("cycle", {})); QCOMPARE(calls, 1);
    // Failure unwinds the invocation, so an independent later call still works.
    QVERIFY(registry.execute("one", {})); QCOMPARE(calls, 2);
}

void ShortcutDispatchTest::callbacksCanMutateOrDestroyTheRegistry()
{
    auto registry = std::make_unique<CommandRegistry>();
    QVERIFY(registry->add({"grow", "Grow", "Test", {}, [&](const CommandContext&) {
        for (int i = 0; i < 100; ++i) registry->add({QString::number(i), "Added", "Test", {}, {}, [](const CommandContext&) { return true; }});
        return true;
    }, [](const CommandContext&) { return true; }}));
    QVERIFY(registry->execute("grow", {}));
    QVERIFY(registry->add({"destroy", "Destroy", "Test", {}, [&](const CommandContext&) { registry.reset(); return true; },
                          [](const CommandContext&) { QTest::qFail("Execution after registry destruction", __FILE__, __LINE__); return true; }}));
    QVERIFY(!registry->execute("destroy", {})); QVERIFY(!registry);
    registry = std::make_unique<CommandRegistry>();
    QVERIFY(registry->add({"destroy", "Destroy", "Test", {}, {}, [&](const CommandContext&) { registry.reset(); return true; }}));
    QVERIFY(!registry->execute("destroy", {})); QVERIFY(!registry);
}

void ShortcutDispatchTest::chordLengths_data()
{
    QTest::addColumn<QString>("sequence");
    QTest::addColumn<QList<int>>("keys");
    QTest::newRow("two-bare-letter") << QString("Ctrl+K, A") << QList<int>{Qt::Key_K, Qt::Key_A};
    QTest::newRow("three-bare-digit") << QString("Ctrl+K, A, 2") << QList<int>{Qt::Key_K, Qt::Key_A, Qt::Key_2};
    QTest::newRow("four-bare-keys") << QString("Ctrl+K, A, 2, Z") << QList<int>{Qt::Key_K, Qt::Key_A, Qt::Key_2, Qt::Key_Z};
}
void ShortcutDispatchTest::chordLengths()
{
    QFETCH(QString, sequence); QFETCH(QList<int>, keys);
    WindowManager manager;
    const auto fixture = createFixture(manager);
    int calls = 0;
    QVERIFY(manager.commands().add({"host.chord", "Chord", "Test", QKeySequence(sequence), {},
        [&](const CommandContext& target) { ++calls; return target.window == fixture.window && target.tab == fixture.tab; }}));
    manager.showWindow(fixture.window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(fixture.window)));
    QVERIFY(manager.focusPane(fixture.tab, manager.model().tab(fixture.tab)->activePane));
    for (qsizetype i = 0; i < keys.size(); ++i) {
        QTest::keyClick(fixture.editor, Qt::Key(keys[i]), i == 0 ? Qt::ControlModifier : Qt::NoModifier);
        QCOMPARE(calls, i == keys.size() - 1 ? 1 : 0);
    }
    QVERIFY(fixture.editor->toPlainText().isEmpty());
}

void ShortcutDispatchTest::overrideReservesWithoutAdvancingAndAvoidsNativeShortcut()
{
    WindowManager manager;
    const auto fixture = createFixture(manager);
    int calls = 0, nativeCalls = 0;
    QVERIFY(manager.commands().add({"host.chord", "Chord", "Test", QKeySequence("Ctrl+K, 3"), {},
                                  [&](const CommandContext&) { ++calls; return true; }}));
    auto* native = new QShortcut(QKeySequence("Ctrl+K"), fixture.editor);
    connect(native, &QShortcut::activated, this, [&] { ++nativeCalls; });
    manager.showWindow(fixture.window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(fixture.window)));
    QVERIFY(manager.focusPane(fixture.tab, manager.model().tab(fixture.tab)->activePane));
    QKeyEvent firstOverride(QEvent::ShortcutOverride, Qt::Key_K, Qt::ControlModifier);
    firstOverride.ignore(); QApplication::sendEvent(fixture.editor, &firstOverride);
    QVERIFY(firstOverride.isAccepted());
    auto* timer = manager.findChild<QTimer*>("CommandChordTimer"); QVERIFY(timer); QVERIFY(!timer->isActive());
    QTest::keyClick(fixture.editor, Qt::Key_K, Qt::ControlModifier);
    QVERIFY(timer->isActive());
    QKeyEvent secondOverride(QEvent::ShortcutOverride, Qt::Key_3, Qt::NoModifier);
    secondOverride.ignore(); QApplication::sendEvent(fixture.editor, &secondOverride);
    QVERIFY(secondOverride.isAccepted()); QCOMPARE(calls, 0);
    QTest::keyClick(fixture.editor, Qt::Key_3);
    QCOMPARE(calls, 1); QCOMPARE(nativeCalls, 0); QVERIFY(!timer->isActive());
    QVERIFY(fixture.editor->toPlainText().isEmpty());
}

void ShortcutDispatchTest::invalidEscapeTimeoutAndModifierInputDoNotLeak()
{
    WindowManager manager;
    const auto fixture = createFixture(manager);
    int calls = 0;
    QVERIFY(manager.commands().add({"host.chord", "Chord", "Test", QKeySequence("Ctrl+K, 3"), {},
                                  [&](const CommandContext&) { ++calls; return true; }}));
    manager.showWindow(fixture.window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(fixture.window)));
    QVERIFY(manager.focusPane(fixture.tab, manager.model().tab(fixture.tab)->activePane));
    QTest::keyClick(fixture.editor, Qt::Key_K, Qt::ControlModifier);
    QTest::keyClick(fixture.editor, Qt::Key_Shift);
    QTest::keyClick(fixture.editor, Qt::Key_3);
    QCOMPARE(calls, 1);
    QTest::keyClick(fixture.editor, Qt::Key_K, Qt::ControlModifier); QTest::keyClick(fixture.editor, Qt::Key_X);
    QVERIFY(fixture.editor->toPlainText().isEmpty()); QCOMPARE(calls, 1);
    QTest::keyClick(fixture.editor, Qt::Key_K, Qt::ControlModifier); QTest::keyClick(fixture.editor, Qt::Key_Escape);
    QVERIFY(fixture.editor->toPlainText().isEmpty()); QCOMPARE(calls, 1);
    QTest::keyClick(fixture.editor, Qt::Key_K, Qt::ControlModifier);
    auto* timer = manager.findChild<QTimer*>("CommandChordTimer"); QVERIFY(timer && timer->isActive());
    QVERIFY(QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection));
    QTest::keyClick(fixture.editor, Qt::Key_3);
    QCOMPARE(fixture.editor->toPlainText(), QString("3")); QCOMPARE(calls, 1);
}

void ShortcutDispatchTest::focusAndWindowChangesCancelPendingChords()
{
    WindowManager manager;
    const auto first = createFixture(manager), second = createFixture(manager);
    int calls = 0;
    QVERIFY(manager.commands().add({"host.chord", "Chord", "Test", QKeySequence("Ctrl+K, 3"), {},
                                  [&](const CommandContext&) { ++calls; return true; }}));
    manager.showWindow(first.window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(first.window)));
    QVERIFY(manager.focusPane(first.tab, manager.model().tab(first.tab)->activePane));
    QTest::keyClick(first.editor, Qt::Key_K, Qt::ControlModifier);
    QFocusEvent out(QEvent::FocusOut, Qt::OtherFocusReason); QApplication::sendEvent(first.editor, &out);
    QTest::keyClick(first.editor, Qt::Key_3);
    QCOMPARE(first.editor->toPlainText(), QString("3")); QCOMPARE(calls, 0);
    QTest::keyClick(first.editor, Qt::Key_K, Qt::ControlModifier);
    // Routing into another managed window must cancel even without a native
    // activation notification (for example a host-provided focus proxy).
    QTest::keyClick(second.editor, Qt::Key_3);
    QCOMPARE(second.editor->toPlainText(), QString("3")); QCOMPARE(calls, 0);
}

void ShortcutDispatchTest::exactBindingWinsOverAnEarlierLongerPrefix()
{
    WindowManager manager;
    const auto fixture = createFixture(manager);
    int shorter = 0, longer = 0;
    QVERIFY(manager.commands().add({"longer", "Longer", "Test", QKeySequence("Ctrl+Alt+J, 1"), {},
                                  [&](const CommandContext&) { ++longer; return true; }}));
    QVERIFY(manager.commands().add({"shorter", "Shorter", "Test", QKeySequence("Ctrl+Alt+J"), {},
                                  [&](const CommandContext&) { ++shorter; return true; }}));
    manager.showWindow(fixture.window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(fixture.window)));
    QTest::keyClick(fixture.editor, Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
    QCOMPARE(shorter, 1); QCOMPARE(longer, 0);
}

void ShortcutDispatchTest::additionalBindingArgumentsDriveCompositeCommands()
{
    WindowManager manager;
    const auto fixture = createFixture(manager);
    QList<int> calls;
    QVERIFY(manager.commands().add({"host", "Host", "Test", {}, {}, [&](const CommandContext& target) {
        calls.append(target.args.value("value").toInt()); return target.tab == fixture.tab;
    }}));
    QVERIFY(manager.commands().setBindings("runCommands", {
        {QKeySequence("Ctrl+Alt+7"), batch({step("host", {{"value", 1}}), step("host", {{"value", 2}})})},
        {QKeySequence("Ctrl+Alt+8"), batch({step("host", {{"value", 3}})})}
    }));
    manager.showWindow(fixture.window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(fixture.window)));
    QTest::keyClick(fixture.editor, Qt::Key_7, Qt::ControlModifier | Qt::AltModifier);
    QTest::keyClick(fixture.editor, Qt::Key_8, Qt::ControlModifier | Qt::AltModifier);
    QCOMPARE(calls, (QList<int>{1, 2, 3})); QVERIFY(fixture.editor->toPlainText().isEmpty());
}

void ShortcutDispatchTest::compositesUseNewActiveContentWithinTheirOriginalWindow()
{
    WindowManager manager;
    const auto fixture = createFixture(manager);
    manager.setContentFactory([](const TabOptions&, PaneId) { return new QPlainTextEdit; });
    TabId created;
    QVERIFY(manager.commands().add({"host.new", "New", "Test", {}, {}, [&](const CommandContext& target) {
        created = manager.addTab(target.window, new QPlainTextEdit, {"New", "test.editor", {}}, target.group);
        return !created.isNull();
    }}));
    QVERIFY(manager.execute("runCommands", batch({"host.new", "pane.split.right"}), fixture.window));
    QVERIFY(!created.isNull());
    QCOMPARE(manager.model().panes(created).size(), 2);
    QCOMPARE(manager.model().panes(fixture.tab).size(), 1);
}

void ShortcutDispatchTest::compositesCloseTheNewlyFocusedGroup()
{
    WindowManager manager;
    const auto fixture = createFixture(manager);
    const auto group = manager.model().groupForTab(fixture.tab);
    const auto next = manager.addTabToNewGroup(fixture.window, new QPlainTextEdit, {"Right", "test.editor", {}}, group, Placement::Right);
    QVERIFY(!next.isNull());
    manager.showWindow(fixture.window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(fixture.window)));
    QVERIFY(manager.activateTab(fixture.tab));
    QVERIFY(manager.execute("runCommands", batch({"workspace.focusGroup.right", "workspace.closeTab"}), fixture.window));
    QVERIFY(manager.model().tab(fixture.tab));
    QVERIFY(!manager.model().tab(next));
}

void ShortcutDispatchTest::compositeCommandsAbortWhenTheirSourceWindowDisappears()
{
    WindowManager manager;
    const auto fixture = createFixture(manager), other = createFixture(manager);
    int secondCalls = 0;
    QVERIFY(manager.commands().add({"remove", "Remove", "Test", {}, {}, [&](const CommandContext& target) {
        return manager.requestCloseWindow(target.window);
    }}));
    QVERIFY(manager.commands().add({"second", "Second", "Test", {}, {}, [&](const CommandContext&) { ++secondCalls; return true; }}));
    QVERIFY(!manager.execute("runCommands", batch({"remove", "second"}), fixture.window));
    QCOMPARE(secondCalls, 0); QVERIFY(!manager.model().window(fixture.window));
    QVERIFY(manager.model().window(other.window));
}

void ShortcutDispatchTest::compositeFirstStepRespectsItsCapturedContext()
{
    WindowManager manager;
    const auto fixture = createFixture(manager);
    auto captured = manager.context(fixture.window);
    const auto other = manager.addTab(fixture.window, new QPlainTextEdit, {"Other", "test.editor", {}});
    QVector<TabId> received;
    QVERIFY(manager.commands().add({"record", "Record", "Test", {}, {}, [&](const CommandContext& target) {
        received.append(target.tab); return true;
    }}));
    captured.args = batch({"record", "record"});
    QVERIFY(manager.executeInContext("runCommands", captured));
    QCOMPARE(received, (QVector<TabId>{fixture.tab, other}));
}

void ShortcutDispatchTest::changedBindingsAndInputMethodsCancelTheChord()
{
    WindowManager manager;
    const auto fixture = createFixture(manager);
    int calls = 0;
    QVERIFY(manager.commands().add({"host.chord", "Chord", "Test", QKeySequence("Ctrl+K, 3"), {},
                                  [&](const CommandContext&) { ++calls; return true; }}));
    manager.showWindow(fixture.window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(fixture.window)));
    auto* timer = manager.findChild<QTimer*>("CommandChordTimer"); QVERIFY(timer);
    QTest::keyClick(fixture.editor, Qt::Key_K, Qt::ControlModifier);
    QVERIFY(timer->isActive());
    QVERIFY(manager.commands().setShortcut("host.chord", QKeySequence("Ctrl+K, 4")));
    QVERIFY(!timer->isActive());
    QTest::keyClick(fixture.editor, Qt::Key_4);
    QCOMPARE(calls, 0); QCOMPARE(fixture.editor->toPlainText(), QString("4"));
    fixture.editor->clear();
    QTest::keyClick(fixture.editor, Qt::Key_K, Qt::ControlModifier);
    QInputMethodEvent preedit(QStringLiteral("拼"), {});
    QApplication::sendEvent(fixture.editor, &preedit);
    QVERIFY(!timer->isActive());
    QTest::keyClick(fixture.editor, Qt::Key_4);
    QCOMPARE(calls, 0);
    QTest::keyClick(fixture.editor, Qt::Key_K, Qt::ControlModifier);
    QInputMethodEvent commit; commit.setCommitString(QStringLiteral("拼"));
    QApplication::sendEvent(fixture.editor, &commit);
    QVERIFY(!timer->isActive()); QCOMPARE(calls, 0);
}

void ShortcutDispatchTest::disabledBindingsDoNotReserveNativeShortcutInput()
{
    WindowManager manager;
    const auto fixture = createFixture(manager);
    QVERIFY(manager.commands().add({"disabled", "Disabled", "Test", QKeySequence("Ctrl+Alt+J"),
                                  [](const CommandContext&) { return false; }, [](const CommandContext&) { return true; }}));
    QKeyEvent override(QEvent::ShortcutOverride, Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
    override.ignore(); QApplication::sendEvent(fixture.editor, &override);
    QVERIFY(!override.isAccepted());
    auto* timer = manager.findChild<QTimer*>("CommandChordTimer"); QVERIFY(timer && !timer->isActive());
}

void ShortcutDispatchTest::enabledCallbacksCanDestroyTheInputReceiver()
{
    WindowManager manager;
    const auto fixture = createFixture(manager);
    auto* receiver = new QPlainTextEdit(manager.windowWidget(fixture.window));
    QPointer<QPlainTextEdit> guarded(receiver);
    QVERIFY(manager.commands().add({"host.destroyInput", "Destroy input", "Test", QKeySequence("Ctrl+Alt+J"),
        [&](const CommandContext&) { delete guarded.data(); return false; }, [](const CommandContext&) { return true; }}));
    QKeyEvent override(QEvent::ShortcutOverride, Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
    override.ignore(); QApplication::sendEvent(receiver, &override);
    QVERIFY(!guarded);
    QVERIFY(manager.model().tab(fixture.tab));
}

void ShortcutDispatchTest::capturedContextIsRecheckedAfterEnabledCallbacks()
{
    WindowManager manager;
    const auto fixture = createFixture(manager);
    const auto captured = manager.context(fixture.window);
    const auto remaining = manager.addTab(fixture.window, new QPlainTextEdit, {"Remaining", "test.editor", {}});
    int executions = 0;
    int closedTargets = 0;
    QVERIFY(manager.commands().add({"host.closeDuringValidation", "Close during validation", "Test", {},
        [&](const CommandContext& target) {
            // Closing refreshes menus, which may query this predicate again
            // for another active tab. Mutate only the captured target once.
            if (target.tab == captured.tab && closedTargets == 0) {
                ++closedTargets;
                return manager.closeTab(target.tab);
            }
            return true;
        },
        [&](const CommandContext&) { ++executions; return true; }}));
    QVERIFY(!manager.executeInContext("host.closeDuringValidation", captured));
    QCOMPARE(executions, 0);
    QCOMPARE(closedTargets, 1);
    QVERIFY(!manager.model().tab(fixture.tab));
    QVERIFY(manager.model().tab(remaining));
}

void ShortcutDispatchTest::directTabCommandsKeepTheirOwnContextContract()
{
    WindowManager manager;
    const auto fixture = createFixture(manager);
    auto selected = manager.context(fixture.window);
    const auto other = manager.addTab(fixture.window, new QPlainTextEdit, {"Other", "test.editor", {}});
    selected.tab = other;
    int executions = 0;
    TabId target;
    QVERIFY(manager.commands().add({"host.tabOnly", "Tab only", "Test", {},
        [&](const CommandContext& context) { return manager.model().tab(context.tab) != nullptr; },
        [&](const CommandContext& context) { target = context.tab; ++executions; return true; }}));
    QVERIFY(manager.commands().execute("host.tabOnly", selected));
    QCOMPARE(target, other);
    QCOMPARE(executions, 1);
    // An old pane from another tab is invalid for captured UI execution even
    // though this direct, tab-scoped command deliberately does not use it.
    QVERIFY(!manager.executeInContext("host.tabOnly", selected));
    QCOMPARE(executions, 1);
}

void ShortcutDispatchTest::bindingSignalsCanDestroyTheManager()
{
    auto manager = std::make_unique<WindowManager>();
    QPointer<WindowManager> guarded(manager.get());
    connect(manager.get(), &WindowManager::commandsChanged, this, [&] { manager.reset(); });
    QVERIFY(!manager->commands().setShortcut("runCommands", QKeySequence("Ctrl+Alt+7")));
    QVERIFY(!guarded);
}

QTEST_MAIN(ShortcutDispatchTest)
#include "shortcut_dispatch_test.moc"
