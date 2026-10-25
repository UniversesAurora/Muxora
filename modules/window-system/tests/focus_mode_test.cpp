// SPDX-License-Identifier: MIT
#include "chrome/workspace_window.hpp"
#include "docking/dock_surface.hpp"
#include "docking/tab_strip.hpp"
#include "panes/tab_content.hpp"
#include <window_system/window_manager.hpp>

#include <QApplication>
#include <QAbstractButton>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QSet>
#include <QSplitter>
#include <QTest>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <functional>
#include <memory>

using namespace window_system;

namespace {

class ParentChangeCallback final : public QObject {
public:
    std::function<void()> changed;
protected:
    bool eventFilter(QObject*, QEvent* event) override
    {
        if (event->type() == QEvent::ParentChange) {
            const auto callback = changed;
            if (callback) callback();
        }
        return false;
    }
};

WorkspaceWindow* workspace(WindowManager& manager, WindowId id)
{
    return static_cast<WorkspaceWindow*>(manager.windowWidget(id));
}

TabContent* tabView(QWidget* content)
{
    for (auto* parent = content->parentWidget(); parent; parent = parent->parentWidget())
        if (auto* tab = dynamic_cast<TabContent*>(parent)) return tab;
    return nullptr;
}

QRect globalBounds(QWidget* widget)
{
    return {widget->mapToGlobal(QPoint()), widget->size()};
}

// `inner` lies inside `outer`, the same few pixels (the workspace margin
// around group cards) from each side.
bool insetEqually(QRect inner, QRect outer)
{
    const int margin = inner.left() - outer.left();
    return margin >= 0 && margin <= 8 && inner == outer.adjusted(margin, margin, -margin, -margin);
}
QRect contentBounds(DockSurface* surface)
{
    auto available = globalBounds(surface);
    const auto rails = surface->edgeBarRects();
    // Content stays flush with each rail; only perpendicular rail junctions
    // expose spacing. Missing or hidden rails consume no workspace area.
    if (rails.contains(Edge::Left)) available.setLeft(rails.value(Edge::Left).right() + 1);
    if (rails.contains(Edge::Right)) available.setRight(rails.value(Edge::Right).left() - 1);
    if (rails.contains(Edge::Top)) available.setTop(rails.value(Edge::Top).bottom() + 1);
    if (rails.contains(Edge::Bottom)) available.setBottom(rails.value(Edge::Bottom).top() - 1);
    return available;
}

QLabel* bar(const QString& text)
{
    auto* result = new QLabel(text);
    result->setFixedHeight(22);
    return result;
}

void settle()
{
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}

} // namespace

class FocusModeTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); }
    void cleanup() { settle(); }
    void groupScopeProjectsTheSameWidgets_data();
    void groupScopeProjectsTheSameWidgets();
    void paneScopePreservesNestedSplitsAndBars_data();
    void paneScopePreservesNestedSplitsAndBars();
    void zenHidesAndRestoresEveryContentBar_data();
    void zenHidesAndRestoresEveryContentBar();
    void contentScopesAreIndependentAcrossWindows();
    void groupScopeSurvivesSelectingAnotherTab();
    void paneScopeFollowsSameWindowMovement();
    void closingTheTargetRestoresRemainingContent();
    void activatingRailOutsideScopeRestoresWindowScope_data();
    void activatingRailOutsideScopeRestoresWindowScope();
    void headerHidingKeepsLocationSemantics_data();
    void headerHidingKeepsLocationSemantics();
    void hidingFocusedBarsPreservesTheActiveContent_data();
    void hidingFocusedBarsPreservesTheActiveContent();
    void inactiveWindowKeepsItsContentFocusWithoutStealingTheOtherWindow();
    void autoHideActivationRevalidatesAfterHostNotification_data();
    void autoHideActivationRevalidatesAfterHostNotification();
    void closingAnExistingPeekPreservesTheNewerRequest();
};

void FocusModeTest::groupScopeProjectsTheSameWidgets_data()
{
    QTest::addColumn<int>("location");
    QTest::addColumn<int>("edge");
    QTest::newRow("central") << int(GroupLocation::Central) << int(Edge::Left);
    for (const auto location : {GroupLocation::Pinned, GroupLocation::AutoHide})
        for (int edge = 0; edge < 4; ++edge)
            QTest::newRow(qPrintable(QString("location-%1-edge-%2").arg(int(location)).arg(edge))) << int(location) << edge;
}

void FocusModeTest::groupScopeProjectsTheSameWidgets()
{
    QFETCH(int, location); QFETCH(int, edge);
    WindowManager manager;
    const auto id = manager.createWindow();
    auto* first = new QPlainTextEdit("first retained content");
    auto* target = new QPlainTextEdit("target retained content");
    auto* third = new QPlainTextEdit("third retained content");
    const auto firstTab = manager.addTab(id, first, {"first", "test.editor", {}});
    const auto targetTab = manager.addTab(id, target, {"target", "test.editor", {}});
    const auto thirdTab = manager.addTab(id, third, {"third", "test.editor", {}});
    QVERIFY(manager.moveTab(thirdTab, manager.model().groupForTab(firstTab), Placement::Bottom));
    if (GroupLocation(location) == GroupLocation::Central)
        QVERIFY(manager.moveTab(targetTab, manager.model().groupForTab(firstTab), Placement::Right));
    else
        QVERIFY(manager.moveTabToEdge(targetTab, Edge(edge), GroupLocation(location) == GroupLocation::Pinned));
    const auto railTab = manager.addTab(id, new QLabel("unrelated rail"), {"rail", {}, {}});
    QVERIFY(manager.moveTabToEdge(railTab, Edge::Bottom));
    const auto targetPane = manager.model().tab(targetTab)->activePane;
    manager.showWindow(id);
    QVERIFY(QTest::qWaitForWindowExposed(workspace(manager, id)));
    QVERIFY(manager.activateTab(targetTab)); settle();
    const auto model = manager.model().toJson();
    const auto group = manager.model().groupForTab(targetTab);
    const auto railEdges = workspace(manager, id)->surface()->edgeBarRects().keys();
    auto* page = tabView(target); QVERIFY(page);
    QPointer<TabContent> originalPage(page);
    QVERIFY(manager.setViewMode(id, ViewMode::GroupFullscreen)); settle();
    auto* surface = workspace(manager, id)->surface();
    QCOMPARE(surface->groupRects().size(), 1);
    QVERIFY(insetEqually(surface->groupRects().value(group), contentBounds(surface)));
    const auto focusedRailEdges = surface->edgeBarRects().keys();
    QCOMPARE(QSet<Edge>(focusedRailEdges.cbegin(), focusedRailEdges.cend()),
        QSet<Edge>(railEdges.cbegin(), railEdges.cend()));
    QCOMPARE(surface->tabBarRects().size(), 1);
    QVERIFY(target->isVisible()); QVERIFY(!first->isVisible()); QVERIFY(!third->isVisible());
    QCOMPARE(manager.paneWidget(targetPane), target); QCOMPARE(tabView(target), originalPage.data());
    QCOMPARE(target->toPlainText(), QStringLiteral("target retained content"));
    QCOMPARE(manager.model().toJson(), model);
    QVERIFY(manager.setSideBarsVisible(id, false)); settle();
    QVERIFY(surface->edgeBarRects().isEmpty());
    QVERIFY(insetEqually(surface->groupRects().value(group), globalBounds(surface)));
    QCOMPARE(manager.viewMode(id), ViewMode::GroupFullscreen);
    QVERIFY(manager.setSideBarsVisible(id, true)); settle();
    QVERIFY(insetEqually(surface->groupRects().value(group), contentBounds(surface)));
    QVERIFY(manager.setTabBarsVisible(id, false)); settle();
    QCOMPARE(surface->tabBarRects().size(), GroupLocation(location) == GroupLocation::Central ? 0 : 1);
    QVERIFY(target->isVisible());
    QCOMPARE(manager.viewMode(id), ViewMode::GroupFullscreen);
    QVERIFY(manager.setTabBarsVisible(id, true)); settle();
    QCOMPARE(surface->tabBarRects().size(), 1);
    target->appendPlainText("still editable in focused scope");
    QVERIFY(manager.setViewMode(id, ViewMode::Normal)); settle();
    QCOMPARE(manager.paneWidget(targetPane), target); QCOMPARE(tabView(target), originalPage.data());
    QVERIFY(first->isVisible()); QVERIFY(third->isVisible());
    if (GroupLocation(location) == GroupLocation::AutoHide) {
        QVERIFY(manager.activateTab(targetTab)); settle();
    }
    QVERIFY(target->isVisible());
    QCOMPARE(manager.model().toJson(), model);
    QCOMPARE(target->toPlainText(), QStringLiteral("target retained content\nstill editable in focused scope"));
}

void FocusModeTest::paneScopePreservesNestedSplitsAndBars_data()
{
    QTest::addColumn<bool>("sharedTop"); QTest::addColumn<bool>("sharedBottom");
    for (bool top : {false, true}) for (bool bottom : {false, true})
        QTest::newRow(qPrintable(QString("top-%1-bottom-%2").arg(top).arg(bottom))) << top << bottom;
}

void FocusModeTest::paneScopePreservesNestedSplitsAndBars()
{
    QFETCH(bool, sharedTop); QFETCH(bool, sharedBottom);
    WindowManager manager;
    const auto id = manager.createWindow();
    TabOptions options{"nested", "test.editor", {}};
    options.topBarSharing = sharedTop ? PaneBarSharing::Shared : PaneBarSharing::PerPane;
    options.bottomBarSharing = sharedBottom ? PaneBarSharing::Shared : PaneBarSharing::PerPane;
    auto* first = new QPlainTextEdit("first");
    const auto tab = manager.addTab(id, first, options);
    const auto firstPane = manager.model().tab(tab)->activePane;
    auto* target = new QPlainTextEdit("focused pane");
    const auto targetPane = manager.splitPane(tab, firstPane, target, Placement::Right);
    auto* third = new QPlainTextEdit("third");
    const auto thirdPane = manager.splitPane(tab, firstPane, third, Placement::Bottom);
    auto* firstTop = bar("first top"); auto* targetTop = bar("target top");
    auto* firstBottom = bar("first bottom"); auto* targetBottom = bar("target bottom");
    QVERIFY(manager.setPaneTopBar(firstPane, firstTop)); QVERIFY(manager.setPaneTopBar(targetPane, targetTop));
    QVERIFY(manager.setPaneBottomBar(firstPane, firstBottom)); QVERIFY(manager.setPaneBottomBar(targetPane, targetBottom));
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(workspace(manager, id)));
    QVERIFY(manager.focusPane(tab, targetPane)); settle();
    const auto model = manager.model().toJson();
    auto* page = tabView(target); QVERIFY(page);
    const auto frames = page->paneFrames();
    QHash<PaneId, QRect> originalFrames;
    for (auto iterator = frames.cbegin(); iterator != frames.cend(); ++iterator)
        originalFrames.insert(iterator.key(), globalBounds(iterator.value()));
    const auto topParent = targetTop->parentWidget(); const auto bottomParent = targetBottom->parentWidget();
    QVERIFY(manager.setViewMode(id, ViewMode::PaneFullscreen)); settle();
    QVERIFY(target->isVisible()); QVERIFY(!first->isVisible()); QVERIFY(!third->isVisible());
    QCOMPARE(page->paneFrames(), frames);
    QCOMPARE(targetTop->parentWidget(), topParent); QCOMPARE(targetBottom->parentWidget(), bottomParent);
    QVERIFY(targetTop->isVisible()); QVERIFY(targetBottom->isVisible());
    QVERIFY(!firstTop->isVisible()); QVERIFY(!firstBottom->isVisible());
    QCOMPARE(manager.paneWidget(firstPane), first); QCOMPARE(manager.paneWidget(targetPane), target);
    QCOMPARE(manager.paneWidget(thirdPane), third); QCOMPARE(manager.model().toJson(), model);
    const auto targetFrame = globalBounds(frames.value(targetPane));
    QCOMPARE(targetFrame.width(), page->width());
    QCOMPARE(targetFrame.height(), page->height() - (sharedTop ? 22 : 0) - (sharedBottom ? 22 : 0));
    QCOMPARE(workspace(manager, id)->surface()->tabBarRects().size(), 1);
    QVERIFY(manager.setTabBarsVisible(id, false)); settle();
    QVERIFY(workspace(manager, id)->surface()->tabBarRects().isEmpty());
    QCOMPARE(manager.viewMode(id), ViewMode::PaneFullscreen); QVERIFY(target->isVisible());
    QVERIFY(manager.setTabBarsVisible(id, true)); settle();
    QVERIFY(manager.setViewMode(id, ViewMode::Normal)); settle();
    QVERIFY(first->isVisible()); QVERIFY(target->isVisible()); QVERIFY(third->isVisible());
    QCOMPARE(page->paneFrames(), frames);
    for (auto iterator = frames.cbegin(); iterator != frames.cend(); ++iterator)
        QCOMPARE(globalBounds(iterator.value()), originalFrames.value(iterator.key()));
    QCOMPARE(firstTop->isVisible(), !sharedTop); QCOMPARE(firstBottom->isVisible(), !sharedBottom);
    QVERIFY(targetTop->isVisible()); QVERIFY(targetBottom->isVisible());
    QCOMPARE(manager.model().toJson(), model);
}

void FocusModeTest::zenHidesAndRestoresEveryContentBar_data()
{
    QTest::addColumn<int>("mode");
    for (const auto mode : {ViewMode::WindowFullscreen, ViewMode::GroupFullscreen, ViewMode::PaneFullscreen})
        QTest::newRow(qPrintable(QString("mode-%1").arg(int(mode)))) << int(mode);
}

void FocusModeTest::zenHidesAndRestoresEveryContentBar()
{
    QFETCH(int, mode);
    WindowManager manager; const auto id = manager.createWindow();
    auto* first = new QPlainTextEdit("first");
    const auto tab = manager.addTab(id, first, {"bars", "test.editor", {}});
    const auto firstPane = manager.model().tab(tab)->activePane;
    auto* second = new QPlainTextEdit("second");
    const auto secondPane = manager.splitPane(tab, firstPane, second, Placement::Bottom);
    auto* explicitTop = bar("tab top"); auto* explicitBottom = bar("tab bottom");
    auto* firstTop = bar("pane top"); auto* firstBottom = bar("pane bottom");
    auto* secondTop = bar("second top"); auto* secondBottom = bar("second bottom");
    QVERIFY(manager.setTabTopBar(tab, explicitTop)); QVERIFY(manager.setTabBottomBar(tab, explicitBottom));
    QVERIFY(manager.setPaneTopBar(firstPane, firstTop)); QVERIFY(manager.setPaneBottomBar(firstPane, firstBottom));
    QVERIFY(manager.setPaneTopBar(secondPane, secondTop)); QVERIFY(manager.setPaneBottomBar(secondPane, secondBottom));
    QVERIFY(manager.setTabTopBarSharing(tab, PaneBarSharing::PerPane));
    QVERIFY(manager.setTabBottomBarSharing(tab, PaneBarSharing::PerPane));
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(workspace(manager, id)));
    QVERIFY(manager.focusPane(tab, firstPane));
    QVERIFY(manager.setViewMode(id, ViewMode(mode))); settle();
    const auto model = manager.model().toJson();
    const QList<QWidget*> bars{explicitTop, explicitBottom, firstTop, firstBottom, secondTop, secondBottom};
    QList<bool> visibility; QList<QWidget*> parents;
    for (auto* widget : bars) { visibility.append(widget->isVisible()); parents.append(widget->parentWidget()); }
    QVERIFY(manager.setZenMode(id, true)); settle();
    for (auto* widget : bars) QVERIFY(!widget->isVisible());
    auto* page = tabView(first); QVERIFY(page);
    const auto visibleFrame = page->paneFrames().value(firstPane); QVERIFY(visibleFrame);
    QCOMPARE(first->geometry(), visibleFrame->rect());
    QVERIFY(manager.setZenMode(id, false)); settle();
    for (int index = 0; index < bars.size(); ++index) {
        QCOMPARE(bars[index]->isVisible(), visibility[index]); QCOMPARE(bars[index]->parentWidget(), parents[index]);
    }
    QCOMPARE(manager.paneWidget(firstPane), first); QCOMPARE(manager.paneWidget(secondPane), second);
    QCOMPARE(manager.model().toJson(), model);
}

void FocusModeTest::contentScopesAreIndependentAcrossWindows()
{
    WindowManager manager; const auto firstWindow = manager.createWindow(); const auto secondWindow = manager.createWindow();
    auto* first = new QPlainTextEdit("first"); auto* second = new QPlainTextEdit("second");
    const auto firstTab = manager.addTab(firstWindow, first, {"first", {}, {}});
    const auto secondTab = manager.addTab(secondWindow, second, {"second", {}, {}});
    const auto firstPane = manager.model().tab(firstTab)->activePane;
    const auto secondPane = manager.model().tab(secondTab)->activePane;
    auto* firstBar = bar("first bar"); auto* secondBar = bar("second bar");
    QVERIFY(manager.setTabTopBar(firstTab, firstBar)); QVERIFY(manager.setTabTopBar(secondTab, secondBar));
    manager.showWindow(firstWindow); manager.showWindow(secondWindow); settle();
    QVERIFY(manager.activateTab(firstTab)); QVERIFY(manager.setViewMode(firstWindow, ViewMode::GroupFullscreen));
    QVERIFY(manager.setZenMode(firstWindow, true));
    QVERIFY(manager.activateTab(secondTab)); QVERIFY(manager.setViewMode(secondWindow, ViewMode::PaneFullscreen)); settle();
    QVERIFY(!firstBar->isVisible()); QVERIFY(secondBar->isVisible());
    QVERIFY(manager.setViewMode(secondWindow, ViewMode::Normal)); settle();
    QVERIFY(!firstBar->isVisible()); QVERIFY(secondBar->isVisible());
    QVERIFY(manager.setZenMode(firstWindow, false)); settle();
    QVERIFY(firstBar->isVisible()); QVERIFY(secondBar->isVisible());
    QCOMPARE(manager.paneWidget(firstPane), first); QCOMPARE(manager.paneWidget(secondPane), second);
}

void FocusModeTest::groupScopeSurvivesSelectingAnotherTab()
{
    WindowManager manager; const auto id = manager.createWindow();
    auto* first = new QPlainTextEdit("first"); auto* second = new QPlainTextEdit("second");
    const auto firstTab = manager.addTab(id, first, {"first", {}, {}});
    const auto secondTab = manager.addTab(id, second, {"second", {}, {}});
    manager.showWindow(id); QVERIFY(manager.activateTab(firstTab));
    QVERIFY(manager.setViewMode(id, ViewMode::GroupFullscreen));
    QVERIFY(manager.activateTab(secondTab)); settle();
    QCOMPARE(manager.viewMode(id), ViewMode::GroupFullscreen);
    QVERIFY(!first->isVisible()); QVERIFY(second->isVisible());
    QCOMPARE(workspace(manager, id)->surface()->groupRects().size(), 1);
}

void FocusModeTest::paneScopeFollowsSameWindowMovement()
{
    WindowManager manager; const auto id = manager.createWindow();
    const auto firstTab = manager.addTab(id, new QPlainTextEdit("source"), {"source", {}, {}});
    const auto firstPane = manager.model().tab(firstTab)->activePane;
    auto* target = new QPlainTextEdit("target");
    const auto pane = manager.splitPane(firstTab, firstPane, target, Placement::Right);
    const auto secondTab = manager.addTab(id, new QPlainTextEdit("destination"), {"destination", {}, {}});
    QVERIFY(manager.moveTab(secondTab, manager.model().groupForTab(firstTab), Placement::Bottom));
    manager.showWindow(id); QVERIFY(manager.activateTab(firstTab)); QVERIFY(manager.focusPane(firstTab, pane));
    QVERIFY(manager.setViewMode(id, ViewMode::PaneFullscreen));
    QVERIFY(manager.movePane(firstTab, pane, secondTab)); settle();
    QCOMPARE(manager.viewMode(id), ViewMode::PaneFullscreen);
    QCOMPARE(manager.windowViewState(id).pane, pane);
    QCOMPARE(manager.windowViewState(id).tab, secondTab);
    QCOMPARE(manager.paneWidget(pane), target); QVERIFY(target->isVisible());
    QCOMPARE(workspace(manager, id)->surface()->groupRects().size(), 1);
}

void FocusModeTest::closingTheTargetRestoresRemainingContent()
{
    WindowManager manager; const auto id = manager.createWindow();
    auto* remaining = new QPlainTextEdit("remaining");
    const auto remainingTab = manager.addTab(id, remaining, {"remaining", {}, {}});
    const auto targetTab = manager.addTab(id, new QPlainTextEdit("target"), {"target", {}, {}});
    QVERIFY(manager.moveTab(targetTab, manager.model().groupForTab(remainingTab), Placement::Right));
    manager.showWindow(id); QVERIFY(manager.activateTab(targetTab));
    QVERIFY(manager.setViewMode(id, ViewMode::PaneFullscreen));
    QVERIFY(manager.closeTab(targetTab)); settle();
    QCOMPARE(manager.viewMode(id), ViewMode::Normal);
    QVERIFY(remaining->isVisible());
    QCOMPARE(workspace(manager, id)->surface()->groupRects().size(), 1);
}

void FocusModeTest::activatingRailOutsideScopeRestoresWindowScope_data()
{
    QTest::addColumn<int>("mode");
    QTest::newRow("group") << int(ViewMode::GroupFullscreen);
    QTest::newRow("pane") << int(ViewMode::PaneFullscreen);
}

void FocusModeTest::activatingRailOutsideScopeRestoresWindowScope()
{
    QFETCH(int, mode);
    WindowManager manager; const auto id = manager.createWindow();
    const auto center = manager.addTab(id, new QPlainTextEdit("center"), {"center", {}, {}});
    auto* railContent = new QPlainTextEdit("rail content");
    const auto rail = manager.addTab(id, railContent, {"rail", {}, {}});
    QVERIFY(manager.moveTabToEdge(rail, Edge::Left));
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(workspace(manager, id)));
    QVERIFY(manager.activateTab(center)); QVERIFY(manager.setViewMode(id, ViewMode(mode))); settle();
    QAbstractButton* railButton = nullptr;
    for (auto* button : workspace(manager, id)->findChildren<QAbstractButton*>())
        if (button->property("tabId").toUuid() == rail && button->isVisible()) { railButton = button; break; }
    QVERIFY(railButton);
    QTest::mouseClick(railButton, Qt::LeftButton); settle();
    QCOMPARE(manager.viewMode(id), ViewMode::WindowFullscreen);
    QVERIFY(railContent->isVisible());
    QCOMPARE(manager.model().groupForTab(rail), manager.context(id).group);
    QVERIFY(workspace(manager, id)->findChild<QWidget*>("PeekPanel"));
}

void FocusModeTest::headerHidingKeepsLocationSemantics_data()
{
    QTest::addColumn<int>("location"); QTest::addColumn<bool>("floating"); QTest::addColumn<int>("mode");
    for (const auto mode : {ViewMode::GroupFullscreen, ViewMode::PaneFullscreen}) {
        QTest::newRow(qPrintable(QString("central-%1").arg(int(mode)))) << int(GroupLocation::Central) << false << int(mode);
        QTest::newRow(qPrintable(QString("floating-central-%1").arg(int(mode)))) << int(GroupLocation::Central) << true << int(mode);
        QTest::newRow(qPrintable(QString("pinned-%1").arg(int(mode)))) << int(GroupLocation::Pinned) << false << int(mode);
        QTest::newRow(qPrintable(QString("autohide-%1").arg(int(mode)))) << int(GroupLocation::AutoHide) << false << int(mode);
    }
}

void FocusModeTest::headerHidingKeepsLocationSemantics()
{
    QFETCH(int, location); QFETCH(bool, floating); QFETCH(int, mode);
    WindowManager manager; const auto owner = manager.createWindow();
    const auto id = floating ? manager.createWindow(WindowKind::Floating, {}, owner) : owner;
    auto* content = new QPlainTextEdit("same live content");
    const auto tab = manager.addTab(id, content, {"header policy", {}, {}});
    if (GroupLocation(location) != GroupLocation::Central)
        QVERIFY(manager.moveTabToEdge(tab, Edge::Left, GroupLocation(location) == GroupLocation::Pinned));
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(workspace(manager, id)));
    QVERIFY(manager.activateTab(tab));
    QVERIFY(manager.setTabBarsVisible(id, false)); settle();
    auto* surface = workspace(manager, id)->surface();
    if (GroupLocation(location) == GroupLocation::Pinned)
        QCOMPARE(surface->tabBarRects().size(), 1);
    else
        QVERIFY(surface->tabBarRects().isEmpty());
    if (GroupLocation(location) == GroupLocation::AutoHide) {
        auto* title = workspace(manager, id)->findChild<QWidget*>("PeekTitleBar");
        QVERIFY(title && title->isVisible());
    }
    const auto pane = manager.model().tab(tab)->activePane;
    const auto model = manager.model().toJson();
    QVERIFY(manager.setViewMode(id, ViewMode(mode))); settle();
    QCOMPARE(surface->tabBarRects().size(), GroupLocation(location) == GroupLocation::Central ? 0 : 1);
    QVERIFY(content->isVisible()); QVERIFY(!manager.tabBarsVisible(id));
    QVERIFY(manager.setZenMode(id, true)); settle();
    QVERIFY(surface->tabBarRects().isEmpty()); QVERIFY(content->isVisible());
    QVERIFY(manager.setZenMode(id, false)); settle();
    QCOMPARE(surface->tabBarRects().size(), GroupLocation(location) == GroupLocation::Central ? 0 : 1);
    QVERIFY(!manager.tabBarsVisible(id));
    QVERIFY(manager.setViewMode(id, ViewMode::Normal)); settle();
    QCOMPARE(surface->tabBarRects().size(), GroupLocation(location) == GroupLocation::Pinned ? 1 : 0);
    if (GroupLocation(location) == GroupLocation::AutoHide) {
        auto* title = workspace(manager, id)->findChild<QWidget*>("PeekTitleBar");
        QVERIFY(title && title->isVisible());
    }
    QVERIFY(content->isVisible()); QCOMPARE(manager.paneWidget(pane), content);
    QCOMPARE(manager.model().toJson(), model);
}

void FocusModeTest::hidingFocusedBarsPreservesTheActiveContent_data()
{
    QTest::addColumn<bool>("attached");
    QTest::newRow("tab-header") << false;
    QTest::newRow("attached-input") << true;
}

void FocusModeTest::hidingFocusedBarsPreservesTheActiveContent()
{
    QFETCH(bool, attached);
    WindowManager manager;
    const auto id = manager.createWindow();
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page); layout->addWidget(new QLabel("Passive content", page));
    const auto tab = manager.addTab(id, page, {"Passive central content", "test.passive", {}});
    auto* navigation = new QLineEdit("Attached navigation");
    auto* pinned = new QTreeWidget;
    pinned->addTopLevelItem(new QTreeWidgetItem(QStringList{"Pinned content"}));
    const auto side = manager.addTab(id, pinned, {"Pinned focus target", "test.tree", {}});
    QVERIFY(manager.setTabTopBar(side, navigation));
    QVERIFY(manager.moveTabToEdge(side, Edge::Left, true));
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(workspace(manager, id)));
    QVERIFY(manager.activateTab(tab));
    QVERIFY(manager.setViewMode(id, ViewMode::WindowFullscreen)); settle();
    QWidget* focus = navigation;
    if (!attached) {
        focus = nullptr;
        for (auto* widget : workspace(manager, id)->findChildren<QWidget*>("TabGroup"))
            if (auto* group = dynamic_cast<GroupTabs*>(widget);
                group && group->property("groupId").toUuid() == manager.model().groupForTab(side)) focus = group->tabBar();
    }
    QVERIFY(focus); focus->setFocus(); QTRY_COMPARE(QApplication::focusWidget(), focus);
    QCOMPARE(manager.context(id).tab, tab);
    const auto before = manager.model().toJson();
    QVERIFY(manager.setZenMode(id, true)); settle();
    QCOMPARE(manager.model().toJson(), before);
    QCOMPARE(manager.context(id).tab, tab);
    QVERIFY(!pinned->hasFocus());
    QVERIFY(QApplication::focusWidget() == page || page->isAncestorOf(QApplication::focusWidget()));
    QVERIFY(manager.setZenMode(id, false)); settle();
    QCOMPARE(manager.model().toJson(), before);
    QCOMPARE(manager.context(id).tab, tab);
}

void FocusModeTest::inactiveWindowKeepsItsContentFocusWithoutStealingTheOtherWindow()
{
    WindowManager manager;
    const auto first = manager.createWindow();
    auto* editor = new QPlainTextEdit("First window active content");
    const auto tab = manager.addTab(first, editor, {"First", "test.editor", {}});
    auto* pinned = new QTreeWidget;
    const auto side = manager.addTab(first, pinned, {"Pinned", "test.tree", {}});
    QVERIFY(manager.moveTabToEdge(side, Edge::Left, true));
    const auto second = manager.createWindow();
    auto* other = new QPlainTextEdit("Other window focus");
    const auto otherTab = manager.addTab(second, other, {"Second", "test.editor", {}});
    manager.showWindow(first); QVERIFY(QTest::qWaitForWindowExposed(workspace(manager, first)));
    QVERIFY(manager.activateTab(tab)); QVERIFY(manager.setViewMode(first, ViewMode::WindowFullscreen)); settle();
    QWidget* header = nullptr;
    for (auto* widget : workspace(manager, first)->findChildren<QWidget*>("TabGroup"))
        if (auto* group = dynamic_cast<GroupTabs*>(widget);
            group && group->property("groupId").toUuid() == manager.model().groupForTab(side)) header = group->tabBar();
    QVERIFY(header); header->setFocus(); QTRY_COMPARE(QApplication::focusWidget(), header);
    QCOMPARE(manager.context(first).tab, tab);
    manager.showWindow(second); QVERIFY(QTest::qWaitForWindowExposed(workspace(manager, second)));
    QVERIFY(manager.focusPane(otherTab, manager.model().tab(otherTab)->activePane));
    QTRY_COMPARE(QApplication::focusWidget(), other);
    const auto before = manager.model().toJson();
    QVERIFY(manager.setZenMode(first, true)); settle();
    QCOMPARE(QApplication::focusWidget(), other);
    QCOMPARE(manager.model().toJson(), before);
    manager.showWindow(first);
    QTRY_VERIFY(workspace(manager, first)->isActiveWindow());
    QTRY_COMPARE(QApplication::focusWidget(), editor);
    QCOMPARE(manager.model().toJson(), before);
    QCOMPARE(manager.context(first).tab, tab);
}

void FocusModeTest::autoHideActivationRevalidatesAfterHostNotification_data()
{
    QTest::addColumn<QString>("mutation");
    for (const auto* mutation : {"move-target", "pin-target", "close-target", "destroy-manager",
                                 "open-other-tab", "cancel-peek"})
        QTest::newRow(mutation) << QString::fromLatin1(mutation);
}

void FocusModeTest::autoHideActivationRevalidatesAfterHostNotification()
{
    QFETCH(QString, mutation);
    auto manager = std::make_unique<WindowManager>();
    const auto id = manager->createWindow();
    const auto keeper = manager->addTab(id, new QPlainTextEdit("Keeper"), {"Keeper", "test.editor", {}});
    QPointer<QPlainTextEdit> content = new QPlainTextEdit("Target retains its widget");
    const auto tab = manager->addTab(id, content, {"Target", "test.editor", {}});
    QVERIFY(manager->moveTabToEdge(tab, Edge::Left));
    const auto group = manager->model().groupForTab(tab);
    const auto pane = manager->model().tab(tab)->activePane;
    auto* otherContent = new QPlainTextEdit("The newer peek owns this content");
    const auto other = manager->addTab(id, otherContent, {"Other", "test.editor", {}});
    QVERIFY(manager->moveTabToEdge(other, Edge::Right));
    const auto otherGroup = manager->model().groupForTab(other);
    manager->showWindow(id);
    QVERIFY(QTest::qWaitForWindowExposed(workspace(*manager, id)));
    QVERIFY(manager->activateTab(keeper)); settle();
    QPointer<WindowManager> owner(manager.get());
    QPointer<WorkspaceWindow> window(workspace(*manager, id));
    QPointer<DockSurface> surface(window->surface());
    bool notified = false;
    bool mutationSucceeded = false;
    connect(manager.get(), &WindowManager::layoutChanged, this, [&] {
        if (notified) return;
        notified = true;
        if (mutation == "move-target") mutationSucceeded = manager->moveTabToEdge(tab, Edge::Top);
        else if (mutation == "pin-target") mutationSucceeded = manager->pinTab(tab, true);
        else if (mutation == "close-target") mutationSucceeded = manager->closeTab(tab);
        else if (mutation == "destroy-manager") { manager.reset(); mutationSucceeded = true; }
        else if (mutation == "open-other-tab") { surface->showEdgeGroup(otherGroup, other); mutationSucceeded = true; }
        else { surface->closePeek(); mutationSucceeded = true; }
    });
    // Activating an auto-hide tab calls updateActive, whose model transaction
    // replaces the borrowed GroupRecord before notifying this host. Every
    // mutation below must take effect before the original peek can continue.
    bool activated = false;
    if (mutation == "open-other-tab" || mutation == "cancel-peek") surface->showEdgeGroup(group, tab);
    else activated = manager->activateTab(tab);
    QVERIFY(notified); QVERIFY(mutationSucceeded);
    if (mutation == "destroy-manager") {
        QVERIFY(!activated); QVERIFY(!owner); QVERIFY(!window); QVERIFY(!surface); QVERIFY(!content);
        settle();
        return;
    }
    QVERIFY(owner); QVERIFY(window); QVERIFY(surface);
    if (mutation == "close-target") {
        QVERIFY(!activated); QVERIFY(!manager->model().tab(tab));
        QVERIFY(!surface->findChild<QWidget*>("PeekPanel"));
        settle(); QVERIFY(!content);
    } else if (mutation == "open-other-tab") {
        auto* panel = surface->findChild<QWidget*>("PeekPanel"); QVERIFY(panel);
        QVERIFY(panel->isAncestorOf(otherContent)); QVERIFY(otherContent->isVisible());
        QVERIFY(!panel->isAncestorOf(content)); QCOMPARE(manager->context(id).tab, other);
        QCOMPARE(manager->paneWidget(pane), content.data());
    } else {
        QVERIFY(!surface->findChild<QWidget*>("PeekPanel"));
        QCOMPARE(manager->paneWidget(pane), content.data());
        if (mutation == "pin-target") {
            QVERIFY(activated);
            QCOMPARE(manager->model().group(manager->model().groupForTab(tab))->location, GroupLocation::Pinned);
            QVERIFY(content->isVisible());
        } else if (mutation == "move-target") {
            QVERIFY(activated);
            QCOMPARE(manager->model().group(manager->model().groupForTab(tab))->edge, Edge::Top);
            // The obsolete left-edge request was canceled by the rebuild. A
            // subsequent activation must create the newly located top peek.
            QVERIFY(manager->activateTab(tab));
            auto* panel = surface->findChild<QWidget*>("PeekPanel"); QVERIFY(panel);
            QVERIFY(panel->isAncestorOf(content)); QVERIFY(content->isVisible());
            auto* handle = panel->findChild<QWidget*>("PeekResizeHandle"); QVERIFY(handle);
            QCOMPARE(handle->property("edge").toInt(), int(Edge::Top));
        }
    }
    QVERIFY(manager->model().validate());
}

void FocusModeTest::closingAnExistingPeekPreservesTheNewerRequest()
{
    WindowManager manager;
    const auto id = manager.createWindow();
    manager.addTab(id, new QPlainTextEdit("Keeper"), {"Keeper", "test.editor", {}});
    auto* first = new QPlainTextEdit("The old peek must return this content safely");
    const auto firstTab = manager.addTab(id, first, {"First peek", "test.editor", {}});
    QVERIFY(manager.moveTabToEdge(firstTab, Edge::Left));
    auto* newer = new QPlainTextEdit("The newer peek must remain open");
    const auto newerTab = manager.addTab(id, newer, {"Newer peek", "test.editor", {}});
    QVERIFY(manager.moveTabToEdge(newerTab, Edge::Right));
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(workspace(manager, id)));
    QVERIFY(manager.activateTab(firstTab));
    auto* surface = workspace(manager, id)->surface();
    QPointer<QWidget> oldPanel = surface->findChild<QWidget*>("PeekPanel"); QVERIFY(oldPanel);
    QPointer<TabContent> firstPage = tabView(first); QVERIFY(firstPage);
    const auto firstGroup = manager.model().groupForTab(firstTab);
    const auto newerGroup = manager.model().groupForTab(newerTab);
    ParentChangeCallback callback;
    bool reopened = false;
    callback.changed = [&] {
        if (reopened || firstPage->parentWidget() != surface) return;
        reopened = true;
        surface->showEdgeGroup(newerGroup, newerTab);
    };
    firstPage->installEventFilter(&callback);
    // Parking the old page emits ParentChange synchronously inside closePeek.
    // A newly opened panel there must invalidate the outer replacement request.
    surface->showEdgeGroup(firstGroup, firstTab);
    QVERIFY(reopened); QVERIFY(!oldPanel); QVERIFY(firstPage);
    auto* panel = surface->findChild<QWidget*>("PeekPanel"); QVERIFY(panel);
    QCOMPARE(surface->findChildren<QWidget*>("PeekPanel").size(), 1);
    QVERIFY(panel->isAncestorOf(newer)); QVERIFY(newer->isVisible());
    QVERIFY(!panel->isAncestorOf(first)); QVERIFY(!first->isVisible());
    QCOMPARE(manager.context(id).tab, newerTab);
    QCOMPARE(manager.paneWidget(manager.model().tab(firstTab)->activePane), first);
    QCOMPARE(manager.paneWidget(manager.model().tab(newerTab)->activePane), newer);
    QVERIFY(manager.model().validate());
}

QTEST_MAIN(FocusModeTest)
#include "focus_mode_test.moc"
