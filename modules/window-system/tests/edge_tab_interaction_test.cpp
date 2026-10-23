#include <window_system/window_manager.hpp>
#include "docking/dock_surface.hpp"
#include "docking/tab_strip.hpp"
#include "docking/edge_tab_button.hpp"
#include "docking/drag_presentation.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QEnterEvent>
#include <QImage>
#include <QLabel>
#include <QLayout>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
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
    void autoHideLabelsKeepOriginalColorsAndAcceptExplicitPresets_data();
    void autoHideLabelsKeepOriginalColorsAndAcceptExplicitPresets();
    void autoHidePresentationUsesIndependentFlagsAndPerTabIconOverrides_data();
    void autoHidePresentationUsesIndependentFlagsAndPerTabIconOverrides();
    void autoHideColorsSurviveActivationChanges_data();
    void autoHideColorsSurviveActivationChanges();
    void edgePresentationKeepsNaturalSizeAndRendersPresetIcon_data();
    void edgePresentationKeepsNaturalSizeAndRendersPresetIcon();
    void titleClippingUsesTheTextAxisAndMatchesTheDragPreview_data();
    void titleClippingUsesTheTextAxisAndMatchesTheDragPreview();
    void clippedRailLabelsUseTheSameInsertionExtent_data();
    void clippedRailLabelsUseTheSameInsertionExtent();
    void oversizedUnclippedTitleStaysWithinTheWidgetSizeLimit();
};

void EdgeTabInteractionTest::autoHideLabelsKeepOriginalColorsAndAcceptExplicitPresets_data()
{
    QTest::addColumn<bool>("vertical");
    QTest::newRow("vertical") << true;
    QTest::newRow("horizontal") << false;
}

void EdgeTabInteractionTest::autoHideLabelsKeepOriginalColorsAndAcceptExplicitPresets()
{
    QFETCH(bool, vertical);
    QWidget root; applyTheme(&root); root.resize(260, 180);
    EdgeTabButton button("Sidebar", vertical, defaultTheme().metrics.edgeThickness, &root);
    button.resize(button.sizeHint()); button.move(40, 40); button.setCheckable(true);
    root.show(); QVERIFY(QTest::qWaitForWindowExposed(&root));
    const auto pixel = [&button] {
        const auto image = button.grab().toImage();
        return image.pixelColor(qRound(3 * image.devicePixelRatio()), qRound(3 * image.devicePixelRatio()));
    };
    button.clearFocus(); button.setAttribute(Qt::WA_UnderMouse, false);
    QCOMPARE(pixel(), QColor("#6A568C"));
    button.setAttribute(Qt::WA_UnderMouse, true); QCOMPARE(pixel(), QColor("#8970AF"));
    button.setAttribute(Qt::WA_UnderMouse, false);
    auto theme = defaultTheme(); theme.edgeTabBackground = QColor("#123456");
    theme.edgeTabHover = QColor("#345678"); theme.edgeTabText = QColor("#ffffff");
    applyTheme(&root, theme); QCOMPARE(pixel(), theme.edgeTabBackground);
    button.setAttribute(Qt::WA_UnderMouse, true); QCOMPARE(pixel(), theme.edgeTabHover);
    button.setAttribute(Qt::WA_UnderMouse, false);
    TabColorPreset preset;
    preset.autoHideInactive = {QColor("#305040"), QColor("#ffffff"), QColor("#208030"), QColor("#ffffff")};
    preset.autoHideActive = {QColor("#406050"), QColor("#ffffff"), QColor("#309040"), QColor("#ffffff")};
    button.setPreset(preset); QCOMPARE(pixel(), preset.autoHideInactive.background);
    button.setChecked(true); QCOMPARE(pixel(), preset.autoHideActive.background);
    button.setPreset(std::nullopt); button.setChecked(false); QCOMPARE(pixel(), theme.edgeTabBackground);
}

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
    QCOMPARE(surface->centralRectGlobal(), expectedContent);
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
    QTRY_COMPARE(surface->centralRectGlobal(), area);
    QVERIFY(manager.setSideBarsVisible(windowId, true));
    QTRY_COMPARE(surface->edgeBarRects(), bars);
    QTRY_COMPARE(surface->centralRectGlobal(), expectedContent);
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

void EdgeTabInteractionTest::autoHideColorsSurviveActivationChanges_data()
{
    QTest::addColumn<int>("edge");
    for (int edge = 0; edge < 4; ++edge) QTest::newRow(qPrintable(QString::number(edge))) << edge;
}

void EdgeTabInteractionTest::autoHideColorsSurviveActivationChanges()
{
    QFETCH(int, edge);
    WindowManager manager; const auto window = manager.createWindow();
    const auto first = manager.addTab(window, content("red"), options("red"));
    const auto second = manager.addTab(window, content("green"), options("green"));
    QVERIFY(manager.moveTabToEdge(first, Edge(edge))); QVERIFY(manager.moveTabToEdge(second, Edge(edge)));
    QVERIFY(manager.setTabColor(first, "red")); QVERIFY(manager.setTabColor(second, "green"));
    const auto red = manager.tabColorPreset("red"); const auto green = manager.tabColorPreset("green");
    QVERIFY(red && green); QVERIFY(red->autoHideActive.background != red->autoHideInactive.background);
    auto* host = manager.windowWidget(window); showTestWindow(host); QVERIFY(QTest::qWaitForWindowExposed(host));
    const auto background = [host](TabId id) {
        auto* button = edgeButton(host, id);
        if (!button) return QColor{};
        const auto image = button->grab().toImage(); const auto dpr = image.devicePixelRatio();
        return image.pixelColor(qRound(3 * dpr), qRound(3 * dpr));
    };
    QVERIFY(manager.activateTab(first));
    QCOMPARE(background(first), red->autoHideActive.background); QCOMPARE(background(second), green->autoHideInactive.background);
    QVERIFY(manager.activateTab(second));
    QCOMPARE(background(first), red->autoHideInactive.background); QCOMPARE(background(second), green->autoHideActive.background);
    QVERIFY(manager.activateTab(first));
    QCOMPARE(background(first), red->autoHideActive.background); QCOMPARE(background(second), green->autoHideInactive.background);
    QCOMPARE(manager.model().tab(first)->colorId, QStringLiteral("red"));
    QCOMPARE(manager.model().tab(second)->colorId, QStringLiteral("green"));
}

void EdgeTabInteractionTest::edgePresentationKeepsNaturalSizeAndRendersPresetIcon_data()
{
    QTest::addColumn<bool>("vertical");
    QTest::newRow("horizontal") << false;
    QTest::newRow("vertical") << true;
}

void EdgeTabInteractionTest::edgePresentationKeepsNaturalSizeAndRendersPresetIcon()
{
    QFETCH(bool, vertical);
    QWidget root; applyTheme(&root); root.resize(280, 240);
    const int thickness = defaultTheme().metrics.edgeThickness;
    EdgeTabButton button("Side label", vertical, thickness, &root); button.setCheckable(true);
    const auto major = [vertical](QSize size) { return vertical ? size.height() : size.width(); };
    const int plainExtent = qMax(64, button.fontMetrics().horizontalAdvance(button.text()) + 24);
    QCOMPARE(major(button.sizeHint()), plainExtent);
    QPixmap pixels(16, 16); pixels.fill(Qt::white); const QIcon icon(pixels);
    button.setPresentation(icon, 12);
    const int numberWidth = button.fontMetrics().horizontalAdvance("12") + 5;
    const int textWidth = button.fontMetrics().horizontalAdvance(button.text());
    const int extent = qMax(64, textWidth + 24 + 20 + numberWidth);
    QCOMPARE(major(button.sizeHint()), extent); QCOMPARE(button.property("tabNumber").toInt(), 12);
    QCOMPARE(vertical ? button.sizeHint().width() : button.sizeHint().height(), thickness);
    button.resize(button.sizeHint()); button.move(30, 30);
    auto preset = defaultTabColorPresets().front();
    preset.autoHideInactive = {QColor("#314159"), QColor("#ffffff"), QColor("#c04020"), QColor("#35a9ce")};
    preset.autoHideActive = {QColor("#426270"), QColor("#ffffff"), QColor("#e06040"), QColor("#e36aac")};
    button.setPreset(preset); root.show(); QVERIFY(QTest::qWaitForWindowExposed(&root));
    button.clearFocus();
    const int span = qMin(extent - 12, textWidth + numberWidth + 20);
    const int iconCenter = qMax(6, (extent - span) / 2) + numberWidth + 8;
    const QPoint logical = vertical ? QPoint(thickness / 2, iconCenter) : QPoint(iconCenter, thickness / 2);
    const auto iconPixel = [&] {
        const auto image = button.grab().toImage(); const auto ratio = image.devicePixelRatio();
        return image.pixelColor(qRound(logical.x() * ratio), qRound(logical.y() * ratio));
    };
    QCOMPARE(iconPixel(), preset.autoHideInactive.icon);
    button.setChecked(true); QCOMPARE(iconPixel(), preset.autoHideActive.icon);
    button.setText(QString(100, QLatin1Char('W')));
    const int longTextWidth = button.fontMetrics().horizontalAdvance(button.text());
    QCOMPARE(major(button.sizeHint()), longTextWidth + 24 + 20 + numberWidth);
    button.setPresentation({}, 0); QCOMPARE(button.property("tabNumber").toInt(), 0); QVERIFY(button.icon().isNull());
    QCOMPARE(major(button.sizeHint()), longTextWidth + 24);
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
    const int decorationWidth = decorated ? 20 + button.fontMetrics().horizontalAdvance("12") + 5 : 0;
    const int textLeft = 12 + decorationWidth;
    const auto major = [vertical](QSize size) { return vertical ? size.height() : size.width(); };
    const int naturalExtent = major(button.sizeHint());
    QCOMPARE(naturalExtent, button.fontMetrics().horizontalAdvance(title) + 24 + decorationWidth);
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
    QCOMPARE(major(button.sizeHint()), limit + 24 + decorationWidth);
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
    TabAppearance appearance; appearance.clipTitles = true; appearance.maximumTitleWidth = limit;
    EdgeDragPresentation preview(title, Edge(edge), defaultTheme(), new QLabel("Live content"), &root);
    preview.configureLabel(preset, icon, number, appearance);
    auto* previewLabel = preview.findChild<QAbstractButton*>("DragEdgeLabel"); QVERIFY(previewLabel);
    QCOMPARE(previewLabel->size(), button.size()); QCOMPARE(previewLabel->text(), title);
    QCOMPARE(previewLabel->toolTip(), title); QCOMPARE(previewLabel->accessibleName(), title);
    button.setTitleClipping(false, limit); button.resize(button.sizeHint());
    QCOMPARE(major(button.sizeHint()), naturalExtent);
    button.setText("i"); button.setPresentation({}, 0); button.setTitleClipping(true, 16);
    QCOMPARE(major(button.sizeHint()), 64);
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
    appearance.clipTitles = true; appearance.maximumTitleWidth = 77;
    appearance.showSidebarIcons = true; appearance.showSidebarNumbers = true;
    QVERIFY(manager.setTabAppearance(appearance));
    const auto window = manager.createWindow(); manager.addTab(window, content("Central"), options("Central"));
    const auto title = QString(25, QLatin1Char('W'));
    const auto side = manager.addTab(window, content("Side"), options(title));
    QVERIFY(manager.moveTabToEdge(side, Edge(edge)));
    auto* host = manager.windowWidget(window); showTestWindow(host); QVERIFY(QTest::qWaitForWindowExposed(host));
    auto* button = edgeButton(host, side); QVERIFY(button);
    const auto extent = [vertical](QSize size) { return vertical ? size.height() : size.width(); };
    const int expected = appearance.maximumTitleWidth + 24 + 20 + button->fontMetrics().horizontalAdvance("1") + 5;
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
    appearance.clipTitles = false; QVERIFY(manager.setTabAppearance(appearance));
    button = edgeButton(host, side); QVERIFY(button);
    QVERIFY(extent(button->sizeHint()) > expected);
    QCOMPARE(manager.model().tab(side)->title, title);
}

void EdgeTabInteractionTest::oversizedUnclippedTitleStaysWithinTheWidgetSizeLimit()
{
    // Measure only: never allocate a widget or image at this natural extent.
    QFont font = qApp->font(); font.setPixelSize(1024);
    const QString title(24000, QLatin1Char('W'));
    const QFontMetrics metrics(font);
    QVERIFY(metrics.horizontalAdvance(title) > QWIDGETSIZE_MAX);
    QCOMPARE(edgeTabExtent(metrics, title, true, 123), QWIDGETSIZE_MAX);
    for (const bool vertical : {false, true}) {
        EdgeTabButton button(title, vertical, defaultTheme().metrics.edgeThickness, nullptr);
        button.setFont(font);
        const auto size = button.sizeHint();
        QCOMPARE(vertical ? size.height() : size.width(), QWIDGETSIZE_MAX);
        QCOMPARE(vertical ? size.width() : size.height(), defaultTheme().metrics.edgeThickness);
        QCOMPARE(button.text(), title); QCOMPARE(button.toolTip(), title);
    }
}

QTEST_MAIN(EdgeTabInteractionTest)
#include "edge_tab_interaction_test.moc"
