// SPDX-License-Identifier: MIT
#include <window_system/window_manager.hpp>

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
#include <QPlainTextEdit>
#include <QPointer>
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

ObjectSnapshot ownedObjects(WindowManager &manager) {
    QSet<QObject *> visited;
    const auto visit = [&visited](auto &&recurse, QObject *object) -> void {
        if (!object || visited.contains(object))
            return;
        visited.insert(object);
        for (auto *child : object->children())
            recurse(recurse, child);
    };
    visit(visit, &manager);
    for (const auto id : manager.model().windowOrder())
        visit(visit, manager.windowWidget(id));
    // Parked pane widgets live in the manager's separate hidden QWidget root.
    // Qt forbids giving that root the non-widget WindowManager as its parent.
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

QJsonObject recordSample(WindowManager &manager, const char *scenario, int iteration) {
    auto result = nativeResources();
    const auto objects = ownedObjects(manager);
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
    WindowManager source;
    const auto window = source.createWindow();
    source.addTab(window, new QLabel(QStringLiteral("Replacement")),
                  {QStringLiteral("Replacement"), QStringLiteral("test.label"), {}});
    return source.saveLayout();
}

} // namespace

class MemoryLifecycleTest final : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); }
    void cleanup() { settle(); }
    void kindChangesKeepOneCommandSubscriptionPerWindow();
    void closedLayoutsReleaseTheirPaneTrees();
    void repeatedLayoutAndWindowCyclesReleaseOwnedResources();
    void removedCommandsReleaseDynamicMenus();
    void commandMayUnloadWhileItsMenuIsBeingCreated();
};

void MemoryLifecycleTest::kindChangesKeepOneCommandSubscriptionPerWindow() {
    QHash<WindowId, int> checks;
    WindowManager manager;
    QVERIFY(manager.commands().add({QStringLiteral("test.memoryProbe"),
                                    QStringLiteral("Lifecycle probe"),
                                    QStringLiteral("Test"),
                                    {},
                                    [&checks](const CommandContext &context) {
                                        ++checks[context.window];
                                        return true;
                                    },
                                    [](const CommandContext &) { return true; },
                                    {QStringLiteral("tools")}}));
    const auto owner = manager.createWindow();
    const auto target = manager.createWindow();
    QVERIFY(!manager
                 .addTab(target, new QLabel(QStringLiteral("Preserved")),
                         {QStringLiteral("Preserved"), QStringLiteral("test.label"), {}})
                 .isNull());
    settle();
    checks.clear();
    manager.commandsChanged();
    QCOMPARE(checks.value(owner), 1);
    QCOMPARE(checks.value(target), 1);
    for (int iteration = 0; iteration < 24; ++iteration) {
        QVERIFY(manager.demoteWindow(target, owner));
        QVERIFY(manager.promoteWindow(target));
        settle();
        checks.clear();
        manager.commandsChanged();
        // A subscription tied to each discarded chrome would produce 3, 5,
        // 7... checks in the surviving target after each conversion pair.
        QCOMPARE(checks.value(owner), 1);
        QCOMPARE(checks.value(target), 1);
    }
    recordSample(manager, "kind-subscription", 24);
}

void MemoryLifecycleTest::closedLayoutsReleaseTheirPaneTrees() {
    WindowManager manager;
    manager.setContentFactory([](const TabOptions &options, PaneId) { return new QLabel(options.title); });
    QVERIFY(manager.restoreLayout(freshLayout()));
    settle();
    for (int iteration = 0; iteration < 12; ++iteration) {
        const auto oldTab = manager.model().tabs().cbegin().key();
        std::weak_ptr<const LayoutNode> oldTree = manager.model().tab(oldTab)->panes;
        QPointer<QWidget> oldContent = manager.paneWidget(manager.model().tab(oldTab)->activePane);
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
    recordSample(manager, "replace-layout", 12);
}

void MemoryLifecycleTest::repeatedLayoutAndWindowCyclesReleaseOwnedResources() {
    WindowManager manager;
    const auto root = manager.createWindow();
    QPointer<QWidget> anchorContent = new QLabel(QStringLiteral("Anchor"));
    const auto anchor =
        manager.addTab(root, anchorContent, {QStringLiteral("Anchor"), QStringLiteral("test.label"), {}});
    const auto anchorGroup = manager.model().groupForTab(anchor);
    manager.showWindow(root);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(root)));
    settle();
    ObjectSnapshot baseline;
    // The first four cycles warm Qt's lazily created native/style resources.
    // Counts are then compared exactly; private bytes and global OS handles
    // are recorded rather than declared leak-free using a generous threshold.
    for (int iteration = 0; iteration < 28; ++iteration) {
        QPointer<QPlainTextEdit> content = new QPlainTextEdit;
        content->setPlainText(QString(16384, QLatin1Char('x')));
        const auto tab = manager.addTab(
            root, content, {QStringLiteral("Temporary"), QStringLiteral("test.editor"), {}}, anchorGroup);
        QVERIFY(!tab.isNull());
        const auto pane = manager.model().tab(tab)->activePane;
        QPointer<QWidget> second = new QLabel(QStringLiteral("Split pane"));
        QVERIFY(!manager.splitPane(tab, pane, second, Placement::Right).isNull());
        QPointer<QWidget> top = new QLabel(QStringLiteral("Top bar"));
        QPointer<QWidget> bottom = new QLabel(QStringLiteral("Bottom bar"));
        QVERIFY(manager.setPaneTopBar(pane, top));
        QVERIFY(manager.setPaneBottomBar(pane, bottom));
        QVERIFY(manager.moveTab(tab, anchorGroup, Placement::Right));
        QVERIFY(manager.moveTabToEdge(tab, Edge(iteration % 4)));
        QVERIFY(manager.activateTab(tab));
        QVERIFY(manager.pinTab(tab, true));
        QVERIFY(manager.pinTab(tab, false));
        QVERIFY(manager.moveTab(tab, anchorGroup));
        const auto floating = manager.detachTab(tab);
        QVERIFY(!floating.isNull());
        QPointer<QWidget> retiredWindow = manager.windowWidget(floating);
#ifdef Q_OS_WIN
        const auto retiredHandle = reinterpret_cast<HWND>(retiredWindow->effectiveWinId());
        QVERIFY(IsWindow(retiredHandle));
#endif
        QVERIFY(manager.promoteWindow(floating));
        QVERIFY(manager.demoteWindow(floating, root));
        QVERIFY(manager.moveAllTabs(floating, anchorGroup));
        QVERIFY(manager.closeTab(tab));
        QVERIFY(manager.activateTab(anchor));
        settle();
        QVERIFY(retiredWindow.isNull());
        QVERIFY(content.isNull());
        QVERIFY(second.isNull());
        QVERIFY(top.isNull());
        QVERIFY(bottom.isNull());
#ifdef Q_OS_WIN
        QVERIFY(!IsWindow(retiredHandle));
#endif
        QCOMPARE(manager.model().windows().size(), 1);
        QCOMPARE(manager.model().groups().size(), 1);
        QCOMPARE(manager.model().tabs().size(), 1);
        QCOMPARE(manager.paneWidget(manager.model().tab(anchor)->activePane), anchorContent.data());
        const auto current = ownedObjects(manager);
        if (iteration == 3)
            baseline = current;
        else if (iteration > 3) {
            QCOMPARE(current.widgets, baseline.widgets);
            QCOMPARE(current.objects, baseline.objects);
            QCOMPARE(current.classes, baseline.classes);
        }
        if (iteration == 3 || iteration == 15 || iteration == 27) {
            const auto sample = recordSample(manager, "open-move-close", iteration + 1);
#ifdef Q_OS_WIN
            QVERIFY(sample.value(QStringLiteral("nativeCountersAvailable")).toBool());
#endif
        }
    }
    QPointer<QWidget> rootWindow = manager.windowWidget(root);
    QVERIFY(manager.requestCloseAllWindows());
    settle();
    QVERIFY(anchorContent.isNull());
    QVERIFY(rootWindow.isNull());
    QCOMPARE(manager.model().windows().size(), 0);
    QCOMPARE(manager.model().tabs().size(), 0);
    const auto final = ownedObjects(manager);
    // Only the empty hidden storage and the manager's long-lived helpers remain.
    QCOMPARE(final.widgets, 1);
    recordSample(manager, "all-closed", 28);
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
