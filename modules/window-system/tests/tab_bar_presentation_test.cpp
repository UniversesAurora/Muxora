// SPDX-License-Identifier: MIT
#include "chrome/workspace_window.hpp"
#include "docking/dock_surface.hpp"
#include "docking/tab_strip.hpp"
#include "docking/edge_strip.hpp"
#include "commands/popup_menu.hpp"
#include <window_system/window_manager.hpp>
#include <QApplication>
#include <QLabel>
#include <QEnterEvent>
#include <QJsonArray>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QStyleOptionSlider>
#include <QTest>
#include <QToolButton>
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
    void newTabControlsFollowTheLastTabAndShareEmphasis();
    void rectangularControlsAndContentUseUniformEdges();
    void emptyStripsFollowTheToolbarBackgroundAfterThemeChanges();
    void invalidPresentationSettingsLeaveTheLayoutUnchanged();
    void lastClosePublishesTheFinalGeometryBeforeRemoval();
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
    const auto headers = window->findChildren<QWidget*>("TabGroupHeader");
    QCOMPARE(headers.size(), 3);
    QVERIFY(manager.execute("window.tabBars.toggle", {}, target));
    QVERIFY(!manager.tabBarsVisible(target));
    QCOMPARE(surface->tabBarRects().size(), 1); QCOMPARE(surface->edgeBarRects().size(), 1);
    for (auto* header : headers) {
        const auto groupId = header->parentWidget()->property("groupId").toUuid();
        const auto* record = manager.model().group(groupId); QVERIFY(record);
        auto* stack = header->parentWidget()->findChild<QStackedWidget*>(); QVERIFY(stack);
        if (record->location == GroupLocation::Central) {
            QVERIFY(header->isHidden()); QCOMPARE(stack->geometry().top(), 0);
        } else {
            QVERIFY(header->isVisible()); QVERIFY(stack->geometry().top() >= header->height());
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

void TabBarPresentationTest::newTabControlsFollowTheLastTabAndShareEmphasis()
{
    GroupTabs tabs(defaultTheme(), nullptr);
    tabs.addTab(new QLabel("Content"), {}, "One", QUuid::createUuid());
    tabs.addTab(new QLabel("Content"), {}, "Two", QUuid::createUuid());
    applyTheme(&tabs); tabs.resize(700, 250); tabs.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* add = tabs.findChild<QToolButton*>("NewTabButton"); QVERIFY(add);
    auto* types = tabs.findChild<QToolButton*>("NewTabTypeButton"); QVERIFY(types);
    auto* controls = tabs.findChild<QWidget*>("TabCreateButtons"); QVERIFY(controls);
    auto* list = tabs.findChild<QToolButton*>("TabListButton"); QVERIFY(list);
    auto* scroll = tabs.findChild<QScrollArea*>("TabScrollViewport"); QVERIFY(scroll);
    QCOMPARE(controls->x(), tabs.tabBar()->width());
    QCOMPARE(add->size(), QSize(26, tabs.tabBar()->height()));
    QCOMPARE(types->size(), QSize(12, tabs.tabBar()->height()));
    QCOMPARE(list->size(), QSize(12, tabs.tabBar()->height()));
    QCOMPARE(types->x(), add->width() + 1);
    QCOMPARE(scroll->widget()->width(), tabs.tabBar()->width() + controls->width());
    int created = 0; tabs.newTabRequested = [&] { ++created; };
    QTest::mouseClick(add, Qt::LeftButton); QCOMPARE(created, 1);
    const auto pixel = [](QWidget* widget, QPoint point) {
        const auto image = widget->grab().toImage(); const auto ratio = image.devicePixelRatio();
        return image.pixelColor(qRound(point.x() * ratio), qRound(point.y() * ratio)).rgb();
    };
    const QPoint point(3, 3);
    QEnterEvent enter(point, add->mapTo(add->window(), point), add->mapToGlobal(point));
    QApplication::sendEvent(add, &enter);
    QCOMPARE(pixel(add, point), defaultTheme().accentSoft.rgb());
    QCOMPARE(pixel(types, point), defaultTheme().accentSoft.rgb());
    QPointer<QMenu> popup;
    tabs.newTabMenuRequested = [&](QWidget* parent) {
        popup = new DismissOnlyMenu(parent); popup->addAction("New workspace"); return popup.data();
    };
    QTest::mouseClick(types, Qt::LeftButton);
    QVERIFY(popup); QTRY_VERIFY(popup->isVisible());
    QVERIFY(controls->property("popupOpen").toBool()); QVERIFY(types->isDown());
    QCOMPARE(pixel(add, point), defaultTheme().headerBackground.rgb());
    QCOMPARE(pixel(types, point), defaultTheme().selection.rgb());
    popup->close(); QVERIFY(!controls->property("popupOpen").toBool()); QVERIFY(!types->isDown());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QVERIFY(popup.isNull());
    for (int index = 0; index < 10; ++index)
        tabs.addTab(new QLabel("Content"), {}, QString("Long tab %1").arg(index), QUuid::createUuid());
    QTRY_VERIFY(scroll->horizontalScrollBar()->maximum() > 0);
    scroll->horizontalScrollBar()->setValue(scroll->horizontalScrollBar()->maximum());
    QCoreApplication::processEvents();
    const auto controlBounds = QRect(controls->mapTo(scroll->viewport(), QPoint()), controls->size());
    QVERIFY(scroll->viewport()->rect().contains(controlBounds));
    QCOMPARE(controls->x(), tabs.tabBar()->width());
    tabs.setDragPresentation(true, false); QVERIFY(controls->isHidden());
    QCOMPARE(scroll->widget()->width(), tabs.tabBar()->width());
    tabs.setDragPresentation(false, false); QVERIFY(!controls->isHidden());
    QCOMPARE(scroll->widget()->width(), tabs.tabBar()->width() + controls->width());
}

void TabBarPresentationTest::rectangularControlsAndContentUseUniformEdges()
{
    WindowManager manager; const auto id = manager.createWindow();
    auto* editor = new QPlainTextEdit;
    const auto tab = manager.addTab(id, editor, {"One", "editor", {}}); QVERIFY(!tab.isNull());
    manager.showWindow(id); auto* window = manager.windowWidget(id);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    editor->clearFocus(); QVERIFY(!editor->hasFocus());
    auto* pane = editor->parentWidget(); QCOMPARE(pane->objectName(), QStringLiteral("PaneHost"));
    QCOMPARE(editor->geometry(), pane->rect());
    const auto image = editor->grab().toImage(); const auto ratio = image.devicePixelRatio();
    const auto colorAt = [&](QPoint point) { return image.pixelColor(qRound(point.x() * ratio), qRound(point.y() * ratio)).rgb(); };
    const auto area = editor->rect();
    for (const QPoint point : {area.topLeft(), area.topRight(), area.bottomLeft(), area.bottomRight(),
                              QPoint(area.center().x(), area.top()), QPoint(area.center().x(), area.bottom()),
                              QPoint(area.left(), area.center().y()), QPoint(area.right(), area.center().y())})
        QCOMPARE(colorAt(point), manager.theme().border.rgb());
    QCOMPARE(colorAt(area.center()), QColor(Qt::white).rgb());
    auto* group = dynamic_cast<GroupTabs*>(window->findChild<QWidget*>("TabGroup")); QVERIFY(group);
    auto* bar = group->tabBar();
    const auto tabImage = bar->grab().toImage(); const auto tabRatio = tabImage.devicePixelRatio();
    const auto tabArea = bar->tabRect(0);
    for (const auto point : {QPoint(tabArea.left(), tabArea.top() + 2), QPoint(tabArea.right(), tabArea.top() + 2)})
        QCOMPARE(tabImage.pixelColor(qRound(point.x() * tabRatio), qRound(point.y() * tabRatio)).rgb(),
                 manager.theme().activeTab.rgb());
    QWidget root; auto* layout = new QVBoxLayout(&root); auto* button = new QPushButton("Action", &root);
    layout->addWidget(button); applyTheme(&root); root.resize(150, 80); root.show();
    QVERIFY(QTest::qWaitForWindowExposed(&root));
    button->clearFocus(); QVERIFY(!button->hasFocus());
    QTest::mouseMove(button, QPoint(4, 4));
    QEnterEvent hover(QPointF(4, 4), button->mapTo(button->window(), QPoint(4, 4)), button->mapToGlobal(QPoint(4, 4)));
    QApplication::sendEvent(button, &hover); QVERIFY(button->underMouse());
    // Qt 6.12 QPushButton updates its private hit state on MouseMove after
    // Enter. Enter alone sets underMouse but does not establish button hover.
    QMouseEvent move(QEvent::MouseMove, QPointF(4, 4), button->mapToGlobal(QPoint(4, 4)),
        Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(button, &move);
    const auto buttonImage = button->grab().toImage(); const auto buttonRatio = buttonImage.devicePixelRatio();
    const auto buttonArea = button->rect();
    for (const auto point : {buttonArea.topLeft(), buttonArea.topRight(), buttonArea.bottomLeft(), buttonArea.bottomRight()})
        QCOMPARE(buttonImage.pixelColor(qRound(point.x() * buttonRatio), qRound(point.y() * buttonRatio)).rgb(),
                 defaultTheme().accent.rgb());
    QCOMPARE(buttonImage.pixelColor(qRound(4 * buttonRatio), qRound(4 * buttonRatio)).rgb(), defaultTheme().buttonHover.rgb());
}

void TabBarPresentationTest::emptyStripsFollowTheToolbarBackgroundAfterThemeChanges()
{
    QWidget root; auto* column = new QVBoxLayout(&root);
    auto* tabs = new GroupTabs(defaultTheme(), &root); column->addWidget(tabs);
    tabs->addTab(new QLabel("Content"), {}, "One", QUuid::createUuid());
    QList<EdgeStrip*> strips;
    for (const auto edge : {Edge::Left, Edge::Right, Edge::Top, Edge::Bottom}) {
        auto* strip = new EdgeStrip(edge, defaultTheme(), &root);
        if (edge == Edge::Left || edge == Edge::Right) strip->setFixedHeight(100);
        column->addWidget(strip); strips.append(strip);
    }
    root.resize(750, 580); applyTheme(&root); root.show();
    QVERIFY(QTest::qWaitForWindowExposed(&root));
    auto* canvas = tabs->findChild<QScrollArea*>("TabScrollViewport")->viewport();
    const auto pixel = [](QWidget* widget) {
        const auto image = widget->grab().toImage(); const auto ratio = image.devicePixelRatio();
        const auto point = widget->rect().bottomRight() - QPoint(6, 6);
        return image.pixelColor(qRound(point.x() * ratio), qRound(point.y() * ratio)).rgb();
    };
    auto theme = defaultTheme();
    for (const auto background : {theme.headerBackground, QColor("#e7caa5")}) {
        theme.headerBackground = background; applyTheme(&root, theme); QCoreApplication::processEvents();
        QCOMPARE(pixel(canvas), background.rgb());
        for (auto* strip : strips) {
            auto* track = strip->findChild<QWidget*>("EdgeTabTrack"); QVERIFY(track);
            QCOMPARE(pixel(track), background.rgb());
            QCOMPARE(pixel(strip), background.rgb());
        }
    }
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
    auto* header = window->findChild<QWidget*>("TabGroupHeader"); QVERIFY(header);
    auto* stack = header->parentWidget()->findChild<QStackedWidget*>(); QVERIFY(stack);
    if (floating) { QVERIFY(header->isHidden()); QCOMPARE(stack->geometry().top(), 0); }
    else { QVERIFY(header->isVisible()); QCOMPARE(stack->geometry().top(), header->height()); }
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
