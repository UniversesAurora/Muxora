// SPDX-License-Identifier: MIT
#include <window_system/localization.hpp>
#include <window_system/window_manager.hpp>
#include "style/tab_appearance_p.hpp"
#include "localization/display_names.hpp"
#include "chrome/status_bar.hpp"
#include "translation_checks.hpp"

#include <QCoreApplication>
#include <QComboBox>
#include <QAction>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenuBar>
#include <QMenu>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>
#include <QXmlStreamReader>

using namespace window_system;

class LocalizationTest final : public QObject {
    Q_OBJECT
private slots:
    void stableLanguageIds();
    void embeddedCatalogsTranslateCommandsAndMenus();
    void requestedLanguageNeedsRestart();
    void missingHostCatalogDoesNotInstallPartialLanguage();
    void standardQtDialogsUseEmbeddedChineseCatalog();
    void customColorNamesRemainContent();
    void languageSettingsStaySynchronizedAndSeparateFromLayout();
    void catalogsHaveNoUnfinishedMessages();
    void sourceLanguageIsEnglish();
    void dynamicMenuTextIsTranslated_data();
    void dynamicMenuTextIsTranslated();
    void pluralMessagesUseCorrectForms_data();
    void pluralMessagesUseCorrectForms();
};

void LocalizationTest::stableLanguageIds()
{
    QCOMPARE(supportedUiLanguages().size(), 3);
    for (const auto language : supportedUiLanguages()) QCOMPARE(uiLanguageFromId(uiLanguageId(language)), language);
    QCOMPARE(uiLanguageFromId(QStringLiteral("zh-CN")), UiLanguage::SimplifiedChinese);
    QCOMPARE(uiLanguageFromId(QStringLiteral("en-US")), UiLanguage::English);
    QCOMPARE(uiLanguageFromId(QStringLiteral("unknown")), UiLanguage::System);
    QCOMPARE(uiLanguageName(UiLanguage::SimplifiedChinese), QStringLiteral("简体中文"));
    QCOMPARE(uiLanguageName(UiLanguage::English), QStringLiteral("English"));
}

void LocalizationTest::embeddedCatalogsTranslateCommandsAndMenus()
{
    TranslationManager translations;
    QVERIFY(translations.installLanguage(UiLanguage::English));
    QCOMPARE(translations.activeLanguage(), UiLanguage::English);
    QCOMPARE(QCoreApplication::translate("WindowSystem", "Settings"), QStringLiteral("Settings"));
    WindowManager manager;
    const auto id = manager.createWindow(WindowKind::Workbench, {QStringLiteral("Host title"), QSize(800, 600)});
    auto* widget = manager.windowWidget(id);
    QVERIFY(widget);
    const auto* command = manager.commands().find(QStringLiteral("window.settings"));
    QVERIFY(command);
    QCOMPARE(command->title, QStringLiteral("Settings"));
    const auto* menu = widget->findChild<QMenuBar*>();
    QVERIFY(menu);
    QVERIFY(!menu->actions().isEmpty());
    QCOMPARE(menu->actions().first()->text(), QStringLiteral("&File"));
    for (const auto& definition : manager.commands().commands()) {
        QVERIFY2(!definition.title.contains(QRegularExpression(QStringLiteral("[\\x{4E00}-\\x{9FFF}]"))),
            qPrintable(definition.id + QStringLiteral(": ") + definition.title));
    }
    // Source keys used for menus/categories are stable regardless of language.
    QCOMPARE(command->category, QStringLiteral("tools"));
}

void LocalizationTest::requestedLanguageNeedsRestart()
{
    TranslationManager translations;
    QVERIFY(translations.installLanguage(UiLanguage::SimplifiedChinese));
    QVERIFY(!translations.restartRequired());
    QSignalSpy changes(&translations, &TranslationManager::requestedLanguageChanged);
    translations.setRequestedLanguage(UiLanguage::English);
    QCOMPARE(changes.size(), 1);
    QCOMPARE(translations.requestedLanguage(), UiLanguage::English);
    QCOMPARE(translations.activeLanguage(), UiLanguage::SimplifiedChinese);
    QVERIFY(translations.restartRequired());
    QCOMPARE(QCoreApplication::translate("WindowSystem", "Settings"), QStringLiteral("设置"));
    translations.setRequestedLanguage(UiLanguage::SimplifiedChinese);
    QVERIFY(!translations.restartRequired());
    translations.setRequestedLanguage(UiLanguage(-1));
    QCOMPARE(translations.requestedLanguage(), UiLanguage::SimplifiedChinese);
    QVERIFY(!translations.installLanguage(UiLanguage(-1)));
}

void LocalizationTest::missingHostCatalogDoesNotInstallPartialLanguage()
{
    TranslationManager translations;
    translations.addCatalog(QStringLiteral(":/missing-host/i18n/catalog"));
    QVERIFY(!translations.installLanguage(UiLanguage::English));
    QCOMPARE(QCoreApplication::translate("WindowSystem", "Settings"), QStringLiteral("Settings"));
    QCOMPARE(translations.requestedLanguage(), UiLanguage::System);
    QVERIFY(!translations.restartRequired());
}

void LocalizationTest::standardQtDialogsUseEmbeddedChineseCatalog()
{
    TranslationManager translations;
    QVERIFY(translations.installLanguage(UiLanguage::SimplifiedChinese));
    QDialogButtonBox chinese(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QVERIFY(chinese.button(QDialogButtonBox::Ok)->text().contains(QStringLiteral("确定")));
    QVERIFY(chinese.button(QDialogButtonBox::Cancel)->text().contains(QStringLiteral("取消")));
    QVERIFY(translations.installLanguage(UiLanguage::English));
    QDialogButtonBox english(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QCOMPARE(english.button(QDialogButtonBox::Cancel)->text(), QStringLiteral("Cancel"));
}

void LocalizationTest::customColorNamesRemainContent()
{
    TranslationManager translations;
    QVERIFY(translations.installLanguage(UiLanguage::SimplifiedChinese));
    auto preset = defaultTabColorPresets().first();
    // The stored name is the English ID-like source; only its display is translated.
    QCOMPARE(preset.name, QStringLiteral("Gray"));
    QCOMPARE(tabColorDisplayName(preset), QStringLiteral("灰色"));
    preset.name = QStringLiteral("我的颜色");
    QCOMPARE(tabColorDisplayName(preset), preset.name);
    preset.id = QStringLiteral("custom"); preset.name = QStringLiteral("Gray");
    QCOMPARE(tabColorDisplayName(preset), preset.name);
}

void LocalizationTest::languageSettingsStaySynchronizedAndSeparateFromLayout()
{
    TranslationManager translations;
    QVERIFY(translations.installLanguage(UiLanguage::SimplifiedChinese));
    WindowManager manager, otherManager;
    const auto first = manager.createWindow();
    const auto second = otherManager.createWindow();
    manager.showSettings(first); otherManager.showSettings(second);
    auto* control = manager.windowWidget(first)->findChild<QComboBox*>(QStringLiteral("DisplayLanguage"));
    auto* other = otherManager.windowWidget(second)->findChild<QComboBox*>(QStringLiteral("DisplayLanguage"));
    QVERIFY(control && other);
    QCOMPARE(control->currentData().toString(), QStringLiteral("zh_CN"));
    const auto english = control->findData(QStringLiteral("en"));
    QVERIFY(english >= 0);
    control->setCurrentIndex(english);
    QCOMPARE(translations.requestedLanguage(), UiLanguage::English);
    QCOMPARE(translations.activeLanguage(), UiLanguage::SimplifiedChinese);
    QVERIFY(translations.restartRequired());
    QCOMPARE(other->currentData().toString(), QStringLiteral("en"));
    const auto layout = QJsonDocument(manager.saveLayout()).toJson();
    QVERIFY(!layout.contains("workbench.language"));
    QVERIFY(!manager.saveLayout().contains(QStringLiteral("language")));
    QVERIFY(!manager.saveLayout().value(QStringLiteral("behavior")).toObject().contains(QStringLiteral("language")));
}

void LocalizationTest::catalogsHaveNoUnfinishedMessages()
{
    for (const auto language : {UiLanguage::English, UiLanguage::SimplifiedChinese}) {
        TranslationManager translations;
        QVERIFY(translations.installLanguage(language));
        const auto path = QDir(translation_test::repositoryPath()).filePath(
            QStringLiteral("modules/window-system/translations/window_system_%1.ts").arg(uiLanguageId(language)));
        const auto catalog = translation_test::readCatalog(path);
        const auto error = translation_test::validateCatalog(catalog);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        // Check the embedded result too, not just the editable catalog.
        for (const auto& message : catalog.messages) {
            const auto context = message.context.toUtf8(), source = message.source.toUtf8(), comment = message.comment.toUtf8();
            for (const int count : (message.plural ? QList<int>{0, 1, 2} : QList<int>{-1})) {
                const int form = message.plural && language == UiLanguage::English && count != 1 ? 1 : 0;
                auto expected = message.forms[form];
                if (message.plural) expected.replace(QStringLiteral("%n"), QString::number(count));
                QCOMPARE(QCoreApplication::translate(context.constData(), source.constData(),
                    comment.isEmpty() ? nullptr : comment.constData(), count), expected);
            }
        }
    }
}

void LocalizationTest::sourceLanguageIsEnglish()
{
    TranslationManager translations;
    QCOMPARE(translations.activeLanguage(), UiLanguage::English);
    QCOMPARE(QCoreApplication::translate("WindowSystem", "Settings"), QStringLiteral("Settings"));
    for (const auto& language : {QStringLiteral("en"), QStringLiteral("zh_CN")}) {
        const auto catalog = translation_test::readCatalog(QDir(translation_test::repositoryPath()).filePath(
            QStringLiteral("modules/window-system/translations/window_system_%1.ts").arg(language)));
        QVERIFY2(catalog.error.isEmpty(), qPrintable(catalog.error));
        QCOMPARE(catalog.sourceLanguage, QStringLiteral("en"));
        for (const auto& message : catalog.messages)
            QVERIFY2(!message.source.contains(QRegularExpression(QStringLiteral("[\\x{4E00}-\\x{9FFF}]"))), qPrintable(message.source));
    }
}

void LocalizationTest::dynamicMenuTextIsTranslated_data()
{
    QTest::addColumn<bool>("chinese");
    QTest::newRow("english") << false;
    QTest::newRow("chinese") << true;
}

void LocalizationTest::dynamicMenuTextIsTranslated()
{
    QFETCH(bool, chinese);
    TranslationManager translations;
    QVERIFY(translations.installLanguage(chinese ? UiLanguage::SimplifiedChinese : UiLanguage::English));
    WindowManager manager;
    const auto window = manager.createWindow();
    QVERIFY(manager.commands().add({"host.dynamic", "User command", "tools", {}, {},
        [](const CommandContext&) { return true; }, {"tools", "newTab"}}));
    QVERIFY(manager.commands().add({"host.custom", "自定义 command", "Settings", {}, {},
        [](const CommandContext&) { return true; }, {"用户菜单", "Settings"}}));
    const auto find = [&](const QStringList& path) -> QMenu* {
        for (auto* menu : manager.windowWidget(window)->findChildren<QMenu*>())
            if (menu->property("commandMenuPath").toStringList() == path) return menu;
        return nullptr;
    };
    QTRY_VERIFY(find({QStringLiteral("tools"), QStringLiteral("newTab")}));
    auto* dynamic = find({QStringLiteral("tools"), QStringLiteral("newTab")});
    QVERIFY(dynamic);
    QCOMPARE(dynamic->title(), chinese ? QStringLiteral("新建标签") : QStringLiteral("New tab"));
    auto* custom = find({QStringLiteral("用户菜单"), QStringLiteral("Settings")});
    QVERIFY(custom);
    QCOMPARE(custom->title(), QStringLiteral("Settings"));
    QCOMPARE(commandCategoryDisplayName(QStringLiteral("tools")), chinese ? QStringLiteral("工具") : QStringLiteral("Tools"));
    QCOMPARE(commandCategoryDisplayName(QStringLiteral("Settings")), QStringLiteral("Settings"));
    QCOMPARE(manager.commands().find(QStringLiteral("host.dynamic"))->menuPath,
        QStringList({QStringLiteral("tools"), QStringLiteral("newTab")}));
    QCOMPARE(manager.commands().find(QStringLiteral("host.custom"))->title, QStringLiteral("自定义 command"));
}

void LocalizationTest::pluralMessagesUseCorrectForms_data()
{
    QTest::addColumn<bool>("chinese"); QTest::addColumn<int>("count");
    for (const bool chinese : {false, true}) for (const int count : {0, 1, 2})
        QTest::newRow(qPrintable(QStringLiteral("%1-%2").arg(chinese ? "zh" : "en").arg(count))) << chinese << count;
}

void LocalizationTest::pluralMessagesUseCorrectForms()
{
    QFETCH(bool, chinese); QFETCH(int, count);
    TranslationManager translations;
    QVERIFY(translations.installLanguage(chinese ? UiLanguage::SimplifiedChinese : UiLanguage::English));
    WindowManager manager;
    const auto window = manager.createWindow();
    WorkspaceStatusBar status(&manager, window);
    status.setWorkspace(QStringLiteral("User title"), count);
    auto* workspace = status.findChild<QLabel*>(QStringLiteral("StatusWorkspace"));
    QVERIFY(workspace);
    const auto tabs = chinese ? QStringLiteral("%1 个标签").arg(count)
        : QStringLiteral("%1 %2").arg(count).arg(count == 1 ? "tab" : "tabs");
    QCOMPARE(workspace->text(), QStringLiteral("User title  ·  ") + tabs);
    status.setWorkspace({}, count);
    QCOMPARE(workspace->text(), tabs);
    QCOMPARE(QCoreApplication::translate("WindowSystem", "Insert %n tabs", nullptr, count),
        chinese ? QStringLiteral("插入 %1 个标签").arg(count)
                : QStringLiteral("Insert %1 %2").arg(count).arg(count == 1 ? "tab" : "tabs"));
}

QTEST_MAIN(LocalizationTest)
#include "localization_test.moc"
