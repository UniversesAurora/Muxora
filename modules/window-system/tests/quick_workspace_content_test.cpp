// SPDX-License-Identifier: MIT
// Phase 10.3: content focus, visibility notifications, and stable pane identity.
// Dimensions: command/user focus, tab/window changes, late subscriptions.
#include <window_system/quick_workspace.hpp>
#include <window_system/window_manager.hpp>
#include <QHash>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <memory>

using namespace window_system;

class QuickWorkspaceContentTest final : public QObject {
    Q_OBJECT
private slots:
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
