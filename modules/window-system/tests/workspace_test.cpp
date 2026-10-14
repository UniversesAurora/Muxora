#include <window_system/window_manager.hpp>

#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QJsonArray>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPointer>
#include <QSet>
#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace window_system;

namespace {

TabOptions editorOptions(const QString& title, const QString& text)
{
    return {title, QStringLiteral("test.editor"), {{QStringLiteral("text"), text}}};
}

QPlainTextEdit* editor(const QString& text)
{
    auto* result = new QPlainTextEdit;
    result->setPlainText(text);
    return result;
}

void deliverDeferredDeletes()
{
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}

QSet<TabId> tabSet(const QVector<TabId>& tabs)
{
    return QSet<TabId>(tabs.cbegin(), tabs.cend());
}

} // namespace

class WorkspaceTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();
    void failedAdoptionLeavesCallerOwnershipIntact();
    void detachPromoteDemoteAndRedockPreserveLiveContent();
    void optionalBarsTravelWithTheTabAndAreReleasedOnClose();
    void splittingAndExtractingMovesExistingPaneWidgets();
    void refusedFamilyCloseLeavesEveryWindowAndContentAlive();
    void closeWindowRejectsContentAddedDuringConfirmation_data();
    void closeWindowRejectsContentAddedDuringConfirmation();
    void closeRejectsPanesAddedDuringConfirmation_data();
    void closeRejectsPanesAddedDuringConfirmation();
    void recursiveCloseCannotBypassConfirmation();
    void closingFloatingWindowOnlyReleasesItsOwnContent();
    void closingAnUnrelatedPaneCannotCloseTheTab();
    void allCommandsCanBeBoundAndDefaultsHaveNoCollisions();
    void customCommandsReceiveArgumentsAndRespectTheirPrecondition();
    void invalidTabArgumentCannotChangeEdgeLayout();
    void aBoundCommandExecutesFromContentKeyboardInput();
    void invalidRestorePreservesTheCurrentModelAndWidgets();
    void failedRestoreReleasesPreparedContentsAndPreservesCurrentContents();
    void restoreFactoriesMayDestroyTheirManager_data();
    void restoreFactoriesMayDestroyTheirManager();
    void restoreFactoryMayReplaceItselfWithoutDestroyingItsCaptures();
    void validRestoreCreatesContentsForExistingStableIds();
    void restoredWindowTitlesSurviveFreshAndExistingHosts();
    void emptyRootWithFloatingContentSurvivesAutomaticClosePolicy();
    void repeatedCrossWindowMovesDoNotRecreateOrDestroyContent();
    void edgePinAndReturnToCenterPreserveTheSameWidget();
    void bulkCloseCommandsRespectTheSelectedTabAndGroup_data();
    void bulkCloseCommandsRespectTheSelectedTabAndGroup();
    void refusedBulkCloseIsAtomicAndDoesNotCloseNewTabs();
    void closeAllApprovesOnceAndSavesTheCompleteWorkspace();
    void refusedCloseAllPreservesEveryFamily();
    void closeAllRejectsChangesDuringCallbacks_data();
    void closeAllRejectsChangesDuringCallbacks();
    void recursiveCloseAllCannotBypassConfirmation();
    void closeAllRejectsContentReplacementWithTheSameIds();
    void closeAllHandlesManagerDestruction_data();
    void closeAllHandlesManagerDestruction();
    void closeAllFromLayoutNotificationEmitsClosedOnce();
    void closeAllAllowsUnchangedFocusAfterApproval();
};

void WorkspaceTest::initTestCase()
{
    qApp->setQuitOnLastWindowClosed(false);
}

void WorkspaceTest::cleanup()
{
    deliverDeferredDeletes();
}

void WorkspaceTest::failedAdoptionLeavesCallerOwnershipIntact()
{
    WindowManager manager;
    QWidget callerParent;
    auto* content = new QPlainTextEdit(&callerParent);
    content->setPlainText("Caller-owned");
    QPointer<QWidget> retained = content;
    QVERIFY(manager.addTab(QUuid::createUuid(), content, editorOptions("Invalid", "Caller-owned")).isNull());
    QCOMPARE(content->parentWidget(), &callerParent);
    QVERIFY(!content->property("windowSystemManaged").toBool());
    QVERIFY(retained);
    const auto root = manager.createWindow();
    const auto tab = manager.addTab(root, content, editorOptions("Valid", "Caller-owned"));
    QVERIFY(!tab.isNull());
    const auto pane = manager.model().tab(tab)->activePane;
    QCOMPARE(manager.paneWidget(pane), content);
    QVERIFY(content->parentWidget() != &callerParent);
    QVERIFY(manager.addTab(root, content, editorOptions("Duplicate", "Caller-owned")).isNull());
    QCOMPARE(manager.model().tabs().size(), 1);

    std::unique_ptr<QWidget> rejected(editor("Not adopted"));
    QVERIFY(manager.splitPane(tab, QUuid::createUuid(), rejected.get(), Placement::Right).isNull());
    QVERIFY(!rejected->parentWidget());
    QVERIFY(!rejected->property("windowSystemManaged").toBool());
    QVERIFY(manager.closeTab(tab));
    deliverDeferredDeletes();
    QVERIFY(retained.isNull());
}

void WorkspaceTest::detachPromoteDemoteAndRedockPreserveLiveContent()
{
    WindowManager manager;
    const auto root = manager.createWindow();
    auto* content = editor("A live, editable document");
    const auto tab = manager.addTab(root, content, editorOptions("Live", content->toPlainText()));
    const auto pane = manager.model().tab(tab)->activePane;
    auto cursor = content->textCursor();
    cursor.setPosition(2);
    cursor.setPosition(8, QTextCursor::KeepAnchor);
    content->setTextCursor(cursor);
    QSignalSpy destroyed(content, &QObject::destroyed);

    const auto floating = manager.detachTab(tab);
    QVERIFY(!floating.isNull());
    QCOMPARE(manager.model().window(floating)->kind, WindowKind::Floating);
    QCOMPARE(manager.paneWidget(pane), content);
    QCOMPARE(content->window(), manager.windowWidget(floating));
    QVERIFY(manager.promoteWindow(floating));
    QCOMPARE(manager.model().window(floating)->kind, WindowKind::Workbench);
    QVERIFY(manager.demoteWindow(floating, root));
    QCOMPARE(manager.model().window(floating)->familyId, manager.model().window(root)->familyId);
    QVERIFY(manager.execute("window.redock", {}, floating));
    deliverDeferredDeletes();
    QCOMPARE(manager.paneWidget(pane), content);
    QCOMPARE(content->window(), manager.windowWidget(root));
    QCOMPARE(content->toPlainText(), QStringLiteral("A live, editable document"));
    QCOMPARE(content->textCursor().selectedText(), QStringLiteral("live, "));
    QCOMPARE(destroyed.count(), 0);
    QVERIFY(!manager.model().window(floating));
}

void WorkspaceTest::optionalBarsTravelWithTheTabAndAreReleasedOnClose()
{
    WindowManager manager;
    const auto root = manager.createWindow();
    const auto tab = manager.addTab(root, editor("Content"), editorOptions("Bars", "Content"));
    QPointer<QLabel> top = new QLabel("Top navigation");
    QPointer<QLabel> bottom = new QLabel("Local status");
    QVERIFY(manager.setTabTopBar(tab, top));
    QVERIFY(manager.setTabBottomBar(tab, bottom));
    const auto floating = manager.detachTab(tab);
    QVERIFY(!floating.isNull());
    QCOMPARE(top->window(), manager.windowWidget(floating));
    QCOMPARE(bottom->window(), manager.windowWidget(floating));
    QCOMPARE(top->text(), QStringLiteral("Top navigation"));
    QCOMPARE(bottom->text(), QStringLiteral("Local status"));
    QVERIFY(manager.execute("window.redock", {}, floating));
    QCOMPARE(top->window(), manager.windowWidget(root));
    QCOMPARE(bottom->window(), manager.windowWidget(root));
    QVERIFY(manager.setTabTopBar(tab, nullptr));
    QVERIFY(top.isNull());
    QVERIFY(bottom);
    QVERIFY(manager.closeTab(tab));
    deliverDeferredDeletes();
    QVERIFY(bottom.isNull());
}

void WorkspaceTest::splittingAndExtractingMovesExistingPaneWidgets()
{
    WindowManager manager;
    const auto root = manager.createWindow();
    auto* first = editor("First pane");
    auto* second = editor("Second pane");
    const auto tab = manager.addTab(root, first, editorOptions("Split", "First pane"));
    const auto firstPane = manager.model().tab(tab)->activePane;
    const auto secondPane = manager.splitPane(tab, firstPane, second, Placement::Right);
    QVERIFY(!secondPane.isNull());
    QPointer<QWidget> firstPointer = first;
    QPointer<QWidget> secondPointer = second;
    const auto extracted = manager.extractPane(tab, secondPane);
    QVERIFY(!extracted.isNull());
    QVERIFY(extracted != tab);
    const auto extractedWindow = manager.model().group(manager.model().groupForTab(extracted))->windowId;
    QCOMPARE(manager.paneWidget(firstPane), first);
    QCOMPARE(manager.paneWidget(secondPane), second);
    QCOMPARE(manager.model().panes(tab), (QVector<PaneId>{firstPane}));
    QCOMPARE(manager.model().panes(extracted), (QVector<PaneId>{secondPane}));
    QCOMPARE(second->window(), manager.windowWidget(extractedWindow));
    QVERIFY(manager.closeTab(tab));
    deliverDeferredDeletes();
    QVERIFY(firstPointer.isNull());
    QVERIFY(secondPointer);
    QCOMPARE(second->toPlainText(), QStringLiteral("Second pane"));
    QVERIFY(manager.requestCloseWindow(extractedWindow));
    deliverDeferredDeletes();
    QVERIFY(secondPointer.isNull());
}

void WorkspaceTest::refusedFamilyCloseLeavesEveryWindowAndContentAlive()
{
    WindowManager manager;
    const auto root = manager.createWindow();
    QPointer<QWidget> first = editor("Root document");
    QPointer<QWidget> second = editor("Floating document");
    const auto rootTab = manager.addTab(root, first, editorOptions("Root", "Root document"));
    const auto floatingTab = manager.addTab(root, second, editorOptions("Float", "Floating document"));
    const auto floating = manager.detachTab(floatingTab);
    QPointer<QWidget> rootWindow = manager.windowWidget(root);
    QPointer<QWidget> floatingWindow = manager.windowWidget(floating);
    QVector<TabId> permissionRequest;
    int permissionCalls = 0;
    manager.setCloseGuard([&](const QVector<TabId>& tabs) {
        permissionRequest = tabs;
        ++permissionCalls;
        return false;
    });
    const auto before = manager.model().toJson();
    QVERIFY(!manager.requestCloseWindow(root));
    QCOMPARE(permissionCalls, 1);
    QCOMPARE(tabSet(permissionRequest), (QSet<TabId>{rootTab, floatingTab}));
    QCOMPARE(manager.model().toJson(), before);
    deliverDeferredDeletes();
    QVERIFY(first && second && rootWindow && floatingWindow);
    manager.setCloseGuard([](const QVector<TabId>&) { return true; });
    QVERIFY(manager.requestCloseWindow(root));
    deliverDeferredDeletes();
    QVERIFY(first.isNull() && second.isNull() && rootWindow.isNull() && floatingWindow.isNull());
}

void WorkspaceTest::closeWindowRejectsContentAddedDuringConfirmation_data()
{
    QTest::addColumn<bool>("newFloatingWindow");
    QTest::newRow("new-tab") << false;
    QTest::newRow("new-family-window") << true;
}

void WorkspaceTest::closeWindowRejectsContentAddedDuringConfirmation()
{
    QFETCH(bool, newFloatingWindow);
    WindowManager manager;
    const auto root = manager.createWindow();
    QPointer<QWidget> original = editor("Original");
    const auto originalTab = manager.addTab(root, original, editorOptions("Original", "Original"));
    QPointer<QWidget> added;
    TabId addedTab;
    WindowId addedWindow;
    QVector<TabId> approvedTabs;
    manager.setCloseGuard([&](const QVector<TabId>& tabs) {
        approvedTabs = tabs;
        addedWindow = newFloatingWindow ? manager.createWindow(WindowKind::Floating, {}, root) : root;
        added = editor("Added during confirmation");
        addedTab = manager.addTab(addedWindow, added, editorOptions("Added", "Added during confirmation"));
        return true;
    });
    QVERIFY(!manager.requestCloseWindow(root));
    QCOMPARE(approvedTabs, (QVector<TabId>{originalTab}));
    QVERIFY(!addedTab.isNull());
    deliverDeferredDeletes();
    QVERIFY(original && added);
    QVERIFY(manager.model().window(root));
    QVERIFY(manager.model().window(addedWindow));
    QVERIFY(manager.model().tab(originalTab));
    QVERIFY(manager.model().tab(addedTab));
    QVERIFY(manager.model().validate().ok);

    manager.setCloseGuard([](const QVector<TabId>&) { return true; });
    QVERIFY(manager.requestCloseWindow(root));
    deliverDeferredDeletes();
    QVERIFY(original.isNull() && added.isNull());
    QVERIFY(manager.model().windows().isEmpty());
}

void WorkspaceTest::closeRejectsPanesAddedDuringConfirmation_data()
{
    QTest::addColumn<bool>("closeOnePane");
    QTest::newRow("close-tab") << false;
    QTest::newRow("close-pane") << true;
}

void WorkspaceTest::closeRejectsPanesAddedDuringConfirmation()
{
    QFETCH(bool, closeOnePane);
    WindowManager manager;
    const auto root = manager.createWindow();
    QPointer<QWidget> original = editor("Original pane");
    const auto tab = manager.addTab(root, original, editorOptions("Original", "Original pane"));
    const auto originalPane = manager.model().tab(tab)->activePane;
    if (closeOnePane)
        QVERIFY(!manager.splitPane(tab, originalPane, editor("Existing second pane"), Placement::Right).isNull());
    const auto originalPanes = tabSet(manager.model().panes(tab));
    QPointer<QWidget> added;
    PaneId addedPane;
    manager.setCloseGuard([&](const QVector<TabId>&) {
        added = editor("Added pane");
        addedPane = manager.splitPane(tab, originalPane, added, Placement::Bottom);
        return true;
    });
    QVERIFY(!(closeOnePane ? manager.closePane(tab, originalPane) : manager.closeTab(tab)));
    QVERIFY(!addedPane.isNull());
    deliverDeferredDeletes();
    QVERIFY(original && added);
    auto expectedPanes = originalPanes;
    expectedPanes.insert(addedPane);
    QCOMPARE(tabSet(manager.model().panes(tab)), expectedPanes);
    QCOMPARE(manager.paneWidget(originalPane), original.data());
    QCOMPARE(manager.paneWidget(addedPane), added.data());
    QVERIFY(manager.model().validate().ok);
}

void WorkspaceTest::recursiveCloseCannotBypassConfirmation()
{
    WindowManager manager;
    const auto root = manager.createWindow();
    QPointer<QWidget> content = editor("Retained until approved");
    const auto tab = manager.addTab(root, content, editorOptions("Retained", "Retained until approved"));
    bool nestedWindowClose = true;
    bool nestedTabClose = true;
    int guardCalls = 0;
    manager.setCloseGuard([&](const QVector<TabId>&) {
        ++guardCalls;
        manager.setCloseGuard({});
        nestedWindowClose = manager.requestCloseWindow(root);
        nestedTabClose = manager.closeTab(tab);
        return false;
    });
    const auto before = manager.model().toJson();
    QVERIFY(!manager.requestCloseWindow(root));
    QCOMPARE(guardCalls, 1);
    QVERIFY(!nestedWindowClose && !nestedTabClose);
    deliverDeferredDeletes();
    QCOMPARE(manager.model().toJson(), before);
    QVERIFY(content);
    QVERIFY(manager.requestCloseWindow(root));
    deliverDeferredDeletes();
    QVERIFY(content.isNull());
}

void WorkspaceTest::closingFloatingWindowOnlyReleasesItsOwnContent()
{
    WindowManager manager;
    const auto root = manager.createWindow();
    QPointer<QWidget> rootContent = editor("Root stays");
    QPointer<QWidget> floatContent = editor("Float closes");
    const auto rootTab = manager.addTab(root, rootContent, editorOptions("Root", "Root stays"));
    const auto floatingTab = manager.addTab(root, floatContent, editorOptions("Float", "Float closes"));
    const auto floating = manager.detachTab(floatingTab);
    QVector<TabId> permissionRequest;
    manager.setCloseGuard([&](const QVector<TabId>& tabs) { permissionRequest = tabs; return true; });
    QVERIFY(manager.requestCloseWindow(floating));
    QCOMPARE(permissionRequest, (QVector<TabId>{floatingTab}));
    deliverDeferredDeletes();
    QVERIFY(floatContent.isNull());
    QVERIFY(rootContent);
    QVERIFY(manager.model().window(root));
    QVERIFY(manager.model().tab(rootTab));
    QVERIFY(!manager.model().tab(floatingTab));
}

void WorkspaceTest::closingAnUnrelatedPaneCannotCloseTheTab()
{
    WindowManager manager;
    const auto root = manager.createWindow();
    QPointer<QWidget> content = editor("Must survive");
    const auto tab = manager.addTab(root, content, editorOptions("Retained", "Must survive"));
    const auto before = manager.model().toJson();
    QVERIFY(!manager.closePane(tab, QUuid::createUuid()));
    deliverDeferredDeletes();
    QCOMPARE(manager.model().toJson(), before);
    QVERIFY(content);
    QVERIFY(manager.model().tab(tab));
}

void WorkspaceTest::allCommandsCanBeBoundAndDefaultsHaveNoCollisions()
{
    WindowManager manager;
    const auto catalog = manager.commands().commands();
    QVERIFY(!catalog.isEmpty());
    QSet<QString> identifiers;
    QSet<QString> defaultShortcuts;
    for (const auto& command : catalog) {
        QVERIFY2(!command.id.isEmpty() && bool(command.execute), qPrintable(command.id));
        QVERIFY2(!identifiers.contains(command.id), qPrintable(command.id));
        identifiers.insert(command.id);
        if (!command.shortcut.isEmpty()) {
            const auto shortcut = command.shortcut.toString(QKeySequence::PortableText);
            QVERIFY2(!defaultShortcuts.contains(shortcut), qPrintable("Default shortcut collision: " + command.id + " / " + shortcut));
            defaultShortcuts.insert(shortcut);
        }
        const QKeySequence userShortcut("Ctrl+Alt+Shift+F24");
        QVERIFY(manager.commands().setShortcut(command.id, userShortcut));
        QCOMPARE(manager.commands().find(command.id)->shortcut, userShortcut);
        QVERIFY(manager.commands().setShortcut(command.id, {}));
    }
    QCOMPARE(manager.commands().commands().size(), catalog.size());
    QVERIFY(!manager.commands().setShortcut("missing.command", QKeySequence("Ctrl+F24")));
    Command duplicate = catalog.first();
    QVERIFY(!manager.commands().add(std::move(duplicate)));
    QCOMPARE(manager.commands().commands().size(), catalog.size());
}

void WorkspaceTest::customCommandsReceiveArgumentsAndRespectTheirPrecondition()
{
    WindowManager manager;
    const auto root = manager.createWindow();
    const auto tab = manager.addTab(root, editor("Context"), editorOptions("Context", "Context"));
    int calls = 0;
    CommandContext captured;
    QVERIFY(manager.commands().add({"test.hostCommand", "Host command", "Host", {},
        [](const CommandContext& context) { return context.args.value("allowed").toBool(); },
        [&](const CommandContext& context) { captured = context; ++calls; return true; }}));
    QVERIFY(!manager.execute("test.hostCommand", {{"allowed", false}}, root));
    QCOMPARE(calls, 0);
    QVERIFY(manager.execute("test.hostCommand", {{"allowed", true}, {"value", 42}}, root));
    QCOMPARE(calls, 1);
    QCOMPARE(captured.window, root);
    QCOMPARE(captured.tab, tab);
    QCOMPARE(captured.pane, manager.model().tab(tab)->activePane);
    QCOMPARE(captured.args.value("value").toInt(), 42);
    QVERIFY(!manager.execute("test.missingCommand", {}, root));
    QCOMPARE(calls, 1);
}

void WorkspaceTest::invalidTabArgumentCannotChangeEdgeLayout()
{
    WindowManager manager;
    const auto source = manager.createWindow();
    const auto target = manager.createWindow();
    QPointer<QWidget> content = editor("Retained content");
    const auto tab = manager.addTab(source, content, editorOptions("Retained", "Retained content"));
    const auto pane = manager.model().tab(tab)->activePane;
    const auto before = manager.model().toJson();
    QSignalSpy failure(&manager, &WindowManager::commandFailed);
    QVERIFY(!manager.execute("workspace.moveTabToEdge", {
        {"tab", QUuid::createUuid()}, {"targetWindow", target}, {"edge", int(Edge::Left)}
    }, source));
    QCOMPARE(failure.count(), 1);
    QCOMPARE(manager.model().toJson(), before);
    QCOMPARE(manager.paneWidget(pane), content.data());
    QCOMPARE(content->window(), manager.windowWidget(source));
    QVERIFY(manager.model().validate().ok);
}

void WorkspaceTest::aBoundCommandExecutesFromContentKeyboardInput()
{
    WindowManager manager;
    const auto root = manager.createWindow();
    auto* content = editor("Keyboard content");
    const auto tab = manager.addTab(root, content, editorOptions("Keyboard", "Keyboard content"));
    int calls = 0;
    TabId commandTab;
    QVERIFY(manager.commands().add({"test.keyboard", "Keyboard action", "Host", {}, {},
        [&](const CommandContext& context) { ++calls; commandTab = context.tab; return true; }}));
    QVERIFY(manager.commands().setShortcut("test.keyboard", QKeySequence("Ctrl+Alt+F9")));
    manager.showWindow(root);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(root)));
    QVERIFY(manager.focusPane(tab, manager.model().tab(tab)->activePane));
    QTest::keyClick(content, Qt::Key_F9, Qt::ControlModifier | Qt::AltModifier);
    QCOMPARE(calls, 1);
    QCOMPARE(commandTab, tab);
    QCOMPARE(content->toPlainText(), QStringLiteral("Keyboard content"));
}

void WorkspaceTest::invalidRestorePreservesTheCurrentModelAndWidgets()
{
    WindowManager manager;
    const auto root = manager.createWindow();
    auto* content = editor("Retained after malformed restore");
    const auto tab = manager.addTab(root, content, editorOptions("Retained", content->toPlainText()));
    const auto pane = manager.model().tab(tab)->activePane;
    int factoryCalls = 0;
    manager.setContentFactory([&](const TabOptions&, PaneId) { ++factoryCalls; return editor("Unexpected"); });
    const auto before = manager.model().toJson();
    auto malformed = manager.saveLayout();
    malformed.insert("model", QJsonObject{{"schemaVersion", 99}});
    QVERIFY(!manager.restoreLayout(malformed).ok);
    QCOMPARE(factoryCalls, 0);
    QCOMPARE(manager.model().toJson(), before);
    QCOMPARE(manager.paneWidget(pane), content);
    QCOMPARE(content->toPlainText(), QStringLiteral("Retained after malformed restore"));
}

void WorkspaceTest::failedRestoreReleasesPreparedContentsAndPreservesCurrentContents()
{
    WindowManager manager;
    const auto root = manager.createWindow();
    QPointer<QWidget> originalA = editor("Original A");
    QPointer<QWidget> originalB = editor("Original B");
    manager.addTab(root, originalA, editorOptions("A", "Original A"));
    manager.addTab(root, originalB, editorOptions("B", "Original B"));
    const auto snapshot = manager.saveLayout();
    const auto before = manager.model().toJson();
    int factoryCalls = 0;
    QPointer<QWidget> prepared;
    manager.setContentFactory([&](const TabOptions&, PaneId) -> QWidget* {
        if (++factoryCalls == 2) return nullptr;
        prepared = editor("Prepared only");
        return prepared;
    });
    QVERIFY(!manager.restoreLayout(snapshot).ok);
    QCOMPARE(factoryCalls, 2);
    QVERIFY(prepared.isNull());
    QVERIFY(originalA && originalB);
    QCOMPARE(manager.model().toJson(), before);
    deliverDeferredDeletes();
    QVERIFY(originalA && originalB);
}

void WorkspaceTest::restoreFactoriesMayDestroyTheirManager_data()
{
    QTest::addColumn<int>("destroyOnCall");
    QTest::newRow("first-factory-return") << 1;
    QTest::newRow("after-one-prepared-pane") << 2;
}

void WorkspaceTest::restoreFactoriesMayDestroyTheirManager()
{
    QFETCH(int, destroyOnCall);
    auto manager = std::make_unique<WindowManager>(); const auto window = manager->createWindow();
    const QPointer<QWidget> originalA = editor("Original A"), originalB = editor("Original B");
    manager->addTab(window, originalA, editorOptions("A", "Original A"));
    manager->addTab(window, originalB, editorOptions("B", "Original B"));
    const auto snapshot = manager->saveLayout();
    int calls = 0, originalDestroyed = 0, preparedDestroyed = 0;
    connect(originalA, &QObject::destroyed, this, [&] { ++originalDestroyed; });
    connect(originalB, &QObject::destroyed, this, [&] { ++originalDestroyed; });
    QVector<QPointer<QWidget>> prepared;
    manager->setContentFactory([&](const TabOptions& options, PaneId) -> QWidget* {
        // Factory replacement/destruction is a plain host callback, not a
        // QWidget show/hide event. Copy all observers before deleting owner.
        auto* owner = &manager; auto* count = &calls; auto* widgets = &prepared;
        auto* destroyed = &preparedDestroyed; auto* receiver = this;
        const auto text = options.state.value("text").toString();
        const bool removeManager = ++*count == destroyOnCall;
        if (removeManager) owner->reset();
        auto* content = editor(text);
        widgets->append(content);
        QObject::connect(content, &QObject::destroyed, receiver, [destroyed] { ++*destroyed; });
        return content;
    });
    const auto result = manager->restoreLayout(snapshot);
    QVERIFY(!result); QVERIFY(!result.error.isEmpty()); QVERIFY(!manager);
    QCOMPARE(calls, destroyOnCall); QCOMPARE(prepared.size(), destroyOnCall);
    QCOMPARE(preparedDestroyed, destroyOnCall); QCOMPARE(originalDestroyed, 2);
    QVERIFY(!originalA); QVERIFY(!originalB);
    for (const auto& content : prepared) QVERIFY(!content);
    deliverDeferredDeletes(); QCOMPARE(preparedDestroyed, destroyOnCall); QCOMPARE(originalDestroyed, 2);
}

void WorkspaceTest::restoreFactoryMayReplaceItselfWithoutDestroyingItsCaptures()
{
    struct Capture { QString text = QStringLiteral("Original factory return"); };
    WindowManager manager; const auto window = manager.createWindow();
    const QPointer<QWidget> originalA = editor("Original A"), originalB = editor("Original B");
    const auto tabA = manager.addTab(window, originalA, editorOptions("A", "Original A"));
    const auto tabB = manager.addTab(window, originalB, editorOptions("B", "Original B"));
    const auto paneA = manager.model().tab(tabA)->activePane, paneB = manager.model().tab(tabB)->activePane;
    const auto snapshot = manager.saveLayout(); const auto modelBefore = manager.model().toJson();
    int firstCalls = 0, replacementCalls = 0; bool capturesSurvivedReplacement = false;
    QHash<PaneId, QString> generated;
    auto capture = std::make_shared<Capture>(); const std::weak_ptr<Capture> weakCapture = capture;
    manager.setContentFactory([&, capture](const TabOptions&, PaneId pane) -> QWidget* {
        const auto text = capture->text;
        const auto observer = weakCapture;
        auto* captureState = &capturesSurvivedReplacement; auto* produced = &generated;
        ++firstCalls;
        manager.setContentFactory([&](const TabOptions&, PaneId next) -> QWidget* {
            ++replacementCalls;
            const auto replacement = QStringLiteral("Replacement factory return");
            generated.insert(next, replacement);
            return editor(replacement);
        });
        *captureState = !observer.expired();
        produced->insert(pane, text);
        return editor(text);
    });
    capture.reset(); QVERIFY(!weakCapture.expired());
    const auto result = manager.restoreLayout(snapshot);
    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(capturesSurvivedReplacement); QVERIFY(weakCapture.expired());
    QCOMPARE(firstCalls, 1); QCOMPARE(replacementCalls, 1);
    QCOMPARE(QSet<PaneId>(generated.keyBegin(), generated.keyEnd()), (QSet<PaneId>{paneA, paneB}));
    QCOMPARE(manager.model().toJson(), modelBefore);
    for (const auto id : {paneA, paneB}) {
        auto* restored = qobject_cast<QPlainTextEdit*>(manager.paneWidget(id)); QVERIFY(restored);
        QCOMPARE(restored->toPlainText(), generated.value(id));
    }
    deliverDeferredDeletes(); QVERIFY(!originalA); QVERIFY(!originalB); QVERIFY(manager.model().validate());
}

void WorkspaceTest::validRestoreCreatesContentsForExistingStableIds()
{
    WindowManager manager;
    const auto root = manager.createWindow();
    QPointer<QWidget> oldA = editor("State A");
    QPointer<QWidget> oldB = editor("State B");
    const auto tabA = manager.addTab(root, oldA, editorOptions("A", "State A"));
    const auto tabB = manager.addTab(root, oldB, editorOptions("B", "State B"));
    const auto floating = manager.detachTab(tabB);
    const auto paneA = manager.model().tab(tabA)->activePane;
    const auto paneB = manager.model().tab(tabB)->activePane;
    const auto snapshot = manager.saveLayout();
    const auto before = manager.model().toJson();
    QSet<PaneId> factoryIds;
    manager.setContentFactory([&](const TabOptions& options, PaneId pane) {
        factoryIds.insert(pane);
        return editor(options.state.value("text").toString());
    });
    const auto result = manager.restoreLayout(snapshot);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(factoryIds, (QSet<PaneId>{paneA, paneB}));
    QCOMPARE(manager.model().toJson(), before);
    QVERIFY(manager.paneWidget(paneA) != oldA);
    QVERIFY(manager.paneWidget(paneB) != oldB);
    QCOMPARE(qobject_cast<QPlainTextEdit*>(manager.paneWidget(paneA))->toPlainText(), QStringLiteral("State A"));
    QCOMPARE(qobject_cast<QPlainTextEdit*>(manager.paneWidget(paneB))->toPlainText(), QStringLiteral("State B"));
    QCOMPARE(manager.paneWidget(paneB)->window(), manager.windowWidget(floating));
    deliverDeferredDeletes();
    QVERIFY(oldA.isNull() && oldB.isNull());
    QVERIFY(manager.model().validate().ok);
}

void WorkspaceTest::restoredWindowTitlesSurviveFreshAndExistingHosts()
{
    QJsonObject snapshot;
    WindowId root;
    WindowId floating;
    {
        WindowManager source;
        root = source.createWindow(WindowKind::Workbench, {"Saved host", QSize(980, 640)});
        source.addTab(root, editor("Root"), editorOptions("Root", "Root"));
        const auto tab = source.addTab(root, editor("Float"), editorOptions("Float", "Float"));
        floating = source.detachTab(tab);
        snapshot = source.saveLayout();
    }
    deliverDeferredDeletes();
    WindowManager restored;
    restored.setContentFactory([](const TabOptions& options, PaneId) {
        return editor(options.state.value("text").toString());
    });
    QVERIFY(restored.restoreLayout(snapshot).ok);
    QVERIFY(restored.windowWidget(root)->windowTitle().endsWith(" — Saved host"));
    QVERIFY(restored.windowWidget(floating)->windowTitle().contains("Saved host"));
    auto records = snapshot.value("windows").toArray();
    for (qsizetype i = 0; i < records.size(); ++i) {
        auto record = records.at(i).toObject();
        record.insert("title", "Updated host"); records[i] = record;
    }
    snapshot.insert("windows", records);
    QVERIFY(restored.restoreLayout(snapshot).ok);
    QVERIFY(restored.windowWidget(root)->windowTitle().endsWith(" — Updated host"));
    QVERIFY(restored.windowWidget(floating)->windowTitle().contains("Updated host"));
    for (const auto& entry : restored.saveLayout().value("windows").toArray())
        QCOMPARE(entry.toObject().value("title").toString(), QStringLiteral("Updated host"));
    QVERIFY(restored.execute("window.new", {}, root));
    const auto newWindow = restored.model().windowOrder().last();
    QVERIFY(newWindow != root && newWindow != floating);
    QCOMPARE(restored.windowWidget(newWindow)->windowTitle(), QStringLiteral("Updated host"));
}

void WorkspaceTest::emptyRootWithFloatingContentSurvivesAutomaticClosePolicy()
{
    WindowManager manager;
    const auto root = manager.createWindow();
    auto* content = editor("Floating keeps family alive");
    const auto tab = manager.addTab(root, content, editorOptions("Only", content->toPlainText()));
    auto behavior = manager.behavior();
    behavior.closeEmptyWorkbench = true;
    manager.setBehavior(behavior);
    const auto floating = manager.detachTab(tab);
    deliverDeferredDeletes();
    QVERIFY(manager.model().window(root));
    QVERIFY(manager.windowWidget(root));
    QVERIFY(manager.model().window(floating));
    QVERIFY(manager.model().orderedTabs(root).isEmpty());
    QVERIFY(manager.model().familyHasTabs(manager.model().window(root)->familyId));
    QVERIFY(manager.requestCloseWindow(floating));
    QTRY_VERIFY(manager.model().windows().isEmpty());
}

void WorkspaceTest::repeatedCrossWindowMovesDoNotRecreateOrDestroyContent()
{
    WindowManager manager;
    const auto root = manager.createWindow();
    auto* content = editor("Persistent instance");
    const auto tab = manager.addTab(root, content, editorOptions("Persistent", "Persistent instance"));
    const auto pane = manager.model().tab(tab)->activePane;
    QSignalSpy destroyed(content, &QObject::destroyed);
    for (int iteration = 0; iteration < 20; ++iteration) {
        const auto floating = manager.detachTab(tab);
        QVERIFY(!floating.isNull());
        QPointer<QWidget> retiredWindow = manager.windowWidget(floating);
        QVERIFY(manager.execute("window.redock", {}, floating));
        deliverDeferredDeletes();
        QVERIFY(retiredWindow.isNull());
        QCOMPARE(manager.paneWidget(pane), content);
        QCOMPARE(content->toPlainText(), QStringLiteral("Persistent instance"));
        QCOMPARE(manager.model().windows().size(), 1);
        QCOMPARE(manager.model().tabs().size(), 1);
        QCOMPARE(manager.model().panes(tab).size(), 1);
        QCOMPARE(destroyed.count(), 0);
    }
    QVERIFY(manager.closeTab(tab));
    deliverDeferredDeletes();
    QCOMPARE(destroyed.count(), 1);
}

void WorkspaceTest::edgePinAndReturnToCenterPreserveTheSameWidget()
{
    WindowManager manager;
    const auto root = manager.createWindow();
    auto* central = editor("Central");
    auto* side = editor("Sessions");
    const auto centralTab = manager.addTab(root, central, editorOptions("Central", "Central"));
    const auto sideTab = manager.addTab(root, side, editorOptions("Sessions", "Sessions"));
    const auto pane = manager.model().tab(sideTab)->activePane;
    QSignalSpy destroyed(side, &QObject::destroyed);
    QVERIFY(manager.moveTabToEdge(sideTab, Edge::Left));
    const auto edgeGroup = manager.model().groupForTab(sideTab);
    QCOMPARE(manager.model().group(edgeGroup)->location, GroupLocation::AutoHide);
    QCOMPARE(manager.model().centralGroups(root), (QVector<GroupId>{manager.model().groupForTab(centralTab)}));
    QVERIFY(manager.pinGroup(edgeGroup, true));
    QCOMPARE(manager.model().group(edgeGroup)->location, GroupLocation::Pinned);
    QCOMPARE(manager.paneWidget(pane), side);
    QCOMPARE(side->window(), manager.windowWidget(root));
    QVERIFY(manager.pinGroup(edgeGroup, false));
    QVERIFY(manager.moveGroupToCenter(edgeGroup));
    QCOMPARE(manager.model().group(edgeGroup)->location, GroupLocation::Central);
    QCOMPARE(manager.paneWidget(pane), side);
    QCOMPARE(side->toPlainText(), QStringLiteral("Sessions"));
    QCOMPARE(destroyed.count(), 0);
    QVERIFY(manager.model().validate().ok);
}

void WorkspaceTest::bulkCloseCommandsRespectTheSelectedTabAndGroup_data()
{
    QTest::addColumn<QString>("scope"); QTest::addColumn<QVector<int>>("surviving");
    QTest::newRow("left") << QStringLiteral("left") << QVector<int>{1, 2, 3};
    QTest::newRow("right") << QStringLiteral("right") << QVector<int>{0, 1};
    QTest::newRow("others") << QStringLiteral("others") << QVector<int>{1};
    QTest::newRow("all") << QStringLiteral("all") << QVector<int>{};
}
void WorkspaceTest::bulkCloseCommandsRespectTheSelectedTabAndGroup()
{
    QFETCH(QString, scope); QFETCH(QVector<int>, surviving);
    WindowManager manager; const auto window = manager.createWindow();
    QVector<TabId> tabs; QVector<QPointer<QWidget>> contents;
    for (int i = 0; i < 4; ++i) {
        contents << editor(QString::number(i));
        tabs << manager.addTab(window, contents.last(), editorOptions(QString::number(i), QString::number(i)));
    }
    QPointer<QWidget> independent = editor("Independent");
    const auto other = manager.addTab(window, independent, editorOptions("Independent", "Independent"));
    QVERIFY(manager.moveTab(other, manager.model().groupForTab(tabs.first()), Placement::Right));
    QVERIFY(manager.activateTab(tabs[1]));
    QVERIFY(manager.execute("workspace.closeTabs." + scope, {}, window));
    deliverDeferredDeletes();
    for (int i = 0; i < tabs.size(); ++i) {
        QCOMPARE(manager.model().tab(tabs[i]) != nullptr, surviving.contains(i));
        QCOMPARE(!contents[i].isNull(), surviving.contains(i));
    }
    QVERIFY(manager.model().tab(other)); QVERIFY(independent);
    QVERIFY(manager.model().validate().ok);
}
void WorkspaceTest::refusedBulkCloseIsAtomicAndDoesNotCloseNewTabs()
{
    WindowManager manager; const auto window = manager.createWindow();
    const auto a = manager.addTab(window, editor("A"), editorOptions("A", "A"));
    const auto b = manager.addTab(window, editor("B"), editorOptions("B", "B"));
    int requests = 0;
    manager.setCloseGuard([&](const QVector<TabId>& tabs) { ++requests; return tabs.size() == 1; });
    const auto before = manager.model().toJson();
    QVERIFY(!manager.closeTabs({a, b})); QCOMPARE(requests, 1); QCOMPARE(manager.model().toJson(), before);
    TabId added;
    manager.setCloseGuard([&](const QVector<TabId>& tabs) {
        ++requests;
        added = manager.addTab(window, editor("New"), editorOptions("New", "New"));
        return tabSet(tabs) == (QSet<TabId>{a, b});
    });
    QVERIFY(manager.closeTabs({a, b}));
    QVERIFY(!added.isNull()); QVERIFY(manager.model().tab(added));
    QCOMPARE(manager.model().orderedTabs(window), (QVector<TabId>{added}));
    QVERIFY(manager.model().validate().ok);
}

void WorkspaceTest::closeAllApprovesOnceAndSavesTheCompleteWorkspace()
{
    WindowManager manager;
    const auto first = manager.createWindow();
    const auto second = manager.createWindow();
    QVector<QPointer<QWidget>> contents{editor("First"), editor("Second"), editor("Float")};
    const auto a = manager.addTab(first, contents[0], editorOptions("First", "First"));
    const auto b = manager.addTab(second, contents[1], editorOptions("Second", "Second"));
    const auto c = manager.addTab(first, contents[2], editorOptions("Float", "Float"));
    const auto floating = manager.detachTab(c);
    const auto before = manager.saveLayout();
    int approvalCalls = 0;
    manager.setCloseGuard([&](const QVector<TabId>& tabs) {
        ++approvalCalls;
        return tabSet(tabs) == (QSet<TabId>{a, b, c});
    });
    QJsonObject saved;
    QObject::connect(&manager, &WindowManager::lastWindowAboutToClose, &manager, [&] {
        saved = manager.saveLayout();
        QVERIFY(manager.model().window(first)); QVERIFY(manager.model().window(second));
        QVERIFY(manager.model().window(floating));
        for (const auto& content : contents) QVERIFY(content);
    });
    QSignalSpy finalSave(&manager, &WindowManager::lastWindowAboutToClose);
    QSignalSpy closed(&manager, &WindowManager::allWindowsClosed);
    QVERIFY(manager.requestCloseAllWindows());
    QCOMPARE(approvalCalls, 1); QCOMPARE(finalSave.count(), 1); QCOMPARE(closed.count(), 1);
    QCOMPARE(saved, before); QCOMPARE(saved.value("windows").toArray().size(), 3);
    QVERIFY(manager.model().windows().isEmpty()); QVERIFY(manager.model().tabs().isEmpty());
    QVERIFY(!manager.requestCloseAllWindows()); QCOMPARE(closed.count(), 1);
    deliverDeferredDeletes();
    for (const auto& content : contents) QVERIFY(content.isNull());
}

void WorkspaceTest::refusedCloseAllPreservesEveryFamily()
{
    WindowManager manager;
    const auto first = manager.createWindow(); const auto second = manager.createWindow();
    QPointer<QWidget> a = editor("First"); QPointer<QWidget> b = editor("Second");
    const auto firstTab = manager.addTab(first, a, editorOptions("First", "First"));
    const auto secondTab = manager.addTab(second, b, editorOptions("Second", "Second"));
    const auto floating = manager.detachTab(secondTab);
    const auto before = manager.saveLayout();
    int approvals = 0;
    QVector<TabId> requested;
    manager.setCloseGuard([&](const QVector<TabId>& tabs) {
        ++approvals; requested = tabs; return false;
    });
    QSignalSpy finalSave(&manager, &WindowManager::lastWindowAboutToClose);
    QSignalSpy closed(&manager, &WindowManager::allWindowsClosed);
    QVERIFY(!manager.requestCloseAllWindows());
    QCOMPARE(approvals, 1); QCOMPARE(manager.saveLayout(), before);
    QCOMPARE(tabSet(requested), (QSet<TabId>{firstTab, secondTab}));
    QCOMPARE(finalSave.count(), 0); QCOMPARE(closed.count(), 0);
    deliverDeferredDeletes(); QVERIFY(a && b); QVERIFY(manager.model().window(floating));
    manager.setCloseGuard({}); QVERIFY(manager.requestCloseAllWindows());
    QCOMPARE(closed.count(), 1);
}

void WorkspaceTest::closeAllRejectsChangesDuringCallbacks_data()
{
    QTest::addColumn<bool>("notification"); QTest::addColumn<QString>("mutation");
    for (const bool notification : {false, true})
        for (const auto* mutation : {"pane", "tab", "window", "options", "placement", "layout"})
            QTest::newRow(qPrintable(QString("%1-%2").arg(notification ? "notification" : "approval", mutation)))
                << notification << QString(mutation);
}
void WorkspaceTest::closeAllRejectsChangesDuringCallbacks()
{
    QFETCH(bool, notification); QFETCH(QString, mutation);
    WindowManager manager; const auto first = manager.createWindow(); const auto second = manager.createWindow();
    QPointer<QWidget> original = editor("Original");
    const auto tab = manager.addTab(first, original, editorOptions("Original", "Original"));
    const auto pane = manager.model().tab(tab)->activePane;
    const auto other = manager.addTab(second, editor("Other"), editorOptions("Other", "Other"));
    QPointer<QWidget> added; TabId addedTab; PaneId addedPane; WindowId addedWindow;
    bool changed = false;
    auto mutate = [&] {
        changed = true;
        if (mutation == "pane") {
            added = editor("Added"); addedPane = manager.splitPane(tab, pane, added, Placement::Right);
        } else if (mutation == "tab") {
            added = editor("Added"); addedTab = manager.addTab(first, added, editorOptions("Added", "Added"));
        } else if (mutation == "window") addedWindow = manager.createWindow();
        else if (mutation == "options") QVERIFY(manager.setSideBarsVisible(first, false));
        else if (mutation == "placement") manager.windowWidget(first)->move(321, 234);
        else QVERIFY(manager.moveTab(other, manager.model().groupForTab(tab), Placement::Right));
    };
    manager.setCloseGuard([&](const QVector<TabId>&) { if (!notification) mutate(); return true; });
    QObject::connect(&manager, &WindowManager::lastWindowAboutToClose, &manager, [&] { if (notification) mutate(); });
    QSignalSpy closed(&manager, &WindowManager::allWindowsClosed);
    QVERIFY(!manager.requestCloseAllWindows()); QVERIFY(changed); QCOMPARE(closed.count(), 0);
    deliverDeferredDeletes(); QVERIFY(original); QVERIFY(manager.model().window(first));
    QVERIFY(manager.model().window(second)); QVERIFY(manager.model().tab(tab)); QVERIFY(manager.model().tab(other));
    if (mutation == "pane") { QVERIFY(!addedPane.isNull()); QCOMPARE(manager.paneWidget(addedPane), added.data()); }
    if (mutation == "tab") { QVERIFY(!addedTab.isNull()); QVERIFY(manager.model().tab(addedTab)); QVERIFY(added); }
    if (mutation == "window") { QVERIFY(!addedWindow.isNull()); QVERIFY(manager.model().window(addedWindow)); }
    QVERIFY(manager.model().validate().ok);
}

void WorkspaceTest::recursiveCloseAllCannotBypassConfirmation()
{
    WindowManager manager; const auto root = manager.createWindow();
    const auto empty = manager.createWindow();
    QPointer<QWidget> original = editor("Original");
    const auto tab = manager.addTab(root, original, editorOptions("Original", "Original"));
    const auto pane = manager.model().tab(tab)->activePane;
    int approvals = 0;
    QVector<bool> nestedResults;
    manager.setCloseGuard([&](const QVector<TabId>&) {
        ++approvals; manager.setCloseGuard({});
        nestedResults = {manager.requestCloseAllWindows(), manager.requestCloseWindow(root),
            manager.requestCloseWindow(empty), manager.closeTab(tab), manager.closeTabs({tab}), manager.closePane(tab, pane)};
        return false;
    });
    const auto before = manager.saveLayout();
    QVERIFY(!manager.requestCloseAllWindows()); QCOMPARE(approvals, 1);
    QCOMPARE(nestedResults, (QVector<bool>{false, false, false, false, false, false}));
    QCOMPARE(manager.saveLayout(), before); QVERIFY(original);
    QVERIFY(manager.requestCloseAllWindows());
}

void WorkspaceTest::closeAllRejectsContentReplacementWithTheSameIds()
{
    WindowManager manager; const auto root = manager.createWindow();
    QPointer<QWidget> original = editor("Original");
    const auto tab = manager.addTab(root, original, editorOptions("Original", "Original"));
    const auto pane = manager.model().tab(tab)->activePane;
    manager.setContentFactory([](const TabOptions& options, PaneId) { return editor(options.state.value("text").toString()); });
    manager.setCloseGuard([&](const QVector<TabId>&) { return manager.restoreLayout(manager.saveLayout()).ok; });
    QVERIFY(!manager.requestCloseAllWindows());
    QPointer<QWidget> replacement = manager.paneWidget(pane); QVERIFY(replacement && replacement != original);
    deliverDeferredDeletes(); QVERIFY(original.isNull()); QVERIFY(replacement);
    QVERIFY(manager.model().tab(tab)); QVERIFY(manager.model().window(root)); QVERIFY(manager.model().validate().ok);
}

void WorkspaceTest::closeAllHandlesManagerDestruction_data()
{
    QTest::addColumn<QString>("stage");
    for (const auto* stage : {"approval", "notification", "closed"}) QTest::newRow(stage) << QString(stage);
}
void WorkspaceTest::closeAllHandlesManagerDestruction()
{
    QFETCH(QString, stage);
    auto manager = std::make_unique<WindowManager>(); const auto root = manager->createWindow();
    manager->addTab(root, editor("Original"), editorOptions("Original", "Original"));
    QPointer<WindowManager> observed(manager.get());
    if (stage == "approval") manager->setCloseGuard([&](const QVector<TabId>&) { manager.reset(); return true; });
    else QObject::connect(manager.get(), stage == "notification" ? &WindowManager::lastWindowAboutToClose
        : &WindowManager::allWindowsClosed, [&] { manager.reset(); });
    const bool accepted = manager->requestCloseAllWindows();
    QVERIFY(observed.isNull()); QCOMPARE(accepted, stage == "closed");
}

void WorkspaceTest::closeAllFromLayoutNotificationEmitsClosedOnce()
{
    WindowManager manager; const auto root = manager.createWindow();
    const auto tab = manager.addTab(root, editor("Original"), editorOptions("Original", "Original"));
    QSignalSpy closed(&manager, &WindowManager::allWindowsClosed);
    QSignalSpy finalSave(&manager, &WindowManager::lastWindowAboutToClose);
    bool requested = false; bool accepted = false;
    QObject::connect(&manager, &WindowManager::layoutChanged, &manager, [&] {
        if (!requested) { requested = true; accepted = manager.requestCloseAllWindows(); }
    });
    QVERIFY(manager.setTabIconVisible(tab, false)); QVERIFY(requested && accepted);
    QCOMPARE(closed.count(), 1); QCOMPARE(finalSave.count(), 1); QVERIFY(manager.model().windows().isEmpty());
}
void WorkspaceTest::closeAllAllowsUnchangedFocusAfterApproval()
{
    WindowManager manager; const auto root = manager.createWindow();
    const auto tab = manager.addTab(root, editor("Original"), editorOptions("Original", "Original"));
    const auto before = manager.saveLayout(); bool focused = false;
    manager.setCloseGuard([&](const QVector<TabId>&) { focused = manager.activateTab(tab); return true; });
    QJsonObject saved;
    QObject::connect(&manager, &WindowManager::lastWindowAboutToClose, &manager, [&] { saved = manager.saveLayout(); });
    QVERIFY(manager.requestCloseAllWindows()); QVERIFY(focused); QCOMPARE(saved, before);
}

QTEST_MAIN(WorkspaceTest)
#include "workspace_test.moc"
