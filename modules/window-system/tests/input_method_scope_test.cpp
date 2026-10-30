// SPDX-License-Identifier: MIT
#include "input_method_scopes.hpp"
#include "settings/settings_page.hpp"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QJsonObject>
#include <QLineEdit>
#include <QTest>

using namespace window_system;
namespace {
class FakeBackend final : public InputMethodBackend {
public:
    int current = 1;
    int reads = 0;
    QVector<int> restored;
    InputMethodState capture(QWindow*) override { ++reads; return current; }
    void restore(const InputMethodState& value, QWindow*) override {
        current = std::any_cast<int>(value); restored.append(current);
    }
};
struct Fixture {
    LayoutModel model;
    WindowId window = model.createWindow();
    TabId first = model.addTab(window, "First");
    TabId second = model.addTab(window, "Second");
    PaneId firstPane = model.tab(first)->activePane;
    PaneId secondPane = model.tab(second)->activePane;
    QWidget top;
    QLineEdit a{&top};
    QLineEdit b{&top};
    FakeBackend* backend = new FakeBackend;
    InputMethodScopes scopes{std::unique_ptr<InputMethodBackend>(backend)};
    Fixture() {
        top.setProperty("windowId", window);
        a.setProperty("paneId", firstPane); b.setProperty("paneId", secondPane);
    }
};
}
class InputMethodScopeTest final : public QObject {
    Q_OBJECT
private slots:
    void scopeKeysMatchHierarchy_data() {
        QTest::addColumn<int>("scope");
        for (int i = 0; i != 6; ++i) QTest::newRow(qPrintable(inputMethodScopeKey(InputMethodScope(i)))) << i;
    }
    void scopeKeysMatchHierarchy() {
        QFETCH(int, scope);
        Fixture f;
        const auto kind = InputMethodScope(scope);
        f.scopes.setScope(kind);
        const auto pane = f.model.splitPane(f.first, f.firstPane, Placement::Right);
        const auto other = f.model.addTabToNewGroup(f.window, "Other", f.model.groupForTab(f.first), Placement::Right);
        const auto floating = f.model.createWindow(WindowKind::Floating, f.window);
        const auto floatingTab = f.model.addTab(floating, "Floating");
        const auto independent = f.model.createWindow();
        const auto independentTab = f.model.addTab(independent, "Independent");
        QVERIFY(!pane.isNull() && !other.isNull() && !floatingTab.isNull() && !independentTab.isNull());
        const auto base = f.scopes.key(f.model, f.firstPane);
        QCOMPARE(base == f.scopes.key(f.model, pane), kind != InputMethodScope::Pane);
        QCOMPARE(base == f.scopes.key(f.model, f.secondPane), scope <= int(InputMethodScope::TabGroup));
        QCOMPARE(base == f.scopes.key(f.model, f.model.tab(other)->activePane), scope <= int(InputMethodScope::FloatingWindow));
        QCOMPARE(base == f.scopes.key(f.model, f.model.tab(floatingTab)->activePane), scope <= int(InputMethodScope::Window));
        QCOMPARE(base == f.scopes.key(f.model, f.model.tab(independentTab)->activePane), kind == InputMethodScope::Application);
        QCOMPARE(inputMethodScopeFromKey(inputMethodScopeKey(kind)), std::optional{kind});
        QVERIFY(f.scopes.key(f.model, QUuid::createUuid()).isNull());
    }
    void switchingRestoresWholeOpaqueValues() {
        Fixture f;
        f.scopes.synchronize(f.model); f.scopes.focus(f.model, &f.a);
        f.backend->current = 17; f.scopes.focus(f.model, &f.b);
        QCOMPARE(f.backend->current, 1);
        f.backend->current = 42; f.scopes.focus(f.model, &f.a);
        QCOMPARE(f.backend->current, 17);
        f.scopes.focus(f.model, &f.b);
        QCOMPARE(f.backend->current, 42);
        QCOMPARE(f.backend->restored, QVector<int>({1, 17, 42}));
    }
    void temporaryUiAndSameRangeNeverCallBackend() {
        Fixture f;
        f.scopes.synchronize(f.model); f.scopes.focus(f.model, &f.a);
        const auto reads = f.backend->reads;
        QLineEdit child(&f.a); QDialog dialog(&f.a); QLineEdit input(&dialog); QWidget external;
        f.scopes.focus(f.model, &child); f.scopes.focus(f.model, &input);
        f.scopes.focus(f.model, nullptr); f.scopes.focus(f.model, &external); f.scopes.focus(f.model, &f.a);
        QCOMPARE(f.backend->reads, reads);
        QVERIFY(f.backend->restored.isEmpty());
    }
    void creationInheritsBeforeItsFirstFocus() {
        Fixture f;
        f.scopes.synchronize(f.model); f.scopes.focus(f.model, &f.a);
        f.backend->current = 27;
        const auto created = f.model.addTab(f.window, "Created");
        QLineEdit c(&f.top); c.setProperty("paneId", f.model.tab(created)->activePane);
        f.scopes.synchronize(f.model);
        f.backend->current = 39; f.scopes.focus(f.model, &c);
        QCOMPARE(f.backend->current, 27);
        f.scopes.focus(f.model, &f.a);
        QCOMPARE(f.backend->current, 39);
    }
    void movingPreservesFineScopesAndUsesCoarseDestination_data() { scopeKeysMatchHierarchy_data(); }
    void movingPreservesFineScopesAndUsesCoarseDestination() {
        QFETCH(int, scope);
        Fixture f;
        f.scopes.setScope(InputMethodScope(scope));
        const auto otherWindow = f.model.createWindow();
        const auto target = f.model.addTab(otherWindow, "Target");
        QWidget otherTop; otherTop.setProperty("windowId", otherWindow);
        QLineEdit c(&otherTop); c.setProperty("paneId", f.model.tab(target)->activePane);
        f.scopes.synchronize(f.model); f.scopes.focus(f.model, &c);
        f.backend->current = 70; f.scopes.focus(f.model, &f.a);
        f.backend->current = 80;
        QVERIFY(f.model.moveTab(f.first, f.model.groupForTab(target)));
        f.scopes.synchronize(f.model);
        f.a.setParent(&otherTop); f.scopes.focus(f.model, &f.a);
        const auto current = f.backend->current;
        f.a.setParent(&f.top);
        QCOMPARE(current, scope >= int(InputMethodScope::Tab) || scope == 0 ? 80 : 70);
    }
    void deletedActiveRangeRestoresSurvivingRange() {
        Fixture f;
        f.scopes.synchronize(f.model); f.scopes.focus(f.model, &f.a);
        f.backend->current = 12; f.scopes.focus(f.model, &f.b);
        f.backend->current = 24;
        QVERIFY(f.model.removeTab(f.second)); f.scopes.synchronize(f.model);
        QCOMPARE(f.scopes.count(), 1);
        f.scopes.focus(f.model, &f.a); QCOMPARE(f.backend->current, 12);
        QVERIFY(f.model.removeTab(f.first)); f.scopes.synchronize(f.model);
        QCOMPARE(f.scopes.count(), 0);
    }
    void mergeDropsOnlyTheDisappearingRange() {
        Fixture f;
        f.scopes.setScope(InputMethodScope::TabGroup);
        const auto added = f.model.addTabToNewGroup(f.window, "Added", f.model.groupForTab(f.first), Placement::Right);
        f.scopes.synchronize(f.model); QCOMPARE(f.scopes.count(), 2);
        QVERIFY(f.model.mergeGroups(f.model.groupForTab(added), f.model.groupForTab(f.first)));
        f.scopes.synchronize(f.model); QCOMPARE(f.scopes.count(), 1);
    }
    void paneMoveUsesItsOwnOrItsNewTabsState_data() {
        QTest::addColumn<bool>("byPane");
        QTest::newRow("pane") << true;
        QTest::newRow("tab") << false;
    }
    void paneMoveUsesItsOwnOrItsNewTabsState() {
        QFETCH(bool, byPane);
        Fixture f;
        f.scopes.setScope(byPane ? InputMethodScope::Pane : InputMethodScope::Tab);
        QVERIFY(!f.model.splitPane(f.first, f.firstPane, Placement::Right).isNull());
        f.scopes.synchronize(f.model); f.scopes.focus(f.model, &f.b);
        f.backend->current = 9; f.scopes.focus(f.model, &f.a);
        f.backend->current = 18;
        QVERIFY(f.model.movePane(f.first, f.firstPane, f.second, f.secondPane, Placement::Right));
        f.scopes.synchronize(f.model); f.scopes.focus(f.model, &f.a);
        QCOMPARE(f.backend->current, byPane ? 18 : 9);
    }
    void settingChangesClearStatesAndValidateAtomically() {
        Fixture f;
        f.scopes.synchronize(f.model); f.scopes.focus(f.model, &f.a);
        f.backend->current = 64; f.scopes.setScope(InputMethodScope::Pane);
        QCOMPARE(f.scopes.count(), 0);
        f.scopes.synchronize(f.model); f.scopes.focus(f.model, &f.a); f.scopes.focus(f.model, &f.b);
        QCOMPARE(f.backend->current, 64);
        QVERIFY(!inputMethodScopeFromKey("unknown"));
        QVERIFY(inputMethodScopeKey(InputMethodScope(-1)).isEmpty());
        WindowManager manager;
        auto behavior = manager.behavior();
        behavior.inputMethodScope = InputMethodScope(99); behavior.closeEmptyWorkbench = true;
        manager.setBehavior(behavior);
        QCOMPARE(manager.behavior().inputMethodScope, InputMethodScope::Tab);
        QVERIFY(!manager.behavior().closeEmptyWorkbench);
    }
    void settingsAndPersistenceShareAllSixValues() {
        WindowManager manager;
        const auto window = manager.createWindow();
        manager.addTab(window, new QLineEdit, {"Editor", "test.input", {}});
        SettingsPage page(&manager);
        auto* combo = page.findChild<QComboBox*>("InputMethodScope");
        QVERIFY(combo); QCOMPARE(combo->count(), 6);
        QCOMPARE(combo->currentData().toInt(), int(InputMethodScope::Tab));
        for (int index = 0; index != 6; ++index) {
            combo->setCurrentIndex(combo->findData(index));
            QCOMPARE(manager.behavior().inputMethodScope, InputMethodScope(index));
            auto saved = manager.saveLayout();
            QCOMPARE(saved["behavior"].toObject()["inputMethodScope"].toString(), inputMethodScopeKey(InputMethodScope(index)));
            WindowManager restored;
            restored.setContentFactory([](const TabOptions&, PaneId) { return new QLineEdit; });
            QVERIFY(restored.restoreLayout(saved));
            QCOMPARE(restored.behavior().inputMethodScope, InputMethodScope(index));
            auto behavior = manager.behavior(); behavior.inputMethodScope = InputMethodScope((index + 1) % 6);
            manager.setBehavior(behavior);
            QCOMPARE(combo->currentData().toInt(), (index + 1) % 6);
        }
        for (const auto& invalid : {QJsonValue("invalid"), QJsonValue(4), QJsonValue(true), QJsonValue(QJsonObject{})}) {
            auto saved = manager.saveLayout(); auto behavior = saved["behavior"].toObject();
            behavior["inputMethodScope"] = invalid; saved["behavior"] = behavior;
            const auto previous = manager.saveLayout();
            QVERIFY(!manager.restoreLayout(saved)); QCOMPARE(manager.saveLayout(), previous);
        }
    }
};
QTEST_MAIN(InputMethodScopeTest)
#include "input_method_scope_test.moc"
