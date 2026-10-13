// SPDX-License-Identifier: MIT
#include <window_system/window_manager.hpp>
#include "chrome/workspace_window.hpp"

#include <QApplication>
#include <QJsonArray>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPointer>
#include <QScreen>
#include <QSignalSpy>
#include <QTest>
#include <algorithm>
#include <memory>
#include <utility>

using namespace window_system;

namespace {

struct Fixture {
    WindowId window;
    TabId tab;
    PaneId pane;
    PaneId otherPane;
    TabId otherTab;
};

Fixture createFixture(WindowManager& manager, const WindowOptions& options = {})
{
    Fixture result;
    result.window = manager.createWindow(WindowKind::Workbench, options);
    result.tab = manager.addTab(result.window, new QPlainTextEdit("Retained first content"), {"First", "test.edit", {}});
    result.pane = manager.model().tab(result.tab)->activePane;
    result.otherPane = manager.splitPane(result.tab, result.pane, new QPlainTextEdit("Retained second content"), Placement::Right);
    result.otherTab = manager.addTab(result.window, new QPlainTextEdit("Retained other content"), {"Other", "test.edit", {}});
    manager.moveTab(result.otherTab, manager.model().groupForTab(result.tab), Placement::Right);
    manager.focusPane(result.tab, result.pane);
    return result;
}

void modeRows()
{
    QTest::addColumn<ViewMode>("mode");
    QTest::newRow("window") << ViewMode::WindowFullscreen;
    QTest::newRow("group") << ViewMode::GroupFullscreen;
    QTest::newRow("pane") << ViewMode::PaneFullscreen;
}

void modeAndBarRows()
{
    QTest::addColumn<ViewMode>("mode"); QTest::addColumn<WindowBar>("bar");
    for (const auto& scope : {std::pair{"window", ViewMode::WindowFullscreen}, std::pair{"group", ViewMode::GroupFullscreen}, std::pair{"pane", ViewMode::PaneFullscreen}})
        for (const auto& bar : {std::pair{"toolbars", WindowBar::Toolbars}, std::pair{"status", WindowBar::StatusBar}, std::pair{"tabs", WindowBar::TabBars}, std::pair{"sidebars", WindowBar::SideBars}})
            QTest::newRow(qPrintable(QString("%1-%2").arg(scope.first, bar.first))) << scope.second << bar.second;
}

bool setBar(WindowManager& manager, WindowId window, WindowBar bar, bool visible)
{
    switch (bar) {
    case WindowBar::Toolbars: return manager.setToolbarsVisible(window, visible);
    case WindowBar::StatusBar: return manager.setStatusBarVisible(window, visible);
    case WindowBar::TabBars: return manager.setTabBarsVisible(window, visible);
    case WindowBar::SideBars: return manager.setSideBarsVisible(window, visible);
    }
    return false;
}

bool visible(const BarVisibility& bars, WindowBar bar)
{
    switch (bar) {
    case WindowBar::Toolbars: return bars.toolbars;
    case WindowBar::StatusBar: return bars.statusBar;
    case WindowBar::TabBars: return bars.tabBars;
    case WindowBar::SideBars: return bars.sideBars;
    }
    return false;
}

QJsonObject windowRecord(const QJsonObject& snapshot, WindowId id)
{
    for (const auto& value : snapshot.value("windows").toArray())
        if (value.toObject().value("id").toString() == id.toString(QUuid::WithoutBraces)) return value.toObject();
    return {};
}

QRect savedPlacement(const QJsonObject& record)
{
    return {record.value("x").toInt(), record.value("y").toInt(),
            record.value("width").toInt(), record.value("height").toInt()};
}

QRect visibleFixtureGeometry(const QRect& available)
{
    const QSize size(std::min(780, available.width()), std::min(520, available.height()));
    QRect geometry(available.topLeft(), size);
    geometry.moveCenter(available.center());
    return geometry;
}

void changeWindowRecord(QJsonObject& snapshot, WindowId id, const std::function<void(QJsonObject&)>& change)
{
    auto windows = snapshot.value("windows").toArray();
    for (qsizetype index = 0; index < windows.size(); ++index) {
        auto record = windows[index].toObject();
        if (WindowId(record.value("id").toString()) != id) continue;
        change(record); windows[index] = record;
    }
    snapshot.insert("windows", windows);
}

void factory(WindowManager& manager, int* calls = nullptr)
{
    manager.setContentFactory([calls](const TabOptions&, PaneId) -> QWidget* {
        if (calls) ++*calls;
        return new QPlainTextEdit("Restored content");
    });
}

} // namespace

class ViewStateTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); }
    void cleanup() { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }
    void fullscreenScopesPreserveWindowVisibility_data() { modeRows(); }
    void fullscreenScopesPreserveWindowVisibility();
    void explicitBarChangesApplyAcrossScopesOnlyInThisWindow_data() { modeAndBarRows(); }
    void explicitBarChangesApplyAcrossScopesOnlyInThisWindow();
    void zenPreservesPreferencesAndShowRestoresThem_data() { modeAndBarRows(); }
    void zenPreservesPreferencesAndShowRestoresThem();
    void changingScopeRetainsContentAndExitingRestoresNormalBars();
    void focusedTargetNavigationLeavesOnlyTheFocusedScope();
    void groupScopeFollowsItsSelectedTab();
    void paneScopeFollowsItsStablePaneWithinTheWindow();
    void removingFocusedTargetsRestoresNormalView_data();
    void removingFocusedTargetsRestoresNormalView();
    void invalidOperationsLeaveStateUnchanged();
    void fullscreenAndZenRestartPreserveNormalPlacement_data();
    void fullscreenAndZenRestartPreserveNormalPlacement();
    void oversizedFullscreenSnapshotsRestoreVisibleNormalPlacement_data() { fullscreenAndZenRestartPreserveNormalPlacement_data(); }
    void oversizedFullscreenSnapshotsRestoreVisibleNormalPlacement();
    void oldSnapshotsRestoreDefaultDisplayState();
    void malformedDisplaySnapshotsFailBeforeCreatingContent_data();
    void malformedDisplaySnapshotsFailBeforeCreatingContent();
    void arbitraryToolbarRowsRestoreAndMalformedUnknownItemsFail();
    void viewNotificationAllowsManagerDestruction();
};

void ViewStateTest::fullscreenScopesPreserveWindowVisibility()
{
    QFETCH(ViewMode, mode);
    WindowManager manager;
    WindowOptions options; options.toolbarsVisible = options.statusBarVisible = options.tabBarsVisible = options.sideBarsVisible = false;
    const auto fixture = createFixture(manager, options);
    const BarVisibility hidden{false, false, false, false};
    QCOMPARE(manager.barVisibility(fixture.window), hidden);
    const auto model = manager.model().toJson();
    QVERIFY(manager.setViewMode(fixture.window, mode));
    const auto state = manager.windowViewState(fixture.window);
    QCOMPARE(state.effective, hidden);
    QCOMPARE(manager.preferredBarVisibility(fixture.window), hidden);
    QCOMPARE(manager.model().toJson(), model);
    QVERIFY(manager.setViewMode(fixture.window, ViewMode::Normal));
    QCOMPARE(manager.barVisibility(fixture.window), hidden);
    QVERIFY(!manager.zenMode(fixture.window));
}

void ViewStateTest::explicitBarChangesApplyAcrossScopesOnlyInThisWindow()
{
    QFETCH(ViewMode, mode); QFETCH(WindowBar, bar);
    WindowManager manager;
    const auto first = createFixture(manager);
    const auto second = createFixture(manager);
    const auto preferences = manager.preferredBarVisibility(first.window);
    const auto otherBefore = manager.barVisibility(second.window);
    QVERIFY(manager.setViewMode(first.window, mode));
    QVERIFY(manager.setViewMode(second.window, mode));
    QVERIFY(setBar(manager, first.window, bar, false));
    QVERIFY(!visible(manager.barVisibility(first.window), bar));
    QCOMPARE(manager.barVisibility(second.window), otherBefore);
    QVERIFY(!visible(manager.preferredBarVisibility(first.window), bar));
    QVERIFY(setBar(manager, first.window, bar, true));
    QVERIFY(visible(manager.barVisibility(first.window), bar));
    QCOMPARE(manager.barVisibility(second.window), otherBefore);
    QCOMPARE(manager.preferredBarVisibility(first.window), preferences);
    QVERIFY(manager.setViewMode(first.window, ViewMode::Normal));
    QCOMPARE(manager.barVisibility(first.window), preferences);
    QVERIFY(setBar(manager, first.window, bar, false));
    QVERIFY(manager.setViewMode(first.window, mode));
    QVERIFY(!visible(manager.barVisibility(first.window), bar));
    QVERIFY(manager.setViewMode(first.window, ViewMode::Normal));
    QVERIFY(!visible(manager.barVisibility(first.window), bar));
}

void ViewStateTest::zenPreservesPreferencesAndShowRestoresThem()
{
    QFETCH(ViewMode, mode); QFETCH(WindowBar, bar);
    WindowManager manager;
    const auto first = createFixture(manager);
    const auto second = createFixture(manager);
    QVERIFY(manager.setViewMode(first.window, mode));
    QVERIFY(manager.setViewMode(second.window, mode));
    QVERIFY(setBar(manager, first.window, bar, false));
    const auto behavior = manager.saveLayout().value("behavior");
    const auto preferences = manager.preferredBarVisibility(first.window);
    const auto before = manager.barVisibility(first.window);
    const auto otherBefore = manager.barVisibility(second.window);
    QVERIFY(manager.setZenMode(first.window, true));
    QCOMPARE(manager.barVisibility(first.window), (BarVisibility{false, false, false, false}));
    QVERIFY(!manager.tabBarsVisible(first.window)); QVERIFY(!manager.sideBarsVisible(first.window));
    QVERIFY(!manager.toolbarsVisible(first.window)); QVERIFY(!manager.statusBarVisible(first.window));
    QVERIFY(setBar(manager, first.window, bar, false));
    QVERIFY(manager.zenMode(first.window));
    QCOMPARE(manager.saveLayout().value("behavior"), behavior);
    QCOMPARE(manager.preferredBarVisibility(first.window), preferences);
    QCOMPARE(manager.barVisibility(second.window), otherBefore);
    QVERIFY(manager.setZenMode(first.window, false));
    QCOMPARE(manager.barVisibility(first.window), before);
    QVERIFY(manager.setZenMode(first.window, true));
    QVERIFY(setBar(manager, first.window, bar, true));
    QVERIFY(!manager.zenMode(first.window));
    auto expected = before;
    switch (bar) {
    case WindowBar::Toolbars: expected.toolbars = true; break;
    case WindowBar::StatusBar: expected.statusBar = true; break;
    case WindowBar::TabBars: expected.tabBars = true; break;
    case WindowBar::SideBars: expected.sideBars = true; break;
    }
    QCOMPARE(manager.barVisibility(first.window), expected);
    QCOMPARE(manager.preferredBarVisibility(first.window), expected);
}

void ViewStateTest::changingScopeRetainsContentAndExitingRestoresNormalBars()
{
    WindowManager manager;
    const auto fixture = createFixture(manager);
    QVERIFY(manager.setToolbarsVisible(fixture.window, false));
    const auto preferences = manager.preferredBarVisibility(fixture.window);
    const auto model = manager.model().toJson();
    QPointer<QWidget> first = manager.paneWidget(fixture.pane), second = manager.paneWidget(fixture.otherPane);
    for (const auto mode : {ViewMode::WindowFullscreen, ViewMode::GroupFullscreen, ViewMode::PaneFullscreen}) {
        QVERIFY(manager.setViewMode(fixture.window, mode));
        QCOMPARE(manager.viewMode(fixture.window), mode);
        QCOMPARE(manager.model().toJson(), model);
        QCOMPARE(manager.paneWidget(fixture.pane), first.data()); QCOMPARE(manager.paneWidget(fixture.otherPane), second.data());
    }
    QVERIFY(manager.setZenMode(fixture.window, true));
    QVERIFY(manager.setViewMode(fixture.window, ViewMode::Normal));
    QCOMPARE(manager.barVisibility(fixture.window), preferences);
    const auto state = manager.windowViewState(fixture.window);
    QVERIFY(!state.zen); QVERIFY(state.group.isNull() && state.tab.isNull() && state.pane.isNull());
}

void ViewStateTest::focusedTargetNavigationLeavesOnlyTheFocusedScope()
{
    WindowManager manager;
    const auto fixture = createFixture(manager);
    QVERIFY(manager.setViewMode(fixture.window, ViewMode::GroupFullscreen));
    QVERIFY(manager.activateTab(fixture.otherTab));
    QCOMPARE(manager.viewMode(fixture.window), ViewMode::WindowFullscreen);
    QVERIFY(manager.focusPane(fixture.tab, fixture.pane));
    QVERIFY(manager.setViewMode(fixture.window, ViewMode::PaneFullscreen));
    QVERIFY(manager.focusPane(fixture.tab, fixture.pane));
    QCOMPARE(manager.viewMode(fixture.window), ViewMode::PaneFullscreen);
    QVERIFY(manager.focusPane(fixture.tab, fixture.otherPane));
    QCOMPARE(manager.viewMode(fixture.window), ViewMode::WindowFullscreen);
}

void ViewStateTest::groupScopeFollowsItsSelectedTab()
{
    WindowManager manager;
    const auto fixture = createFixture(manager);
    QVERIFY(manager.moveTab(fixture.otherTab, manager.model().groupForTab(fixture.tab)));
    QVERIFY(manager.activateTab(fixture.tab));
    QVERIFY(manager.setViewMode(fixture.window, ViewMode::GroupFullscreen));
    QVERIFY(manager.activateTab(fixture.otherTab));
    QCOMPARE(manager.viewMode(fixture.window), ViewMode::GroupFullscreen);
    QCOMPARE(manager.windowViewState(fixture.window).tab, fixture.otherTab);
}

void ViewStateTest::paneScopeFollowsItsStablePaneWithinTheWindow()
{
    WindowManager manager;
    const auto fixture = createFixture(manager);
    auto* content = manager.paneWidget(fixture.pane);
    QVERIFY(manager.setViewMode(fixture.window, ViewMode::PaneFullscreen));
    const auto targetPane = manager.model().tab(fixture.otherTab)->activePane;
    QVERIFY(manager.movePane(fixture.tab, fixture.pane, fixture.otherTab, targetPane, Placement::Left));
    const auto state = manager.windowViewState(fixture.window);
    QCOMPARE(state.mode, ViewMode::PaneFullscreen); QCOMPARE(state.pane, fixture.pane);
    QCOMPARE(state.tab, fixture.otherTab); QCOMPARE(state.group, manager.model().groupForTab(fixture.otherTab));
    QCOMPARE(manager.context(fixture.window).pane, fixture.pane);
    QCOMPARE(manager.paneWidget(fixture.pane), content);
}

void ViewStateTest::removingFocusedTargetsRestoresNormalView_data()
{
    QTest::addColumn<bool>("pane"); QTest::newRow("group") << false; QTest::newRow("pane") << true;
}

void ViewStateTest::removingFocusedTargetsRestoresNormalView()
{
    QFETCH(bool, pane);
    WindowManager manager;
    const auto fixture = createFixture(manager);
    QVERIFY(manager.setViewMode(fixture.window, pane ? ViewMode::PaneFullscreen : ViewMode::GroupFullscreen));
    QVERIFY(manager.setZenMode(fixture.window, true));
    QVERIFY(pane ? manager.closePane(fixture.tab, fixture.pane) : manager.closeTab(fixture.tab));
    QCOMPARE(manager.viewMode(fixture.window), ViewMode::Normal);
    QVERIFY(!manager.zenMode(fixture.window));
    QCOMPARE(manager.barVisibility(fixture.window), manager.preferredBarVisibility(fixture.window));
}

void ViewStateTest::invalidOperationsLeaveStateUnchanged()
{
    WindowManager manager;
    const auto fixture = createFixture(manager);
    const auto before = manager.saveLayout();
    QSignalSpy changes(&manager, &WindowManager::layoutChanged);
    QVERIFY(!manager.setViewMode(fixture.window, ViewMode(99))); QVERIFY(!manager.setViewMode({}, ViewMode::WindowFullscreen));
    QVERIFY(!manager.setZenMode(fixture.window, true)); QVERIFY(!manager.setToolbarsVisible({}, false));
    QVERIFY(!manager.setStatusBarVisible(QUuid::createUuid(), false));
    QCOMPARE(manager.saveLayout(), before); QVERIFY(changes.isEmpty());
}

void ViewStateTest::fullscreenAndZenRestartPreserveNormalPlacement_data()
{
    QTest::addColumn<ViewMode>("mode"); QTest::addColumn<bool>("zen");
    for (const auto& scope : {std::pair{"window", ViewMode::WindowFullscreen}, std::pair{"group", ViewMode::GroupFullscreen}, std::pair{"pane", ViewMode::PaneFullscreen}}) {
        QTest::newRow(qPrintable(QString("%1").arg(scope.first))) << scope.second << false;
        QTest::newRow(qPrintable(QString("%1-zen").arg(scope.first))) << scope.second << true;
    }
}

void ViewStateTest::fullscreenAndZenRestartPreserveNormalPlacement()
{
    QFETCH(ViewMode, mode); QFETCH(bool, zen);
    WindowManager original;
    const auto fixture = createFixture(original);
    auto* source = dynamic_cast<WorkspaceWindow*>(original.windowWidget(fixture.window)); QVERIFY(source);
    const auto available = qApp->primaryScreen()->availableGeometry();
    const auto normal = visibleFixtureGeometry(available);
    QVERIFY(available.contains(normal));
    source->restorePlacement(normal, false);
    QVERIFY(original.setToolbarsVisible(fixture.window, false));
    const auto preferred = original.preferredBarVisibility(fixture.window);
    QVERIFY(original.setViewMode(fixture.window, mode));
    QVERIFY(original.setZenMode(fixture.window, zen));
    const auto snapshot = original.saveLayout();
    const auto saved = windowRecord(snapshot, fixture.window);
    QCOMPARE(QRect(saved.value("x").toInt(), saved.value("y").toInt(), saved.value("width").toInt(), saved.value("height").toInt()), normal);
    QVERIFY(!saved.value("toolbarsVisible").toBool());
    source->hide();
    WindowManager restored; factory(restored);
    QVERIFY(restored.restoreLayout(snapshot));
    QCOMPARE(restored.viewMode(fixture.window), mode); QCOMPARE(restored.zenMode(fixture.window), zen);
    QCOMPARE(restored.preferredBarVisibility(fixture.window), preferred);
    QCOMPARE(restored.windowViewState(fixture.window), original.windowViewState(fixture.window));
    auto* target = dynamic_cast<WorkspaceWindow*>(restored.windowWidget(fixture.window)); QVERIFY(target);
    QTRY_VERIFY(target->isFullScreen());
    QCOMPARE(target->normalPlacementGeometry(), normal);
    QVERIFY(restored.setViewMode(fixture.window, ViewMode::Normal));
    QTRY_COMPARE(target->geometry(), normal); QVERIFY(!target->isFullScreen());
    QCOMPARE(restored.barVisibility(fixture.window), preferred);
}

void ViewStateTest::oversizedFullscreenSnapshotsRestoreVisibleNormalPlacement()
{
    QFETCH(ViewMode, mode); QFETCH(bool, zen);
    // The first largest current screen gives an independent, unambiguous
    // oracle: this rectangle fully covers every screen, while no other screen
    // can have a larger overlap. Its border is outside the entire desktop.
    // The expected repaired rectangle is the first largest screen itself.
    QRect available, desktopBounds;
    qint64 largestArea = -1;
    for (const auto* screen : qApp->screens()) {
        const auto bounds = screen->availableGeometry();
        desktopBounds = desktopBounds.united(bounds);
        const qint64 area = qint64(bounds.width()) * bounds.height();
        if (area > largestArea) { largestArea = area; available = bounds; }
    }
    QVERIFY(available.isValid());
    const auto visible = visibleFixtureGeometry(available);
    QVERIFY(available.contains(visible));
    const QRect outside(desktopBounds.topLeft() - QPoint(128, 128), desktopBounds.size() + QSize(256, 256));
    QVERIFY(!available.contains(outside));
    QVERIFY(outside.width() <= QWIDGETSIZE_MAX && outside.height() <= QWIDGETSIZE_MAX);
    QVERIFY(qAbs(qint64(outside.x())) <= QWIDGETSIZE_MAX && qAbs(qint64(outside.y())) <= QWIDGETSIZE_MAX);
    WindowManager original;
    const auto fixture = createFixture(original);
    auto* source = dynamic_cast<WorkspaceWindow*>(original.windowWidget(fixture.window)); QVERIFY(source);
    const auto minimum = source->minimumSize().expandedTo(source->minimumSizeHint());
    QVERIFY(available.width() >= minimum.width() && available.height() >= minimum.height());
    source->restorePlacement(visible, false);
    QVERIFY(original.setToolbarsVisible(fixture.window, false));
    QVERIFY(original.setTabBarsVisible(fixture.window, false));
    const auto preferences = original.preferredBarVisibility(fixture.window);
    QVERIFY(original.setViewMode(fixture.window, mode)); QVERIFY(original.setZenMode(fixture.window, zen));
    auto snapshot = original.saveLayout();
    const auto model = original.model().toJson();
    QCOMPARE(savedPlacement(windowRecord(snapshot, fixture.window)), visible);
    changeWindowRecord(snapshot, fixture.window, [&](QJsonObject& record) {
        record.insert("x", outside.x()); record.insert("y", outside.y());
        record.insert("width", outside.width()); record.insert("height", outside.height());
    });
    QCOMPARE(savedPlacement(windowRecord(snapshot, fixture.window)), outside);
    source->hide();
    WindowManager restored; factory(restored);
    QVERIFY(restored.restoreLayout(snapshot));
    QCOMPARE(savedPlacement(windowRecord(snapshot, fixture.window)), outside);
    QCOMPARE(restored.viewMode(fixture.window), mode); QCOMPARE(restored.zenMode(fixture.window), zen);
    QCOMPARE(restored.preferredBarVisibility(fixture.window), preferences);
    QCOMPARE(restored.model().toJson(), model);
    QCOMPARE(restored.model().panes(fixture.tab), original.model().panes(fixture.tab));
    auto* target = dynamic_cast<WorkspaceWindow*>(restored.windowWidget(fixture.window)); QVERIFY(target);
    QTRY_VERIFY(target->isFullScreen());
    QCOMPARE(target->normalPlacementGeometry(), available);
    const auto repaired = windowRecord(restored.saveLayout(), fixture.window);
    QCOMPARE(savedPlacement(repaired), available); QVERIFY(!repaired.value("maximized").toBool());
    QVERIFY(restored.setViewMode(fixture.window, ViewMode::Normal));
    QTRY_COMPARE(target->geometry(), available); QVERIFY(!target->isFullScreen());
    QCOMPARE(target->normalPlacementGeometry(), available);
    QCOMPARE(restored.preferredBarVisibility(fixture.window), preferences);
    QCOMPARE(restored.barVisibility(fixture.window), preferences);
    QCOMPARE(restored.model().toJson(), model);
    const auto savedAgain = windowRecord(restored.saveLayout(), fixture.window);
    QCOMPARE(savedPlacement(savedAgain), available); QVERIFY(!savedAgain.value("maximized").toBool());
}

void ViewStateTest::oldSnapshotsRestoreDefaultDisplayState()
{
    WindowManager original;
    const auto fixture = createFixture(original);
    auto snapshot = original.saveLayout();
    changeWindowRecord(snapshot, fixture.window, [](QJsonObject& record) {
        record.remove("viewState"); record.remove("toolbarsVisible"); record.remove("statusBarVisible");
    });
    WindowManager restored; factory(restored);
    QVERIFY(restored.restoreLayout(snapshot));
    QCOMPARE(restored.viewMode(fixture.window), ViewMode::Normal); QVERIFY(!restored.zenMode(fixture.window));
    QVERIFY(restored.toolbarsVisible(fixture.window)); QVERIFY(restored.statusBarVisible(fixture.window));
    QCOMPARE(restored.barVisibility(fixture.window), BarVisibility{});
}

void ViewStateTest::malformedDisplaySnapshotsFailBeforeCreatingContent_data()
{
    QTest::addColumn<QString>("kind");
    for (const auto* kind : {"toolbar-flag", "status-flag", "view-object", "unknown-mode", "zen-type", "normal-zen",
                            "foreign-group", "foreign-tab", "foreign-pane", "missing-pane", "target-type", "normal-target",
                            "toolbar-rows", "bottom-rows", "unknown-toolbar-item"})
        QTest::newRow(kind) << QString::fromLatin1(kind);
}

void ViewStateTest::malformedDisplaySnapshotsFailBeforeCreatingContent()
{
    QFETCH(QString, kind);
    WindowManager manager; int created = 0; factory(manager, &created);
    const auto fixture = createFixture(manager);
    auto snapshot = manager.saveLayout();
    changeWindowRecord(snapshot, fixture.window, [&](QJsonObject& record) {
        QJsonObject view{{"mode", "paneFullscreen"}, {"zen", false},
            {"group", manager.model().groupForTab(fixture.tab).toString(QUuid::WithoutBraces)},
            {"tab", fixture.tab.toString(QUuid::WithoutBraces)}, {"pane", fixture.pane.toString(QUuid::WithoutBraces)}};
        if (kind == "toolbar-flag") record.insert("toolbarsVisible", "invalid");
        else if (kind == "status-flag") record.insert("statusBarVisible", 1);
        else if (kind == "view-object") record.insert("viewState", false);
        else if (kind == "toolbar-rows" || kind == "bottom-rows" || kind == "unknown-toolbar-item") {
            auto strips = record.value("strips").toObject();
            if (kind == "toolbar-rows") strips.insert("rows", QJsonArray{QJsonArray{42}});
            if (kind == "bottom-rows") strips.insert("bottomRows", "invalid");
            if (kind == "unknown-toolbar-item") {
                auto settings = strips.value("strips").toObject();
                settings.insert("future-tool", QJsonObject{{"excluded", QJsonArray{42}}}); strips.insert("strips", settings);
            }
            record.insert("strips", strips);
        } else {
            if (kind == "unknown-mode") view.insert("mode", "broken");
            if (kind == "zen-type") view.insert("zen", "yes");
            if (kind == "normal-zen") view = QJsonObject{{"mode", "normal"}, {"zen", true}};
            if (kind == "foreign-group") view.insert("group", QUuid::createUuid().toString());
            if (kind == "foreign-tab") view.insert("tab", fixture.otherTab.toString());
            if (kind == "foreign-pane") view.insert("pane", manager.model().tab(fixture.otherTab)->activePane.toString());
            if (kind == "missing-pane") view.remove("pane");
            if (kind == "target-type") view.insert("group", 42);
            if (kind == "normal-target") view.insert("mode", "normal");
            record.insert("viewState", view);
        }
    });
    const auto before = manager.saveLayout();
    QPointer<QWidget> pane = manager.paneWidget(fixture.pane);
    QVERIFY(!manager.restoreLayout(snapshot));
    QCOMPARE(created, 0); QCOMPARE(manager.saveLayout(), before);
    QCOMPARE(manager.paneWidget(fixture.pane), pane.data()); QVERIFY(pane);
}

void ViewStateTest::arbitraryToolbarRowsRestoreAndMalformedUnknownItemsFail()
{
    WindowManager original;
    const auto fixture = createFixture(original);
    auto snapshot = original.saveLayout();
    changeWindowRecord(snapshot, fixture.window, [](QJsonObject& record) {
        auto strips = record.value("strips").toObject();
        strips.insert("rows", QJsonArray{QJsonArray{"menu"}, QJsonArray{"title"}, QJsonArray{"workspace"}, QJsonArray{"caption-controls"}});
        strips.insert("bottomRows", QJsonArray{QJsonArray{"future-one"}, QJsonArray{"future-two"}, QJsonArray{"future-three"}});
        auto settings = strips.value("strips").toObject();
        settings.insert("future-one", QJsonObject{{"visible", true}, {"width", -1}, {"excluded", QJsonArray{"future-action"}}});
        strips.insert("strips", settings); record.insert("strips", strips);
    });
    WindowManager restored; factory(restored);
    QVERIFY(restored.restoreLayout(snapshot));
    const auto saved = windowRecord(restored.saveLayout(), fixture.window).value("strips").toObject();
    QCOMPARE(saved.value("rows").toArray().size(), 4);
    QCOMPARE(saved.value("bottomRows").toArray().size(), 3);
    QVERIFY(saved.value("strips").toObject().contains("future-one"));
    auto invalid = snapshot;
    changeWindowRecord(invalid, fixture.window, [](QJsonObject& record) {
        auto strips = record.value("strips").toObject();
        strips.insert("bottomRows", QJsonArray{QJsonArray{"future-one", "future-one"}}); record.insert("strips", strips);
    });
    const auto before = restored.saveLayout();
    QVERIFY(!restored.restoreLayout(invalid)); QCOMPARE(restored.saveLayout(), before);
}

void ViewStateTest::viewNotificationAllowsManagerDestruction()
{
    auto manager = std::make_unique<WindowManager>();
    const auto fixture = createFixture(*manager);
    QPointer<WindowManager> observer(manager.get());
    QObject::connect(manager.get(), &WindowManager::layoutChanged, qApp, [&] { manager.reset(); });
    auto* raw = manager.get();
    QVERIFY(raw->setViewMode(fixture.window, ViewMode::WindowFullscreen));
    QVERIFY(!observer);
}

QTEST_MAIN(ViewStateTest)
#include "view_state_test.moc"
