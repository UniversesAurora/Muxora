// SPDX-License-Identifier: MIT
#include "scripting_setup.hpp"
#include "script_services.hpp"
#include "user_preferences.hpp"
#include <window_system_lua/script_host.hpp>
#include <window_system/window_manager.hpp>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPlainTextEdit>
#include <QPointer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace window_system;
namespace {
bool write(const QString &path, QByteArray data) {
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}
QByteArray read(const QString &path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{};
}
lua_runtime::ScriptResult evaluate(window_system_lua::ScriptHost *host, QString code) {
    QSignalSpy done(host->runtime(), &lua_runtime::ScriptRuntime::finished);
    const auto job = host->run(code);
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < 5000) {
        for (const auto &item : done)
            if (item[0].toULongLong() == job)
                return qvariant_cast<lua_runtime::ScriptResult>(item[1]);
        done.wait(50);
    }
    return {false, {}, "timeout"};
}
} // namespace
class ScriptingTest final : public QObject {
    Q_OBJECT
  private slots:
    void startupLoadsScriptCommandsBeforeUserBindings();
    void reloadRetainsUserOverridesWithoutWritingIntermediateState();
    void missingConfigurationDoesNotCreateUserFiles();
    void malformedConfigurationKeepsBuiltinCommandsAndUserFiles();
    void invalidUtf8ScriptsAreRejected_data();
    void invalidUtf8ScriptsAreRejected();
    void runCommandAcceptsInlineAndFileScripts();
    void removedCommandDefaultsDoNotAccumulate();
    void notificationsHaveBoundedLifetimeAndPlainText();
    void externalProgramArgumentsAreNotShellText();
    void launchCompletedObserverMayDestroyServices();
};
void ScriptingTest::startupLoadsScriptCommandsBeforeUserBindings() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(write(dir.filePath("init.lua"), R"(
      count=0
      muxora.commands.register({id='user.action',title='Action'},function()count=count+1 end)
      muxora.keymap.set('ctrl+alt+f7','user.action')
    )"));
    const QByteArray rules =
        R"([{"key":"ctrl+alt+f7","command":"-user.action"},{"key":"ctrl+alt+f6","command":"user.action"}])";
    QVERIFY(write(dir.filePath("keybindings.json"), rules));
    WindowManager manager;
    auto window = manager.createWindow();
    manager.addTab(window, new QPlainTextEdit, {"Editor", "notes", {}});
    muxora::UserPreferences prefs(dir.path());
    muxora::AppScriptingSetup setup(&manager, prefs);
    QSignalSpy initialized(&setup, &muxora::AppScriptingSetup::initialized);
    setup.start();
    QTRY_COMPARE(initialized.size(), 1);
    QVERIFY2(initialized[0][0].toBool(), qPrintable(initialized[0][1].toString()));
    QCOMPARE(manager.commands().bindings("user.action"),
             (QVector<CommandBinding>{{QKeySequence("Ctrl+Alt+F6"), {}}}));
    QVERIFY(manager.execute("user.action", {}, window));
    auto reply = evaluate(setup.host(), "return count");
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QCOMPARE(reply.values[0].toInt(), 1);
    QVERIFY(setup.flushKeybindings());
    QCOMPARE(read(dir.filePath("keybindings.json")), rules);
}
void ScriptingTest::reloadRetainsUserOverridesWithoutWritingIntermediateState() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto path = dir.filePath("init.lua");
    const auto source = [](int value) {
        return QString("value=%1;muxora.commands.register({id='user.action',title='Action'},function()value="
                       "value+1 end);muxora.keymap.set('ctrl+alt+f7','user.action')")
            .arg(value)
            .toUtf8();
    };
    QVERIFY(write(path, source(1)));
    WindowManager manager;
    auto window = manager.createWindow();
    manager.addTab(window, new QPlainTextEdit, {"Editor", "notes", {}});
    muxora::UserPreferences prefs(dir.path());
    muxora::AppScriptingSetup setup(&manager, prefs);
    QSignalSpy initialized(&setup, &muxora::AppScriptingSetup::initialized);
    setup.start();
    QTRY_COMPARE(initialized.size(), 1);
    QVERIFY(initialized[0][0].toBool());
    QVERIFY(manager.commands().setBindings("user.action", {{QKeySequence("Ctrl+Alt+F6"), {}}}));
    QVERIFY(setup.flushKeybindings());
    const auto saved = read(dir.filePath("keybindings.json"));
    QVERIFY(!saved.isEmpty());
    for (int i = 2; i <= 8; ++i) {
        QVERIFY(write(path, source(i)));
        QVERIFY(setup.reload());
        QTRY_COMPARE(initialized.size(), i);
        QVERIFY(initialized.last()[0].toBool());
        QCOMPARE(read(dir.filePath("keybindings.json")), saved);
        QCOMPARE(manager.commands().bindings("user.action")[0].sequence, QKeySequence("Ctrl+Alt+F6"));
        const auto reply = evaluate(setup.host(), "return value");
        QVERIFY(reply.ok);
        QCOMPARE(reply.values[0].toInt(), i);
    }
}
void ScriptingTest::missingConfigurationDoesNotCreateUserFiles() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    WindowManager manager;
    manager.createWindow();
    muxora::UserPreferences prefs(dir.path());
    muxora::AppScriptingSetup setup(&manager, prefs);
    QSignalSpy initialized(&setup, &muxora::AppScriptingSetup::initialized);
    setup.start();
    QTRY_COMPARE(initialized.size(), 1);
    QVERIFY(initialized[0][0].toBool());
    QVERIFY(!QFileInfo::exists(dir.filePath("init.lua")));
    QVERIFY(!QFileInfo::exists(dir.filePath("keybindings.json")));
    QVERIFY(setup.createConfigurationFile());
    QVERIFY(read(dir.filePath("init.lua")).contains("muxora.keymap.set"));
}
void ScriptingTest::malformedConfigurationKeepsBuiltinCommandsAndUserFiles() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QByteArray invalid = "this is not a valid lua script";
    QVERIFY(write(dir.filePath("init.lua"), invalid));
    const QByteArray bindings = R"([{"key":"ctrl+alt+f4","command":"window.close"}])";
    QVERIFY(write(dir.filePath("keybindings.json"), bindings));
    WindowManager manager;
    manager.createWindow();
    muxora::UserPreferences prefs(dir.path());
    muxora::AppScriptingSetup setup(&manager, prefs);
    QSignalSpy initialized(&setup, &muxora::AppScriptingSetup::initialized);
    setup.start();
    QTRY_COMPARE(initialized.size(), 1);
    QVERIFY(!initialized[0][0].toBool());
    QVERIFY(manager.commands().find("lua.reload"));
    QCOMPARE(manager.commands().bindings("window.close").last().sequence, QKeySequence("Ctrl+Alt+F4"));
    QCOMPARE(read(dir.filePath("init.lua")), invalid);
    QCOMPARE(read(dir.filePath("keybindings.json")), bindings);
}
void ScriptingTest::runCommandAcceptsInlineAndFileScripts() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    WindowManager manager;
    auto window = manager.createWindow();
    manager.addTab(window, new QPlainTextEdit, {"Editor", "notes", {}});
    muxora::UserPreferences prefs(dir.path());
    muxora::AppScriptingSetup setup(&manager, prefs);
    QSignalSpy initialized(&setup, &muxora::AppScriptingSetup::initialized);
    setup.start();
    QTRY_COMPARE(initialized.size(), 1);
    QVERIFY(initialized[0][0].toBool());
    QVERIFY(manager.execute("lua.run", {{"code", "answer=20"}}, window));
    QVERIFY(write(dir.filePath("more.lua"), "answer=answer+22"));
    QVERIFY(manager.execute("lua.run", {{"file", "more.lua"}}, window));
    auto reply = evaluate(setup.host(), "return answer");
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QCOMPARE(reply.values[0].toInt(), 42);
    QVERIFY(!manager.execute("lua.run", {{"code", "return 1"}, {"file", "more.lua"}}, window));
}
void ScriptingTest::invalidUtf8ScriptsAreRejected_data() {
    QTest::addColumn<bool>("startup");
    QTest::addColumn<QByteArray>("invalidTail");
    QTest::newRow("init-invalid-byte") << true << QByteArray::fromHex("ff");
    QTest::newRow("init-incomplete-two-byte") << true << QByteArray::fromHex("c2");
    QTest::newRow("init-incomplete-three-byte") << true << QByteArray::fromHex("e282");
    QTest::newRow("run-invalid-byte") << false << QByteArray::fromHex("ff");
    QTest::newRow("run-incomplete-two-byte") << false << QByteArray::fromHex("c2");
    QTest::newRow("run-incomplete-three-byte") << false << QByteArray::fromHex("e282");
}
void ScriptingTest::invalidUtf8ScriptsAreRejected() {
    QFETCH(bool, startup);
    QFETCH(QByteArray, invalidTail);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto fileName = startup ? QStringLiteral("init.lua") : QStringLiteral("invalid.lua");
    const QByteArray source = QByteArray("rejected_execution=true ") + invalidTail;
    QVERIFY(write(dir.filePath(fileName), source));
    WindowManager manager;
    const auto window = manager.createWindow();
    manager.addTab(window, new QPlainTextEdit, {"Editor", "notes", {}});
    muxora::UserPreferences prefs(dir.path());
    muxora::AppScriptingSetup setup(&manager, prefs);
    QSignalSpy initialized(&setup, &muxora::AppScriptingSetup::initialized);
    QSignalSpy diagnostics(&setup, &muxora::AppScriptingSetup::diagnostic);
    setup.start();
    QTRY_COMPARE(initialized.size(), 1);
    if (startup) {
        QVERIFY(!initialized[0][0].toBool());
        QVERIFY(initialized[0][1].toString().contains("UTF-8"));
    } else {
        QVERIFY(initialized[0][0].toBool());
        QVERIFY(!manager.execute("lua.run", {{"file", fileName}}, window));
        QVERIFY(!diagnostics.isEmpty());
        QVERIFY(diagnostics.last()[0].toString().contains("UTF-8"));
    }
    const auto reply = evaluate(setup.host(), "return rejected_execution==nil");
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QCOMPARE(reply.values, QVariantList{true});
    QCOMPARE(read(dir.filePath(fileName)), source);
}
void ScriptingTest::removedCommandDefaultsDoNotAccumulate() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    muxora::UserPreferences prefs(dir.path());
    CommandRegistry commands;
    QVERIFY(prefs.loadKeybindings(commands));
    const auto base = prefs.trackedDefaultCommandCount();
    for (int i = 0; i < 100; ++i) {
        const auto id = QString("user.temporary.%1").arg(i);
        QVERIFY(commands.add({id, "Temporary", "Test", {}, {}, [](const CommandContext &) { return true; }}));
        QVERIFY(prefs.stageKeybindings(commands));
        QCOMPARE(prefs.trackedDefaultCommandCount(), base + 1);
        QVERIFY(commands.remove(id));
        QVERIFY(prefs.stageKeybindings(commands));
        QCOMPARE(prefs.trackedDefaultCommandCount(), base);
        QVERIFY(!prefs.hasPendingKeybindingChanges());
    }
    const auto id = QString("user.persisted");
    QVERIFY(commands.add({id, "Persisted", "Test", {}, {}, [](const CommandContext &) { return true; }}));
    QVERIFY(prefs.stageKeybindings(commands));
    QVERIFY(commands.addBinding(id, {QKeySequence("Ctrl+Alt+F7"), {}}));
    QVERIFY(prefs.stageKeybindings(commands));
    QVERIFY(commands.remove(id));
    QVERIFY(prefs.saveKeybindings(commands));
    QVERIFY(read(dir.filePath("keybindings.json")).contains("user.persisted"));
    QCOMPARE(prefs.trackedDefaultCommandCount(), base);
}
void ScriptingTest::notificationsHaveBoundedLifetimeAndPlainText() {
    WindowManager manager;
    manager.createWindow();
    muxora::AppScriptServices services(&manager);
    for (int i = 0; i < 16; ++i)
        QVERIFY(services.invoke("ui.notify", {QVariantMap{{"text", "<b>literal</b>"}, {"timeout_ms", 0}}}, {})
                    .ok);
    QCOMPARE(services.notificationCount(), 16);
    QVERIFY(!services.invoke("ui.notify", {"excess"}, {}).ok);
    services.reset();
    QCOMPARE(services.notificationCount(), 0);
    QVERIFY(!services.invoke("app.play_sound", {QVariantMap{}}, {}).ok);
}
void ScriptingTest::externalProgramArgumentsAreNotShellText() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto output = dir.filePath("参数 with spaces.json");
    WindowManager manager;
    manager.createWindow();
    muxora::AppScriptServices services(&manager);
    QSignalSpy launched(&services, &muxora::AppScriptServices::launchCompleted);
    const auto binary = QDir(QCoreApplication::applicationDirPath()).filePath("muxora-script-child.exe");
    const QVariantList arguments{output, QStringLiteral("a b"), QStringLiteral("literal & | ; $()"),
                                 QStringLiteral("中文")};
    auto reply = services.invoke("app.launch", {QVariantMap{{"program", binary}, {"args", arguments}}}, {});
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QTRY_COMPARE(launched.size(), 1);
    QVERIFY2(launched[0][1].toLongLong() > 0, qPrintable(launched[0][2].toString()));
    QTRY_VERIFY(QFileInfo::exists(output));
    const auto actual = QJsonDocument::fromJson(read(output)).array();
    QCOMPARE(actual, QJsonArray({"a b", "literal & | ; $()", QStringLiteral("中文")}));
    QCOMPARE(services.pendingLaunchCount(), 0);
}
void ScriptingTest::launchCompletedObserverMayDestroyServices() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto output = dir.filePath("launch-observer.json");
    WindowManager manager;
    manager.createWindow();
    auto services = std::make_unique<muxora::AppScriptServices>(&manager);
    const QPointer<muxora::AppScriptServices> retained(services.get());
    int completed = 0;
    int published = 0;
    qint64 launchedPid = 0;
    QString launchError;
    connect(services.get(), &muxora::AppScriptServices::eventPublished, this, [&published] { ++published; });
    connect(services.get(), &muxora::AppScriptServices::launchCompleted, this,
            [&](const QString &, qint64 pid, const QString &error) {
                ++completed;
                launchedPid = pid;
                launchError = error;
                services.reset();
            });
    const auto binary = QDir(QCoreApplication::applicationDirPath()).filePath("muxora-script-child.exe");
    const auto reply = services->invoke(
        "app.launch", {QVariantMap{{"program", binary}, {"args", QVariantList{output}}}}, {});
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QTRY_COMPARE(completed, 1);
    QVERIFY2(launchedPid > 0, qPrintable(launchError));
    QVERIFY(!retained);
    QCOMPARE(published, 0);
    QTRY_VERIFY(QFileInfo::exists(output));
    QCOMPARE(QJsonDocument::fromJson(read(output)).array(), QJsonArray{});
}
QTEST_MAIN(ScriptingTest)
#include "scripting_test.moc"
