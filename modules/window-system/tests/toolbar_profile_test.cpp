// SPDX-License-Identifier: MIT
#include "chrome/command_strip.hpp"
#include "chrome/workspace_window.hpp"
#include <window_system/window_manager.hpp>
#include <window_system/quick_workspace.hpp>

#include <QApplication>
#include <QJsonArray>
#include <QLabel>
#include <QMouseEvent>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

using namespace window_system;

namespace {
WorkspaceWindow* view(WindowManager& manager, WindowId id)
{
    return static_cast<WorkspaceWindow*>(manager.windowWidget(id));
}

QJsonObject profile(const QJsonObject& snapshot, bool floating)
{
    return snapshot.value("toolbarProfiles").toObject().value(floating ? "floating" : "workbench").toObject();
}

QJsonObject windowRecord(const QJsonObject& snapshot, WindowId id)
{
    for (const auto& value : snapshot.value("windows").toArray()) {
        const auto record = value.toObject();
        if (record.value("id").toString() == id.toString(QUuid::WithoutBraces)) return record;
    }
    return {};
}

bool initializeQuickToolbars(WindowManager& manager, QuickWorkspace& workspace)
{
    QCoreApplication::sendPostedEvents(&workspace, QEvent::MetaCall);
    for (const auto id : manager.model().windowOrder()) {
        // Materialize the same complete default layout used by the old fixture.
        if (!manager.setToolbarsLocked(id, false) || !manager.setToolbarsLocked(id, true)) return false;
    }
    QCoreApplication::sendPostedEvents(&workspace, QEvent::MetaCall);
    return true;
}

QJsonObject quickLayout(WindowManager& manager, QuickWorkspace& workspace, WindowId id)
{
    QCoreApplication::sendPostedEvents(&workspace, QEvent::MetaCall);
    return windowRecord(manager.saveLayout(), id).value("strips").toObject();
}

void kinds()
{
    QTest::addColumn<bool>("floating");
    QTest::newRow("workbench") << false;
    QTest::newRow("floating") << true;
}

void sendMouse(QWidget* target, QEvent::Type type, QPoint global, Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QMouseEvent event(type, target->mapFromGlobal(global), global, button, buttons, Qt::NoModifier);
    QApplication::sendEvent(target, &event);
}
} // namespace

class ToolbarProfileTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); }
    void cleanup() { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }
    void latestEditSurvivesSavingOlderWindows_data() { kinds(); }
    void latestEditSurvivesSavingOlderWindows();
    void unrelatedEditsInOlderWindowsKeepTheNewestOrder_data() { kinds(); }
    void unrelatedEditsInOlderWindowsKeepTheNewestOrder();
    void windowKindsRestoreTheirOwnLayoutAndLiveContent();
    void closingEveryFloatingWindowKeepsItsDefault();
    void incompleteSnapshotsDoNotReconstructProfiles();
    void malformedProfilesLeaveTheManagerUntouched_data();
    void malformedProfilesLeaveTheManagerUntouched();
    void cancellingToolbarDragDoesNotPublish_data() { kinds(); }
    void cancellingToolbarDragDoesNotPublish();
    void exclusionsMergePerButton_data() { kinds(); }
    void exclusionsMergePerButton();
    void synchronousSavingObservesOnlyTheCompletedRestore();
};

// Caption controls have a fixed width; width edits use the window's menu strip.
QString menuStrip(WorkspaceWindow* window)
{
    return window->strips()->stripIds().contains("menu") ? QStringLiteral("menu") : QStringLiteral("floating-menu");
}

void ToolbarProfileTest::latestEditSurvivesSavingOlderWindows()
{
    QFETCH(bool, floating);
    WindowManager manager;
    const auto root = manager.createWindow();
    const auto first = floating ? manager.createWindow(WindowKind::Floating, {}, root) : root;
    const auto second = manager.createWindow(floating ? WindowKind::Floating : WindowKind::Workbench, {}, root);
    const auto opposite = manager.createWindow(floating ? WindowKind::Workbench : WindowKind::Floating, {}, root);
    if (!floating) QVERIFY(!manager.createWindow(WindowKind::Floating, {}, root).isNull());
    QVERIFY(view(manager, opposite)->strips()->setStripWidth(menuStrip(view(manager, opposite)), 211));
    const auto oppositeEdit = view(manager, opposite)->saveStripLayout();
    auto* older = view(manager, first); auto* newer = view(manager, second);
    QVERIFY(older && newer);
    const auto olderOriginal = older->saveStripLayout();
    QVERIFY(older->strips()->moveStrip("caption-controls", 0, 0));
    const auto firstEdit = older->saveStripLayout();
    QCOMPARE(profile(manager.saveLayout(), floating), firstEdit);
    QVERIFY(newer->strips()->setStripWidth(menuStrip(newer), 236));
    QVERIFY(newer->strips()->moveStrip("caption-controls", CommandStripHost::Area::Bottom, 0, 0));
    const auto newestEdit = newer->saveStripLayout();
    QVERIFY(newestEdit != firstEdit);
    QCOMPARE(profile(manager.saveLayout(), floating), newestEdit);

    // Restore/geometry/save callbacks on an older live instance are not edits.
    older->restoreStripLayout(olderOriginal);
    older->resize(820, 540); older->move(90, 70);
    const auto snapshot = manager.saveLayout();
    QCOMPARE(profile(snapshot, floating), newestEdit);
    QCOMPARE(profile(snapshot, !floating), oppositeEdit);
    QCOMPARE(windowRecord(snapshot, first).value("strips").toObject(), olderOriginal);
    QCOMPARE(windowRecord(snapshot, second).value("strips").toObject(), newestEdit);
    const auto third = manager.createWindow(floating ? WindowKind::Floating : WindowKind::Workbench, {}, root);
    QCOMPARE(view(manager, third)->saveStripLayout(), newestEdit);
    QCOMPARE(older->saveStripLayout(), olderOriginal);

    WindowManager restarted;
    QVERIFY(restarted.restoreLayout(snapshot));
    QCOMPARE(view(restarted, first)->saveStripLayout(), olderOriginal);
    QCOMPARE(view(restarted, second)->saveStripLayout(), newestEdit);
    const auto fourth = restarted.createWindow(floating ? WindowKind::Floating : WindowKind::Workbench, {}, root);
    QCOMPARE(view(restarted, fourth)->saveStripLayout(), newestEdit);
    QCOMPARE(profile(restarted.saveLayout(), floating), newestEdit);
    QCOMPARE(profile(restarted.saveLayout(), !floating), oppositeEdit);

    // A later width edit updates that field without restoring the old order.
    QVERIFY(older->strips()->setStripWidth(menuStrip(older), 244));
    const auto latest = profile(manager.saveLayout(), floating);
    QCOMPARE(latest.value("rows"), newestEdit.value("rows"));
    QCOMPARE(latest.value("bottomRows"), newestEdit.value("bottomRows"));
    QCOMPARE(latest.value("strips").toObject().value(menuStrip(older)).toObject().value("width"), QJsonValue(244));
}

void ToolbarProfileTest::unrelatedEditsInOlderWindowsKeepTheNewestOrder()
{
    QFETCH(bool, floating);
    WindowManager manager(WindowManager::Presentation::External);
    QuickWorkspace managerWorkspace(manager);
    const auto owner = manager.createWindow();
    const auto first = floating ? manager.createWindow(WindowKind::Floating, {}, owner) : owner;
    const auto second = manager.createWindow(floating ? WindowKind::Floating : WindowKind::Workbench, {}, owner);
    QVERIFY(initializeQuickToolbars(manager, managerWorkspace));
    const auto oldOrder = quickLayout(manager, managerWorkspace, second).value("rows");
    QVERIFY(manager.moveToolbar(first, "caption-controls", ToolbarArea::Top, 0, 0));
    QVERIFY(manager.setToolbarWidth(first, floating ? "floating-menu" : "menu", 232));
    const auto sorted = quickLayout(manager, managerWorkspace, first);
    QVERIFY(sorted.value("rows") != oldOrder);

    manager.setToolbarsLocked(second, false);
    const auto menu = floating ? "floating-menu" : "menu";
    QVERIFY(manager.setToolbarItemVisible(second, menu, "view", false));
    const auto updated = profile(manager.saveLayout(), floating);
    QCOMPARE(updated.value("rows"), sorted.value("rows"));
    QCOMPARE(updated.value("locked"), QJsonValue(false));
    const auto settings = updated.value("strips").toObject();
    QCOMPARE(settings.value(menu).toObject().value("width"), QJsonValue(232));
    QVERIFY(settings.value(menu).toObject().value("excluded").toArray().contains(QStringLiteral("view")));
    // The old window can keep its displayed order without publishing it.
    QCOMPARE(quickLayout(manager, managerWorkspace, second).value("rows"), oldOrder);
    WindowManager restarted(WindowManager::Presentation::External);
    QuickWorkspace restartedWorkspace(restarted);
    QVERIFY(restarted.restoreLayout(manager.saveLayout()));
    const auto fresh = restarted.createWindow(floating ? WindowKind::Floating : WindowKind::Workbench, {}, owner);
    const auto freshLayout = quickLayout(restarted, restartedWorkspace, fresh);
    QCOMPARE(freshLayout.value("rows"), sorted.value("rows"));
    QCOMPARE(freshLayout.value("locked"), QJsonValue(false));
    QVERIFY(restarted.toolbarState(fresh, menu)->hiddenItems.contains("view"));

    // Moving a strip in the old instance explicitly replaces the order
    // dimension, while retaining a newer width it has never adjusted.
    QVERIFY(manager.moveToolbar(second, menu, ToolbarArea::Bottom, 0, 0));
    const auto moved = profile(manager.saveLayout(), floating);
    QCOMPARE(moved.value("rows"), quickLayout(manager, managerWorkspace, second).value("rows"));
    QCOMPARE(moved.value("bottomRows"), quickLayout(manager, managerWorkspace, second).value("bottomRows"));
    QCOMPARE(moved.value("strips").toObject().value(menu).toObject().value("width"), QJsonValue(232));
}

void ToolbarProfileTest::windowKindsRestoreTheirOwnLayoutAndLiveContent()
{
    WindowManager manager(WindowManager::Presentation::External);
    QuickWorkspace managerWorkspace(manager);
    const auto root = manager.createWindow(); const auto owner = manager.createWindow();
    managerWorkspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
    const auto tab = manager.addTab(root, {"content", "label", {}});
    const auto pane = manager.model().tab(tab)->activePane;
    QVERIFY(initializeQuickToolbars(manager, managerWorkspace));
    QPointer<QQuickItem> content = managerWorkspace.content(pane);
    QVERIFY(manager.moveToolbar(root, "menu", ToolbarArea::Bottom, 0, 0));
    QVERIFY(manager.setToolbarItemVisible(root, "menu", "view", false));
    QVERIFY(manager.setToolbarVisible(root, "workspace", false));
    const auto workbench = quickLayout(manager, managerWorkspace, root);
    QVERIFY(manager.demoteWindow(root, owner));
    QCoreApplication::sendPostedEvents(&managerWorkspace, QEvent::MetaCall);
    QVERIFY(manager.moveToolbar(root, "caption-controls", ToolbarArea::Top, 0, 1));
    QVERIFY(manager.moveToolbar(root, "title", ToolbarArea::Top, 0, 0));
    QVERIFY(manager.setToolbarWidth(root, "floating-menu", 214));
    const auto floating = quickLayout(manager, managerWorkspace, root);
    QCOMPARE(floating.value("rows").toArray().first().toArray(), (QJsonArray{"title", "floating-menu", "caption-controls"}));
    QVERIFY(!floating.value("strips").toObject().contains("menu"));
    QVERIFY(!floating.value("strips").toObject().contains("workspace"));
    QCOMPARE(profile(manager.saveLayout(), false), workbench);
    QCOMPARE(profile(manager.saveLayout(), true), floating);
    for (int cycle = 0; cycle < 3; ++cycle) {
        QVERIFY(manager.promoteWindow(root)); QCOMPARE(quickLayout(manager, managerWorkspace, root), workbench);
        QCOMPARE(managerWorkspace.content(pane), content.data());
        QVERIFY(manager.demoteWindow(root, owner)); QCOMPARE(quickLayout(manager, managerWorkspace, root), floating);
        QCOMPARE(managerWorkspace.content(pane), content.data());
    }
    const auto snapshot = manager.saveLayout();
    WindowManager restarted(WindowManager::Presentation::External);
    QuickWorkspace restartedWorkspace(restarted);
    restartedWorkspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
    QVERIFY(restarted.restoreLayout(snapshot));
    QCOMPARE(quickLayout(restarted, restartedWorkspace, root), floating);
    QVERIFY(restarted.promoteWindow(root)); QCOMPARE(quickLayout(restarted, restartedWorkspace, root), workbench);
    QVERIFY(restarted.demoteWindow(root, owner)); QCOMPARE(quickLayout(restarted, restartedWorkspace, root), floating);
    QCOMPARE(profile(restarted.saveLayout(), false), workbench);
    QCOMPARE(profile(restarted.saveLayout(), true), floating);
}

void ToolbarProfileTest::closingEveryFloatingWindowKeepsItsDefault()
{
    WindowManager manager(WindowManager::Presentation::External);
    QuickWorkspace managerWorkspace(manager);
    const auto root = manager.createWindow();
    const auto floating = manager.createWindow(WindowKind::Floating, {}, root);
    QVERIFY(initializeQuickToolbars(manager, managerWorkspace));
    QVERIFY(manager.moveToolbar(floating, "caption-controls", ToolbarArea::Top, 0, 0));
    QVERIFY(manager.setToolbarWidth(floating, "floating-menu", 221));
    const auto customized = quickLayout(manager, managerWorkspace, floating);
    QVERIFY(manager.requestCloseWindow(floating));
    QVERIFY(!manager.model().window(floating));
    const auto snapshot = manager.saveLayout();
    QCOMPARE(profile(snapshot, true), customized);
    WindowManager restarted(WindowManager::Presentation::External);
    QuickWorkspace restartedWorkspace(restarted);
    QVERIFY(restarted.restoreLayout(snapshot));
    const auto next = restarted.createWindow(WindowKind::Floating, {}, root);
    QCOMPARE(quickLayout(restarted, restartedWorkspace, next), customized);
    QVERIFY(!quickLayout(restarted, restartedWorkspace, root).value("strips").toObject().contains("floating-menu"));
}

void ToolbarProfileTest::incompleteSnapshotsDoNotReconstructProfiles()
{
    WindowManager manager(WindowManager::Presentation::External);
    QuickWorkspace managerWorkspace(manager);
    const auto owner = manager.createWindow();
    const auto floating = manager.createWindow(WindowKind::Floating, {}, owner);
    int created = 0;
    managerWorkspace.setContentFactory([&created](const TabOptions&, PaneId) { ++created; return new QQuickItem; });
    const auto tab = manager.addTab(owner, {"retained", "label", {}});
    const auto pane = manager.model().tab(tab)->activePane;
    QVERIFY(initializeQuickToolbars(manager, managerWorkspace));
    QPointer<QQuickItem> content = managerWorkspace.content(pane);
    created = 0;
    const auto before = manager.saveLayout();
    for (const auto* field : {"toolbarProfiles", "visibilityProfiles"}) {
        auto incomplete = before; incomplete.remove(field);
        QVERIFY(!manager.restoreLayout(incomplete));
        QCoreApplication::sendPostedEvents(&managerWorkspace, QEvent::MetaCall);
        QCOMPARE(created, 0);
        QCOMPARE(manager.saveLayout(), before);
        QCOMPARE(managerWorkspace.content(pane), content.data());
    }
    auto mixed = before;
    auto records = mixed.value("windows").toArray();
    for (qsizetype i = 0; i < records.size(); ++i) {
        auto record = records[i].toObject();
        if (record.value("id").toString() != floating.toString(QUuid::WithoutBraces)) continue;
        auto hybrid = quickLayout(manager, managerWorkspace, owner);
        auto rows = hybrid.value("rows").toArray();
        rows[0] = QJsonArray{"caption-controls", "floating-menu", "title", "menu"};
        hybrid.insert("rows", rows);
        auto settings = hybrid.value("strips").toObject();
        settings.insert("floating-menu", quickLayout(manager, managerWorkspace, floating).value("strips").toObject().value("floating-menu"));
        hybrid.insert("strips", settings); record.insert("strips", hybrid);
        records[i] = record;
    }
    mixed.insert("windows", records);
    QVERIFY(!manager.restoreLayout(mixed));
    QCoreApplication::sendPostedEvents(&managerWorkspace, QEvent::MetaCall);
    QCOMPARE(created, 0);
    QCOMPARE(manager.saveLayout(), before);
    QCOMPARE(managerWorkspace.content(pane), content.data());
}

void ToolbarProfileTest::malformedProfilesLeaveTheManagerUntouched_data()
{
    QTest::addColumn<QString>("mutation");
    for (const auto* name : {"not-object", "unknown-kind", "duplicate-id", "mixed-menu", "bad-width", "inactive-not-object", "inactive-mixed-menu", "current-mixed-menu", "zero-width", "short-title-width", "short-menu-width", "unlocked-menu-width", "inactive-short-width", "current-short-width",
                            "profile-missing-caption", "current-missing-caption", "inactive-missing-caption", "current-missing-menu", "current-missing-title", "current-missing-workspace", "current-unpositioned-caption", "short-custom-width"})
        QTest::newRow(name) << QString::fromLatin1(name);
}

void ToolbarProfileTest::malformedProfilesLeaveTheManagerUntouched()
{
    QFETCH(QString, mutation);
    WindowManager manager(WindowManager::Presentation::External);
    QuickWorkspace managerWorkspace(manager);
    const auto root = manager.createWindow();
    int created = 0;
    managerWorkspace.setContentFactory([&created](const TabOptions&, PaneId) { ++created; return new QQuickItem; });
    const auto tab = manager.addTab(root, {"content", "label", {}});
    const auto pane = manager.model().tab(tab)->activePane;
    QVERIFY(initializeQuickToolbars(manager, managerWorkspace));
    QPointer<QQuickItem> content = managerWorkspace.content(pane);
    auto* window = managerWorkspace.window(root);
    QVERIFY(manager.setToolbarWidth(root, "workspace", 224));
    created = 0;
    const auto before = manager.saveLayout(); auto malformed = before;
    auto profiles = malformed.value("toolbarProfiles").toObject();
    auto layout = profiles.value("workbench").toObject();
    if (mutation.contains("missing-") || mutation == "current-unpositioned-caption") {
        const auto id = mutation.endsWith("caption") ? "caption-controls"
            : mutation.endsWith("menu") ? "menu" : mutation.endsWith("title") ? "title" : "workspace";
        for (const auto* field : {"rows", "bottomRows"}) {
            if (!layout.contains(field)) continue;
            auto rows = layout.value(field).toArray();
            for (qsizetype row = 0; row < rows.size(); ++row) {
                auto entries = rows[row].toArray();
                for (qsizetype index = entries.size(); index-- > 0;)
                    if (entries[index].toString() == id) entries.removeAt(index);
                rows[row] = entries;
            }
            layout.insert(field, rows);
        }
        if (mutation != "current-unpositioned-caption") {
            auto settings = layout.value("strips").toObject(); settings.remove(id); layout.insert("strips", settings);
        }
        if (mutation.startsWith("profile")) {
            profiles.insert("workbench", layout); malformed.insert("toolbarProfiles", profiles);
        } else {
            auto records = malformed.value("windows").toArray(); auto record = records[0].toObject();
            if (mutation.startsWith("inactive")) record.insert("stripLayouts", QJsonObject{{"workbench", layout}});
            else record.insert("strips", layout);
            records[0] = record; malformed.insert("windows", records);
        }
    }
    else if (mutation.endsWith("width") && mutation != "bad-width") {
        auto settings = layout.value("strips").toObject();
        const bool menuWidth = mutation.contains("menu"), customWidth = mutation == "short-custom-width";
        const auto id = customWidth ? "custom.commands" : menuWidth ? "menu" : "title";
        auto value = settings.value(id).toObject();
        value.insert("width", mutation == "zero-width" ? 0 : menuWidth || customWidth ? 23 : 11);
        if (mutation == "unlocked-menu-width") { layout.insert("locked", false); value.insert("width", 35); }
        settings.insert(id, value); layout.insert("strips", settings);
        if (mutation.startsWith("inactive") || mutation.startsWith("current")) {
            auto records = malformed.value("windows").toArray(); auto record = records[0].toObject();
            if (mutation.startsWith("inactive")) record.insert("stripLayouts", QJsonObject{{"workbench", layout}});
            else record.insert("strips", layout);
            records[0] = record; malformed.insert("windows", records);
        } else { profiles.insert("workbench", layout); malformed.insert("toolbarProfiles", profiles); }
    }
    else if (mutation == "not-object") malformed.insert("toolbarProfiles", QJsonArray{});
    else if (mutation == "unknown-kind") { profiles.insert("tool", layout); malformed.insert("toolbarProfiles", profiles); }
    else if (mutation.startsWith("inactive") || mutation == "current-mixed-menu") {
        auto records = malformed.value("windows").toArray(); auto record = records[0].toObject();
        if (mutation == "inactive-not-object") record.insert("stripLayouts", QJsonArray{});
        else if (mutation == "inactive-mixed-menu") record.insert("stripLayouts", QJsonObject{{"floating", layout}});
        else {
            auto settings = layout.value("strips").toObject(); settings.insert("floating-menu", QJsonObject{});
            layout.insert("strips", settings); record.insert("strips", layout);
        }
        records[0] = record; malformed.insert("windows", records);
    } else {
        if (mutation == "duplicate-id") {
            auto rows = layout.value("rows").toArray(); auto row = rows[0].toArray(); row.append("title"); rows[0] = row; layout.insert("rows", rows);
        } else if (mutation == "mixed-menu") {
            auto settings = layout.value("strips").toObject(); settings.insert("floating-menu", QJsonObject{}); layout.insert("strips", settings);
        } else {
            auto settings = layout.value("strips").toObject(); auto value = settings.value("title").toObject();
            value.insert("width", -8); settings.insert("title", value); layout.insert("strips", settings);
        }
        profiles.insert("workbench", layout); malformed.insert("toolbarProfiles", profiles);
    }
    QVERIFY(!manager.restoreLayout(malformed));
    QCoreApplication::sendPostedEvents(&managerWorkspace, QEvent::MetaCall);
    QCOMPARE(created, 0);
    QCOMPARE(manager.saveLayout(), before);
    QCOMPARE(managerWorkspace.window(root), window);
    QCOMPARE(managerWorkspace.content(pane), content.data());
}

void ToolbarProfileTest::cancellingToolbarDragDoesNotPublish()
{
    QFETCH(bool, floating);
    WindowManager manager; const auto owner = manager.createWindow();
    const auto id = floating ? manager.createWindow(WindowKind::Floating, {}, owner) : owner;
    auto* window = view(manager, id); window->strips()->setLocked(false);
    manager.showWindow(id); QVERIFY(QTest::qWaitForWindowExposed(window));
    auto* strips = window->strips(); const auto before = window->saveStripLayout();
    const auto profilesBefore = manager.saveLayout().value("toolbarProfiles");
    QPointer<QWidget> controls = strips->stripWidget("caption-controls");
    auto* handle = controls->findChild<QWidget*>("StripHandle"); QVERIFY(handle);
    const auto start = handle->mapToGlobal(handle->rect().center());
    sendMouse(handle, QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    const auto target = strips->mapToGlobal(QPoint(70, strips->height() - 4));
    sendMouse(handle, QEvent::MouseMove, target, Qt::NoButton, Qt::LeftButton);
    QVERIFY(strips->isDragging());
    QCOMPARE(manager.saveLayout().value("toolbarProfiles"), profilesBefore);
    QTest::keyClick(strips, Qt::Key_Escape);
    QVERIFY(!strips->isDragging()); QCOMPARE(window->saveStripLayout(), before);
    QCOMPARE(manager.saveLayout().value("toolbarProfiles"), profilesBefore);
    QCOMPARE(strips->stripWidget("caption-controls"), controls.data());
}

void ToolbarProfileTest::exclusionsMergePerButton()
{
    QFETCH(bool, floating);
    WindowManager manager(WindowManager::Presentation::External);
    QuickWorkspace managerWorkspace(manager);
    const auto root = manager.createWindow();
    const auto first = floating ? manager.createWindow(WindowKind::Floating, {}, root) : root;
    const auto second = manager.createWindow(floating ? WindowKind::Floating : WindowKind::Workbench, {}, root);
    QVERIFY(initializeQuickToolbars(manager, managerWorkspace));
    const auto menu = floating ? "floating-menu" : "menu";
    QVERIFY(manager.setToolbarItemVisible(first, menu, "file", false));
    QVERIFY(manager.setToolbarItemVisible(second, menu, "view", false));
    auto excluded = profile(manager.saveLayout(), floating).value("strips").toObject().value(menu).toObject().value("excluded").toArray();
    QCOMPARE(excluded.size(), 2); QVERIFY(excluded.contains("file")); QVERIFY(excluded.contains("view"));
    QVERIFY(manager.setToolbarItemVisible(second, menu, "view", true));
    excluded = profile(manager.saveLayout(), floating).value("strips").toObject().value(menu).toObject().value("excluded").toArray();
    QCOMPARE(excluded, QJsonArray{"file"});
    const auto next = manager.createWindow(floating ? WindowKind::Floating : WindowKind::Workbench, {}, root);
    QCoreApplication::sendPostedEvents(&managerWorkspace, QEvent::MetaCall);
    QVERIFY(manager.toolbarState(next, menu)->hiddenItems.contains("file"));
    QVERIFY(!manager.toolbarState(next, menu)->hiddenItems.contains("view"));
}

void ToolbarProfileTest::synchronousSavingObservesOnlyTheCompletedRestore()
{
    WindowManager original; const auto root = original.createWindow(); const auto second = original.createWindow();
    auto* first = view(original, root); auto* other = view(original, second);
    QVERIFY(first->strips()->moveStrip("menu", CommandStripHost::Area::Bottom, 0, 0));
    const auto complete = first->saveStripLayout();
    QVERIFY(original.demoteWindow(root, second));
    QVERIFY(first->strips()->moveStrip("title", 0, 0));
    const auto floating = first->saveStripLayout();
    QVERIFY(other->strips()->moveStrip("caption-controls", 0, 0));
    const auto snapshot = original.saveLayout();
    WindowManager restored; QVector<QJsonObject> saves;
    connect(&restored, &WindowManager::layoutChanged, this, [&] { saves.append(restored.saveLayout()); });
    QVERIFY(restored.restoreLayout(snapshot));
    QCOMPARE(saves.size(), 1);
    QCOMPARE(saves[0].value("toolbarProfiles"), snapshot.value("toolbarProfiles"));
    QCOMPARE(windowRecord(saves[0], root).value("strips").toObject(), floating);
    QCOMPARE(windowRecord(saves[0], root).value("stripLayouts").toObject().value("workbench").toObject(), complete);
    QCOMPARE(windowRecord(saves[0], second).value("strips"), windowRecord(snapshot, second).value("strips"));
}

QTEST_MAIN(ToolbarProfileTest)
#include "toolbar_profile_test.moc"
