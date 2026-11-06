// SPDX-License-Identifier: MIT
// Phase 10.4h: the Quick settings page from presentation-free descriptions,
// and the controls the shortcuts page needs. Dimensions: control kind (check
// box, drop-down, whole and decimal number with unit, text, slider with
// number, JSON text with apply button, key recorder, list, button, text
// area), edited here or elsewhere,
// search, opening an open page again, shortcuts while recording keys.
#include "quick/settings_page_model.hpp"
#include "quick/qml_scene_component.hpp"
#include "quick/settings_style.hpp"
#include "settings/setting_description.hpp"
#include "quick_menu_test_support.hpp"
#include <window_system/quick_workspace.hpp>
#include <window_system/window_manager.hpp>
#include <QFontMetricsF>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <algorithm>

using namespace window_system;
using namespace quick_menu_test;

namespace {
QPoint centre(const QQuickItem* item) { return item->mapToScene(item->boundingRect().center()).toPoint(); }
// Scrolls the settings list so the control is shown, as a user would.
void scrollTo(QQuickItem* page, QQuickItem* control)
{
    auto* rows = namedItem(page, QStringLiteral("SettingsRows"));
    auto* content = rows->property("contentItem").value<QQuickItem*>();
    const qreal top = control->mapToItem(content, QPointF()).y() - 40;
    const qreal end = rows->property("contentHeight").toReal() - rows->height();
    rows->setProperty("contentY", std::clamp(top, 0.0, std::max(0.0, end)));
}
// Form controls on their own, from QML given here.
std::unique_ptr<QQuickItem> createControls(quick::SettingsStyle& style, const QByteArray& body)
{
    QQmlComponent component(quick::sceneComponent(quick::SceneComponent::SettingsPage).engine());
    component.setData("import QtQuick\nimport WindowSystem.Internal\nColumn {\n"
                      "    id: root\n    required property SettingsStyle style\n    spacing: 8\n" + body + "\n}\n", QUrl());
    std::unique_ptr<QQuickItem> item(qobject_cast<QQuickItem*>(
        component.createWithInitialProperties({{"style", QVariant::fromValue(&style)}})));
    if (!item) qWarning() << component.errorString();
    return item;
}
void typeInto(QQuickWindow* window, QQuickItem* field, const QString& text)
{
    QTest::mouseDClick(window, Qt::LeftButton, Qt::NoModifier, centre(field));
    QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
    for (const auto letter : text) QTest::keyClick(window, letter.toLatin1());
    QTest::keyClick(window, Qt::Key_Return);
}
} // namespace

class QuickSettingsTest final : public QObject {
    Q_OBJECT
private slots:
    void init()
    {
        QTest::failOnWarning(QRegularExpression(".*(Unable to assign|TypeError|ReferenceError|Binding loop).*"));
    }

    void windowSettingsEditTheBehavior()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto window = manager.createWindow(WindowKind::Workbench, {QStringLiteral("Main"), QSize(1000, 900)});
        manager.addTab(window, {QStringLiteral("Content")});
        QTRY_VERIFY(workspace.window(window));
        auto* view = workspace.window(window);
        view->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(view));
        manager.showSettings(window);
        QQuickItem* page = nullptr;
        QTRY_VERIFY((page = namedItem(view->contentItem(), QStringLiteral("SettingsPage"))) && page->isVisible() && page->width() > 0);
        const auto tabs = manager.model().orderedTabs(window).size();
        // Opening it again brings the same page forward.
        manager.showSettings(window);
        QCOMPARE(manager.model().orderedTabs(window).size(), tabs);

        auto* reveal = namedItem(page, QStringLiteral("SettingControl/window.revealDuringDrag"));
        auto* delay = namedItem(page, QStringLiteral("SettingControl/window.revealDelayMs"));
        QVERIFY(reveal && delay);
        scrollTo(page, reveal);
        QTRY_VERIFY(delay->isEnabled() == manager.behavior().revealDuringDrag);
        const bool revealed = manager.behavior().revealDuringDrag;
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, centre(reveal));
        QTRY_COMPARE(manager.behavior().revealDuringDrag, !revealed);
        QTRY_COMPARE(delay->isEnabled(), !revealed);
        if (!delay->isEnabled()) {
            QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, centre(reveal));
            QTRY_VERIFY(delay->isEnabled());
        }
        // The unit is only shown: the leading number is read.
        typeInto(view, delay, QStringLiteral("800 ms"));
        QTRY_COMPARE(manager.behavior().revealDelayMs, 800);
        QTRY_COMPARE(delay->property("displayText").toString(), QStringLiteral("800 ms"));

        // The drop-down marks the default entry and applies a choice.
        auto* position = namedItem(page, QStringLiteral("SettingControl/window.newTabPosition"));
        QVERIFY(position);
        scrollTo(page, position);
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, centre(position));
        QQuickWindow* list = nullptr;
        QTRY_VERIFY((list = popup(view)));
        QVERIFY(QTest::qWaitForWindowExposed(list));
        QQuickItem* start = nullptr;
        bool marked = false;
        QTRY_VERIFY([&] {
            QList<QQuickItem*> items{list->contentItem()};
            while (!items.isEmpty()) {
                auto* item = items.takeFirst();
                const auto text = item->property("text").toString();
                if (text == QStringLiteral("Start of tab group")) start = item;
                if (text == QStringLiteral("Default") && item->isVisible()) marked = true;
                items += item->childItems();
            }
            return start && marked;
        }());
        QTest::mouseClick(list, Qt::LeftButton, Qt::NoModifier, centre(start));
        QTRY_COMPARE(manager.behavior().newTabPosition, NewTabPosition::Beginning);

        // A change made elsewhere reaches the controls.
        auto behavior = manager.behavior();
        behavior.newTabPosition = NewTabPosition::End;
        manager.setBehavior(behavior);
        QTRY_COMPARE(position->property("currentText").toString(), QStringLiteral("End of tab group"));

        // Search keeps the matching settings, their category and its parents,
        // as the Widgets page does.
        auto* search = namedItem(page, QStringLiteral("SettingsSearch"));
        QVERIFY(search);
        typeInto(view, search, QStringLiteral("delay"));
        QTRY_VERIFY(!namedItem(page, QStringLiteral("SettingRow/window.newTabPosition"))->isVisible());
        QVERIFY(namedItem(page, QStringLiteral("SettingRow/window.revealDelayMs"))->isVisible());
        QVERIFY(namedItem(page, QStringLiteral("SettingsCategoryHeading/window/drag"))->isVisible());
        QVERIFY(namedItem(page, QStringLiteral("SettingsCategoryHeading/window"))->isVisible());
        typeInto(view, search, QStringLiteral("window.newTabPosition"));
        QTRY_VERIFY(namedItem(page, QStringLiteral("SettingRow/window.newTabPosition"))->isVisible());
        QVERIFY(!namedItem(page, QStringLiteral("SettingsCategoryHeading/window/drag"))->isVisible());
        QCOMPARE(namedItem(page, QStringLiteral("SettingsResultCount"))->property("text").toString(), QStringLiteral("1 settings"));
    }

    void visibilityEditsFollowTheSettingsPage()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto first = manager.createWindow(WindowKind::Workbench, {QStringLiteral("First"), QSize(1000, 900)});
        manager.addTab(first, {QStringLiteral("Content")});
        const auto second = manager.createWindow(WindowKind::Workbench, {QStringLiteral("Second"), QSize(1000, 900)});
        manager.addTab(second, {QStringLiteral("Other")});
        manager.showSettings(first);
        QTRY_VERIFY(workspace.window(first));
        auto* view = workspace.window(first);
        view->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(view));
        QQuickItem* page = nullptr;
        QTRY_VERIFY((page = namedItem(view->contentItem(), QStringLiteral("SettingsPage"))) && page->width() > 0);
        auto* compact = namedItem(page, QStringLiteral("SettingControl/window.hideToolbarOptionsWhenLocked"));
        auto* bars = namedItem(page, QStringLiteral("SettingControl/window.visibility.toolbars"));
        QVERIFY(compact && bars);
        scrollTo(page, compact);
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, centre(compact));
        QTRY_VERIFY(manager.behavior().hideToolbarOptionsWhenLocked);
        auto behavior = manager.behavior();
        behavior.hideToolbarOptionsWhenLocked = false;
        manager.setBehavior(behavior);
        QTRY_VERIFY(!compact->property("checked").toBool());
        scrollTo(page, bars);
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, centre(bars));
        QTRY_VERIFY(!manager.barVisibility(first).toolbars);
        QVERIFY(manager.barVisibility(second).toolbars);

        const auto pane = page->property("paneId").toUuid();
        const auto tab = manager.model().tabForPane(pane);
        QVERIFY(manager.moveTabToWindow(tab, second));
        auto* target = workspace.window(second);
        QTRY_COMPARE(page->window(), target);
        QTRY_VERIFY(bars->property("checked").toBool());
        target->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(target));
        scrollTo(page, bars);
        QTest::mouseClick(target, Qt::LeftButton, Qt::NoModifier, centre(bars));
        QTRY_VERIFY(!manager.barVisibility(second).toolbars);
        // A change made through commands or another page reaches this one.
        QVERIFY(manager.setToolbarsVisible(second, true));
        QTRY_VERIFY(bars->property("checked").toBool());
        QVERIFY(!manager.barVisibility(first).toolbars);
        QCOMPARE(workspace.content(pane), page);
    }

    void tabSettingsKeepDependentValues()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto window = manager.createWindow(WindowKind::Workbench, {QStringLiteral("Main"), QSize(1000, 900)});
        manager.addTab(window, {QStringLiteral("Content")});
        manager.showSettings(window);
        QTRY_VERIFY(workspace.window(window));
        auto* view = workspace.window(window);
        view->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(view));
        QQuickItem* page = nullptr;
        QTRY_VERIFY((page = namedItem(view->contentItem(), QStringLiteral("SettingsPage"))) && page->width() > 0);
        auto* clip = namedItem(page, QStringLiteral("SettingControl/tabs.clipTitles"));
        auto* width = namedItem(page, QStringLiteral("SettingControl/tabs.maximumTitleWidth"));
        auto* allRows = namedItem(page, QStringLiteral("SettingControl/tabs.showAllRows"));
        auto* sizing = namedItem(page, QStringLiteral("SettingControl/tabs.stickySizing"));
        auto* shrink = namedItem(page, QStringLiteral("SettingControl/tabs.shrinkStickyTitleWidth"));
        auto* separate = namedItem(page, QStringLiteral("SettingControl/tabs.stickyTabsOnSeparateRow"));
        QVERIFY(clip && width && allRows && sizing && shrink && separate);
        QVERIFY(!width->isEnabled());
        scrollTo(page, clip);
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, centre(clip));
        QTRY_VERIFY(manager.tabAppearance().clipTitles);
        QTRY_VERIFY(width->isEnabled());
        scrollTo(page, width);
        QQuickItem* box = nullptr;
        for (auto* child : width->childItems()) if (child->property("displayText").isValid()) box = child;
        QVERIFY(box);
        typeInto(view, box, QStringLiteral("5000"));
        QTRY_COMPARE(manager.tabAppearance().maximumTitleWidth, 5000);
        auto appearance = manager.tabAppearance();
        appearance.clipTitles = false;
        appearance.maximumTitleWidth = 420;
        appearance.multiline = true;
        manager.setTabAppearance(appearance);
        QTRY_VERIFY(!width->isEnabled());
        QTRY_COMPARE(box->property("displayText").toString(), QStringLiteral("420 px"));
        QTRY_VERIFY(allRows->isEnabled());
        QVERIFY(!shrink->isEnabled());

        scrollTo(page, sizing);
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, centre(sizing));
        QQuickWindow* choices = nullptr;
        QTRY_VERIFY((choices = popup(view)));
        QVERIFY(QTest::qWaitForWindowExposed(choices));
        QQuickItem* shrinkChoice = nullptr;
        QTRY_VERIFY([&] {
            QList<QQuickItem*> items{choices->contentItem()};
            while (!items.isEmpty()) {
                auto* item = items.takeFirst();
                if (item->property("text").toString() == QStringLiteral("Shrink: the start of the title")) shrinkChoice = item;
                items += item->childItems();
            }
            return shrinkChoice;
        }());
        QTest::mouseClick(choices, Qt::LeftButton, Qt::NoModifier, centre(shrinkChoice));
        QTRY_COMPARE(manager.tabAppearance().stickySizing, StickyTabSizing::Shrink);
        QTRY_VERIFY(shrink->isEnabled());
        scrollTo(page, separate);
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, centre(separate));
        QTRY_VERIFY(manager.tabAppearance().stickyTabsOnSeparateRow);
        appearance = manager.tabAppearance();
        appearance.stickySizing = StickyTabSizing::Compact;
        appearance.centralPlacement = appearance.pinnedPlacement = TabBarPlacement::Right;
        manager.setTabAppearance(appearance);
        QTRY_VERIFY(!allRows->isEnabled());
        QTRY_VERIFY(!shrink->isEnabled());
        QTRY_COMPARE(sizing->property("currentText").toString(), QStringLiteral("Compact: icon or first letter only"));
        QVERIFY(manager.tabAppearance().multiline);
        QCOMPARE(manager.tabAppearance().maximumTitleWidth, 420);
    }

    // Text, decimal and slider controls, from descriptions made up here.
    void textDecimalAndSliderControls()
    {
        QString text = QStringLiteral("first");
        double scale = 120;
        int width = 160;
        QVector<SettingDescription> settings;
        SettingDescription field{QStringLiteral("test"), QStringLiteral("test.text"), QStringLiteral("Text"), {}, SettingDescription::Kind::Text};
        field.read = [&] { return text; };
        field.write = [&](const QVariant& value) { text = value.toString(); return true; };
        settings.append(field);
        SettingDescription real{QStringLiteral("test"), QStringLiteral("test.real"), QStringLiteral("Real"), {}, SettingDescription::Kind::Real};
        real.minimum = 50; real.maximum = 300; real.step = 10; real.decimals = 1; real.suffix = QStringLiteral(" %");
        real.read = [&] { return scale; };
        real.write = [&](const QVariant& value) { scale = value.toDouble(); return true; };
        settings.append(real);
        SettingDescription slider{QStringLiteral("test"), QStringLiteral("test.slider"), QStringLiteral("Slider"), {}, SettingDescription::Kind::IntegerSlider};
        slider.minimum = 0; slider.maximum = 1000000; slider.sliderMaximum = 1000; slider.suffix = QStringLiteral(" px");
        slider.read = [&] { return width; };
        slider.write = [&](const QVariant& value) { width = value.toInt(); return true; };
        settings.append(slider);
        auto* model = new quick::SettingsPageModel({{QStringLiteral("test"), QStringLiteral("Test")}}, settings, defaultTheme());
        std::unique_ptr<QQuickItem> page(qobject_cast<QQuickItem*>(
            quick::sceneComponent(quick::SceneComponent::SettingsPage).createWithInitialProperties({{"page", QVariant::fromValue(model)}})));
        QVERIFY(page);
        model->setParent(page.get());
        QQuickWindow window;
        window.resize(900, 700);
        page->setParentItem(window.contentItem());
        page->setSize(window.size());
        window.show(); window.requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(&window));

        auto* textField = namedItem(page.get(), QStringLiteral("SettingControl/test.text"));
        QVERIFY(textField);
        QCOMPARE(textField->property("text").toString(), QStringLiteral("first"));
        // Focusing the field keeps its text.
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, centre(textField));
        QTRY_VERIFY(textField->hasActiveFocus());
        QCOMPARE(textField->property("text").toString(), QStringLiteral("first"));
        typeInto(&window, textField, QStringLiteral("second"));
        QTRY_COMPARE(text, QStringLiteral("second"));

        auto* realBox = namedItem(page.get(), QStringLiteral("SettingControl/test.real"));
        QVERIFY(realBox);
        QCOMPARE(realBox->property("displayText").toString(), QStringLiteral("120.0 %"));
        typeInto(&window, realBox, QStringLiteral("144"));
        QTRY_COMPARE(scale, 144.0);

        // The number box takes the whole range; the slider keeps a value
        // beyond its own and changes it only when moved.
        auto* sliderRow = namedItem(page.get(), QStringLiteral("SettingControl/test.slider"));
        QVERIFY(sliderRow);
        width = 5000;
        model->refresh();
        QQuickItem* box = nullptr;
        for (auto* child : sliderRow->childItems()) if (child->property("displayText").isValid()) box = child;
        QVERIFY(box);
        QTRY_COMPARE(box->property("displayText").toString(), QStringLiteral("5000 px"));
        QCOMPARE(width, 5000);
        auto* bar = sliderRow->childItems().first();
        bar->forceActiveFocus();
        QTest::keyClick(&window, Qt::Key_Left);
        QTRY_COMPARE(width, 999);
    }

    // The tab color list is JSON applied with a button, as on the Widgets
    // page: edited text stays until applied, an invalid text is reported and
    // changes nothing, an applied one shows as saved.
    void tabColorsApplyEditedJson()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto window = manager.createWindow(WindowKind::Workbench, {QStringLiteral("Main"), QSize(1000, 900)});
        manager.addTab(window, {QStringLiteral("Content")});
        QTRY_VERIFY(workspace.window(window));
        auto* view = workspace.window(window);
        view->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(view));
        manager.showSettings(window);
        QQuickItem* page = nullptr;
        QTRY_VERIFY((page = namedItem(view->contentItem(), QStringLiteral("SettingsPage"))) && page->width() > 0);
        auto* text = namedItem(page, QStringLiteral("SettingJson/tabs.colors"));
        auto* apply = namedItem(page, QStringLiteral("SettingApply/tabs.colors"));
        auto* message = namedItem(page, QStringLiteral("SettingMessage/tabs.colors"));
        QVERIFY(text && apply && message);
        QCOMPARE(text->property("text").toString(), tabColorsText(manager.tabAppearance()));
        scrollTo(page, apply);

        const auto original = manager.tabAppearance();
        auto* editor = text->property("editor").value<QQuickItem*>();
        // The text is taller than the box; click its visible part.
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, centre(text));
        QTRY_VERIFY(editor->hasActiveFocus());
        QTest::keyClick(view, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(view, 'x');
        QCOMPARE(text->property("text").toString(), QStringLiteral("x"));
        // A change made elsewhere leaves the edited text.
        auto other = original;
        other.colors.first().name = QStringLiteral("Elsewhere");
        QVERIFY(manager.setTabAppearance(other));
        QCOMPARE(text->property("text").toString(), QStringLiteral("x"));
        scrollTo(page, apply);
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, centre(apply));
        QTRY_VERIFY(message->isVisible());
        QCOMPARE(message->property("text").toString(), QStringLiteral("Color list unchanged: enter a valid JSON array."));
        QCOMPARE(manager.tabAppearance(), other);

        auto renamed = original;
        renamed.colors.first().name = QStringLiteral("Renamed");
        text->setProperty("text", tabColorsText(renamed));
        scrollTo(page, apply);
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, centre(apply));
        QTRY_COMPARE(manager.tabAppearance().colors.first().name, QStringLiteral("Renamed"));
        QCOMPARE(message->property("text").toString(), QStringLiteral("Color list applied."));
        // Unedited again, it follows changes made elsewhere.
        QVERIFY(manager.setTabAppearance(original));
        QTRY_COMPARE(text->property("text").toString(), tabColorsText(original));
    }

    // While the recorder has the focus, keys bound to commands are recorded
    // and run nothing; elsewhere they run their command.
    void keyRecorderKeepsBoundKeys()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto window = manager.createWindow(WindowKind::Workbench, {QStringLiteral("Main"), QSize(800, 600)});
        manager.addTab(window, {QStringLiteral("Content")});
        QTRY_VERIFY(workspace.window(window));
        auto* view = workspace.window(window);
        view->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(view));
        int runs = 0;
        QVERIFY(manager.commands().add({"test.run", "Run", "Test", {}, {}, [&runs](const CommandContext&) { ++runs; return true; }}));
        QVERIFY(manager.commands().setBindings("test.run", {{QKeySequence(QStringLiteral("Ctrl+K, A")), {}}}));
        QTest::keyClick(view, Qt::Key_K, Qt::ControlModifier);
        QTest::keyClick(view, Qt::Key_A);
        QTRY_COMPARE(runs, 1);

        quick::SettingsStyle style(manager.interfaceTheme());
        auto controls = createControls(style, R"(SettingKeySequenceField { objectName: "keys"; style: root.style; placeholderText: "Press keys" })");
        QVERIFY(controls);
        controls->setParentItem(view->contentItem());
        controls->setZ(100);
        auto* keys = namedItem(controls.get(), QStringLiteral("keys"));
        QVERIFY(keys);
        QSignalSpy edits(keys, SIGNAL(edited()));
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, centre(keys));
        QTRY_VERIFY(keys->hasActiveFocus());
        QTest::keyClick(view, Qt::Key_K, Qt::ControlModifier);
        QTest::keyClick(view, Qt::Key_A);
        QTest::keyClick(view, Qt::Key_B);
        QCOMPARE(runs, 1);
        QCOMPARE(keys->property("sequence").toString(), QStringLiteral("Ctrl+K, A, B"));
        QCOMPARE(edits.count(), 3);
        // The fourth stroke ends the sequence; Backspace then clears it and
        // the next stroke starts anew.
        QTest::keyClick(view, Qt::Key_D);
        QCOMPARE(keys->property("sequence").toString(), QStringLiteral("Ctrl+K, A, B, D"));
        QTest::keyClick(view, Qt::Key_Backspace);
        QCOMPARE(keys->property("sequence").toString(), QString());
        QTest::keyClick(view, Qt::Key_E);
        QCOMPARE(keys->property("sequence").toString(), QStringLiteral("E"));
        QCOMPARE(runs, 1);
    }

    void listButtonAndTextArea()
    {
        quick::SettingsStyle style(defaultTheme());
        auto controls = createControls(style, R"(
            property int activated: -1
            SettingTable {
                objectName: "table"; style: root.style; width: 400; height: 160
                columns: [{title: "Command", role: "title"}, {title: "Shortcut", role: "keys", fit: true}]
                rows: [{title: "Open", keys: "Ctrl+O"}, {title: "Close", keys: "Ctrl+Shift+Alt+W"}, {title: "Save", keys: ""}]
                onActivated: (index) => root.activated = index
            }
            SettingButton { objectName: "button"; style: root.style; text: "Record keys"; checkable: true }
            SettingTextArea { objectName: "area"; style: root.style; width: 300; placeholderText: "Arguments" })");
        QVERIFY(controls);
        QQuickWindow window;
        window.resize(500, 400);
        controls->setParentItem(window.contentItem());
        window.show(); window.requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(&window));

        auto* table = namedItem(controls.get(), QStringLiteral("table"));
        auto* rows = namedItem(table, QStringLiteral("SettingTableRows"));
        QVERIFY(table && rows);
        QTRY_COMPARE(table->property("count").toInt(), 3);
        // The fit column is as wide as its widest text; the other takes the rest.
        const auto widths = table->property("widths").toList();
        QFontMetricsF metrics(style.font());
        QVERIFY(widths.at(1).toReal() >= metrics.horizontalAdvance(QStringLiteral("Ctrl+Shift+Alt+W")));
        QCOMPARE(widths.at(0).toReal() + widths.at(1).toReal(), table->width() - 2 * style.unit());
        const qreal rowHeight = table->property("rowHeight").toReal();
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, rows->mapToScene(QPointF(20, rowHeight * 1.5)).toPoint());
        QTRY_COMPARE(table->property("currentIndex").toInt(), 1);
        QVERIFY(rows->hasActiveFocus());
        QTest::keyClick(&window, Qt::Key_Down);
        QCOMPARE(table->property("currentIndex").toInt(), 2);
        QTest::keyClick(&window, Qt::Key_Return);
        QCOMPARE(controls->property("activated").toInt(), 2);

        auto* button = namedItem(controls.get(), QStringLiteral("button"));
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, centre(button));
        QTRY_VERIFY(button->property("checked").toBool());

        auto* area = namedItem(controls.get(), QStringLiteral("area"));
        auto* editor = area->property("editor").value<QQuickItem*>();
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, centre(editor));
        QTRY_VERIFY(editor->hasActiveFocus());
        QTest::keyClick(&window, '{');
        QTest::keyClick(&window, '}');
        QCOMPARE(area->property("text").toString(), QStringLiteral("{}"));
    }
};

QTEST_MAIN(QuickSettingsTest)
#include "quick_settings_test.moc"
