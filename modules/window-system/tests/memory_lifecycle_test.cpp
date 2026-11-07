// SPDX-License-Identifier: MIT
#include <window_system/window_manager.hpp>
#include <window_system/quick_workspace.hpp>

#include <QApplication>
#include <QAction>
#include <QCoreApplication>
#include <QEvent>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QTest>

#include <memory>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <psapi.h>
#endif

using namespace window_system;

namespace {

void settle() {
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

struct ObjectSnapshot {
    qsizetype objects = 0;
    qsizetype widgets = 0;
    QHash<QString, qsizetype> classes;
};

ObjectSnapshot ownedObjects(WindowManager &manager, QuickWorkspace *workspace = nullptr) {
    QSet<QObject *> visited;
    const auto visit = [&visited](auto &&recurse, QObject *object) -> void {
        if (!object || visited.contains(object))
            return;
        visited.insert(object);
        for (auto *child : object->children())
            recurse(recurse, child);
    };
    visit(visit, &manager);
    visit(visit, workspace);
    for (const auto id : manager.model().windowOrder())
        visit(visit, workspace ? static_cast<QObject *>(workspace->window(id)) : manager.windowWidget(id));
    // Parked pane widgets live in the manager's separate hidden QWidget root.
    // Qt forbids giving that root the non-widget WindowManager as its parent.
    if (!workspace)
        for (auto *widget : QApplication::topLevelWidgets())
            if (widget->objectName() == QStringLiteral("WindowSystemStorage"))
                visit(visit, widget);
    ObjectSnapshot result;
    result.objects = visited.size();
    for (auto *object : visited) {
        if (qobject_cast<QWidget *>(object))
            ++result.widgets;
        ++result.classes[QString::fromLatin1(object->metaObject()->className())];
    }
    return result;
}

QJsonObject nativeResources() {
    QJsonObject result;
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    const bool readMemory = GetProcessMemoryInfo(
        GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&memory), sizeof(memory));
    DWORD handles = 0;
    const bool readHandles = GetProcessHandleCount(GetCurrentProcess(), &handles);
    result.insert(QStringLiteral("nativeCountersAvailable"), readMemory && readHandles);
    if (readMemory) {
        result.insert(QStringLiteral("privateBytes"), qint64(memory.PrivateUsage));
        result.insert(QStringLiteral("workingSetBytes"), qint64(memory.WorkingSetSize));
    }
    if (readHandles)
        result.insert(QStringLiteral("handles"), qint64(handles));
    result.insert(QStringLiteral("gdiObjects"), qint64(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS)));
    result.insert(QStringLiteral("userObjects"),
                  qint64(GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS)));
#else
    result.insert(QStringLiteral("nativeCountersAvailable"), false);
#endif
    return result;
}

QJsonObject recordSample(WindowManager &manager, const char *scenario, int iteration,
                         QuickWorkspace *workspace = nullptr) {
    auto result = nativeResources();
    const auto objects = ownedObjects(manager, workspace);
    result.insert(QStringLiteral("scenario"), QString::fromLatin1(scenario));
    result.insert(QStringLiteral("iteration"), iteration);
    result.insert(QStringLiteral("ownedObjects"), objects.objects);
    result.insert(QStringLiteral("ownedWidgets"), objects.widgets);
    result.insert(QStringLiteral("windows"), manager.model().windows().size());
    result.insert(QStringLiteral("tabs"), manager.model().tabs().size());
    qInfo().noquote() << "MEMORY_LIFECYCLE" << QJsonDocument(result).toJson(QJsonDocument::Compact);
    return result;
}

QJsonObject freshLayout() {
    WindowManager source(WindowManager::Presentation::External);
    QuickWorkspace workspace(source);
    const auto window = source.createWindow();
    source.addTab(window, {QStringLiteral("Replacement"), QStringLiteral("test.label"), {}});
    QCoreApplication::sendPostedEvents(&workspace, QEvent::MetaCall);
    return source.saveLayout();
}

} // namespace

class MemoryLifecycleTest final : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); }
    void cleanup() { settle(); }
    void closedLayoutsReleaseTheirPaneTrees();
    void removedCommandsReleaseDynamicMenus();
    void commandMayUnloadWhileItsMenuIsBeingCreated();
};

void MemoryLifecycleTest::closedLayoutsReleaseTheirPaneTrees() {
    WindowManager manager(WindowManager::Presentation::External);
    QuickWorkspace workspace(manager);
    workspace.setContentFactory([](const TabOptions &, PaneId) { return new QQuickItem; });
    QVERIFY(manager.restoreLayout(freshLayout()));
    settle();
    for (int iteration = 0; iteration < 12; ++iteration) {
        const auto oldTab = manager.model().tabs().cbegin().key();
        std::weak_ptr<const LayoutNode> oldTree = manager.model().tab(oldTab)->panes;
        QTRY_VERIFY(workspace.content(manager.model().tab(oldTab)->activePane));
        QPointer<QQuickItem> oldContent = workspace.content(manager.model().tab(oldTab)->activePane);
        QVERIFY(!oldTree.expired());
        QVERIFY(manager.restoreLayout(freshLayout()));
        settle();
        QVERIFY(oldContent.isNull());
        // A snapshot contains values, never ownership of the prior live tree.
        // A write-only rendering cache used to retain every restored Tab ID.
        QVERIFY(oldTree.expired());
        QCOMPARE(manager.model().windows().size(), 1);
        QCOMPARE(manager.model().tabs().size(), 1);
    }
    recordSample(manager, "replace-layout", 12, &workspace);
}

void MemoryLifecycleTest::removedCommandsReleaseDynamicMenus() {
    WindowManager manager;
    const auto root = manager.createWindow();
    const auto floating = manager.createWindow(WindowKind::Floating, {}, root);
    const QStringList sharedPath{QStringLiteral("tools"), QStringLiteral("Shared runtime")};
    QVERIFY(manager.commands().add({QStringLiteral("test.retainedContribution"),
                                    QStringLiteral("Keep shared menu"),
                                    {},
                                    {},
                                    {},
                                    [](const CommandContext &) { return true; },
                                    sharedPath}));
    settle();
    QList<QPointer<QMenuBar>> bars;
    QList<QPointer<QMenu>> sharedMenus;
    QList<QPointer<QMenu>> originalMenus;
    for (const auto id : {root, floating}) {
        auto *bar = manager.windowWidget(id)->findChild<QMenuBar *>(QStringLiteral("MenuStrip"));
        QVERIFY(bar);
        bars.append(bar);
        QMenu *shared = nullptr;
        for (auto *menu : bar->findChildren<QMenu *>()) {
            originalMenus.append(menu);
            if (menu->property("commandMenuPath").toStringList() == sharedPath)
                shared = menu;
        }
        QVERIFY(shared);
        sharedMenus.append(shared);
    }
    const auto baseline = ownedObjects(manager);
    for (int iteration = 0; iteration < 100; ++iteration) {
        const auto id = QStringLiteral("test.temporaryContribution.%1").arg(iteration);
        QStringList path = sharedPath;
        if (iteration % 3 == 0)
            path = {QStringLiteral("Temporary %1").arg(iteration), QStringLiteral("Nested"),
                    QStringLiteral("Commands")};
        else if (iteration % 3 == 1)
            path = {QStringLiteral("tools"), QStringLiteral("Temporary %1").arg(iteration)};
        QVERIFY(manager.commands().add({id,
                                        QStringLiteral("Temporary command"),
                                        {},
                                        {},
                                        {},
                                        [](const CommandContext &) { return true; },
                                        path}));
        settle();
        QList<QPointer<QAction>> temporaryActions;
        QList<QPointer<QMenu>> temporaryMenus;
        for (const auto &bar : bars) {
            const auto actions = bar->findChildren<QAction *>(id);
            QCOMPARE(actions.size(), 1);
            temporaryActions.append(actions.first());
            for (auto *menu : bar->findChildren<QMenu *>())
                if (!originalMenus.contains(menu))
                    temporaryMenus.append(menu);
        }
        QVERIFY(manager.commands().remove(id));
        settle();
        for (const auto &action : temporaryActions)
            QVERIFY(action.isNull());
        for (const auto &menu : temporaryMenus)
            QVERIFY(menu.isNull());
        for (const auto &menu : originalMenus)
            QVERIFY(menu);
        for (const auto &menu : sharedMenus) {
            QVERIFY(menu);
            QCOMPARE(menu->findChildren<QAction *>(QStringLiteral("test.retainedContribution")).size(), 1);
        }
        const auto current = ownedObjects(manager);
        QCOMPARE(current.objects, baseline.objects);
        QCOMPARE(current.widgets, baseline.widgets);
        QCOMPARE(current.classes, baseline.classes);
    }
    QVERIFY(manager.commands().remove(QStringLiteral("test.retainedContribution")));
    settle();
    for (const auto &menu : sharedMenus)
        QVERIFY(menu.isNull());
    for (const auto &bar : bars) {
        // Core placeholders remain even if their host command is not present.
        QVERIFY(bar->findChild<QMenu *>(QStringLiteral("file")));
        QVERIFY(bar->findChild<QMenu *>(QStringLiteral("tools")));
    }
    recordSample(manager, "register-remove-menu", 100);
}

void MemoryLifecycleTest::commandMayUnloadWhileItsMenuIsBeingCreated() {
    bool removeOnCheck = true;
    WindowManager manager;
    const auto root = manager.createWindow();
    auto *bar = manager.windowWidget(root)->findChild<QMenuBar *>(QStringLiteral("MenuStrip"));
    QVERIFY(bar);
    settle();
    const auto baseline = ownedObjects(manager);
    QVERIFY(manager.commands().add({QStringLiteral("test.selfUnloadingContribution"),
                                    QStringLiteral("Self unloading"),
                                    {},
                                    {},
                                    [&manager, &removeOnCheck](const CommandContext &) {
                                        if (removeOnCheck) {
                                            removeOnCheck = false;
                                            manager.commands().remove(
                                                QStringLiteral("test.selfUnloadingContribution"));
                                        }
                                        return true;
                                    },
                                    [](const CommandContext &) { return true; },
                                    {QStringLiteral("Self unloading")}}));
    settle();
    QVERIFY(!removeOnCheck);
    QVERIFY(!manager.commands().find(QStringLiteral("test.selfUnloadingContribution")));
    QVERIFY(!bar->findChild<QAction *>(QStringLiteral("test.selfUnloadingContribution")));
    for (auto *menu : bar->findChildren<QMenu *>())
        QVERIFY(menu->property("commandMenuPath").toStringList() !=
                QStringList{QStringLiteral("Self unloading")});
    settle();
    const auto current = ownedObjects(manager);
    QCOMPARE(current.objects, baseline.objects);
    QCOMPARE(current.widgets, baseline.widgets);
    QCOMPARE(current.classes, baseline.classes);
}

QTEST_MAIN(MemoryLifecycleTest)
#include "memory_lifecycle_test.moc"
