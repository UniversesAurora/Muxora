// SPDX-License-Identifier: MIT
#include <window_system/window_manager.hpp>

#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTest>
#include <QTreeWidget>

using namespace window_system;

namespace {

QWidget* shortcutPage(WindowManager& manager, WindowId window)
{
    manager.execute(QStringLiteral("preferences.openKeyboardShortcuts"), {}, window);
    for (const auto& tab : manager.model().tabs())
        for (const auto pane : manager.model().panes(tab.id)) {
            auto* widget = manager.paneWidget(pane);
            if (widget && widget->objectName() == QStringLiteral("ShortcutsPage")) return widget;
        }
    return nullptr;
}

struct Editor {
    QLineEdit* search;
    QTreeWidget* commands;
    QTreeWidget* bindings;
    QKeySequenceEdit* capture;
    QPlainTextEdit* arguments;
    QPushButton* apply;
    QPushButton* add;
    QPushButton* remove;
    QPushButton* composite;
    QLabel* diagnostic;

    explicit Editor(QWidget* page)
        : search(page->findChild<QLineEdit*>(QStringLiteral("ShortcutSearch"))),
          commands(page->findChild<QTreeWidget*>(QStringLiteral("ShortcutCommands"))),
          bindings(page->findChild<QTreeWidget*>(QStringLiteral("ShortcutBindings"))),
          capture(page->findChild<QKeySequenceEdit*>(QStringLiteral("ShortcutCapture"))),
          arguments(page->findChild<QPlainTextEdit*>(QStringLiteral("ShortcutArguments"))),
          apply(page->findChild<QPushButton*>(QStringLiteral("ApplyShortcut"))),
          add(page->findChild<QPushButton*>(QStringLiteral("AddShortcut"))),
          remove(page->findChild<QPushButton*>(QStringLiteral("ClearShortcut"))),
          composite(page->findChild<QPushButton*>(QStringLiteral("AddCompositeShortcut"))),
          diagnostic(page->findChild<QLabel*>(QStringLiteral("ShortcutDiagnostic"))) {}

    bool complete() const
    {
        return search && commands && bindings && capture && arguments && apply && add && remove && composite && diagnostic;
    }
};

} // namespace

class ShortcutSettingsTest final : public QObject {
    Q_OBJECT
private slots:
    void captureAcceptsUnmodifiedContinuationLettersAndDigits();
    void multipleBindingsCanBeAddedEditedAndRemovedIndependently();
    void secondaryBindingsAppearInSearchAndConflictChecks();
    void compositeArgumentsValidateAndRetainExecutionOrder();
    void externalChangeDoesNotOverwriteAnUnfinishedBindingEdit();
};

void ShortcutSettingsTest::captureAcceptsUnmodifiedContinuationLettersAndDigits()
{
    WindowManager manager;
    const auto window = manager.createWindow();
    auto* page = shortcutPage(manager, window);
    QVERIFY(page);
    Editor editor(page);
    QVERIFY(editor.complete());
    editor.search->setText(QStringLiteral("workspace.closeTab"));
    editor.add->click();
    QTest::keyClick(editor.capture, Qt::Key_K, Qt::ControlModifier | Qt::AltModifier);
    QTest::keyClick(editor.capture, Qt::Key_G);
    QTest::keyClick(editor.capture, Qt::Key_7);
    QCOMPARE(editor.capture->keySequence(), QKeySequence(QStringLiteral("Ctrl+Alt+K, G, 7")));
    QTest::keyClick(editor.capture, Qt::Key_Tab);
    QCOMPARE(editor.capture->keySequence(), QKeySequence(QStringLiteral("Ctrl+Alt+K, G, 7, Tab")));
    QVERIFY(editor.apply->isEnabled());
    editor.apply->click();
    const auto bindings = manager.commands().bindings(QStringLiteral("workspace.closeTab"));
    QCOMPARE(bindings.size(), 2);
    QCOMPARE(bindings.last().sequence, QKeySequence(QStringLiteral("Ctrl+Alt+K, G, 7, Tab")));
}

void ShortcutSettingsTest::multipleBindingsCanBeAddedEditedAndRemovedIndependently()
{
    WindowManager manager;
    const auto window = manager.createWindow();
    auto* page = shortcutPage(manager, window);
    QVERIFY(page);
    Editor editor(page);
    QVERIFY(editor.complete());
    const QString id = QStringLiteral("workspace.closeTab");
    editor.search->setText(id);
    const auto original = manager.commands().bindings(id);
    QCOMPARE(original.size(), 1);
    editor.add->click();
    editor.capture->setKeySequence(QKeySequence(QStringLiteral("Ctrl+Alt+F10")));
    editor.arguments->setPlainText(QStringLiteral("{\"source\":\"secondary\",\"count\":2}"));
    QVERIFY(editor.apply->isEnabled());
    editor.apply->click();
    QCOMPARE(manager.commands().bindings(id).size(), 2);
    QCOMPARE(manager.commands().bindings(id).first(), original.first());
    QCOMPARE(manager.commands().bindings(id).last().args.value(QStringLiteral("count")).toInt(), 2);
    QCOMPARE(editor.bindings->topLevelItemCount(), 2);
    editor.bindings->setCurrentItem(editor.bindings->topLevelItem(1));
    editor.capture->setKeySequence(QKeySequence(QStringLiteral("Ctrl+Alt+F11")));
    editor.apply->click();
    QCOMPARE(manager.commands().bindings(id).size(), 2);
    QCOMPARE(manager.commands().bindings(id).first(), original.first());
    QCOMPARE(manager.commands().bindings(id).last().sequence, QKeySequence(QStringLiteral("Ctrl+Alt+F11")));
    editor.remove->click();
    QCOMPARE(manager.commands().bindings(id), original);
    QCOMPARE(manager.commands().find(id)->shortcut, original.first().sequence);
}

void ShortcutSettingsTest::secondaryBindingsAppearInSearchAndConflictChecks()
{
    WindowManager manager;
    const auto window = manager.createWindow();
    const QKeySequence secondary(QStringLiteral("Ctrl+Alt+F10"));
    QVERIFY(manager.commands().addBinding(QStringLiteral("window.select"), {secondary, {}}));
    auto* page = shortcutPage(manager, window);
    QVERIFY(page);
    Editor editor(page);
    QVERIFY(editor.complete());
    editor.search->setText(secondary.toString(QKeySequence::NativeText));
    QCOMPARE(editor.commands->topLevelItemCount(), 1);
    QCOMPARE(editor.commands->topLevelItem(0)->data(0, Qt::UserRole).toString(), QStringLiteral("window.select"));
    QVERIFY(editor.commands->topLevelItem(0)->text(1).contains(secondary.toString(QKeySequence::NativeText)));
    editor.search->setText(QStringLiteral("workspace.closeTab"));
    editor.capture->setKeySequence(secondary);
    QVERIFY(!editor.apply->isEnabled());
    QVERIFY(!editor.diagnostic->text().isEmpty());
}

void ShortcutSettingsTest::compositeArgumentsValidateAndRetainExecutionOrder()
{
    WindowManager manager;
    const auto window = manager.createWindow();
    QStringList events;
    QVERIFY(manager.commands().add({QStringLiteral("test.first"), QStringLiteral("First"), {}, {}, {},
        [&events](const CommandContext&) { events.append(QStringLiteral("first")); return true; }}));
    QVERIFY(manager.commands().add({QStringLiteral("test.second"), QStringLiteral("Second"), {}, {}, {},
        [&events](const CommandContext& context) { events.append(context.args.value(QStringLiteral("value")).toString()); return true; }}));
    auto* page = shortcutPage(manager, window);
    QVERIFY(page);
    Editor editor(page);
    QVERIFY(editor.complete());
    editor.composite->click();
    QCOMPARE(editor.commands->currentItem()->data(0, Qt::UserRole).toString(), QStringLiteral("runCommands"));
    editor.capture->setKeySequence(QKeySequence(QStringLiteral("Ctrl+Alt+F10")));
    editor.arguments->setPlainText(QStringLiteral("{\"commands\":[\"test.first\",{\"command\":\"test.second\",\"args\":{\"value\":\"second\"}}]}"));
    QVERIFY(editor.apply->isEnabled());
    editor.apply->click();
    const auto stored = manager.commands().bindings(QStringLiteral("runCommands"));
    QCOMPARE(stored.size(), 1);
    auto context = manager.context(window);
    context.args = stored.first().args;
    QVERIFY(manager.executeInContext(QStringLiteral("runCommands"), context));
    QCOMPARE(events, QStringList({QStringLiteral("first"), QStringLiteral("second")}));
    for (const auto& invalid : {QStringLiteral("{"), QStringLiteral("[]"), QStringLiteral("{\"commands\":[]}"),
                               QStringLiteral("{\"commands\":[\"missing.command\"]}"),
                               QStringLiteral("{\"commands\":[{\"command\":\"test.first\",\"args\":[]}]}" )}) {
        editor.arguments->setPlainText(invalid);
        QVERIFY(!editor.apply->isEnabled());
        editor.apply->click();
        QCOMPARE(manager.commands().bindings(QStringLiteral("runCommands")), stored);
    }
}

void ShortcutSettingsTest::externalChangeDoesNotOverwriteAnUnfinishedBindingEdit()
{
    WindowManager manager;
    const auto window = manager.createWindow();
    auto* page = shortcutPage(manager, window);
    QVERIFY(page);
    Editor editor(page);
    QVERIFY(editor.complete());
    const QString id = QStringLiteral("workspace.closeTab");
    editor.search->setText(id);
    const QKeySequence draft(QStringLiteral("Ctrl+Alt+F10"));
    editor.capture->setKeySequence(draft);
    const QKeySequence external(QStringLiteral("Ctrl+Alt+F11"));
    QVERIFY(manager.commands().setShortcut(id, external));
    QCOMPARE(editor.capture->keySequence(), draft);
    QVERIFY(!editor.apply->isEnabled());
    QVERIFY(!editor.remove->isEnabled());
    editor.apply->click();
    QCOMPARE(manager.commands().find(id)->shortcut, external);
    // Selecting the command again explicitly loads the external revision.
    editor.search->clear();
    editor.search->setText(id);
    QCOMPARE(editor.capture->keySequence(), external);
    QVERIFY(editor.apply->isEnabled());
}

QTEST_MAIN(ShortcutSettingsTest)
#include "shortcut_settings_test.moc"
