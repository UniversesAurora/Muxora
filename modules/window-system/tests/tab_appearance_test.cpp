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
#include <QSpinBox>
#include <QTest>
#include <memory>
#include <cmath>
#include <limits>

using namespace window_system;

class TabAppearanceTest final : public QObject {
    Q_OBJECT
private slots:
    void defaultsHaveSeparatePlacementAndConservativeFlags();
    void defaultPaletteKeepsColorIdentityAndReadableForegrounds();
    void explicitPaletteValuesRoundTripWithBuiltinIds();
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
    void sidebarFlagsHaveIndependentDefaultsPersistenceCommandsAndControls();
    void titleClippingRetainsWidthAndDefaultsOmittedFields();
    void sidebarTitleClippingIsIndependentInLayoutSnapshots();
    void sidebarTitleClippingControlsStayIndependent();
};

void TabAppearanceTest::defaultsHaveSeparatePlacementAndConservativeFlags()
{
    WindowManager manager;
    const auto appearance = manager.tabAppearance();
    QCOMPARE(appearance.centralPlacement, TabBarPlacement::Top);
    QCOMPARE(appearance.pinnedPlacement, TabBarPlacement::Top);
    QVERIFY(!appearance.multiline); QVERIFY(!appearance.wheelExpandRows);
    QVERIFY(appearance.showIcons); QVERIFY(appearance.showNewTabButton); QVERIFY(!appearance.showNumbers);
    QVERIFY(!appearance.clipTitles); QCOMPARE(appearance.maximumTitleWidth, defaultTabTitleWidth);
    QVERIFY(!appearance.clipSidebarTitles); QCOMPARE(appearance.maximumSidebarTitleWidth, defaultTabTitleWidth);
    QCOMPARE(appearance.colors.size(), 9);
    for (const auto& preset : appearance.colors) {
        QVERIFY(manager.tabColorPreset(preset.id).has_value());
        QCOMPARE(*manager.tabColorPreset(preset.id), preset);
        QVERIFY(preset.autoHideInactive.background != preset.autoHideActive.background);
        QCOMPARE(preset.autoHideInactive, preset.groupInactive);
    }
    QVERIFY(!manager.tabColorPreset("missing")); QVERIFY(!manager.tabColorPreset({}));
}

void TabAppearanceTest::defaultPaletteKeepsColorIdentityAndReadableForegrounds()
{
    const auto luminance = [](const QColor& color) {
        const auto linear = [](double value) { return value <= .04045 ? value / 12.92 : std::pow((value + .055) / 1.055, 2.4); };
        return .2126 * linear(color.redF()) + .7152 * linear(color.greenF()) + .0722 * linear(color.blueF());
    };
    for (const auto& preset : defaultTabColorPresets()) {
        const auto& active = preset.groupActive;
        const auto& inactive = preset.groupInactive;
        QVERIFY2(inactive.background.saturationF() > active.background.saturationF(), qPrintable(preset.id));
        QVERIFY(luminance(active.background) > luminance(inactive.background));
        QCOMPARE(preset.autoHideActive, active); QCOMPARE(preset.autoHideInactive, inactive);
        for (const auto& colors : {active, inactive}) {
            QVERIFY(colors.background != colors.line); QVERIFY(colors.background != colors.icon);
            QVERIFY(colors.text != colors.icon); QVERIFY(colors.line != colors.icon);
            const double background = luminance(colors.background), text = luminance(colors.text);
            const auto contrast = (std::max(background, text) + .05) / (std::min(background, text) + .05);
            QVERIFY2(contrast >= 4.5, qPrintable(preset.id + QStringLiteral(" foreground contrast %1").arg(contrast)));
        }
    }
}

void TabAppearanceTest::explicitPaletteValuesRoundTripWithBuiltinIds()
{
    auto saved = TabAppearance{};
    for (auto& preset : saved.colors) {
        const auto accent = preset.groupActive.line;
        preset.groupActive.icon = accent;
        preset.groupInactive = {QColor("#f7f7f8"), QColor("#3c4043"), accent, QColor("#5f6368")};
        preset.autoHideActive = {accent, QColor(Qt::white), accent, QColor(Qt::white)};
        preset.autoHideInactive = {QColor("#6A568C"), QColor(Qt::white), accent, QColor(Qt::white)};
    }
    auto customized = saved.colors[1]; customized.groupInactive.text = QColor("#123456");
    saved.colors[1] = customized;
    TabAppearance restored;
    QVERIFY(parseTabAppearance(tabAppearanceToJson(saved), restored));
    QCOMPARE(restored, saved);
    TabAppearance again;
    QVERIFY(parseTabAppearance(tabAppearanceToJson(restored), again)); QCOMPARE(again, restored);
    WindowManager manager;
    QVERIFY(manager.setTabAppearance(saved));
    const auto snapshot = manager.saveLayout();
    WindowManager restoredManager;
    QVERIFY(restoredManager.restoreLayout(snapshot));
    QCOMPARE(restoredManager.tabAppearance(), saved);
    QCOMPARE(restoredManager.saveLayout().value("tabAppearance"), snapshot.value("tabAppearance"));
}

void TabAppearanceTest::appearanceAndEveryPaletteStateRoundTrip()
{
    WindowManager manager;
    auto appearance = manager.tabAppearance();
    appearance.centralPlacement = TabBarPlacement::Bottom;
    appearance.pinnedPlacement = TabBarPlacement::Top;
    appearance.multiline = true; appearance.wheelExpandRows = true;
    appearance.showIcons = false; appearance.showNewTabButton = false; appearance.showNumbers = true;
    appearance.clipTitles = true; appearance.maximumTitleWidth = 173;
    appearance.clipSidebarTitles = false; appearance.maximumSidebarTitleWidth = 251;
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
    auto omittedAppearance = snapshot; omittedAppearance.remove("tabAppearance");
    QVERIFY(restored.restoreLayout(omittedAppearance)); QCOMPARE(restored.tabAppearance(), TabAppearance{});
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
    QTest::newRow("title-width-too-small") << 8;
    QTest::newRow("title-width-too-large") << 9;
    QTest::newRow("sidebar-title-width-too-small") << 10;
    QTest::newRow("sidebar-title-width-too-large") << 11;
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
    case 8: candidate.maximumTitleWidth = minimumTabTitleWidth - 1; break;
    case 9: candidate.maximumTitleWidth = maximumTabTitleWidth + 1; break;
    case 10: candidate.maximumSidebarTitleWidth = minimumTabTitleWidth - 1; break;
    case 11: candidate.maximumSidebarTitleWidth = maximumTabTitleWidth + 1; break;
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
    QTest::newRow("wrongClippingFlag") << QString("clipTitles") << QJsonValue("true");
    QTest::newRow("titleWidthString") << QString("maximumTitleWidth") << QJsonValue("160");
    QTest::newRow("titleWidthBoolean") << QString("maximumTitleWidth") << QJsonValue(true);
    QTest::newRow("titleWidthNull") << QString("maximumTitleWidth") << QJsonValue(QJsonValue::Null);
    QTest::newRow("titleWidthFraction") << QString("maximumTitleWidth") << QJsonValue(160.5);
    QTest::newRow("titleWidthSmall") << QString("maximumTitleWidth") << QJsonValue(minimumTabTitleWidth - 1);
    QTest::newRow("titleWidthLarge") << QString("maximumTitleWidth") << QJsonValue(maximumTabTitleWidth + 1);
    QTest::newRow("titleWidthInfinity") << QString("maximumTitleWidth") << QJsonValue(std::numeric_limits<double>::infinity());
    QTest::newRow("titleWidthNaN") << QString("maximumTitleWidth") << QJsonValue(std::numeric_limits<double>::quiet_NaN());
    QTest::newRow("wrongSidebarClippingFlag") << QString("clipSidebarTitles") << QJsonValue("true");
    for (const auto& item : {std::pair{"String", QJsonValue("160")}, std::pair{"Boolean", QJsonValue(true)},
        std::pair{"Null", QJsonValue(QJsonValue::Null)}, std::pair{"Fraction", QJsonValue(160.5)},
        std::pair{"Small", QJsonValue(minimumTabTitleWidth - 1)}, std::pair{"Large", QJsonValue(maximumTabTitleWidth + 1)},
        std::pair{"Infinity", QJsonValue(std::numeric_limits<double>::infinity())},
        std::pair{"NaN", QJsonValue(std::numeric_limits<double>::quiet_NaN())}})
        QTest::newRow(qPrintable(QStringLiteral("sidebarTitleWidth") + QLatin1String(item.first)))
            << QString("maximumSidebarTitleWidth") << item.second;
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
        std::pair{"presentation.toggleNumbers", &TabAppearance::showNumbers},
        std::pair{"presentation.toggleTitleClipping", &TabAppearance::clipTitles},
        std::pair{"presentation.toggleSidebarTitleClipping", &TabAppearance::clipSidebarTitles}}) {
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

void TabAppearanceTest::sidebarFlagsHaveIndependentDefaultsPersistenceCommandsAndControls()
{
    WindowManager manager;
    QVERIFY(manager.tabAppearance().showIcons);
    QVERIFY(!manager.tabAppearance().showSidebarIcons);
    QVERIFY(!manager.tabAppearance().showSidebarNumbers);
    SettingsPage page(&manager);
    auto* icons = page.findChild<QCheckBox*>("ShowSidebarTabIcons");
    auto* numbers = page.findChild<QCheckBox*>("ShowSidebarTabNumbers");
    QVERIFY(icons && numbers); QVERIFY(!icons->isChecked()); QVERIFY(!numbers->isChecked());
    QVERIFY(manager.execute("presentation.toggleIcons", {{"visible", false}}));
    QVERIFY(manager.execute("presentation.toggleNumbers", {{"visible", true}}));
    QVERIFY(!manager.tabAppearance().showSidebarIcons); QVERIFY(!manager.tabAppearance().showSidebarNumbers);
    icons->setChecked(true); numbers->setChecked(true);
    QVERIFY(manager.tabAppearance().showSidebarIcons); QVERIFY(manager.tabAppearance().showSidebarNumbers);
    QVERIFY(!manager.tabAppearance().showIcons); QVERIFY(manager.tabAppearance().showNumbers);
    const auto snapshot = manager.saveLayout();
    const auto saved = snapshot.value("tabAppearance").toObject();
    QCOMPARE(saved.value("showSidebarIcons"), QJsonValue(true));
    QCOMPARE(saved.value("showSidebarNumbers"), QJsonValue(true));
    WindowManager restored; QVERIFY(restored.restoreLayout(snapshot));
    QCOMPARE(restored.tabAppearance(), manager.tabAppearance());
    auto omittedSidebarFlags = snapshot; auto flags = saved; flags.remove("showSidebarIcons"); flags.remove("showSidebarNumbers");
    omittedSidebarFlags.insert("tabAppearance", flags); QVERIFY(restored.restoreLayout(omittedSidebarFlags));
    QVERIFY(!restored.tabAppearance().showSidebarIcons); QVERIFY(!restored.tabAppearance().showSidebarNumbers);
    QVERIFY(!restored.tabAppearance().showIcons); QVERIFY(restored.tabAppearance().showNumbers);
    for (const auto& item : {std::pair{"presentation.toggleSidebarIcons", &TabAppearance::showSidebarIcons},
        std::pair{"presentation.toggleSidebarNumbers", &TabAppearance::showSidebarNumbers}}) {
        const auto before = manager.tabAppearance();
        QVERIFY(!manager.execute(item.first, {{"visible", "false"}})); QCOMPARE(manager.tabAppearance(), before);
        QVERIFY(manager.execute(item.first, {{"visible", false}})); QVERIFY(!(manager.tabAppearance().*item.second));
        QVERIFY(manager.execute(item.first)); QVERIFY(manager.tabAppearance().*item.second);
    }
    for (const auto* field : {"showSidebarIcons", "showSidebarNumbers"}) {
        auto invalid = snapshot; auto flags = saved; flags.insert(field, "true"); invalid.insert("tabAppearance", flags);
        const auto before = restored.saveLayout(); QVERIFY(!restored.restoreLayout(invalid)); QCOMPARE(restored.saveLayout(), before);
    }
}

void TabAppearanceTest::titleClippingRetainsWidthAndDefaultsOmittedFields()
{
    WindowManager manager; SettingsPage page(&manager);
    auto* clipped = page.findChild<QCheckBox*>("ClipTabTitles");
    auto* width = page.findChild<QSpinBox*>("MaximumTabTitleWidth");
    QVERIFY(clipped && width);
    QVERIFY(!clipped->isChecked()); QVERIFY(!width->isEnabled());
    QCOMPARE(width->minimum(), minimumTabTitleWidth); QCOMPARE(width->maximum(), maximumTabTitleWidth);
    QCOMPARE(width->value(), defaultTabTitleWidth);
    clipped->setChecked(true); QVERIFY(width->isEnabled());
    width->setValue(137); QCOMPARE(manager.tabAppearance().maximumTitleWidth, 137);
    QVERIFY(manager.execute("presentation.toggleTitleClipping", {{"visible", false}}));
    QVERIFY(!clipped->isChecked()); QVERIFY(!width->isEnabled()); QCOMPARE(width->value(), 137);
    const auto saved = manager.saveLayout();
    WindowManager restored; QVERIFY(restored.restoreLayout(saved));
    QVERIFY(!restored.tabAppearance().clipTitles); QCOMPARE(restored.tabAppearance().maximumTitleWidth, 137);
    QVERIFY(manager.execute("presentation.toggleTitleClipping"));
    QVERIFY(clipped->isChecked()); QVERIFY(width->isEnabled()); QCOMPARE(width->value(), 137);
    for (const int boundary : {minimumTabTitleWidth, maximumTabTitleWidth}) {
        auto next = manager.tabAppearance(); next.maximumTitleWidth = boundary;
        QVERIFY(manager.setTabAppearance(next)); QCOMPARE(width->value(), boundary);
    }
    auto omittedClippingSettings = saved; auto appearance = omittedClippingSettings.value("tabAppearance").toObject();
    appearance.remove("clipTitles"); appearance.remove("maximumTitleWidth"); omittedClippingSettings.insert("tabAppearance", appearance);
    QVERIFY(manager.restoreLayout(omittedClippingSettings));
    QVERIFY(!manager.tabAppearance().clipTitles); QCOMPARE(manager.tabAppearance().maximumTitleWidth, defaultTabTitleWidth);
    QVERIFY(!clipped->isChecked()); QVERIFY(!width->isEnabled()); QCOMPARE(width->value(), defaultTabTitleWidth);
}

void TabAppearanceTest::sidebarTitleClippingIsIndependentInLayoutSnapshots()
{
    WindowManager manager;
    auto appearance = manager.tabAppearance();
    appearance.clipTitles = true; appearance.maximumTitleWidth = 91;
    appearance.clipSidebarTitles = false; appearance.maximumSidebarTitleWidth = 243;
    QVERIFY(manager.setTabAppearance(appearance));
    const auto snapshot = manager.saveLayout();
    WindowManager restored; QVERIFY(restored.restoreLayout(snapshot));
    QCOMPARE(restored.tabAppearance(), appearance);
    const auto json = snapshot.value("tabAppearance").toObject();
    QCOMPARE(json.value("clipSidebarTitles"), QJsonValue(false));
    QCOMPARE(json.value("maximumSidebarTitleWidth"), QJsonValue(243));

    appearance.clipTitles = false; appearance.maximumTitleWidth = 111;
    QVERIFY(manager.setTabAppearance(appearance));
    QVERIFY(!manager.tabAppearance().clipSidebarTitles);
    QCOMPARE(manager.tabAppearance().maximumSidebarTitleWidth, 243);
    appearance.clipSidebarTitles = true; appearance.maximumSidebarTitleWidth = 345;
    QVERIFY(manager.setTabAppearance(appearance));
    QVERIFY(!manager.tabAppearance().clipTitles);
    QCOMPARE(manager.tabAppearance().maximumTitleWidth, 111);
    for (const int boundary : {minimumTabTitleWidth, maximumTabTitleWidth}) {
        appearance.maximumSidebarTitleWidth = boundary;
        QVERIFY(manager.setTabAppearance(appearance));
        QCOMPARE(manager.tabAppearance().maximumSidebarTitleWidth, boundary);
        QCOMPARE(manager.tabAppearance().maximumTitleWidth, 111);
    }

    // Missing optional fields use their own defaults, never another policy.
    TabAppearance parsed;
    auto missingFlag = json; missingFlag.remove("clipSidebarTitles");
    QVERIFY(parseTabAppearance(missingFlag, parsed));
    QVERIFY(!parsed.clipSidebarTitles); QCOMPARE(parsed.maximumSidebarTitleWidth, 243);
    auto missingWidth = json; missingWidth.remove("maximumSidebarTitleWidth");
    QVERIFY(parseTabAppearance(missingWidth, parsed));
    QVERIFY(!parsed.clipSidebarTitles); QCOMPARE(parsed.maximumSidebarTitleWidth, defaultTabTitleWidth);
    QVERIFY(parseTabAppearance(QJsonObject{}, parsed));
    QCOMPARE(parsed, TabAppearance{});
}

void TabAppearanceTest::sidebarTitleClippingControlsStayIndependent()
{
    WindowManager manager; SettingsPage page(&manager);
    auto* regular = page.findChild<QCheckBox*>("ClipTabTitles");
    auto* regularWidth = page.findChild<QSpinBox*>("MaximumTabTitleWidth");
    auto* sidebar = page.findChild<QCheckBox*>("ClipSidebarTabTitles");
    auto* sidebarWidth = page.findChild<QSpinBox*>("MaximumSidebarTabTitleWidth");
    QVERIFY(regular && regularWidth && sidebar && sidebarWidth);
    QVERIFY(!regular->isChecked() && !sidebar->isChecked());
    QVERIFY(!regularWidth->isEnabled() && !sidebarWidth->isEnabled());
    QCOMPARE(sidebarWidth->minimum(), minimumTabTitleWidth);
    QCOMPARE(sidebarWidth->maximum(), maximumTabTitleWidth);
    regular->setChecked(true); regularWidth->setValue(73);
    QVERIFY(!sidebar->isChecked() && !sidebarWidth->isEnabled());
    sidebar->setChecked(true); sidebarWidth->setValue(137);
    QVERIFY(regular->isChecked() && regularWidth->isEnabled()); QCOMPARE(regularWidth->value(), 73);
    QVERIFY(manager.execute("presentation.toggleSidebarTitleClipping", {{"visible", false}}));
    QVERIFY(!sidebar->isChecked() && !sidebarWidth->isEnabled()); QCOMPARE(sidebarWidth->value(), 137);
    QVERIFY(regular->isChecked()); QCOMPARE(manager.tabAppearance().maximumTitleWidth, 73);
    auto next = manager.tabAppearance(); next.clipTitles = false; next.clipSidebarTitles = true;
    next.maximumSidebarTitleWidth = 211; QVERIFY(manager.setTabAppearance(next));
    QVERIFY(!regular->isChecked() && !regularWidth->isEnabled()); QCOMPARE(regularWidth->value(), 73);
    QVERIFY(sidebar->isChecked() && sidebarWidth->isEnabled()); QCOMPARE(sidebarWidth->value(), 211);
    const auto saved = manager.saveLayout();
    WindowManager restored; QVERIFY(restored.restoreLayout(saved));
    QCOMPARE(restored.tabAppearance(), manager.tabAppearance());
    QVERIFY(!manager.execute("presentation.toggleSidebarTitleClipping", {{"visible", "false"}}));
    QCOMPARE(manager.saveLayout(), saved);
}

QTEST_MAIN(TabAppearanceTest)
#include "tab_appearance_test.moc"
