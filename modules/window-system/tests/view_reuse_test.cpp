// SPDX-License-Identifier: MIT
#include <window_system/window_manager.hpp>

#include <QCoreApplication>
#include <QLabel>
#include <QPointer>
#include <QSplitter>
#include <QTest>

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
} // namespace

class ViewReuseTest final : public QObject {
    Q_OBJECT

private slots:
    void cleanup() { settle(); }
    void unrelatedTabsKeepTheirViewWidgets();
    void changedTreeRebuildsOnlyThatTab();
    void themeMetricChangeRebuilds();
    void tabViewsStayInTheirWindow();
    void crossWindowMoveReparentsOnlyTheMovedTab();
    void deferredRootDeletionKeepsCarriedPages();
    void mountedRebuildIsVisibleImmediately();
};

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
    QVERIFY(manager.closeTab(layout.firstTabs[0]));
    settle();
    for (const auto tab : remaining) {
        QVERIFY(pages[tab]);
        QCOMPARE(pageOf(manager, tab), pages[tab].data());
        QCOMPARE(pages[tab]->window(), manager.windowWidget(layout.window));
        QVERIFY(!recorder.everParkedIn(pages[tab], QStringLiteral("WindowSystemStorage")));
    }
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
    QVERIFY(manager.moveTab(moved, targetGroup, Placement::Center));
    settle();
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

QTEST_MAIN(ViewReuseTest)
#include "view_reuse_test.moc"
