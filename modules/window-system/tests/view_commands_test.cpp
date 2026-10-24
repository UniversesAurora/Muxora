// SPDX-License-Identifier: MIT
#include "translation_checks.hpp"
#include <window_system/window_manager.hpp>
#include "chrome/command_strip.hpp"
#include "chrome/workspace_window.hpp"
#include "docking/dock_surface.hpp"
#include "settings/settings_page.hpp"

#include <QAction>
#include <QApplication>
#include <QJsonArray>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPointer>
#include <QScrollArea>
#include <QSet>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <memory>
#include <utility>

using namespace window_system;

namespace {

struct Fixture {
    WindowId window;
    TabId tab;
    PaneId pane;
    QPlainTextEdit* editor;
};

Fixture createFixture(WindowManager& manager)
{
    const auto window = manager.createWindow();
    auto* editor = new QPlainTextEdit(QStringLiteral("Keyboard content remains unchanged"));
    const auto tab = manager.addTab(window, editor, {"Keyboard target", "test.edit", {}});
    const auto pane = manager.model().tab(tab)->activePane;
    manager.splitPane(tab, pane, new QPlainTextEdit("Other pane"), Placement::Right);
    manager.focusPane(tab, pane);
    return {window, tab, pane, editor};
}

void modeRows()
{
    QTest::addColumn<ViewMode>("mode"); QTest::addColumn<Qt::KeyboardModifiers>("modifiers");
    QTest::newRow("F11-window") << ViewMode::WindowFullscreen << Qt::KeyboardModifiers(Qt::NoModifier);
    QTest::newRow("CtrlShiftF11-group") << ViewMode::GroupFullscreen << Qt::KeyboardModifiers(Qt::ControlModifier | Qt::ShiftModifier);
    QTest::newRow("ShiftF11-pane") << ViewMode::PaneFullscreen << Qt::KeyboardModifiers(Qt::ShiftModifier);
}

QString modeCommand(ViewMode mode)
{
    return mode == ViewMode::GroupFullscreen ? "window.fullscreen.group"
        : mode == ViewMode::PaneFullscreen ? "window.fullscreen.pane" : "window.fullscreen.toggle";
}

QAction* actionFor(QWidget* window, const QString& id)
{
    auto* bar = window->findChild<QMenuBar*>("MenuStrip");
    if (!bar) return nullptr;
    for (auto* action : bar->findChildren<QAction*>())
        if (action->data().toString() == id) return action;
    return nullptr;
}

SettingsPage* settingsPage(WindowManager& manager)
{
    for (const auto& tab : manager.model().tabs()) for (const auto pane : manager.model().panes(tab.id))
        if (auto* page = dynamic_cast<SettingsPage*>(manager.paneWidget(pane))) return page;
    return nullptr;
}

TabId tabFor(WindowManager& manager, QWidget* page)
{
    for (const auto& tab : manager.model().tabs()) for (const auto pane : manager.model().panes(tab.id))
        if (manager.paneWidget(pane) == page) return tab.id;
    return {};
}

QRect globalRect(QWidget* widget) { return {widget->mapToGlobal(QPoint()), widget->size()}; }

QString visibilityKey(WindowBar bar)
{
    switch (bar) {
    case WindowBar::Toolbars: return "window.visibility.toolbars";
    case WindowBar::StatusBar: return "window.visibility.statusBar";
    case WindowBar::TabBars: return "window.visibility.tabBars";
    case WindowBar::SideBars: return "window.visibility.sideBars";
    }
    return {};
}

bool shown(const BarVisibility& bars, WindowBar bar)
{
    switch (bar) {
    case WindowBar::Toolbars: return bars.toolbars;
    case WindowBar::StatusBar: return bars.statusBar;
    case WindowBar::TabBars: return bars.tabBars;
    case WindowBar::SideBars: return bars.sideBars;
    }
    return false;
}

BarVisibility withShown(BarVisibility bars, WindowBar bar)
{
    switch (bar) {
    case WindowBar::Toolbars: bars.toolbars = true; break;
    case WindowBar::StatusBar: bars.statusBar = true; break;
    case WindowBar::TabBars: bars.tabBars = true; break;
    case WindowBar::SideBars: bars.sideBars = true; break;
    }
    return bars;
}

void setPreferences(WindowManager& manager, WindowId window, const BarVisibility& bars)
{
    manager.setToolbarsVisible(window, bars.toolbars); manager.setStatusBarVisible(window, bars.statusBar);
    manager.setTabBarsVisible(window, bars.tabBars); manager.setSideBarsVisible(window, bars.sideBars);
}

enum class PickerAction { Choose, CancelStrip, CancelTarget, CloseStrip, CloseTarget, DestroyStrip, DestroyTarget };
struct PickerResult {
    bool executed = false;
    bool interacted = false;
    bool timedOut = false;
    bool aborted = false;
    QStringList visited;
};

PickerResult toolbarPicker(WindowManager& manager, WindowId window, PickerAction action,
                           int targetRow = 0, const std::function<void()>& destroy = {})
{
    PickerResult result;
    int stage = 0;
    QTimer navigation, watchdog;
    navigation.setInterval(20); watchdog.setSingleShot(true);
    QObject::connect(&navigation, &QTimer::timeout, qApp, [&] {
        auto* picker = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!picker) return;
        const bool first = stage == 0;
        if ((first && picker->objectName() != "ToolbarMovePicker")
            || (stage == 1 && picker->objectName() != "ToolbarMoveTargetPicker")) return;
        if (stage < 2 && !result.visited.contains(picker->objectName())) result.visited.append(picker->objectName());
        if ((first && action == PickerAction::CancelStrip) || (stage == 1 && action == PickerAction::CancelTarget)) {
            navigation.stop(); result.interacted = true; QTest::keyClick(picker, Qt::Key_Escape); return;
        }
        if ((first && action == PickerAction::CloseStrip) || (stage == 1 && action == PickerAction::CloseTarget)) {
            navigation.stop(); result.interacted = true; result.aborted = manager.requestCloseWindow(window); return;
        }
        if ((first && action == PickerAction::DestroyStrip) || (stage == 1 && action == PickerAction::DestroyTarget)) {
            navigation.stop(); result.interacted = true; result.aborted = true; destroy(); return;
        }
        if (first) {
            for (auto* candidate : picker->actions()) if (candidate->data().toString() == "workspace") {
                picker->setActiveAction(candidate); ++stage; QTest::keyClick(picker, Qt::Key_Return); return;
            }
        } else if (stage == 1) {
            for (auto* candidate : picker->actions()) if (candidate->menu() && candidate->text() == QStringLiteral("底部")) {
                picker->setActiveAction(candidate); ++stage; QTest::keyClick(picker, Qt::Key_Right); return;
            }
        } else {
            for (auto* candidate : picker->actions()) {
                const auto target = candidate->data().toMap();
                if (target.value("area").toString() != "bottom" || target.value("row").toInt() != targetRow) continue;
                navigation.stop(); result.interacted = true; picker->setActiveAction(candidate);
                QTest::keyClick(picker, Qt::Key_Return); return;
            }
        }
    });
    QObject::connect(&watchdog, &QTimer::timeout, qApp, [&] {
        result.timedOut = true; navigation.stop();
        for (auto* widget : QApplication::topLevelWidgets())
            if (auto* picker = qobject_cast<QMenu*>(widget); picker && picker->isVisible()) picker->close();
    });
    navigation.start(); watchdog.start(2000);
    result.executed = manager.execute("window.toolbar.move", {}, window);
    navigation.stop(); watchdog.stop();
    return result;
}

} // namespace

class ViewCommandsTest final : public QObject {
    Q_OBJECT
    translation_test::ChineseUiCatalog chineseUi_;
private slots:
    void initTestCase() { QVERIFY(chineseUi_.install()); qApp->setQuitOnLastWindowClosed(false); }
    void cleanup() { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }
    void functionKeysToggleTheirActualFullscreenScope_data() { modeRows(); }
    void functionKeysToggleTheirActualFullscreenScope();
    void escapeExitsFullscreenAndItsZenProjection_data() { modeRows(); }
    void escapeExitsFullscreenAndItsZenProjection();
    void chordEscapeCancelsThePrefixBeforeExitingFullscreen();
    void visibilityShortcutsChangeOnlyTheirOwnWindowPreferences();
    void viewMenuReflectsFullscreenZenAndAllFourVisibilityPreferences();
    void settingsShowRestoresZenPreferences_data();
    void settingsShowRestoresZenPreferences();
    void movedSettingsEditTheirCurrentWindow();
    void displaySettingsSearchKeepsExactRowsAndSectionHeadings();
    void bottomToolbarAndHiddenChromeUseNoExtraLayoutSpace_data();
    void bottomToolbarAndHiddenChromeUseNoExtraLayoutSpace();
    void toolbarMoveCommandPreservesIdentityAndRejectsInvalidArguments();
    void toolbarMovePickersChooseExistingAndNewBottomRows();
    void toolbarMoveCancellationInEitherPhaseKeepsTheSnapshot();
    void closingTheWindowSafelyDismissesEitherToolbarMovePicker();
    void registryEnabledCanRegisterCommandsWithoutInvalidatingItsHandler();
    void commandAndToolbarPickerCanDestroyTheirManagerSafely();
    void zenChordStartsTheConfiguredScopeFromNormal_data() { modeRows(); }
    void zenChordStartsTheConfiguredScopeFromNormal();
    void settingsChooseTheZenDefaultAndEnableItFromNormal_data() { modeRows(); }
    void settingsChooseTheZenDefaultAndEnableItFromNormal();
};

void ViewCommandsTest::functionKeysToggleTheirActualFullscreenScope()
{
    QFETCH(ViewMode, mode); QFETCH(Qt::KeyboardModifiers, modifiers);
    WindowManager manager;
    const auto fixture = createFixture(manager);
    auto* window = manager.windowWidget(fixture.window);
    manager.showWindow(fixture.window); QVERIFY(QTest::qWaitForWindowExposed(window));
    QVERIFY(manager.focusPane(fixture.tab, fixture.pane));
    const auto text = fixture.editor->toPlainText();
    const auto model = manager.model().toJson();
    const auto* command = manager.commands().find(modeCommand(mode)); QVERIFY(command);
    QCOMPARE(command->shortcut, QKeySequence(QKeyCombination(modifiers, Qt::Key_F11)));
    QTest::keyClick(fixture.editor, Qt::Key_F11, modifiers);
    QTRY_COMPARE(manager.viewMode(fixture.window), mode); QTRY_VERIFY(window->isFullScreen());
    QCOMPARE(manager.model().toJson(), model); QCOMPARE(manager.paneWidget(fixture.pane), fixture.editor);
    QCOMPARE(fixture.editor->toPlainText(), text);
    QTest::keyClick(fixture.editor, Qt::Key_F11, modifiers);
    QTRY_COMPARE(manager.viewMode(fixture.window), ViewMode::Normal); QTRY_VERIFY(!window->isFullScreen());
    QCOMPARE(fixture.editor->toPlainText(), text);
}

void ViewCommandsTest::escapeExitsFullscreenAndItsZenProjection()
{
    QFETCH(ViewMode, mode); QFETCH(Qt::KeyboardModifiers, modifiers);
    WindowManager manager;
    const auto fixture = createFixture(manager);
    manager.showWindow(fixture.window); QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(fixture.window)));
    setPreferences(manager, fixture.window, {false, true, false, true});
    const auto preferences = manager.preferredBarVisibility(fixture.window);
    const auto text = fixture.editor->toPlainText();
    QTest::keyClick(fixture.editor, Qt::Key_F11, modifiers);
    QTRY_COMPARE(manager.viewMode(fixture.window), mode);
    QTest::keyClick(fixture.editor, Qt::Key_K, Qt::ControlModifier);
    QVERIFY(!manager.zenMode(fixture.window));
    QTest::keyClick(fixture.editor, Qt::Key_Z);
    QTRY_VERIFY(manager.zenMode(fixture.window));
    QCOMPARE(manager.barVisibility(fixture.window), (BarVisibility{false, false, false, false}));
    QCOMPARE(manager.preferredBarVisibility(fixture.window), preferences);
    QTest::keyClick(fixture.editor, Qt::Key_Escape);
    QTRY_COMPARE(manager.viewMode(fixture.window), ViewMode::Normal);
    QVERIFY(!manager.zenMode(fixture.window)); QCOMPARE(manager.barVisibility(fixture.window), preferences);
    QCOMPARE(fixture.editor->toPlainText(), text);
}

void ViewCommandsTest::chordEscapeCancelsThePrefixBeforeExitingFullscreen()
{
    WindowManager manager;
    const auto fixture = createFixture(manager);
    manager.showWindow(fixture.window); QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(fixture.window)));
    QTest::keyClick(fixture.editor, Qt::Key_F11);
    QTRY_COMPARE(manager.viewMode(fixture.window), ViewMode::WindowFullscreen);
    const auto text = fixture.editor->toPlainText();
    QTest::keyClick(fixture.editor, Qt::Key_K, Qt::ControlModifier);
    QTest::keyClick(fixture.editor, Qt::Key_Escape);
    QCOMPARE(manager.viewMode(fixture.window), ViewMode::WindowFullscreen); QVERIFY(!manager.zenMode(fixture.window));
    QTest::keyClick(fixture.editor, Qt::Key_K, Qt::ControlModifier); QTest::keyClick(fixture.editor, Qt::Key_Z);
    QTRY_VERIFY(manager.zenMode(fixture.window));
    QTest::keyClick(fixture.editor, Qt::Key_K, Qt::ControlModifier); QTest::keyClick(fixture.editor, Qt::Key_Z);
    QTRY_VERIFY(!manager.zenMode(fixture.window));
    QTest::keyClick(fixture.editor, Qt::Key_Escape); QTRY_COMPARE(manager.viewMode(fixture.window), ViewMode::Normal);
    QCOMPARE(fixture.editor->toPlainText(), text);
}

void ViewCommandsTest::visibilityShortcutsChangeOnlyTheirOwnWindowPreferences()
{
    WindowManager manager;
    const auto fixture = createFixture(manager), other = createFixture(manager);
    manager.showWindow(fixture.window); QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(fixture.window)));
    const auto otherPreferences = manager.preferredBarVisibility(other.window);
    const auto text = fixture.editor->toPlainText();
    for (const auto& action : {std::pair{Qt::Key_H, WindowBar::Toolbars}, std::pair{Qt::Key_S, WindowBar::StatusBar},
                               std::pair{Qt::Key_T, WindowBar::TabBars}, std::pair{Qt::Key_B, WindowBar::SideBars}}) {
        QTest::keyClick(fixture.editor, action.first, Qt::ControlModifier | Qt::AltModifier);
        QVERIFY(!shown(manager.barVisibility(fixture.window), action.second));
        QTest::keyClick(fixture.editor, Qt::Key_F11); QTRY_VERIFY(manager.windowWidget(fixture.window)->isFullScreen());
        QVERIFY(!shown(manager.barVisibility(fixture.window), action.second));
        QTest::keyClick(fixture.editor, action.first, Qt::ControlModifier | Qt::AltModifier);
        QVERIFY(shown(manager.barVisibility(fixture.window), action.second));
        QTest::keyClick(fixture.editor, Qt::Key_Escape); QTRY_COMPARE(manager.viewMode(fixture.window), ViewMode::Normal);
        QVERIFY(shown(manager.barVisibility(fixture.window), action.second));
        QCOMPARE(manager.preferredBarVisibility(other.window), otherPreferences);
    }
    QCOMPARE(fixture.editor->toPlainText(), text);
}

void ViewCommandsTest::viewMenuReflectsFullscreenZenAndAllFourVisibilityPreferences()
{
    WindowManager manager;
    const auto fixture = createFixture(manager);
    auto* window = manager.windowWidget(fixture.window);
    const QStringList bars{"window.toolbars.toggle", "window.statusBar.toggle", "window.tabBars.toggle", "window.sideBars.toggle"};
    for (const auto& id : bars) {
        auto* action = actionFor(window, id); QVERIFY(action); QVERIFY(action->isCheckable()); QVERIFY(action->isChecked());
        QVERIFY(action->shortcut().isEmpty());
    }
    auto* zen = actionFor(window, "window.zen.toggle"); auto* exit = actionFor(window, "window.fullscreen.exit");
    QVERIFY(zen && exit); QVERIFY(zen->isEnabled()); QVERIFY(!zen->isChecked()); QVERIFY(!exit->isEnabled());
    setPreferences(manager, fixture.window, {false, true, false, true});
    for (const auto mode : {ViewMode::WindowFullscreen, ViewMode::GroupFullscreen, ViewMode::PaneFullscreen}) {
        QVERIFY(manager.setViewMode(fixture.window, mode));
        for (const auto candidate : {ViewMode::WindowFullscreen, ViewMode::GroupFullscreen, ViewMode::PaneFullscreen}) {
            auto* action = actionFor(window, modeCommand(candidate)); QVERIFY(action);
            QTRY_COMPARE(action->isChecked(), mode == candidate);
        }
        QVERIFY(zen->isEnabled()); QVERIFY(exit->isEnabled());
    }
    QVERIFY(!actionFor(window, bars[0])->isChecked()); QVERIFY(actionFor(window, bars[1])->isChecked());
    QVERIFY(!actionFor(window, bars[2])->isChecked()); QVERIFY(actionFor(window, bars[3])->isChecked());
    QVERIFY(manager.setZenMode(fixture.window, true));
    QTRY_VERIFY(zen->isChecked()); for (const auto& id : bars) QTRY_VERIFY(!actionFor(window, id)->isChecked());
    actionFor(window, bars[0])->trigger();
    QTRY_VERIFY(!manager.zenMode(fixture.window));
    QCOMPARE(manager.barVisibility(fixture.window), (BarVisibility{true, true, false, true}));
    QVERIFY(actionFor(window, bars[0])->isChecked()); QVERIFY(actionFor(window, bars[1])->isChecked());
    QVERIFY(!actionFor(window, bars[2])->isChecked()); QVERIFY(actionFor(window, bars[3])->isChecked());
    QVERIFY(!zen->isChecked());
}

void ViewCommandsTest::settingsShowRestoresZenPreferences_data()
{
    QTest::addColumn<WindowBar>("bar");
    QTest::newRow("toolbars") << WindowBar::Toolbars; QTest::newRow("status") << WindowBar::StatusBar;
    QTest::newRow("tabs") << WindowBar::TabBars; QTest::newRow("sidebars") << WindowBar::SideBars;
}

void ViewCommandsTest::settingsShowRestoresZenPreferences()
{
    QFETCH(WindowBar, bar);
    WindowManager manager;
    const auto fixture = createFixture(manager);
    manager.showSettings(fixture.window);
    auto* page = settingsPage(manager); QVERIFY(page);
    QVERIFY(!page->findChild<QCheckBox*>("WindowZenMode"));
    QVERIFY(!page->findChild<QComboBox*>("WindowViewMode"));
    QCOMPARE(manager.viewMode(fixture.window), ViewMode::Normal); QVERIFY(!manager.zenMode(fixture.window));
    QSet<QString> visibilityIds;
    for (auto* box : page->findChildren<QCheckBox*>()) if (box->objectName().startsWith("window.visibility.")) visibilityIds.insert(box->objectName());
    QCOMPARE(visibilityIds, (QSet<QString>{"window.visibility.toolbars", "window.visibility.statusBar", "window.visibility.tabBars", "window.visibility.sideBars"}));
    const BarVisibility preferences{false, true, false, true}; setPreferences(manager, fixture.window, preferences);
    QVERIFY(manager.execute("window.fullscreen.toggle", {}, fixture.window));
    QVERIFY(manager.execute("window.zen.toggle", {}, fixture.window));
    QTRY_VERIFY(manager.zenMode(fixture.window));
    QCOMPARE(manager.viewMode(fixture.window), ViewMode::WindowFullscreen);
    for (const auto& id : visibilityIds) { auto* box = page->findChild<QCheckBox*>(id); QVERIFY(box); QVERIFY(!box->isChecked()); }
    auto* control = page->findChild<QCheckBox*>(visibilityKey(bar)); QVERIFY(control);
    auto* scroll = page->findChild<QScrollArea*>("SettingsScrollArea"); QVERIFY(scroll);
    scroll->ensureWidgetVisible(control); QTRY_VERIFY(control->isVisible());
    QTest::mouseClick(control, Qt::LeftButton, Qt::NoModifier, QPoint(8, control->height() / 2));
    QTRY_VERIFY(!manager.zenMode(fixture.window));
    QCOMPARE(manager.viewMode(fixture.window), ViewMode::WindowFullscreen);
    QCOMPARE(manager.preferredBarVisibility(fixture.window), withShown(preferences, bar));
    QCOMPARE(manager.barVisibility(fixture.window), withShown(preferences, bar));
    for (const auto candidate : {WindowBar::Toolbars, WindowBar::StatusBar, WindowBar::TabBars, WindowBar::SideBars})
        QCOMPARE(page->findChild<QCheckBox*>(visibilityKey(candidate))->isChecked(), shown(withShown(preferences, bar), candidate));
}

void ViewCommandsTest::movedSettingsEditTheirCurrentWindow()
{
    WindowManager manager;
    const auto first = createFixture(manager), second = createFixture(manager);
    const BarVisibility sourcePreferences{true, true, false, false}, targetPreferences{false, false, true, true};
    setPreferences(manager, first.window, sourcePreferences); setPreferences(manager, second.window, targetPreferences);
    manager.showSettings(first.window);
    QPointer<SettingsPage> page = settingsPage(manager); QVERIFY(page);
    const auto tab = tabFor(manager, page); QVERIFY(!tab.isNull());
    const auto pane = manager.model().tab(tab)->activePane;
    QVERIFY(manager.moveTabToWindow(tab, second.window));
    QCOMPARE(manager.paneWidget(pane), page.data()); QCOMPARE(page->window(), manager.windowWidget(second.window));
    for (const auto bar : {WindowBar::Toolbars, WindowBar::StatusBar, WindowBar::TabBars, WindowBar::SideBars})
        QTRY_COMPARE(page->findChild<QCheckBox*>(visibilityKey(bar))->isChecked(), shown(targetPreferences, bar));
    QVERIFY(!page->findChild<QComboBox*>("WindowViewMode"));
    QVERIFY(!page->findChild<QCheckBox*>("WindowZenMode"));
    const auto currentWindow = page->window()->property("windowId").toUuid(); QCOMPARE(currentWindow, second.window);
    QVERIFY(manager.execute("window.fullscreen.toggle", {}, currentWindow));
    QTRY_COMPARE(manager.viewMode(second.window), ViewMode::WindowFullscreen);
    QCOMPARE(manager.viewMode(first.window), ViewMode::Normal);
    QVERIFY(manager.execute("window.zen.toggle", {}, currentWindow)); QTRY_VERIFY(manager.zenMode(second.window));
    QVERIFY(!manager.zenMode(first.window));
    auto* toolbar = page->findChild<QCheckBox*>("window.visibility.toolbars"); QVERIFY(toolbar); toolbar->click();
    QTRY_VERIFY(!manager.zenMode(second.window));
    QCOMPARE(manager.viewMode(second.window), ViewMode::WindowFullscreen);
    QCOMPARE(manager.preferredBarVisibility(second.window), (BarVisibility{true, false, true, true}));
    QCOMPARE(manager.preferredBarVisibility(first.window), sourcePreferences);
    QCOMPARE(manager.paneWidget(pane), page.data()); QCOMPARE(manager.model().groupForTab(tab), manager.context(second.window).group);
}

void ViewCommandsTest::displaySettingsSearchKeepsExactRowsAndSectionHeadings()
{
    WindowManager manager;
    const auto fixture = createFixture(manager); manager.showSettings(fixture.window);
    auto* page = settingsPage(manager); QVERIFY(page);
    auto* search = page->findChild<QLineEdit*>("SettingsSearch"); QVERIFY(search);
    QSet<QString> allRows;
    for (auto* row : page->findChildren<QWidget*>("SettingRow")) allRows.insert(row->property("settingId").toString());
    QCOMPARE(allRows, (QSet<QString>{"window.newTabPosition", "window.dragOutsideKind", "window.closeEmptyWorkbench",
        "window.revealDuringDrag", "window.revealDelayMs", "window.zenFullscreenMode", "window.hideToolbarOptionsWhenLocked",
        "window.captionButtonOrder",
        "window.visibility.toolbars", "window.visibility.statusBar", "window.visibility.tabBars", "window.visibility.sideBars",
        "window.hideSingleTabWorkbench", "window.hideSingleTabFloating", "tabs.centralPlacement", "tabs.pinnedPlacement",
        "tabs.multiline", "tabs.wheelExpandRows", "tabs.showIcons", "tabs.showNewTabButton", "tabs.showNumbers",
        "tabs.showSidebarIcons", "tabs.showSidebarNumbers", "tabs.colors", "tabs.clipTitles", "tabs.maximumTitleWidth",
        "tabs.clipSidebarTitles", "tabs.maximumSidebarTitleWidth", "tabs.stickySizing"}));
    search->setText("window.visibility.");
    QSet<QString> visibleRows, visibleSections;
    for (auto* row : page->findChildren<QWidget*>("SettingRow")) if (!row->isHidden()) visibleRows.insert(row->property("settingId").toString());
    for (auto* heading : page->findChildren<QLabel*>("SettingsCategoryHeading")) if (!heading->isHidden()) visibleSections.insert(heading->property("categoryId").toString());
    QCOMPARE(visibleRows, (QSet<QString>{"window.visibility.toolbars", "window.visibility.statusBar", "window.visibility.tabBars", "window.visibility.sideBars"}));
    QCOMPARE(visibleSections, (QSet<QString>{"window", "window/display"}));
    for (const auto* removed : {"window.zenMode", "window.viewMode"}) {
        search->setText(QString::fromLatin1(removed)); visibleRows.clear(); visibleSections.clear();
        for (auto* row : page->findChildren<QWidget*>("SettingRow")) if (!row->isHidden()) visibleRows.insert(row->property("settingId").toString());
        for (auto* heading : page->findChildren<QLabel*>("SettingsCategoryHeading")) if (!heading->isHidden()) visibleSections.insert(heading->property("categoryId").toString());
        QCOMPARE(visibleRows, QSet<QString>{}); QCOMPARE(visibleSections, QSet<QString>{});
    }
    search->setText("window.zenFullscreenMode"); visibleRows.clear(); visibleSections.clear();
    for (auto* row : page->findChildren<QWidget*>("SettingRow")) if (!row->isHidden()) visibleRows.insert(row->property("settingId").toString());
    for (auto* heading : page->findChildren<QLabel*>("SettingsCategoryHeading")) if (!heading->isHidden()) visibleSections.insert(heading->property("categoryId").toString());
    QCOMPARE(visibleRows, (QSet<QString>{"window.zenFullscreenMode"}));
    QCOMPARE(visibleSections, (QSet<QString>{"window", "window/display"}));
}

void ViewCommandsTest::bottomToolbarAndHiddenChromeUseNoExtraLayoutSpace_data()
{
    QTest::addColumn<bool>("fullscreen"); QTest::newRow("normal") << false; QTest::newRow("fullscreen") << true;
}

void ViewCommandsTest::bottomToolbarAndHiddenChromeUseNoExtraLayoutSpace()
{
    QFETCH(bool, fullscreen);
    WindowManager manager;
    const auto fixture = createFixture(manager);
    const auto pinned = manager.addTab(fixture.window, new QLabel("Bottom fixed panel"), {"Pinned bottom", "test.label", {}});
    QVERIFY(manager.moveTabToEdge(pinned, Edge::Bottom, true));
    const auto hidden = manager.addTab(fixture.window, new QLabel("Bottom rail content"), {"Bottom rail", "test.label", {}});
    QVERIFY(manager.moveTabToEdge(hidden, Edge::Bottom, false));
    QVERIFY(manager.activateTab(fixture.tab));
    auto* window = dynamic_cast<WorkspaceWindow*>(manager.windowWidget(fixture.window)); QVERIFY(window);
    auto* host = window->strips(); QVERIFY(host);
    QVERIFY(host->moveStrip("workspace", CommandStripHost::Area::Bottom, 0, 0));
    manager.showWindow(fixture.window); QVERIFY(QTest::qWaitForWindowExposed(window));
    if (fullscreen) QVERIFY(manager.setViewMode(fixture.window, ViewMode::WindowFullscreen));
    auto* status = window->findChild<QLabel*>("StatusBar"); auto* top = window->findChild<QWidget*>("WindowHeader");
    auto* bottom = host->bottomArea(); auto* surface = window->surface(); QVERIFY(status && top && bottom && surface);
    QTRY_VERIFY(top->isVisible() && bottom->isVisible() && status->isVisible());
    QTRY_COMPARE(globalRect(surface).bottom() + 1, globalRect(bottom).top());
    QTRY_COMPARE(globalRect(bottom).bottom() + 1, globalRect(status).top());
    const auto rail = surface->edgeBarRects().value(Edge::Bottom); QVERIFY(!rail.isEmpty());
    QVERIFY(rail.bottom() < globalRect(bottom).top());
    QVERIFY(manager.setToolbarsVisible(fixture.window, false));
    QTRY_VERIFY(top->isHidden() && bottom->isHidden());
    QTRY_COMPARE(globalRect(surface).top(), window->mapToGlobal(window->layout()->contentsRect().topLeft()).y());
    QTRY_COMPARE(globalRect(surface).bottom() + 1, globalRect(status).top());
    QVERIFY(manager.setStatusBarVisible(fixture.window, false)); QTRY_VERIFY(status->isHidden());
    QTRY_COMPARE(globalRect(surface), QRect(window->mapToGlobal(window->layout()->contentsRect().topLeft()), window->layout()->contentsRect().size()));
    QVERIFY(manager.setStatusBarVisible(fixture.window, true)); QTRY_COMPARE(globalRect(surface).bottom() + 1, globalRect(status).top());
    QVERIFY(manager.setToolbarsVisible(fixture.window, true));
    QTRY_VERIFY(top->isVisible() && bottom->isVisible());
    QTRY_COMPARE(globalRect(top).bottom() + 1, globalRect(surface).top());
    QTRY_COMPARE(globalRect(surface).bottom() + 1, globalRect(bottom).top());
    QTRY_COMPARE(globalRect(bottom).bottom() + 1, globalRect(status).top());
}

void ViewCommandsTest::toolbarMoveCommandPreservesIdentityAndRejectsInvalidArguments()
{
    WindowManager manager;
    const auto fixture = createFixture(manager);
    auto* window = dynamic_cast<WorkspaceWindow*>(manager.windowWidget(fixture.window)); QVERIFY(window);
    auto* host = window->strips(); QVERIFY(host);
    QPointer<QWidget> strip = host->stripWidget("workspace"); QVERIFY(strip);
    QPointer<QWidget> viewport = strip->findChild<QWidget*>("StripViewport"); QVERIFY(viewport);
    const auto settings = host->saveLayout().value("strips").toObject().value("workspace");
    QVERIFY(manager.execute("window.toolbar.move", {{"strip", "workspace"}, {"area", "bottom"}, {"row", 0}, {"index", 0}}, fixture.window));
    QCOMPARE(host->stripOrder(CommandStripHost::Area::Bottom, 0), QStringList{"workspace"});
    QVERIFY(manager.execute("window.toolbar.move", {{"strip", "menu"}, {"area", "bottom"}, {"row", 0}, {"index", 0}}, fixture.window));
    QCOMPARE(host->stripOrder(CommandStripHost::Area::Bottom, 0), (QStringList{"menu", "workspace"}));
    const auto newTopRow = host->rowCount(CommandStripHost::Area::Top);
    QVERIFY(manager.execute("window.toolbar.move", {{"strip", "workspace"}, {"area", "top"}, {"row", newTopRow}, {"index", 0}}, fixture.window));
    QVERIFY(manager.execute("window.toolbar.move", {{"strip", "menu"}, {"area", "top"}, {"row", newTopRow}, {"index", 0}}, fixture.window));
    QCOMPARE(host->stripOrder(CommandStripHost::Area::Top, newTopRow), (QStringList{"menu", "workspace"}));
    QCOMPARE(host->stripWidget("workspace"), strip.data()); QCOMPARE(strip->findChild<QWidget*>("StripViewport"), viewport.data());
    QCOMPARE(host->saveLayout().value("strips").toObject().value("workspace"), settings);
    QCOMPARE(manager.paneWidget(fixture.pane), fixture.editor);
    const auto before = manager.saveLayout();
    const QVector<QVariantMap> invalid{
        {{"strip", "missing"}, {"area", "top"}}, {{"strip", "workspace"}, {"area", "left"}},
        {{"strip", "workspace"}, {"area", "bottom"}, {"row", -1}},
        {{"strip", "workspace"}, {"area", "top"}, {"row", 1.5}},
        {{"strip", "workspace"}, {"area", "top"}, {"row", "invalid"}},
        {{"strip", "workspace"}, {"area", "top"}, {"row", true}},
        {{"strip", "workspace"}, {"area", "bottom"}, {"row", 100000}},
        {{"strip", "workspace"}, {"area", "top"}, {"row", 0}, {"index", -2}},
        {{"strip", "workspace"}, {"area", "top"}, {"row", 0}, {"index", 0.5}},
        {{"strip", "workspace"}, {"area", "top"}, {"row", 0}, {"index", "invalid"}},
        {{"strip", "workspace"}, {"area", "top"}, {"row", 0}, {"index", true}},
        {{"strip", "workspace"}, {"area", "top"}, {"row", 0}, {"index", 100000}}};
    QSignalSpy changes(&manager, &WindowManager::layoutChanged);
    for (const auto& args : invalid) {
        QVERIFY(!manager.execute("window.toolbar.move", args, fixture.window));
        QCOMPARE(manager.saveLayout(), before); QCOMPARE(changes.count(), 0);
    }
}

void ViewCommandsTest::toolbarMovePickersChooseExistingAndNewBottomRows()
{
    for (const int targetRow : {0, 1}) {
        WindowManager manager;
        const auto fixture = createFixture(manager);
        auto* window = dynamic_cast<WorkspaceWindow*>(manager.windowWidget(fixture.window)); QVERIFY(window);
        auto* host = window->strips(); QVERIFY(host);
        host->addStrip("bottom-existing", new QLabel("Existing bottom strip"), 1);
        QVERIFY(manager.moveToolbar(fixture.window, "bottom-existing", ToolbarArea::Bottom, 0));
        QPointer<QWidget> strip = host->stripWidget("workspace"); QVERIFY(strip);
        manager.showWindow(fixture.window); QVERIFY(QTest::qWaitForWindowExposed(window));
        const auto result = toolbarPicker(manager, fixture.window, PickerAction::Choose, targetRow);
        QVERIFY(result.executed && result.interacted && !result.timedOut);
        QCOMPARE(result.visited, (QStringList{"ToolbarMovePicker", "ToolbarMoveTargetPicker"}));
        QCOMPARE(host->stripArea("workspace"), CommandStripHost::Area::Bottom);
        QCOMPARE(host->stripOrder(CommandStripHost::Area::Bottom, targetRow),
            (targetRow ? QStringList{"workspace"} : QStringList{"bottom-existing", "workspace"}));
        QCOMPARE(host->stripWidget("workspace"), strip.data()); QCOMPARE(manager.paneWidget(fixture.pane), fixture.editor);
        QVERIFY(!QApplication::activePopupWidget());
    }
}

void ViewCommandsTest::toolbarMoveCancellationInEitherPhaseKeepsTheSnapshot()
{
    for (const auto phase : {PickerAction::CancelStrip, PickerAction::CancelTarget}) {
        WindowManager manager;
        const auto fixture = createFixture(manager);
        manager.showWindow(fixture.window); QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(fixture.window)));
        const auto before = manager.saveLayout();
        const auto result = toolbarPicker(manager, fixture.window, phase);
        QVERIFY(result.executed && result.interacted && !result.timedOut);
        QCOMPARE(result.visited, (phase == PickerAction::CancelStrip ? QStringList{"ToolbarMovePicker"}
            : QStringList{"ToolbarMovePicker", "ToolbarMoveTargetPicker"}));
        QCOMPARE(manager.saveLayout(), before); QVERIFY(!QApplication::activePopupWidget());
    }
}

void ViewCommandsTest::closingTheWindowSafelyDismissesEitherToolbarMovePicker()
{
    for (const auto phase : {PickerAction::CloseStrip, PickerAction::CloseTarget}) {
        WindowManager manager;
        const auto fixture = createFixture(manager);
        manager.showWindow(fixture.window); QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(fixture.window)));
        const auto result = toolbarPicker(manager, fixture.window, phase);
        QVERIFY(!result.executed && result.interacted && result.aborted && !result.timedOut);
        QVERIFY(!manager.model().window(fixture.window)); QVERIFY(!QApplication::activePopupWidget());
    }
}

void ViewCommandsTest::registryEnabledCanRegisterCommandsWithoutInvalidatingItsHandler()
{
    WindowManager manager;
    const auto fixture = createFixture(manager);
    int enabled = 0, executed = 0; bool registered = true;
    QVERIFY(manager.commands().add({"test.reentrant-enabled", "Reentrant command", "test", {},
        [&](const CommandContext&) {
            ++enabled;
            for (int index = 0; index < 100; ++index)
                registered = manager.commands().add({QString("test.contribution.%1").arg(index), QString::number(index), "test", {}, {},
                    [](const CommandContext&) { return true; }}) && registered;
            return true;
        }, [&](const CommandContext& context) { ++executed; return context.window == fixture.window; }}));
    const auto before = manager.commands().commands().size();
    QVERIFY(manager.commands().execute("test.reentrant-enabled", manager.context(fixture.window)));
    QVERIFY(registered); QCOMPARE(enabled, 1); QCOMPARE(executed, 1);
    QCOMPARE(manager.commands().commands().size(), before + 100);
}

void ViewCommandsTest::commandAndToolbarPickerCanDestroyTheirManagerSafely()
{
    {
        auto manager = std::make_unique<WindowManager>();
        const auto fixture = createFixture(*manager);
        QPointer<WindowManager> observer(manager.get());
        int executed = 0;
        QVERIFY(manager->commands().add({"test.destroy-enabled-owner", "Destroy enabled owner", "test", {},
            [&](const CommandContext&) { manager.reset(); return true; },
            [&](const CommandContext&) { ++executed; return true; }}));
        auto* raw = manager.get();
        QVERIFY(!raw->execute("test.destroy-enabled-owner", {}, fixture.window));
        QVERIFY(!observer); QCOMPARE(executed, 0);
    }
    {
        auto manager = std::make_unique<WindowManager>();
        const auto fixture = createFixture(*manager);
        QPointer<WindowManager> observer(manager.get());
        QVERIFY(manager->commands().add({"test.destroy-owner", "Destroy command owner", "test", {}, {},
            [&](const CommandContext&) { manager.reset(); return false; }}));
        auto* raw = manager.get();
        QVERIFY(!raw->execute("test.destroy-owner", {}, fixture.window)); QVERIFY(!observer);
    }
    for (const auto phase : {PickerAction::DestroyStrip, PickerAction::DestroyTarget}) {
        auto manager = std::make_unique<WindowManager>();
        const auto fixture = createFixture(*manager);
        manager->showWindow(fixture.window); QVERIFY(QTest::qWaitForWindowExposed(manager->windowWidget(fixture.window)));
        QPointer<WindowManager> observer(manager.get());
        const auto result = toolbarPicker(*manager, fixture.window, phase, 0, [&] { manager.reset(); });
        QVERIFY(!result.executed && result.interacted && result.aborted && !result.timedOut);
        QVERIFY(!observer); QVERIFY(!QApplication::activePopupWidget());
    }
}

void ViewCommandsTest::zenChordStartsTheConfiguredScopeFromNormal()
{
    QFETCH(ViewMode, mode);
    WindowManager manager; const auto fixture = createFixture(manager);
    auto behavior = manager.behavior(); behavior.zenFullscreenMode = mode; manager.setBehavior(behavior);
    manager.showWindow(fixture.window); QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(fixture.window)));
    QVERIFY(manager.focusPane(fixture.tab, fixture.pane));
    setPreferences(manager, fixture.window, {false, true, false, true});
    const auto preferred = manager.preferredBarVisibility(fixture.window);
    const auto text = fixture.editor->toPlainText(); const auto model = manager.model().toJson();
    const auto* command = manager.commands().find("window.zen.toggle"); QVERIFY(command && command->enabled(manager.context(fixture.window)));
    auto* menuAction = actionFor(manager.windowWidget(fixture.window), "window.zen.toggle"); QVERIFY(menuAction && menuAction->isEnabled());
    QTest::keyClick(fixture.editor, Qt::Key_K, Qt::ControlModifier); QVERIFY(!manager.zenMode(fixture.window));
    QTest::keyClick(fixture.editor, Qt::Key_Z);
    QTRY_VERIFY(manager.zenMode(fixture.window)); QCOMPARE(manager.viewMode(fixture.window), mode);
    QTRY_VERIFY(manager.windowWidget(fixture.window)->isFullScreen());
    QCOMPARE(manager.barVisibility(fixture.window), (BarVisibility{false, false, false, false}));
    QCOMPARE(manager.preferredBarVisibility(fixture.window), preferred);
    QCOMPARE(manager.model().toJson(), model); QCOMPARE(fixture.editor->toPlainText(), text);
    QTest::keyClick(fixture.editor, Qt::Key_K, Qt::ControlModifier); QTest::keyClick(fixture.editor, Qt::Key_Z);
    QTRY_VERIFY(!manager.zenMode(fixture.window)); QCOMPARE(manager.viewMode(fixture.window), mode);
    QCOMPARE(manager.barVisibility(fixture.window), preferred);
    QTest::keyClick(fixture.editor, Qt::Key_Escape); QTRY_COMPARE(manager.viewMode(fixture.window), ViewMode::Normal);
    QCOMPARE(fixture.editor->toPlainText(), text);
}

void ViewCommandsTest::settingsChooseTheZenDefaultAndEnableItFromNormal()
{
    QFETCH(ViewMode, mode);
    WindowManager manager; const auto fixture = createFixture(manager); manager.showSettings(fixture.window);
    auto* page = settingsPage(manager); QVERIFY(page);
    auto* choice = page->findChild<QComboBox*>("ZenFullscreenMode");
    QVERIFY(choice); QVERIFY(!page->findChild<QCheckBox*>("WindowZenMode"));
    QVERIFY(!page->findChild<QComboBox*>("WindowViewMode"));
    QCOMPARE(choice->count(), 3); QVERIFY(choice->findData(int(ViewMode::Normal)) < 0);
    QCOMPARE(choice->currentData().toInt(), int(ViewMode::WindowFullscreen));
    choice->setCurrentIndex(choice->findData(int(mode))); QCOMPARE(manager.behavior().zenFullscreenMode, mode);
    QCOMPARE(manager.viewMode(fixture.window), ViewMode::Normal); QVERIFY(!manager.zenMode(fixture.window));
    const auto preferred = manager.preferredBarVisibility(fixture.window);
    const auto model = manager.model().toJson(); const auto text = fixture.editor->toPlainText();
    QVERIFY(manager.execute("window.zen.toggle", {}, fixture.window));
    QTRY_VERIFY(manager.zenMode(fixture.window)); QCOMPARE(manager.viewMode(fixture.window), mode);
    QTRY_VERIFY(manager.windowWidget(fixture.window)->isFullScreen());
    QCOMPARE(manager.barVisibility(fixture.window), (BarVisibility{false, false, false, false}));
    for (const auto& entry : manager.saveLayout().value("windows").toArray()) QVERIFY(!entry.toObject().contains("viewState"));
    QVERIFY(manager.execute("window.zen.toggle", {}, fixture.window)); QTRY_VERIFY(!manager.zenMode(fixture.window));
    QCOMPARE(manager.viewMode(fixture.window), mode); QCOMPARE(manager.barVisibility(fixture.window), preferred);
    QCOMPARE(manager.model().toJson(), model); QCOMPARE(manager.paneWidget(fixture.pane), fixture.editor);
    QCOMPARE(fixture.editor->toPlainText(), text);
    auto behavior = manager.behavior(); behavior.zenFullscreenMode = ViewMode::WindowFullscreen; manager.setBehavior(behavior);
    QCOMPARE(choice->currentData().toInt(), int(ViewMode::WindowFullscreen));
}

QTEST_MAIN(ViewCommandsTest)
#include "view_commands_test.moc"
