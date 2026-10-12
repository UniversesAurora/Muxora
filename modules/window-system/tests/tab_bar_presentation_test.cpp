// SPDX-License-Identifier: MIT
#include "chrome/workspace_window.hpp"
#include "docking/dock_surface.hpp"
#include "docking/tab_strip.hpp"
#include <window_system/window_manager.hpp>
#include <QApplication>
#include <QLabel>
#include <QJsonArray>
#include <QMenuBar>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QStyleOptionSlider>
#include <QTest>
#include <QWheelEvent>
#include <QVBoxLayout>
#include <algorithm>

using namespace window_system;

class TabBarPresentationTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); }
    void hidingAppliesToTheWholeWindow_data();
    void hidingAppliesToTheWholeWindow();
    void hiddenSettingRestoresAndSurvivesNewGroups();
    void thinScrollbarRendersAndDragsWithoutSelectingTabs();
    void compactIconsLeaveARealCloseTarget();
    void invalidPresentationSettingsLeaveTheLayoutUnchanged();
    void lastClosePublishesTheFinalGeometryBeforeRemoval();
};

void TabBarPresentationTest::hidingAppliesToTheWholeWindow_data()
{
    QTest::addColumn<bool>("floating");
    QTest::newRow("workbench") << false;
    QTest::newRow("floating") << true;
}
void TabBarPresentationTest::hidingAppliesToTheWholeWindow()
{
    QFETCH(bool, floating);
    WindowManager manager;
    const auto root = manager.createWindow();
    const auto target = floating ? manager.createWindow(WindowKind::Floating, {}, root) : root;
    auto* firstContent = new QLabel("First");
    const auto first = manager.addTab(target, firstContent, {"First", "label", {}});
    const auto second = manager.addTab(target, new QLabel("Second"), {"Second", "label", {}});
    QVERIFY(manager.moveTab(second, manager.model().groupForTab(first), Placement::Right));
    const auto pinned = manager.addTab(target, new QLabel("Pinned"), {"Pinned", "label", {}});
    QVERIFY(manager.moveTabToEdge(pinned, Edge::Left, true));
    const auto hidden = manager.addTab(target, new QLabel("Hidden"), {"Hidden", "label", {}});
    QVERIFY(manager.moveTabToEdge(hidden, Edge::Bottom));
    manager.showWindow(target);
    auto* window = static_cast<WorkspaceWindow*>(manager.windowWidget(target));
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    const auto model = manager.model().toJson();
    auto* surface = window->surface();
    QCOMPARE(surface->tabBarRects().size(), 3);
    QCOMPARE(surface->edgeBarRects().size(), 1);
    const auto headers = window->findChildren<QWidget*>("TabGroupHeader");
    QCOMPARE(headers.size(), 3);
    QVERIFY(manager.execute("window.tabBars.toggle", {}, target));
    QVERIFY(!manager.tabBarsVisible(target));
    QVERIFY(surface->tabBarRects().isEmpty()); QVERIFY(surface->edgeBarRects().isEmpty());
    for (auto* header : headers) {
        QVERIFY(header->isHidden());
        auto* stack = header->parentWidget()->findChild<QStackedWidget*>(); QVERIFY(stack);
        QCOMPARE(stack->geometry().top(), 0);
    }
    QCOMPARE(manager.model().toJson(), model);
    QCOMPARE(manager.paneWidget(manager.model().tab(first)->activePane), firstContent);
    QVERIFY(manager.activateTab(hidden));
    auto* peek = window->findChild<QWidget*>("PeekTitleBar"); QVERIFY(peek); QVERIFY(peek->isHidden());
    QVERIFY(manager.setTabBarsVisible(target, true));
    for (auto* header : headers) QVERIFY(header->isVisible());
    QVERIFY(peek->isVisible());
    QCOMPARE(surface->tabBarRects().size(), 3); QCOMPARE(surface->edgeBarRects().size(), 1);
    if (floating) QVERIFY(manager.tabBarsVisible(root));
}

void TabBarPresentationTest::hiddenSettingRestoresAndSurvivesNewGroups()
{
    WindowManager manager;
    const auto root = manager.createWindow();
    const auto tab = manager.addTab(root, new QLabel("one"), {"one", "label", {}});
    QVERIFY(manager.setTabBarsVisible(root, false));
    const auto other = manager.addTab(root, new QLabel("two"), {"two", "label", {}});
    QVERIFY(manager.moveTab(other, manager.model().groupForTab(tab), Placement::Right));
    WindowManager restored;
    restored.setContentFactory([](const TabOptions&, PaneId) { return new QLabel("Restored"); });
    QVERIFY(restored.restoreLayout(manager.saveLayout()));
    QVERIFY(!restored.tabBarsVisible(root));
    auto* window = static_cast<WorkspaceWindow*>(restored.windowWidget(root));
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QVERIFY(window->surface()->tabBarRects().isEmpty());
    QVERIFY(restored.setTabBarsVisible(root, true));
    QCOMPARE(window->surface()->tabBarRects().size(), 2);
    auto* toggle = window->findChild<QAction*>("window.tabBars.toggle"); QVERIFY(toggle);
    auto* menu = qobject_cast<QMenu*>(toggle->parent()); QVERIFY(menu);
    QMetaObject::invokeMethod(menu, "aboutToShow");
    QVERIFY(toggle->isCheckable()); QVERIFY(toggle->isChecked());
}

void TabBarPresentationTest::thinScrollbarRendersAndDragsWithoutSelectingTabs()
{
    QWidget root;
    auto* column = new QVBoxLayout(&root);
    auto* tabs = new GroupTabs(defaultTheme(), &root); column->addWidget(tabs);
    for (int index = 0; index < 12; ++index)
        tabs->addTab(new QLabel("Content"), {}, QString("Long tab %1").arg(index), QUuid::createUuid());
    applyTheme(&root); root.resize(380, 240); root.show();
    QVERIFY(QTest::qWaitForWindowExposed(&root));
    auto* scroll = tabs->findChild<QScrollArea*>("TabScrollViewport"); QVERIFY(scroll);
    auto* scrollbar = tabs->findChild<QScrollBar*>("TabScrollBar"); QVERIFY(scrollbar);
    QTRY_VERIFY(scrollbar->maximum() > 0);
    QTest::mouseMove(tabs->tabBar(), QPoint(30, 10));
    QTRY_VERIFY(scrollbar->isVisible());
    QVERIFY(scrollbar->height() <= 4);
    QStyleOptionSlider option; option.initFrom(scrollbar);
    option.orientation = Qt::Horizontal; option.minimum = 0; option.maximum = scrollbar->maximum();
    option.pageStep = scrollbar->pageStep(); option.sliderPosition = scrollbar->value();
    const auto handle = scrollbar->style()->subControlRect(QStyle::CC_ScrollBar, &option, QStyle::SC_ScrollBarSlider, scrollbar);
    QVERIFY(!handle.isEmpty()); QVERIFY(handle.height() >= 2);
    const auto image = scrollbar->grab().toImage();
    const auto center = QPoint(qRound(handle.center().x() * image.devicePixelRatio()), qRound(handle.center().y() * image.devicePixelRatio()));
    QCOMPARE(image.pixelColor(center).rgb(), QColor("#9C8FB0").rgb());
    const auto backward = scrollbar->style()->subControlRect(QStyle::CC_ScrollBar, &option, QStyle::SC_ScrollBarSubLine, scrollbar);
    const auto forward = scrollbar->style()->subControlRect(QStyle::CC_ScrollBar, &option, QStyle::SC_ScrollBarAddLine, scrollbar);
    QVERIFY(backward.isEmpty()); QVERIFY(forward.isEmpty());
    const auto selected = tabs->currentIndex();
    QTest::mousePress(scrollbar, Qt::LeftButton, Qt::NoModifier, handle.center());
    QTest::mouseMove(scrollbar, handle.center() + QPoint(60, 0));
    QTest::mouseRelease(scrollbar, Qt::LeftButton, Qt::NoModifier, handle.center() + QPoint(60, 0));
    QTRY_VERIFY(scrollbar->value() > 0); QCOMPARE(scroll->horizontalScrollBar()->value(), scrollbar->value());
    QCOMPARE(tabs->currentIndex(), selected);
    QTest::mouseMove(&root, QPoint(1, root.height() - 1)); QTRY_VERIFY(scrollbar->isHidden());
    tabs->setTabBarVisible(false); QVERIFY(tabs->viewportRectGlobal().isEmpty()); QVERIFY(scrollbar->isHidden());
}

void TabBarPresentationTest::compactIconsLeaveARealCloseTarget()
{
    GroupTabs tabs(defaultTheme(), nullptr);
    QPixmap icon(16, 16); icon.fill(QColor("#ff0000"));
    tabs.addTab(new QLabel("Content"), QIcon(icon), "MMMM", QUuid::createUuid());
    applyTheme(&tabs); tabs.resize(380, 240); tabs.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar(); QCOMPARE(bar->iconSize(), QSize(16, 16));
    const auto image = bar->grab().toImage(); const auto ratio = image.devicePixelRatio();
    int iconRight = -1; int textLeft = image.width();
    for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) {
        const auto color = image.pixelColor(x, y);
        if (color.rgb() == QColor("#ff0000").rgb()) iconRight = std::max(iconRight, x);
        if (color.rgb() == defaultTheme().text.rgb()) textLeft = std::min(textLeft, x);
    }
    QVERIFY(iconRight >= 0); QVERIFY(textLeft < image.width());
    QVERIFY2((textLeft - iconRight - 1) / ratio <= 7, "Icon/text spacing must be compact");
    QSignalSpy changes(bar, &QTabBar::currentChanged);
    int closed = 0; tabs.closeRequested = [&](int index) { QCOMPARE(index, 0); ++closed; };
    const auto rect = bar->tabRect(0);
    QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, QPoint(rect.right() - 14, rect.center().y()));
    QCOMPARE(closed, 1); QCOMPARE(changes.count(), 0);
}

void TabBarPresentationTest::invalidPresentationSettingsLeaveTheLayoutUnchanged()
{
    WindowManager manager; const auto root = manager.createWindow();
    const auto tab = manager.addTab(root, new QLabel("Content"), {"one", "label", {}});
    const auto before = manager.saveLayout();
    QLabel unadopted;
    TabOptions invalidOptions{"invalid", "label", {}};
    invalidOptions.topBarSharing = static_cast<PaneBarSharing>(42);
    QVERIFY(manager.addTab(root, &unadopted, invalidOptions).isNull());
    QVERIFY(!unadopted.parentWidget());
    QVERIFY(!manager.setTabBarsVisible({}, false));
    QVERIFY(!manager.execute("tab.topBar.setSharing", {{"sharing", "invalid"}}, root));
    QCOMPARE(manager.saveLayout(), before);
    WindowManager restored; restored.setContentFactory([](const TabOptions&, PaneId) { return new QLabel; });
    auto malformed = before; auto panes = malformed.value("panes").toArray();
    auto descriptor = panes[0].toObject(); descriptor.insert("topBarSharing", 42); panes[0] = descriptor; malformed.insert("panes", panes);
    QVERIFY(!restored.restoreLayout(malformed)); QVERIFY(restored.model().windows().isEmpty());
    QVERIFY(manager.execute("tab.topBar.setSharing", {{"sharing", "perPane"}}, root));
    QCOMPARE(manager.tabOptions(tab).topBarSharing, PaneBarSharing::PerPane);
    QCOMPARE(manager.tabOptions(tab).bottomBarSharing, PaneBarSharing::Shared);
}

void TabBarPresentationTest::lastClosePublishesTheFinalGeometryBeforeRemoval()
{
    WindowManager manager; const auto root = manager.createWindow();
    manager.addTab(root, new QLabel("Content"), {"one", "label", {}});
    manager.showWindow(root); auto* window = manager.windowWidget(root);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QJsonObject saved;
    connect(&manager, &WindowManager::lastWindowAboutToClose, this, [&] { saved = manager.saveLayout(); });
    window->setGeometry(100, 120, 640, 480);
    QVERIFY(manager.requestCloseWindow(root));
    QVERIFY(!saved.isEmpty()); const auto record = saved.value("windows").toArray()[0].toObject();
    QCOMPARE(record.value("x").toInt(), 100); QCOMPARE(record.value("y").toInt(), 120);
    QCOMPARE(record.value("width").toInt(), 640); QCOMPARE(record.value("height").toInt(), 480);
    QVERIFY(manager.model().windows().isEmpty());
}

QTEST_MAIN(TabBarPresentationTest)
#include "tab_bar_presentation_test.moc"
