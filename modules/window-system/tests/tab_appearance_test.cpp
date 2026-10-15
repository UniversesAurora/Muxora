// SPDX-License-Identifier: MIT
#include <window_system/window_manager.hpp>
#include "settings/settings_page.hpp"
#include "style/tab_appearance_p.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <memory>

using namespace window_system;

class TabAppearanceTest final : public QObject {
    Q_OBJECT
private slots:
    void defaultsHaveSeparatePlacementAndConservativeFlags();
    void appearanceAndEveryPaletteStateRoundTrip();
    void invalidPublicAppearanceDoesNotEmitOrMutate_data();
    void invalidPublicAppearanceDoesNotEmitOrMutate();
    void invalidAppearanceIsAtomic_data();
    void invalidAppearanceIsAtomic();
    void flagCommandsUseSameAppearanceAndValidateArguments();
    void settingsExposeEachFlagAndOnlyEnableWheelExpansionInMultiline();
    void settingsPaletteEditorValidatesBeforeApplyingAndKeepsDraft();
    void paletteApplyAllowsSynchronousHostDestruction_data();
    void paletteApplyAllowsSynchronousHostDestruction();
};

void TabAppearanceTest::defaultsHaveSeparatePlacementAndConservativeFlags()
{
    WindowManager manager;
    const auto appearance = manager.tabAppearance();
    QCOMPARE(appearance.centralPlacement, TabBarPlacement::Top);
    QCOMPARE(appearance.pinnedPlacement, TabBarPlacement::Top);
    QVERIFY(!appearance.multiline); QVERIFY(!appearance.wheelExpandRows);
    QVERIFY(appearance.showIcons); QVERIFY(appearance.showNewTabButton); QVERIFY(!appearance.showNumbers);
    QCOMPARE(appearance.colors.size(), 9);
    for (const auto& preset : appearance.colors) {
        QVERIFY(manager.tabColorPreset(preset.id).has_value());
        QCOMPARE(*manager.tabColorPreset(preset.id), preset);
        QCOMPARE(preset.autoHideInactive.background, QColor("#6A568C"));
        QCOMPARE(preset.autoHideInactive.text, QColor(Qt::white));
    }
    QVERIFY(!manager.tabColorPreset("missing")); QVERIFY(!manager.tabColorPreset({}));
}

void TabAppearanceTest::appearanceAndEveryPaletteStateRoundTrip()
{
    WindowManager manager;
    auto appearance = manager.tabAppearance();
    appearance.centralPlacement = TabBarPlacement::Bottom;
    appearance.pinnedPlacement = TabBarPlacement::Top;
    appearance.multiline = true; appearance.wheelExpandRows = true;
    appearance.showIcons = false; appearance.showNewTabButton = false; appearance.showNumbers = true;
    appearance.colors = {{"custom", "自定义", {QColor("#104050"), QColor("#fafafa"), QColor("#123456"), QColor("#345678")},
        {QColor("#203040"), QColor("#abcdef"), QColor("#777777"), QColor("#333333")},
        {QColor("#667788"), QColor("#ffffff"), QColor("#8899aa"), QColor("#aabbcc")},
        {QColor("#445566"), QColor("#bbbbbb"), QColor("#223344"), QColor("#778899")}}};
    QSignalSpy changed(&manager, &WindowManager::layoutChanged);
    QVERIFY(manager.setTabAppearance(appearance)); QCOMPARE(manager.tabAppearance(), appearance);
    QCOMPARE(changed.size(), 1);
    QVERIFY(manager.setTabAppearance(appearance)); QCOMPARE(changed.size(), 1);
    const auto snapshot = manager.saveLayout();
    const auto json = snapshot.value("tabAppearance").toObject();
    const auto palette = json.value("colors").toArray(); QCOMPARE(palette.size(), 1);
    const auto definition = palette.first().toObject(); QCOMPARE(definition.size(), 6);
    QCOMPARE(definition.value("id").toString(), QStringLiteral("custom"));
    const QStringList states{"groupActive", "groupInactive", "autoHideActive", "autoHideInactive"};
    for (const auto& state : states) {
        const auto colors = definition.value(state).toObject(); QCOMPARE(colors.size(), 4);
        for (const auto* channel : {"background", "text", "line", "icon"})
            QVERIFY(QColor(colors.value(channel).toString()).isValid());
    }
    WindowManager restored;
    QVERIFY(restored.restoreLayout(snapshot)); QCOMPARE(restored.tabAppearance(), appearance);
    QCOMPARE(restored.saveLayout().value("tabAppearance"), snapshot.value("tabAppearance"));
    auto legacy = snapshot; legacy.remove("tabAppearance");
    QVERIFY(restored.restoreLayout(legacy)); QCOMPARE(restored.tabAppearance(), TabAppearance{});
}

void TabAppearanceTest::invalidPublicAppearanceDoesNotEmitOrMutate_data()
{
    QTest::addColumn<int>("fault");
    QTest::newRow("central-placement") << 0;
    QTest::newRow("pinned-placement") << 1;
    QTest::newRow("wheel-needs-multiline") << 2;
    QTest::newRow("duplicate-id") << 3;
    QTest::newRow("invalid-color") << 4;
    QTest::newRow("empty-id") << 5;
    QTest::newRow("empty-name") << 6;
    QTest::newRow("too-many-presets") << 7;
}

void TabAppearanceTest::invalidPublicAppearanceDoesNotEmitOrMutate()
{
    QFETCH(int, fault);
    WindowManager manager; const auto before = manager.saveLayout();
    auto candidate = manager.tabAppearance();
    switch (fault) {
    case 0: candidate.centralPlacement = static_cast<TabBarPlacement>(-1); break;
    case 1: candidate.pinnedPlacement = static_cast<TabBarPlacement>(2); break;
    case 2: candidate.wheelExpandRows = true; break;
    case 3: candidate.colors.append(candidate.colors.front()); break;
    case 4: candidate.colors[0].autoHideInactive.icon = QColor{}; break;
    case 5: candidate.colors[0].id.clear(); break;
    case 6: candidate.colors[0].name.clear(); break;
    case 7:
        while (candidate.colors.size() < 129) {
            auto preset = candidate.colors.front(); preset.id = QString("color-%1").arg(candidate.colors.size());
            candidate.colors.append(preset);
        }
        break;
    }
    QSignalSpy changed(&manager, &WindowManager::layoutChanged);
    QVERIFY(!manager.setTabAppearance(candidate)); QCOMPARE(manager.saveLayout(), before); QCOMPARE(changed.size(), 0);
}

void TabAppearanceTest::invalidAppearanceIsAtomic_data()
{
    QTest::addColumn<QString>("field"); QTest::addColumn<QJsonValue>("value");
    QTest::newRow("nonobject") << QString() << QJsonValue(3);
    QTest::newRow("wrongPlacement") << QString("centralPlacement") << QJsonValue("left");
    QTest::newRow("wrongPinnedPlacement") << QString("pinnedPlacement") << QJsonValue(false);
    QTest::newRow("wrongFlag") << QString("showIcons") << QJsonValue("false");
    QTest::newRow("wheelWithoutMultiline") << QString("wheelExpandRows") << QJsonValue(true);
    QTest::newRow("paletteObject") << QString("colors") << QJsonValue(QJsonObject{});
    QTest::newRow("incompletePreset") << QString("colors") << QJsonValue(QJsonArray{QJsonObject{{"id", "blue"}, {"name", "蓝色"}}});
    auto palette = tabAppearanceToJson(TabAppearance{}).value("colors").toArray();
    auto duplicate = palette; duplicate.append(palette.at(0));
    QTest::newRow("duplicatePaletteId") << QString("colors") << QJsonValue(duplicate);
    auto preset = palette.at(0).toObject(); auto active = preset.value("groupActive").toObject();
    active.insert("text", "not-a-color"); preset.insert("groupActive", active); palette[0] = preset;
    QTest::newRow("invalidStateColor") << QString("colors") << QJsonValue(palette);
    palette = tabAppearanceToJson(TabAppearance{}).value("colors").toArray();
    preset = palette.at(0).toObject(); preset.insert("unexpected", "value"); palette[0] = preset;
    QTest::newRow("unexpectedPresetField") << QString("colors") << QJsonValue(palette);
    palette = tabAppearanceToJson(TabAppearance{}).value("colors").toArray();
    preset = palette.at(0).toObject(); active = preset.value("groupActive").toObject();
    active.insert("text", "#ffff"); preset.insert("groupActive", active); palette[0] = preset;
    QTest::newRow("shortStateColor") << QString("colors") << QJsonValue(palette);
    palette = tabAppearanceToJson(TabAppearance{}).value("colors").toArray();
    preset = palette.at(0).toObject(); active = preset.value("groupActive").toObject();
    active.insert("extra", "#ffffff"); preset.insert("groupActive", active); palette[0] = preset;
    QTest::newRow("unexpectedStateChannel") << QString("colors") << QJsonValue(palette);
}

void TabAppearanceTest::invalidAppearanceIsAtomic()
{
    QFETCH(QString, field); QFETCH(QJsonValue, value);
    WindowManager manager;
    const auto before = manager.saveLayout(); auto invalid = before;
    if (field.isEmpty()) invalid.insert("tabAppearance", value);
    else { auto appearance = invalid.value("tabAppearance").toObject(); appearance.insert(field, value); invalid.insert("tabAppearance", appearance); }
    QSignalSpy changes(&manager, &WindowManager::layoutChanged);
    QVERIFY(!manager.restoreLayout(invalid)); QCOMPARE(manager.saveLayout(), before); QCOMPARE(changes.size(), 0);
}

void TabAppearanceTest::flagCommandsUseSameAppearanceAndValidateArguments()
{
    WindowManager manager;
    for (const auto& item : {std::pair{"presentation.toggleIcons", &TabAppearance::showIcons},
        std::pair{"presentation.toggleNewTabButton", &TabAppearance::showNewTabButton},
        std::pair{"presentation.toggleNumbers", &TabAppearance::showNumbers}}) {
        QVERIFY(manager.execute(item.first, {{"visible", false}})); QVERIFY(!(manager.tabAppearance().*item.second));
        QVERIFY(manager.execute(item.first, {{"visible", true}})); QVERIFY(manager.tabAppearance().*item.second);
        const auto before = manager.tabAppearance();
        QVERIFY(!manager.execute(item.first, {{"visible", "false"}})); QCOMPARE(manager.tabAppearance(), before);
    }
    QVERIFY(!manager.execute("presentation.toggleWheelExpandRows"));
    QVERIFY(manager.execute("presentation.toggleMultiline"));
    QVERIFY(manager.execute("presentation.toggleWheelExpandRows")); QVERIFY(manager.tabAppearance().wheelExpandRows);
    QVERIFY(manager.execute("presentation.toggleMultiline")); QVERIFY(!manager.tabAppearance().wheelExpandRows);
    QVERIFY(manager.execute("presentation.setCentralPlacement", {{"position", "bottom"}}));
    QCOMPARE(manager.tabAppearance().centralPlacement, TabBarPlacement::Bottom);
    QCOMPARE(manager.tabAppearance().pinnedPlacement, TabBarPlacement::Top);
    QVERIFY(!manager.execute("presentation.setPinnedPlacement", {{"position", "left"}}));
}

void TabAppearanceTest::settingsExposeEachFlagAndOnlyEnableWheelExpansionInMultiline()
{
    WindowManager manager; SettingsPage page(&manager);
    auto* central = page.findChild<QComboBox*>("CentralTabPlacement");
    auto* pinned = page.findChild<QComboBox*>("PinnedTabPlacement");
    auto* multiline = page.findChild<QCheckBox*>("MultilineTabs");
    auto* wheel = page.findChild<QCheckBox*>("WheelExpandTabRows");
    QVERIFY(central && pinned && multiline && wheel);
    QVERIFY(!wheel->isEnabled()); QVERIFY(!wheel->isChecked());
    central->setCurrentIndex(1); QCOMPARE(manager.tabAppearance().centralPlacement, TabBarPlacement::Bottom);
    QCOMPARE(manager.tabAppearance().pinnedPlacement, TabBarPlacement::Top);
    pinned->setCurrentIndex(1); QCOMPARE(manager.tabAppearance().pinnedPlacement, TabBarPlacement::Bottom);
    multiline->setChecked(true); QVERIFY(wheel->isEnabled());
    wheel->setChecked(true); QVERIFY(manager.tabAppearance().wheelExpandRows);
    multiline->setChecked(false); QVERIFY(!wheel->isEnabled()); QVERIFY(!wheel->isChecked());
    for (const auto& item : {std::pair{"ShowTabIcons", &TabAppearance::showIcons},
        std::pair{"ShowNewTabButton", &TabAppearance::showNewTabButton}, std::pair{"ShowTabNumbers", &TabAppearance::showNumbers}}) {
        auto* control = page.findChild<QCheckBox*>(QString::fromLatin1(item.first)); QVERIFY(control);
        control->setChecked(false); QVERIFY(!(manager.tabAppearance().*item.second));
        auto next = manager.tabAppearance(); next.*item.second = true;
        QVERIFY(manager.setTabAppearance(next)); QVERIFY(control->isChecked());
    }
}

void TabAppearanceTest::settingsPaletteEditorValidatesBeforeApplyingAndKeepsDraft()
{
    WindowManager manager; SettingsPage page(&manager);
    auto* editor = page.findChild<QPlainTextEdit*>("TabColorPresetsJson");
    auto* apply = page.findChild<QPushButton*>("ApplyTabColorPresets");
    auto* diagnostic = page.findChild<QLabel*>("TabColorPresetsDiagnostic");
    QVERIFY(editor && apply && diagnostic);
    const auto before = manager.tabAppearance();
    editor->setPlainText("[{broken]"); apply->click();
    QCOMPARE(manager.tabAppearance(), before); QVERIFY(!diagnostic->text().isEmpty());
    auto external = before; external.showNumbers = true; QVERIFY(manager.setTabAppearance(external));
    QCOMPARE(editor->toPlainText(), QString("[{broken]"));
    auto palette = tabAppearanceToJson(external).value("colors").toArray();
    auto preset = palette.at(0).toObject(); preset.insert("id", "local"); preset.insert("name", "本地");
    editor->setPlainText(QString::fromUtf8(QJsonDocument(QJsonArray{preset}).toJson())); apply->click();
    QCOMPARE(manager.tabAppearance().colors.size(), 1);
    QVERIFY(manager.tabColorPreset("local")); QVERIFY(!manager.tabColorPreset("gray"));
    QVERIFY(manager.tabAppearance().showNumbers);
}

void TabAppearanceTest::paletteApplyAllowsSynchronousHostDestruction_data()
{
    QTest::addColumn<bool>("deleteManager");
    QTest::newRow("page") << false;
    QTest::newRow("manager") << true;
}

void TabAppearanceTest::paletteApplyAllowsSynchronousHostDestruction()
{
    QFETCH(bool, deleteManager);
    auto manager = std::make_unique<WindowManager>(); const QPointer<WindowManager> owner(manager.get());
    const auto window = manager->createWindow(); manager->showSettings(window);
    QPointer<SettingsPage> page;
    for (const auto& tab : manager->model().tabs()) for (const auto pane : manager->model().panes(tab.id)) {
        auto* content = manager->paneWidget(pane);
        if (content && content->property("windowSystemSettingsKind") == "window_system.settings")
            page = static_cast<SettingsPage*>(content);
    }
    QVERIFY(page);
    auto* editor = page->findChild<QPlainTextEdit*>("TabColorPresetsJson");
    QPointer<QPushButton> apply = page->findChild<QPushButton*>("ApplyTabColorPresets"); QVERIFY(editor && apply);
    auto palette = tabAppearanceToJson(manager->tabAppearance()).value("colors").toArray();
    auto preset = palette.at(0).toObject(); preset.insert("id", "safe-callback");
    editor->setPlainText(QString::fromUtf8(QJsonDocument(QJsonArray{preset}).toJson()));
    int callbacks = 0; QJsonObject saved;
    connect(manager.get(), &WindowManager::layoutChanged, this, [&] {
        ++callbacks; saved = manager->saveLayout();
        if (deleteManager) manager.reset();
        else delete page.data();
    });
    // Emitting the actual signal directly tests this callback's lifetime
    // contract without deleting QPushButton inside its own click() setter.
    QVERIFY(QMetaObject::invokeMethod(apply, "clicked", Qt::DirectConnection, Q_ARG(bool, false)));
    QCOMPARE(callbacks, 1);
    const auto savedPalette = saved.value("tabAppearance").toObject().value("colors").toArray();
    QCOMPARE(savedPalette.size(), 1); QCOMPARE(savedPalette.first().toObject().value("id").toString(), QStringLiteral("safe-callback"));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(!page); QVERIFY(!apply);
    QCOMPARE(bool(owner), !deleteManager);
}

QTEST_MAIN(TabAppearanceTest)
#include "tab_appearance_test.moc"
