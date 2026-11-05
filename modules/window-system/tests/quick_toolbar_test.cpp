// SPDX-License-Identifier: MIT
// Toolbar command routing, retained input objects, current-format restore,
// hidden content and native caption hit testing after moving strips;
// layout edits, saving them, the kind profile, the offered choices, the
// toolbar menus and handle drags; the status bar below them.
#include "quick/command_strip_item.hpp"
#include "quick/command_strip_host_item.hpp"
#include "quick/picture_item.hpp"
#include "chrome/strip_constraints.hpp"
#include "quick/button_item.hpp"
#include "quick/window_chrome_item.hpp"
#include "native_input.hpp"
#include "quick_menu_test_support.hpp"
#include <window_system/quick_workspace.hpp>
#include <window_system/window_manager.hpp>
#include <window_system/ui_scale.hpp>
#include <QContextMenuEvent>
#include <QJsonArray>
#include <QTest>

using namespace window_system;
using namespace quick_menu_test;

namespace {
QJsonObject withStrips(QJsonObject snapshot, WindowId window, const QJsonObject& layout)
{
    auto records = snapshot.value("windows").toArray();
    for (qsizetype index = 0; index < records.size(); ++index) {
        auto record = records.at(index).toObject();
        if (record.value("id").toString() != window.toString(QUuid::WithoutBraces)) continue;
        record.insert("strips", layout); records[index] = record;
    }
    snapshot.insert("windows", records); return snapshot;
}
QJsonObject withBottomToolbar(QJsonObject snapshot, WindowId window, const QString& hidden)
{
    QJsonObject settings;
    for (const auto* id : {"menu", "title", "caption-controls", "workspace", "test.bar"})
        settings.insert(id, QJsonObject{{"width", -1}, {"visible", true}});
    settings.insert("test.bar", QJsonObject{{"width", -1}, {"visible", true}, {"excluded", QJsonArray{hidden}}});
    QJsonArray bottomRows; bottomRows.append(QJsonArray{"test.bar"});
    return withStrips(std::move(snapshot), window, {{"locked", true}, {"strips", settings},
        {"rows", QJsonArray{QJsonArray{"menu", "title", "caption-controls"}, QJsonArray{"workspace"}}},
        {"bottomRows", bottomRows}});
}
quick::CommandStripItem* stripItem(QQuickWindow* view, const QString& id)
{
    for (auto* item : view->contentItem()->findChildren<quick::CommandStripItem*>())
        if (item->property("stripId").toString() == id) return item;
    return nullptr;
}
} // namespace

class QuickToolbarTest final : public QObject {
    Q_OBJECT
private slots:
    void init()
    {
        QTest::failOnWarning(QRegularExpression(".*(Unable to assign|TypeError|ReferenceError|Binding loop).*"));
    }
    void commandToolbarsRetainContextStateAndLifetime_data()
    {
        QTest::addColumn<int>("mode");
        QTest::newRow("button") << 0;
        QTest::newRow("overflow") << 1;
        QTest::newRow("owner-closes") << 2;
        QTest::newRow("keyboard") << 3;
    }
    void commandToolbarsRetainContextStateAndLifetime()
    {
        QFETCH(int, mode);
        WindowManager manager(WindowManager::Presentation::External);
        auto workspace = std::make_unique<QuickWorkspace>(manager);
        workspace->setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        bool enabled = false;
        CommandContext invoked;
        QStringList commands;
        const int count = mode == 1 ? 22 : 2;
        for (int index = 0; index < count; ++index) {
            Command command;
            command.id = index == count - 1 ? QStringLiteral("test.toolbarTarget") : QStringLiteral("test.toolbar%1").arg(index);
            command.title = command.id;
            command.enabled = [&](const CommandContext&) { return enabled; };
            command.execute = [&](const CommandContext& context) { invoked = context; if (mode == 2) workspace.reset(); return true; };
            commands.append(command.id);
            QVERIFY(manager.commands().add(std::move(command)));
        }
        QVERIFY(manager.registerToolbar({"test.bar", "Commands", commands}));
        const auto first = manager.createWindow();
        const auto tab = manager.addTab(first, {QStringLiteral("First")});
        const auto second = manager.createWindow(); manager.addTab(second, {QStringLiteral("Second")});
        QTRY_VERIFY(workspace->window(first));
        const QPointer<QQuickWindow> view(workspace->window(first));
        view->resize(mode == 1 ? 320 : 1000, 500);
        view->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(view));
        QPointer<quick::CommandStripItem> strip;
        for (auto* item : view->contentItem()->findChildren<quick::CommandStripItem*>())
            if (item->property("stripId").toString() == "test.bar") strip = item;
        QVERIFY(strip);
        QPointer<quick::ButtonItem> target, keep;
        for (auto* item : strip->findChildren<quick::ButtonItem*>()) {
            if (item->property("commandId").toString() == "test.toolbarTarget") target = item;
            if (item->property("commandId").toString() == commands.first()) keep = item;
        }
        QVERIFY(target); QVERIFY(keep); QVERIFY(!target->isEnabled());
        enabled = true;
        QVERIFY(manager.commands().setShortcut("test.toolbarTarget", QKeySequence("Ctrl+Alt+T")));
        QTRY_VERIFY(target->isEnabled());
        if (mode == 3) {
            // Restore an ordinary current-format layout, with a hidden button
            // and the contributed strip moved from its default top row.
            const auto snapshot = withBottomToolbar(manager.saveLayout(), first, commands.first());
            const auto restored = manager.restoreLayout(snapshot);
            QVERIFY2(restored, qPrintable(restored.error));
            QTRY_VERIFY(!keep->isVisible());
            QVERIFY(manager.setZenMode(first, true));
            QTRY_VERIFY(!target->isVisible());
            QVERIFY(manager.setZenMode(first, false));
            QTRY_VERIFY(target->isVisible());
        }
        QPointer<QQuickWindow> menu;
        if (mode == 1) {
            auto* more = strip->findChild<quick::ButtonItem*>("StripOverflowButton"); QVERIFY(more);
            native_input::mouse(view.data(), more->mapToScene(more->boundingRect().center()).toPoint(), true);
            QTRY_VERIFY(popup(view)); menu = popup(view); QVERIFY(QTest::qWaitForWindowExposed(menu));
            auto* action = namedItem(menu->contentItem(), QStringLiteral("test.toolbarTarget")); QVERIFY(action);
            QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, action->mapToScene(action->boundingRect().center()).toPoint());
        } else if (mode == 3) {
            target->forceActiveFocus(Qt::TabFocusReason);
            QTest::keyPress(view, Qt::Key_Space);
            QVERIFY(invoked.window.isNull());
            QTest::keyRelease(view, Qt::Key_Space);
        } else {
            QVERIFY(target->isVisible());
            QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, target->mapToScene(target->boundingRect().center()).toPoint());
        }
        QTRY_COMPARE(invoked.window, first); QCOMPARE(invoked.tab, tab);
        if (mode == 2) { QTRY_VERIFY(!view); return; }
        QTRY_VERIFY(!menu);
        QVERIFY(manager.updateToolbar({"test.bar", "Commands", {commands.first()}}));
        QTRY_VERIFY(!target); QVERIFY(keep);
        QVERIFY(manager.removeToolbar("test.bar"));
        QTRY_VERIFY(!strip); QTRY_VERIFY(!keep);
    }
    void restoredChromeKeepsCaptionInputAndSkipsExcludedMenus_data()
    {
        QTest::addColumn<bool>("bottom");
        QTest::newRow("top") << false;
        QTest::newRow("bottom") << true;
    }
    void restoredChromeKeepsCaptionInputAndSkipsExcludedMenus()
    {
        QFETCH(bool, bottom);
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        bool invoked = false;
        Command command;
        command.id = "test.help"; command.title = "Help action"; command.menuPath = {"help"};
        command.execute = [&](const CommandContext&) { invoked = true; return true; };
        QVERIFY(manager.commands().add(std::move(command)));
        const auto window = manager.createWindow(); manager.addTab(window, {QStringLiteral("Content")});
        QTRY_VERIFY(workspace.window(window));
        const QPointer<QQuickWindow> view(workspace.window(window));
        view->resize(1000, 650);
        auto* chrome = view->contentItem()->findChild<quick::WindowChromeItem*>(); QVERIFY(chrome);
        QJsonObject settings;
        for (const auto* id : {"menu", "title", "caption-controls", "workspace"})
            settings.insert(id, QJsonObject{{"width", -1}, {"visible", true}});
        settings.insert("title", QJsonObject{{"width", -1}, {"contentVisible", false}});
        settings.insert("menu", QJsonObject{{"width", -1}, {"contentVisible", false}, {"excluded", QJsonArray{"tools"}}});
        QJsonArray first{"menu", "title"}, rows, bottomRows;
        if (bottom) bottomRows.append(QJsonArray{"caption-controls"});
        else first.append("caption-controls");
        rows.append(first); rows.append(QJsonArray{"workspace"});
        const auto restored = manager.restoreLayout(withStrips(manager.saveLayout(), window,
            {{"locked", true}, {"rows", rows}, {"bottomRows", bottomRows}, {"strips", settings}}));
        QVERIFY2(restored, qPrintable(restored.error));
        QTRY_VERIFY(!chrome->isVisible());
        QVERIFY(chrome->captionContent()->isVisible());
        view->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(view));
        auto* tools = namedItem(view->contentItem(), QStringLiteral("tools")); QVERIFY(tools);
        QVERIFY(!tools->isVisible()); QVERIFY(!tools->isEnabled());
        QTest::keyClick(view, Qt::Key_W, Qt::AltModifier);
        QTRY_VERIFY(popup(view));
        QTest::keyClick(popup(view), Qt::Key_Right);
        QTRY_VERIFY(popup(view) && namedItem(popup(view)->contentItem(), QStringLiteral("test.help")));
        QTest::keyClick(popup(view), Qt::Key_Home); QTest::keyClick(popup(view), Qt::Key_Return);
        QTRY_VERIFY(invoked); QTRY_VERIFY(!popup(view));
        quick::ButtonItem* maximize = nullptr;
        for (auto* button : chrome->captionContent()->findChildren<quick::ButtonItem*>())
            if (button->property("role").toString() == "maximize") maximize = button;
        QVERIFY(maximize);
        const auto point = maximize->mapToScene(maximize->boundingRect().center());
        QCOMPARE(chrome->captionButtonAt(point), maximize);
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, point.toPoint());
        QTRY_VERIFY(view->windowStates() & Qt::WindowMaximized);
        QVERIFY(manager.requestCloseWindow(window));
        QTRY_VERIFY(!view);
    }
    // Edits made through the manager, as a script or a command makes them:
    // the window shows them, saving keeps them and restoring brings them back.
    void editsShowInTheWindowAndSurviveSaving_data()
    {
        QTest::addColumn<QString>("edit");
        for (const auto* edit : {"hidden", "width", "elastic", "button", "bottom", "unlocked"}) QTest::newRow(edit) << QString(edit);
    }
    void editsShowInTheWindowAndSurviveSaving()
    {
        QFETCH(QString, edit);
        QJsonObject saved;
        for (const bool restoring : {false, true}) {
            WindowManager manager(WindowManager::Presentation::External);
            QuickWorkspace workspace(manager);
            workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
            for (const auto* id : {"test.a", "test.b"}) {
                Command command;
                command.id = id; command.title = id;
                command.execute = [](const CommandContext&) { return true; };
                QVERIFY(manager.commands().add(std::move(command)));
            }
            QVERIFY(manager.registerToolbar({"test.bar", "Commands", {"test.a", "test.b"}}));
            WindowId window;
            if (restoring) {
                const auto restored = manager.restoreLayout(saved);
                QVERIFY2(restored, qPrintable(restored.error));
                window = manager.model().windowOrder().constFirst();
            } else {
                window = manager.createWindow();
                manager.addTab(window, {QStringLiteral("Content")});
            }
            QTRY_VERIFY(workspace.window(window));
            auto* view = workspace.window(window);
            view->resize(1200, 600);
            QVERIFY(QTest::qWaitForWindowExposed(view));
            QPointer<quick::CommandStripItem> strip;
            QTRY_VERIFY((strip = stripItem(view, "test.bar")));
            if (!restoring) {
                if (edit == "hidden") QVERIFY(manager.setToolbarVisible(window, "test.bar", false));
                if (edit == "width") QVERIFY(manager.setToolbarWidth(window, "test.bar", 240));
                if (edit == "elastic") QVERIFY(manager.setToolbarElastic(window, "test.bar", true));
                if (edit == "button") QVERIFY(manager.setToolbarItemVisible(window, "test.bar", "test.b", false));
                if (edit == "bottom") QVERIFY(manager.moveToolbar(window, "test.bar", ToolbarArea::Bottom, 0, -1));
                if (edit == "unlocked") QVERIFY(manager.execute("window.toggleStripsLock", {}, window));
                saved = manager.saveLayout();
            }
            const auto state = manager.toolbarState(window, "test.bar");
            QVERIFY(state);
            QCOMPARE(state->visible, edit != "hidden");
            QCOMPARE(state->width, edit == "width" ? 240 : -1);
            QCOMPARE(state->elastic, edit == "elastic");
            QCOMPARE(state->hiddenItems, edit == "button" ? QStringList{"test.b"} : QStringList{});
            QCOMPARE(state->area, edit == "bottom" ? ToolbarArea::Bottom : ToolbarArea::Top);
            QCOMPARE(manager.toolbarsLocked(window), edit != "unlocked");
            QCOMPARE(state->locked, edit != "unlocked");
            // What the window shows.
            const auto* host = view->contentItem()->findChild<quick::CommandStripHostItem*>();
            QVERIFY(host);
            QTRY_COMPARE(strip->isVisible(), edit != "hidden");
            if (edit == "width") QTRY_COMPARE(qRound(strip->width()), uiPixels(240));
            if (edit == "elastic") QTRY_COMPARE(qRound(strip->x() + strip->width()), qRound(host->width()));
            if (edit == "bottom") QTRY_COMPARE(strip->parentItem(), host->bottomArea());
            if (edit == "button") {
                quick::ButtonItem* hidden = nullptr;
                for (auto* button : strip->findChildren<quick::ButtonItem*>())
                    if (button->property("commandId").toString() == "test.b") hidden = button;
                QVERIFY(hidden);
                QTRY_VERIFY(!hidden->isVisible());
            }
        }
    }

    // As the Widgets windows do, an edit becomes the window kind's profile,
    // which a window created afterwards starts from.
    void aNewWindowStartsFromTheEditedLayout()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto first = manager.createWindow();
        manager.addTab(first, {QStringLiteral("First")});
        QTRY_VERIFY(workspace.window(first));
        QVERIFY(manager.moveToolbar(first, "workspace", ToolbarArea::Bottom, 0, -1));
        const auto second = manager.createWindow();
        manager.addTab(second, {QStringLiteral("Second")});
        QTRY_VERIFY(workspace.window(second));
        QTRY_VERIFY(manager.toolbarState(second, "workspace"));
        QCOMPARE(manager.toolbarState(second, "workspace")->area, ToolbarArea::Bottom);
        // Later edits in one window leave the other window's own layout.
        QVERIFY(manager.setToolbarVisible(first, "workspace", false));
        QVERIFY(manager.toolbarState(second, "workspace")->visible);
    }

    // Run without arguments, a toolbar command offers its choices in a Quick
    // menu; each choice runs the command again with its arguments.
    void toolbarCommandsOfferTheirChoicesInQuickMenus()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto window = manager.createWindow();
        manager.addTab(window, {QStringLiteral("Content")});
        QTRY_VERIFY(workspace.window(window));
        const QPointer<QQuickWindow> view(workspace.window(window));
        view->resize(1000, 600);
        view->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(view));
        const auto choose = [&](QQuickWindow* menu, const QString& id) {
            QVERIFY(QTest::qWaitForWindowExposed(menu));
            QQuickItem* row = nullptr;
            QTRY_VERIFY((row = namedItem(menu->contentItem(), id)));
            QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, row->mapToScene(row->boundingRect().center()).toPoint());
        };
        QVERIFY(manager.execute("window.toolbar.move", {}, window));
        QTRY_VERIFY(popup(view));
        choose(popup(view), QStringLiteral("Workspace"));
        // The same command again, now with the strip: where to.
        QTRY_VERIFY(popup(view) && namedItem(popup(view)->contentItem(), QStringLiteral("Bottom")));
        auto* destinations = popup(view);
        choose(destinations, QStringLiteral("Bottom"));
        QTRY_VERIFY(popup(destinations));
        choose(popup(destinations), QStringLiteral("Bottom/New row"));
        QTRY_COMPARE(manager.toolbarState(window, "workspace")->area, ToolbarArea::Bottom);
        QTRY_VERIFY(!popup(view));
    }
    // The toolbar area's menu and a strip's options menu, as in Widgets:
    // each choice changes the layout through the toolbar commands.
    void toolbarMenusChangeTheLayout()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto window = manager.createWindow();
        manager.addTab(window, {QStringLiteral("Content")});
        QTRY_VERIFY(workspace.window(window));
        const QPointer<QQuickWindow> view(workspace.window(window));
        view->resize(1000, 600);
        view->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(view));
        QPointer<quick::CommandStripItem> strip;
        QTRY_VERIFY((strip = stripItem(view, "workspace")));
        const auto choose = [&](QQuickWindow* menu, const QString& id) {
            QVERIFY(QTest::qWaitForWindowExposed(menu));
            QQuickItem* row = nullptr;
            QTRY_VERIFY((row = namedItem(menu->contentItem(), id)));
            QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, row->mapToScene(row->boundingRect().center()).toPoint());
        };
        const auto openAreaMenu = [&] {
            // Past the end of the workspace strip: the toolbar area itself.
            const auto point = strip->mapToScene(QPointF(strip->width() + 8, strip->height() / 2)).toPoint();
            QContextMenuEvent event(QContextMenuEvent::Mouse, point, view->mapToGlobal(point));
            QCoreApplication::sendEvent(view, &event);
        };
        // Unlocking from the area menu shows each strip's options button.
        openAreaMenu();
        QTRY_VERIFY(popup(view));
        choose(popup(view), QStringLiteral("LockStripsAction"));
        QTRY_VERIFY(!manager.toolbarsLocked(window));
        QTRY_VERIFY(!popup(view));
        auto* more = strip->findChild<quick::ButtonItem*>("StripOverflowButton");
        QVERIFY(more);
        // The unlocked strip gains its handle; click once its tail settles.
        QTRY_VERIFY(more->isVisible() && qRound(more->x() + more->width()) == qRound(strip->width()));
        native_input::mouse(view.data(), more->mapToScene(more->boundingRect().center()).toPoint(), true);
        QTRY_VERIFY(popup(view));
        auto* options = popup(view);
        QVERIFY(QTest::qWaitForWindowExposed(options));
        QVERIFY(namedItem(options->contentItem(), QStringLiteral("StripResetWidthAction")));
        choose(options, QStringLiteral("StripCustomizeMenu"));
        QTRY_VERIFY(popup(options));
        choose(popup(options), QStringLiteral("window.settings"));
        QTRY_COMPARE(manager.toolbarState(window, "workspace")->hiddenItems, QStringList{"window.settings"});
        QTRY_VERIFY(!popup(view));
        openAreaMenu();
        QTRY_VERIFY(popup(view));
        choose(popup(view), QStringLiteral("StripVisibility/workspace"));
        QTRY_VERIFY(!manager.toolbarState(window, "workspace")->visible);
        QTRY_VERIFY(!strip->isVisible());
    }
    // Dragging an unlocked strip's handle, as in Widgets: a drop on the band
    // past the last bottom row makes a new row, a drop before a neighbour
    // reorders the row, and Esc puts everything back.
    void handleDragsMoveStrips_data()
    {
        QTest::addColumn<QString>("drop");
        for (const auto* drop : {"bottom", "reorder", "cancel"}) QTest::newRow(drop) << QString(drop);
    }
    void handleDragsMoveStrips()
    {
        QFETCH(QString, drop);
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        QVERIFY(manager.registerToolbar({"test.bar", "Commands", {"window.settings"}}));
        const auto window = manager.createWindow();
        manager.addTab(window, {QStringLiteral("Content")});
        QTRY_VERIFY(workspace.window(window));
        const QPointer<QQuickWindow> view(workspace.window(window));
        view->resize(1000, 600);
        view->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(view));
        QVERIFY(manager.setToolbarsLocked(window, false));
        QPointer<quick::CommandStripItem> strip, neighbour;
        QTRY_VERIFY((strip = stripItem(view, "test.bar")) && (neighbour = stripItem(view, "workspace")));
        QTRY_COMPARE(manager.toolbarState(window, "test.bar")->index, 1);
        const auto* host = view->contentItem()->findChild<quick::CommandStripHostItem*>();
        QVERIFY(host);
        // Positions are taken once the layout follows the window's size.
        QTRY_COMPARE(qRound(host->width()), view->width());
        const auto handle = strip->mapToScene(QPointF(uiPixels(stripHandleWidth) / 2.0, strip->height() / 2)).toPoint();
        QPoint target;
        if (drop == "reorder") target = neighbour->mapToScene(QPointF(4, neighbour->height() / 2)).toPoint();
        else target = host->bottomArea()->mapToScene(QPointF(200, -uiPixels(compactRowHeight) / 2.0)).toPoint();
        const auto savedStrips = [&] {
            return manager.saveLayout().value("windows").toArray().first().toObject().value("strips").toObject();
        };
        const auto before = savedStrips();
        QTest::mousePress(view, Qt::LeftButton, Qt::NoModifier, handle);
        QTest::mouseMove(view, handle + QPoint(12, 0));
        QTest::mouseMove(view, (handle + target) / 2);
        QTest::mouseMove(view, target);
        if (drop == "cancel") {
            const auto preview = [&] {
                for (auto* item : view->contentItem()->childItems())
                    if (qobject_cast<quick::PictureItem*>(item)) return item;
                return static_cast<QQuickItem*>(nullptr);
            };
            QTRY_VERIFY(preview());
            QTest::keyClick(view, Qt::Key_Escape);
            QTRY_VERIFY(!preview());
        }
        QTest::mouseRelease(view, Qt::LeftButton, Qt::NoModifier, target);
        const auto state = manager.toolbarState(window, "test.bar");
        QVERIFY(state);
        QCOMPARE(state->area, drop == "bottom" ? ToolbarArea::Bottom : ToolbarArea::Top);
        if (drop == "bottom") QTRY_COMPARE(strip->parentItem(), host->bottomArea());
        if (drop == "reorder") QCOMPARE(state->index, 0);
        if (drop == "cancel") {
            QCOMPARE(state->index, 1);
            QCOMPARE(manager.toolbarState(window, "workspace")->index, 0);
        }
        // The drop is saved with the layout; a cancelled drag saves nothing.
        QCOMPARE(savedStrips() == before, drop == "cancel");
    }
    // The workbench status bar, as the Widgets one: the message, the
    // workspace information, a host item whose click runs its command, and
    // its menu hiding an entry. A floating window has none.
    void statusBarShowsMessagesItemsAndItsMenu()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        int runs = 0;
        Command command;
        command.id = "test.status"; command.title = "Status command";
        command.execute = [&](const CommandContext&) { ++runs; return true; };
        QVERIFY(manager.commands().add(std::move(command)));
        QVERIFY(manager.registerStatusItem({"test.item", "Item text", "Item tip", "test.status"}));
        const auto window = manager.createWindow();
        manager.addTab(window, {QStringLiteral("Status tab")});
        const auto floating = manager.createWindow(WindowKind::Floating);
        manager.addTab(floating, {QStringLiteral("Floating tab")});
        QTRY_VERIFY(workspace.window(window) && workspace.window(floating));
        const QPointer<QQuickWindow> view(workspace.window(window));
        view->resize(900, 500);
        view->requestActivate(); QVERIFY(QTest::qWaitForWindowActive(view));
        const auto entry = [&](const QString& name) { return view->contentItem()->findChild<QQuickItem*>(name); };
        auto* bar = entry(QStringLiteral("StatusBar"));
        QVERIFY(bar);
        QTRY_VERIFY(bar->isVisible() && bar->height() > 0);
        QTRY_COMPARE(entry(QStringLiteral("StatusWorkspace"))->property("text").toString().contains("Status tab"), true);
        manager.setStatus(window, QStringLiteral("Working"));
        QTRY_COMPARE(entry(QStringLiteral("StatusMessage"))->property("text").toString(), QStringLiteral("Working"));
        auto* item = entry(QStringLiteral("CustomStatusCommandItem"));
        QVERIFY(item);
        QCOMPARE(item->property("text").toString(), QStringLiteral("Item text"));
        QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, item->mapToScene(item->boundingRect().center()).toPoint());
        QTRY_COMPARE(runs, 1);
        // Hiding the message from the bar's menu.
        const auto point = bar->mapToScene(QPointF(4, bar->height() / 2)).toPoint();
        QContextMenuEvent event(QContextMenuEvent::Mouse, point, view->mapToGlobal(point));
        QCoreApplication::sendEvent(view, &event);
        QTRY_VERIFY(popup(view));
        auto* menu = popup(view);
        QVERIFY(QTest::qWaitForWindowExposed(menu));
        QQuickItem* toggle = nullptr;
        QTRY_VERIFY((toggle = namedItem(menu->contentItem(), QStringLiteral("StatusItemToggle/message"))));
        QTest::mouseClick(menu, Qt::LeftButton, Qt::NoModifier, toggle->mapToScene(toggle->boundingRect().center()).toPoint());
        QTRY_VERIFY(!manager.statusItemVisible(window, "message"));
        QTRY_VERIFY(!entry(QStringLiteral("StatusMessage"))->isVisible());
        auto* floatingBar = workspace.window(floating)->contentItem()->findChild<QQuickItem*>(QStringLiteral("StatusBar"));
        QVERIFY(floatingBar);
        QVERIFY(!floatingBar->isVisible());
    }
};

QTEST_MAIN(QuickToolbarTest)
#include "quick_toolbar_test.moc"
