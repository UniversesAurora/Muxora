// SPDX-License-Identifier: MIT
#include <window_system/floating_panel.hpp>
#include <window_system/window_manager.hpp>

#include <QApplication>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QTest>
#include <QTreeWidget>
#include <functional>
#include <memory>

using namespace window_system;

namespace {
class DeferPanelDeletionOnEvent final : public QObject {
public:
    QPointer<QWidget> owner;
    QEvent::Type trigger = QEvent::None;
    bool armed = false;
    std::function<void()> scheduleDeletion;

protected:
    bool eventFilter(QObject*, QEvent* event) override
    {
        if (!armed || event->type() != trigger) return false;
        armed = false;
        // QWidget's show/hide callers continue using their private data after
        // event delivery. Qt requires deferred destruction in this context:
        // https://doc.qt.io/qt-6/qobject.html#dtor.QObject
        if (scheduleDeletion) scheduleDeletion();
        else owner->deleteLater();
        return false;
    }
};
} // namespace

class FloatingPanelTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); }
    void cleanup() { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); }
    void defaultPaletteIsNonModalAndOutsidePressClosesWithoutConsumingInput();
    void panelOptionsAreIndependent_data();
    void panelOptionsAreIndependent();
    void visiblePolicyChangesUpdateModality();
    void visiblePolicyChangesAllowDeferredOwnerDeletion_data();
    void visiblePolicyChangesAllowDeferredOwnerDeletion();
    void ownedPopupsAndInsidePressDoNotDismiss();
    void applicationDeactivationFollowsOutsidePolicy_data();
    void applicationDeactivationFollowsOutsidePolicy();
    void paletteExecutionPreservesCapturedContext();
    void rejectedPaletteNeverExecutes();
    void pendingExecutionDoesNotOutliveManager();
    void paletteEnabledCallbackMayDestroyItsManager();
    void closeCallbacksMayDestroyThePanelOwner();
    void selectorIsNonModalAndActivatesTheSelectedWindow();
    void windowActivationAllowsDeferredManagerDeletionOnShow();
    void paneFocusAllowsDeferredManagerDeletion_data();
    void paneFocusAllowsDeferredManagerDeletion();
};

void FloatingPanelTest::defaultPaletteIsNonModalAndOutsidePressClosesWithoutConsumingInput()
{
    WindowManager manager;
    const auto window = manager.createWindow();
    manager.addTab(window, new QLabel("Content"), {"First", {}, {}});
    manager.showWindow(window);
    QVERIFY(QTest::qWaitForWindowExposed(manager.windowWidget(window)));
    manager.showCommandPalette(window);
    auto* panel = manager.windowWidget(window)->findChild<FloatingPanel*>("CommandPalette");
    QVERIFY(panel); QVERIFY(panel->isVisible()); QVERIFY(!panel->isModal());
    QCOMPARE(panel->windowModality(), Qt::NonModal);
    QVERIFY(!QApplication::activeModalWidget());
    const QPointer<FloatingPanel> retained(panel);

    // Logical screen space shrinks with DPI. A fixed offset after the panel's
    // bottom corner can put the entire input target outside the visible screen.
    auto* screen = panel->screen(); QVERIFY(screen);
    const QRect available = screen->availableGeometry();
    QPushButton other("Another window");
    other.setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
    const QSize targetSize = other.sizeHint().expandedTo(other.minimumSizeHint());
    const QRect inset = available.adjusted(8, 8, -8, -8);
    QRect outside;
    for (const auto& point : {inset.topLeft(), QPoint(inset.right() - targetSize.width() + 1, inset.top()),
                             QPoint(inset.left(), inset.bottom() - targetSize.height() + 1),
                             inset.bottomRight() - QPoint(targetSize.width() - 1, targetSize.height() - 1)}) {
        const QRect candidate(point, targetSize);
        if (available.contains(candidate) && !panel->frameGeometry().intersects(candidate)) {
            outside = candidate;
            break;
        }
    }
    QVERIFY2(!outside.isEmpty(), "The current screen must expose a non-overlapping outside-click target");
    QVERIFY(available.contains(outside)); QVERIFY(!panel->frameGeometry().intersects(outside));
    other.setGeometry(outside);
    other.show(); QVERIFY(QTest::qWaitForWindowExposed(&other));
    QVERIFY(retained); QVERIFY(retained->isVisible());
    QVERIFY(available.contains(other.frameGeometry()));
    QVERIFY(!retained->frameGeometry().intersects(other.frameGeometry()));
    int clicks = 0; connect(&other, &QPushButton::clicked, &other, [&] { ++clicks; });
    QTest::mouseClick(&other, Qt::LeftButton);
    QCOMPARE(clicks, 1); QVERIFY(!retained || !retained->isVisible());
}

void FloatingPanelTest::panelOptionsAreIndependent_data()
{
    QTest::addColumn<bool>("dismiss"); QTest::addColumn<bool>("modal");
    QTest::newRow("nonmodal-dismiss") << true << false;
    QTest::newRow("nonmodal-persistent") << false << false;
    QTest::newRow("modal-dismiss") << true << true;
    QTest::newRow("modal-persistent") << false << true;
}
void FloatingPanelTest::panelOptionsAreIndependent()
{
    QFETCH(bool, dismiss); QFETCH(bool, modal);
    QWidget owner; owner.resize(640, 480); owner.show();
    FloatingPanel panel(&owner, {dismiss, modal}); panel.resize(240, 120); panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    QCOMPARE(panel.options().dismissOnOutsidePress, dismiss);
    QCOMPARE(panel.options().modal, modal);
    QCOMPARE(panel.isModal(), modal);
    QCOMPARE(panel.windowModality(), modal ? Qt::WindowModal : Qt::NonModal);
    if (modal) QCOMPARE(QApplication::activeModalWidget(), static_cast<QWidget*>(&panel));
    panel.reject(); QVERIFY(!panel.isVisible());
}

void FloatingPanelTest::visiblePolicyChangesUpdateModality()
{
    QWidget owner; owner.show(); FloatingPanel panel(&owner); panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    panel.setOptions({false, true}); QVERIFY(panel.isVisible());
    QCOMPARE(panel.windowModality(), Qt::WindowModal);
    QTRY_COMPARE(QApplication::activeModalWidget(), static_cast<QWidget*>(&panel));
    panel.setOptions({true, false}); QVERIFY(panel.isVisible());
    QCOMPARE(panel.windowModality(), Qt::NonModal); QTRY_VERIFY(!QApplication::activeModalWidget());
}

void FloatingPanelTest::visiblePolicyChangesAllowDeferredOwnerDeletion_data()
{
    QTest::addColumn<int>("eventType");
    QTest::newRow("hide") << int(QEvent::Hide);
    QTest::newRow("show") << int(QEvent::Show);
}
void FloatingPanelTest::visiblePolicyChangesAllowDeferredOwnerDeletion()
{
    QFETCH(int, eventType);
    QPointer<QWidget> owner = new QWidget;
    auto* panel = new FloatingPanel(owner); panel->resize(240, 120); panel->show();
    QVERIFY(QTest::qWaitForWindowExposed(panel));
    const QPointer<FloatingPanel> retained(panel);
    int ownerDestroyed = 0, panelDestroyed = 0, pendingCallbacks = 0;
    connect(owner, &QObject::destroyed, this, [&] { ++ownerDestroyed; });
    connect(panel, &QObject::destroyed, this, [&] { ++panelDestroyed; });
    DeferPanelDeletionOnEvent observer; observer.owner = owner;
    observer.trigger = QEvent::Type(eventType); observer.armed = true;
    panel->installEventFilter(&observer);
    panel->setOptions({false, true});
    QVERIFY(owner); QVERIFY(retained); QVERIFY(!observer.armed);
    QVERIFY(retained->isVisible()); QCOMPARE(retained->windowModality(), Qt::WindowModal);
    QVERIFY(!retained->options().dismissOnOutsidePress); QVERIFY(retained->options().modal);
    QCOMPARE(ownerDestroyed, 0); QCOMPARE(panelDestroyed, 0);
    QMetaObject::invokeMethod(owner, [&] { ++pendingCallbacks; }, Qt::QueuedConnection);
    QMetaObject::invokeMethod(panel, [&] { ++pendingCallbacks; }, Qt::QueuedConnection);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(!owner); QVERIFY(!retained); QCOMPARE(ownerDestroyed, 1); QCOMPARE(panelDestroyed, 1);
    QCoreApplication::processEvents(); QCOMPARE(pendingCallbacks, 0);
}

void FloatingPanelTest::ownedPopupsAndInsidePressDoNotDismiss()
{
    FloatingPanel panel; panel.resize(320, 220);
    QPushButton inside("Inside", &panel); inside.setGeometry(20, 20, 80, 30);
    panel.show(); QVERIFY(QTest::qWaitForWindowExposed(&panel));
    QTest::mouseClick(&inside, Qt::LeftButton); QVERIFY(panel.isVisible());
    QMenu popup(&panel); popup.addAction("Owned popup"); popup.move(panel.frameGeometry().bottomRight() + QPoint(30, 30));
    const QPoint local(8, 8), global = popup.mapToGlobal(local);
    QVERIFY(!panel.frameGeometry().contains(global));
    QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&popup, &press); QVERIFY(panel.isVisible());
}

void FloatingPanelTest::applicationDeactivationFollowsOutsidePolicy_data()
{
    QTest::addColumn<bool>("dismiss");
    QTest::newRow("dismiss") << true; QTest::newRow("persistent") << false;
}
void FloatingPanelTest::applicationDeactivationFollowsOutsidePolicy()
{
    QFETCH(bool, dismiss);
    FloatingPanel panel(nullptr, {dismiss, false}); panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    QEvent deactivate(QEvent::ApplicationDeactivate); QApplication::sendEvent(qApp, &deactivate);
    QCOMPARE(panel.isVisible(), !dismiss);
}

void FloatingPanelTest::paletteExecutionPreservesCapturedContext()
{
    WindowManager manager; const auto window = manager.createWindow();
    const auto original = manager.addTab(window, new QLabel("Original"), {"Original", {}, {}});
    const auto firstPane = manager.model().tab(original)->activePane;
    const auto selectedPane = manager.splitPane(original, firstPane, new QLabel("Second pane"), Placement::Right);
    const auto other = manager.addTab(window, new QLabel("Other"), {"Other", {}, {}});
    QVERIFY(manager.activateTab(original)); QVERIFY(manager.focusPane(original, selectedPane));
    int executions = 0; CommandContext executed;
    QVERIFY(manager.commands().add({"host.paletteProbe", "Palette probe", "Host", {}, {},
        [&](const CommandContext& context) { executed = context; ++executions; return true; }}));
    manager.showWindow(window); manager.showCommandPalette(window, {false, false});
    auto* panel = manager.windowWidget(window)->findChild<FloatingPanel*>("CommandPalette"); QVERIFY(panel);
    auto* search = panel->findChild<QLineEdit*>("CommandPaletteSearch"); QVERIFY(search);
    QVERIFY(manager.activateTab(other));
    search->setText("host.paletteProbe"); QTest::keyClick(search, Qt::Key_Return);
    QCOMPARE(executions, 0); QTRY_COMPARE(executions, 1);
    QCOMPARE(executed.window, window); QCOMPARE(executed.tab, original); QCOMPARE(executed.pane, selectedPane);
}

void FloatingPanelTest::rejectedPaletteNeverExecutes()
{
    WindowManager manager; const auto window = manager.createWindow();
    manager.addTab(window, new QLabel("Content"), {"First", {}, {}});
    int executions = 0;
    QVERIFY(manager.commands().add({"host.never", "Never execute", "Host", {}, {},
        [&](const CommandContext&) { ++executions; return true; }}));
    manager.showWindow(window); manager.showCommandPalette(window);
    auto* panel = manager.windowWidget(window)->findChild<FloatingPanel*>("CommandPalette"); QVERIFY(panel);
    auto* search = panel->findChild<QLineEdit*>("CommandPaletteSearch"); search->setText("host.never");
    QTest::keyClick(search, Qt::Key_Escape); QCoreApplication::processEvents(); QCOMPARE(executions, 0);
}

void FloatingPanelTest::pendingExecutionDoesNotOutliveManager()
{
    auto manager = std::make_unique<WindowManager>(); const auto window = manager->createWindow();
    manager->addTab(window, new QLabel("Content"), {"First", {}, {}});
    int executions = 0;
    QVERIFY(manager->commands().add({"host.pending", "Pending", "Host", {}, {},
        [&](const CommandContext&) { ++executions; return true; }}));
    manager->showWindow(window); manager->showCommandPalette(window);
    auto* search = manager->windowWidget(window)->findChild<QLineEdit*>("CommandPaletteSearch"); QVERIFY(search);
    search->setText("host.pending"); QTest::keyClick(search, Qt::Key_Return);
    manager.reset(); QCoreApplication::processEvents(); QCOMPARE(executions, 0);
}

void FloatingPanelTest::paletteEnabledCallbackMayDestroyItsManager()
{
    auto manager = std::make_unique<WindowManager>(); const auto window = manager->createWindow();
    manager->addTab(window, new QLabel("Content"), {"First", {}, {}});
    bool destroyDuringQuery = false;
    QVERIFY(manager->commands().add({"host.destroyDuringQuery", "Destroy during query", "Host", {},
        [&](const CommandContext&) { if (destroyDuringQuery) manager.reset(); return false; },
        [](const CommandContext&) { return false; }}));
    destroyDuringQuery = true;
    manager->showCommandPalette(window);
    QVERIFY(!manager);
}

void FloatingPanelTest::closeCallbacksMayDestroyThePanelOwner()
{
    QPointer<QWidget> owner = new QWidget;
    auto* panel = new FloatingPanel(owner); panel->resize(240, 120); panel->show();
    QVERIFY(QTest::qWaitForWindowExposed(panel));
    const QPointer<FloatingPanel> retained(panel);
    connect(panel, &QDialog::finished, panel, [owner] { delete owner.data(); });
    QPushButton outside; outside.move(panel->frameGeometry().bottomRight() + QPoint(30, 30));
    QTest::mouseClick(&outside, Qt::LeftButton);
    QVERIFY(!owner); QVERIFY(!retained);
}

void FloatingPanelTest::selectorIsNonModalAndActivatesTheSelectedWindow()
{
    WindowManager manager; const auto owner = manager.createWindow();
    manager.addTab(owner, new QLabel("Owner"), {"Owner", {}, {}});
    const auto floating = manager.createWindow(WindowKind::Floating, {}, owner);
    const auto targetTab = manager.addTab(floating, new QLabel("Target"), {"Selector target", {}, {}});
    manager.showWindow(owner); manager.showWindow(floating); QVERIFY(manager.activateWindow(owner));
    manager.showWindowSelector(owner);
    auto* panel = manager.windowWidget(owner)->findChild<FloatingPanel*>("WindowSelector");
    QVERIFY(panel); QVERIFY(panel->isVisible()); QVERIFY(!panel->isModal());
    auto* search = panel->findChild<QLineEdit*>("WindowSelectorSearch"); search->setText("Selector target");
    QTest::keyClick(search, Qt::Key_Return); QTRY_COMPARE(manager.context().window, floating);
    QCOMPARE(manager.context(floating).tab, targetTab);
}

void FloatingPanelTest::windowActivationAllowsDeferredManagerDeletionOnShow()
{
    auto manager = std::make_unique<WindowManager>(); const auto window = manager->createWindow();
    const auto tab = manager->addTab(window, new QLabel("Content"), {"First", {}, {}});
    const QPointer<WindowManager> retained(manager.get());
    const QPointer<QWidget> retainedWindow(manager->windowWidget(window));
    const QPointer<QWidget> retainedContent(manager->paneWidget(manager->model().tab(tab)->activePane));
    QVERIFY(retainedWindow->isHidden());
    DeferPanelDeletionOnEvent observer; observer.trigger = QEvent::Show; observer.armed = true;
    observer.scheduleDeletion = [&] { manager.release()->deleteLater(); };
    retainedWindow->installEventFilter(&observer);
    QVERIFY(manager->activateWindow(window));
    QVERIFY(!observer.armed); QVERIFY(!manager); QVERIFY(retained); QVERIFY(retainedWindow->isVisible());
    QCOMPARE(retained->context().window, window); QCOMPARE(retained->context(window).tab, tab);
    int pendingCallbacks = 0;
    QMetaObject::invokeMethod(retained, [&] { ++pendingCallbacks; }, Qt::QueuedConnection);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(!retained); QVERIFY(!retainedWindow); QVERIFY(!retainedContent);
    QCoreApplication::processEvents(); QCOMPARE(pendingCallbacks, 0);
}

void FloatingPanelTest::paneFocusAllowsDeferredManagerDeletion_data()
{
    QTest::addColumn<bool>("sharedBar");
    QTest::newRow("focus-in") << false;
    QTest::newRow("shared-bar-hide") << true;
}

void FloatingPanelTest::paneFocusAllowsDeferredManagerDeletion()
{
    QFETCH(bool, sharedBar);
    auto manager = std::make_unique<WindowManager>(); const auto window = manager->createWindow();
    const auto tab = manager->addTab(window, new QLabel("First"), {"First", {}, {}});
    const auto first = manager->model().tab(tab)->activePane;
    const auto second = manager->splitPane(tab, first, new QLabel("Second"), Placement::Right);
    const QPointer<QLabel> firstBar = new QLabel("First top bar");
    const QPointer<QLabel> secondBar = new QLabel("Second top bar");
    QVERIFY(manager->setPaneTopBar(first, firstBar)); QVERIFY(manager->setPaneTopBar(second, secondBar));
    manager->showWindow(window); QVERIFY(QTest::qWaitForWindowActive(manager->windowWidget(window)));
    QVERIFY(manager->focusPane(tab, first)); QVERIFY(firstBar->isVisible());
    const QPointer<WindowManager> retained(manager.get());
    const QPointer<QWidget> secondContent(manager->paneWidget(second));
    DeferPanelDeletionOnEvent observer; observer.trigger = sharedBar ? QEvent::Hide : QEvent::FocusIn;
    observer.armed = true; observer.scheduleDeletion = [&] { manager.release()->deleteLater(); };
    QWidget* receiver = sharedBar ? static_cast<QWidget*>(firstBar.data()) : secondContent.data();
    receiver->installEventFilter(&observer);
    QVERIFY(manager->focusPane(tab, second));
    QVERIFY(!observer.armed); QVERIFY(!manager); QVERIFY(retained);
    QCOMPARE(retained->model().tab(tab)->activePane, second);
    QVERIFY(firstBar->isHidden()); QVERIFY(secondBar->isVisible());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(!retained); QVERIFY(!firstBar); QVERIFY(!secondBar); QVERIFY(!secondContent);
}

QTEST_MAIN(FloatingPanelTest)
#include "floating_panel_test.moc"
