// SPDX-License-Identifier: MIT
// Family lists, validation, live changes in existing/future windows and zoom
// round trips. No visible windows or assertions about unsettled appearance.
#include <window_system/interface_appearance.hpp>
#include <window_system/window_manager.hpp>
#include "docking/tab_bar.hpp"
#include "chrome/command_strip.hpp"
#include "platform/font_families.hpp"
#include <QApplication>
#include <QPlainTextEdit>
#include <QLabel>
#include <QMenuBar>
#include <QPaintEngine>
#include <QPainter>
#include <QStyleOptionMenuItem>
#include <QSpinBox>
#include <QTest>
#include <QToolButton>
#include <limits>

using namespace window_system;
namespace {
class FontRecordingDevice final : public QPaintDevice {
    class Engine final : public QPaintEngine {
    public:
        Engine() : QPaintEngine(AllFeatures) {}
        QList<QFont> fonts;
        bool begin(QPaintDevice*) override { return true; }
        bool end() override { return true; }
        Type type() const override { return User; }
        void updateState(const QPaintEngineState&) override {}
        void drawPixmap(const QRectF&, const QPixmap&, const QRectF&) override {}
        void drawPath(const QPainterPath&) override {}
        void drawTextItem(const QPointF&, const QTextItem& item) override { fonts.append(item.font()); }
    };
    mutable Engine engine_;
public:
    QPaintEngine* paintEngine() const override { return &engine_; }
    const QList<QFont>& fonts() const { return engine_.fonts; }
protected:
    int metric(PaintDeviceMetric metric) const override {
        switch (metric) {
        case PdmWidth: return 400; case PdmHeight: return 80; case PdmDepth: return 32;
        case PdmDpiX: case PdmDpiY: case PdmPhysicalDpiX: case PdmPhysicalDpiY: return 96;
        case PdmDevicePixelRatio: return 1;
        case PdmDevicePixelRatioScaled: return int(devicePixelRatioFScale());
        default: return 0;
        }
    }
};
}
class InterfaceAppearanceTest final : public QObject {
    Q_OBJECT
private slots:
    void familyListsAndValidation();
    void menusFollowFontChanges();
    void menusFollowFontChanges_data();
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
#ifdef Q_OS_WIN
    QCOMPARE(controller.font().hintingPreference(), QFont::PreferNoHinting);
    QCOMPARE(qApp->font().hintingPreference(), QFont::PreferNoHinting);
    QVERIFY(!(controller.font().styleStrategy() & (QFont::NoAntialias | QFont::NoSubpixelAntialias)));
#endif
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
    auto* caption = root->findChild<QToolButton*>("CaptionButton"); QVERIFY(caption);
    const auto originalCaption = caption->size();
    auto value = controller.appearance(); value.zoomLevel = 1;
    QVERIFY(controller.setAppearance(value));
    QCOMPARE(manager.paneWidget(pane), content); QCOMPARE(manager.windowWidget(window), root);
    QVERIFY(bar->metrics().iconSize > icon); QVERIFY(caption->width() > originalCaption.width());
    QVERIFY(qAbs(bar->font().pointSizeF() / originalFont - 1.2) < 0.01);
    const auto second = manager.createWindow(WindowKind::Floating, {"Second", QSize(600, 400)}, window);
    manager.addTab(second, new QPlainTextEdit, {"Second tab", "test", {}});
    const auto* secondBar = manager.windowWidget(second)->findChild<TabBar*>(); QVERIFY(secondBar);
    QCOMPARE(secondBar->metrics().iconSize, bar->metrics().iconSize);
    for (int step = 0; step < 8; ++step) {
        value.zoomLevel = 0.5; QVERIFY(controller.setAppearance(value));
        value.zoomLevel = 0; QVERIFY(controller.setAppearance(value));
    }
    QCOMPARE(bar->metrics().iconSize, icon); QCOMPARE(caption->size(), originalCaption);
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
    QCOMPARE(menu->font().families(), controller.font().families());
    QCOMPARE(menu->font().pointSizeF(), controller.font().pointSizeF());
    QCOMPARE(menu->font().hintingPreference(), controller.font().hintingPreference());
    for (auto* popup : root->findChildren<QMenu*>()) {
        QCOMPARE(popup->font().families(), controller.font().families());
        QCOMPARE(popup->font().pointSizeF(), controller.font().pointSizeF());
        QCOMPARE(popup->font().hintingPreference(), controller.font().hintingPreference());
    }
    // Record the font actually passed to Qt's text drawing, not a screenshot
    // or just the QWidget property. Match QMenuBar::initStyleOption's inputs.
    FontRecordingDevice recorded;
    QPainter painter(&recorded); painter.setFont(menu->font());
    QStyleOptionMenuItemV2 item;
    item.palette = menu->palette(); item.state = QStyle::State_Enabled; item.fontMetrics = menu->fontMetrics();
    item.rect = QRect(0, 0, 400, 80); item.text = "File(&F)";
    menu->style()->drawControl(QStyle::CE_MenuBarItem, &item, &painter, menu);
    painter.end();
    QVERIFY(!recorded.fonts().isEmpty());
    QCOMPARE(recorded.fonts().last().families(), controller.font().families());
    QCOMPARE(recorded.fonts().last().pointSizeF(), controller.font().pointSizeF());
    QCOMPARE(recorded.fonts().last().hintingPreference(), controller.font().hintingPreference());
}
QTEST_MAIN(InterfaceAppearanceTest)
#include "interface_appearance_test.moc"
