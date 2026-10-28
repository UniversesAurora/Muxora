// SPDX-License-Identifier: MIT
#include "chrome/command_strip.hpp"
#include "chrome/workspace_window.hpp"
#include "docking/dock_surface.hpp"
#include "docking/tab_strip.hpp"
#include "style/compact_tool_button.hpp"

#include <window_system/window_manager.hpp>

#include <QApplication>
#include <QCursor>
#include <QDir>
#include <QEnterEvent>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QPalette>
#include <QPainter>
#include <QPointer>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyleOptionSlider>
#include <QTest>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <memory>

using namespace window_system;

namespace {

class ApplicationPaletteGuard final {
public:
    explicit ApplicationPaletteGuard(bool dark) : previous_(qApp->palette())
    {
        auto palette = previous_;
        palette.setColor(QPalette::Window, dark ? QColor("#171717") : QColor("#eeeeee"));
        palette.setColor(QPalette::Base, dark ? QColor("#202020") : QColor("#ffffff"));
        palette.setColor(QPalette::WindowText, dark ? QColor("#f2f2f2") : QColor("#181818"));
        palette.setColor(QPalette::Text, palette.color(QPalette::WindowText));
        palette.setColor(QPalette::Button, palette.color(QPalette::Window));
        palette.setColor(QPalette::ButtonText, palette.color(QPalette::WindowText));
        qApp->setPalette(palette);
    }
    ~ApplicationPaletteGuard() { qApp->setPalette(previous_); }
private:
    QPalette previous_;
};

void addSystemPalettes()
{
    QTest::addColumn<bool>("dark");
    QTest::newRow("light-system-palette") << false;
    QTest::newRow("dark-system-palette") << true;
}

QRect boundsIn(QWidget* widget, QWidget* parent)
{
    return QRect(widget->mapTo(parent, QPoint()), widget->size());
}

void sendEnter(QWidget* widget)
{
    const QPoint point(20, 5);
    QEnterEvent event(point, widget->mapTo(widget->window(), point), widget->mapToGlobal(point));
    QCoreApplication::sendEvent(widget, &event);
}

} // namespace

class CompactUiTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); }
    void elasticSizingIsAdjacentToResetAndUsesTheHostCommand();
    void protectedSizingDoesNotAppearInEitherMenu();
    void scrollbarOverlaysTheTabsWithoutMovingTheContent_data();
    void scrollbarOverlaysTheTabsWithoutMovingTheContent();
    void groupActionsInvokeTheirOwnCallbacks_data();
    void groupActionsInvokeTheirOwnCallbacks();
    void compactPeekAndEdgeBarsKeepRealContent_data();
    void compactPeekAndEdgeBarsKeepRealContent();
    void groupSeparatorsResizeWithoutReplacingContent_data();
    void groupSeparatorsResizeWithoutReplacingContent();
};

void CompactUiTest::elasticSizingIsAdjacentToResetAndUsesTheHostCommand()
{
    CommandStripHost host;
    host.addStrip("tools", "Tools", new QLabel("Tools"), 0);
    int requests = 0;
    host.setSizingChangeRequest([&](const QString& id, CommandStripHost::StripSizing sizing) {
        QCOMPARE(id, QStringLiteral("tools"));
        ++requests;
        QVERIFY(host.setStripSizing(id, sizing));
    });
    std::unique_ptr<QMenu> menu(host.createOverflowMenu("tools", &host));
    const auto items = menu->actions();
    QCOMPARE(items.size(), 3);
    QCOMPARE(items[0]->objectName(), QStringLiteral("StripResetWidthAction"));
    QCOMPARE(items[1]->objectName(), QStringLiteral("StripElasticAction"));
    QVERIFY(items[1]->isCheckable()); QVERIFY(!items[1]->isChecked());
    auto* customize = menu->findChild<QMenu*>("StripCustomizeMenu", Qt::FindDirectChildrenOnly);
    QVERIFY(customize); QCOMPARE(items[2]->menu(), customize);
    QVERIFY(!customize->findChild<QAction*>("StripElasticAction"));
    QCOMPARE(customize->actions().size(), 1);
    QCOMPARE(customize->actions().first()->data().toString(), QStringLiteral("content"));
    items[1]->trigger();
    QCOMPARE(requests, 1); QCOMPARE(host.stripSizing("tools"), CommandStripHost::StripSizing::Elastic);
    menu.reset(host.createOverflowMenu("tools", &host));
    auto* elastic = menu->findChild<QAction*>("StripElasticAction", Qt::FindDirectChildrenOnly);
    QVERIFY(elastic); QVERIFY(elastic->isChecked());
    elastic->trigger();
    QCOMPARE(requests, 2); QCOMPARE(host.stripSizing("tools"), CommandStripHost::StripSizing::Content);
}

void CompactUiTest::protectedSizingDoesNotAppearInEitherMenu()
{
    CommandStripHost host;
    host.addStrip("required", "Required", new QLabel("Title"), 0);
    host.addStrip("ordinary", "Ordinary", new QLabel("Tools"), 0);
    QVERIFY(host.setStripConstraints("required", {true, true, 96, true}));
    for (const auto& id : {"required", "ordinary"}) {
        std::unique_ptr<QMenu> menu(host.createOverflowMenu(id, &host));
        QVERIFY(menu); QCOMPARE(menu->actions().size(), 2);
        QCOMPARE(menu->actions().first()->objectName(), QStringLiteral("StripResetWidthAction"));
        QVERIFY(menu->actions().last()->menu());
        QVERIFY(!menu->findChild<QAction*>("StripElasticAction"));
    }
}

void CompactUiTest::scrollbarOverlaysTheTabsWithoutMovingTheContent_data() { addSystemPalettes(); }
void CompactUiTest::scrollbarOverlaysTheTabsWithoutMovingTheContent()
{
    // The strip's scrollbar is drawn over its tabs: showing it or scrolling
    // never moves the content, under light and dark system palettes.
    QFETCH(bool, dark);
    ApplicationPaletteGuard palette(dark);
    QWidget root;
    auto* column = new QVBoxLayout(&root); column->setContentsMargins(0, 0, 0, 0);
    auto* tabs = new GroupTabs(defaultTheme(), &root); column->addWidget(tabs);
    for (int index = 0; index < 12; ++index) {
        const auto id = QUuid::createUuid();
        tabs->addTab(new QLabel("Content"), {}, QString("Long tab %1").arg(index), id);
        TabRecord record; record.id = id; record.colorId = "red"; tabs->setTabMetadata(index, record);
    }
    applyTheme(&root); root.resize(380, 240); root.show();
    QVERIFY(QTest::qWaitForWindowExposed(&root));
    auto* bar = tabs->tabBar(); auto* stack = tabs->findChild<QStackedWidget*>(); QVERIFY(stack);
    QTRY_VERIFY(bar->isOverflowing());
    const auto barBefore = boundsIn(bar, tabs), contentBefore = boundsIn(stack, tabs);
    QVERIFY(contentBefore.top() > barBefore.bottom());
    const QPoint inside = bar->displayTabRect(1).center();
    QMouseEvent move(QEvent::MouseMove, QPointF(inside), bar->mapToGlobal(QPointF(inside)), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(bar, &move);
    QCOMPARE(boundsIn(bar, tabs), barBefore); QCOMPARE(boundsIn(stack, tabs), contentBefore);
    const auto selected = tabs->currentIndex();
    bar->setScrollOffset(bar->maximumScrollOffset() / 2);
    QVERIFY(tabs->scrollOffset() > 0); QCOMPARE(tabs->currentIndex(), selected);
    QCOMPARE(boundsIn(stack, tabs), contentBefore);
    for (int width : {520, 280}) {
        root.resize(width, root.height());
        QCoreApplication::processEvents();
        QVERIFY(boundsIn(stack, tabs).top() > boundsIn(bar, tabs).bottom());
    }
    tabs->setTabBarVisible(false);
    QVERIFY(stack->geometry().top() <= bar->metrics().cardMargin);
    tabs->setTabBarVisible(true);
    QVERIFY(boundsIn(stack, tabs).top() > boundsIn(bar, tabs).bottom());
}

void CompactUiTest::groupActionsInvokeTheirOwnCallbacks_data() { addSystemPalettes(); }
void CompactUiTest::groupActionsInvokeTheirOwnCallbacks()
{
    QFETCH(bool, dark);
    ApplicationPaletteGuard palette(dark);
    GroupTabs tabs(defaultTheme(), nullptr);
    tabs.addTab(new QLabel("Content"), {}, "Session", QUuid::createUuid());
    int current = 0; int all = 0;
    tabs.setPinnedActions([&] { ++current; }, [&] { ++all; });
    applyTheme(&tabs); tabs.resize(380, 240); tabs.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    for (const auto& name : {"TabListButton", "UnpinCurrentButton", "UnpinAllButton"}) {
        auto* button = tabs.findChild<QToolButton*>(name); QVERIFY(button);
        QVERIFY(!button->accessibleName().isEmpty());
    }
    auto* currentButton = tabs.findChild<QToolButton*>("UnpinCurrentButton");
    auto* allButton = tabs.findChild<QToolButton*>("UnpinAllButton");
    QTest::mouseClick(currentButton, Qt::LeftButton); QCOMPARE(current, 1); QCOMPARE(all, 0);
    QTest::mouseClick(allButton, Qt::LeftButton); QCOMPARE(current, 1); QCOMPARE(all, 1);
    QCOMPARE(tabs.currentIndex(), 0);
}

void CompactUiTest::compactPeekAndEdgeBarsKeepRealContent_data()
{
    QTest::addColumn<int>("edgeValue");
    QTest::newRow("left") << int(Edge::Left);
    QTest::newRow("right") << int(Edge::Right);
    QTest::newRow("top") << int(Edge::Top);
    QTest::newRow("bottom") << int(Edge::Bottom);
}
void CompactUiTest::compactPeekAndEdgeBarsKeepRealContent()
{
    QFETCH(int, edgeValue); const auto edge = Edge(edgeValue);
    WindowManager manager; const auto id = manager.createWindow();
    const auto center = manager.addTab(id, new QLabel("Center"), {"Center", "label", {}});
    auto visibleGroup = [&](TabId target) -> GroupTabs* {
        for (auto* widget : manager.windowWidget(id)->findChildren<QWidget*>())
            if (auto* group = dynamic_cast<GroupTabs*>(widget); group && group->isVisible()
                && group->property("groupId").toUuid() == manager.model().groupForTab(target)) return group;
        return nullptr;
    };
    manager.showWindow(id);
    auto* central = visibleGroup(center); QVERIFY(central);
    auto* content = new QLabel("Live side content");
    const auto tab = manager.addTab(id, content, {"Side", "label", {}});
    const auto pane = manager.model().tab(tab)->activePane;
    QVERIFY(manager.moveTabToEdge(tab, edge));
    manager.showWindow(id); auto* window = static_cast<WorkspaceWindow*>(manager.windowWidget(id));
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    const auto edgeRect = window->surface()->edgeBarRects().value(edge);
    QVERIFY(!edgeRect.isEmpty());
    auto* pinEdge = window->findChild<QToolButton*>("PinEdgeButton"); QVERIFY(pinEdge);
    QVERIFY(manager.activateTab(tab));
    auto* header = window->findChild<QWidget*>("PeekTitleBar"); QVERIFY(header); QVERIFY(header->isVisible());
    central = visibleGroup(center); QVERIFY(central);
    for (const auto& name : {"PinPeekTabButton", "ClosePeekTabButton"}) {
        auto* button = header->findChild<QToolButton*>(name); QVERIFY(button);
        QVERIFY(!button->accessibleName().isEmpty());
    }
    QCOMPARE(manager.paneWidget(pane), content); QVERIFY(content->isVisible());
    QTest::mouseClick(header->findChild<QToolButton*>("PinPeekTabButton"), Qt::LeftButton);
    QCOMPARE(manager.model().group(manager.model().groupForTab(tab))->location, GroupLocation::Pinned);
    QCOMPARE(manager.paneWidget(pane), content);
    auto* pinned = visibleGroup(tab); QVERIFY(pinned);
    for (const auto& name : {"UnpinCurrentButton", "UnpinAllButton", "TabListButton"}) {
        auto* button = pinned->findChild<QToolButton*>(name); QVERIFY(button);
    }
}

void CompactUiTest::groupSeparatorsResizeWithoutReplacingContent_data()
{
    QTest::addColumn<bool>("vertical");
    QTest::newRow("side-by-side") << false;
    QTest::newRow("stacked") << true;
}

void CompactUiTest::groupSeparatorsResizeWithoutReplacingContent()
{
    QFETCH(bool, vertical);
    WindowManager manager;
    const auto windowId = manager.createWindow();
    auto* firstContent = new QLabel("First");
    const auto first = manager.addTab(windowId, firstContent, {"First", "label", {}});
    const auto firstPane = manager.model().tab(first)->activePane;
    auto* paneContent = new QLabel("Pane");
    QVERIFY(!manager.splitPane(first, firstPane, paneContent, Placement::Right).isNull());
    const auto second = manager.addTab(windowId, new QLabel("Second"), {"Second", "label", {}});
    QVERIFY(manager.moveTab(second, manager.model().groupForTab(first), vertical ? Placement::Bottom : Placement::Right));
    manager.showWindow(windowId);
    auto* window = manager.windowWidget(windowId);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    auto* splitter = window->findChild<QSplitter*>("GroupSplitter"); QVERIFY(splitter);
    auto* paneSplitter = window->findChild<QSplitter*>("PaneSplitter"); QVERIFY(paneSplitter);
    QCOMPARE(splitter->orientation(), vertical ? Qt::Vertical : Qt::Horizontal);
    const auto node = manager.model().window(windowId)->central->id;
    const auto initialRatio = manager.model().window(windowId)->central->ratio;
    auto* handle = splitter->handle(1); QVERIFY(handle);
    QSignalSpy moves(splitter, &QSplitter::splitterMoved);
    const auto start = handle->rect().center();
    const auto delta = vertical ? QPoint(0, 45) : QPoint(45, 0);
    QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(handle, start + delta);
    QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier, start + delta);
    QTRY_VERIFY(!moves.isEmpty());
    QCOMPARE(manager.model().window(windowId)->central->id, node);
    QVERIFY(manager.model().window(windowId)->central->ratio > initialRatio);
    QCOMPARE(manager.paneWidget(firstPane), firstContent);
    QVERIFY(paneContent->isVisible());
    window->resize(window->width() - 100, window->height() - 80);
    QCoreApplication::processEvents();
    const auto paneNode = manager.model().tab(first)->panes->id;
    const auto paneRatio = manager.model().tab(first)->panes->ratio;
    auto* paneHandle = paneSplitter->handle(1); QVERIFY(paneHandle);
    QSignalSpy paneMoves(paneSplitter, &QSplitter::splitterMoved);
    const auto paneStart = paneHandle->rect().center();
    QTest::mousePress(paneHandle, Qt::LeftButton, Qt::NoModifier, paneStart);
    QTest::mouseMove(paneHandle, paneStart + QPoint(30, 0));
    QTest::mouseRelease(paneHandle, Qt::LeftButton, Qt::NoModifier, paneStart + QPoint(30, 0));
    QTRY_VERIFY(!paneMoves.isEmpty());
    QCOMPARE(manager.model().tab(first)->panes->id, paneNode);
    QVERIFY(manager.model().tab(first)->panes->ratio > paneRatio);
    QCOMPARE(manager.paneWidget(firstPane), firstContent);
    QVERIFY(manager.model().validate());
}

QTEST_MAIN(CompactUiTest)
#include "compact_ui_test.moc"
