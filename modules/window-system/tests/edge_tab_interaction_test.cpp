#include "translation_checks.hpp"
#include <window_system/window_manager.hpp>
#include "docking/dock_surface.hpp"
#include "docking/tab_strip.hpp"
#include "style/tab_label_geometry.hpp"
#include "docking/edge_tab_button.hpp"
#include "docking/drag_presentation.hpp"
#include "chrome/workspace_window.hpp"
#include "authored_mouse_input.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QEnterEvent>
#include <QImage>
#include <QLabel>
#include <QLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPointer>
#include <QScrollBar>
#include <QScreen>
#include <QStyle>
#include <QTest>
#include <QScrollArea>
#include <QSignalSpy>
#include <QJsonObject>
#include <QToolButton>
#include <climits>
#include <QWheelEvent>

using namespace window_system;

namespace {
// `inner` lies inside `outer`, the same few pixels from each side.
bool insetEqually(QRect inner, QRect outer)
{
    const int margin = inner.left() - outer.left();
    return margin >= 0 && margin <= 8 && inner == outer.adjusted(margin, margin, -margin, -margin);
}
QAbstractButton* edgeButton(QWidget* window, TabId id) {
    for (auto* button : window->findChildren<QAbstractButton*>())
        if (button->isVisible() && button->objectName() == "EdgeButton" && button->property("tabId").toUuid() == id) return button;
    return nullptr;
}
QWidget* content(const QString& name) { auto* widget = new QLabel(name); widget->setObjectName(name); return widget; }
TabOptions options(const QString& title) { TabOptions result; result.title = title; result.contentType = "test"; return result; }
void showTestWindow(QWidget* window) { window->setWindowFlag(Qt::WindowStaysOnTopHint); window->show(); }
QImage renderAtRatio(QWidget* widget, qreal ratio)
{
    QImage image(QSize(qRound(widget->width() * ratio), qRound(widget->height() * ratio)), QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(ratio); image.fill(Qt::transparent);
    QPainter painter(&image); widget->render(&painter); painter.end(); return image;
}
QImage logicalCrop(const QImage& image, QRect rect)
{
    const auto ratio = image.devicePixelRatio();
    return image.copy(QRect(qRound(rect.x() * ratio), qRound(rect.y() * ratio),
        qRound(rect.width() * ratio), qRound(rect.height() * ratio)));
}
GroupTabs* groupView(QWidget* window, GroupId id)
{
    for (auto* widget : window->findChildren<QWidget*>())
        if (auto* group = dynamic_cast<GroupTabs*>(widget);
            group && group->isVisible() && group->property("groupId").toUuid() == id) return group;
    return nullptr;
}
void sendDragMouse(QWidget* receiver, QEvent::Type type, QPoint global,
                   Qt::MouseButton button = Qt::NoButton)
{
    test::authoredMouseInput().takePriority();
    QMouseEvent event(type, receiver->mapFromGlobal(global), global, button, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(receiver, &event);
    test::authoredMouseInput().takePriority();
    QApplication::processEvents();
}
// A strip tab's width with a title of `title` pixels (tab strip design 2.1).
int stripTabWidth(const TabBar* bar, int index, int title)
{
    const int icon = bar->appearance().showIcons && !bar->tabIcon(index).isNull() ? bar->iconSize().width() : 0;
    return tabBarTabWidth({0, icon, title});
}
QWidget* dragLabelPreview()
{
    for (auto* widget : QApplication::topLevelWidgets())
        if (widget->objectName() == "DragLabelPreview") return widget;
    return nullptr;
}
}

class EdgeTabInteractionTest final : public QObject {
    Q_OBJECT
    translation_test::ChineseUiCatalog chineseUi_;
private slots:
    void initTestCase() { QVERIFY(chineseUi_.install()); }
    void cleanup() { test::authoredMouseInput().finish(); }
    void peekShowsOnlyTheChosenTabAndPinsOnlyThatTab();
    void railTitleLengthChangesKeepTheLeadingTabInPlace();
    void appearanceChangesRelabelRailsInPlace();
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
    void sideBarInsertionUsesTheAppropriateAxis_data();
    void sideBarInsertionUsesTheAppropriateAxis();
    void sideBarReorderCommandsKeepLiveContent();
    void autoHidePresentationUsesIndependentFlagsAndPerTabIconOverrides_data();
    void autoHidePresentationUsesIndependentFlagsAndPerTabIconOverrides();
    void titleClippingUsesTheTextAxisAndMatchesTheDragPreview_data();
    void titleClippingUsesTheTextAxisAndMatchesTheDragPreview();
    void clippedRailLabelsUseTheSameInsertionExtent_data();
    void clippedRailLabelsUseTheSameInsertionExtent();
    void oversizedUnclippedTitleStaysWithinTheWidgetSizeLimit();
    void regularPinnedAndAutoHideTitlesUseIndependentLimits_data();
    void regularPinnedAndAutoHideTitlesUseIndependentLimits();
    void independentTitleLimitsFollowActualDragPresentations_data();
    void independentTitleLimitsFollowActualDragPresentations();
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
    QCOMPARE(peek->findChildren<TabBar*>().size(), 0);
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
        QCOMPARE(bars[Edge::Left].left(), area.left());
        QCOMPARE(bars[Edge::Left].top(), area.top());
        QCOMPARE(bars[Edge::Left].bottom(), bars.contains(Edge::Bottom) ? bars[Edge::Bottom].top() - 3 : area.bottom());
    }
    if (bars.contains(Edge::Right)) {
        QCOMPARE(bars[Edge::Right].right(), area.right());
        QCOMPARE(bars[Edge::Right].top(), area.top());
        QCOMPARE(bars[Edge::Right].bottom(), bars.contains(Edge::Bottom) ? bars[Edge::Bottom].top() - 3 : area.bottom());
    }
    if (bars.contains(Edge::Top)) {
        QCOMPARE(bars[Edge::Top].top(), area.top());
        QCOMPARE(bars[Edge::Top].left(), bars.contains(Edge::Left) ? bars[Edge::Left].right() + 3 : area.left());
        QCOMPARE(bars[Edge::Top].right(), bars.contains(Edge::Right) ? bars[Edge::Right].left() - 3 : area.right());
    }
    if (bars.contains(Edge::Bottom)) {
        QCOMPARE(bars[Edge::Bottom].bottom(), area.bottom());
        QCOMPARE(bars[Edge::Bottom].left(), area.left()); QCOMPARE(bars[Edge::Bottom].right(), area.right());
    }
    for (int i = 0; i < edges.size(); ++i) for (int j = i + 1; j < edges.size(); ++j)
        if (bars.contains(edges[i]) && bars.contains(edges[j])) QVERIFY(!bars[edges[i]].intersects(bars[edges[j]]));
    // The trailing all-pin control also stops before a neighbouring rail;
    // its reserved space must not erase the corner separator.
    for (auto* pin : window->findChildren<QToolButton*>("PinEdgeButton")) {
        if (!pin->isVisible()) continue;
        const QRect button(pin->mapToGlobal(QPoint()), pin->size());
        const auto edge = Edge(pin->parentWidget()->property("edge").toInt());
        QVERIFY(bars.value(edge).contains(button));
        for (auto iterator = bars.cbegin(); iterator != bars.cend(); ++iterator)
            if (iterator.key() != edge) QVERIFY(!button.adjusted(-1, -1, 1, 1).intersects(iterator.value()));
    }
    QRect expectedContent = area;
    if (bars.contains(Edge::Left)) expectedContent.setLeft(bars.value(Edge::Left).right() + 1);
    if (bars.contains(Edge::Right)) expectedContent.setRight(bars.value(Edge::Right).left() - 1);
    if (bars.contains(Edge::Top)) expectedContent.setTop(bars.value(Edge::Top).bottom() + 1);
    if (bars.contains(Edge::Bottom)) expectedContent.setBottom(bars.value(Edge::Bottom).top() - 1);
    // Group cards keep the same small margin from the rails on every side.
    QVERIFY(insetEqually(surface->centralRectGlobal(), expectedContent));
    // Junctions expose the same light background as the space between rail
    // labels. They are empty layout space, not another painted border.
    QList<QRect> junctions;
    if (bars.contains(Edge::Top)) {
        const auto top = bars.value(Edge::Top);
        if (bars.contains(Edge::Left)) junctions.append(QRect(bars.value(Edge::Left).right() + 1, top.top(), 2, top.height()));
        if (bars.contains(Edge::Right)) junctions.append(QRect(top.right() + 1, top.top(), 2, top.height()));
    }
    if (bars.contains(Edge::Bottom)) {
        const int gapTop = bars.value(Edge::Bottom).top() - 2;
        for (const auto edge : {Edge::Left, Edge::Right})
            if (bars.contains(edge)) junctions.append(QRect(bars.value(edge).left(), gapTop, bars.value(edge).width(), 2));
    }
    const auto verifyGapPixels = [&](const QColor& expected) {
        const auto image = surface->grab().toImage();
        const auto ratio = image.devicePixelRatio();
        for (const auto junction : junctions) {
            const auto local = junction.translated(-area.topLeft());
            for (int y = local.top(); y <= local.bottom(); ++y)
                for (int x = local.left(); x <= local.right(); ++x)
                    QCOMPARE(image.pixelColor(qRound(x * ratio), qRound(y * ratio)), expected);
        }
    };
    verifyGapPixels(manager.theme().headerBackground);
    auto changedTheme = manager.theme(); changedTheme.headerBackground = QColor("#bfcfde");
    manager.setTheme(changedTheme);
    verifyGapPixels(changedTheme.headerBackground);
    QVERIFY(manager.setSideBarsVisible(windowId, false));
    QTRY_VERIFY(surface->edgeBarRects().isEmpty());
    QTRY_VERIFY(insetEqually(surface->centralRectGlobal(), area));
    QVERIFY(manager.setSideBarsVisible(windowId, true));
    QTRY_COMPARE(surface->edgeBarRects(), bars);
    QTRY_VERIFY(insetEqually(surface->centralRectGlobal(), expectedContent));
    verifyGapPixels(changedTheme.headerBackground);
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
    auto* rail = buttonA->parentWidget();
    while (rail && rail->objectName() != "EdgeBar") rail = rail->parentWidget();
    QVERIFY(rail);
    auto* pinAll = rail->findChild<QToolButton*>("PinEdgeButton"); QVERIFY(pinAll);
    auto* row = pinAll->parentWidget()->layout(); QVERIFY(row);
    QVERIFY(pinAll->parentWidget() != buttonA->parentWidget());
    QVERIFY(row->indexOf(pinAll) > 0);
    QVERIFY(pinAll->mapToGlobal(QPoint()).y() > buttonB->mapToGlobal(buttonB->rect().bottomLeft()).y());
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
    QTRY_VERIFY(tabs.tabBar()->isOverflowing());
    const auto active = tabs.currentIndex();
    const auto point = tabs.tabBar()->displayTabRect(0).center();
    QWheelEvent wheel(QPointF(point), tabs.tabBar()->mapToGlobal(QPointF(point)), {}, QPoint(0, -120),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(tabs.tabBar(), &wheel);
    QVERIFY(tabs.scrollOffset() > 0); QCOMPARE(tabs.currentIndex(), active);
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
    QTRY_VERIFY(tabs.tabBar()->isOverflowing());
    tabs.tabBar()->setScrollOffset(tabs.tabBar()->contentTabRect(9).left());
    const auto ninth = tabs.tabBar()->displayTabRect(9); QVERIFY(!ninth.isEmpty());
    const auto ninthPosition = tabs.tabBar()->mapToGlobal(ninth.center() - QPoint(8, 0));
    QVERIFY(tabs.viewportRectGlobal().contains(ninthPosition)); QCOMPARE(tabs.insertionIndex(ninthPosition), 9);
    QCOMPARE(tabs.insertionIndex(tabs.tabBar()->mapToGlobal(ninth.center() + QPoint(8, 0))), 10);
}
void EdgeTabInteractionTest::dragHoverScrollsAtViewportEdges()
{
    GroupTabs tabs(Theme{}, nullptr); tabs.resize(400, 240);
    for (int i = 0; i < 18; ++i) tabs.addTab(content(QString::number(i)), {}, QStringLiteral("Session number %1").arg(i), QUuid::createUuid());
    showTestWindow(&tabs); QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    QTRY_VERIFY(tabs.tabBar()->isOverflowing());
    const auto viewport = tabs.viewportRectGlobal(); tabs.setDragHover(QPoint(viewport.right() - 2, viewport.center().y()));
    QTRY_VERIFY(tabs.scrollOffset() > 0);
    tabs.setDragHover({}); const auto position = tabs.scrollOffset(); QTest::qWait(100); QCOMPARE(tabs.scrollOffset(), position);
}
void EdgeTabInteractionTest::tabIconVisibilityUsesTheHostApi()
{
    WindowManager manager;
    const auto window = manager.createWindow();
    const auto tab = manager.addTab(window, content("Content"), options("Tab"));
    showTestWindow(manager.windowWidget(window)); QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    const auto barForTab = [&]() -> TabBar* {
        for (auto* bar : manager.windowWidget(window)->findChildren<TabBar*>())
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
    QVERIFY(manager.execute("workspace.reorderTab.previous", {}, window));
    QCOMPARE(manager.model().autoHiddenTabs(window, Edge::Left), (QVector<TabId>{b, c, a}));
    QVERIFY(manager.execute("workspace.reorderTab.next", {}, window));
    QCOMPARE(manager.model().autoHiddenTabs(window, Edge::Left), (QVector<TabId>{a, b, c}));
    QVERIFY(manager.execute("workspace.reorderTab.next", {}, window));
    QCOMPARE(manager.model().autoHiddenTabs(window, Edge::Left), (QVector<TabId>{b, a, c}));
    QVERIFY(manager.execute("workspace.reorderTab.next", {}, window));
    QCOMPARE(manager.model().autoHiddenTabs(window, Edge::Left), (QVector<TabId>{b, c, a}));
    QVERIFY(manager.execute("workspace.reorderTab.next", {}, window));
    QCOMPARE(manager.model().autoHiddenTabs(window, Edge::Left), (QVector<TabId>{a, b, c}));
    QVERIFY(manager.execute("workspace.reorderTab.previous", {}, window));
    QCOMPARE(manager.model().autoHiddenTabs(window, Edge::Left), (QVector<TabId>{b, c, a}));
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

void EdgeTabInteractionTest::autoHidePresentationUsesIndependentFlagsAndPerTabIconOverrides_data()
{
    QTest::addColumn<int>("edge");
    QTest::newRow("left") << int(Edge::Left);
    QTest::newRow("right") << int(Edge::Right);
    QTest::newRow("top") << int(Edge::Top);
    QTest::newRow("bottom") << int(Edge::Bottom);
}

void EdgeTabInteractionTest::autoHidePresentationUsesIndependentFlagsAndPerTabIconOverrides()
{
    QFETCH(int, edge);
    WindowManager manager; const auto window = manager.createWindow();
    manager.addTab(window, content("Central"), options("Central"));
    const QVector<QString> titles{"Side A", "Side B", "Separate group"};
    QVector<TabId> ids; QVector<PaneId> panes; QVector<QWidget*> contents;
    for (const auto& title : titles) {
        auto* widget = content(title); contents.append(widget);
        const auto id = manager.addTab(window, widget, options(title)); ids.append(id);
        panes.append(manager.model().tab(id)->activePane);
    }
    QVERIFY(manager.moveTabToEdge(ids[0], Edge(edge)));
    const auto firstGroup = manager.model().groupForTab(ids[0]);
    QVERIFY(manager.moveTab(ids[1], firstGroup));
    QVERIFY(manager.moveTabToEdge(ids[2], Edge(edge)));
    const auto otherGroup = manager.model().groupForTab(ids[2]); QVERIFY(otherGroup != firstGroup);
    QVERIFY(manager.setTabIconVisible(ids[1], false));
    auto* host = manager.windowWidget(window); showTestWindow(host); QVERIFY(QTest::qWaitForWindowExposed(host));
    for (int index = 0; index < ids.size(); ++index) {
        auto* button = edgeButton(host, ids[index]); QVERIFY(button);
        QCOMPARE(button->text(), titles[index]); QCOMPARE(button->toolTip(), titles[index]);
        QCOMPARE(button->accessibleName(), titles[index]); QCOMPARE(button->property("tabNumber").toInt(), 0);
        QVERIFY(button->icon().isNull());
    }
    auto appearance = manager.tabAppearance(); appearance.showNumbers = true; appearance.showIcons = false;
    QVERIFY(manager.setTabAppearance(appearance));
    for (const auto id : ids) {
        auto* button = edgeButton(host, id); QVERIFY(button);
        QVERIFY(button->icon().isNull()); QCOMPARE(button->property("tabNumber").toInt(), 0);
    }
    appearance.showSidebarNumbers = true; appearance.showSidebarIcons = true;
    QVERIFY(manager.setTabAppearance(appearance));
    const QVector<int> expectedNumbers{1, 2, 3};
    for (int index = 0; index < ids.size(); ++index) {
        auto* button = edgeButton(host, ids[index]); QVERIFY(button);
        QCOMPARE(button->property("tabNumber").toInt(), expectedNumbers[index]); QCOMPARE(button->text(), titles[index]);
    }
    // Suppression by the sidebar preference cannot change the host's own
    // per-tab flag. Re-enabling it must respect that original exception.
    appearance.showSidebarIcons = false; QVERIFY(manager.setTabAppearance(appearance));
    for (const auto id : ids) { auto* button = edgeButton(host, id); QVERIFY(button); QVERIFY(button->icon().isNull()); }
    QVERIFY(manager.tabOptions(ids[0]).showIcon); QVERIFY(!manager.tabOptions(ids[1]).showIcon);
    appearance.showSidebarIcons = true; QVERIFY(manager.setTabAppearance(appearance));
    for (int index = 0; index < ids.size(); ++index) {
        auto* button = edgeButton(host, ids[index]); QVERIFY(button);
        QCOMPARE(button->icon().isNull(), index == 1);
        QCOMPARE(button->property("tabNumber").toInt(), expectedNumbers[index]);
    }
    QVERIFY(manager.setTabIconVisible(ids[1], true));
    QVERIFY(edgeButton(host, ids[1])); QVERIFY(!edgeButton(host, ids[1])->icon().isNull());
    QVERIFY(manager.setTabIconVisible(ids[1], false));
    QVERIFY(edgeButton(host, ids[1])); QVERIFY(edgeButton(host, ids[1])->icon().isNull());
    // The number follows the entire same-edge rail across model groups,
    // including reordering inside one of those groups.
    QVERIFY(manager.moveTab(ids[1], firstGroup, Placement::Center, 0));
    QCOMPARE(manager.model().group(firstGroup)->tabs, (QVector<TabId>{ids[1], ids[0]}));
    for (int index = 0; index < ids.size(); ++index) {
        auto* button = edgeButton(host, ids[index]); QVERIFY(button);
        QCOMPARE(button->property("tabNumber").toInt(), index == 0 ? 2 : index == 1 ? 1 : 3);
        QCOMPARE(button->text(), titles[index]); QCOMPARE(button->toolTip(), titles[index]);
    }
    appearance.showSidebarNumbers = false; QVERIFY(manager.setTabAppearance(appearance));
    for (int index = 0; index < ids.size(); ++index) {
        auto* button = edgeButton(host, ids[index]); QVERIFY(button); QCOMPARE(button->property("tabNumber").toInt(), 0);
        QCOMPARE(manager.model().tab(ids[index])->activePane, panes[index]);
        QCOMPARE(manager.paneWidget(panes[index]), contents[index]);
        QCOMPARE(manager.model().tab(ids[index])->title, titles[index]);
    }
    QCOMPARE(manager.model().groupForTab(ids[2]), otherGroup); QVERIFY(manager.model().validate());
}

void EdgeTabInteractionTest::titleClippingUsesTheTextAxisAndMatchesTheDragPreview_data()
{
    QTest::addColumn<int>("edge"); QTest::addColumn<bool>("decorated");
    for (const auto edge : {Edge::Left, Edge::Right, Edge::Top, Edge::Bottom}) {
        QTest::newRow(qPrintable(QString("plain-%1").arg(int(edge)))) << int(edge) << false;
        QTest::newRow(qPrintable(QString("decorated-%1").arg(int(edge)))) << int(edge) << true;
    }
}

void EdgeTabInteractionTest::titleClippingUsesTheTextAxisAndMatchesTheDragPreview()
{
    QFETCH(int, edge); QFETCH(bool, decorated);
    const bool vertical = Edge(edge) == Edge::Left || Edge(edge) == Edge::Right;
    QWidget root; applyTheme(&root); root.resize(900, 900);
    auto font = root.font(); font.setPixelSize(decorated ? 19 : 15); root.setFont(font);
    const auto title = QString(25, QLatin1Char('W'));
    const int thickness = defaultTheme().metrics.edgeThickness;
    EdgeTabButton button(title, vertical, thickness, &root);
    QPixmap iconPixels(16, 16); iconPixels.fill(Qt::white);
    const auto icon = decorated ? QIcon(iconPixels) : QIcon{}; const int number = decorated ? 12 : 0;
    button.setPresentation(icon, number);
    auto preset = defaultTabColorPresets().front();
    preset.autoHideInactive = {Qt::white, Qt::black, QColor("#123456"), QColor("#2468ac")};
    button.setPreset(preset); button.resize(button.sizeHint()); button.move(10, 10);
    root.show(); QVERIFY(QTest::qWaitForWindowExposed(&root));
    const int decorationWidth = decorated ? 20 + button.fontMetrics().horizontalAdvance("12") + 4 : 0;
    const int textLeft = 8 + decorationWidth;
    const auto major = [vertical](QSize size) { return vertical ? size.height() : size.width(); };
    const int naturalExtent = major(button.sizeHint());
    QCOMPARE(naturalExtent, button.fontMetrics().horizontalAdvance(title) + 16 + decorationWidth);
    const auto full = renderAtRatio(&button, 1);
    const auto inkPoint = [vertical, thickness](int x, int y) {
        return vertical ? QPoint(thickness - 1 - y, x) : QPoint(x, y);
    };
    int limit = 0;
    for (int x = textLeft + 48; x < textLeft + 120 && !limit; ++x)
        for (int y = 2; y < thickness - 3; ++y)
            if (full.pixelColor(inkPoint(x, y)) != QColor(Qt::white)
                && full.pixelColor(inkPoint(x + 1, y)) != QColor(Qt::white)) {
                limit = x - textLeft + 1; break;
            }
    QVERIFY(limit >= 16);
    QVector<QImage> references;
    for (const qreal ratio : {1.0, 2.0}) references.append(renderAtRatio(&button, ratio));
    button.setTitleClipping(true, limit); button.resize(button.sizeHint());
    QCOMPARE(major(button.sizeHint()), limit + 16 + decorationWidth);
    QCOMPARE(vertical ? button.width() : button.height(), thickness);
    QCOMPARE(button.text(), title); QCOMPARE(button.toolTip(), title); QCOMPARE(button.accessibleName(), title);
    const QRect textRect = vertical ? QRect(3, textLeft, thickness - 5, limit) : QRect(textLeft, 2, limit, thickness - 5);
    for (int index = 0; index < references.size(); ++index) {
        const auto actual = renderAtRatio(&button, index ? 2 : 1);
        QCOMPARE(logicalCrop(actual, textRect), logicalCrop(references[index], textRect));
        const QRect beyond = vertical ? QRect(3, textLeft + limit, thickness - 5, 5)
            : QRect(textLeft + limit, 2, 5, thickness - 5);
        const auto blank = logicalCrop(actual, beyond);
        for (int y = 0; y < blank.height(); ++y) for (int x = 0; x < blank.width(); ++x)
            QCOMPARE(blank.pixelColor(x, y), QColor(Qt::white));
    }
    TabAppearance appearance; appearance.clipSidebarTitles = true; appearance.maximumSidebarTitleWidth = limit;
    EdgeDragPresentation preview(title, Edge(edge), defaultTheme(), new QLabel("Live content"), &root);
    preview.configureLabel(preset, icon, number, appearance);
    auto* previewLabel = preview.findChild<QAbstractButton*>("DragEdgeLabel"); QVERIFY(previewLabel);
    QCOMPARE(previewLabel->size(), button.size()); QCOMPARE(previewLabel->text(), title);
    QCOMPARE(previewLabel->toolTip(), title); QCOMPARE(previewLabel->accessibleName(), title);
    button.setTitleClipping(false, limit); button.resize(button.sizeHint());
    QCOMPARE(major(button.sizeHint()), naturalExtent);
    button.setText("i"); button.setPresentation({}, 0); button.setTitleClipping(true, 16);
    // A short label is not stretched; only an empty one keeps the rail's thickness.
    QCOMPARE(major(button.sizeHint()), qMax(thickness, 16 + qMin(16, button.fontMetrics().horizontalAdvance("i"))));
    button.setTitleClipping(true, 0);
    QCOMPARE(major(button.sizeHint()), thickness);
}

void EdgeTabInteractionTest::clippedRailLabelsUseTheSameInsertionExtent_data()
{
    peekShowsTheRealContentAndSharedBars_data();
}

void EdgeTabInteractionTest::clippedRailLabelsUseTheSameInsertionExtent()
{
    QFETCH(int, edge);
    const bool vertical = Edge(edge) == Edge::Left || Edge(edge) == Edge::Right;
    WindowManager manager; auto appearance = manager.tabAppearance();
    appearance.clipSidebarTitles = true; appearance.maximumSidebarTitleWidth = 77;
    appearance.showSidebarIcons = true; appearance.showSidebarNumbers = true;
    QVERIFY(manager.setTabAppearance(appearance));
    const auto window = manager.createWindow(); manager.addTab(window, content("Central"), options("Central"));
    const auto title = QString(25, QLatin1Char('W'));
    const auto side = manager.addTab(window, content("Side"), options(title));
    QVERIFY(manager.moveTabToEdge(side, Edge(edge)));
    auto* host = manager.windowWidget(window); showTestWindow(host); QVERIFY(QTest::qWaitForWindowExposed(host));
    auto* button = edgeButton(host, side); QVERIFY(button);
    const auto extent = [vertical](QSize size) { return vertical ? size.height() : size.width(); };
    const int expected = appearance.maximumSidebarTitleWidth + 16 + 20 + button->fontMetrics().horizontalAdvance("1") + 4;
    QCOMPARE(extent(button->size()), expected);
    auto* surface = host->findChild<QFrame*>("DockSurface"); QVERIFY(surface);
    auto* dock = static_cast<DockSurface*>(surface);
    QCOMPARE(dock->dragLabelExtent(side, true), expected);
    dock->setEdgeInsertionPreview(Edge(edge), 1, dock->dragLabelExtent(side, true));
    const auto gap = dock->edgeInsertionPreviewRect(Edge(edge)); QVERIFY(!gap.isEmpty());
    QCOMPARE(extent(gap.size()), expected);
    QCOMPARE(dock->edgeTabInsertionIndex(Edge(edge), gap.center()), 1);
    dock->clearInsertionPreviews();
    QCOMPARE(manager.model().tab(side)->title, title);
    QCOMPARE(button->text(), title); QCOMPARE(button->toolTip(), title); QCOMPARE(button->accessibleName(), title);
    appearance.clipSidebarTitles = false; QVERIFY(manager.setTabAppearance(appearance));
    button = edgeButton(host, side); QVERIFY(button);
    QVERIFY(extent(button->sizeHint()) > expected);
    QCOMPARE(manager.model().tab(side)->title, title);
}

void EdgeTabInteractionTest::railTitleLengthChangesKeepTheLeadingTabInPlace()
{
    // Rails are rebuilt on every refresh. Changing their label lengths keeps
    // the tab at the viewport's leading edge in place instead of reapplying a
    // raw offset that now points into unrelated tabs.
    WindowManager manager; auto appearance = manager.tabAppearance();
    appearance.clipSidebarTitles = true; appearance.maximumSidebarTitleWidth = 160;
    QVERIFY(manager.setTabAppearance(appearance));
    const auto window = manager.createWindow(WindowKind::Workbench, {"Rail anchor", {700, 500}});
    manager.addTab(window, content("Central"), options("Central"));
    QVector<TabId> side;
    for (int index = 0; index < 25; ++index) {
        side << manager.addTab(window, content(QString("Side %1").arg(index)),
                               options(QString("Side %1 ").arg(index) + QString(30, QLatin1Char('W'))));
        QVERIFY(manager.moveTabToEdge(side.last(), Edge::Left));
    }
    auto* host = manager.windowWidget(window); showTestWindow(host); QVERIFY(QTest::qWaitForWindowExposed(host));
    const auto viewportOf = [host, &side]() -> QWidget* {
        auto* button = edgeButton(host, side.first());
        for (auto* current = button ? button->parentWidget() : nullptr; current; current = current->parentWidget())
            if (auto* area = qobject_cast<QScrollArea*>(current)) return area->viewport();
        return nullptr;
    };
    const auto position = [&](int index) {
        auto* button = edgeButton(host, side[index]); auto* viewport = viewportOf();
        return button && viewport ? button->mapTo(viewport, QPoint()).y() : INT_MIN;
    };
    auto* area = qobject_cast<QScrollArea*>(viewportOf()->parentWidget()); QVERIFY(area);
    QVERIFY(area->verticalScrollBar()->maximum() > 0);
    area->verticalScrollBar()->setValue(area->verticalScrollBar()->value() + position(8));
    QCoreApplication::processEvents();
    QCOMPARE(position(8), 0);
    for (const int length : {40, 0, 300, 160}) {
        appearance.maximumSidebarTitleWidth = length; QVERIFY(manager.setTabAppearance(appearance));
        QCoreApplication::processEvents(); QCoreApplication::processEvents();
        QCOMPARE(position(8), 0);
    }
}

void EdgeTabInteractionTest::appearanceChangesRelabelRailsInPlace()
{
    // Tab appearance only relabels built strips: an open peek stays open and
    // the rail keeps its buttons, which only change length.
    WindowManager manager; auto appearance = manager.tabAppearance();
    appearance.clipSidebarTitles = true; appearance.maximumSidebarTitleWidth = 160;
    QVERIFY(manager.setTabAppearance(appearance));
    const auto window = manager.createWindow();
    manager.addTab(window, content("Central"), options("Central"));
    auto* sideContent = content("Side content");
    const auto side = manager.addTab(window, sideContent, options(QString(30, QLatin1Char('W'))));
    QVERIFY(manager.moveTabToEdge(side, Edge::Left));
    auto* host = manager.windowWidget(window); showTestWindow(host); QVERIFY(QTest::qWaitForWindowExposed(host));
    QVERIFY(manager.activateTab(side));
    const QPointer<QWidget> peek = host->findChild<QFrame*>("PeekPanel"); QVERIFY(peek);
    QTRY_VERIFY(sideContent->isVisible());
    const QPointer<QAbstractButton> button = edgeButton(host, side); QVERIFY(button);
    const int before = button->height();
    // The first show reports the window placement from a zero-delay timer.
    // Let it run so the spy counts only the appearance change.
    QCoreApplication::processEvents();
    QSignalSpy changes(&manager, &WindowManager::layoutChanged);
    appearance.maximumSidebarTitleWidth = 40; QVERIFY(manager.setTabAppearance(appearance));
    QCoreApplication::processEvents();
    QCOMPARE(changes.size(), 1);
    QVERIFY(peek && peek->isVisible()); QVERIFY(sideContent->isVisible());
    QCOMPARE(edgeButton(host, side), button.data());
    QCOMPARE(button->height(), before - 120);
    appearance.showSidebarNumbers = true; QVERIFY(manager.setTabAppearance(appearance));
    QCoreApplication::processEvents();
    QCOMPARE(edgeButton(host, side), button.data()); QCOMPARE(button->property("tabNumber").toInt(), 1);
    QVERIFY(peek && peek->isVisible());
    QCOMPARE(manager.saveLayout().value("tabAppearance").toObject().value("maximumSidebarTitleWidth").toInt(), 40);
}

void EdgeTabInteractionTest::oversizedUnclippedTitleStaysWithinTheWidgetSizeLimit()
{
    // Measure only: never allocate a widget or image at this natural extent.
    QFont font = qApp->font(); font.setPixelSize(1024);
    const QString title(24000, QLatin1Char('W'));
    const QFontMetrics metrics(font);
    QVERIFY(metrics.horizontalAdvance(title) > QWIDGETSIZE_MAX);
    QCOMPARE(edgeTabExtent(metrics, metrics.horizontalAdvance(title), true, 123, defaultTheme().metrics.edgeThickness), QWIDGETSIZE_MAX);
    for (const bool vertical : {false, true}) {
        EdgeTabButton button(title, vertical, defaultTheme().metrics.edgeThickness, nullptr);
        button.setFont(font);
        const auto size = button.sizeHint();
        QCOMPARE(vertical ? size.height() : size.width(), QWIDGETSIZE_MAX);
        QCOMPARE(vertical ? size.width() : size.height(), defaultTheme().metrics.edgeThickness);
        QCOMPARE(button.text(), title); QCOMPARE(button.toolTip(), title);
    }
}

void EdgeTabInteractionTest::regularPinnedAndAutoHideTitlesUseIndependentLimits_data()
{
    QTest::addColumn<bool>("clipTabs"); QTest::addColumn<bool>("clipSidebar");
    for (const bool tabs : {false, true}) for (const bool sidebar : {false, true})
        QTest::newRow(qPrintable(QString("tabs-%1-sidebar-%2").arg(tabs).arg(sidebar))) << tabs << sidebar;
}

void EdgeTabInteractionTest::regularPinnedAndAutoHideTitlesUseIndependentLimits()
{
    QFETCH(bool, clipTabs); QFETCH(bool, clipSidebar);
    WindowManager manager; auto appearance = manager.tabAppearance();
    appearance.showIcons = false; appearance.showSidebarIcons = false;
    appearance.showNumbers = false; appearance.showSidebarNumbers = false;
    appearance.clipTitles = clipTabs; appearance.maximumTitleWidth = 73;
    appearance.clipSidebarTitles = clipSidebar; appearance.maximumSidebarTitleWidth = 137;
    QVERIFY(manager.setTabAppearance(appearance));
    const auto window = manager.createWindow(WindowKind::Workbench, {"Independent title limits", {900, 650}});
    const QString title(25, QLatin1Char('W'));
    const auto regular = manager.addTab(window, content("Regular"), options(title));
    const auto pinned = manager.addTab(window, content("Pinned"), options(title));
    const auto sidebar = manager.addTab(window, content("Sidebar"), options(title));
    QVERIFY(manager.moveTabToEdge(pinned, Edge::Left, true));
    QVERIFY(manager.moveTabToEdge(sidebar, Edge::Right));
    auto* host = manager.windowWidget(window); showTestWindow(host);
    QVERIFY(QTest::qWaitForWindowExposed(host));
    for (const auto tab : {regular, pinned}) {
        auto* group = groupView(host, manager.model().groupForTab(tab)); QVERIFY(group);
        auto* bar = group->tabBar();
        const int natural = bar->fontMetrics().size(Qt::TextShowMnemonic | Qt::TextSingleLine, title).width();
        QVERIFY(natural > appearance.maximumTitleWidth);
        QCOMPARE(bar->contentTabRect(0).width(), stripTabWidth(bar, 0, clipTabs ? appearance.maximumTitleWidth : natural));
        QCOMPARE(bar->tabText(0), title);
    }
    auto* button = edgeButton(host, sidebar); QVERIFY(button);
    const int natural = button->fontMetrics().horizontalAdvance(title);
    QVERIFY(natural > appearance.maximumSidebarTitleWidth);
    QCOMPARE(button->height(), (clipSidebar ? appearance.maximumSidebarTitleWidth : natural) + 16);
    QCOMPARE(manager.model().tab(sidebar)->title, title); QCOMPARE(button->text(), title);
    QCOMPARE(button->toolTip(), title); QCOMPARE(button->accessibleName(), title);
}

void EdgeTabInteractionTest::independentTitleLimitsFollowActualDragPresentations_data()
{
    QTest::addColumn<bool>("sidebarSource");
    QTest::newRow("tab-bar-source") << false;
    QTest::newRow("sidebar-source") << true;
}

void EdgeTabInteractionTest::independentTitleLimitsFollowActualDragPresentations()
{
    QFETCH(bool, sidebarSource);
    WindowManager manager; auto appearance = manager.tabAppearance();
    appearance.showIcons = false; appearance.showSidebarIcons = false;
    appearance.showNumbers = false; appearance.showSidebarNumbers = false;
    appearance.clipTitles = true; appearance.maximumTitleWidth = 73;
    appearance.clipSidebarTitles = true; appearance.maximumSidebarTitleWidth = 137;
    QVERIFY(manager.setTabAppearance(appearance));
    const auto available = qApp->primaryScreen()->availableGeometry();
    const QSize size(qMin(900, available.width() - 24), qMin(650, available.height() - 24));
    const auto window = manager.createWindow(WindowKind::Workbench, {"Drag title limits", size});
    const QString title(25, QLatin1Char('W'));
    const auto anchor = manager.addTab(window, content("Anchor"), options(title));
    const auto anchorGroup = manager.model().groupForTab(anchor);
    for (const auto edge : {Edge::Left, Edge::Right, Edge::Top, Edge::Bottom}) {
        const auto side = manager.addTab(window, content(QString("Edge %1").arg(int(edge))), options(title), anchorGroup);
        QVERIFY(manager.moveTabToEdge(side, edge));
    }
    auto* editor = new QPlainTextEdit("Persistent drag content"); const QPointer<QPlainTextEdit> observed(editor);
    const auto moving = manager.addTab(window, editor, options(title), anchorGroup);
    if (sidebarSource) QVERIFY(manager.moveTabToEdge(moving, Edge::Left));
    const auto pane = manager.model().tab(moving)->activePane;
    auto* host = static_cast<WorkspaceWindow*>(manager.windowWidget(window));
    host->move(available.topLeft() + QPoint(12, 12)); showTestWindow(host);
    QVERIFY(QTest::qWaitForWindowExposed(host));
    for (int policy = 0; policy < 2; ++policy) {
        if (policy) {
            appearance.maximumTitleWidth = 137; appearance.maximumSidebarTitleWidth = 73;
            QVERIFY(manager.setTabAppearance(appearance));
        }
        QVERIFY(manager.activateTab(moving));
        QTRY_VERIFY(editor->isVisible());
        const auto before = manager.model().toJson();
        const auto sourceGroup = manager.model().groupForTab(moving);
        QWidget* receiver = nullptr; QRect sourceLabel;
        if (sidebarSource) {
            auto* button = edgeButton(host, moving); QVERIFY(button); receiver = button;
            sourceLabel = QRect(button->mapToGlobal(QPoint()), button->size());
        } else {
            auto* group = groupView(host, sourceGroup); QVERIFY(group); auto* bar = group->tabBar(); receiver = bar;
            const int index = manager.model().group(sourceGroup)->tabs.indexOf(moving);
            sourceLabel = bar->tabRect(index).translated(bar->mapToGlobal(QPoint()));
        }
        const QPoint threshold = sourceLabel.center() + (sidebarSource ? QPoint(0, 20) : QPoint(20, 0));
        const auto sourceStrip = sidebarSource ? host->surface()->edgeBarRects().value(Edge::Left)
            : host->surface()->tabBarRects().value(sourceGroup);
        QVERIFY(sourceStrip.contains(threshold));
        sendDragMouse(receiver, QEvent::MouseButtonPress, sourceLabel.center(), Qt::LeftButton);
        sendDragMouse(receiver, QEvent::MouseMove, threshold);
        QCOMPARE(QWidget::mouseGrabber(), host);
        // A rail shows a label window; a strip lifts its own tab.
        int sourceExtent = appearance.maximumSidebarTitleWidth + 16;
        if (sidebarSource) {
            auto* labelOnly = dragLabelPreview(); QVERIFY(labelOnly && labelOnly->isVisible());
            QCOMPARE(labelOnly->height(), sourceExtent + 12);
        } else {
            auto* bar = groupView(host, sourceGroup)->tabBar();
            sourceExtent = stripTabWidth(bar, manager.model().group(sourceGroup)->tabs.indexOf(moving), appearance.maximumTitleWidth);
            QCOMPARE(bar->liftedRect().width(), sourceExtent);
        }
        const auto sourceGap = sidebarSource ? host->surface()->edgeInsertionPreviewRect(Edge::Left)
            : host->surface()->insertionPreviewRect(sourceGroup);
        QCOMPARE(sidebarSource ? sourceGap.height() : sourceGap.width(), sourceExtent);
        QCOMPARE(manager.model().toJson(), before); QCOMPARE(manager.paneWidget(pane), editor);
        QVERIFY(editor->isVisible());

        // The first exit creates the carrier; subsequent targets only change its presentation.
        sendDragMouse(host, QEvent::MouseMove, host->surface()->centralRectGlobal().center());
        QPointer<WorkspaceWindow> carrier = dynamic_cast<WorkspaceWindow*>(QWidget::mouseGrabber());
        QVERIFY(carrier && carrier != host); QVERIFY(!dragLabelPreview());
        const auto duringDrag = manager.model().toJson();
        const auto carrierId = carrier->id();
        for (const auto edge : {Edge::Left, Edge::Right, Edge::Top, Edge::Bottom}) {
            auto* targetGroup = groupView(host, manager.model().groupForTab(anchor)); QVERIFY(targetGroup);
            const auto tabPoint = targetGroup->viewportRectGlobal().center();
            sendDragMouse(carrier, QEvent::MouseMove, tabPoint);
            QCOMPARE(carrier->dragPresentation(), WorkspaceWindow::DragPresentation::Tab);
            auto* carriedGroup = groupView(carrier, manager.model().groupForTab(moving)); QVERIFY(carriedGroup);
            const int carried = stripTabWidth(carriedGroup->tabBar(), 0, appearance.maximumTitleWidth);
            QCOMPARE(carriedGroup->tabBar()->tabRect(0).width(), carried);
            QCOMPARE(targetGroup->insertionPreviewRectGlobal().width(), carried);
            QCOMPARE(carrier->surface()->dragLabelExtent(moving, false), carried);

            const bool vertical = edge == Edge::Left || edge == Edge::Right;
            const auto area = host->surface()->edgeBarRects().value(edge); QVERIFY(!area.isEmpty());
            const QPoint point = vertical ? QPoint(area.center().x(), area.top() + 8)
                : QPoint(area.left() + 8, area.center().y());
            sendDragMouse(carrier, QEvent::MouseMove, point);
            QCOMPARE(carrier->dragPresentation(), WorkspaceWindow::DragPresentation(int(WorkspaceWindow::DragPresentation::Left) + int(edge)));
            auto* label = carrier->findChild<QAbstractButton*>("DragEdgeLabel"); QVERIFY(label && label->isVisible());
            QCOMPARE(vertical ? label->height() : label->width(), appearance.maximumSidebarTitleWidth + 16);
            const auto gap = host->surface()->edgeInsertionPreviewRect(edge); QVERIFY(!gap.isEmpty());
            QCOMPARE(vertical ? gap.height() : gap.width(), appearance.maximumSidebarTitleWidth + 16);
            QCOMPARE(carrier->surface()->dragLabelExtent(moving, true), appearance.maximumSidebarTitleWidth + 16);
            QCOMPARE(label->text(), title); QCOMPARE(label->toolTip(), title); QCOMPARE(label->accessibleName(), title);
            QCOMPARE(manager.model().tab(moving)->title, title); QCOMPARE(manager.model().tab(moving)->activePane, pane);
            QCOMPARE(manager.paneWidget(pane), editor); QVERIFY(observed && observed->isVisible());
            QCOMPARE(manager.model().toJson(), duringDrag);
        }
        QTest::keyClick(carrier, Qt::Key_Escape); QApplication::processEvents();
        QVERIFY(!QWidget::mouseGrabber()); QVERIFY(!manager.model().window(carrierId));
        QCOMPARE(manager.model().toJson(), before); QCOMPARE(manager.paneWidget(pane), editor);
        QCOMPARE(editor->toPlainText(), QStringLiteral("Persistent drag content"));
        QVERIFY(manager.model().validate());
    }
}

QTEST_MAIN(EdgeTabInteractionTest)
#include "edge_tab_interaction_test.moc"
