// SPDX-License-Identifier: MIT
#include "chrome/workspace_window.hpp"
#include "chrome/command_strip.hpp"
#include "docking/dock_surface.hpp"
#include "docking/tab_strip.hpp"
#include "style/tab_label_geometry.hpp"
#include <window_system/window_manager.hpp>
#include <QAction>
#include <QApplication>
#include <QJsonArray>
#include <QLabel>
#include <QMenu>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>
#include <QToolButton>

using namespace window_system;

class SidebarPresentationTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); }
    void hidingKeepsPinnedContentAndCentralTabs_data();
    void hidingKeepsPinnedContentAndCentralTabs();
    void firstPinUsesTheOuterPartition_data();
    void firstPinUsesTheOuterPartition();
    void bulkFirstPinPreservesTheNewGroupOrder_data();
    void bulkFirstPinPreservesTheNewGroupOrder();
    void visibilityRestoresAndRejectsMalformedSnapshots();
    void hiddenRailsDoNotOccupySpaceWhenExplicitlyOpeningATab();
    void pinnedIconsAndNumbersUseSidebarPreferences_data();
    void pinnedIconsAndNumbersUseSidebarPreferences();
};

void SidebarPresentationTest::hidingKeepsPinnedContentAndCentralTabs_data()
{
    QTest::addColumn<int>("edge"); QTest::addColumn<bool>("floating");
    for (int edge = 0; edge < 4; ++edge) for (bool floating : {false, true})
        QTest::newRow(qPrintable(QString("edge-%1-%2").arg(edge).arg(floating))) << edge << floating;
}
void SidebarPresentationTest::hidingKeepsPinnedContentAndCentralTabs()
{
    QFETCH(int, edge); QFETCH(bool, floating);
    WindowManager manager; const auto root = manager.createWindow();
    const auto id = floating ? manager.createWindow(WindowKind::Floating, {}, root) : root;
    const auto central = manager.addTab(id, new QLabel("central"), {"central", "label", {}});
    auto* pinnedContent = new QLabel("pinned");
    const auto pinned = manager.addTab(id, pinnedContent, {"pinned", "label", {}});
    QVERIFY(manager.moveTabToEdge(pinned, Edge(edge), true));
    auto* hiddenContent = new QLabel("hidden");
    const auto hidden = manager.addTab(id, hiddenContent, {"hidden", "label", {}});
    QVERIFY(manager.moveTabToEdge(hidden, Edge(edge)));
    manager.showWindow(id); auto* window = static_cast<WorkspaceWindow*>(manager.windowWidget(id));
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QVERIFY(manager.activateTab(hidden)); QVERIFY(window->findChild<QWidget*>("PeekPanel"));
    const auto model = manager.model().toJson();
    QSignalSpy changes(&manager, &WindowManager::layoutChanged);
    QVERIFY(manager.execute("window.sideBars.toggle", {}, id));
    QVERIFY(!manager.sideBarsVisible(id)); QVERIFY(manager.tabBarsVisible(id));
    QVERIFY(window->surface()->edgeBarRects().isEmpty());
    QVERIFY(!window->findChild<QWidget*>("PeekPanel"));
    QCOMPARE(window->surface()->tabBarRects().size(), 2);
    QVERIFY(pinnedContent->isVisible()); QVERIFY(!hiddenContent->isVisible());
    QCOMPARE(manager.model().toJson(), model); QVERIFY(!changes.isEmpty());
    const auto* command = manager.commands().find("window.sideBars.toggle"); QVERIFY(command);
    QCOMPARE(command->shortcut, QKeySequence("Ctrl+Alt+B")); QVERIFY(command->enabled(manager.context(id)));
    // Both window kinds expose a menu action bound to their own display state.
    auto* toggle = window->findChild<QAction*>("window.sideBars.toggle"); QVERIFY(toggle);
    QVERIFY(toggle->isCheckable()); auto* menu = qobject_cast<QMenu*>(toggle->parent()); QVERIFY(menu);
    QMetaObject::invokeMethod(menu, "aboutToShow"); QVERIFY(!toggle->isChecked());
    if (floating) QCOMPARE(window->strips()->stripIds(), (QStringList{"floating-menu", "title", "caption-controls"}));
    // Rebuild while hidden: new rails inherit the preference, pinned content
    // and its identity survive, and tab-bar visibility remains independent.
    const auto extra = manager.addTab(id, new QLabel("extra"), {"extra", "label", {}});
    QVERIFY(manager.moveTabToEdge(extra, Edge(edge)));
    QVERIFY(window->surface()->edgeBarRects().isEmpty());
    QCOMPARE(manager.paneWidget(manager.model().tab(pinned)->activePane), pinnedContent);
    QVERIFY(manager.setTabBarsVisible(id, false));
    QCOMPARE(window->surface()->tabBarRects().size(), 1);
    QVERIFY(manager.setSideBarsVisible(id, true));
    QCOMPARE(window->surface()->edgeBarRects().size(), 1);
    QCOMPARE(window->surface()->tabBarRects().size(), 1);
    QVERIFY(manager.activateTab(central));
    QTest::keyClick(window, Qt::Key_B, Qt::ControlModifier | Qt::AltModifier);
    QVERIFY(!manager.sideBarsVisible(id)); QVERIFY(!manager.tabBarsVisible(id));
    if (floating) QVERIFY(manager.sideBarsVisible(root));
}

void SidebarPresentationTest::firstPinUsesTheOuterPartition_data()
{
    QTest::addColumn<int>("edge"); QTest::addColumn<QString>("method");
    for (int edge = 0; edge < 4; ++edge) for (const auto& method : {"peek", "group", "all", "multi"})
        QTest::newRow(qPrintable(QString("%1-%2").arg(edge).arg(method))) << edge << QString(method);
}
void SidebarPresentationTest::firstPinUsesTheOuterPartition()
{
    QFETCH(int, edge); QFETCH(QString, method);
    WindowManager manager; const auto id = manager.createWindow();
    manager.addTab(id, new QLabel("central"), {"central", "label", {}});
    const auto existing = manager.addTab(id, new QLabel("existing"), {"existing", "label", {}});
    QVERIFY(manager.moveTabToEdge(existing, Edge(edge), true));
    const auto tab = manager.addTab(id, new QLabel("new"), {"new", "label", {}});
    const auto pane = manager.model().tab(tab)->activePane;
    auto* content = manager.paneWidget(pane);
    QVERIFY(manager.moveTabToEdge(tab, Edge(edge)));
    TabId peer;
    if (method == "multi") {
        peer = manager.addTab(id, new QLabel("peer"), {"peer", "label", {}});
        QVERIFY(manager.moveTab(peer, manager.model().groupForTab(tab)));
    }
    manager.showWindow(id); auto* window = static_cast<WorkspaceWindow*>(manager.windowWidget(id));
    QVERIFY(QTest::qWaitForWindowExposed(window));
    if (method == "peek") {
        QVERIFY(manager.activateTab(tab));
        auto* pin = window->findChild<QToolButton*>("PinPeekTabButton"); QVERIFY(pin); pin->click();
    } else if (method == "group") QVERIFY(manager.pinGroup(manager.model().groupForTab(tab), true));
    else if (method == "all") QVERIFY(manager.pinEdge(id, Edge(edge), true));
    else QVERIFY(manager.pinTab(tab, true));
    const auto group = manager.model().groupForTab(tab);
    const auto previous = manager.model().groupForTab(existing);
    QCOMPARE(manager.model().group(group)->location, GroupLocation::Pinned);
    const auto order = manager.model().window(id)->edgeGroups;
    QVERIFY(order.indexOf(group) < order.indexOf(previous));
    const auto bounds = window->surface()->groupRects();
    const auto current = bounds.value(group); const auto other = bounds.value(previous);
    QVERIFY(!current.isEmpty()); QVERIFY(!other.isEmpty());
    if (Edge(edge) == Edge::Left) QVERIFY(current.right() < other.left());
    if (Edge(edge) == Edge::Right) QVERIFY(current.left() > other.right());
    if (Edge(edge) == Edge::Top) QVERIFY(current.bottom() < other.top());
    if (Edge(edge) == Edge::Bottom) QVERIFY(current.top() > other.bottom());
    QCOMPARE(manager.paneWidget(pane), content);
    if (!peer.isNull()) QCOMPARE(manager.model().group(manager.model().groupForTab(peer))->location, GroupLocation::AutoHide);
    // An existing inner partition must return to its old rank, not jump out.
    QVERIFY(manager.pinTab(existing, false)); QVERIFY(manager.pinTab(existing, true));
    QVERIFY(manager.model().window(id)->edgeGroups.indexOf(group)
        < manager.model().window(id)->edgeGroups.indexOf(manager.model().groupForTab(existing)));
    if (method == "group") {
        const auto previousGroup = manager.model().groupForTab(existing);
        const auto previousIndex = manager.model().window(id)->edgeGroups.indexOf(previousGroup);
        QVERIFY(manager.pinGroup(previousGroup, false)); QVERIFY(manager.pinGroup(previousGroup, true));
        QCOMPARE(manager.model().window(id)->edgeGroups.indexOf(previousGroup), previousIndex);
    }
    QVERIFY(manager.model().validate());
}

void SidebarPresentationTest::bulkFirstPinPreservesTheNewGroupOrder_data()
{
    QTest::addColumn<int>("edge");
    for (int edge = 0; edge < 4; ++edge) QTest::newRow(qPrintable(QString::number(edge))) << edge;
}
void SidebarPresentationTest::bulkFirstPinPreservesTheNewGroupOrder()
{
    QFETCH(int, edge);
    WindowManager manager; const auto id = manager.createWindow();
    const auto existing = manager.addTab(id, new QLabel("existing"), {"existing", "label", {}});
    QVERIFY(manager.moveTabToEdge(existing, Edge(edge), true));
    const auto a = manager.addTab(id, new QLabel("a"), {"a", "label", {}});
    const auto b = manager.addTab(id, new QLabel("b"), {"b", "label", {}});
    QVERIFY(manager.moveTabToEdge(a, Edge(edge))); QVERIFY(manager.moveTabToEdge(b, Edge(edge)));
    const auto old = manager.model().groupForTab(existing);
    const auto first = manager.model().groupForTab(a); const auto second = manager.model().groupForTab(b);
    QVERIFY(manager.pinEdge(id, Edge(edge), true));
    QCOMPARE(manager.model().window(id)->edgeGroups, (QVector<GroupId>{first, second, old}));
    QVERIFY(manager.pinEdge(id, Edge(edge), false)); QVERIFY(manager.pinEdge(id, Edge(edge), true));
    QCOMPARE(manager.model().window(id)->edgeGroups, (QVector<GroupId>{first, second, old}));
    QVERIFY(manager.model().validate());
}

void SidebarPresentationTest::visibilityRestoresAndRejectsMalformedSnapshots()
{
    WindowManager manager; const auto id = manager.createWindow(WindowKind::Workbench, {"Hidden", {900, 600}, true, false});
    const auto tab = manager.addTab(id, new QLabel("side"), {"side", "label", {}});
    QVERIFY(manager.moveTabToEdge(tab, Edge::Right));
    auto snapshot = manager.saveLayout();
    WindowManager restored; restored.setContentFactory([](const TabOptions&, PaneId) { return new QLabel("restored"); });
    QVERIFY(restored.restoreLayout(snapshot)); QVERIFY(!restored.sideBarsVisible(id));
    auto* window = static_cast<WorkspaceWindow*>(restored.windowWidget(id));
    QVERIFY(QTest::qWaitForWindowExposed(window)); QVERIFY(window->surface()->edgeBarRects().isEmpty());
    const auto before = restored.saveLayout();
    auto windows = snapshot.value("windows").toArray(); auto record = windows[0].toObject();
    record.insert("sideBarsVisible", "false"); windows[0] = record; snapshot.insert("windows", windows);
    QVERIFY(!restored.restoreLayout(snapshot)); QCOMPARE(restored.saveLayout(), before);
    QVERIFY(!restored.setSideBarsVisible({}, true)); QVERIFY(!restored.sideBarsVisible({}));
    record.remove("sideBarsVisible"); windows[0] = record; snapshot.insert("windows", windows);
    QVERIFY(restored.restoreLayout(snapshot)); QVERIFY(restored.sideBarsVisible(id));
    QCOMPARE(window->surface()->edgeBarRects().size(), 1);
}
void SidebarPresentationTest::hiddenRailsDoNotOccupySpaceWhenExplicitlyOpeningATab()
{
    WindowManager manager; const auto id = manager.createWindow();
    const auto tab = manager.addTab(id, new QLabel("side"), {"side", "label", {}});
    QVERIFY(manager.moveTabToEdge(tab, Edge::Left)); manager.showWindow(id);
    auto* window = static_cast<WorkspaceWindow*>(manager.windowWidget(id));
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QVERIFY(manager.setSideBarsVisible(id, false)); QVERIFY(manager.activateTab(tab));
    auto* peek = window->findChild<QWidget*>("PeekPanel"); QVERIFY(peek); QVERIFY(peek->isVisible());
    QCOMPARE(peek->geometry().left(), 0); QVERIFY(window->surface()->edgeBarRects().isEmpty());
}
void SidebarPresentationTest::pinnedIconsAndNumbersUseSidebarPreferences_data()
{
    QTest::addColumn<int>("edge");
    for (int edge = 0; edge < 4; ++edge) QTest::newRow(qPrintable(QString::number(edge))) << edge;
}

void SidebarPresentationTest::pinnedIconsAndNumbersUseSidebarPreferences()
{
    QFETCH(int, edge);
    WindowManager manager; const auto id = manager.createWindow();
    const auto ordinary = manager.addTab(id, new QLabel("central"), {"Central", "label", {}});
    const auto side = manager.addTab(id, new QLabel("side"), {"Sidebar", "label", {}});
    QVERIFY(manager.moveTabToEdge(side, Edge(edge), true));
    const auto group = manager.model().groupForTab(side); const auto centralGroup = manager.model().groupForTab(ordinary);
    const auto pane = manager.model().tab(side)->activePane; QPointer<QWidget> content(manager.paneWidget(pane));
    manager.showWindow(id); auto* host = manager.windowWidget(id); QVERIFY(QTest::qWaitForWindowExposed(host));
    const auto barFor = [host](GroupId target) -> TabBar* {
        for (auto* frame : host->findChildren<QFrame*>("TabGroup")) {
            if (!frame->isVisible() || frame->property("groupId").toUuid() != target) continue;
            return static_cast<GroupTabs*>(frame)->tabBar();
        }
        return nullptr;
    };
    const auto width = [barFor](GroupId target) {
        auto* bar = barFor(target); return bar ? bar->displayTabRect(0).width() : 0;
    };
    QVERIFY(barFor(group));
    // Each shown part adds its size and one part gap (tab strip design 2.1).
    QTRY_VERIFY(width(group) > 0);
    const int sidePlain = width(group);
    const int icon = manager.theme().metrics.iconSize + tabPartGap;
    QVERIFY(!manager.tabAppearance().showSidebarIcons); QVERIFY(!manager.tabAppearance().showSidebarNumbers);
    auto appearance = manager.tabAppearance(); appearance.showIcons = false; appearance.showNumbers = true;
    QVERIFY(manager.setTabAppearance(appearance)); QTRY_COMPARE(width(group), sidePlain);
    const int ordinaryWidth = width(centralGroup); QVERIFY(ordinaryWidth > 0);
    appearance.showSidebarIcons = true; QVERIFY(manager.setTabAppearance(appearance));
    QTRY_COMPARE(width(group), sidePlain + icon);
    QCOMPARE(width(centralGroup), ordinaryWidth);
    appearance.showSidebarNumbers = true; QVERIFY(manager.setTabAppearance(appearance));
    const int numberWidth = barFor(group)->fontMetrics().horizontalAdvance("1") + tabPartGap;
    QTRY_COMPARE(width(group), sidePlain + icon + numberWidth);
    QCOMPARE(width(centralGroup), ordinaryWidth);
    QVERIFY(manager.setTabIconVisible(side, false)); QTRY_COMPARE(width(group), sidePlain + numberWidth);
    QVERIFY(manager.setTabIconVisible(side, true)); QTRY_COMPARE(width(group), sidePlain + icon + numberWidth);
    appearance.showSidebarIcons = false; appearance.showSidebarNumbers = false;
    QVERIFY(manager.setTabAppearance(appearance)); QTRY_COMPARE(width(group), sidePlain);
    QCOMPARE(width(centralGroup), ordinaryWidth); QCOMPARE(manager.model().groupForTab(side), group);
    QCOMPARE(manager.paneWidget(pane), content.data()); QVERIFY(content); QVERIFY(manager.model().validate());
}

QTEST_MAIN(SidebarPresentationTest)
#include "sidebar_presentation_test.moc"
