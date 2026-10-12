#include <window_system/window_manager.hpp>
#include "docking/dock_surface.hpp"
#include "docking/tab_strip.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QEnterEvent>
#include <QLabel>
#include <QLayout>
#include <QMenu>
#include <QPlainTextEdit>
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
    void peekShowsTheRealContentAndSharedBars_data();
    void peekShowsTheRealContentAndSharedBars();
    void edgeBarCornerOwnership_data();
    void edgeBarCornerOwnership();
    void allPinActionsHaveDistinctIconsAndFollowTheEdgeTabs();
    void unpinRepinRestoresTheOriginalGroupAndContentPointers();
    void tabWheelScrollsWithoutChangingSelection();
    void tabListUsesHoverCloseAndIncludesEveryTab();
    void insertionIndexAccountsForTheScrolledViewport();
    void dragHoverScrollsAtViewportEdges();
    void tabIconVisibilityUsesTheHostApi();
    void pinnedExtentSurvivesRefreshAndUnpinRepin();
    void scrollbarRemainsVisibleWhenMovingFromTabsOntoTheHandle();
    void sideBarInsertionUsesTheAppropriateAxis_data();
    void sideBarInsertionUsesTheAppropriateAxis();
    void sideBarReorderCommandsKeepLiveContent();
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
    QTRY_VERIFY(aContent->isVisible());
    QVERIFY(aContent->width() > 120); QVERIFY(aContent->height() > 80);
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
void EdgeTabInteractionTest::peekShowsTheRealContentAndSharedBars_data()
{
    QTest::addColumn<int>("edge");
    QTest::newRow("left") << int(Edge::Left);
    QTest::newRow("right") << int(Edge::Right);
    QTest::newRow("top") << int(Edge::Top);
    QTest::newRow("bottom") << int(Edge::Bottom);
}
void EdgeTabInteractionTest::peekShowsTheRealContentAndSharedBars()
{
    QFETCH(int, edge);
    WindowManager manager;
    const auto windowId = manager.createWindow();
    auto* document = new QPlainTextEdit;
    document->setPlainText(QStringLiteral("Persistent editor content"));
    const auto tab = manager.addTab(windowId, document, options("Editor"));
    const auto pane = manager.model().tab(tab)->activePane;
    auto* top = content("top shared bar"); auto* bottom = content("bottom shared bar");
    QVERIFY(manager.setPaneTopBar(pane, top)); QVERIFY(manager.setPaneBottomBar(pane, bottom));
    QVERIFY(manager.moveTabToEdge(tab, Edge(edge)));
    auto* window = manager.windowWidget(windowId); showTestWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QVERIFY(manager.activateTab(tab));
    auto* peek = window->findChild<QFrame*>("PeekPanel"); QVERIFY(peek);
    QTRY_VERIFY(document->isVisible()); QTRY_VERIFY(top->isVisible()); QTRY_VERIFY(bottom->isVisible());
    QVERIFY(document->width() > 120); QVERIFY(document->height() > 80);
    QVERIFY(peek->isAncestorOf(document)); QVERIFY(peek->isAncestorOf(top)); QVERIFY(peek->isAncestorOf(bottom));
    QCOMPARE(manager.paneWidget(pane), document);
    QTest::keyClicks(document, " kept");
    QVERIFY(document->toPlainText().contains(" kept"));
    auto* surface = dynamic_cast<DockSurface*>(window->findChild<QFrame*>("DockSurface")); QVERIFY(surface);
    surface->closePeek(); QVERIFY(!document->isVisible());
    QVERIFY(manager.activateTab(tab));
    QTRY_VERIFY(document->isVisible()); QVERIFY(top->isVisible()); QVERIFY(bottom->isVisible());
    QCOMPARE(manager.paneWidget(pane), document);
    QVERIFY(document->toPlainText().contains(" kept"));
    QVERIFY(manager.pinTab(tab, true));
    QTRY_VERIFY(document->isVisible()); QCOMPARE(manager.paneWidget(pane), document);
    QVERIFY(manager.model().validate().ok);
}
void EdgeTabInteractionTest::edgeBarCornerOwnership_data()
{
    QTest::addColumn<int>("visibleEdges");
    for (int mask = 0; mask < 16; ++mask)
        QTest::newRow(qPrintable(QStringLiteral("visible-edges-%1").arg(mask, 4, 2, QChar('0')))) << mask;
}
void EdgeTabInteractionTest::edgeBarCornerOwnership()
{
    QFETCH(int, visibleEdges);
    WindowManager manager;
    const auto windowId = manager.createWindow();
    manager.addTab(windowId, content("central"), options("Central"));
    const QVector<Edge> edges{Edge::Left, Edge::Right, Edge::Top, Edge::Bottom};
    for (int i = 0; i < edges.size(); ++i) if (visibleEdges & (1 << i)) {
        const auto tab = manager.addTab(windowId, content(QString::number(i)), options(QStringLiteral("Edge %1").arg(i)));
        QVERIFY(manager.moveTabToEdge(tab, edges[i]));
    }
    auto* window = manager.windowWidget(windowId); showTestWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto* surface = dynamic_cast<DockSurface*>(window->findChild<QFrame*>("DockSurface")); QVERIFY(surface);
    const auto bars = surface->edgeBarRects();
    const QRect area(surface->mapToGlobal(QPoint()), surface->size());
    for (int i = 0; i < edges.size(); ++i) QCOMPARE(bars.contains(edges[i]), bool(visibleEdges & (1 << i)));
    if (bars.contains(Edge::Left)) {
        QCOMPARE(bars[Edge::Left].top(), area.top());
        QCOMPARE(bars[Edge::Left].bottom(), bars.contains(Edge::Bottom) ? bars[Edge::Bottom].top() - 1 : area.bottom());
    }
    if (bars.contains(Edge::Right)) {
        QCOMPARE(bars[Edge::Right].top(), area.top());
        QCOMPARE(bars[Edge::Right].bottom(), bars.contains(Edge::Bottom) ? bars[Edge::Bottom].top() - 1 : area.bottom());
    }
    if (bars.contains(Edge::Top)) {
        QCOMPARE(bars[Edge::Top].left(), bars.contains(Edge::Left) ? bars[Edge::Left].right() + 1 : area.left());
        QCOMPARE(bars[Edge::Top].right(), bars.contains(Edge::Right) ? bars[Edge::Right].left() - 1 : area.right());
    }
    if (bars.contains(Edge::Bottom)) {
        QCOMPARE(bars[Edge::Bottom].left(), area.left()); QCOMPARE(bars[Edge::Bottom].right(), area.right());
    }
    for (int i = 0; i < edges.size(); ++i) for (int j = i + 1; j < edges.size(); ++j)
        if (bars.contains(edges[i]) && bars.contains(edges[j])) QVERIFY(!bars[edges[i]].intersects(bars[edges[j]]));
}
void EdgeTabInteractionTest::allPinActionsHaveDistinctIconsAndFollowTheEdgeTabs()
{
    WindowManager manager;
    const auto windowId = manager.createWindow();
    const auto a = manager.addTab(windowId, content("A"), options("A"));
    const auto b = manager.addTab(windowId, content("B"), options("B"));
    QVERIFY(manager.moveTabToEdge(a, Edge::Left));
    QVERIFY(manager.moveTab(b, manager.model().groupForTab(a)));
    auto* window = manager.windowWidget(windowId); showTestWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto* buttonA = edgeButton(window, a); auto* buttonB = edgeButton(window, b);
    QVERIFY(buttonA); QVERIFY(buttonB);
    auto* pinAll = buttonA->parentWidget()->findChild<QToolButton*>("PinEdgeButton"); QVERIFY(pinAll);
    auto* row = buttonA->parentWidget()->layout();
    QVERIFY(row->indexOf(pinAll) > row->indexOf(buttonA)); QVERIFY(row->indexOf(pinAll) > row->indexOf(buttonB));
    QVERIFY(manager.activateTab(a));
    auto* pinCurrent = window->findChild<QToolButton*>("PinPeekTabButton"); QVERIFY(pinCurrent);
    QVERIFY(pinCurrent->icon().pixmap(16, 16).toImage() != pinAll->icon().pixmap(16, 16).toImage());
    pinAll->click();
    QCOMPARE(manager.model().group(manager.model().groupForTab(a))->location, GroupLocation::Pinned);
    QCOMPARE(manager.model().group(manager.model().groupForTab(b))->location, GroupLocation::Pinned);
    QToolButton* unpinCurrent = nullptr; QToolButton* unpinAll = nullptr;
    for (auto* button : window->findChildren<QToolButton*>()) if (button->isVisible()) {
        if (button->toolTip() == QStringLiteral("取消固定当前标签")) unpinCurrent = button;
        if (button->toolTip() == QStringLiteral("取消固定这一侧全部标签")) unpinAll = button;
    }
    QVERIFY(unpinCurrent); QVERIFY(unpinAll);
    QVERIFY(unpinCurrent->icon().pixmap(16, 16).toImage() != unpinAll->icon().pixmap(16, 16).toImage());
    unpinAll->click();
    QVERIFY(edgeButton(window, a)); QVERIFY(edgeButton(window, b));
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

void EdgeTabInteractionTest::sideBarInsertionUsesTheAppropriateAxis_data()
{
    QTest::addColumn<int>("edge");
    QTest::newRow("left") << int(Edge::Left);
    QTest::newRow("right") << int(Edge::Right);
    QTest::newRow("top") << int(Edge::Top);
    QTest::newRow("bottom") << int(Edge::Bottom);
}
void EdgeTabInteractionTest::sideBarInsertionUsesTheAppropriateAxis()
{
    QFETCH(int, edge);
    WindowManager manager;
    const auto windowId = manager.createWindow();
    const auto a = manager.addTab(windowId, content("A"), options("A"));
    const auto b = manager.addTab(windowId, content("B"), options("B"));
    const auto c = manager.addTab(windowId, content("C"), options("C"));
    QVERIFY(manager.moveTabToEdge(a, Edge(edge)));
    QVERIFY(manager.moveTab(b, manager.model().groupForTab(a)));
    QVERIFY(manager.moveTabToEdge(c, Edge(edge)));
    auto* window = manager.windowWidget(windowId); showTestWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto* surface = dynamic_cast<DockSurface*>(window->findChild<QFrame*>("DockSurface")); QVERIFY(surface);
    const bool vertical = edge == int(Edge::Left) || edge == int(Edge::Right);
    for (int i = 0; i < 3; ++i) {
        const auto id = QVector<TabId>{a, b, c}[i];
        auto* button = edgeButton(window, id); QVERIFY(button);
        const auto center = button->rect().center();
        const auto offset = vertical ? QPoint(0, 8) : QPoint(8, 0);
        QCOMPARE(surface->edgeTabInsertionIndex(Edge(edge), button->mapToGlobal(center - offset)), i);
        QCOMPARE(surface->edgeTabInsertionIndex(Edge(edge), button->mapToGlobal(center + offset)), i + 1);
        const auto marker = surface->edgeTabInsertionRect(Edge(edge), i);
        QVERIFY(marker.isValid());
        QCOMPARE(vertical ? marker.height() : marker.width(), 4);
    }
}
void EdgeTabInteractionTest::sideBarReorderCommandsKeepLiveContent()
{
    WindowManager manager;
    const auto window = manager.createWindow();
    auto* aContent = content("A");
    const auto a = manager.addTab(window, aContent, options("A"));
    const auto b = manager.addTab(window, content("B"), options("B"));
    const auto c = manager.addTab(window, content("C"), options("C"));
    const auto pane = manager.model().tab(a)->activePane;
    QVERIFY(manager.moveTabToEdge(a, Edge::Left));
    QVERIFY(manager.moveTab(b, manager.model().groupForTab(a)));
    QVERIFY(manager.moveTabToEdge(c, Edge::Left));
    QVERIFY(manager.activateTab(a));
    QVERIFY(!manager.execute("workspace.reorderTab.previous", {}, window));
    QVERIFY(manager.execute("workspace.reorderTab.next", {}, window));
    QCOMPARE(manager.model().autoHiddenTabs(window, Edge::Left), (QVector<TabId>{b, a, c}));
    QVERIFY(manager.execute("workspace.reorderTab.next", {}, window));
    QCOMPARE(manager.model().autoHiddenTabs(window, Edge::Left), (QVector<TabId>{b, c, a}));
    QVERIFY(!manager.execute("workspace.reorderTab.next", {}, window));
    QVERIFY(manager.execute("workspace.reorderTab.previous", {}, window));
    QCOMPARE(manager.model().autoHiddenTabs(window, Edge::Left), (QVector<TabId>{b, a, c}));
    QCOMPARE(manager.paneWidget(pane), aContent);
    QCOMPARE(manager.commands().find("workspace.reorderTab.previous")->shortcut, QKeySequence("Ctrl+Shift+PgUp"));
    QCOMPARE(manager.commands().find("workspace.reorderTab.next")->shortcut, QKeySequence("Ctrl+Shift+PgDown"));
    showTestWindow(manager.windowWidget(window));
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    QTest::keyClick(manager.windowWidget(window), Qt::Key_PageDown, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(manager.model().autoHiddenTabs(window, Edge::Left), (QVector<TabId>{b, c, a}));
    QTest::keyClick(manager.windowWidget(window), Qt::Key_PageUp, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(manager.model().autoHiddenTabs(window, Edge::Left), (QVector<TabId>{b, a, c}));
    QCOMPARE(manager.paneWidget(pane), aContent);
    QVERIFY(manager.model().validate().ok);
}

QTEST_MAIN(EdgeTabInteractionTest)
#include "edge_tab_interaction_test.moc"
