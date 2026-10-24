// SPDX-License-Identifier: MIT
#include "layout_file.hpp"

#include <window_system/window_manager.hpp>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QTemporaryDir>
#include <QTest>

using namespace window_system;

namespace {
void useLabels(WindowManager& manager)
{
    manager.setContentFactory([](const TabOptions& options, PaneId) { return new QLabel(options.title); });
}

// A saved workspace with two tabs and a non-default title width.
QJsonObject savedLayout()
{
    WindowManager manager; useLabels(manager);
    const auto window = manager.createWindow();
    manager.addTab(window, new QLabel("First"), {"First", "label", {}});
    manager.addTab(window, new QLabel("Second"), {"Second", "label", {}});
    auto appearance = manager.tabAppearance(); appearance.maximumTitleWidth = 321;
    if (!manager.setTabAppearance(appearance)) return {};
    return manager.saveLayout();
}

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

QStringList copies(const QTemporaryDir& directory)
{
    return QDir(directory.path()).entryList({"window-layout-invalid-*.json"}, QDir::Files);
}
}

class LayoutFileTest : public QObject {
    Q_OBJECT
private slots:
    void validFileRestoresWithoutCopy();
    void missingFileIsNotAnError();
    void invalidSettingResetsOnlyItsSection_data();
    void invalidSettingResetsOnlyItsSection();
    void invalidStructureKeepsTheFile();
    void unreadableFileKeepsTheFile();
};

void LayoutFileTest::validFileRestoresWithoutCopy()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const auto path = directory.filePath("window-layout.json");
    QVERIFY(write(path, QJsonDocument(savedLayout()).toJson()));
    WindowManager manager; useLabels(manager);
    const auto result = muxora::restoreLayoutFile(manager, path);
    QVERIFY(result.restored); QVERIFY(result.message.isEmpty());
    QCOMPARE(manager.model().tabs().size(), 2); QCOMPARE(manager.tabAppearance().maximumTitleWidth, 321);
    QVERIFY(copies(directory).isEmpty());
}

void LayoutFileTest::missingFileIsNotAnError()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    WindowManager manager; useLabels(manager);
    const auto result = muxora::restoreLayoutFile(manager, directory.filePath("window-layout.json"));
    QVERIFY(!result.restored); QVERIFY(result.message.isEmpty()); QVERIFY(copies(directory).isEmpty());
}

void LayoutFileTest::invalidSettingResetsOnlyItsSection_data()
{
    QTest::addColumn<QString>("section");
    QTest::addColumn<QString>("field");
    QTest::addColumn<QJsonValue>("value");
    QTest::newRow("tabAppearance") << "tabAppearance" << "shrinkStickyTitleWidth" << QJsonValue(-1);
    QTest::newRow("behavior") << "behavior" << "newTabPosition" << QJsonValue(99);
}

void LayoutFileTest::invalidSettingResetsOnlyItsSection()
{
    QFETCH(QString, section); QFETCH(QString, field); QFETCH(QJsonValue, value);
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const auto path = directory.filePath("window-layout.json");
    auto layout = savedLayout();
    auto object = layout.value(section).toObject(); object.insert(field, value); layout.insert(section, object);
    const auto original = QJsonDocument(layout).toJson();
    QVERIFY(write(path, original));
    WindowManager manager; useLabels(manager);
    const auto result = muxora::restoreLayoutFile(manager, path);
    // The windows and tabs come back; only the broken section is reset.
    QVERIFY(result.restored);
    QCOMPARE(manager.model().tabs().size(), 2);
    QCOMPARE(manager.tabAppearance().maximumTitleWidth, section == "tabAppearance" ? defaultTabTitleWidth : 321);
    QVERIFY2(!result.message.isEmpty(), "the user is told what was reset");
    const auto kept = copies(directory);
    QCOMPARE(kept.size(), 1); QCOMPARE(read(QDir(directory.path()).filePath(kept.first())), original);
    QVERIFY(result.message.contains(QDir::toNativeSeparators(QDir(directory.path()).filePath(kept.first()))));
}

void LayoutFileTest::invalidStructureKeepsTheFile()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const auto path = directory.filePath("window-layout.json");
    auto layout = savedLayout(); layout.insert("schemaVersion", 99);
    const auto original = QJsonDocument(layout).toJson();
    QVERIFY(write(path, original));
    WindowManager manager; useLabels(manager);
    const auto result = muxora::restoreLayoutFile(manager, path);
    QVERIFY(!result.restored); QVERIFY(manager.model().windows().isEmpty()); QVERIFY(!result.message.isEmpty());
    const auto kept = copies(directory);
    QCOMPARE(kept.size(), 1); QCOMPARE(read(QDir(directory.path()).filePath(kept.first())), original);
}

void LayoutFileTest::unreadableFileKeepsTheFile()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const auto path = directory.filePath("window-layout.json");
    QVERIFY(write(path, "{ not json"));
    WindowManager manager; useLabels(manager);
    const auto result = muxora::restoreLayoutFile(manager, path);
    QVERIFY(!result.restored); QVERIFY(!result.message.isEmpty());
    const auto kept = copies(directory);
    QCOMPARE(kept.size(), 1); QCOMPARE(read(QDir(directory.path()).filePath(kept.first())), QByteArray("{ not json"));
}

QTEST_MAIN(LayoutFileTest)
#include "layout_file_test.moc"
