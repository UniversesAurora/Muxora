// SPDX-License-Identifier: MIT
#include "chrome/command_strip.hpp"
#include "chrome/workspace_window.hpp"
#include <window_system/window_manager.hpp>

#include <QApplication>
#include <QJsonArray>
#include <QLabel>
#include <QMouseEvent>
#include <QPointer>
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
    WindowManager manager; const auto owner = manager.createWindow();
    const auto first = floating ? manager.createWindow(WindowKind::Floating, {}, owner) : owner;
    const auto second = manager.createWindow(floating ? WindowKind::Floating : WindowKind::Workbench, {}, owner);
    auto* newer = view(manager, first); auto* older = view(manager, second);
    const auto oldOrder = older->saveStripLayout().value("rows");
    QVERIFY(newer->strips()->moveStrip("caption-controls", 0, 0));
    QVERIFY(newer->strips()->setStripWidth(menuStrip(newer), 232));
    const auto sorted = newer->saveStripLayout();
    QVERIFY(sorted.value("rows") != oldOrder);

    older->strips()->setLocked(false);
    const auto menu = floating ? "floating-menu" : "menu";
    QVERIFY(older->strips()->setStripItemVisible(menu, "view", false));
    const auto updated = profile(manager.saveLayout(), floating);
    QCOMPARE(updated.value("rows"), sorted.value("rows"));
    QCOMPARE(updated.value("locked"), QJsonValue(false));
    const auto settings = updated.value("strips").toObject();
    QCOMPARE(settings.value(menu).toObject().value("width"), QJsonValue(232));
    QVERIFY(settings.value(menu).toObject().value("excluded").toArray().contains(QStringLiteral("view")));
    // The old window can keep its displayed order without publishing it.
    QCOMPARE(older->saveStripLayout().value("rows"), oldOrder);
    WindowManager restarted; QVERIFY(restarted.restoreLayout(manager.saveLayout()));
    const auto fresh = restarted.createWindow(floating ? WindowKind::Floating : WindowKind::Workbench, {}, owner);
    const auto freshLayout = view(restarted, fresh)->saveStripLayout();
    QCOMPARE(freshLayout.value("rows"), sorted.value("rows"));
    QCOMPARE(freshLayout.value("locked"), QJsonValue(false));
    QVERIFY(!view(restarted, fresh)->strips()->isStripItemVisible(menu, "view"));

    // Moving a strip in the old instance explicitly replaces the order
    // dimension, while retaining a newer width it has never adjusted.
    QVERIFY(older->strips()->moveStrip(menu, CommandStripHost::Area::Bottom, 0, 0));
    const auto moved = profile(manager.saveLayout(), floating);
    QCOMPARE(moved.value("rows"), older->saveStripLayout().value("rows"));
    QCOMPARE(moved.value("bottomRows"), older->saveStripLayout().value("bottomRows"));
    QCOMPARE(moved.value("strips").toObject().value(menu).toObject().value("width"), QJsonValue(232));
}

void ToolbarProfileTest::windowKindsRestoreTheirOwnLayoutAndLiveContent()
{
    WindowManager manager;
    const auto root = manager.createWindow(); const auto owner = manager.createWindow();
    QPointer<QWidget> content = new QLabel("same live content");
    const auto tab = manager.addTab(root, content, {"content", "label", {}});
    const auto pane = manager.model().tab(tab)->activePane;
    auto* window = view(manager, root);
    QVERIFY(window->strips()->moveStrip("menu", CommandStripHost::Area::Bottom, 0, 0));
    QVERIFY(window->strips()->setStripItemVisible("menu", "view", false));
    QVERIFY(window->strips()->setStripVisible("workspace", false));
    const auto workbench = window->saveStripLayout();
    QVERIFY(manager.demoteWindow(root, owner));
    auto* strips = window->strips();
    QVERIFY(strips->moveStrip("caption-controls", 0, 1));
    QVERIFY(strips->moveStrip("title", 0, 0));
    QVERIFY(strips->setStripWidth("floating-menu", 214));
    const auto floating = window->saveStripLayout();
    QCOMPARE(strips->stripOrder(0), (QStringList{"title", "floating-menu", "caption-controls"}));
    QVERIFY(!floating.value("strips").toObject().contains("menu"));
    QVERIFY(!floating.value("strips").toObject().contains("workspace"));
    QCOMPARE(profile(manager.saveLayout(), false), workbench);
    QCOMPARE(profile(manager.saveLayout(), true), floating);
    for (int cycle = 0; cycle < 3; ++cycle) {
        QVERIFY(manager.promoteWindow(root)); QCOMPARE(window->saveStripLayout(), workbench);
        QCOMPARE(manager.paneWidget(pane), content.data());
        QVERIFY(manager.demoteWindow(root, owner)); QCOMPARE(window->saveStripLayout(), floating);
        QCOMPARE(manager.paneWidget(pane), content.data());
    }
    const auto snapshot = manager.saveLayout();
    WindowManager restarted;
    restarted.setContentFactory([](const TabOptions& options, PaneId) { return new QLabel(options.title); });
    QVERIFY(restarted.restoreLayout(snapshot));
    QCOMPARE(view(restarted, root)->saveStripLayout(), floating);
    QVERIFY(restarted.promoteWindow(root)); QCOMPARE(view(restarted, root)->saveStripLayout(), workbench);
    QVERIFY(restarted.demoteWindow(root, owner)); QCOMPARE(view(restarted, root)->saveStripLayout(), floating);
    QCOMPARE(profile(restarted.saveLayout(), false), workbench);
    QCOMPARE(profile(restarted.saveLayout(), true), floating);
}

void ToolbarProfileTest::closingEveryFloatingWindowKeepsItsDefault()
{
    WindowManager manager; const auto root = manager.createWindow();
    const auto floating = manager.createWindow(WindowKind::Floating, {}, root);
    auto* window = view(manager, floating);
    QVERIFY(window->strips()->moveStrip("caption-controls", 0, 0));
    QVERIFY(window->strips()->setStripWidth("floating-menu", 221));
    const auto customized = window->saveStripLayout();
    QVERIFY(manager.requestCloseWindow(floating));
    QVERIFY(!manager.model().window(floating));
    const auto snapshot = manager.saveLayout();
    QCOMPARE(profile(snapshot, true), customized);
    WindowManager restarted; QVERIFY(restarted.restoreLayout(snapshot));
    const auto next = restarted.createWindow(WindowKind::Floating, {}, root);
    QCOMPARE(view(restarted, next)->saveStripLayout(), customized);
    QVERIFY(!view(restarted, root)->saveStripLayout().value("strips").toObject().contains("floating-menu"));
}

void ToolbarProfileTest::incompleteSnapshotsDoNotReconstructProfiles()
{
    WindowManager manager; const auto owner = manager.createWindow();
    const auto floating = manager.createWindow(WindowKind::Floating, {}, owner);
    QPointer<QWidget> content = new QLabel("retained");
    const auto tab = manager.addTab(owner, content, {"retained", "label", {}});
    const auto pane = manager.model().tab(tab)->activePane;
    int created = 0;
    manager.setContentFactory([&created](const TabOptions& options, PaneId) { ++created; return new QLabel(options.title); });
    const auto before = manager.saveLayout();
    for (const auto* field : {"toolbarProfiles", "visibilityProfiles"}) {
        auto incomplete = before; incomplete.remove(field);
        QVERIFY(!manager.restoreLayout(incomplete));
        QCOMPARE(created, 0);
        QCOMPARE(manager.saveLayout(), before);
        QCOMPARE(manager.paneWidget(pane), content.data());
    }
    auto mixed = before;
    auto records = mixed.value("windows").toArray();
    for (qsizetype i = 0; i < records.size(); ++i) {
        auto record = records[i].toObject();
        if (record.value("id").toString() != floating.toString(QUuid::WithoutBraces)) continue;
        auto hybrid = view(manager, owner)->saveStripLayout();
        auto rows = hybrid.value("rows").toArray();
        rows[0] = QJsonArray{"caption-controls", "floating-menu", "title", "menu"};
        hybrid.insert("rows", rows);
        auto settings = hybrid.value("strips").toObject();
        settings.insert("floating-menu", view(manager, floating)->saveStripLayout().value("strips").toObject().value("floating-menu"));
        hybrid.insert("strips", settings); record.insert("strips", hybrid);
        records[i] = record;
    }
    mixed.insert("windows", records);
    QVERIFY(!manager.restoreLayout(mixed));
    QCOMPARE(created, 0);
    QCOMPARE(manager.saveLayout(), before);
    QCOMPARE(manager.paneWidget(pane), content.data());
}

void ToolbarProfileTest::malformedProfilesLeaveTheManagerUntouched_data()
{
    QTest::addColumn<QString>("mutation");
    for (const auto* name : {"not-object", "unknown-kind", "duplicate-id", "mixed-menu", "bad-width", "inactive-not-object", "inactive-mixed-menu", "current-mixed-menu", "zero-width", "short-title-width", "short-menu-width", "unlocked-menu-width", "inactive-short-width", "current-short-width",
                            "profile-missing-caption", "current-missing-caption", "inactive-missing-caption", "current-missing-menu", "current-missing-title", "current-missing-workspace", "current-unpositioned-caption"})
        QTest::newRow(name) << QString::fromLatin1(name);
}

void ToolbarProfileTest::malformedProfilesLeaveTheManagerUntouched()
{
    QFETCH(QString, mutation);
    WindowManager manager; const auto root = manager.createWindow();
    QPointer<QWidget> content = new QLabel("not replaced");
    const auto tab = manager.addTab(root, content, {"content", "label", {}});
    const auto pane = manager.model().tab(tab)->activePane;
    auto* window = view(manager, root); QVERIFY(window->strips()->setStripWidth("workspace", 224));
    int created = 0;
    manager.setContentFactory([&created](const TabOptions& options, PaneId) { ++created; return new QLabel(options.title); });
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
        const bool menuWidth = mutation.contains("menu"); const auto id = menuWidth ? "menu" : "title";
        auto value = settings.value(id).toObject();
        value.insert("width", mutation == "zero-width" ? 0 : menuWidth ? 23 : 11);
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
    QCOMPARE(created, 0);
    QCOMPARE(manager.saveLayout(), before);
    QCOMPARE(manager.windowWidget(root), window);
    QCOMPARE(manager.paneWidget(pane), content.data());
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
    WindowManager manager; const auto root = manager.createWindow();
    const auto first = floating ? manager.createWindow(WindowKind::Floating, {}, root) : root;
    const auto second = manager.createWindow(floating ? WindowKind::Floating : WindowKind::Workbench, {}, root);
    auto* newer = view(manager, first); auto* older = view(manager, second);
    const auto menu = floating ? "floating-menu" : "menu";
    QVERIFY(newer->strips()->setStripItemVisible(menu, "file", false));
    QVERIFY(older->strips()->setStripItemVisible(menu, "view", false));
    auto excluded = profile(manager.saveLayout(), floating).value("strips").toObject().value(menu).toObject().value("excluded").toArray();
    QCOMPARE(excluded.size(), 2); QVERIFY(excluded.contains("file")); QVERIFY(excluded.contains("view"));
    QVERIFY(older->strips()->setStripItemVisible(menu, "view", true));
    excluded = profile(manager.saveLayout(), floating).value("strips").toObject().value(menu).toObject().value("excluded").toArray();
    QCOMPARE(excluded, QJsonArray{"file"});
    const auto next = manager.createWindow(floating ? WindowKind::Floating : WindowKind::Workbench, {}, root);
    QVERIFY(!view(manager, next)->strips()->isStripItemVisible(menu, "file"));
    QVERIFY(view(manager, next)->strips()->isStripItemVisible(menu, "view"));
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
