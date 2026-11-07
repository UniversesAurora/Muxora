// SPDX-License-Identifier: MIT
#include <window_system/window_manager.hpp>
#include <window_system/quick_workspace.hpp>

#include <QGuiApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QJsonDocument>
#include <QJsonObject>
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

qsizetype ownedObjects(WindowManager &manager, QuickWorkspace &workspace) {
    QSet<QObject *> visited;
    const auto visit = [&visited](auto &&recurse, QObject *object) -> void {
        if (!object || visited.contains(object)) return;
        visited.insert(object);
        for (auto *child : object->children()) recurse(recurse, child);
    };
    visit(visit, &manager);
    visit(visit, &workspace);
    for (const auto id : manager.model().windowOrder()) visit(visit, workspace.window(id));
    return visited.size();
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

void recordSample(WindowManager &manager, const char *scenario, int iteration,
                  QuickWorkspace &workspace) {
    auto result = nativeResources();
    const auto objects = ownedObjects(manager, workspace);
    result.insert(QStringLiteral("scenario"), QString::fromLatin1(scenario));
    result.insert(QStringLiteral("iteration"), iteration);
    result.insert(QStringLiteral("ownedObjects"), objects);
    result.insert(QStringLiteral("windows"), manager.model().windows().size());
    result.insert(QStringLiteral("tabs"), manager.model().tabs().size());
    qInfo().noquote() << "MEMORY_LIFECYCLE" << QJsonDocument(result).toJson(QJsonDocument::Compact);
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
    recordSample(manager, "replace-layout", 12, workspace);
}

QTEST_MAIN(MemoryLifecycleTest)
#include "memory_lifecycle_test.moc"
