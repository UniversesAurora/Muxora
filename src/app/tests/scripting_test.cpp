// SPDX-License-Identifier: MIT
#include "scripting_setup.hpp"
#include "script_services.hpp"
#include "user_preferences.hpp"
#include <window_system_lua/script_host.hpp>
#include <window_system/window_manager.hpp>
#include <window_system/localization.hpp>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

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
bool releaseBlockedProcess(QTemporaryDir &directory) {
    bool valid = false;
    const auto pid = read(directory.filePath("blocked-ready")).toULongLong(&valid);
    if (!valid || !pid)
        return false;
#ifdef Q_OS_WIN
    const auto process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
    if (!process)
        return false;
    const auto close = qScopeGuard([&] { CloseHandle(process); });
    if (!write(directory.filePath("release"), "release"))
        return false;
    if (WaitForSingleObject(process, 5000) != WAIT_OBJECT_0)
        return false;
    DWORD result = 1;
    return GetExitCodeProcess(process, &result) && result == 0;
#else
    Q_UNUSED(pid);
    return false;
#endif
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
    void generatedConfigurationUsesRequestedLanguage_data();
    void generatedConfigurationUsesRequestedLanguage();
    void malformedConfigurationKeepsBuiltinCommandsAndUserFiles();
    void invalidUtf8ScriptsAreRejected_data();
    void invalidUtf8ScriptsAreRejected();
    void runCommandAcceptsInlineAndFileScripts();
    void removedCommandDefaultsDoNotAccumulate();
    void notificationsHaveBoundedLifetimeAndPlainText();
    void externalProgramArgumentsAreNotShellText();
    void launchCompletedObserverMayDestroyServices();
    void utf8ProcessManifestRetainsApplicationCompatibility();
    void standardLibraryHandlesChinesePathsAndEnvironment();
    void directScriptTransportAllowsFilesLargerThanOneMiB();
    void osExitUsesApplicationCommandAndHonorsClosePermission();
    void osExitSavesTheActualApplicationLayout();
    void applicationRestorePreservesExplicitWindowTitle();
    void longRunningStatusOffersStopAndClearsAfterwards();
    void blockedReloadReportsThePendingStop();
    void blockedStandardCallDoesNotDelayProcessExit();
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
void ScriptingTest::generatedConfigurationUsesRequestedLanguage_data() {
    QTest::addColumn<bool>("chinese");
    QTest::newRow("english") << false;
    QTest::newRow("chinese") << true;
}
void ScriptingTest::generatedConfigurationUsesRequestedLanguage() {
    QFETCH(bool, chinese);
    TranslationManager translations;
    translations.addCatalog(QStringLiteral(":/muxora/i18n/muxora"));
    QVERIFY(translations.installLanguage(chinese ? UiLanguage::SimplifiedChinese : UiLanguage::English));
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    WindowManager manager;
    manager.createWindow();
    muxora::UserPreferences preferences(directory.path());
    muxora::AppScriptingSetup setup(&manager, preferences);
    QVERIFY(setup.createConfigurationFile());
    const auto original = read(setup.configurationFilePath());
    QVERIFY(original.contains("id = 'user.hello'"));
    QVERIFY(original.contains("'ctrl+alt+l'"));
    QVERIFY(QString::fromUtf8(original).contains(chinese ? QStringLiteral("Lua 已就绪。")
        : QStringLiteral("Lua is ready.")));
    QSignalSpy initialized(&setup, &muxora::AppScriptingSetup::initialized);
    setup.start();
    QTRY_COMPARE(initialized.size(), 1);
    QVERIFY2(initialized[0][0].toBool(), qPrintable(initialized[0][1].toString()));
    const auto command = manager.commands().find(QStringLiteral("user.hello"));
    QVERIFY(command);
    QCOMPARE(command->title, chinese ? QStringLiteral("Lua: 问候") : QStringLiteral("Lua: Hello"));
    QCOMPARE(manager.commands().bindings(QStringLiteral("user.hello")).last().sequence,
        QKeySequence(QStringLiteral("Ctrl+Alt+L")));
    QVERIFY(translations.installLanguage(chinese ? UiLanguage::English : UiLanguage::SimplifiedChinese));
    QVERIFY(setup.createConfigurationFile());
    QCOMPARE(read(setup.configurationFilePath()), original);
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
void ScriptingTest::utf8ProcessManifestRetainsApplicationCompatibility() {
#ifdef Q_OS_WIN
    QCOMPARE(GetACP(), UINT(CP_UTF8));
    const auto module = GetModuleHandleW(nullptr);
    const auto resource = FindResourceW(module, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(24));
    QVERIFY(resource);
    const auto loaded = LoadResource(module, resource);
    QVERIFY(loaded);
    const QByteArray manifest(static_cast<const char *>(LockResource(loaded)),
                              int(SizeofResource(module, resource)));
    QVERIFY(manifest.contains("UTF-8</activeCodePage>"));
    QCOMPARE(manifest.count("<activeCodePage"), 1);
    QCOMPARE(manifest.count("<requestedExecutionLevel"), 1);
    QVERIFY(manifest.contains("level=\"asInvoker\""));
    QVERIFY(manifest.contains("longPathAware"));
    QVERIFY(manifest.contains("8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a"));
#else
    QVERIFY(true); // Process ANSI code pages are a Windows-only facility.
#endif
}
void ScriptingTest::standardLibraryHandlesChinesePathsAndEnvironment() {
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    const auto directory = temporary.filePath(QStringLiteral("中文目录 有空格"));
    QVERIFY(QDir().mkpath(QDir(directory).filePath("lua/中文模块")));
    QVERIFY(write(QDir(directory).filePath("lua/中文模块/init.lua"), "return {value='模块内容'}"));
    constexpr auto variable = "MUXORA_LUA_UTF8_TEST";
    const auto previous = qgetenv(variable);
    const bool wasSet = qEnvironmentVariableIsSet(variable);
    const auto restore = qScopeGuard([&] {
        if (wasSet)
            qputenv(variable, previous);
        else
            qunsetenv(variable);
    });
    QVERIFY(qputenv(variable, QStringLiteral("中文环境值").toUtf8()));
    WindowManager manager;
    manager.createWindow();
    muxora::UserPreferences preferences(directory);
    muxora::AppScriptingSetup setup(&manager, preferences);
    QSignalSpy initialized(&setup, &muxora::AppScriptingSetup::initialized);
    setup.start();
    QTRY_COMPARE(initialized.size(), 1);
    QVERIFY(initialized[0][0].toBool());
    const auto source = QStringLiteral(R"(
        local original = [=[%1/原始文件.txt]=]
        local renamed = [=[%1/重命名 文件.txt]=]
        local file = assert(io.open(original, 'wb'))
        assert(file:write('文件正文')); assert(file:close())
        assert(os.rename(original, renamed))
        file = assert(io.open(renamed, 'rb'))
        local contents = file:read('*a'); assert(file:close())
        local first = require('中文模块')
        return contents, os.getenv('MUXORA_LUA_UTF8_TEST'), first.value,
               first == require('中文模块')
    )").arg(QDir::fromNativeSeparators(directory));
    const auto reply = evaluate(setup.host(), source);
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QCOMPARE(reply.values,
             (QVariantList{QStringLiteral("文件正文"), QStringLiteral("中文环境值"),
                           QStringLiteral("模块内容"), true}));
    QCOMPARE(read(QDir(directory).filePath(QStringLiteral("重命名 文件.txt"))),
             QStringLiteral("文件正文").toUtf8());
}
void ScriptingTest::directScriptTransportAllowsFilesLargerThanOneMiB() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QByteArray source("--");
    source += QByteArray(1024 * 1024 + 32, ' ');
    source += "\nlarge_script_ran=true";
    QVERIFY(write(directory.filePath("large.lua"), source));
    WindowManager manager;
    const auto window = manager.createWindow();
    muxora::UserPreferences preferences(directory.path());
    muxora::AppScriptingSetup setup(&manager, preferences);
    QSignalSpy initialized(&setup, &muxora::AppScriptingSetup::initialized);
    setup.start();
    QTRY_COMPARE(initialized.size(), 1);
    QVERIFY(manager.execute("lua.run", {{"file", "large.lua"}}, window));
    const auto reply = evaluate(setup.host(), "return large_script_ran");
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QCOMPARE(reply.values, QVariantList{true});
}
void ScriptingTest::osExitUsesApplicationCommandAndHonorsClosePermission() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    WindowManager manager;
    const auto window = manager.createWindow();
    const auto tab = manager.addTab(window, new QPlainTextEdit, {"Editor", "notes", {}});
    QVERIFY(!tab.isNull());
    int commands = 0;
    int closeRequests = 0;
    manager.commands().add({"application.exit", "Exit", "File", {}, {}, [&](const CommandContext &) {
        ++commands;
        return manager.requestCloseAllWindows();
    }});
    manager.setCloseGuard([&](const QVector<TabId> &tabs) {
        ++closeRequests;
        return tabs.isEmpty();
    });
    muxora::UserPreferences preferences(directory.path());
    muxora::AppScriptingSetup setup(&manager, preferences);
    QSignalSpy initialized(&setup, &muxora::AppScriptingSetup::initialized);
    setup.start();
    QTRY_COMPARE(initialized.size(), 1);
    const auto reply = evaluate(setup.host(), "return os.exit(57,true), 'still running'");
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QCOMPARE(reply.values, (QVariantList{false, QStringLiteral("still running")}));
    QCOMPARE(commands, 1);
    QCOMPARE(closeRequests, 1);
    QVERIFY(manager.model().window(window));
    QVERIFY(manager.model().tab(tab));
}
void ScriptingTest::osExitSavesTheActualApplicationLayout() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(QDir().mkpath(directory.filePath("User")));
    QVERIFY(write(directory.filePath("User/init.lua"), "os.exit(73,true)"));
    const auto binary = QDir(QCoreApplication::applicationDirPath()).filePath("../../bin/muxora.exe");
    QVERIFY(QFileInfo::exists(binary));
    QProcess child;
    child.setProgram(binary);
    child.setArguments({"--fresh", "--state-dir", directory.path()});
    child.start();
    QVERIFY(child.waitForStarted());
    QVERIFY2(child.waitForFinished(10000), qPrintable(child.errorString()));
    QCOMPARE(child.exitStatus(), QProcess::NormalExit);
    QCOMPARE(child.exitCode(), 0);
    const auto layout = QJsonDocument::fromJson(read(directory.filePath("window-layout.json"))).object();
    QVERIFY(!layout.value("windows").toArray().isEmpty());
    QVERIFY(!layout.value("model").toObject().isEmpty());
}
void ScriptingTest::applicationRestorePreservesExplicitWindowTitle() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(QDir().mkpath(directory.filePath("User")));
    QVERIFY(write(directory.filePath("User/init.lua"), "os.exit(0,true)"));
    WindowManager manager;
    const auto window = manager.createWindow(WindowKind::Workbench, {"WindowSystem", QSize(900, 600)});
    QVERIFY(!manager.addTab(window, new QPlainTextEdit, {"Editor", "notes", {}}).isNull());
    const auto snapshot = manager.saveLayout();
    const auto savedWindows = snapshot.value("windows").toArray();
    QCOMPARE(savedWindows.size(), 1);
    QCOMPARE(savedWindows.first().toObject().value("title"), QJsonValue("WindowSystem"));
    QVERIFY(write(directory.filePath("window-layout.json"), QJsonDocument(snapshot).toJson()));
    const auto binary = QDir(QCoreApplication::applicationDirPath()).filePath("../../bin/muxora.exe");
    QVERIFY(QFileInfo::exists(binary));
    QProcess child;
    child.setProgram(binary);
    child.setArguments({"--state-dir", directory.path()});
    child.start();
    QVERIFY(child.waitForStarted());
    QVERIFY2(child.waitForFinished(10000), qPrintable(child.errorString()));
    QCOMPARE(child.exitStatus(), QProcess::NormalExit);
    QCOMPARE(child.exitCode(), 0);
    const auto restored = QJsonDocument::fromJson(read(directory.filePath("window-layout.json"))).object();
    const auto restoredWindows = restored.value("windows").toArray();
    QCOMPARE(restoredWindows.size(), 1);
    QCOMPARE(restoredWindows.first().toObject().value("id"), savedWindows.first().toObject().value("id"));
    QCOMPARE(restoredWindows.first().toObject().value("title"), QJsonValue("WindowSystem"));
}
void ScriptingTest::longRunningStatusOffersStopAndClearsAfterwards() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    WindowManager manager;
    const auto window = manager.createWindow();
    muxora::UserPreferences preferences(directory.path());
    muxora::AppScriptingSetup setup(&manager, preferences);
    QSignalSpy initialized(&setup, &muxora::AppScriptingSetup::initialized);
    setup.start();
    QTRY_COMPARE(initialized.size(), 1);
    QVERIFY(!manager.execute("lua.stop", {}, window));
    const auto job = setup.host()->run("while true do end");
    QVERIFY(job);
    const auto runningText = [&manager] {
        const auto items = manager.statusItems();
        for (const auto &item : items)
            if (item.id == QStringLiteral("lua.running"))
                return item.text;
        return QString();
    };
    QTRY_VERIFY_WITH_TIMEOUT(!runningText().isEmpty(), 4000);
    const auto items = manager.statusItems();
    const auto status = std::find_if(items.cbegin(), items.cend(), [](const StatusItem &item) {
        return item.id == QStringLiteral("lua.running");
    });
    QVERIFY(status != items.cend());
    QCOMPARE(status->command, QStringLiteral("lua.stop"));
    QVERIFY(!status->text.isEmpty());
    QVERIFY(manager.execute("lua.stop", {}, window));
    QTRY_VERIFY(runningText().isEmpty());
    QTRY_VERIFY(!setup.host()->runtime()->isRunning());
    const auto reply = evaluate(setup.host(), "return 42");
    QVERIFY2(reply.ok, qPrintable(reply.error));
    QCOMPARE(reply.values, QVariantList{42});
}
void ScriptingTest::blockedReloadReportsThePendingStop() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    WindowManager manager;
    manager.createWindow();
    muxora::UserPreferences preferences(directory.path());
    muxora::AppScriptingSetup setup(&manager, preferences);
    bool released = false;
    const auto release = qScopeGuard([&] {
        if (!released && !releaseBlockedProcess(directory)) {
            directory.setAutoRemove(false);
            qWarning() << "Preserving blocked process fixture after failed release:" << directory.path();
        }
    });
    QSignalSpy initialized(&setup, &muxora::AppScriptingSetup::initialized);
    QSignalSpy diagnostics(&setup, &muxora::AppScriptingSetup::diagnostic);
    setup.start();
    QTRY_COMPARE(initialized.size(), 1);
    const auto helper = QDir(QCoreApplication::applicationDirPath()).filePath("muxora-script-child.exe");
    const auto command = QStringLiteral("\"\"%1\" --wait-for-release \"%2\" \"%3\"\"")
                             .arg(QDir::toNativeSeparators(helper),
                                  QDir::toNativeSeparators(directory.filePath("blocked-ready")),
                                  QDir::toNativeSeparators(directory.filePath("release")));
    QVERIFY(setup.host()->run(QStringLiteral("os.execute([=[%1]=])").arg(command)));
    QTRY_VERIFY(QFile::exists(directory.filePath("blocked-ready")));
    QVERIFY(setup.reload());
    QTRY_VERIFY_WITH_TIMEOUT(std::any_of(diagnostics.cbegin(), diagnostics.cend(), [](const auto &entry) {
        return entry[0].toString().contains(QCoreApplication::translate("Muxora", "The script is still running. A stop has been requested."));
    }), 4000);
    QVERIFY(!setup.host()->ready());
    released = releaseBlockedProcess(directory);
    QVERIFY(released);
    QTRY_COMPARE(initialized.size(), 2);
    QVERIFY(initialized.last()[0].toBool());
}
void ScriptingTest::blockedStandardCallDoesNotDelayProcessExit() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    bool released = false;
    const auto release = qScopeGuard([&] {
        if (!released && !releaseBlockedProcess(directory)) {
            directory.setAutoRemove(false);
            qWarning() << "Preserving blocked process fixture after failed release:" << directory.path();
        }
    });
    QProcess child;
    child.setProgram(QDir(QCoreApplication::applicationDirPath()).filePath("muxora-scripting-exit-child.exe"));
    child.setArguments({directory.path()});
    child.start();
    QVERIFY(child.waitForStarted());
    QVERIFY2(child.waitForFinished(10000), qPrintable(child.errorString()));
    // Windows QElapsedTimer uses the machine-wide QPC reference. Capture the
    // instant after the process handle signals, before reading any reports.
    QElapsedTimer completed;
    completed.start();
    QCOMPARE(child.exitStatus(), QProcess::NormalExit);
    QVERIFY2(child.exitCode() == 0, child.readAllStandardError().constData());
    const auto result = QJsonDocument::fromJson(read(directory.filePath("shutdown.json"))).object();
    QVERIFY(!result.isEmpty());
    const auto elapsed = result.value("elapsed_ms").toInt(-1);
    QVERIFY2(elapsed >= 0 && elapsed < 2000, qPrintable(QString::number(elapsed)));
    bool timestampValid = false;
    const auto started = read(directory.filePath("shutdown-started")).toLongLong(&timestampValid);
    QVERIFY(timestampValid);
    QVERIFY(QElapsedTimer::isMonotonic());
    const auto processElapsed = completed.msecsSinceReference() - started;
    QVERIFY2(processElapsed >= 0 && processElapsed < 2000,
             qPrintable(QString::number(processElapsed)));
    QVERIFY(QFile::exists(directory.filePath("blocked-ready")));
    QVERIFY(!QFile::exists(directory.filePath("release")));
    released = releaseBlockedProcess(directory);
    QVERIFY(released);
}
QTEST_MAIN(ScriptingTest)
#include "scripting_test.moc"
