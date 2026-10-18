// SPDX-License-Identifier: MIT
#include "user_preferences.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace window_system;

namespace {
bool write(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray read(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
void registerCommands(CommandRegistry& commands)
{
    commands.add({"test.first", "First", "Tests", QKeySequence("Ctrl+Alt+A"), {}, [](const CommandContext&) { return true; }});
    commands.add({"test.second", "Second", "Tests", QKeySequence("Ctrl+Alt+B"), {}, [](const CommandContext&) { return true; }});
}
}

class UserPreferencesTest final : public QObject {
    Q_OBJECT
private slots:
    void languageIsIndependentAndPreservesOtherSettings();
    void malformedSettingsAreNotOverwritten();
    void portableAndExplicitStateKeepUserFilesIsolated();
    void bindingsAndCompositeArgumentsSurviveRestart();
    void invalidRulesKeepAllBindings_data();
    void invalidRulesKeepAllBindings();
    void unknownCommandsSurviveEditingKnownBindings();
    void externalChangesAreNotOverwritten();
    void writeFailureIsReported();
    void portableAndVsCodeChordSpellingsAreAccepted();
    void removalRulesMatchExplicitArguments_data();
    void removalRulesMatchExplicitArguments();
};

void UserPreferencesTest::languageIsIndependentAndPreservesOtherSettings()
{
    QTemporaryDir root; QVERIFY(root.isValid());
    const auto path = root.filePath("settings.json");
    QVERIFY(write(path, R"({"unrelated":{"fontSize":14},"workbench.language":"zh_CN"})"));
    muxora::UserPreferences preferences(root.path());
    QCOMPARE(preferences.language(), "zh_CN");
    QVERIFY(preferences.saveLanguage("en"));
    const auto object = QJsonDocument::fromJson(read(path)).object();
    QCOMPARE(object.value("unrelated").toObject().value("fontSize").toInt(), 14);
    muxora::UserPreferences restarted(root.path());
    QCOMPARE(restarted.language(), "en");
    const auto previous = read(path);
    QVERIFY(!preferences.saveLanguage("unknown"));
    QCOMPARE(read(path), previous);
    QVERIFY(!QFileInfo::exists(root.filePath("window-layout.json")));
}

void UserPreferencesTest::malformedSettingsAreNotOverwritten()
{
    QTemporaryDir root; QVERIFY(root.isValid());
    const auto path = root.filePath("settings.json");
    const QByteArray malformed = "{ incomplete user input";
    QVERIFY(write(path, malformed));
    muxora::UserPreferences preferences(root.path());
    QCOMPARE(preferences.language(), "system");
    QVERIFY(!preferences.diagnostic().isEmpty());
    QVERIFY(!preferences.saveLanguage("en"));
    QCOMPARE(read(path), malformed);
}

void UserPreferencesTest::portableAndExplicitStateKeepUserFilesIsolated()
{
    QTemporaryDir root; QVERIFY(root.isValid());
    QVERIFY(QDir().mkpath(root.filePath("data")));
    QCOMPARE(muxora::resolveUserDirectory({}, root.path()), root.filePath("data/User"));
    QCOMPARE(muxora::resolveUserDirectory(root.filePath("isolated"), root.path()), root.filePath("isolated/User"));
}

void UserPreferencesTest::bindingsAndCompositeArgumentsSurviveRestart()
{
    QTemporaryDir root; QVERIFY(root.isValid());
    muxora::UserPreferences preferences(root.path());
    CommandRegistry commands; registerCommands(commands);
    QVERIFY(preferences.loadKeybindings(commands));
    const QVector<CommandBinding> first{{QKeySequence("Ctrl+K, 1"), {{"value", 7}}}, {QKeySequence("Ctrl+Alt+F9"), {}}};
    QVERIFY(commands.setBindings("test.first", first));
    QVERIFY(commands.setBindings("test.second", {}));
    const QVariantMap composite{{"commands", QVariantList{QString("test.first"), QVariantMap{{"command", "test.second"}, {"args", QVariantMap{{"enabled", true}}}}}}};
    QVERIFY(commands.addBinding("runCommands", {QKeySequence("Ctrl+K, R"), composite}));
    const QVariantMap secondComposite{{"commands", QVariantList{QVariantMap{{"command", "test.first"}, {"args", QVariantMap{{"value", 42}}}}, QString("test.second")}}};
    QVERIFY(commands.addBinding("runCommands", {QKeySequence("Ctrl+K, S"), secondComposite}));
    const auto compositeBindings = commands.bindings("runCommands");
    QVERIFY(preferences.saveKeybindings(commands));
    QVERIFY(!read(root.filePath("keybindings.json")).contains("window-layout"));
    muxora::UserPreferences restarted(root.path());
    CommandRegistry restored; registerCommands(restored);
    QVERIFY(restarted.loadKeybindings(restored));
    QCOMPARE(restored.bindings("test.first"), first);
    QVERIFY(restored.bindings("test.second").isEmpty());
    QCOMPARE(restored.bindings("runCommands"), compositeBindings);
    QCOMPARE(restored.bindings("runCommands").first().args, composite);
    QCOMPARE(restored.bindings("runCommands").last().args, secondComposite);
}

void UserPreferencesTest::invalidRulesKeepAllBindings_data()
{
    QTest::addColumn<QByteArray>("input");
    QTest::newRow("syntax") << QByteArray("[{invalid]");
    QTest::newRow("root") << QByteArray("{}");
    QTest::newRow("item") << QByteArray("[1]");
    QTest::newRow("unknown-key") << QByteArray(R"([{"command":"test.first","key":"ctrl+k z","when":"editorTextFocus"}])");
    QTest::newRow("bad-key") << QByteArray(R"([{"command":"test.first","key":"NoSuchKey"}])");
    QTest::newRow("too-many-strokes") << QByteArray(R"([{"command":"test.first","key":"Ctrl+K, A, B, C, D"}])");
    QTest::newRow("bad-args") << QByteArray(R"([{"command":"test.first","key":"ctrl+k z","args":false}])");
    QTest::newRow("partly-valid") << QByteArray(R"([{"command":"-test.first","key":"ctrl+alt+a"},{"command":"test.second","key":"NoSuchKey"}])");
    QTest::newRow("oversized") << QByteArray(1024 * 1024 + 1, ' ');
}

void UserPreferencesTest::invalidRulesKeepAllBindings()
{
    QFETCH(QByteArray, input);
    QTemporaryDir root; QVERIFY(root.isValid());
    const auto path = root.filePath("keybindings.json");
    QVERIFY(write(path, input));
    CommandRegistry commands; registerCommands(commands);
    const auto first = commands.bindings("test.first");
    const auto second = commands.bindings("test.second");
    muxora::UserPreferences preferences(root.path());
    QVERIFY(!preferences.loadKeybindings(commands));
    QCOMPARE(commands.bindings("test.first"), first);
    QCOMPARE(commands.bindings("test.second"), second);
    QVERIFY(!preferences.saveKeybindings(commands));
    QCOMPARE(read(path), input);
}

void UserPreferencesTest::unknownCommandsSurviveEditingKnownBindings()
{
    QTemporaryDir root; QVERIFY(root.isValid());
    const auto path = root.filePath("keybindings.json");
    const QJsonObject unknown{{"key", "ctrl+k x"}, {"command", "extension.future"}, {"args", QJsonObject{{"value", 3}}}};
    QVERIFY(write(path, QJsonDocument(QJsonArray{unknown}).toJson()));
    CommandRegistry commands; registerCommands(commands);
    muxora::UserPreferences preferences(root.path());
    QVERIFY(preferences.loadKeybindings(commands));
    QVERIFY(commands.addBinding("test.second", {QKeySequence("Ctrl+K, Y"), {}}));
    QVERIFY(preferences.saveKeybindings(commands));
    const auto rules = QJsonDocument::fromJson(read(path)).array();
    QVERIFY(rules.contains(unknown));
}

void UserPreferencesTest::externalChangesAreNotOverwritten()
{
    QTemporaryDir root; QVERIFY(root.isValid());
    const auto path = root.filePath("keybindings.json");
    muxora::UserPreferences preferences(root.path());
    CommandRegistry commands; registerCommands(commands);
    QVERIFY(preferences.loadKeybindings(commands));
    QVERIFY(commands.addBinding("test.first", {QKeySequence("Ctrl+K, A"), {}}));
    QVERIFY(preferences.saveKeybindings(commands));
    const QByteArray external = R"([{"key":"ctrl+k b","command":"test.second"}])";
    QVERIFY(write(path, external));
    QVERIFY(commands.setBindings("test.second", {}));
    QVERIFY(!preferences.saveKeybindings(commands));
    QCOMPARE(read(path), external);
}

void UserPreferencesTest::writeFailureIsReported()
{
    QTemporaryDir root; QVERIFY(root.isValid());
    const auto blockedDirectory = root.filePath("file-not-directory");
    QVERIFY(write(blockedDirectory, "user file"));
    muxora::UserPreferences preferences(blockedDirectory);
    CommandRegistry commands; registerCommands(commands);
    QVERIFY(preferences.loadKeybindings(commands));
    QVERIFY(commands.setBindings("test.first", {}));
    QVERIFY(!preferences.saveKeybindings(commands));
    QVERIFY(!preferences.diagnostic().isEmpty());
    QCOMPARE(read(blockedDirectory), QByteArray("user file"));
}

void UserPreferencesTest::portableAndVsCodeChordSpellingsAreAccepted()
{
    QTemporaryDir root; QVERIFY(root.isValid());
    QVERIFY(write(root.filePath("keybindings.json"), R"([
      {"key":"ctrl+k z","command":"test.first"},
      {"key":"Ctrl+K, 1","command":"test.first"},
      {"key":"ctrl+,","command":"test.second"}
    ])"));
    CommandRegistry commands; registerCommands(commands);
    muxora::UserPreferences preferences(root.path());
    QVERIFY(preferences.loadKeybindings(commands));
    const auto bindings = commands.bindings("test.first");
    QCOMPARE(bindings.size(), 3);
    QCOMPARE(bindings[1].sequence, QKeySequence("Ctrl+K, Z"));
    QCOMPARE(bindings[2].sequence, QKeySequence("Ctrl+K, 1"));
    QCOMPARE(commands.bindings("test.second").last().sequence, QKeySequence("Ctrl+,"));
}

void UserPreferencesTest::removalRulesMatchExplicitArguments_data()
{
    QTest::addColumn<QByteArray>("rule");
    QTest::addColumn<bool>("removed");
    QTest::newRow("without-args-removes-by-key")
        << QByteArray(R"({"key":"ctrl+alt+a","command":"-test.first"})") << true;
    QTest::newRow("matching-args-remove")
        << QByteArray(R"({"key":"ctrl+alt+a","command":"-test.first","args":{"mode":"left","options":{"count":2}}})") << true;
    QTest::newRow("different-args-preserve")
        << QByteArray(R"({"key":"ctrl+alt+a","command":"-test.first","args":{"mode":"right","options":{"count":2}}})") << false;
    QTest::newRow("explicit-empty-args-do-not-match-nonempty")
        << QByteArray(R"({"key":"ctrl+alt+a","command":"-test.first","args":{}})") << false;
    QTest::newRow("nested-difference-preserves")
        << QByteArray(R"({"key":"ctrl+alt+a","command":"-test.first","args":{"mode":"left","options":{"count":3}}})") << false;
}

void UserPreferencesTest::removalRulesMatchExplicitArguments()
{
    QFETCH(QByteArray, rule);
    QFETCH(bool, removed);
    QTemporaryDir root; QVERIFY(root.isValid());
    const auto path = root.filePath("keybindings.json");
    QVERIFY(write(path, QByteArray("[") + rule + QByteArray("]")));
    CommandRegistry commands; registerCommands(commands);
    const QVector<CommandBinding> original{
        {QKeySequence("Ctrl+Alt+A"), {{"mode", "left"}, {"options", QVariantMap{{"count", 2}}}}},
        {QKeySequence("Ctrl+Alt+F9"), {{"mode", "other"}}}
    };
    QVERIFY(commands.setBindings("test.first", original));
    muxora::UserPreferences preferences(root.path());
    QVERIFY(preferences.loadKeybindings(commands));
    const auto expected = removed ? QVector<CommandBinding>{original.last()} : original;
    QCOMPARE(commands.bindings("test.first"), expected);
    // Applying/removing one key must preserve the command's second key. A UI
    // save canonicalizes the rules; the effective result must survive reload.
    QVERIFY(preferences.saveKeybindings(commands));
    CommandRegistry restored; registerCommands(restored);
    QVERIFY(restored.setBindings("test.first", original));
    muxora::UserPreferences restarted(root.path());
    QVERIFY(restarted.loadKeybindings(restored));
    QCOMPARE(restored.bindings("test.first"), expected);
}

QTEST_GUILESS_MAIN(UserPreferencesTest)
#include "user_preferences_test.moc"
