// SPDX-License-Identifier: MIT
#include <window_system/window_manager.hpp>

#include <QApplication>
#include <QCoreApplication>
#include <QFocusEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>

using namespace window_system;

namespace {

void processDeletes()
{
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}

QWidget* paneHost(QWidget* content)
{
    for (auto* parent = content->parentWidget(); parent; parent = parent->parentWidget())
        if (parent->objectName() == QStringLiteral("PaneHost")) return parent;
    return nullptr;
}

QWidget* tabPage(QWidget* content)
{
    for (auto* parent = content->parentWidget(); parent; parent = parent->parentWidget())
        if (parent->objectName() == QStringLiteral("TabPage")) return parent;
    return nullptr;
}

void sendContentFocus(QWidget* content)
{
    QFocusEvent focus(QEvent::FocusIn, Qt::MouseFocusReason);
    QApplication::sendEvent(content, &focus);
}

} // namespace

class PaneBarPolicyTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanup();
    void topAndBottomPoliciesAreIndependent_data();
    void topAndBottomPoliciesAreIndependent();
    void tabFallbackStaysSingleAndMissingPaneBarsTakeNoSpace();
    void changingPolicyPreservesWidgetsFocusAndSplitModel();
    void policiesAndBarsSurviveMovesAndPaneExtraction();
    void closingPaneDestroysOnlyItsBars();
    void replacingTabBarsDefersDeletionLikePaneBars();
    void layoutRestoresPolicyWithoutSerializingWidgets();
    void invalidCallsLeaveThePoliciesUnchanged();
};

void PaneBarPolicyTest::initTestCase()
{
    qApp->setQuitOnLastWindowClosed(false);
}

void PaneBarPolicyTest::cleanup() { processDeletes(); }

void PaneBarPolicyTest::topAndBottomPoliciesAreIndependent_data()
{
    QTest::addColumn<bool>("sharedTop");
    QTest::addColumn<bool>("sharedBottom");
    QTest::newRow("both-shared") << true << true;
    QTest::newRow("top-per-pane") << false << true;
    QTest::newRow("bottom-per-pane") << true << false;
    QTest::newRow("both-per-pane") << false << false;
}

void PaneBarPolicyTest::topAndBottomPoliciesAreIndependent()
{
    QFETCH(bool, sharedTop);
    QFETCH(bool, sharedBottom);
    WindowManager manager;
    const auto window = manager.createWindow();
    TabOptions options{QStringLiteral("independent"), QStringLiteral("test.editor"), {}};
    options.topBarSharing = sharedTop ? PaneBarSharing::Shared : PaneBarSharing::PerPane;
    options.bottomBarSharing = sharedBottom ? PaneBarSharing::Shared : PaneBarSharing::PerPane;
    auto* firstContent = new QPlainTextEdit;
    const auto tab = manager.addTab(window, firstContent, options);
    const auto first = manager.model().tab(tab)->activePane;
    auto* secondContent = new QPlainTextEdit;
    const auto second = manager.splitPane(tab, first, secondContent, Placement::Right);
    auto* firstTop = new QLabel(QStringLiteral("first-top"));
    auto* secondTop = new QLabel(QStringLiteral("second-top"));
    auto* firstBottom = new QLabel(QStringLiteral("first-bottom"));
    auto* secondBottom = new QLabel(QStringLiteral("second-bottom"));
    QVERIFY(manager.setPaneTopBar(first, firstTop));
    QVERIFY(manager.setPaneTopBar(second, secondTop));
    QVERIFY(manager.setPaneBottomBar(first, firstBottom));
    QVERIFY(manager.setPaneBottomBar(second, secondBottom));
    manager.showWindow(window);
    QVERIFY(manager.focusPane(tab, first));
    QCoreApplication::processEvents();
    auto* page = tabPage(firstContent);
    auto* firstFrame = paneHost(firstContent);
    auto* secondFrame = paneHost(secondContent);
    QVERIFY(page && firstFrame && secondFrame);
    QCOMPARE(firstTop->parentWidget(), sharedTop ? page : firstFrame);
    QCOMPARE(secondTop->parentWidget(), sharedTop ? page : secondFrame);
    QCOMPARE(firstBottom->parentWidget(), sharedBottom ? page : firstFrame);
    QCOMPARE(secondBottom->parentWidget(), sharedBottom ? page : secondFrame);
    QVERIFY(firstTop->isVisible());
    QVERIFY(firstBottom->isVisible());
    QCOMPARE(secondTop->isVisible(), !sharedTop);
    QCOMPARE(secondBottom->isVisible(), !sharedBottom);
    if (!sharedTop) {
        QCOMPARE(firstFrame->layout()->itemAt(0)->widget(), firstTop);
        QCOMPARE(secondFrame->layout()->itemAt(0)->widget(), secondTop);
    }
    if (!sharedBottom) {
        QCOMPARE(firstFrame->layout()->itemAt(firstFrame->layout()->count() - 1)->widget(), firstBottom);
        QCOMPARE(secondFrame->layout()->itemAt(secondFrame->layout()->count() - 1)->widget(), secondBottom);
    }
    const auto firstSize = firstFrame->size();
    const auto secondSize = secondFrame->size();
    sendContentFocus(secondContent);
    QCOMPARE(manager.model().tab(tab)->activePane, second);
    QCOMPARE(firstTop->isVisible(), !sharedTop);
    QCOMPARE(firstBottom->isVisible(), !sharedBottom);
    QVERIFY(secondTop->isVisible());
    QVERIFY(secondBottom->isVisible());
    QCOMPARE(firstFrame->size(), firstSize);
    QCOMPARE(secondFrame->size(), secondSize);
    QCOMPARE(manager.paneWidget(first), firstContent);
    QCOMPARE(manager.paneWidget(second), secondContent);
    QCOMPARE(manager.tabOptions(tab).topBarSharing, options.topBarSharing);
    QCOMPARE(manager.tabOptions(tab).bottomBarSharing, options.bottomBarSharing);
}

void PaneBarPolicyTest::tabFallbackStaysSingleAndMissingPaneBarsTakeNoSpace()
{
    WindowManager manager;
    const auto window = manager.createWindow();
    auto* firstContent = new QPlainTextEdit;
    const auto tab = manager.addTab(window, firstContent, {QStringLiteral("fallback"), {}, {}});
    const auto first = manager.model().tab(tab)->activePane;
    auto* secondContent = new QPlainTextEdit;
    const auto second = manager.splitPane(tab, first, secondContent, Placement::Bottom);
    auto* tabTop = new QLabel(QStringLiteral("tab-top"));
    auto* tabBottom = new QLabel(QStringLiteral("tab-bottom"));
    auto* firstTop = new QLabel(QStringLiteral("pane-top"));
    auto* secondBottom = new QLabel(QStringLiteral("pane-bottom"));
    QVERIFY(manager.setTabTopBar(tab, tabTop));
    QVERIFY(manager.setTabBottomBar(tab, tabBottom));
    QVERIFY(manager.setPaneTopBar(first, firstTop));
    QVERIFY(manager.setPaneBottomBar(second, secondBottom));
    QVERIFY(manager.setTabTopBarSharing(tab, PaneBarSharing::PerPane));
    QVERIFY(manager.setTabBottomBarSharing(tab, PaneBarSharing::PerPane));
    manager.showWindow(window);
    QCoreApplication::processEvents();
    auto* page = tabPage(firstContent);
    auto* firstFrame = paneHost(firstContent);
    auto* secondFrame = paneHost(secondContent);
    QVERIFY(page && firstFrame && secondFrame);
    QCOMPARE(tabTop->parentWidget(), page);
    QCOMPARE(tabBottom->parentWidget(), page);
    QVERIFY(tabTop->isVisible());
    QVERIFY(tabBottom->isVisible());
    QCOMPARE(page->layout()->itemAt(0)->widget(), tabTop);
    QCOMPARE(page->layout()->itemAt(page->layout()->count() - 1)->widget(), tabBottom);
    QCOMPARE(firstFrame->layout()->count(), 2);
    QCOMPARE(secondFrame->layout()->count(), 2);
    QCOMPARE(firstFrame->layout()->itemAt(1)->widget(), firstContent);
    QCOMPARE(secondFrame->layout()->itemAt(0)->widget(), secondContent);
    QVERIFY(manager.setPaneTopBar(first, nullptr));
    QVERIFY(manager.setPaneBottomBar(second, nullptr));
    processDeletes();
    QCOMPARE(paneHost(firstContent)->layout()->count(), 1);
    QCOMPARE(paneHost(secondContent)->layout()->count(), 1);
    QVERIFY(tabTop->isVisible());
    QVERIFY(tabBottom->isVisible());
    QVERIFY(manager.setTabTopBar(tab, nullptr));
    QVERIFY(manager.setTabBottomBar(tab, nullptr));
    QCOMPARE(page->layout()->count(), 1);
}

void PaneBarPolicyTest::changingPolicyPreservesWidgetsFocusAndSplitModel()
{
    WindowManager manager;
    const auto window = manager.createWindow();
    auto* firstContent = new QPlainTextEdit;
    const auto tab = manager.addTab(window, firstContent, {QStringLiteral("focus"), {}, {}});
    const auto first = manager.model().tab(tab)->activePane;
    auto* secondContent = new QPlainTextEdit;
    const auto second = manager.splitPane(tab, first, secondContent, Placement::Right);
    QPointer<QLineEdit> firstTop = new QLineEdit(QStringLiteral("retain text"));
    QPointer<QLabel> secondTop = new QLabel(QStringLiteral("second"));
    QPointer<QLabel> firstBottom = new QLabel(QStringLiteral("status"));
    QVERIFY(manager.setPaneTopBar(first, firstTop));
    QVERIFY(manager.setPaneTopBar(second, secondTop));
    QVERIFY(manager.setPaneBottomBar(first, firstBottom));
    manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    QVERIFY(manager.focusPane(tab, first));
    firstTop->setFocus();
    QTRY_VERIFY(firstTop->hasFocus());
    firstTop->setCursorPosition(4);
    const auto tree = manager.model().tab(tab)->panes;
    QSignalSpy changes(&manager, &WindowManager::layoutChanged);
    for (int iteration = 0; iteration < 6; ++iteration) {
        const auto policy = iteration % 2 == 0 ? PaneBarSharing::PerPane : PaneBarSharing::Shared;
        QVERIFY(manager.setTabTopBarSharing(tab, policy));
        QVERIFY(manager.setTabBottomBarSharing(tab, policy));
        QCoreApplication::processEvents();
        QVERIFY(firstTop && secondTop && firstBottom);
        QCOMPARE(manager.paneWidget(first), firstContent);
        QCOMPARE(manager.paneWidget(second), secondContent);
        QCOMPARE(manager.model().tab(tab)->panes, tree);
        QCOMPARE(manager.model().tab(tab)->activePane, first);
        QCOMPARE(firstTop->text(), QStringLiteral("retain text"));
        QCOMPARE(firstTop->cursorPosition(), 4);
        QVERIFY(firstTop->hasFocus());
    }
    QVERIFY(changes.count() >= 12);
    // A repeated setting is an idempotent API operation with no reparenting.
    auto* parent = firstTop->parentWidget();
    QVERIFY(manager.setTabTopBarSharing(tab, PaneBarSharing::Shared));
    QCOMPARE(firstTop->parentWidget(), parent);
    QVERIFY(firstTop->hasFocus());
}

void PaneBarPolicyTest::policiesAndBarsSurviveMovesAndPaneExtraction()
{
    WindowManager manager;
    const auto window = manager.createWindow();
    TabOptions options{QStringLiteral("movable"), QStringLiteral("test.editor"), {}};
    options.topBarSharing = PaneBarSharing::PerPane;
    options.bottomBarSharing = PaneBarSharing::Shared;
    auto* firstContent = new QPlainTextEdit;
    const auto tab = manager.addTab(window, firstContent, options);
    const auto first = manager.model().tab(tab)->activePane;
    auto* secondContent = new QPlainTextEdit;
    const auto second = manager.splitPane(tab, first, secondContent, Placement::Right);
    QPointer<QLabel> top = new QLabel(QStringLiteral("move-top"));
    QPointer<QLabel> bottom = new QLabel(QStringLiteral("move-bottom"));
    QPointer<QLabel> fallback = new QLabel(QStringLiteral("stay-on-tab"));
    QVERIFY(manager.setPaneTopBar(second, top));
    QVERIFY(manager.setPaneBottomBar(second, bottom));
    QVERIFY(manager.setTabTopBar(tab, fallback));
    manager.showWindow(window);
    const auto floating = manager.detachTab(tab);
    QVERIFY(!floating.isNull());
    QCOMPARE(top->window(), manager.windowWidget(floating));
    QCOMPARE(top->parentWidget(), paneHost(secondContent));
    QCOMPARE(bottom->parentWidget(), tabPage(secondContent));
    QCOMPARE(manager.tabOptions(tab).topBarSharing, PaneBarSharing::PerPane);
    const auto extracted = manager.extractPane(tab, second);
    QVERIFY(!extracted.isNull());
    QVERIFY(extracted != tab);
    const auto extractedWindow = manager.model().group(manager.model().groupForTab(extracted))->windowId;
    QCOMPARE(manager.tabOptions(extracted).topBarSharing, PaneBarSharing::PerPane);
    QCOMPARE(manager.tabOptions(extracted).bottomBarSharing, PaneBarSharing::Shared);
    QCOMPARE(manager.paneWidget(second), secondContent);
    QCOMPARE(top->parentWidget(), paneHost(secondContent));
    QCOMPARE(bottom->parentWidget(), tabPage(secondContent));
    QCOMPARE(top->window(), manager.windowWidget(extractedWindow));
    QCOMPARE(bottom->window(), manager.windowWidget(extractedWindow));
    QCOMPARE(fallback->window(), manager.windowWidget(floating));
    QVERIFY(fallback->isVisible());
    QVERIFY(manager.moveTab(extracted, manager.model().groupForTab(tab)));
    QCOMPARE(top->window(), manager.windowWidget(floating));
    QCOMPARE(manager.tabOptions(extracted).topBarSharing, PaneBarSharing::PerPane);
    QCOMPARE(manager.paneWidget(first), firstContent);
    QCOMPARE(manager.paneWidget(second), secondContent);
}

void PaneBarPolicyTest::closingPaneDestroysOnlyItsBars()
{
    WindowManager manager;
    const auto window = manager.createWindow();
    TabOptions options{QStringLiteral("lifetime"), {}, {}};
    options.topBarSharing = options.bottomBarSharing = PaneBarSharing::PerPane;
    const auto tab = manager.addTab(window, new QPlainTextEdit, options);
    const auto first = manager.model().tab(tab)->activePane;
    const auto second = manager.splitPane(tab, first, new QPlainTextEdit, Placement::Right);
    QPointer<QLabel> firstTop = new QLabel(QStringLiteral("kept"));
    QPointer<QLabel> secondTop = new QLabel(QStringLiteral("removed-top"));
    QPointer<QLabel> secondBottom = new QLabel(QStringLiteral("removed-bottom"));
    QVERIFY(manager.setPaneTopBar(first, firstTop));
    QVERIFY(manager.setPaneTopBar(second, secondTop));
    QVERIFY(manager.setPaneBottomBar(second, secondBottom));
    manager.showWindow(window);
    QVERIFY(manager.closePane(tab, second));
    processDeletes();
    QVERIFY(firstTop);
    QVERIFY(firstTop->isVisible());
    QVERIFY(secondTop.isNull());
    QVERIFY(secondBottom.isNull());
    QCOMPARE(manager.model().panes(tab).size(), 1);
    QVERIFY(manager.closeTab(tab));
    processDeletes();
    QVERIFY(firstTop.isNull());
}

void PaneBarPolicyTest::replacingTabBarsDefersDeletionLikePaneBars()
{
    WindowManager manager;
    const auto window = manager.createWindow();
    const auto tab = manager.addTab(window, new QPlainTextEdit, {QStringLiteral("bars"), {}, {}});
    manager.showWindow(window);
    for (const bool top : {true, false}) {
        const auto set = [&](QWidget* bar) { return top ? manager.setTabTopBar(tab, bar) : manager.setTabBottomBar(tab, bar); };
        QPointer<QWidget> first = new QLabel(QStringLiteral("first"));
        QVERIFY(set(first));
        // A replaced bar is hidden at once and deleted by the event loop.
        QPointer<QWidget> second = new QWidget;
        auto* replace = new QPushButton(QStringLiteral("Replace"), second);
        QVERIFY(set(second));
        QVERIFY(first); QVERIFY(first->isHidden());
        processDeletes();
        QVERIFY(first.isNull()); QVERIFY(second);
        // Replacing a bar from its own button's signal keeps the sender alive
        // until the signal returns.
        QPointer<QWidget> third;
        bool aliveDuringSignal = false;
        connect(replace, &QPushButton::clicked, this, [&] {
            third = new QLabel(QStringLiteral("third"));
            QVERIFY(set(third));
            aliveDuringSignal = !second.isNull();
        });
        replace->click();
        QVERIFY(aliveDuringSignal); QVERIFY(third);
        processDeletes();
        QVERIFY(second.isNull()); QVERIFY(third);
        QVERIFY(set(nullptr));
        processDeletes();
        QVERIFY(third.isNull());
    }
}

void PaneBarPolicyTest::layoutRestoresPolicyWithoutSerializingWidgets()
{
    WindowManager original;
    const auto window = original.createWindow();
    const auto tab = original.addTab(window, new QPlainTextEdit,
                                     {QStringLiteral("persist"), QStringLiteral("test.editor"), {}});
    const auto first = original.model().tab(tab)->activePane;
    const auto second = original.splitPane(tab, first, new QPlainTextEdit, Placement::Right);
    QVERIFY(original.setTabTopBarSharing(tab, PaneBarSharing::PerPane));
    const auto snapshot = original.saveLayout();
    WindowManager restored;
    int creations = 0;
    restored.setContentFactory([&creations](const TabOptions& options, PaneId) {
        ++creations;
        if (options.topBarSharing != PaneBarSharing::PerPane ||
            options.bottomBarSharing != PaneBarSharing::Shared) return static_cast<QWidget*>(nullptr);
        return static_cast<QWidget*>(new QPlainTextEdit);
    });
    QVERIFY(restored.restoreLayout(snapshot));
    QCOMPARE(creations, 2);
    QCOMPARE(restored.tabOptions(tab).topBarSharing, PaneBarSharing::PerPane);
    QCOMPARE(restored.tabOptions(tab).bottomBarSharing, PaneBarSharing::Shared);
    auto* top = new QLabel(QStringLiteral("new-host-bar"));
    QVERIFY(restored.setPaneTopBar(second, top));
    restored.showWindow(window);
    QCOMPARE(top->parentWidget(), paneHost(restored.paneWidget(second)));
    QVERIFY(top->isVisible());
}

void PaneBarPolicyTest::invalidCallsLeaveThePoliciesUnchanged()
{
    WindowManager manager;
    const auto window = manager.createWindow();
    const auto tab = manager.addTab(window, new QPlainTextEdit, {QStringLiteral("invalid"), {}, {}});
    const auto snapshot = manager.saveLayout();
    QVERIFY(!manager.setTabTopBarSharing(QUuid::createUuid(), PaneBarSharing::PerPane));
    QVERIFY(!manager.setTabBottomBarSharing(QUuid::createUuid(), PaneBarSharing::PerPane));
    QVERIFY(!manager.setTabTopBarSharing(tab, static_cast<PaneBarSharing>(-1)));
    QVERIFY(!manager.setTabBottomBarSharing(tab, static_cast<PaneBarSharing>(42)));
    QCOMPARE(manager.tabOptions(tab).topBarSharing, PaneBarSharing::Shared);
    QCOMPARE(manager.tabOptions(tab).bottomBarSharing, PaneBarSharing::Shared);
    QCOMPARE(manager.saveLayout(), snapshot);
}

QTEST_MAIN(PaneBarPolicyTest)
#include "pane_bar_policy_test.moc"
