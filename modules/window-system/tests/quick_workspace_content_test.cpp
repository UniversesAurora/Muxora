// SPDX-License-Identifier: MIT
// Phase 10.3: content focus, visibility notifications, and stable pane identity.
// Dimensions: command/user focus, tab/window changes, late subscriptions,
// shared/per-pane bars, tab/pane ownership, fullscreen and zen.
#include "quick/dock_surface_item.hpp"
#include <window_system/quick_workspace.hpp>
#include <window_system/window_manager.hpp>
#include <QHash>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTest>
#include <memory>

using namespace window_system;

namespace {
class InputBar final : public QQuickItem {
public:
    InputBar() { setImplicitHeight(24); setAcceptedMouseButtons(Qt::LeftButton); setFocusPolicy(Qt::StrongFocus); }
    int presses = 0;
protected:
    void mousePressEvent(QMouseEvent* event) override { ++presses; event->accept(); forceActiveFocus(Qt::MouseFocusReason); }
};

QRectF area(const QQuickItem* item) { return item->mapRectToScene(item->boundingRect()); }
}

class QuickWorkspaceContentTest final : public QObject {
    Q_OBJECT
private slots:
    void liveEditorsSurviveWindowAndPaneTransfers_data()
    {
        QTest::addColumn<int>("route");
        QTest::newRow("detach-promote-demote-redock") << -1;
        QTest::newRow("extract-pane") << -2;
        for (const auto edge : {Edge::Left, Edge::Right, Edge::Top, Edge::Bottom})
            QTest::newRow(qPrintable(QStringLiteral("edge-%1").arg(int(edge)))) << int(edge);
    }

    void liveEditorsSurviveWindowAndPaneTransfers()
    {
        QFETCH(int, route);
        QQmlEngine engine;
        QQmlComponent component(&engine);
        // Like the original editor, the host keeps its selection on focus loss.
        component.setData("import QtQuick; TextEdit { text: 'A live, editable document'; persistentSelection: true }", QUrl());
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        int creations = 0;
        workspace.setContentFactory([&](const TabOptions&, PaneId) {
            ++creations;
            return qobject_cast<QQuickItem*>(component.create());
        });
        const auto root = manager.createWindow();
        const auto staying = manager.addTab(root, {QStringLiteral("Staying")});
        const auto stayingPane = manager.model().tab(staying)->activePane;
        const auto moving = manager.addTab(root, {QStringLiteral("Moving")});
        const auto originalPane = manager.model().tab(moving)->activePane;
        const auto pane = route == -2 ? manager.splitPane(moving, originalPane, Placement::Right) : originalPane;
        QTRY_VERIFY(workspace.content(pane));
        QPointer<QQuickItem> content = workspace.content(pane), stationary = workspace.content(stayingPane);
        QVERIFY(stationary);
        QSignalSpy destroyed(content, &QObject::destroyed);
        QVERIFY(QMetaObject::invokeMethod(content, "select", Q_ARG(int, 2), Q_ARG(int, 8)));
        QCOMPARE(content->property("selectedText").toString(), QStringLiteral("live, "));
        const int initialCreations = creations;
        if (route == -1) {
            for (int iteration = 0; iteration < 20; ++iteration) {
                const auto floating = manager.detachTab(moving);
                QVERIFY(!floating.isNull());
                QTRY_VERIFY(workspace.window(floating));
                QPointer<QQuickWindow> retired = workspace.window(floating);
                QTRY_COMPARE(content->window(), retired.data());
                QVERIFY(manager.promoteWindow(floating));
                QTRY_COMPARE(workspace.window(floating), retired.data());
                QCOMPARE(manager.model().window(floating)->kind, WindowKind::Workbench);
                QVERIFY(manager.demoteWindow(floating, root));
                QTRY_COMPARE(workspace.window(floating), retired.data());
                QVERIFY(manager.execute("window.redock", {}, floating));
                QTRY_COMPARE(content->window(), workspace.window(root));
                QTRY_VERIFY(!retired);
                QCOMPARE(workspace.content(pane), content.data());
                QCOMPARE(content->property("selectedText").toString(), QStringLiteral("live, "));
                QCOMPARE(creations, initialCreations);
            }
        } else if (route == -2) {
            const auto extracted = manager.extractPane(moving, pane);
            QVERIFY(!extracted.isNull());
            const auto destination = manager.model().group(manager.model().groupForTab(extracted))->windowId;
            QTRY_VERIFY(workspace.window(destination));
            QTRY_COMPARE(content->window(), workspace.window(destination));
            QVERIFY(manager.closeTab(moving));
            QTRY_VERIFY(!workspace.content(originalPane));
            QVERIFY(content);
            QCOMPARE(workspace.content(pane), content.data());
            QCOMPARE(content->property("text").toString(), QStringLiteral("A live, editable document"));
            QCOMPARE(content->property("selectedText").toString(), QStringLiteral("live, "));
            QCOMPARE(destroyed.count(), 0);
            QVERIFY(manager.requestCloseWindow(destination));
        } else {
            QVERIFY(manager.moveTabToEdge(moving, Edge(route)));
            const auto group = manager.model().groupForTab(moving);
            QVERIFY(manager.activateTab(moving));
            QTRY_VERIFY(content->isVisible());
            QVERIFY(manager.pinGroup(group, true));
            QTRY_COMPARE(content->window(), workspace.window(root));
            QCOMPARE(manager.model().group(group)->location, GroupLocation::Pinned);
            QVERIFY(manager.pinGroup(group, false));
            QVERIFY(manager.moveGroupToCenter(group));
            QTRY_VERIFY(content->isVisible());
            QCOMPARE(manager.model().group(group)->location, GroupLocation::Central);
        }
        if (route != -2) {
            QCOMPARE(workspace.content(pane), content.data());
            QCOMPARE(content->property("text").toString(), QStringLiteral("A live, editable document"));
            QCOMPARE(content->property("selectedText").toString(), QStringLiteral("live, "));
            QCOMPARE(destroyed.count(), 0);
            QVERIFY(manager.closeTab(moving));
        }
        QTRY_VERIFY(!content);
        QCOMPARE(destroyed.count(), 1);
        QCOMPARE(workspace.content(stayingPane), stationary.data());
        QCOMPARE(stationary->property("text").toString(), QStringLiteral("A live, editable document"));
        QCOMPARE(creations, initialCreations);
        QVERIFY(manager.model().validate());
    }

    void editorStateSurvivesCommandsAndRejectedRestore()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQuick; TextEdit { text: 'Keyboard content' }", QUrl());
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        int creations = 0;
        workspace.setContentFactory([&](const TabOptions&, PaneId) {
            ++creations;
            return qobject_cast<QQuickItem*>(component.create());
        });
        const auto window = manager.createWindow();
        const auto tab = manager.addTab(window, {QStringLiteral("Editor")});
        const auto pane = manager.model().tab(tab)->activePane;
        QTRY_VERIFY(workspace.content(pane));
        QPointer<QQuickItem> editor = workspace.content(pane);
        auto* view = workspace.window(window);
        view->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(view));
        editor->forceActiveFocus();
        QTRY_VERIFY(editor->hasActiveFocus());
        int calls = 0;
        TabId commandTab;
        QVERIFY(manager.commands().add({QStringLiteral("test.keyboard"), QStringLiteral("Keyboard action"),
            QStringLiteral("Host"), QKeySequence(QStringLiteral("Ctrl+Alt+F9")), {},
            [&](const CommandContext& context) { ++calls; commandTab = context.tab; return true; }}));
        QTest::keyClick(view, Qt::Key_F9, Qt::ControlModifier | Qt::AltModifier);
        QCOMPARE(calls, 1);
        QCOMPARE(commandTab, tab);
        QCOMPARE(editor->property("text").toString(), QStringLiteral("Keyboard content"));
        const auto before = manager.model().toJson();
        auto malformed = manager.saveLayout();
        malformed.insert("model", QJsonObject{{"schemaVersion", 99}});
        QVERIFY(!manager.restoreLayout(malformed));
        QCoreApplication::sendPostedEvents(&workspace, QEvent::MetaCall);
        QCOMPARE(manager.model().toJson(), before);
        QCOMPARE(workspace.content(pane), editor.data());
        QCOMPARE(creations, 1);
        QCOMPARE(editor->property("text").toString(), QStringLiteral("Keyboard content"));
    }

    void hierarchyConversionKeepsTheExistingBars_data()
    {
        QTest::addColumn<bool>("wholeGroup");
        QTest::addColumn<bool>("paneBars");
        for (const bool group : {false, true}) for (const bool bars : {false, true})
            QTest::newRow(qPrintable(QStringLiteral("%1-%2").arg(group ? "group" : "tab", bars ? "stacked" : "outer"))) << group << bars;
    }

    void hierarchyConversionKeepsTheExistingBars()
    {
        QFETCH(bool, wholeGroup);
        QFETCH(bool, paneBars);
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto window = manager.createWindow();
        const auto target = manager.addTab(window, {QStringLiteral("Target")});
        const auto targetPane = manager.model().tab(target)->activePane;
        TabOptions options{QStringLiteral("Source")};
        options.topBarSharing = options.bottomBarSharing = PaneBarSharing::PerPane;
        const auto source = manager.addTab(window, options);
        QVERIFY(manager.moveTab(source, manager.model().groupForTab(target), Placement::Left));
        const auto sourceGroup = manager.model().groupForTab(source);
        const auto sourcePane = manager.model().tab(source)->activePane;
        QPointer<QQuickItem> extraBar;
        if (wholeGroup) {
            const auto extra = manager.addTab(window, {QStringLiteral("Extra")}, sourceGroup);
            extraBar = new InputBar;
            QVERIFY(workspace.setTabBar(extra, extraBar, true));
        }
        QPointer<InputBar> top = new InputBar, bottom = new InputBar;
        QPointer<InputBar> innerTop, innerBottom;
        QVERIFY(workspace.setTabBar(source, top, true));
        QVERIFY(workspace.setTabBar(source, bottom, false));
        if (paneBars) {
            innerTop = new InputBar; innerBottom = new InputBar;
            QVERIFY(workspace.setPaneBar(sourcePane, innerTop, true));
            QVERIFY(workspace.setPaneBar(sourcePane, innerBottom, false));
        }
        QVERIFY(manager.activateTab(source));
        QTRY_VERIFY(top->window());
        auto* view = workspace.window(window);
        view->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(view));
        top->forceActiveFocus();
        QTRY_VERIFY(top->hasActiveFocus());
        if (wholeGroup) QVERIFY(manager.moveGroupIntoTab(sourceGroup, target, targetPane, Placement::Left));
        else QVERIFY(manager.moveTabIntoTab(source, target, targetPane, Placement::Left));
        QVERIFY(!manager.model().tab(source));
        QTRY_VERIFY(top->isVisible() && bottom->isVisible());
        QTRY_VERIFY(top->hasActiveFocus());
        QCOMPARE(manager.context().pane, sourcePane);
        if (wholeGroup) QTRY_VERIFY(extraBar->isVisible());
        QList<QPointer<InputBar>> bars{top, bottom};
        if (paneBars) { bars.append(innerTop); bars.append(innerBottom); }
        for (const auto& bar : bars) {
            QTRY_VERIFY(bar->isVisible());
            const int presses = bar->presses;
            QTest::mouseClick(view, Qt::LeftButton, {}, area(bar).center().toPoint());
            QCOMPARE(bar->presses, presses + 1);
            QTRY_COMPARE(manager.context().pane, sourcePane);
            QVERIFY(!area(bar).intersects(area(workspace.content(sourcePane))));
        }
        QVERIFY(manager.closeTab(target));
        QTRY_VERIFY(!top && !bottom && !innerTop && !innerBottom && !extraBar);
    }

    void barsFollowTheirSharingPolicy_data()
    {
        QTest::addColumn<bool>("perPane");
        QTest::newRow("shared") << false;
        QTest::newRow("per-pane") << true;
    }

    void barsFollowTheirSharingPolicy()
    {
        QFETCH(bool, perPane);
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto window = manager.createWindow();
        TabOptions options{QStringLiteral("Bars")};
        options.topBarSharing = options.bottomBarSharing = perPane ? PaneBarSharing::PerPane : PaneBarSharing::Shared;
        const auto tab = manager.addTab(window, options);
        const auto left = manager.model().tab(tab)->activePane;
        const auto right = manager.splitPane(tab, left, Placement::Right);
        QTRY_VERIFY(workspace.content(right));
        auto* view = workspace.window(window);
        view->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(view));
        auto* dock = view->contentItem()->findChild<quick::DockSurfaceItem*>();
        QVERIFY(dock);
        const auto minimum = dock->minimumSize();
        auto* tabTop = new InputBar; auto* tabBottom = new InputBar;
        auto* leftTop = new InputBar; auto* leftBottom = new InputBar;
        auto* rightTop = new InputBar; auto* rightBottom = new InputBar;
        QVERIFY(workspace.setTabBar(tab, tabTop, true));
        QVERIFY(workspace.setTabBar(tab, tabBottom, false));
        QVERIFY(workspace.setPaneBar(left, leftTop, true));
        QVERIFY(workspace.setPaneBar(left, leftBottom, false));
        QVERIFY(workspace.setPaneBar(right, rightTop, true));
        QVERIFY(workspace.setPaneBar(right, rightBottom, false));
        QTRY_VERIFY(rightTop->isVisible() && rightBottom->isVisible());
        QTRY_VERIFY(dock->minimumSize().height() > minimum.height());
        QCOMPARE(leftTop->isVisible(), perPane);
        QCOMPARE(leftBottom->isVisible(), perPane);
        QCOMPARE(tabTop->isVisible(), perPane);
        QCOMPARE(tabBottom->isVisible(), perPane);
        for (const auto pane : {left, right}) {
            auto* top = perPane && pane == left ? leftTop : rightTop;
            auto* bottom = perPane && pane == left ? leftBottom : rightBottom;
            QVERIFY(!area(top).intersects(area(workspace.content(pane))));
            QVERIFY(!area(bottom).intersects(area(workspace.content(pane))));
            const QPoint point(qRound(area(workspace.content(pane)).center().x()), qRound(area(top).center().y()));
            const int presses = top->presses;
            QTest::mouseClick(view, Qt::LeftButton, {}, point);
            QCOMPARE(top->presses, presses + 1);
            QTRY_VERIFY(top->hasActiveFocus());
        }
        const int beforeGrowth = dock->minimumSize().height();
        rightBottom->setImplicitHeight(rightBottom->implicitHeight() + 40);
        QTRY_VERIFY(dock->minimumSize().height() > beforeGrowth);
        if (!perPane) {
            QVERIFY(manager.focusPane(tab, left));
            QTRY_VERIFY(leftTop->isVisible() && !rightTop->isVisible());
            QVERIFY(workspace.setPaneBar(left, nullptr, true));
            QTRY_VERIFY(tabTop->isVisible()); // Fall back to the tab's own bar.
        } else {
            QVERIFY(manager.focusPane(tab, left));
            QVERIFY(manager.setViewMode(window, ViewMode::PaneFullscreen));
            QTRY_VERIFY(leftTop->isVisible() && !rightTop->isVisible());
            QVERIFY(!workspace.content(right)->isVisible());
            QVERIFY(manager.setViewMode(window, ViewMode::Normal));
            QTRY_VERIFY(rightTop->isVisible());
            QVERIFY(manager.setZenMode(window, true));
            QTRY_VERIFY(!tabTop->isVisible() && !tabBottom->isVisible() && !leftTop->isVisible()
                && !leftBottom->isVisible() && !rightTop->isVisible() && !rightBottom->isVisible());
            QVERIFY(manager.setZenMode(window, false));
            QTRY_VERIFY(tabTop->isVisible() && leftTop->isVisible() && rightTop->isVisible());
        }
    }

    // Scripts set text bars through the manager; the Quick page shows them
    // above or below the content, replaces and removes them.
    void textBarsShowAroundTheContent()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto window = manager.createWindow();
        const auto tab = manager.addTab(window, TabOptions{QStringLiteral("Text bars")});
        const auto pane = manager.model().tab(tab)->activePane;
        QTRY_VERIFY(workspace.content(pane));
        auto* view = workspace.window(window);
        QVERIFY(QTest::qWaitForWindowExposed(view));
        const auto bar = [&](const QString& text) -> QQuickItem* {
            for (QList<QQuickItem*> items{view->contentItem()}; !items.isEmpty();) {
                auto* item = items.takeFirst();
                if (item->property("text").toString() == text && item->isVisible() && item->height() > 0) return item;
                items += item->childItems();
            }
            return nullptr;
        };
        QVERIFY(manager.setTabTextBar(tab, true, TextBar{QStringLiteral("Tab & status"), QStringLiteral("Tip")}));
        QVERIFY(manager.setPaneTextBar(pane, false, TextBar{QStringLiteral("Pane status"), {}}));
        QTRY_VERIFY(bar(QStringLiteral("Tab & status")) && bar(QStringLiteral("Pane status")));
        const auto content = area(workspace.content(pane));
        QVERIFY(area(bar(QStringLiteral("Tab & status"))).bottom() <= content.top());
        QVERIFY(area(bar(QStringLiteral("Pane status"))).top() >= content.bottom());
        QVERIFY(manager.setTabTextBar(tab, true, TextBar{QStringLiteral("Replaced"), {}}));
        QTRY_VERIFY(bar(QStringLiteral("Replaced")) && !bar(QStringLiteral("Tab & status")));
        QVERIFY(manager.setPaneTextBar(pane, false, std::nullopt));
        QTRY_VERIFY(!bar(QStringLiteral("Pane status")));
        QVERIFY(!manager.setTabTextBar(TabId::createUuid(), true, TextBar{QStringLiteral("Lost"), {}}));
    }

    void barOwnershipFollowsTabsAndPanesRatherThanPages()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto first = manager.createWindow();
        manager.addTab(first, {QStringLiteral("Staying")});
        TabOptions options{QStringLiteral("Moving")};
        options.topBarSharing = PaneBarSharing::PerPane;
        const auto moving = manager.addTab(first, options);
        const auto left = manager.model().tab(moving)->activePane;
        const auto right = manager.splitPane(moving, left, Placement::Right);
        const auto second = manager.createWindow();
        const auto other = manager.addTab(second, {QStringLiteral("Other")});
        QTRY_VERIFY(workspace.content(right));
        QPointer<QQuickItem> tabBar = new InputBar, leftBar = new InputBar, rightBar = new InputBar;
        QVERIFY(workspace.setTabBar(moving, tabBar, true));
        QVERIFY(workspace.setPaneBar(left, leftBar, true));
        QVERIFY(workspace.setPaneBar(right, rightBar, true));
        QVERIFY(!workspace.setPaneBar(left, tabBar, false));
        QVERIFY(!workspace.setTabBar(other, workspace.content(right), true));
        auto rejected = std::make_unique<InputBar>();
        QVERIFY(!workspace.setTabBar(QUuid::createUuid(), rejected.get(), true));
        QVERIFY(!rejected->parent());
        auto* retiring = new InputBar;
        QPointer<QQuickItem> successor = new InputBar;
        connect(retiring, &QObject::destroyed, &workspace, [&] { QVERIFY(workspace.setTabBar(moving, successor, false)); });
        QVERIFY(workspace.setTabBar(moving, retiring, false));
        delete retiring;
        QTRY_VERIFY(successor->isVisible());
        const auto owner = tabBar->parent();
        QVERIFY(owner);
        QVERIFY(manager.moveTabToWindow(moving, second));
        QVERIFY(manager.activateTab(moving));
        QTRY_COMPARE(tabBar->window(), workspace.window(second));
        QTRY_COMPARE(rightBar->window(), workspace.window(second));
        QCOMPARE(tabBar->parent(), owner);
        QVERIFY(manager.movePane(moving, right, other, manager.model().tab(other)->activePane, Placement::Right));
        QVERIFY(manager.activateTab(other));
        QTRY_VERIFY(rightBar->isVisible());
        QVERIFY(manager.closePane(other, right));
        QTRY_VERIFY(!rightBar);
        QVERIFY(tabBar && leftBar);
        QVERIFY(manager.closeTab(moving));
        QTRY_VERIFY(!tabBar && !leftBar && !successor);
    }

    void focusTracksThePaneAndPreservesItsEditorAcrossWindows()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        QHash<PaneId, QPointer<QQuickItem>> editors;
        workspace.setContentFactory([&](const TabOptions&, PaneId pane) {
            auto* content = new QQuickItem;
            content->setFlag(QQuickItem::ItemIsFocusScope);
            auto* editor = new QQuickItem(content);
            editor->setFocusPolicy(Qt::StrongFocus);
            editors.insert(pane, editor);
            return content;
        });
        const auto first = manager.createWindow();
        const auto tab = manager.addTab(first, {QStringLiteral("Editor")});
        const auto left = manager.model().tab(tab)->activePane;
        const auto right = manager.splitPane(tab, left, Placement::Right);
        const auto second = manager.createWindow();
        manager.addTab(second, {QStringLiteral("Other")});
        QTRY_VERIFY(workspace.content(right));
        auto* window = workspace.window(first);
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));
        editors[left]->forceActiveFocus();
        QTRY_COMPARE(manager.context().pane, left);
        editors[right]->forceActiveFocus();
        QTRY_COMPARE(manager.context().pane, right);
        QVERIFY(manager.focusPane(tab, left));
        QTRY_VERIFY(editors[left]->hasActiveFocus());
        const QPointer<QQuickItem> editor = editors[left];
        QVERIFY(manager.moveTabToWindow(tab, second));
        QTRY_COMPARE(workspace.content(left)->window(), workspace.window(second));
        QTRY_VERIFY(editor->hasFocus());
        QCOMPARE(editors.value(left), editor);
        QCOMPARE(manager.context().pane, left);
        workspace.window(second)->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(workspace.window(second)));
        QTRY_VERIFY(editor->hasActiveFocus());

        bool ran = false;
        manager.commands().add({QStringLiteral("test.contentChord"), QStringLiteral("Content chord"), QStringLiteral("test"),
            QKeySequence(QStringLiteral("Ctrl+K, Ctrl+T")), {}, [&](const CommandContext&) { ran = true; return true; }});
        QTest::keyClick(workspace.window(second), Qt::Key_K, Qt::ControlModifier);
        QVERIFY(manager.renameTab(tab, QStringLiteral("Renamed")));
        QTRY_VERIFY(workspace.window(second)->title().contains(QStringLiteral("Renamed")));
        QTest::keyClick(workspace.window(second), Qt::Key_T, Qt::ControlModifier);
        QVERIFY(ran); // Layout-only synchronization must not cancel a chord.
    }

    void visibilityReportsActualContentAndResendsALateSnapshot()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QHash<PaneId, bool> visibility;
        const auto observe = [&](const QVector<ContentVisibilityChange>& changes) {
            for (const auto& change : changes) visibility.insert(change.pane, change.visible);
        };
        manager.setContentVisibilityHandler(observe);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto window = manager.createWindow();
        const auto first = manager.addTab(window, {QStringLiteral("First")});
        const auto second = manager.addTab(window, {QStringLiteral("Second")});
        const auto firstPane = manager.model().tab(first)->activePane;
        const auto secondPane = manager.model().tab(second)->activePane;
        QTRY_VERIFY(workspace.content(secondPane));
        QVERIFY(QTest::qWaitForWindowExposed(workspace.window(window)));
        QTRY_COMPARE(visibility.size(), 2);
        QVERIFY(!visibility.value(firstPane));
        QVERIFY(visibility.value(secondPane));
        QVERIFY(manager.activateTab(first));
        QTRY_VERIFY(visibility.value(firstPane) && !visibility.value(secondPane));
        workspace.window(window)->hide();
        QTRY_VERIFY(!visibility.value(firstPane));
        workspace.window(window)->show();
        QTRY_VERIFY(visibility.value(firstPane));
        // Replacing the observer must receive all panes even without a delta.
        visibility.clear();
        manager.setContentVisibilityHandler(observe);
        QTRY_COMPARE(visibility.size(), 2);
        QVERIFY(visibility.value(firstPane));
        QVERIFY(!visibility.value(secondPane));
        QVERIFY(manager.closeTab(first));
        QTRY_VERIFY(!visibility.value(firstPane));
        QTRY_VERIFY(visibility.value(secondPane));
        manager.setContentVisibilityHandler({});
    }

    void aVisibilityObserverCanReleaseTheWorkspaceDuringAWindowMove()
    {
        WindowManager manager(WindowManager::Presentation::External);
        auto workspace = std::make_unique<QuickWorkspace>(manager);
        workspace->setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto first = manager.createWindow();
        const auto moving = manager.addTab(first, {QStringLiteral("Moving")});
        const auto pane = manager.model().tab(moving)->activePane;
        const auto second = manager.createWindow();
        manager.addTab(second, {QStringLiteral("Other")});
        QVERIFY(manager.activateTab(moving));
        QTRY_VERIFY(workspace->content(pane));
        QPointer<QQuickItem> content = workspace->content(pane);
        QVERIFY(QTest::qWaitForWindowExposed(workspace->window(first)));
        bool visible = false, closing = false, reportedBeforeMoving = false;
        manager.setContentVisibilityHandler([&](const QVector<ContentVisibilityChange>& changes) {
            for (const auto& change : changes) if (change.pane == pane) {
                visible = change.visible;
                if (closing && !visible) {
                    reportedBeforeMoving = content && content->window() == workspace->window(first);
                    workspace.reset();
                    return;
                }
            }
        });
        QTRY_VERIFY(visible);
        closing = true;
        QVERIFY(manager.moveTabToWindow(moving, second));
        QVERIFY(manager.activateTab(moving)); // Mount it, rather than leave it as a lazy background page.
        QTRY_VERIFY(!workspace);
        QVERIFY(reportedBeforeMoving);
        QVERIFY(!content);
        manager.setContentVisibilityHandler({});
    }
};

QTEST_MAIN(QuickWorkspaceContentTest)
#include "quick_workspace_content_test.moc"
