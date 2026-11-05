// SPDX-License-Identifier: MIT
// Toolbar command routing, retained input objects, current-format restore,
// hidden content and native caption hit testing after moving strips.
#include "quick/command_strip_item.hpp"
#include "quick/button_item.hpp"
#include "quick/window_chrome_item.hpp"
#include "native_input.hpp"
#include "quick_menu_test_support.hpp"
#include <window_system/quick_workspace.hpp>
#include <window_system/window_manager.hpp>
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
};

QTEST_MAIN(QuickToolbarTest)
#include "quick_toolbar_test.moc"
