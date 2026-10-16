// SPDX-License-Identifier: MIT
#include <window_system/localization.hpp>
#include <window_system/window_manager.hpp>
#include "style/tab_appearance_p.hpp"

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
    QCOMPARE(QCoreApplication::translate("WindowSystem", "设置"), QStringLiteral("Settings"));
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
    QCOMPARE(command->category, QStringLiteral("工具"));
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
    QCOMPARE(QCoreApplication::translate("WindowSystem", "设置"), QStringLiteral("设置"));
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
    QCOMPARE(QCoreApplication::translate("WindowSystem", "设置"), QStringLiteral("设置"));
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
    QVERIFY(translations.installLanguage(UiLanguage::English));
    auto preset = defaultTabColorPresets().first();
    QCOMPARE(preset.name, QStringLiteral("灰色"));
    QCOMPARE(tabColorDisplayName(preset), QStringLiteral("Gray"));
    preset.name = QStringLiteral("我的颜色");
    QCOMPARE(tabColorDisplayName(preset), preset.name);
    preset.id = QStringLiteral("custom"); preset.name = QStringLiteral("灰色");
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
    // The catalog itself is checked by the test runner: no screenshot review or
    // developer judgment is required to detect missing translations/placeholders.
    const QDir source(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath());
    for (const auto& name : {QStringLiteral("window_system_en.ts"), QStringLiteral("window_system_zh_CN.ts")}) {
        QFile file(source.filePath(QStringLiteral("../translations/") + name));
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.fileName()));
        QXmlStreamReader xml(&file);
        int messages = 0;
        QString original;
        while (!xml.atEnd()) {
            xml.readNext();
            if (!xml.isStartElement()) continue;
            if (xml.name() == QStringLiteral("source")) original = xml.readElementText();
            if (xml.name() != QStringLiteral("translation")) continue;
            QVERIFY(xml.attributes().value(QStringLiteral("type")) != QStringLiteral("unfinished"));
            const auto translated = xml.readElementText();
            QVERIFY2(!translated.isEmpty(), qPrintable(original));
            const auto placeholders = [](const QString& text) {
                QStringList result;
                auto found = QRegularExpression(QStringLiteral("%L?[1-9][0-9]?|%n")).globalMatch(text);
                while (found.hasNext()) result.append(found.next().captured());
                result.sort(); return result;
            };
            QCOMPARE(placeholders(translated), placeholders(original));
            ++messages;
        }
        QVERIFY2(!xml.hasError(), qPrintable(xml.errorString()));
        QVERIFY(messages > 0);
    }
}

QTEST_MAIN(LocalizationTest)
#include "localization_test.moc"
