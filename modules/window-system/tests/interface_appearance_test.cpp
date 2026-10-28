// SPDX-License-Identifier: MIT
// Family lists, validation, live changes in existing/future windows and zoom
// round trips. No visible windows or assertions about unsettled appearance.
#include <window_system/interface_appearance.hpp>
#include <window_system/window_manager.hpp>
#include "docking/tab_bar.hpp"
#include "docking/edge_tab_button.hpp"
#include "chrome/command_strip.hpp"
#include "platform/font_families.hpp"
#include <QApplication>
#include <QPlainTextEdit>
#include <QLabel>
#include <QMenuBar>
#include <QSpinBox>
#include <QTest>
#include <limits>

using namespace window_system;
class InterfaceAppearanceTest final : public QObject {
    Q_OBJECT
private slots:
    void familyListsAndValidation();
    void menusFollowFontChanges();
    void menusFollowFontChanges_data();
    void railsFollowFontChanges();
    void railsFollowFontChanges_data();
    void familyAliasesPreserveFallbacks();
    void zoomUpdatesExistingAndFutureWindowsWithoutReplacingContent_data();
    void zoomUpdatesExistingAndFutureWindowsWithoutReplacingContent();
    void settingsAndCommandsShareOneState();
    void savedToolbarWidthsRemainValidAtAnotherZoom();
};
void InterfaceAppearanceTest::familyListsAndValidation()
{
    QCOMPARE(interfaceFontFamilies("'First Family', \"Second, Family\", sans-serif, first family"),
        QStringList({"First Family", "Second, Family", "sans-serif"}));
    InterfaceAppearanceController controller;
    const auto original = controller.appearance();
    for (auto bad : {InterfaceAppearance{"'unclosed", 13, 0}, InterfaceAppearance{{}, 0, 0},
         InterfaceAppearance{{}, 97, 0}, InterfaceAppearance{{}, 13, 9},
         InterfaceAppearance{{}, 13, std::numeric_limits<double>::quiet_NaN()}}) {
        QVERIFY(!controller.setAppearance(bad)); QVERIFY(controller.appearance() == original);
    }
    auto value = original; value.fontFamily = "Missing Family, sans-serif";
    QVERIFY(controller.setAppearance(value)); QCOMPARE(controller.font().families(), interfaceFontFamilies(value.fontFamily));
    value.zoomLevel = InterfaceAppearance::maximumZoomLevel; QVERIFY(controller.setAppearance(value));
    QCOMPARE(uiPixels(std::numeric_limits<int>::max()), std::numeric_limits<int>::max());
}
void InterfaceAppearanceTest::zoomUpdatesExistingAndFutureWindowsWithoutReplacingContent_data()
{
    QTest::addColumn<int>("placement");
    QTest::newRow("top") << int(TabBarPlacement::Top);
    QTest::newRow("bottom") << int(TabBarPlacement::Bottom);
    QTest::newRow("left") << int(TabBarPlacement::Left);
    QTest::newRow("right") << int(TabBarPlacement::Right);
}
void InterfaceAppearanceTest::zoomUpdatesExistingAndFutureWindowsWithoutReplacingContent()
{
    QFETCH(int, placement);
    InterfaceAppearanceController controller;
    WindowManager manager;
    auto appearance = manager.tabAppearance(); appearance.centralPlacement = TabBarPlacement(placement);
    QVERIFY(manager.setTabAppearance(appearance));
    const auto window = manager.createWindow(WindowKind::Workbench, {"Test", QSize(900, 600)});
    auto* content = new QPlainTextEdit;
    const auto tab = manager.addTab(window, content, {"A long enough tab", "test", {}});
    auto* root = manager.windowWidget(window);
    auto* bar = root->findChild<TabBar*>(); QVERIFY(bar);
    const auto icon = bar->metrics().iconSize;
    const auto originalFont = bar->font().pointSizeF();
    const auto pane = manager.model().tab(tab)->activePane;
    auto value = controller.appearance(); value.zoomLevel = 1;
    QVERIFY(controller.setAppearance(value));
    QCOMPARE(manager.paneWidget(pane), content); QCOMPARE(manager.windowWidget(window), root);
    QVERIFY(bar->metrics().iconSize > icon);
    QVERIFY(qAbs(bar->font().pointSizeF() / originalFont - 1.2) < 0.01);
    const auto second = manager.createWindow(WindowKind::Floating, {"Second", QSize(600, 400)}, window);
    manager.addTab(second, new QPlainTextEdit, {"Second tab", "test", {}});
    const auto* secondBar = manager.windowWidget(second)->findChild<TabBar*>(); QVERIFY(secondBar);
    QCOMPARE(secondBar->metrics().iconSize, bar->metrics().iconSize);
    for (int step = 0; step < 8; ++step) {
        value.zoomLevel = 0.5; QVERIFY(controller.setAppearance(value));
        value.zoomLevel = 0; QVERIFY(controller.setAppearance(value));
    }
    QCOMPARE(bar->metrics().iconSize, icon);
    QCOMPARE(bar->font().pointSizeF(), originalFont); QCOMPARE(manager.paneWidget(pane), content);
}
void InterfaceAppearanceTest::settingsAndCommandsShareOneState()
{
    InterfaceAppearanceController controller;
    WindowManager manager;
    const auto window = manager.createWindow(WindowKind::Workbench);
    manager.showSettings(window);
    auto* size = manager.windowWidget(window)->findChild<QSpinBox*>("InterfaceFontSize"); QVERIFY(size);
    const int original = size->value();
    QVERIFY(manager.execute("workbench.action.increaseInterfaceFontSize", {}, window));
    QCOMPARE(size->value(), original + 1);
    size->setValue(original + 2); QCOMPARE(controller.appearance().fontSize, original + 2);
    QVERIFY(manager.execute("workbench.action.zoomIn", {}, window)); QCOMPARE(controller.appearance().zoomLevel, 1);
    QVERIFY(manager.execute("workbench.action.zoomReset", {}, window)); QCOMPARE(interfaceScale(), 1);
    QVERIFY(manager.execute("workbench.action.resetInterfaceFontSize", {}, window)); QCOMPARE(size->value(), original);
}
void InterfaceAppearanceTest::savedToolbarWidthsRemainValidAtAnotherZoom()
{
    InterfaceAppearanceController controller;
    QWidget root;
    CommandStripHost original(&root);
    original.addStrip("tools", new QLabel("Tools", &original), 0);
    QVERIFY(original.setStripWidth("tools", CommandStripHost::minimumSavedWidth(true, true)));
    const auto saved = original.saveLayout();
    auto value = controller.appearance(); value.zoomLevel = 4; QVERIFY(controller.setAppearance(value));
    CommandStripHost restored(&root);
    restored.addStrip("tools", new QLabel("Tools", &restored), 0);
    restored.setLocked(false);
    restored.restoreLayout(saved);
    QVERIFY(restored.isLocked());
    QCOMPARE(restored.saveLayout().value("strips").toObject().value("tools").toObject().value("width"),
        saved.value("strips").toObject().value("tools").toObject().value("width"));
}
void InterfaceAppearanceTest::familyAliasesPreserveFallbacks()
{
    QStringList lookedUp;
    const auto mapped = mapFontFamilyAliases({"Legacy Family", "Typographic Family", "Missing Family", "SECOND"},
        {"Typographic Family", "Second"}, [&](const QString& name) {
            lookedUp.append(name);
            return name == "Legacy Family" ? QStringLiteral("Typographic Family") : QStringLiteral("Unavailable Target");
        });
    QCOMPARE(mapped, QStringList({"Typographic Family", "Typographic Family", "Missing Family", "SECOND"}));
    QCOMPARE(lookedUp, QStringList({"Legacy Family", "Missing Family"}));
}
void InterfaceAppearanceTest::menusFollowFontChanges_data()
{
    QTest::addColumn<bool>("startup");
    QTest::newRow("startup") << true;
    QTest::newRow("live") << false;
}
void InterfaceAppearanceTest::menusFollowFontChanges()
{
    QFETCH(bool, startup);
    InterfaceAppearanceController controller;
    auto value = controller.appearance(); value.fontFamily = "serif"; value.fontSize = 26;
    if (startup) QVERIFY(controller.setAppearance(value));
    WindowManager manager;
    const auto window = manager.createWindow();
    manager.addTab(window, new QLabel("Content"), {"Tab", "test", {}});
    auto* root = manager.windowWidget(window);
    auto* menu = root->findChild<QMenuBar*>("MenuStrip"); QVERIFY(menu);
    if (!startup) QVERIFY(controller.setAppearance(value));
    QCoreApplication::processEvents();
    // Theme stylesheet installation repopulates Qt's native class fonts.
    // Newly created controls must still inherit the full interface font.
    QMenu futurePopup; QMenuBar futureBar;
    for (auto* widget : {static_cast<QWidget*>(&futurePopup), static_cast<QWidget*>(&futureBar)}) {
        QCOMPARE(widget->font().families(), controller.font().families());
        QCOMPARE(widget->font().pointSizeF(), controller.font().pointSizeF());
        QVERIFY(!widget->testAttribute(Qt::WA_SetFont));
    }
    QCOMPARE(menu->font().families(), controller.font().families());
    QCOMPARE(menu->font().pointSizeF(), controller.font().pointSizeF());
    for (auto* popup : root->findChildren<QMenu*>()) {
        QCOMPARE(popup->font().families(), controller.font().families());
        QCOMPARE(popup->font().pointSizeF(), controller.font().pointSizeF());
    }

}
void InterfaceAppearanceTest::railsFollowFontChanges_data()
{
    QTest::addColumn<int>("edge"); QTest::addColumn<bool>("startup");
    for (int edge = 0; edge < 4; ++edge)
        for (bool startup : {false, true})
            QTest::newRow(qPrintable(QString("%1-%2").arg(edge).arg(startup))) << edge << startup;
}
void InterfaceAppearanceTest::railsFollowFontChanges()
{
    QFETCH(int, edge); QFETCH(bool, startup);
    InterfaceAppearanceController controller;
    auto value = controller.appearance(); value.fontFamily = "serif"; value.fontSize = 26; value.zoomLevel = 0.5;
    if (startup) QVERIFY(controller.setAppearance(value));
    WindowManager manager;
    auto appearance = manager.tabAppearance(); appearance.showSidebarIcons = true; appearance.showSidebarNumbers = true;
    QVERIFY(manager.setTabAppearance(appearance));
    const auto window = manager.createWindow();
    const auto tab = manager.addTab(window, new QLabel("Content"), {"A wide sidebar label", "test", {}});
    QVERIFY(manager.moveTabToEdge(tab, Edge(edge)));
    auto* root = manager.windowWidget(window);
    auto* rail = dynamic_cast<EdgeTabButton*>(root->findChild<QAbstractButton*>("EdgeButton")); QVERIFY(rail);
    if (!startup) QVERIFY(controller.setAppearance(value));
    for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
    QCOMPARE(rail->font().families(), controller.font().families());
}
QTEST_MAIN(InterfaceAppearanceTest)
#include "interface_appearance_test.moc"
