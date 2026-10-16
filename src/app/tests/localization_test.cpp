// SPDX-License-Identifier: MIT
#include "workspace_host/placeholder_content.hpp"
#include <window_system/localization.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QTableWidget>
#include <QTest>
#include <QTreeWidget>
#include <QXmlStreamReader>
#include <memory>

using namespace window_system;

class AppLocalizationTest final : public QObject {
    Q_OBJECT
private slots:
    void englishHostContentAndInitialTitles();
    void chineseHostCatalogAndPreferenceDiagnostics();
    void hostCatalogsAreComplete();
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
    QCOMPARE(QCoreApplication::translate("Muxora", "新建工作区标签"), QStringLiteral("新建工作区标签"));
    QVERIFY(translations.installLanguage(UiLanguage::English));
    // Host configuration diagnostics retain their module context while living
    // in the host-owned catalog; no host dependency enters WindowSystem.
    QCOMPARE(QCoreApplication::translate("WindowSystem", "不支持此界面语言。"), QStringLiteral("Unsupported display language."));
}

void AppLocalizationTest::hostCatalogsAreComplete()
{
    const QDir source(QFileInfo(QString::fromUtf8(__FILE__)).absolutePath());
    for (const auto& name : {QStringLiteral("muxora_en.ts"), QStringLiteral("muxora_zh_CN.ts")}) {
        QFile file(source.filePath(QStringLiteral("../translations/") + name));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QXmlStreamReader xml(&file);
        QString original;
        int messages = 0;
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
                auto matches = QRegularExpression(QStringLiteral("%L?[1-9][0-9]?|%n")).globalMatch(text);
                while (matches.hasNext()) result.append(matches.next().captured());
                result.sort(); return result;
            };
            QCOMPARE(placeholders(translated), placeholders(original));
            ++messages;
        }
        QVERIFY(!xml.hasError());
        QVERIFY(messages > 0);
    }
}

QTEST_MAIN(AppLocalizationTest)
#include "localization_test.moc"
