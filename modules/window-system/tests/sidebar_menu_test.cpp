// SPDX-License-Identifier: MIT
#include "translation_checks.hpp"
#include <window_system/window_manager.hpp>
#include "docking/tab_strip.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPointer>
#include <QTest>
#include <QTimer>
#include <QToolButton>

using namespace window_system;

namespace {

void processDeletes()
{
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}

QAbstractButton* edgeButton(QWidget* window, TabId tab)
{
    for (auto* button : window->findChildren<QAbstractButton*>(QStringLiteral("EdgeButton")))
        if (button->isVisible() && button->property("tabId").toUuid() == tab) return button;
    return nullptr;
}

} // namespace

class SidebarMenuTest final : public QObject {
    Q_OBJECT
    translation_test::ChineseUiCatalog chineseUi_;
private slots:
    void initTestCase();
    void cleanup();
    void peekCloseRemovesTheActualTab_data();
    void peekCloseRemovesTheActualTab();
    void rejectedPeekCloseKeepsThePanelAndContent();
    void sidebarContextMenuUsesTheExistingTabCommands_data();
    void sidebarContextMenuUsesTheExistingTabCommands();
    void sidebarContextMenuCanPinTheClickedInactiveTab();
    void tabListHoverExposesCloseWithoutChangingSelection_data();
    void tabListHoverExposesCloseWithoutChangingSelection();
};

void SidebarMenuTest::initTestCase() { QVERIFY(chineseUi_.install()); qApp->setQuitOnLastWindowClosed(false); }
void SidebarMenuTest::cleanup() { processDeletes(); }

void SidebarMenuTest::peekCloseRemovesTheActualTab_data()
{
    QTest::addColumn<int>("edge");
    QTest::newRow("left") << int(Edge::Left);
    QTest::newRow("right") << int(Edge::Right);
    QTest::newRow("top") << int(Edge::Top);
    QTest::newRow("bottom") << int(Edge::Bottom);
}

void SidebarMenuTest::peekCloseRemovesTheActualTab()
{
    QFETCH(int, edge);
    WindowManager manager;
    const auto window = manager.createWindow();
    QPointer<QPlainTextEdit> content = new QPlainTextEdit;
    const auto tab = manager.addTab(window, content, {QStringLiteral("side"), {}, {}});
    const auto pane = manager.model().tab(tab)->activePane;
    const auto kept = manager.addTab(window, new QLabel(QStringLiteral("kept")), {QStringLiteral("kept"), {}, {}});
    QVERIFY(manager.moveTabToEdge(tab, Edge(edge)));
    int approvals = 0;
    QVector<TabId> approved;
    manager.setCloseGuard([&](const QVector<TabId>& tabs) {
        ++approvals; approved = tabs; return true;
    });
    manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    QVERIFY(manager.activateTab(tab));
    QPointer<QWidget> peek = manager.windowWidget(window)->findChild<QWidget*>(QStringLiteral("PeekPanel"));
    QVERIFY(peek);
    auto* close = peek->findChild<QToolButton*>(QStringLiteral("ClosePeekTabButton"));
    QVERIFY(close);
    QCOMPARE(close->toolTip(), QStringLiteral("关闭标签"));
    close->click();
    QCOMPARE(approvals, 1);
    QCOMPARE(approved, QVector<TabId>{tab});
    QVERIFY(!manager.model().tab(tab));
    QVERIFY(manager.model().tab(kept));
    QVERIFY(peek.isNull());
    QVERIFY(!edgeButton(manager.windowWidget(window), tab));
    QVERIFY(!manager.paneWidget(pane));
    processDeletes();
    QVERIFY(content.isNull());
    QVERIFY(manager.model().validate().ok);
}

void SidebarMenuTest::rejectedPeekCloseKeepsThePanelAndContent()
{
    WindowManager manager;
    const auto window = manager.createWindow();
    QPointer<QPlainTextEdit> content = new QPlainTextEdit;
    content->setPlainText(QStringLiteral("retained contents"));
    const auto tab = manager.addTab(window, content, {QStringLiteral("guarded"), {}, {}});
    const auto pane = manager.model().tab(tab)->activePane;
    QVERIFY(manager.moveTabToEdge(tab, Edge::Left));
    int approvals = 0;
    manager.setCloseGuard([&](const QVector<TabId>& tabs) {
        ++approvals; return tabs != QVector<TabId>{tab};
    });
    manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    QVERIFY(manager.activateTab(tab));
    QPointer<QWidget> peek = manager.windowWidget(window)->findChild<QWidget*>(QStringLiteral("PeekPanel"));
    QVERIFY(peek);
    auto* close = peek->findChild<QToolButton*>(QStringLiteral("ClosePeekTabButton"));
    QVERIFY(close);
    close->click();
    processDeletes();
    QCOMPARE(approvals, 1);
    QVERIFY(manager.model().tab(tab));
    QVERIFY(peek && peek->isVisible());
    QVERIFY(content && content->isVisible());
    QCOMPARE(manager.paneWidget(pane), content.data());
    QCOMPARE(content->toPlainText(), QStringLiteral("retained contents"));
    QVERIFY(peek->isAncestorOf(content));
    QVERIFY(edgeButton(manager.windowWidget(window), tab));
}

void SidebarMenuTest::sidebarContextMenuUsesTheExistingTabCommands_data()
{
    QTest::addColumn<int>("edge");
    QTest::newRow("left") << int(Edge::Left);
    QTest::newRow("right") << int(Edge::Right);
    QTest::newRow("top") << int(Edge::Top);
    QTest::newRow("bottom") << int(Edge::Bottom);
}

void SidebarMenuTest::sidebarContextMenuUsesTheExistingTabCommands()
{
    QFETCH(int, edge);
    WindowManager manager;
    const auto window = manager.createWindow();
    const auto tab = manager.addTab(window, new QLabel(QStringLiteral("side")), {QStringLiteral("side"), {}, {}});
    const auto active = manager.addTab(window, new QLabel(QStringLiteral("central")), {QStringLiteral("central"), {}, {}});
    QVERIFY(manager.moveTabToEdge(tab, Edge(edge)));
    manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    QVERIFY(manager.activateTab(active));
    const auto original = manager.model().toJson();
    auto* button = edgeButton(manager.windowWidget(window), tab);
    QVERIFY(button);
    bool observed = false;
    bool timedOut = false;
    QStringList commandIds;
    bool selectionKept = false;
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, [&] {
        timedOut = true;
        if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) menu->close();
    });
    watchdog.start(1000);
    QTimer::singleShot(30, &manager, [&] {
        if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
            observed = true;
            selectionKept = manager.context(window).tab == active
                && manager.model().toJson() == original
                && !manager.windowWidget(window)->findChild<QWidget*>(QStringLiteral("PeekPanel"));
            for (auto* action : menu->findChildren<QAction*>())
                if (action->isVisible()) commandIds.append(action->data().toString());
            QTest::keyClick(menu, Qt::Key_Escape);
        }
    });
    QTest::mousePress(button, Qt::RightButton); QTest::mouseRelease(button, Qt::RightButton);
    QContextMenuEvent event(QContextMenuEvent::Mouse, button->rect().center(), button->mapToGlobal(button->rect().center()));
    QApplication::sendEvent(button, &event);
    watchdog.stop();
    QVERIFY(observed);
    QVERIFY(selectionKept);
    QVERIFY(!timedOut);
    QVERIFY(commandIds.contains(QStringLiteral("workspace.closeTab")));
    QVERIFY(commandIds.contains(QStringLiteral("workspace.closeTabs.left")));
    QVERIFY(commandIds.contains(QStringLiteral("workspace.closeTabs.right")));
    QVERIFY(commandIds.contains(QStringLiteral("workspace.closeTabs.others")));
    QVERIFY(commandIds.contains(QStringLiteral("workspace.closeTabs.all")));
    QVERIFY(commandIds.contains(QStringLiteral("workspace.detachFloating")));
    QVERIFY(commandIds.contains(QStringLiteral("workspace.pinTab")));
    QVERIFY(!commandIds.contains(QStringLiteral("workspace.unpinTab")));
    QVERIFY(manager.model().tab(tab));
    QCOMPARE(manager.context(window).tab, active); QCOMPARE(manager.model().toJson(), original);
    auto* peek = manager.windowWidget(window)->findChild<QWidget*>(QStringLiteral("PeekPanel"));
    QVERIFY(!peek);
}

void SidebarMenuTest::sidebarContextMenuCanPinTheClickedInactiveTab()
{
    WindowManager manager;
    const auto window = manager.createWindow();
    QPointer<QLabel> clickedContent = new QLabel(QStringLiteral("clicked"));
    const auto clicked = manager.addTab(window, clickedContent, {QStringLiteral("clicked"), {}, {}});
    const auto other = manager.addTab(window, new QLabel(QStringLiteral("other")), {QStringLiteral("other"), {}, {}});
    QVERIFY(manager.moveTabToEdge(clicked, Edge::Left));
    QVERIFY(manager.moveTab(other, manager.model().groupForTab(clicked)));
    manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    QVERIFY(manager.activateTab(other));
    auto* button = edgeButton(manager.windowWidget(window), clicked);
    QVERIFY(button);
    bool observed = false;
    bool inactiveDuringMenu = false;
    bool timedOut = false;
    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, [&] {
        timedOut = true;
        if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) menu->close();
    });
    watchdog.start(1000);
    QTimer::singleShot(30, &manager, [&] {
        if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget()))
            for (auto* action : menu->actions()) if (action->data().toString() == QStringLiteral("workspace.pinTab")) {
                observed = true;
                inactiveDuringMenu = manager.context(window).tab == other && !clickedContent->isVisible();
                menu->setActiveAction(action);
                QTest::keyClick(menu, Qt::Key_Return);
                break;
            }
    });
    QContextMenuEvent event(QContextMenuEvent::Keyboard, button->rect().center(), button->mapToGlobal(button->rect().center()));
    QApplication::sendEvent(button, &event);
    watchdog.stop();
    QVERIFY(observed);
    QVERIFY(inactiveDuringMenu);
    QVERIFY(!timedOut);
    QTRY_COMPARE(manager.model().group(manager.model().groupForTab(clicked))->location, GroupLocation::Pinned);
    QCOMPARE(manager.model().group(manager.model().groupForTab(other))->location, GroupLocation::AutoHide);
    QCOMPARE(manager.paneWidget(manager.model().tab(clicked)->activePane), clickedContent.data());
    QVERIFY(clickedContent);
}

void SidebarMenuTest::tabListHoverExposesCloseWithoutChangingSelection_data()
{
    QTest::addColumn<bool>("dark");
    QTest::newRow("light") << false;
    QTest::newRow("dark") << true;
}

void SidebarMenuTest::tabListHoverExposesCloseWithoutChangingSelection()
{
    QFETCH(bool, dark);
    Theme theme;
    theme.surface = QColor(dark ? "#20232B" : "#F3F7FB");
    theme.text = QColor(dark ? "#E8EDF4" : "#223246");
    theme.selection = QColor(dark ? "#34558C" : "#BCD2F5");
    theme.selectionText = QColor(dark ? "#FFFFFF" : "#13243B");
    GroupTabs tabs(theme, nullptr);
    tabs.addTab(new QLabel(QStringLiteral("first content")), {}, QStringLiteral("First visible tab title"), QUuid::createUuid());
    tabs.addTab(new QLabel(QStringLiteral("second content")), {}, QStringLiteral("Second visible tab title"), QUuid::createUuid());
    tabs.setCurrentIndex(1);
    tabs.resize(460, 260);
    applyTheme(&tabs, theme);
    tabs.setWindowFlag(Qt::WindowStaysOnTopHint);
    tabs.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    auto* list = tabs.findChild<QToolButton*>(QStringLiteral("TabListButton"));
    QVERIFY(list);
    QTest::mouseClick(list, Qt::LeftButton);
    QTRY_VERIFY(QApplication::activePopupWidget());
    auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
    QVERIFY(menu);
    const auto rows = menu->findChildren<QWidget*>(QStringLiteral("TabListRow"));
    QCOMPARE(rows.size(), 2);
    auto* first = rows.first();
    auto* second = rows.last();
    auto* close = first->findChild<QToolButton*>();
    QVERIFY(close);
    QTest::mouseMove(first, QPoint(3, first->height() / 2));
    QTRY_VERIFY(first->property("emphasized").toBool());
    QTRY_VERIFY(close->isVisible());
    QTest::mouseMove(close, close->rect().center());
    QTest::mouseMove(second, QPoint(3, second->height() / 2));
    QTRY_VERIFY(!first->property("emphasized").toBool());
    QTRY_VERIFY(!close->isVisible());
    QTest::mouseMove(first, QPoint(3, first->height() / 2));
    QCOMPARE(tabs.currentIndex(), 1);
    QTest::keyClick(menu, Qt::Key_Escape);
    QTRY_VERIFY(!QApplication::activePopupWidget());
}

QTEST_MAIN(SidebarMenuTest)
#include "sidebar_menu_test.moc"
