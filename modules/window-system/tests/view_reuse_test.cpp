// SPDX-License-Identifier: MIT
#include <window_system/window_manager.hpp>
#include "docking/dock_surface.hpp"
#include "docking/tab_strip.hpp"

#include <QCoreApplication>
#include <QApplication>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QSplitter>
#include "docking/tab_bar.hpp"
#include <QTest>
#include <QVBoxLayout>
#include <algorithm>

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
QSet<QWidget*> framesOf(const WindowManager& manager, TabId tab)
{
    QSet<QWidget*> result;
    for (const auto pane : manager.model().panes(tab)) result.insert(frameOf(manager, pane));
    return result;
}
QWidget* stripOf(const WindowManager& manager, WindowId window, GroupId group)
{
    for (auto* widget : manager.windowWidget(window)->findChildren<QWidget*>(QStringLiteral("TabGroup")))
        if (widget->property("groupId").toUuid() == group) return widget;
    return nullptr;
}
// One window with two central groups of three tabs each.
struct TwoGroups {
    WindowId window;
    GroupId first, second;
    QVector<TabId> firstTabs, secondTabs;
};
QJsonObject snapshotWithInactiveTabs(int count)
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
    WindowManager seed;
    seed.createWindow(WindowKind::Workbench);
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
TwoGroups createTwoGroups(WindowManager& manager, WindowId window)
{
    TwoGroups result{window, {}, {}, {}, {}};
    result.firstTabs << add(manager, window, "a0");
    result.first = manager.model().groupForTab(result.firstTabs[0]);
    result.firstTabs << add(manager, window, "a1", result.first) << add(manager, window, "a2", result.first);
    const auto other = add(manager, window, "b0", result.first);
    manager.moveTab(other, result.first, Placement::Right);
    result.second = manager.model().groupForTab(other);
    result.secondTabs << other << add(manager, window, "b1", result.second) << add(manager, window, "b2", result.second);
    return result;
}
void settle()
{
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}
// Runs a host callback from the content's own resize event.
class ResizeHook final : public QLabel {
public:
    using QLabel::QLabel;
    std::function<void()> resized;
protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QLabel::resizeEvent(event);
        const auto callback = resized;
        if (callback) callback();
    }
};
// Records every parent a watched widget receives.
class ParentRecorder final : public QObject {
public:
    void watch(QWidget* widget) { widget->installEventFilter(this); }
    int changes(QObject* widget) const { return int(parents_.value(widget).size()); }
    bool everParkedIn(QObject* widget, const QString& name) const
    {
        for (const auto& parent : parents_.value(widget))
            if (parent && parent->objectName() == name) return true;
        return false;
    }
protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::ParentChange)
            parents_[watched].append(static_cast<QWidget*>(watched)->parentWidget());
        return false;
    }
private:
    QHash<QObject*, QVector<QPointer<QWidget>>> parents_;
};
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
    void unrelatedTabsKeepTheirViewWidgets();
    void restoredInactiveTabsCreateViewsOnFirstActivation();
    void pendingTabBarsFollowLazyActivationAndRemoval();
    void pendingTabBarsSurviveHierarchyConversion();
    void restoringLayoutDiscardsPendingTabBars();
    void contentVisibilityNotificationsAreCoalesced();
    void contentVisibilityTracksWindowStateAndPeek();
    void changedTreeRebuildsOnlyThatTab();
    void themeMetricChangeRebuilds();
    void tabViewsStayInTheirWindow();
    void crossWindowMoveReparentsOnlyTheMovedTab();
    void crossWindowMoveReportsHiddenBeforeReparent();
    void deferredRootDeletionKeepsCarriedPages();
    void mountedRebuildIsVisibleImmediately();
    void reusedStripFollowsMetadataAndTheme();
    void unrelatedRefreshKeepsTheFocusedChildWhilePaneNavigationStillWorks();
    void mutationFromSplitterLayoutKeepsReusedStrip_data();
    void mutationFromSplitterLayoutKeepsReusedStrip();
};

void ViewReuseTest::restoredInactiveTabsCreateViewsOnFirstActivation()
{
    const auto snapshot = snapshotWithInactiveTabs(100);
    WindowManager manager;
    QVERIFY(restoreTabs(manager, snapshot));
    const auto window = manager.model().windowOrder().first();
    manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    settle();
    const auto pageCount = [&] {
        return manager.windowWidget(window)->findChildren<QWidget*>(QStringLiteral("TabPage")).size();
    };
    const auto tabs = manager.model().orderedTabs(window);
    QCOMPARE(tabs.size(), 100);
    QCOMPARE(pageCount(), 1);
    auto* strip = dynamic_cast<GroupTabs*>(
        manager.windowWidget(window)->findChild<QWidget*>(QStringLiteral("TabGroup")));
    QVERIFY(strip);
    QCOMPARE(strip->count(), 100);
    strip->setCurrentIndex(0);
    settle();
    QCOMPARE(manager.context(window).tab, tabs.first());
    QCOMPARE(pageCount(), 2);
    int expected = int(pageCount());
    for (const auto tab : tabs) {
        const auto pane = manager.model().tab(tab)->activePane;
        const auto* group = manager.model().group(manager.model().groupForTab(tab));
        if (group->activeTab == tab || manager.context(window).tab == tab) continue;
        QPointer<QWidget> content = manager.paneWidget(pane);
        QVERIFY(content);
        QVERIFY(manager.activateTab(tab));
        settle();
        QCOMPARE(pageCount(), ++expected);
        QCOMPARE(manager.paneWidget(pane), content.data());
        if (expected == 3) break;
    }
    QCOMPARE(expected, 3);
}

void ViewReuseTest::pendingTabBarsFollowLazyActivationAndRemoval()
{
    const auto snapshot = snapshotWithInactiveTabs(3);
    WindowManager manager;
    QVERIFY(restoreTabs(manager, snapshot));
    const auto window = manager.model().windowOrder().first();
    manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    settle();
    const auto tabs = manager.model().orderedTabs(window);
    QVector<TabId> inactive;
    for (const auto tab : tabs)
        if (manager.model().group(manager.model().groupForTab(tab))->activeTab != tab) inactive.append(tab);
    QVERIFY(inactive.size() >= 2);
    auto* bar = new QLabel(QStringLiteral("Tab bar"));
    QPointer<QWidget> first(bar);
    QVERIFY(manager.setTabTopBar(inactive[0], bar));
    QVERIFY(bar->parentWidget());
    QCOMPARE(bar->parentWidget()->objectName(), QStringLiteral("WindowSystemStorage"));
    auto* replacement = new QLabel(QStringLiteral("Replacement"));
    QPointer<QWidget> observer(replacement);
    QVERIFY(manager.setTabTopBar(inactive[0], replacement));
    QVERIFY(first && first->isHidden());
    settle();
    QVERIFY(first.isNull());
    QVERIFY(manager.activateTab(inactive[0]));
    settle();
    QVERIFY(observer && observer->isVisible());
    QVERIFY(observer->parentWidget()->objectName() == QStringLiteral("TabPage"));
    QVERIFY(manager.setTabTopBar(inactive[0], nullptr));
    settle();
    QVERIFY(observer.isNull());

    auto* pending = new QLabel(QStringLiteral("Pending"));
    QPointer<QWidget> pendingObserver(pending);
    QVERIFY(manager.setTabTopBar(inactive[1], pending));
    QVERIFY(manager.closeTab(inactive[1]));
    settle();
    QVERIFY(pendingObserver.isNull());
}

void ViewReuseTest::pendingTabBarsSurviveHierarchyConversion()
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
    auto* top = new QLabel(QStringLiteral("Pending top"));
    auto* bottom = new QLabel(QStringLiteral("Pending bottom"));
    QPointer<QWidget> topObserver(top), bottomObserver(bottom);
    QVERIFY(manager.setTabTopBar(inactive, top));
    QVERIFY(manager.setTabBottomBar(inactive, bottom));
    const auto group = manager.model().groupForTab(inactive);
    QVERIFY(!manager.convertGroupToTab(group).isNull());
    settle();
    QVERIFY(topObserver && bottomObserver);
    QVERIFY(topObserver->isVisible());
    QVERIFY(bottomObserver->isVisible());
    QVERIFY(ancestorNamed(topObserver, QStringLiteral("TabPage")));
    QVERIFY(ancestorNamed(bottomObserver, QStringLiteral("TabPage")));
}

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
    const auto snapshot = snapshotWithInactiveTabs(3);
    WindowManager manager;
    QVERIFY(restoreTabs(manager, snapshot));
    const auto window = manager.model().windowOrder().first();
    manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
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

void ViewReuseTest::contentVisibilityTracksWindowStateAndPeek()
{
    WindowManager manager;
    const auto window = manager.createWindow();
    const auto central = add(manager, window, "central");
    const auto side = add(manager, window, "side");
    QVERIFY(manager.moveTabToEdge(side, Edge::Right));
    const auto sideGroup = manager.model().groupForTab(side);
    QVERIFY(manager.activateTab(central));
    manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowActive(manager.windowWidget(window)));
    settle();
    QVector<QVector<ContentVisibilityChange>> batches;
    manager.setContentVisibilityHandler([&batches](const QVector<ContentVisibilityChange>& changes) {
        batches.append(changes);
    });
    QTRY_COMPARE(batches.size(), 1);
    batches.clear();

    auto* surface = dynamic_cast<DockSurface*>(
        manager.windowWidget(window)->findChild<QWidget*>("DockSurface"));
    QVERIFY(surface);
    surface->showEdgeGroup(sideGroup, side);
    QTRY_COMPARE(batches.size(), 1);
    const auto sidePane = manager.model().tab(side)->activePane;
    const auto centralPane = manager.model().tab(central)->activePane;
    const auto& peekBatch = batches.last();
    QVERIFY(std::any_of(peekBatch.cbegin(), peekBatch.cend(), [sidePane](const auto& change) {
        return change.pane == sidePane && change.visible;
    }));

    batches.clear();
    surface->closePeek();
    QTRY_COMPARE(batches.size(), 1);
    QVERIFY(std::any_of(batches.last().cbegin(), batches.last().cend(), [sidePane](const auto& change) {
        return change.pane == sidePane && !change.visible;
    }));

    batches.clear();
    manager.windowWidget(window)->showMinimized();
    QTRY_VERIFY(manager.windowWidget(window)->isMinimized());
    QTRY_VERIFY(!batches.isEmpty());
    QVERIFY(std::any_of(batches.last().cbegin(), batches.last().cend(), [centralPane](const auto& change) {
        return change.pane == centralPane && !change.visible;
    }));
    batches.clear();
    manager.windowWidget(window)->showNormal();
    QTRY_VERIFY(!manager.windowWidget(window)->isMinimized());
    QTRY_VERIFY(!batches.isEmpty());
    QVERIFY(std::any_of(batches.last().cbegin(), batches.last().cend(), [centralPane](const auto& change) {
        return change.pane == centralPane && change.visible;
    }));
}

void ViewReuseTest::unrelatedTabsKeepTheirViewWidgets()
{
    WindowManager manager;
    const auto window = manager.createWindow();
    const auto first = add(manager, window, "first");
    const auto group = manager.model().groupForTab(first);
    const auto second = add(manager, window, "second", group);
    const auto third = add(manager, window, "third", group);
    const auto thirdFirst = activePane(manager, third);
    const auto thirdSecond = manager.splitPane(third, thirdFirst, new QLabel("third-split"), Placement::Right);
    QVERIFY(!thirdSecond.isNull());
    settle();
    const auto secondPane = activePane(manager, second);
    const QVector<PaneId> panes{secondPane, thirdFirst, thirdSecond};
    QVector<QPointer<QWidget>> contents, frames, parents;
    for (const auto pane : panes) {
        contents << manager.paneWidget(pane);
        frames << frameOf(manager, pane);
        parents << manager.paneWidget(pane)->parentWidget();
        QVERIFY(frames.last());
    }
    const QPointer<QWidget> splitter = frames[1]->parentWidget();
    QVERIFY(qobject_cast<QSplitter*>(splitter.data()));
    QCOMPARE(splitter->objectName(), QStringLiteral("PaneSplitter"));
    QCOMPARE(frames[2]->parentWidget(), splitter.data());
    ParentRecorder recorder;
    for (const auto& content : contents) recorder.watch(content);

    const auto verify = [&](const char* step) {
        settle();
        for (int i = 0; i < panes.size(); ++i) {
            QVERIFY2(contents[i] && manager.paneWidget(panes[i]) == contents[i], step);
            QVERIFY2(frames[i] && frameOf(manager, panes[i]) == frames[i], step);
            QVERIFY2(contents[i]->parentWidget() == parents[i], step);
            QVERIFY2(recorder.changes(contents[i]) == 0, step);
        }
        QVERIFY2(splitter && frames[1]->parentWidget() == splitter, step);
    };
    QVERIFY(manager.closeTab(first));
    verify("close");
    const auto other = add(manager, window, "other");
    QVERIFY(manager.moveTab(other, group, Placement::Right));
    const auto moved = add(manager, window, "moved", manager.model().groupForTab(other));
    QVERIFY(manager.moveTab(moved, group, Placement::Center));
    verify("move");
    QVERIFY(manager.renameTab(second, "renamed"));
    verify("rename");
    QCOMPARE(manager.model().tab(second)->title, QStringLiteral("renamed"));
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

void ViewReuseTest::changedTreeRebuildsOnlyThatTab()
{
    WindowManager manager;
    const auto window = manager.createWindow();
    const auto first = add(manager, window, "first");
    const auto group = manager.model().groupForTab(first);
    const auto second = add(manager, window, "second", group);
    const auto third = add(manager, window, "third", group);
    QVERIFY(!manager.splitPane(third, activePane(manager, third), new QLabel("split"), Placement::Bottom).isNull());
    settle();
    const auto secondFrames = framesOf(manager, second);
    const auto thirdFrames = framesOf(manager, third);
    QCOMPARE(secondFrames.size(), 1);
    QCOMPARE(thirdFrames.size(), 2);
    QVector<QPointer<QWidget>> watchedThird;
    for (auto* frame : thirdFrames) watchedThird << frame;
    const QPointer<QWidget> oldSecondFrame = *secondFrames.cbegin();
    const auto secondContent = manager.paneWidget(activePane(manager, second));

    const auto created = manager.splitPane(second, activePane(manager, second), new QLabel("new"), Placement::Right);
    QVERIFY(!created.isNull());
    settle();
    const auto rebuilt = framesOf(manager, second);
    QCOMPARE(rebuilt.size(), 2);
    QVERIFY(!rebuilt.contains(oldSecondFrame.data()));
    QCOMPARE(manager.paneWidget(manager.model().panes(second).first()), secondContent);
    // Compare through QPointer: a rebuilt view would leave dangling pointers.
    const auto current = framesOf(manager, third);
    QCOMPARE(current.size(), thirdFrames.size());
    for (const auto& frame : watchedThird) {
        QVERIFY(frame);
        QVERIFY(current.contains(frame.data()));
    }
}

void ViewReuseTest::themeMetricChangeRebuilds()
{
    WindowManager manager;
    const auto window = manager.createWindow();
    const auto tab = add(manager, window, "tab");
    const auto other = add(manager, window, "other", manager.model().groupForTab(tab));
    QVERIFY(!manager.splitPane(tab, activePane(manager, tab), new QLabel("split"), Placement::Right).isNull());
    settle();
    const auto splitterOf = [&] {
        return qobject_cast<QSplitter*>(frameOf(manager, activePane(manager, tab))->parentWidget());
    };
    QVERIFY(splitterOf());
    auto theme = manager.theme();
    const int width = qMax(1, theme.metrics.splitterWidth) + 5;
    QCOMPARE_NE(splitterOf()->handleWidth(), width);
    theme.metrics.splitterWidth = width;
    manager.setTheme(theme);
    // An unrelated change refreshes the views; reuse must still honour the
    // new metric instead of keeping the splitter built for the old theme.
    QVERIFY(manager.renameTab(other, "renamed"));
    settle();
    QVERIFY(splitterOf());
    QCOMPARE(splitterOf()->handleWidth(), width);
}

void ViewReuseTest::tabViewsStayInTheirWindow()
{
    WindowManager manager;
    const auto layout = createTwoGroups(manager, manager.createWindow());
    settle();
    QVERIFY(layout.first != layout.second);
    const auto remaining = layout.firstTabs.mid(1) + layout.secondTabs;
    QHash<TabId, QPointer<QWidget>> pages;
    ParentRecorder recorder;
    for (const auto tab : remaining) {
        pages.insert(tab, pageOf(manager, tab));
        QVERIFY(pages[tab]);
        recorder.watch(pages[tab]);
    }
    const QPointer<QWidget> secondStrip = stripOf(manager, layout.window, layout.second);
    QVERIFY(secondStrip);
    QVERIFY(manager.closeTab(layout.firstTabs[0]));
    settle();
    QVERIFY(secondStrip);
    QCOMPARE(stripOf(manager, layout.window, layout.second), secondStrip.data());
    for (const auto tab : remaining) {
        QVERIFY(pages[tab]);
        QCOMPARE(pageOf(manager, tab), pages[tab].data());
        QCOMPARE(pages[tab]->window(), manager.windowWidget(layout.window));
        QVERIFY(!recorder.everParkedIn(pages[tab], QStringLiteral("WindowSystemStorage")));
    }
    for (const auto tab : layout.secondTabs) QCOMPARE(recorder.changes(pages[tab]), 0);
}

void ViewReuseTest::crossWindowMoveReparentsOnlyTheMovedTab()
{
    WindowManager manager;
    const auto source = createTwoGroups(manager, manager.createWindow());
    const auto targetWindow = manager.createWindow();
    const auto targetTab = add(manager, targetWindow, "c0");
    const auto targetGroup = manager.model().groupForTab(targetTab);
    const auto targetOther = add(manager, targetWindow, "c1", targetGroup);
    settle();
    const auto moved = source.firstTabs[1];
    const auto stayed = source.firstTabs.mid(0, 1) + source.firstTabs.mid(2) + source.secondTabs
        + QVector<TabId>{targetTab, targetOther};
    QHash<TabId, QPointer<QWidget>> pages;
    QHash<PaneId, QPointer<QWidget>> contents;
    ParentRecorder recorder;
    for (const auto tab : stayed + QVector<TabId>{moved}) {
        pages.insert(tab, pageOf(manager, tab));
        QVERIFY(pages[tab]);
        recorder.watch(pages[tab]);
        const auto pane = activePane(manager, tab);
        contents.insert(pane, manager.paneWidget(pane));
        recorder.watch(contents[pane]);
    }
    const QPointer<QWidget> unchangedStrip = stripOf(manager, source.window, source.second);
    QVERIFY(unchangedStrip);
    QVERIFY(manager.moveTab(moved, targetGroup, Placement::Center));
    settle();
    QCOMPARE(stripOf(manager, source.window, source.second), unchangedStrip.data());
    for (const auto tab : source.secondTabs) QCOMPARE(recorder.changes(pages[tab]), 0);
    QCOMPARE(manager.model().group(manager.model().groupForTab(moved))->windowId, targetWindow);
    QVERIFY(pages[moved]);
    QCOMPARE(pageOf(manager, moved), pages[moved].data());
    QCOMPARE(pages[moved]->window(), manager.windowWidget(targetWindow));
    QVERIFY(recorder.changes(pages[moved]) > 0);
    for (const auto tab : stayed) {
        QVERIFY(pages[tab]);
        QCOMPARE(pageOf(manager, tab), pages[tab].data());
        QVERIFY(!recorder.everParkedIn(pages[tab], QStringLiteral("WindowSystemStorage")));
    }
    // Pane content stays in its frame; only tab views move.
    for (auto it = contents.cbegin(); it != contents.cend(); ++it) {
        QVERIFY(it.value());
        QCOMPARE(manager.paneWidget(it.key()), it.value().data());
        QCOMPARE(recorder.changes(it.value()), 0);
    }
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

void ViewReuseTest::deferredRootDeletionKeepsCarriedPages()
{
    WindowManager manager;
    const auto layout = createTwoGroups(manager, manager.createWindow());
    settle();
    QHash<TabId, QPointer<QWidget>> pages;
    QHash<TabId, QPointer<QWidget>> contents;
    for (const auto tab : layout.firstTabs + layout.secondTabs) {
        pages.insert(tab, pageOf(manager, tab));
        contents.insert(tab, manager.paneWidget(activePane(manager, tab)));
    }
    const auto verify = [&](const QVector<TabId>& tabs, const char* step) {
        // Run the old root's deferred deletion before anything else.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        for (const auto tab : tabs) {
            QVERIFY2(pages[tab] && contents[tab], step);
            QVERIFY2(manager.paneWidget(activePane(manager, tab)) == contents[tab], step);
            QVERIFY2(pages[tab]->isAncestorOf(contents[tab]), step);
        }
    };
    QVERIFY(manager.closeTab(layout.firstTabs[0]));
    verify(layout.firstTabs.mid(1) + layout.secondTabs, "close");
    // An auto-hide tab has no group on the surface; its carried view must
    // still survive the old root.
    QVERIFY(manager.moveTabToEdgeBar(layout.firstTabs[1], layout.window, Edge::Left));
    verify(layout.firstTabs.mid(1) + layout.secondTabs, "edge");
    QVERIFY(manager.activateTab(layout.firstTabs[1]));
    settle();
    QVERIFY(pages[layout.firstTabs[1]]);
    QCOMPARE(pageOf(manager, layout.firstTabs[1]), pages[layout.firstTabs[1]].data());
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

void ViewReuseTest::reusedStripFollowsMetadataAndTheme()
{
    WindowManager manager;
    const auto layout = createTwoGroups(manager, manager.createWindow());
    settle();
    const auto labelOf = [&](GroupId group, int index) {
        auto* strip = stripOf(manager, layout.window, group);
        auto* bar = strip ? strip->findChild<TabBar*>() : nullptr;
        return bar ? bar->tabText(index) : QString();
    };
    QPointer<QWidget> strip = stripOf(manager, layout.window, layout.second);
    // Renaming a tab rebuilds only its own group's strip.
    QVERIFY(manager.renameTab(layout.secondTabs[1], "renamed"));
    settle();
    QCOMPARE(labelOf(layout.second, 1), QStringLiteral("renamed"));
    QVERIFY(stripOf(manager, layout.window, layout.second) != strip.data());
    strip = stripOf(manager, layout.window, layout.first);
    QVERIFY(manager.closeTab(layout.secondTabs[2]));
    settle();
    QCOMPARE(stripOf(manager, layout.window, layout.first), strip.data());
    // A new theme reaches every strip at the next refresh.
    auto theme = manager.theme();
    theme.metrics.iconSize += 4;
    manager.setTheme(theme);
    QVERIFY(manager.closeTab(layout.secondTabs[1]));
    settle();
    QVERIFY(stripOf(manager, layout.window, layout.first) != strip.data());
    auto* bar = stripOf(manager, layout.window, layout.first)->findChild<TabBar*>();
    QCOMPARE(bar->iconSize(), QSize(theme.metrics.iconSize, theme.metrics.iconSize));
}

void ViewReuseTest::mutationFromSplitterLayoutKeepsReusedStrip_data()
{
    QTest::addColumn<bool>("command");
    QTest::addColumn<bool>("removeGroup");
    for (const bool command : {true, false})
        for (const bool removeGroup : {false, true})
            QTest::newRow(qPrintable(QStringLiteral("%1-%2").arg(command ? "ratio-command" : "splitter-resize",
                removeGroup ? "remove-group" : "close-tab"))) << command << removeGroup;
}
void ViewReuseTest::mutationFromSplitterLayoutKeepsReusedStrip()
{
    QFETCH(bool, command);
    QFETCH(bool, removeGroup);
    WindowManager manager;
    const auto window = manager.createWindow();
    auto* hook = new ResizeHook("hook");
    const QPointer<QWidget> content(hook);
    const auto left = manager.addTab(window, hook, {"left", "test.text", {}});
    const auto right = add(manager, window, "right");
    QVERIFY(manager.moveTab(right, manager.model().groupForTab(left), Placement::Right));
    const auto extra = add(manager, window, "extra", manager.model().groupForTab(right));
    if (removeGroup) QVERIFY(manager.closeTab(right));
    QVERIFY(manager.activateTab(left));
    manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    settle();
    const auto leftGroup = manager.model().groupForTab(left);
    const QPointer<QWidget> leftStrip = stripOf(manager, window, leftGroup);
    QVERIFY(leftStrip);
    QVERIFY(hook->isVisible());
    // The central splitter resizes its first child, the left strip, whose
    // content closes a tab of the right group before the splitter reaches
    // its second child. Closing the group's last tab changes the splitter
    // structure, so the unchanged left strip moves to a new tree in the
    // middle of that layout pass; otherwise the right strip is replaced in
    // place.
    bool fired = false;
    hook->resized = [&] {
        if (fired) return;
        fired = true;
        QVERIFY(manager.closeTab(extra));
    };
    const auto node = manager.model().window(window)->central->id;
    if (command) {
        QVERIFY(manager.execute("layout.setRatio", {{"node", node}, {"ratio", .3}}, window));
    } else {
        QPointer<QSplitter> central;
        for (auto* splitter : manager.windowWidget(window)->findChildren<QSplitter*>())
            if (splitter->property("splitNodeId").toUuid() == node) central = splitter;
        QVERIFY(central);
        // Qt delivers the resize; the splitter lays out its children in it.
        central->resize(central->width() - 80, central->height());
    }
    QVERIFY(fired);
    QVERIFY(!manager.model().tab(extra));
    QCOMPARE(manager.model().window(window)->central->isLeaf(), removeGroup);
    settle();
    QVERIFY(content && leftStrip);
    QCOMPARE(stripOf(manager, window, leftGroup), leftStrip.data());
    QCOMPARE(manager.paneWidget(activePane(manager, left)), content.data());
    QVERIFY(content->isVisible());
    QVERIFY(manager.model().validate());
}

QTEST_MAIN(ViewReuseTest)
#include "view_reuse_test.moc"
