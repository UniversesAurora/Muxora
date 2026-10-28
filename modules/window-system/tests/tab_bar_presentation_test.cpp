// SPDX-License-Identifier: MIT
#include "chrome/workspace_window.hpp"
#include "docking/dock_surface.hpp"
#include "docking/tab_strip.hpp"
#include "style/tab_label_geometry.hpp"
#include "docking/edge_strip.hpp"
#include "commands/popup_menu.hpp"
#include <window_system/window_manager.hpp>
#include <QApplication>
#include <QLabel>
#include <QEnterEvent>
#include <QJsonArray>
#include <QMenuBar>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QStackedWidget>
#include <QStyleOptionSlider>
#include <QTest>
#include <QToolButton>
#include <QWheelEvent>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <algorithm>

using namespace window_system;

class TabBarPresentationTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); }
    void hidingAppliesToTheWholeWindow_data();
    void hidingAppliesToTheWholeWindow();
    void hiddenSettingRestoresAndSurvivesNewGroups();
    void scrollbarDraggingDoesNotSelectTabs();
    void tabListMarksTheActiveTabWithoutChangingSelection();
    void compactIconsLeaveARealCloseTarget();
    void newTabControlsCreateTabsAndManageTheirMenu();
    void invalidPresentationSettingsLeaveTheLayoutUnchanged();
    void lastClosePublishesTheFinalGeometryBeforeRemoval();
    void departingFramesSurviveTheirSourceStrip_data();
    void departingFramesSurviveTheirSourceStrip();
    void singleTabDefaultsFollowWindowKindAndContentCount_data();
    void singleTabDefaultsFollowWindowKindAndContentCount();
    void singleTabPoliciesNeverOverrideExplicitHidingOrEdgeHeaders();
    void singleTabVisibilityFollowsPromotionAndRestore();
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
    QList<TabBar*> headers;
    for (auto* bar : window->findChildren<TabBar*>()) if (dynamic_cast<GroupTabs*>(bar->parentWidget())) headers.append(bar);
    QCOMPARE(headers.size(), 3);
    QVERIFY(manager.execute("window.tabBars.toggle", {}, target));
    QVERIFY(!manager.tabBarsVisible(target));
    QCOMPARE(surface->tabBarRects().size(), 1); QCOMPARE(surface->edgeBarRects().size(), 1);
    for (auto* header : headers) {
        const auto groupId = header->parentWidget()->property("groupId").toUuid();
        const auto* record = manager.model().group(groupId); QVERIFY(record);
        auto* stack = header->parentWidget()->findChild<QStackedWidget*>(); QVERIFY(stack);
        // Without its strip the content starts at its card.
        if (record->location == GroupLocation::Central) {
            QVERIFY(header->isHidden()); QVERIFY(stack->geometry().top() <= header->metrics().cardMargin);
        } else {
            QVERIFY(header->isVisible()); QVERIFY(stack->geometry().top() > header->geometry().bottom());
        }
    }
    QCOMPARE(manager.model().toJson(), model);
    QCOMPARE(manager.paneWidget(manager.model().tab(first)->activePane), firstContent);
    QVERIFY(manager.activateTab(hidden));
    auto* peek = window->findChild<QWidget*>("PeekTitleBar"); QVERIFY(peek); QVERIFY(peek->isVisible());
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
    const auto pinned = manager.addTab(root, new QLabel("pinned"), {"pinned", "label", {}});
    QVERIFY(manager.moveTabToEdge(pinned, Edge::Right, true));
    const auto hidden = manager.addTab(root, new QLabel("hidden"), {"hidden", "label", {}});
    QVERIFY(manager.moveTabToEdge(hidden, Edge::Top));
    WindowManager restored;
    restored.setContentFactory([](const TabOptions&, PaneId) { return new QLabel("Restored"); });
    QVERIFY(restored.restoreLayout(manager.saveLayout()));
    QVERIFY(!restored.tabBarsVisible(root));
    auto* window = static_cast<WorkspaceWindow*>(restored.windowWidget(root));
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QCOMPARE(window->surface()->tabBarRects().size(), 1);
    QCOMPARE(window->surface()->edgeBarRects().size(), 1);
    QVERIFY(restored.activateTab(hidden));
    QVERIFY(window->findChild<QWidget*>("PeekTitleBar")->isVisible());
    QVERIFY(restored.setTabBarsVisible(root, true));
    QCOMPARE(window->surface()->tabBarRects().size(), 3);
    auto* toggle = window->findChild<QAction*>("window.tabBars.toggle"); QVERIFY(toggle);
    auto* menu = qobject_cast<QMenu*>(toggle->parent()); QVERIFY(menu);
    QMetaObject::invokeMethod(menu, "aboutToShow");
    QVERIFY(toggle->isCheckable()); QVERIFY(toggle->isChecked());
}

void TabBarPresentationTest::scrollbarDraggingDoesNotSelectTabs()
{
    QWidget root;
    auto* column = new QVBoxLayout(&root);
    auto* tabs = new GroupTabs(defaultTheme(), &root); column->addWidget(tabs);
    for (int index = 0; index < 12; ++index)
        tabs->addTab(new QLabel("Content"), {}, QString("Long tab %1").arg(index), QUuid::createUuid());
    applyTheme(&root); root.resize(380, 240); root.show();
    QVERIFY(QTest::qWaitForWindowExposed(&root));
    auto* bar = tabs->tabBar();
    QTRY_VERIFY(bar->isOverflowing());
    // The current strip policy reveals the target on scrolling, not hover.
    bar->setScrollOffset(std::min(40, bar->maximumScrollOffset()));
    QWidget* input = nullptr;
    QTRY_VERIFY((input = root.findChild<QWidget*>("TabScrollbarInput")) && input->isVisible());
    const auto point = input->rect().center();
    const auto selected = tabs->currentIndex();
    QTest::mousePress(input, Qt::LeftButton, Qt::NoModifier, point);
    QTest::mouseMove(input, point + QPoint(60, 0));
    QTest::mouseRelease(input, Qt::LeftButton, Qt::NoModifier, point + QPoint(60, 0));
    QTRY_VERIFY(tabs->scrollOffset() > 0);
    QCOMPARE(tabs->currentIndex(), selected);
    tabs->setTabBarVisible(false); QVERIFY(tabs->viewportRectGlobal().isEmpty());
}

void TabBarPresentationTest::tabListMarksTheActiveTabWithoutChangingSelection()
{
    GroupTabs tabs(defaultTheme(), nullptr);
    for (int i = 0; i < 3; ++i)
        tabs.addTab(new QLabel("Content"), {}, QStringLiteral("Entry %1").arg(i), QUuid::createUuid());
    TabRecord record; record.id = tabs.tabBar()->tabData(1).toUuid(); record.colorId = "red";
    tabs.setTabMetadata(1, record);
    tabs.setCurrentIndex(1); applyTheme(&tabs); tabs.resize(500, 250); tabs.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    QTest::mouseClick(tabs.findChild<QToolButton*>("TabListButton"), Qt::LeftButton);
    auto* menu = tabs.findChild<QMenu*>(); QVERIFY(menu && menu->isVisible());
    QCOMPARE(menu->actions().size(), 3);
    menu->setActiveAction(nullptr);
    for (int i = 0; i < menu->actions().size(); ++i) {
        auto* action = qobject_cast<QWidgetAction*>(menu->actions()[i]); QVERIFY(action);
        auto* row = action->defaultWidget(); QVERIFY(row);
        QCOMPARE(row->property("activeTab").toBool(), i == 1);
        QVERIFY(action->isCheckable()); QCOMPARE(action->isChecked(), i == 1);
        auto* close = row->findChild<QToolButton*>("TabListCloseButton"); QVERIFY(close);
        QVERIFY(close->isHidden());
    }
    menu->setActiveAction(menu->actions().first());
    auto* hovered = qobject_cast<QWidgetAction*>(menu->actions().first())->defaultWidget();
    auto* active = qobject_cast<QWidgetAction*>(menu->actions()[1])->defaultWidget();
    QTRY_VERIFY(hovered->property("emphasized").toBool());
    QTRY_VERIFY(active->property("emphasized").toBool());
    QCOMPARE(tabs.currentIndex(), 1); menu->close();
}

void TabBarPresentationTest::compactIconsLeaveARealCloseTarget()
{
    GroupTabs tabs(defaultTheme(), nullptr);
    QPixmap icon(16, 16); icon.fill(QColor("#ff0000"));
    tabs.addTab(new QLabel("Content"), QIcon(icon), "MMMM", QUuid::createUuid());
    applyTheme(&tabs); tabs.resize(380, 240); tabs.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* bar = tabs.tabBar();
    QSignalSpy changes(bar, &TabBar::currentChanged);
    int closed = 0; tabs.closeRequested = [&](int index) { QCOMPARE(index, 0); ++closed; };
    QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, bar->actionRect(0).center());
    QCOMPARE(closed, 1); QCOMPARE(changes.count(), 0);
}

void TabBarPresentationTest::newTabControlsCreateTabsAndManageTheirMenu()
{
    GroupTabs tabs(defaultTheme(), nullptr);
    tabs.addTab(new QLabel("Content"), {}, "One", QUuid::createUuid());
    tabs.addTab(new QLabel("Content"), {}, "Two", QUuid::createUuid());
    applyTheme(&tabs); tabs.resize(700, 250); tabs.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* add = tabs.findChild<QToolButton*>("NewTabButton"); QVERIFY(add);
    auto* types = tabs.findChild<QToolButton*>("NewTabTypeButton"); QVERIFY(types);
    auto* controls = tabs.findChild<QWidget*>("TabCreateButtons"); QVERIFY(controls);
    int created = 0; tabs.newTabRequested = [&] { ++created; };
    QTest::mouseClick(add, Qt::LeftButton); QCOMPARE(created, 1);
    // Hovering either button emphasizes both.
    const QPoint point(3, 3);
    QEnterEvent enter(point, add->mapTo(add->window(), point), add->mapToGlobal(point));
    QApplication::sendEvent(add, &enter);
    QVERIFY(controls->property("emphasized").toBool());
    QPointer<QMenu> popup;
    tabs.newTabMenuRequested = [&](QWidget* parent) {
        popup = new DismissOnlyMenu(parent); popup->addAction("New workspace"); return popup.data();
    };
    QTest::mouseClick(types, Qt::LeftButton);
    QVERIFY(popup); QTRY_VERIFY(popup->isVisible());
    QVERIFY(controls->property("popupOpen").toBool()); QVERIFY(types->isDown());
    popup->close(); QVERIFY(!controls->property("popupOpen").toBool()); QVERIFY(!types->isDown());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QVERIFY(popup.isNull());
    // A drag carrier shows its tabs alone.
    tabs.setDragPresentation(true, false); QVERIFY(!controls->isVisible());
    tabs.setDragPresentation(false, false); QVERIFY(controls->isVisible());
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

void TabBarPresentationTest::departingFramesSurviveTheirSourceStrip_data()
{
    QTest::addColumn<TabBarPlacement>("placement");
    for (const auto side : {TabBarPlacement::Top, TabBarPlacement::Bottom, TabBarPlacement::Left, TabBarPlacement::Right})
        QTest::newRow(qPrintable(QString::number(int(side)))) << side;
}

void TabBarPresentationTest::departingFramesSurviveTheirSourceStrip()
{
    QFETCH(TabBarPlacement, placement);
    const bool animations = TabBar::animationsAllowed(); TabBar::setAnimationsEnabled(true);
    const auto restore = qScopeGuard([=] { TabBar::setAnimationsEnabled(animations); });
    if (!TabBar::animationsAllowed()) QSKIP("System client-area animations are disabled");
    auto bar = std::make_unique<TabBar>(defaultTheme(), nullptr);
    bar->setAppearance(TabAppearance{}, placement);
    bar->resize(bar->vertical() ? QSize(220, 300) : QSize(700, 50)); bar->setAvailableSize(bar->size());
    const auto removed = QUuid::createUuid();
    for (int index = 0; index < 2; ++index) {
        bar->addTab({}, index == 0 ? QStringLiteral("Removed label") : QStringLiteral("Remaining label"));
        bar->setTabData(index, index == 0 ? removed : QUuid::createUuid());
    }
    bar->setCurrentIndex(0); bar->show(); QVERIFY(QTest::qWaitForWindowExposed(bar.get()));
    auto previous = bar->captureVisualState(); QVERIFY(previous.value(removed).retired);
    bar->removeTab(0); bar->transitionFrom(std::move(previous));
    const auto frame = bar->captureStripFrame(bar->font(), bar->layoutDirection());
    QCOMPARE(frame->departures.size(), 1); QCOMPARE(frame->departures.first().id, removed);
    const auto& label = frame->departures.first().label;
    QCOMPARE(label.state.title, QStringLiteral("Removed label"));
    QVERIFY(label.surface); QVERIFY(!label.surface->joined); QVERIFY(label.surface->body.isEmpty());
    QVERIFY(!label.state.elevationManaged); QCOMPARE(label.presence.opacity, qreal(1));
    QTRY_VERIFY_WITH_TIMEOUT(bar->captureStripFrame(bar->font(), bar->layoutDirection())->departures.isEmpty(), 1000);
    bar.reset();
    QCOMPARE(label.state.title, QStringLiteral("Removed label")); QVERIFY(!label.parts.title.isEmpty());
}

void TabBarPresentationTest::singleTabDefaultsFollowWindowKindAndContentCount_data()
{
    QTest::addColumn<bool>("floating");
    QTest::newRow("workbench") << false; QTest::newRow("floating") << true;
}

void TabBarPresentationTest::singleTabDefaultsFollowWindowKindAndContentCount()
{
    QFETCH(bool, floating);
    WindowManager manager; const auto root = manager.createWindow();
    const auto windowId = floating ? manager.createWindow(WindowKind::Floating, {}, root) : root;
    auto* content = new QPlainTextEdit("Preserved editor");
    const auto first = manager.addTab(windowId, content, {"First", "editor", {}});
    const auto pane = manager.model().tab(first)->activePane;
    manager.showWindow(windowId); auto* window = static_cast<WorkspaceWindow*>(manager.windowWidget(windowId));
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QVERIFY(!manager.behavior().hideSingleTabWorkbench); QVERIFY(manager.behavior().hideSingleTabFloating);
    auto* surface = window->surface(); QTRY_COMPARE(surface->tabBarRects().size(), floating ? 0 : 1);
    QVERIFY(manager.tabBarsVisible(windowId)); QVERIFY(manager.preferredBarVisibility(windowId).tabBars);
    auto* header = window->findChild<TabBar*>(); QVERIFY(header);
    auto* stack = header->parentWidget()->findChild<QStackedWidget*>(); QVERIFY(stack);
    if (floating) { QVERIFY(header->isHidden()); QVERIFY(stack->geometry().top() <= header->metrics().cardMargin); }
    else { QVERIFY(header->isVisible()); QVERIFY(stack->geometry().top() > header->geometry().bottom()); }
    const auto second = manager.addTab(windowId, new QLabel("Second"), {"Second", "label", {}});
    QTRY_COMPARE(surface->tabBarRects().size(), 1);
    QVERIFY(manager.moveTab(second, manager.model().groupForTab(first), Placement::Right));
    QTRY_COMPARE(surface->tabBarRects().size(), 2);
    QVERIFY(manager.closeTab(second));
    QTRY_COMPARE(surface->tabBarRects().size(), floating ? 0 : 1);
    QCOMPARE(manager.paneWidget(pane), content); QCOMPARE(content->toPlainText(), QStringLiteral("Preserved editor"));
    QVERIFY(content->isVisible()); QCOMPARE(manager.model().orderedTabs(windowId), (QVector<TabId>{first}));
}

void TabBarPresentationTest::singleTabPoliciesNeverOverrideExplicitHidingOrEdgeHeaders()
{
    WindowManager manager; const auto root = manager.createWindow();
    const auto windowId = manager.createWindow(WindowKind::Floating, {}, root);
    auto* content = new QLabel("Central"); const auto central = manager.addTab(windowId, content, {"Central", "label", {}});
    manager.showWindow(windowId); auto* window = static_cast<WorkspaceWindow*>(manager.windowWidget(windowId));
    QVERIFY(QTest::qWaitForWindowExposed(window)); auto* surface = window->surface();
    QCOMPARE(surface->tabBarRects().size(), 0);
    const auto hidden = manager.addTab(windowId, new QLabel("Hidden"), {"Hidden", "label", {}});
    QVERIFY(manager.moveTabToEdge(hidden, Edge::Left));
    // Count the whole window, including an auto-hidden tab. A central group
    // with one visible tab is not the same as a window containing one tab.
    QTRY_COMPARE(surface->tabBarRects().size(), 1); QCOMPARE(surface->edgeBarRects().size(), 1);
    QVERIFY(manager.setTabBarsVisible(windowId, false)); QCOMPARE(surface->tabBarRects().size(), 0);
    auto behavior = manager.behavior(); behavior.hideSingleTabFloating = false; manager.setBehavior(behavior);
    QCOMPARE(surface->tabBarRects().size(), 0); QCOMPARE(surface->edgeBarRects().size(), 1);
    QVERIFY(manager.setTabBarsVisible(windowId, true)); QCOMPARE(surface->tabBarRects().size(), 1);
    QVERIFY(manager.closeTab(hidden)); QCOMPARE(surface->tabBarRects().size(), 1);
    behavior.hideSingleTabFloating = true; manager.setBehavior(behavior); QCOMPARE(surface->tabBarRects().size(), 0);
    QVERIFY(manager.moveTabToEdge(central, Edge::Right, true));
    QTRY_COMPARE(surface->tabBarRects().size(), 1); QVERIFY(surface->edgeBarRects().isEmpty());
    QVERIFY(manager.setTabBarsVisible(windowId, false)); QCOMPARE(surface->tabBarRects().size(), 1);
    QVERIFY(manager.pinTab(central, false));
    QTRY_COMPARE(surface->edgeBarRects().size(), 1); QVERIFY(surface->tabBarRects().isEmpty());
    QVERIFY(manager.activateTab(central));
    auto* title = window->findChild<QWidget*>("PeekTitleBar"); QVERIFY(title); QVERIFY(title->isVisible());
    QCOMPARE(manager.paneWidget(manager.model().tab(central)->activePane), content); QVERIFY(content->isVisible());
}

void TabBarPresentationTest::singleTabVisibilityFollowsPromotionAndRestore()
{
    WindowManager manager; const auto root = manager.createWindow();
    const auto floating = manager.createWindow(WindowKind::Floating, {}, root);
    auto* content = new QLabel("Carried"); const auto tab = manager.addTab(floating, content, {"Carried", "label", {}});
    const auto pane = manager.model().tab(tab)->activePane;
    manager.showWindow(floating); auto* window = static_cast<WorkspaceWindow*>(manager.windowWidget(floating));
    QVERIFY(QTest::qWaitForWindowExposed(window)); QCOMPARE(window->surface()->tabBarRects().size(), 0);
    QVERIFY(manager.promoteWindow(floating));
    QTRY_COMPARE(window->surface()->tabBarRects().size(), 1); QCOMPARE(manager.model().window(floating)->kind, WindowKind::Workbench);
    QCOMPARE(manager.paneWidget(pane), content);
    QVERIFY(manager.demoteWindow(floating, root));
    QTRY_COMPARE(window->surface()->tabBarRects().size(), 0); QCOMPARE(manager.model().window(floating)->kind, WindowKind::Floating);
    QCOMPARE(manager.paneWidget(pane), content);
    manager.addTab(root, new QLabel("Root"), {"Root", "label", {}});
    auto behavior = manager.behavior(); behavior.hideSingleTabWorkbench = true; behavior.hideSingleTabFloating = false;
    manager.setBehavior(behavior);
    QCOMPARE(static_cast<WorkspaceWindow*>(manager.windowWidget(root))->surface()->tabBarRects().size(), 0);
    QCOMPARE(window->surface()->tabBarRects().size(), 1);
    const auto saved = manager.saveLayout();
    WindowManager restored; restored.setContentFactory([](const TabOptions& options, PaneId) { return new QLabel(options.title); });
    QVERIFY(restored.restoreLayout(saved));
    QVERIFY(restored.behavior().hideSingleTabWorkbench); QVERIFY(!restored.behavior().hideSingleTabFloating);
    QCOMPARE(static_cast<WorkspaceWindow*>(restored.windowWidget(root))->surface()->tabBarRects().size(), 0);
    QCOMPARE(static_cast<WorkspaceWindow*>(restored.windowWidget(floating))->surface()->tabBarRects().size(), 1);
}

QTEST_MAIN(TabBarPresentationTest)
#include "tab_bar_presentation_test.moc"
