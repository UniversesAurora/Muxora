// SPDX-License-Identifier: MIT
#include <window_system/window_manager.hpp>
#include <QApplication>
#include <QJsonArray>
#include <QLabel>
#include <QTest>
using namespace window_system;

namespace {
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
bool preference(WindowManager& manager, WindowId window, WindowBar bar)
{
    const auto bars = manager.preferredBarVisibility(window);
    switch (bar) {
    case WindowBar::Toolbars: return bars.toolbars;
    case WindowBar::StatusBar: return bars.statusBar;
    case WindowBar::TabBars: return bars.tabBars;
    case WindowBar::SideBars: return bars.sideBars;
    }
    return false;
}
void addContent(WindowManager& manager, WindowId window)
{
    manager.addTab(window, new QLabel("Retained content"), {"Content", "label", {}});
}
}

class VisibilityProfileTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); }
    void cleanup() { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }
    void latestEditSurvivesOlderWindowsAndRestart_data();
    void latestEditSurvivesOlderWindowsAndRestart();
    void kindChangesKeepTheirOwnHistory();
    void hostMayExplicitlyBypassProfiles();
    void newWindowCommandUsesLatestDefaultsAfterRestart();
    void detachedTabsAndExtractedPanesUseTargetKind();
    void zenDoesNotOverwritePreferences();
    void invalidProfileRejectedAtomically_data();
    void invalidProfileRejectedAtomically();
};
void VisibilityProfileTest::latestEditSurvivesOlderWindowsAndRestart_data()
{
    QTest::addColumn<WindowBar>("bar"); QTest::addColumn<WindowKind>("kind");
    for (const auto kind : {WindowKind::Workbench, WindowKind::Floating})
        for (const auto bar : {WindowBar::Toolbars, WindowBar::StatusBar, WindowBar::TabBars, WindowBar::SideBars})
            QTest::newRow(qPrintable(QString("kind-%1-bar-%2").arg(int(kind)).arg(int(bar)))) << bar << kind;
}
void VisibilityProfileTest::latestEditSurvivesOlderWindowsAndRestart()
{
    QFETCH(WindowBar, bar); QFETCH(WindowKind, kind);
    WindowManager manager; const auto owner = manager.createWindow(); addContent(manager, owner);
    const auto old = manager.createWindow(kind, {}, owner), recent = manager.createWindow(kind, {}, owner);
    addContent(manager, old); addContent(manager, recent);
    QVERIFY(setBar(manager, recent, bar, false)); QVERIFY(preference(manager, old, bar));
    const auto snapshot = manager.saveLayout();
    QVERIFY(manager.requestCloseWindow(old));
    WindowManager restored; restored.setContentFactory([](const TabOptions&, PaneId) { return new QLabel; });
    QVERIFY(restored.restoreLayout(snapshot)); QVERIFY(preference(restored, old, bar)); QVERIFY(!preference(restored, recent, bar));
    const auto fresh = restored.createWindow(kind, {}, owner); QVERIFY(!preference(restored, fresh, bar));
    const auto otherKind = kind == WindowKind::Workbench ? WindowKind::Floating : WindowKind::Workbench;
    const auto other = restored.createWindow(otherKind, {}, owner); QVERIFY(preference(restored, other, bar));
    QVERIFY(setBar(restored, fresh, bar, true));
    const auto reenabled = restored.createWindow(kind, {}, owner); QVERIFY(preference(restored, reenabled, bar));
    QVERIFY(!preference(restored, recent, bar));
}
void VisibilityProfileTest::kindChangesKeepTheirOwnHistory()
{
    WindowManager manager; const auto owner = manager.createWindow(); addContent(manager, owner);
    const auto floating = manager.createWindow(WindowKind::Floating, {}, owner); addContent(manager, floating);
    QVERIFY(manager.setToolbarsVisible(floating, false)); QVERIFY(manager.setSideBarsVisible(floating, false));
    QVERIFY(manager.promoteWindow(floating)); QVERIFY(manager.preferredBarVisibility(floating).toolbars);
    QVERIFY(manager.preferredBarVisibility(floating).sideBars); QVERIFY(manager.setStatusBarVisible(floating, false));
    QVERIFY(manager.demoteWindow(floating, owner)); QVERIFY(!manager.preferredBarVisibility(floating).toolbars);
    QVERIFY(manager.preferredBarVisibility(floating).statusBar); QVERIFY(!manager.preferredBarVisibility(floating).sideBars);
    const auto snapshot = manager.saveLayout();
    WindowManager restored; restored.setContentFactory([](const TabOptions&, PaneId) { return new QLabel; });
    QVERIFY(restored.restoreLayout(snapshot)); QVERIFY(restored.promoteWindow(floating));
    QVERIFY(restored.preferredBarVisibility(floating).toolbars); QVERIFY(!restored.preferredBarVisibility(floating).statusBar);
    QVERIFY(restored.demoteWindow(floating, owner)); QVERIFY(!restored.preferredBarVisibility(floating).toolbars);
    QVERIFY(restored.preferredBarVisibility(floating).statusBar);
}
void VisibilityProfileTest::hostMayExplicitlyBypassProfiles()
{
    WindowManager manager; const auto first = manager.createWindow(); QVERIFY(manager.setTabBarsVisible(first, false));
    QVERIFY(!manager.preferredBarVisibility(manager.createWindow()).tabBars);
    WindowOptions options; options.inheritVisibilityProfile = false;
    const auto explicitWindow = manager.createWindow(WindowKind::Workbench, options); QVERIFY(manager.preferredBarVisibility(explicitWindow).tabBars);
    QVERIFY(!manager.preferredBarVisibility(manager.createWindow()).tabBars);
}
void VisibilityProfileTest::newWindowCommandUsesLatestDefaultsAfterRestart()
{
    WindowManager manager; const auto old = manager.createWindow(); addContent(manager, old);
    const auto recent = manager.createWindow(); addContent(manager, recent);
    for (const auto bar : {WindowBar::Toolbars, WindowBar::StatusBar, WindowBar::TabBars, WindowBar::SideBars}) {
        QVERIFY(setBar(manager, old, bar, false)); QVERIFY(setBar(manager, recent, bar, true));
    }
    WindowManager restored; restored.setContentFactory([](const TabOptions&, PaneId) { return new QLabel; });
    QVERIFY(restored.restoreLayout(manager.saveLayout()));
    const auto before = restored.model().windowOrder(); QVERIFY(restored.execute("window.new", {}, old));
    const auto after = restored.model().windowOrder(); QCOMPARE(after.size(), before.size() + 1);
    WindowId created;
    for (const auto id : after) if (!before.contains(id)) created = id;
    QVERIFY(!created.isNull()); QCOMPARE(restored.preferredBarVisibility(created), BarVisibility{});
    QCOMPARE(restored.preferredBarVisibility(old), (BarVisibility{false, false, false, false}));
    QVERIFY(restored.model().validate());
}
void VisibilityProfileTest::detachedTabsAndExtractedPanesUseTargetKind()
{
    WindowManager manager; const auto root = manager.createWindow();
    const auto first = manager.addTab(root, new QLabel, {"First", "label", {}});
    const auto second = manager.addTab(root, new QLabel, {"Second", "label", {}});
    const auto third = manager.addTab(root, new QLabel, {"Third", "label", {}});
    QVERIFY(manager.setTabBarsVisible(root, false));
    const auto floating = manager.detachTab(second); QVERIFY(manager.preferredBarVisibility(floating).tabBars);
    QVERIFY(manager.setToolbarsVisible(floating, false));
    const auto next = manager.detachTab(third); QVERIFY(!manager.preferredBarVisibility(next).toolbars);
    const auto pane = manager.splitPane(first, manager.model().tab(first)->activePane, new QLabel, Placement::Right);
    const auto extracted = manager.extractPane(first, pane); QVERIFY(!extracted.isNull());
    const auto extractedWindow = manager.model().group(manager.model().groupForTab(extracted))->windowId;
    QVERIFY(!manager.preferredBarVisibility(extractedWindow).toolbars); QVERIFY(manager.preferredBarVisibility(extractedWindow).tabBars);
}
void VisibilityProfileTest::zenDoesNotOverwritePreferences()
{
    WindowManager manager; const auto root = manager.createWindow(); addContent(manager, root);
    QVERIFY(manager.setViewMode(root, ViewMode::WindowFullscreen)); QVERIFY(manager.setZenMode(root, true));
    QVERIFY(manager.setToolbarsVisible(root, false));
    const auto next = manager.createWindow(); QVERIFY(manager.preferredBarVisibility(next).toolbars);
    QVERIFY(manager.setStatusBarVisible(root, true)); QVERIFY(!manager.zenMode(root));
    QVERIFY(manager.preferredBarVisibility(root).toolbars);
}
void VisibilityProfileTest::invalidProfileRejectedAtomically_data()
{
    QTest::addColumn<QJsonValue>("profile");
    QTest::newRow("array") << QJsonValue(QJsonArray{});
    QTest::newRow("unknown-kind") << QJsonValue(QJsonObject{{"unexpected", QJsonObject{}}});
    QTest::newRow("missing-fields") << QJsonValue(QJsonObject{{"floating", QJsonObject{{"tabBars", false}}}});
    QTest::newRow("wrong-value-type") << QJsonValue(QJsonObject{{"workbench", QJsonObject{{"toolbars", true}, {"statusBar", true}, {"tabBars", "false"}, {"sideBars", true}}}});
}
void VisibilityProfileTest::invalidProfileRejectedAtomically()
{
    QFETCH(QJsonValue, profile); WindowManager manager; const auto root = manager.createWindow(); addContent(manager, root);
    const auto before = manager.saveLayout(); int calls = 0;
    manager.setContentFactory([&](const TabOptions&, PaneId) { ++calls; return new QLabel; });
    auto malformed = before; malformed.insert("visibilityProfiles", profile);
    QVERIFY(!manager.restoreLayout(malformed)); QCOMPARE(calls, 0); QCOMPARE(manager.saveLayout(), before);
    malformed = before; auto windows = malformed.value("windows").toArray(); auto record = windows[0].toObject();
    record.insert("barLayouts", profile); windows[0] = record; malformed.insert("windows", windows);
    QVERIFY(!manager.restoreLayout(malformed)); QCOMPARE(calls, 0); QCOMPARE(manager.saveLayout(), before);
}
QTEST_MAIN(VisibilityProfileTest)
#include "visibility_profile_test.moc"
