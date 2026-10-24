// SPDX-License-Identifier: MIT
#include "workspace_host/placeholder_content.hpp"
#include <window_system/localization.hpp>
#include <window_system_lua/script_host.hpp>
#include "../user_preferences.hpp"
#include "../../../modules/window-system/tests/translation_checks.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLabel>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QTableWidget>
#include <QTest>
#include <QTreeWidget>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <memory>

using namespace window_system;

namespace {
QStringList multilineText(QWidget* root)
{
    QStringList result;
    if (const auto* editor = qobject_cast<QPlainTextEdit*>(root)) result.append(editor->toPlainText());
    if (const auto* label = qobject_cast<QLabel*>(root)) result.append(label->text());
    for (const auto* label : root->findChildren<QLabel*>()) result.append(label->text());
    result.removeIf([](const QString& text) { return !text.contains(QLatin1Char('\n')); });
    return result;
}
}

class AppLocalizationTest final : public QObject {
    Q_OBJECT
private slots:
    void englishHostContentAndInitialTitles();
    void chineseHostCatalogAndPreferenceDiagnostics();
    void hostCatalogsAreComplete();
    void multilineHostTextMatchesExactCatalogKeys_data();
    void multilineHostTextMatchesExactCatalogKeys();
    void sourceLanguageIsEnglish();
    void sourceAndCatalogCoverage();
    void dynamicMenuTextIsTranslated_data();
    void dynamicMenuTextIsTranslated();
    void pluralMessagesUseCorrectForms_data();
    void pluralMessagesUseCorrectForms();
    void scriptDiagnosticsPreserveUserText();
    void commandLineHelpUsesTheRequestedLanguage_data();
    void commandLineHelpUsesTheRequestedLanguage();
};

void AppLocalizationTest::englishHostContentAndInitialTitles()
{
    TranslationManager translations;
    translations.addCatalog(QStringLiteral(":/muxora/i18n/muxora"));
    QVERIFY(translations.installLanguage(UiLanguage::English));
    WindowManager manager;
    std::unique_ptr<QWidget> notes(muxora::createPlaceholder({QStringLiteral("User title"), "notes", {}}, &manager));
    auto* editor = qobject_cast<QPlainTextEdit*>(notes.get());
    QVERIFY(editor);
    QVERIFY(editor->toPlainText().contains(QStringLiteral("Workspace notes")));
    QVERIFY(!editor->toPlainText().contains(QStringLiteral("工作区")));
    const QString userContent = QStringLiteral("用户输入不会因界面语言变化而丢失");
    editor->setPlainText(userContent);
    translations.setRequestedLanguage(UiLanguage::SimplifiedChinese);
    QCOMPARE(editor->toPlainText(), userContent);
    QCOMPARE(translations.activeLanguage(), UiLanguage::English);

    std::unique_ptr<QWidget> files(muxora::createPlaceholder({QStringLiteral("User files"), "files", {}}, &manager));
    auto* table = qobject_cast<QTableWidget*>(files.get());
    QVERIFY(table);
    QCOMPARE(table->horizontalHeaderItem(0)->text(), QStringLiteral("Name"));
    QCOMPARE(table->item(0, 2)->text(), QStringLiteral("Directory"));
    std::unique_ptr<QWidget> sessions(muxora::createPlaceholder({QStringLiteral("User sessions"), "sessions", {}}, &manager));
    auto* tree = qobject_cast<QTreeWidget*>(sessions.get());
    QVERIFY(tree);
    QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("Sessions · Layout preview"));

    const auto window = manager.createWindow(WindowKind::Workbench, {QStringLiteral("Muxora"), QSize(800, 600)});
    muxora::populateInitialWorkspace(manager, window);
    QStringList titles;
    for (const auto& tab : manager.model().tabs()) titles.append(tab.title);
    QVERIFY(titles.contains(QStringLiteral("Welcome")));
    QVERIFY(titles.contains(QStringLiteral("Workspace 1")));
    QVERIFY(titles.contains(QStringLiteral("Files")));
    QVERIFY(titles.contains(QStringLiteral("Sessions")));
    QVERIFY(titles.contains(QStringLiteral("Transfers")));
}

void AppLocalizationTest::chineseHostCatalogAndPreferenceDiagnostics()
{
    TranslationManager translations;
    translations.addCatalog(QStringLiteral(":/muxora/i18n/muxora"));
    QVERIFY(translations.installLanguage(UiLanguage::SimplifiedChinese));
    QCOMPARE(QCoreApplication::translate("Muxora", "New workspace tab"), QStringLiteral("新建工作区标签"));
    QVERIFY(translations.installLanguage(UiLanguage::English));
    // Host configuration diagnostics retain their module context while living
    // in the host-owned catalog; no host dependency enters WindowSystem.
    QCOMPARE(QCoreApplication::translate("WindowSystem", "Unsupported display language."), QStringLiteral("Unsupported display language."));
}

void AppLocalizationTest::hostCatalogsAreComplete()
{
    for (const auto language : {UiLanguage::English, UiLanguage::SimplifiedChinese}) {
        TranslationManager translations;
        translations.addCatalog(QStringLiteral(":/muxora/i18n/muxora"));
        QVERIFY(translations.installLanguage(language));
        const auto catalog = translation_test::readCatalog(QDir(translation_test::repositoryPath()).filePath(
            QStringLiteral("src/app/translations/muxora_%1.ts").arg(uiLanguageId(language))));
        const auto error = translation_test::validateCatalog(catalog);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        for (const auto& message : catalog.messages) {
            const auto context = message.context.toUtf8(), source = message.source.toUtf8();
            for (const int count : (message.plural ? QList<int>{0, 1, 2} : QList<int>{-1})) {
                const int form = message.plural && language == UiLanguage::English && count != 1 ? 1 : 0;
                auto expected = message.forms[form];
                if (message.plural) expected.replace(QStringLiteral("%n"), QString::number(count));
                QCOMPARE(QCoreApplication::translate(context.constData(), source.constData(), nullptr, count), expected);
            }
        }
    }
}

void AppLocalizationTest::multilineHostTextMatchesExactCatalogKeys_data()
{
    QTest::addColumn<QString>("contentType");
    QTest::newRow("notes-with-terminal-newline") << QStringLiteral("notes");
    QTest::newRow("transfers") << QStringLiteral("transfers");
    QTest::newRow("welcome") << QStringLiteral("welcome");
}

void AppLocalizationTest::multilineHostTextMatchesExactCatalogKeys()
{
    QFETCH(QString, contentType);
    TranslationManager translations;
    translations.addCatalog(QStringLiteral(":/muxora/i18n/muxora"));
    QVERIFY(translations.installLanguage(UiLanguage::SimplifiedChinese));
    WindowManager manager;
    const TabOptions options{QStringLiteral("User title"), contentType, {}};
    std::unique_ptr<QWidget> chinese(muxora::createPlaceholder(options, &manager));
    const auto originals = multilineText(chinese.get());
    QVERIFY(!originals.isEmpty());
    if (contentType == QStringLiteral("notes")) QVERIFY(originals.first().endsWith(QLatin1Char('\n')));

    const auto catalog = translation_test::readCatalog(QDir(translation_test::repositoryPath()).filePath(
        QStringLiteral("src/app/translations/muxora_zh_CN.ts")));
    QVERIFY2(catalog.error.isEmpty(), qPrintable(catalog.error));
    QHash<QString, QString> sourceForChinese;
    for (const auto& message : catalog.messages) if (message.context == QStringLiteral("Muxora") && !message.plural)
        sourceForChinese.insert(message.forms.first(), message.source);
    QStringList expectedEnglish;
    for (const auto& original : originals) {
        // Keep the final newline and all indentation in the lookup key.
        QVERIFY2(sourceForChinese.contains(original), qPrintable(original));
        expectedEnglish.append(sourceForChinese.value(original));
    }
    QVERIFY(translations.installLanguage(UiLanguage::English));
    std::unique_ptr<QWidget> english(muxora::createPlaceholder(options, &manager));
    QCOMPARE(multilineText(english.get()), expectedEnglish);
}

void AppLocalizationTest::sourceLanguageIsEnglish()
{
    for (const auto& language : {QStringLiteral("en"), QStringLiteral("zh_CN")}) {
        const auto catalog = translation_test::readCatalog(QDir(translation_test::repositoryPath()).filePath(
            QStringLiteral("src/app/translations/muxora_%1.ts").arg(language)));
        QVERIFY2(catalog.error.isEmpty(), qPrintable(catalog.error));
        QCOMPARE(catalog.sourceLanguage, QStringLiteral("en"));
        for (const auto& message : catalog.messages)
            QVERIFY2(!message.source.contains(QRegularExpression(QStringLiteral("[\\x{4E00}-\\x{9FFF}]"))), qPrintable(message.source));
    }
}

void AppLocalizationTest::sourceAndCatalogCoverage()
{
    const auto error = translation_test::checkSourceCoverage(QStringLiteral("Muxora"));
    QVERIFY2(error.isEmpty(), qPrintable(error));
}

void AppLocalizationTest::dynamicMenuTextIsTranslated_data()
{
    QTest::addColumn<bool>("chinese");
    QTest::newRow("english") << false; QTest::newRow("chinese") << true;
}

void AppLocalizationTest::dynamicMenuTextIsTranslated()
{
    QFETCH(bool, chinese);
    TranslationManager translations;
    translations.addCatalog(QStringLiteral(":/muxora/i18n/muxora"));
    QVERIFY(translations.installLanguage(chinese ? UiLanguage::SimplifiedChinese : UiLanguage::English));
    for (const auto& source : {QStringLiteral("Files"), QStringLiteral("Sessions"), QStringLiteral("Transfers")}) {
        const auto title = QCoreApplication::translate("Muxora", source.toUtf8().constData());
        const auto command = QCoreApplication::translate("Muxora", "New %1 tab").arg(title);
        QVERIFY(!command.contains(QStringLiteral("%1")));
        QVERIFY(command.contains(title));
        if (!chinese) QVERIFY(!command.contains(QRegularExpression(QStringLiteral("[\\x{4E00}-\\x{9FFF}]"))));
    }
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile settings(QDir(directory.path()).filePath(QStringLiteral("settings.json")));
    QVERIFY(settings.open(QIODevice::WriteOnly));
    settings.write("{\"workbench.language\":false}"); settings.close();
    muxora::UserPreferences preferences(directory.path());
    QCOMPARE(preferences.diagnostic(), chinese ? QStringLiteral("workbench.language 只支持 system、zh_CN 或 en。")
        : QStringLiteral("workbench.language supports only system, zh_CN, or en."));
}

void AppLocalizationTest::pluralMessagesUseCorrectForms_data()
{
    QTest::addColumn<bool>("chinese"); QTest::addColumn<int>("count");
    for (const bool chinese : {false, true}) for (const int count : {0, 1, 2})
        QTest::newRow(qPrintable(QStringLiteral("%1-%2").arg(chinese ? "zh" : "en").arg(count))) << chinese << count;
}

void AppLocalizationTest::pluralMessagesUseCorrectForms()
{
    QFETCH(bool, chinese); QFETCH(int, count);
    TranslationManager translations;
    translations.addCatalog(QStringLiteral(":/muxora/i18n/muxora"));
    QVERIFY(translations.installLanguage(chinese ? UiLanguage::SimplifiedChinese : UiLanguage::English));
    QCOMPARE(QCoreApplication::translate("Muxora", "%n items  ·  File operations are not connected yet", nullptr, count),
        chinese ? QStringLiteral("%1 项  ·  文件功能尚未接入").arg(count)
        : QStringLiteral("%1 %2  ·  File operations are not connected yet").arg(count).arg(count == 1 ? "item" : "items"));
}

void AppLocalizationTest::scriptDiagnosticsPreserveUserText()
{
    TranslationManager translations;
    translations.addCatalog(QStringLiteral(":/muxora/i18n/muxora"));
    QVERIFY(translations.installLanguage(UiLanguage::SimplifiedChinese));
    WindowManager manager;
    manager.createWindow();
    window_system_lua::ScriptHost scripts(&manager);
    QSignalSpy initialized(&scripts, &window_system_lua::ScriptHost::initialized);
    scripts.initialize();
    QTRY_COMPARE(initialized.size(), 1);
    QVERIFY(initialized.first().first().toBool());
    QSignalSpy finished(scripts.runtime(), &lua_runtime::ScriptRuntime::finished);
    QVERIFY(scripts.run(QStringLiteral("return muxora.tabs.new({title=false})")));
    QTRY_COMPARE(finished.size(), 1);
    const auto invalid = qvariant_cast<lua_runtime::ScriptResult>(finished.takeFirst().at(1));
    QVERIFY(!invalid.ok);
    QVERIFY2(invalid.error.contains(QStringLiteral("需要字符串")), qPrintable(invalid.error));
    QSignalSpy logs(&scripts, &window_system_lua::ScriptHost::logMessage);
    // This text also happens to be a first-party diagnostic source. User code
    // must never be translated by matching the final error or log string.
    QVERIFY(scripts.run(QStringLiteral("print('The tab does not exist.'); error('The tab does not exist.', 0)")));
    QTRY_COMPARE(finished.size(), 1);
    const auto user = qvariant_cast<lua_runtime::ScriptResult>(finished.takeFirst().at(1));
    QVERIFY(!user.ok);
    QVERIFY(user.error.startsWith(QStringLiteral("The tab does not exist.")));
    QTRY_COMPARE(logs.size(), 1);
    QCOMPARE(logs.first().first().toString(), QStringLiteral("The tab does not exist."));
    QVERIFY(scripts.run(QStringLiteral("return muxora.commands.register(false, false)")));
    QTRY_COMPARE(finished.size(), 1);
    const auto adapter = qvariant_cast<lua_runtime::ScriptResult>(finished.takeFirst().at(1));
    QVERIFY(!adapter.ok);
    QVERIFY2(adapter.error.contains(QStringLiteral("register 需要描述对象和函数")), qPrintable(adapter.error));
}

void AppLocalizationTest::commandLineHelpUsesTheRequestedLanguage_data()
{
    QTest::addColumn<bool>("chinese");
    QTest::newRow("english") << false; QTest::newRow("chinese") << true;
}

void AppLocalizationTest::commandLineHelpUsesTheRequestedLanguage()
{
    QFETCH(bool, chinese);
    QTemporaryDir state;
    QVERIFY(state.isValid());
    QDir directory(state.path());
    QVERIFY(directory.mkpath(QStringLiteral("User")));
    QFile settings(directory.filePath(QStringLiteral("User/settings.json")));
    QVERIFY(settings.open(QIODevice::WriteOnly));
    settings.write(chinese ? "{\"workbench.language\":\"zh_CN\"}" : "{\"workbench.language\":\"en\"}");
    settings.close();
    QProcess command;
    command.setProcessChannelMode(QProcess::MergedChannels);
    command.start(QString::fromUtf8(MUXORA_APPLICATION_PATH),
        {QStringLiteral("--state-dir"), state.path(), QStringLiteral("--help")});
    QVERIFY2(command.waitForStarted(), qPrintable(command.errorString()));
    QVERIFY2(command.waitForFinished(), qPrintable(command.errorString()));
    QCOMPARE(command.exitStatus(), QProcess::NormalExit);
    QCOMPARE(command.exitCode(), 0);
    const auto output = QString::fromUtf8(command.readAll());
    QVERIFY2(output.contains(chinese ? QStringLiteral("Muxora 窗口系统预览")
        : QStringLiteral("Muxora window system preview")), qPrintable(output));
    QVERIFY(output.contains(chinese ? QStringLiteral("使用初始布局，不读取保存状态")
        : QStringLiteral("Use the initial layout without loading saved state")));
    QVERIFY(output.contains(chinese ? QStringLiteral("启动后执行已注册命令")
        : QStringLiteral("Run a registered command after startup")));
    for (const auto* option : {"--fresh", "--state-dir", "--command"}) QVERIFY(output.contains(QLatin1String(option)));
    QVERIFY(!QFileInfo::exists(directory.filePath(QStringLiteral("window-layout.json"))));
}

QTEST_MAIN(AppLocalizationTest)
#include "localization_test.moc"
