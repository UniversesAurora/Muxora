// SPDX-License-Identifier: MIT
#include <window_system/window_manager.hpp>
#include <window_system/quick_workspace.hpp>

#include <QCoreApplication>
#include <QApplication>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QVBoxLayout>
#include <algorithm>
#include <memory>

using namespace window_system;

namespace {
TabId add(WindowManager& manager, WindowId window, const QString& title, GroupId group = {})
{
    return manager.addTab(window, new QLabel(title), {title, "test.text", {}}, group);
}
PaneId activePane(const WindowManager& manager, TabId tab)
{
    return manager.model().tab(tab)->activePane;
}
QWidget* ancestorNamed(QWidget* widget, const QString& name)
{
    for (; widget; widget = widget->parentWidget())
        if (widget->objectName() == name) return widget;
    return nullptr;
}
QWidget* frameOf(const WindowManager& manager, PaneId pane)
{
    return ancestorNamed(manager.paneWidget(pane), QStringLiteral("PaneHost"));
}
QWidget* pageOf(const WindowManager& manager, TabId tab)
{
    return ancestorNamed(manager.paneWidget(activePane(manager, tab)), QStringLiteral("TabPage"));
}
QJsonObject snapshotWithInactiveTabs(int count,
                                    WindowManager::Presentation presentation = WindowManager::Presentation::Widgets)
{
    LayoutModel model;
    const auto window = model.createWindow();
    GroupId group;
    QVector<TabId> tabs;
    for (int index = 0; index < count; ++index) {
        const auto title = QStringLiteral("Tab %1").arg(index);
        const TabId tab = group.isNull() ? model.addTab(window, title)
                                         : model.addTab(window, title, group);
        if (tab.isNull()) return {};
        group = model.groupForTab(tab);
        tabs.append(tab);
    }
    WindowManager seed(presentation);
    std::unique_ptr<QuickWorkspace> workspace;
    if (presentation == WindowManager::Presentation::External)
        workspace = std::make_unique<QuickWorkspace>(seed);
    seed.createWindow(WindowKind::Workbench);
    if (workspace) QCoreApplication::sendPostedEvents(workspace.get(), QEvent::MetaCall);
    auto snapshot = seed.saveLayout();
    snapshot.insert(QStringLiteral("model"), model.toJson());
    QJsonArray panes;
    for (const auto tab : tabs) {
        const auto* record = model.tab(tab);
        panes.append(QJsonObject{{"id", record->activePane.toString(QUuid::WithoutBraces)},
            {"title", record->title}, {"type", "test.text"}, {"state", QJsonObject{}},
            {"showIcon", true}, {"topBarSharing", "shared"}, {"bottomBarSharing", "shared"}});
    }
    snapshot.insert(QStringLiteral("panes"), panes);
    auto windows = snapshot.value("windows").toArray();
    auto metadata = windows.first().toObject();
    metadata.insert("id", window.toString(QUuid::WithoutBraces));
    metadata.insert("title", "Lazy tabs");
    windows.replace(0, metadata);
    snapshot.insert(QStringLiteral("windows"), windows);
    return snapshot;
}
bool restoreTabs(WindowManager& manager, const QJsonObject& snapshot)
{
    manager.setContentFactory([](const TabOptions& options, PaneId) {
        return new QLabel(options.title);
    });
    return manager.restoreLayout(snapshot).ok;
}
void settle()
{
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}
class ParentChangeProbe final : public QObject {
public:
    ParentChangeProbe(QWidget* watched, const bool& notified)
        : watched_(watched), notified_(notified)
    {
        watched->installEventFilter(this);
    }
    bool firstChangeHadVisibilityNotification = false;
    int changes = 0;
protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == watched_ && event->type() == QEvent::ParentChange) {
            if (changes++ == 0) firstChangeHadVisibilityNotification = notified_;
        }
        return false;
    }
private:
    QPointer<QWidget> watched_;
    const bool& notified_;
};
} // namespace

class ViewReuseTest final : public QObject {
    Q_OBJECT

private slots:
    void cleanup() { settle(); }
    void restoringLayoutDiscardsPendingTabBars();
    void contentVisibilityNotificationsAreCoalesced();
    void crossWindowMoveReportsHiddenBeforeReparent();
    void mountedRebuildIsVisibleImmediately();
    void unrelatedRefreshKeepsTheFocusedChildWhilePaneNavigationStillWorks();
};

void ViewReuseTest::restoringLayoutDiscardsPendingTabBars()
{
    const auto snapshot = snapshotWithInactiveTabs(2);
    WindowManager manager;
    QVERIFY(restoreTabs(manager, snapshot));
    const auto window = manager.model().windowOrder().first();
    const auto tabs = manager.model().orderedTabs(window);
    TabId inactive;
    for (const auto tab : tabs)
        if (manager.model().group(manager.model().groupForTab(tab))->activeTab != tab) inactive = tab;
    QVERIFY(!inactive.isNull());
    auto* pending = new QLabel(QStringLiteral("Old pending"));
    QPointer<QWidget> observer(pending);
    QVERIFY(manager.setTabTopBar(inactive, pending));
    QVERIFY(restoreTabs(manager, snapshot));
    settle();
    QVERIFY(observer.isNull());
}

void ViewReuseTest::contentVisibilityNotificationsAreCoalesced()
{
    const auto snapshot = snapshotWithInactiveTabs(3, WindowManager::Presentation::External);
    WindowManager manager(WindowManager::Presentation::External);
    QuickWorkspace workspace(manager);
    workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
    QVERIFY(manager.restoreLayout(snapshot));
    const auto window = manager.model().windowOrder().first();
    QTRY_VERIFY(workspace.window(window));
    manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(workspace.window(window)));
    settle();
    QVector<QVector<ContentVisibilityChange>> batches;
    manager.setContentVisibilityHandler([&batches](const QVector<ContentVisibilityChange>& changes) {
        batches.append(changes);
    });
    QTRY_COMPARE(batches.size(), 1);
    QCOMPARE(batches.first().size(), 3);
    batches.clear();
    const auto tabs = manager.model().orderedTabs(window);
    const auto initiallyActive = manager.context(window).tab;
    QVector<TabId> candidates;
    for (const auto tab : tabs) if (tab != initiallyActive) candidates.append(tab);
    QVERIFY(candidates.size() >= 2);
    QVERIFY(manager.activateTab(candidates[0]));
    QVERIFY(manager.activateTab(candidates[1]));
    QTRY_COMPARE(batches.size(), 1);
    const auto& last = batches.first();
    const auto previous = manager.model().tab(initiallyActive)->activePane;
    const auto current = manager.model().tab(candidates[1])->activePane;
    QVERIFY(std::any_of(last.cbegin(), last.cend(), [previous](const auto& change) {
        return change.pane == previous && !change.visible;
    }));
    QVERIFY(std::any_of(last.cbegin(), last.cend(), [current](const auto& change) {
        return change.pane == current && change.visible;
    }));
}

void ViewReuseTest::unrelatedRefreshKeepsTheFocusedChildWhilePaneNavigationStillWorks()
{
    WindowManager manager;
    const auto window = manager.createWindow();
    auto* content = new QWidget;
    auto* column = new QVBoxLayout(content);
    auto* defaultEditor = new QLineEdit(content);
    auto* currentEditor = new QLineEdit(QStringLiteral("editing"), content);
    column->addWidget(defaultEditor);
    column->addWidget(currentEditor);
    content->setFocusProxy(defaultEditor);
    const auto tab = manager.addTab(window, content, {"composite", "test.form", {}});
    const auto pane = activePane(manager, tab);
    auto* otherEditor = new QLineEdit;
    const auto otherPane = manager.splitPane(tab, pane, otherEditor, Placement::Right);
    QVERIFY(!otherPane.isNull());
    const auto unrelatedTab = add(manager, window, QStringLiteral("unrelated"));
    QVERIFY(manager.moveTab(unrelatedTab, manager.model().groupForTab(tab), Placement::Right));
    manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowActive(manager.windowWidget(window)));
    currentEditor->setFocus();
    QCOMPARE(QApplication::focusWidget(), currentEditor);
    currentEditor->setSelection(1, 3);
    QVERIFY(manager.renameTab(unrelatedTab, QStringLiteral("renamed")));
    settle();
    QCOMPARE(QApplication::focusWidget(), currentEditor);
    QCOMPARE(currentEditor->selectionStart(), 1);
    QCOMPARE(currentEditor->selectedText(), QStringLiteral("dit"));
    QVERIFY(manager.focusPane(tab, pane));
    QCOMPARE(QApplication::focusWidget(), currentEditor);
    QCOMPARE(currentEditor->selectedText(), QStringLiteral("dit"));
    QVERIFY(manager.focusPane(tab, otherPane));
    QCOMPARE(QApplication::focusWidget(), otherEditor);
    QCOMPARE(manager.context(window).pane, otherPane);
    QVERIFY(manager.focusPane(tab, pane));
    QCOMPARE(QApplication::focusWidget(), defaultEditor);
    QCOMPARE(manager.context(window).pane, pane);
}

void ViewReuseTest::crossWindowMoveReportsHiddenBeforeReparent()
{
    WindowManager manager;
    const auto source = manager.createWindow();
    const auto target = manager.createWindow();
    const auto tab = add(manager, source, "moving");
    QVERIFY(!add(manager, target, "target").isNull());
    manager.showWindow(source);
    manager.showWindow(target);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(source)));
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(target)));
    settle();
    const auto pane = activePane(manager, tab);
    QPointer<QWidget> page = pageOf(manager, tab);
    QVERIFY(page);
    bool notifiedHidden = false;
    manager.setContentVisibilityHandler([&](const QVector<ContentVisibilityChange>& changes) {
        if (std::any_of(changes.cbegin(), changes.cend(), [pane](const auto& change) {
                return change.pane == pane && !change.visible;
            })) notifiedHidden = true;
    });
    settle();
    notifiedHidden = false;
    ParentChangeProbe probe(page, notifiedHidden);
    QVERIFY(manager.moveTabToWindow(tab, target));
    QVERIFY(probe.changes > 0);
    QVERIFY(probe.firstChangeHadVisibilityNotification);
}

void ViewReuseTest::mountedRebuildIsVisibleImmediately()
{
    WindowManager manager;
    const auto window = manager.createWindow();
    const auto tab = add(manager, window, "tab");
    add(manager, window, "other", manager.model().groupForTab(tab));
    QVERIFY(manager.activateTab(tab));
    manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    settle();
    const auto original = manager.paneWidget(activePane(manager, tab));
    QVERIFY(original->isVisible());
    // Without returning to the event loop, the rebuilt tree is shown.
    const auto created = manager.splitPane(tab, activePane(manager, tab), new QLabel("split"), Placement::Right);
    QVERIFY(!created.isNull());
    QVERIFY(manager.paneWidget(created)->isVisible());
    QVERIFY(original->isVisible());
    QVERIFY(frameOf(manager, created)->isVisible());
}

QTEST_MAIN(ViewReuseTest)
#include "view_reuse_test.moc"
