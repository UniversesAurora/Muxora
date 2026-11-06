// SPDX-License-Identifier: MIT
// Phase 10.4d/k: menu bar commands, input and lifetime. Dimensions: window
// kind, visible/folded roots, Alt gestures, focus return, registry changes
// and owner destruction. Popup command trees are in quick_popup_menu_test.
#include "quick/popup_menu.hpp"
#include "quick/menu_bar_overflow.hpp"
#include "platform/keyboard_cues.hpp"
#include "native_input.hpp"
#include "quick_menu_test_support.hpp"
#include <window_system/quick_workspace.hpp>
#include <window_system/window_manager.hpp>
#include <window_system/interface_appearance.hpp>
#include <QContextMenuEvent>
#include <QQuickWindow>
#include <QTest>
#include <memory>

using namespace window_system;
using namespace quick_menu_test;

class QuickMenuBarTest final : public QObject {
    Q_OBJECT
private slots:
    void init()
    {
        QTest::failOnWarning(QRegularExpression(".*(Unable to assign|TypeError|ReferenceError|Binding loop).*"));
    }

    // As Windows menus: access keys are underlined only while Alt is held,
    // and in a menu opened with it until it closes, unless the system asks
    // for them always.
    void menuAccessKeysShowWithTheKeyboard()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto window = manager.createWindow(WindowKind::Workbench, {QStringLiteral("Main"), QSize(1000, 600)});
        manager.addTab(window, {QStringLiteral("Content")});
        QTRY_VERIFY(workspace.window(window));
        auto* view = workspace.window(window);
        view->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(view));
        auto* bar = namedItem(view->contentItem(), QStringLiteral("MenuStrip"));
        QVERIFY(bar);
        auto* session = qobject_cast<quick::PopupMenu*>(bar->property("session").value<QObject*>());
        QVERIFY(session);
        const bool always = keyboardCuesAlwaysShown();
        QCOMPARE(session->mnemonicsShown(), always);
        QTest::keyPress(view, Qt::Key_Alt, Qt::AltModifier);
        QVERIFY(session->mnemonicsShown());
        QTest::keyRelease(view, Qt::Key_Alt, Qt::NoModifier);
        QVERIFY(session->mnemonicsShown());
        QTest::keyClick(view, Qt::Key_F);
        QTRY_VERIFY(popup(view));
        QTest::keyClick(popup(view), Qt::Key_Escape);
        QTRY_VERIFY(!popup(view));
        QTRY_VERIFY(!session->overflow()->keyboardItem());
        QCOMPARE(session->mnemonicsShown(), always);
        // Alt+F opens the File menu; it keeps the underlines after Alt goes up.
        QTest::keyPress(view, Qt::Key_Alt, Qt::AltModifier);
        QTest::keyClick(view, Qt::Key_F, Qt::AltModifier);
        QTRY_VERIFY(popup(view));
        QTest::keyRelease(popup(view), Qt::Key_Alt, Qt::NoModifier);
        QVERIFY(session->mnemonicsShown());
        QTest::keyClick(popup(view), Qt::Key_Escape);
        QTRY_VERIFY(!popup(view));
        QTRY_COMPARE(session->mnemonicsShown(), always);
    }

    void menuBarNavigationCanReleaseTheWorkspace()
    {
        WindowManager manager(WindowManager::Presentation::External);
        auto workspace = std::make_unique<QuickWorkspace>(manager);
        workspace->setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto window = manager.createWindow(WindowKind::Workbench, {QStringLiteral("Main"), QSize(1000, 600)});
        const auto tab = manager.addTab(window, {QStringLiteral("Content")});
        QTRY_VERIFY(workspace->window(window));
        QPointer<QQuickWindow> view(workspace->window(window));
        view->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(view));
        workspace->content(manager.model().panes(tab).first())->forceActiveFocus();
        auto* bar = namedItem(view->contentItem(), QStringLiteral("MenuStrip"));
        QVERIFY(bar);
        auto* session = qobject_cast<quick::PopupMenu*>(bar->property("session").value<QObject*>());
        QVERIFY(session);
        auto* navigation = session->overflow();
        QObject::connect(navigation, &quick::MenuBarOverflow::keyboardModeChanged, &manager, [&] {
            if (navigation->keyboardItem()) workspace.reset();
        });
        QTest::keyPress(view, Qt::Key_Alt, Qt::AltModifier);
        QTest::keyRelease(view, Qt::Key_Alt, Qt::NoModifier);
        QTRY_VERIFY(!view);
        QVERIFY(!workspace);
    }

    void menuBarKeyboardNavigation_data()
    {
        QTest::addColumn<int>("width");
        QTest::newRow("wide") << 1000;
        QTest::newRow("folded") << 380;
    }

    void menuBarKeyboardNavigation()
    {
        QFETCH(int, width);
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto window = manager.createWindow(WindowKind::Workbench, {QStringLiteral("Main"), QSize(width, 600)});
        const auto tab = manager.addTab(window, {QStringLiteral("Content")});
        QTRY_VERIFY(workspace.window(window));
        auto* view = workspace.window(window);
        view->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(view));
        auto* content = workspace.content(manager.model().panes(tab).first());
        QVERIFY(content);
        content->forceActiveFocus();
        auto* bar = namedItem(view->contentItem(), QStringLiteral("MenuStrip"));
        QVERIFY(bar);
        auto* session = qobject_cast<quick::PopupMenu*>(bar->property("session").value<QObject*>());
        QVERIFY(session);
        auto* navigation = session->overflow();
        QTest::keyPress(view, Qt::Key_Alt, Qt::AltModifier);
        QTest::keyRelease(view, Qt::Key_Alt, Qt::NoModifier);
        QVERIFY(navigation->keyboardItem());
        QVERIFY(navigation->keyboardItem()->isVisible());
        QVERIFY(session->mnemonicsShown());
        QTest::keyClick(view, Qt::Key_Right);
        QVERIFY(navigation->keyboardItem());
        if (width == 1000) QCOMPARE(navigation->keyboardItem()->objectName(), QStringLiteral("view"));
        else QVERIFY(bar->property("firstHiddenIndex").toInt() < bar->property("count").toInt());
        QTest::keyClick(view, Qt::Key_Down);
        QTRY_VERIFY(popup(view));
        QTest::keyClick(popup(view), Qt::Key_Escape);
        QTRY_VERIFY(!popup(view));
        QTRY_VERIFY(!navigation->keyboardItem());
        QTRY_VERIFY(content->hasActiveFocus());
        QCOMPARE(session->mnemonicsShown(), keyboardCuesAlwaysShown());
        QTest::keyPress(view, Qt::Key_Alt, Qt::AltModifier);
        QTest::keyClick(view, Qt::Key_Escape, Qt::AltModifier);
        QTest::keyRelease(view, Qt::Key_Alt, Qt::NoModifier);
        QVERIFY(!navigation->keyboardItem());
        QTest::keyPress(view, Qt::Key_Alt, Qt::AltModifier);
        QTest::keyRelease(view, Qt::Key_Alt, Qt::NoModifier);
        QVERIFY(navigation->keyboardItem());
        QTest::keyPress(view, Qt::Key_Alt, Qt::AltModifier);
        QTest::keyRelease(view, Qt::Key_Alt, Qt::NoModifier);
        QVERIFY(!navigation->keyboardItem());
        QVERIFY(content->hasActiveFocus());
        QTest::keyPress(view, Qt::Key_Alt, Qt::AltModifier);
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, content->mapToScene(content->boundingRect().center()).toPoint());
        QTest::keyRelease(view, Qt::Key_Alt, Qt::NoModifier);
        QVERIFY(!navigation->keyboardItem());
        QTest::keyPress(view, Qt::Key_Alt, Qt::AltModifier);
        QTest::keyRelease(view, Qt::Key_Alt, Qt::NoModifier);
        QVERIFY(navigation->keyboardItem());
        QQuickWindow other;
        other.resize(200, 120); other.show(); other.requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(&other));
        QTRY_VERIFY(!navigation->keyboardItem());
        QVERIFY(content->hasFocus());
    }

    void menuBarUsesLateCommandsAndItsOwnWindow_data()
    {
        QTest::addColumn<bool>("floating");
        QTest::newRow("workbench") << false;
        QTest::newRow("floating") << true;
    }
    void menuBarUsesLateCommandsAndItsOwnWindow()
    {
        QFETCH(bool, floating);
        InterfaceAppearanceController appearance;
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto owner = manager.createWindow();
        manager.addTab(owner, {QStringLiteral("Owner")});
        const auto window = floating ? manager.createWindow(WindowKind::Floating, {}, owner) : owner;
        const auto tab = floating ? manager.addTab(window, {QStringLiteral("Floating")}) : manager.context(owner).tab;
        QTRY_VERIFY(workspace.window(window));
        auto* view = workspace.window(window);
        view->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(view));
        workspace.content(manager.model().tab(tab)->activePane)->forceActiveFocus();
        auto* bar = namedItem(view->contentItem(), QStringLiteral("MenuStrip")); QVERIFY(bar);
        bool enabled = false;
        CommandContext invoked;
        Command command;
        command.id = "test.menuBar"; command.title = "Run late command"; command.menuPath = {"tools"};
        command.enabled = [&](const CommandContext&) { return enabled; };
        command.execute = [&](const CommandContext& context) { invoked = context; return true; };
        QVERIFY(manager.commands().add(std::move(command)));
        auto changed = appearance.appearance(); changed.fontSize += 2; changed.zoomLevel = 0.5;
        QVERIFY(appearance.setAppearance(changed));
        QTest::keyClick(view, Qt::Key_T, Qt::AltModifier);
        QTRY_VERIFY(popup(view));
        auto* menu = popup(view); QVERIFY(QTest::qWaitForWindowExposed(menu));
        QTRY_VERIFY(namedItem(menu->contentItem(), QStringLiteral("test.menuBar")));
        QTest::keyClick(menu, Qt::Key_Left);
        QTRY_VERIFY(popup(view) && namedItem(popup(view)->contentItem(), QStringLiteral("window.select")));
        QTest::keyClick(popup(view), Qt::Key_Right);
        QTRY_VERIFY(popup(view) && namedItem(popup(view)->contentItem(), QStringLiteral("test.menuBar")));
        menu = popup(view); QVERIFY(QTest::qWaitForWindowExposed(menu));
        auto* action = namedItem(menu->contentItem(), QStringLiteral("test.menuBar"));
        QVERIFY(!action->isEnabled());
        enabled = true;
        QVERIFY(manager.commands().setShortcut("test.menuBar", QKeySequence("Ctrl+Alt+M")));
        QTRY_VERIFY(action->isEnabled());
        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, action->mapToScene(action->boundingRect().center()).toPoint());
        QTRY_COMPARE(invoked.window, window); QCOMPARE(invoked.tab, tab);
        QTRY_VERIFY(!popup(view));
        QTRY_VERIFY(workspace.content(manager.model().tab(tab)->activePane)->hasActiveFocus());
        const auto point = bar->mapToScene(bar->boundingRect().center()).toPoint();
        QContextMenuEvent context(QContextMenuEvent::Mouse, point, view->mapToGlobal(point));
        QCoreApplication::sendEvent(view, &context);
        QTRY_VERIFY(popup(view));
        menu = popup(view); QVERIFY(QTest::qWaitForWindowExposed(menu));
        action = namedItem(menu->contentItem(), QStringLiteral("presentation.toggleNumbers")); QVERIFY(action);
        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, action->mapToScene(action->boundingRect().center()).toPoint());
        QTRY_VERIFY(manager.tabAppearance().showNumbers);
    }

    void menuBarWindowListTracksWindowsCreatedAfterItsBar()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto first = manager.createWindow(); manager.addTab(first, {QStringLiteral("First")});
        QTRY_VERIFY(workspace.window(first));
        const auto retired = manager.createWindow(); manager.addTab(retired, {QStringLiteral("Retired")});
        const auto target = manager.createWindow(); manager.addTab(target, {QStringLiteral("Target")});
        QTRY_VERIFY(workspace.window(target));
        auto* view = workspace.window(first);
        view->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(view));
        auto* bar = namedItem(view->contentItem(), QStringLiteral("MenuStrip")); QVERIFY(bar);
        auto* button = namedItem(bar, QStringLiteral("window")); QVERIFY(button);
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, button->mapToScene(button->boundingRect().center()).toPoint());
        QTRY_VERIFY(popup(view));
        auto* menu = popup(view); QVERIFY(QTest::qWaitForWindowExposed(menu));
        QTRY_VERIFY(namedItem(menu->contentItem(), retired.toString(QUuid::WithoutBraces)));
        const QPointer<QQuickItem> removed(namedItem(menu->contentItem(), retired.toString(QUuid::WithoutBraces)));
        QVERIFY(manager.requestCloseWindow(retired));
        QTRY_VERIFY(!removed);
        QVERIFY(menu->isVisible());
        auto* activate = namedItem(menu->contentItem(), target.toString(QUuid::WithoutBraces)); QVERIFY(activate);
        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, activate->mapToScene(activate->boundingRect().center()).toPoint());
        QTRY_COMPARE(manager.context().window, target);
        QTRY_VERIFY(workspace.window(target)->isActive());
    }

    void menuBarReleasesRemovedRootContributions()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto window = manager.createWindow(); manager.addTab(window, {QStringLiteral("Content")});
        QTRY_VERIFY(workspace.window(window));
        auto* bar = namedItem(workspace.window(window)->contentItem(), QStringLiteral("MenuStrip")); QVERIFY(bar);
        Command command;
        command.id = "test.temporary"; command.title = "Temporary action"; command.menuPath = {"temporary"};
        command.execute = [](const CommandContext&) { return true; };
        QVERIFY(manager.commands().add(std::move(command)));
        QTRY_VERIFY(namedItem(bar, QStringLiteral("extension.temporary")));
        const QPointer<QQuickItem> row(namedItem(bar, QStringLiteral("extension.temporary")));
        const QPointer<QObject> menu(row->property("menu").value<QObject*>()); QVERIFY(menu);
        QVERIFY(manager.commands().remove("test.temporary"));
        QTRY_VERIFY(!row); QTRY_VERIFY(!menu);
        QVERIFY(manager.model().window(window));
    }

    void foldedMenusKeepMouseMnemonicAndOwnerLifetime_data()
    {
        QTest::addColumn<int>("mode");
        QTest::newRow("extension") << 0;
        QTest::newRow("hidden-mnemonic") << 1;
        QTest::newRow("owner-closes") << 2;
        QTest::newRow("arrows") << 3;
    }
    void foldedMenusKeepMouseMnemonicAndOwnerLifetime()
    {
        QFETCH(int, mode);
        WindowManager manager(WindowManager::Presentation::External);
        auto workspace = std::make_unique<QuickWorkspace>(manager);
        workspace->setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        TabId invoked;
        Command command;
        command.id = "test.folded"; command.title = "Folded action"; command.menuPath = {"tools"};
        command.execute = [&](const CommandContext& context) { invoked = context.tab; return true; };
        QVERIFY(manager.commands().add(std::move(command)));
        const auto window = manager.createWindow();
        const auto tab = manager.addTab(window, {QStringLiteral("Content")});
        QTRY_VERIFY(workspace->window(window));
        auto* view = workspace->window(window);
        view->resize(300, 420); view->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(view));
        auto* content = workspace->content(manager.model().tab(tab)->activePane); content->forceActiveFocus();
        auto* bar = namedItem(view->contentItem(), QStringLiteral("MenuStrip")); QVERIFY(bar);
        auto* more = namedItem(bar, QStringLiteral("MenuOverflowButton")); QVERIFY(more);
        auto* tools = namedItem(bar, QStringLiteral("tools")); QVERIFY(tools);
        QTRY_VERIFY(more->isVisible() && !tools->isVisible());
        if (mode == 1 || mode == 3) QTest::keyClick(view, Qt::Key_T, Qt::AltModifier);
        else native_input::mouse(view, more->mapToScene(more->boundingRect().center()).toPoint(), true);
        QTRY_VERIFY(popup(view));
        QPointer<QQuickWindow> menu(popup(view)); QVERIFY(QTest::qWaitForWindowExposed(menu));
        if (mode == 2) { workspace.reset(); QTRY_VERIFY(!menu); return; }
        if (mode == 0) {
            auto* branch = namedItem(menu->contentItem(), QStringLiteral("tools")); QVERIFY(branch);
            native_input::mouse(menu.data(), branch->mapToScene(branch->boundingRect().center()).toPoint());
            QTRY_VERIFY(branch->property("highlighted").toBool());
            QTest::keyClick(menu, Qt::Key_Right);
            QTRY_VERIFY(popup(menu));
            menu = popup(menu); QVERIFY(QTest::qWaitForWindowExposed(menu));
        }
        if (mode == 3) {
            QTest::keyClick(menu, Qt::Key_Left);
            QTRY_VERIFY(popup(view) && namedItem(popup(view)->contentItem(), QStringLiteral("window.select")));
            QTest::keyClick(popup(view), Qt::Key_Right);
            QTRY_VERIFY(popup(view) && namedItem(popup(view)->contentItem(), QStringLiteral("test.folded")));
            menu = popup(view); QVERIFY(QTest::qWaitForWindowExposed(menu));
        }
        auto* action = namedItem(menu->contentItem(), QStringLiteral("test.folded")); QVERIFY(action);
        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, action->mapToScene(action->boundingRect().center()).toPoint());
        QTRY_COMPARE(invoked, tab);
        QTRY_VERIFY(!popup(view));
        QTRY_VERIFY(content->hasActiveFocus());
        view->resize(1200, 700);
        QTRY_VERIFY(!more->isVisible() && tools->isVisible());
        QCOMPARE(namedItem(bar, QStringLiteral("tools")), tools);
        invoked = {};
        QTest::keyClick(view, Qt::Key_T, Qt::AltModifier);
        QTRY_VERIFY(popup(view));
        menu = popup(view); QVERIFY(QTest::qWaitForWindowExposed(menu));
        action = namedItem(menu->contentItem(), QStringLiteral("test.folded")); QVERIFY(action);
        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, action->mapToScene(action->boundingRect().center()).toPoint());
        QTRY_COMPARE(invoked, tab);
    }
};

QTEST_MAIN(QuickMenuBarTest)
#include "quick_menu_bar_test.moc"
