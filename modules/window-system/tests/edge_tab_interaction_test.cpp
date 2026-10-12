#include <window_system/window_manager.hpp>
#include "docking/tab_strip.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QEnterEvent>
#include <QLabel>
#include <QMenu>
#include <QScrollBar>
#include <QStyle>
#include <QTest>
#include <QToolButton>
#include <QWheelEvent>

using namespace window_system;

namespace {
QAbstractButton* edgeButton(QWidget* window, TabId id) {
    for (auto* button : window->findChildren<QAbstractButton*>())
        if (button->isVisible() && button->objectName() == "EdgeButton" && button->property("tabId").toUuid() == id) return button;
    return nullptr;
}
QWidget* content(const QString& name) { auto* widget = new QLabel(name); widget->setObjectName(name); return widget; }
TabOptions options(const QString& title) { TabOptions result; result.title = title; result.contentType = "test"; return result; }
void showTestWindow(QWidget* window) { window->setWindowFlag(Qt::WindowStaysOnTopHint); window->show(); }
}

class EdgeTabInteractionTest final : public QObject {
    Q_OBJECT
private slots:
    void peekShowsOnlyTheChosenTabAndPinsOnlyThatTab();
    void unpinRepinRestoresTheOriginalGroupAndContentPointers();
    void tabWheelScrollsWithoutChangingSelection();
    void tabListUsesHoverCloseAndIncludesEveryTab();
    void insertionIndexAccountsForTheScrolledViewport();
    void dragHoverScrollsAtViewportEdges();
    void tabIconVisibilityUsesTheHostApi();
    void pinnedExtentSurvivesRefreshAndUnpinRepin();
    void scrollbarRemainsVisibleWhenMovingFromTabsOntoTheHandle();
};

void EdgeTabInteractionTest::peekShowsOnlyTheChosenTabAndPinsOnlyThatTab()
{
    WindowManager manager;
    const auto windowId = manager.createWindow();
    auto* aContent = content("A content"); auto* bContent = content("B content");
    const auto a = manager.addTab(windowId, aContent, options("A"));
    const auto b = manager.addTab(windowId, bContent, options("B"));
    QVERIFY(manager.moveTabToEdge(a, Edge::Left));
    const auto hiddenGroup = manager.model().groupForTab(a);
    QVERIFY(manager.moveTab(b, hiddenGroup));
    auto* window = manager.windowWidget(windowId); showTestWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QVERIFY(manager.activateTab(a));
    auto* peek = window->findChild<QFrame*>("PeekPanel"); QVERIFY(peek);
    QVERIFY(peek->isAncestorOf(aContent)); QVERIFY(!peek->isAncestorOf(bContent));
    QVERIFY(!bContent->isVisible());
    QCOMPARE(peek->findChildren<QTabBar*>().size(), 0);
    for (auto* label : peek->findChildren<QLabel*>()) QVERIFY(label->text() != QStringLiteral("临时展开"));
    auto* pin = peek->findChild<QToolButton*>("PinPeekTabButton"); QVERIFY(pin);
    pin->click();
    QCOMPARE(manager.model().group(manager.model().groupForTab(a))->location, GroupLocation::Pinned);
    QCOMPARE(manager.model().group(manager.model().groupForTab(b))->location, GroupLocation::AutoHide);
    QVERIFY(!edgeButton(window, a)); QVERIFY(edgeButton(window, b));
    QVERIFY(!window->findChild<QFrame*>("PeekPanel"));
    QVERIFY(manager.model().validate().ok);
}
void EdgeTabInteractionTest::unpinRepinRestoresTheOriginalGroupAndContentPointers()
{
    WindowManager manager;
    const auto window = manager.createWindow();
    auto* aContent = content("A content"); auto* bContent = content("B content");
    const auto a = manager.addTab(window, aContent, options("A"));
    const auto b = manager.addTab(window, bContent, options("B"));
    QVERIFY(manager.moveTabToEdge(a, Edge::Right, true));
    const auto group = manager.model().groupForTab(a);
    QVERIFY(manager.moveTab(b, group));
    const auto aPane = manager.model().tab(a)->activePane;
    const auto bPane = manager.model().tab(b)->activePane;
    QVERIFY(manager.pinTab(a, false));
    QCOMPARE(manager.model().group(group)->tabs, (QVector<TabId>{b}));
    QVERIFY(manager.pinEdge(window, Edge::Right, false));
    QVERIFY(manager.pinTab(b, true)); QVERIFY(manager.pinTab(a, true));
    QCOMPARE(manager.model().group(group)->tabs, (QVector<TabId>{a, b}));
    QCOMPARE(manager.paneWidget(aPane), aContent); QCOMPARE(manager.paneWidget(bPane), bContent);
    QVERIFY(manager.model().validate().ok);
}
void EdgeTabInteractionTest::tabWheelScrollsWithoutChangingSelection()
{
    GroupTabs tabs(Theme{}, nullptr); tabs.resize(420, 240);
    for (int i = 0; i < 18; ++i) tabs.addTab(content(QString::number(i)), {}, QStringLiteral("Session number %1").arg(i), QUuid::createUuid());
    showTestWindow(&tabs); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* scrollbar = tabs.findChild<QScrollBar*>("TabScrollBar"); QVERIFY(scrollbar); QTRY_VERIFY(scrollbar->maximum() > 0);
    const auto active = tabs.currentIndex();
    QWheelEvent wheel(QPointF(20, 12), tabs.tabBar()->mapToGlobal(QPoint(20, 12)), {}, QPoint(0, -120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(tabs.tabBar(), &wheel);
    QVERIFY(scrollbar->value() > 0); QCOMPARE(tabs.currentIndex(), active);
    for (auto* button : tabs.tabBar()->findChildren<QToolButton*>()) QVERIFY(!button->isVisible());
}
void EdgeTabInteractionTest::tabListUsesHoverCloseAndIncludesEveryTab()
{
    GroupTabs tabs(Theme{}, nullptr); tabs.resize(420, 240);
    QVector<TabId> ids;
    for (int i = 0; i < 18; ++i) {
        ids << QUuid::createUuid(); tabs.addTab(content(QString::number(i)), {}, QStringLiteral("Session %1").arg(i), ids.last());
    }
    showTestWindow(&tabs); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    int closed = -1; tabs.closeRequested = [&](int index) { closed = index; };
    auto* list = tabs.findChild<QToolButton*>("TabListButton"); QVERIFY(list); list->click();
    auto* menu = tabs.findChild<QMenu*>(); QVERIFY(menu); QTRY_VERIFY(menu->isVisible());
    QVERIFY(menu->style()->styleHint(QStyle::SH_Menu_Scrollable, nullptr, menu));
    QCOMPARE(menu->actions().size(), ids.size());
    const auto rows = menu->findChildren<QWidget*>("TabListRow"); QCOMPARE(rows.size(), ids.size());
    auto* row = rows.first(); QCOMPARE(row->property("tabId").toUuid(), ids.first());
    auto* close = row->findChild<QToolButton*>(); QVERIFY(close);
    QEvent leave(QEvent::Leave); QApplication::sendEvent(row, &leave); QVERIFY(close->isHidden());
    QEnterEvent enter(QPointF(8, 8), QPointF(8, 8), row->mapToGlobal(QPoint(8, 8)));
    QApplication::sendEvent(row, &enter); QVERIFY(!close->isHidden());
    close->click(); QCOMPARE(closed, 0); QVERIFY(!menu->isVisible());
}
void EdgeTabInteractionTest::insertionIndexAccountsForTheScrolledViewport()
{
    GroupTabs tabs(Theme{}, nullptr); tabs.resize(400, 240);
    for (int i = 0; i < 18; ++i) tabs.addTab(content(QString::number(i)), {}, QStringLiteral("Session number %1").arg(i), QUuid::createUuid());
    showTestWindow(&tabs); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* scrollbar = tabs.findChild<QScrollBar*>("TabScrollBar"); QVERIFY(scrollbar); QTRY_VERIFY(scrollbar->maximum() > 0);
    const auto ninth = tabs.tabBar()->tabRect(9); scrollbar->setValue(ninth.left());
    const auto ninthPosition = tabs.tabBar()->mapToGlobal(ninth.center() - QPoint(8, 0));
    QVERIFY(tabs.viewportRectGlobal().contains(ninthPosition)); QCOMPARE(tabs.insertionIndex(ninthPosition), 9);
    QCOMPARE(tabs.insertionIndex(tabs.tabBar()->mapToGlobal(ninth.center() + QPoint(8, 0))), 10);
}
void EdgeTabInteractionTest::dragHoverScrollsAtViewportEdges()
{
    GroupTabs tabs(Theme{}, nullptr); tabs.resize(400, 240);
    for (int i = 0; i < 18; ++i) tabs.addTab(content(QString::number(i)), {}, QStringLiteral("Session number %1").arg(i), QUuid::createUuid());
    showTestWindow(&tabs); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* scrollbar = tabs.findChild<QScrollBar*>("TabScrollBar"); QVERIFY(scrollbar); QTRY_VERIFY(scrollbar->maximum() > 0);
    const auto viewport = tabs.viewportRectGlobal(); tabs.setDragHover(QPoint(viewport.right() - 2, viewport.center().y()));
    QTRY_VERIFY(scrollbar->value() > 0);
    tabs.setDragHover({}); const auto position = scrollbar->value(); QTest::qWait(100); QCOMPARE(scrollbar->value(), position);
}
void EdgeTabInteractionTest::tabIconVisibilityUsesTheHostApi()
{
    WindowManager manager;
    const auto window = manager.createWindow();
    const auto tab = manager.addTab(window, content("Content"), options("Tab"));
    showTestWindow(manager.windowWidget(window)); QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    const auto barForTab = [&]() -> QTabBar* {
        for (auto* bar : manager.windowWidget(window)->findChildren<QTabBar*>())
            for (int i = 0; i < bar->count(); ++i) if (bar->isVisible() && bar->tabData(i).toUuid() == tab) return bar;
        return nullptr;
    };
    auto* bar = barForTab(); QVERIFY(bar); QVERIFY(!bar->tabIcon(0).isNull());
    QVERIFY(manager.setTabIconVisible(tab, false));
    bar = barForTab(); QVERIFY(bar); QVERIFY(bar->tabIcon(0).isNull());
    QVERIFY(!manager.tabOptions(tab).showIcon);
    QVERIFY(manager.setTabIconVisible(tab, true));
    bar = barForTab(); QVERIFY(bar); QVERIFY(!bar->tabIcon(0).isNull());
    QVERIFY(manager.tabOptions(tab).showIcon);
}
void EdgeTabInteractionTest::pinnedExtentSurvivesRefreshAndUnpinRepin()
{
    WindowManager manager;
    const auto window = manager.createWindow();
    const auto side = manager.addTab(window, content("Side"), options("Side"));
    const auto central = manager.addTab(window, content("Central"), options("Central"));
    QVERIFY(manager.moveTabToEdge(side, Edge::Left, true));
    showTestWindow(manager.windowWidget(window)); QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    const auto group = manager.model().groupForTab(side);
    auto groupWidget = [&]() -> QFrame* {
        for (auto* frame : manager.windowWidget(window)->findChildren<QFrame*>())
            if (frame->isVisible() && frame->property("groupId").toUuid() == group) return frame;
        return nullptr;
    };
    QVERIFY(manager.execute("layout.setEdgeExtent", {{"group", group}, {"extent", 347}}, window));
    QVERIFY(manager.pinTab(side, false)); QVERIFY(manager.pinTab(side, true));
    QTRY_VERIFY(groupWidget()); QTRY_COMPARE(groupWidget()->width(), 347);
    const auto centralGroup = manager.model().groupForTab(central);
    QVERIFY(!manager.addTab(window, content("Another"), options("Another"), centralGroup).isNull());
    QTRY_VERIFY(groupWidget()); QTRY_COMPARE(groupWidget()->width(), 347);
    manager.windowWidget(window)->resize(1440, 920);
    QTRY_COMPARE(groupWidget()->width(), 347);
    QVERIFY(manager.pinTab(side, false)); QVERIFY(manager.pinTab(side, true));
    QTRY_VERIFY(groupWidget()); QTRY_COMPARE(groupWidget()->width(), 347);
    QCOMPARE(manager.model().group(group)->edgeExtent, 347);
}
void EdgeTabInteractionTest::scrollbarRemainsVisibleWhenMovingFromTabsOntoTheHandle()
{
    GroupTabs tabs(Theme{}, nullptr); tabs.resize(420, 240);
    for (int i = 0; i < 18; ++i) tabs.addTab(content(QString::number(i)), {}, QStringLiteral("Session number %1").arg(i), QUuid::createUuid());
    showTestWindow(&tabs); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* scrollbar = tabs.findChild<QScrollBar*>("TabScrollBar"); QVERIFY(scrollbar); QTRY_VERIFY(scrollbar->maximum() > 0);
    QEnterEvent barEnter(QPointF(8, 8), QPointF(8, 8), tabs.tabBar()->mapToGlobal(QPoint(8, 8)));
    QApplication::sendEvent(tabs.tabBar(), &barEnter); QVERIFY(scrollbar->isVisible());
    QEvent barLeave(QEvent::Leave); QApplication::sendEvent(tabs.tabBar(), &barLeave);
    QEvent viewportLeave(QEvent::Leave); QApplication::sendEvent(tabs.tabBar()->parentWidget(), &viewportLeave);
    QEnterEvent handleEnter(QPointF(8, 2), QPointF(8, 2), scrollbar->mapToGlobal(QPoint(8, 2)));
    QApplication::sendEvent(scrollbar, &handleEnter); QApplication::processEvents();
    QVERIFY(scrollbar->isVisible());
    QEvent handleLeave(QEvent::Leave); QApplication::sendEvent(scrollbar, &handleLeave);
    QTRY_VERIFY(scrollbar->isHidden());
}

QTEST_MAIN(EdgeTabInteractionTest)
#include "edge_tab_interaction_test.moc"
