// SPDX-License-Identifier: MIT
// Native capture handoff (tab-strip-design §6.2): leave a strip, enter another,
// move between rows, leave and reenter, then drop. Layout combinations belong
// to tab-layout and tab-strip-layout; here real held input drives Windows'
// modal move loop, which authored QMouseEvents cannot exercise.
#include "native_input.hpp"
#include "chrome/workspace_window.hpp"
#include "docking/tab_strip.hpp"
#include <window_system/window_manager.hpp>
#include <QApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <QScopeGuard>
#include <QScreen>
#include <QTimer>
#include <algorithm>

using namespace window_system;

namespace {
GroupTabs* viewFor(QWidget* window, GroupId id)
{
    for (auto* widget : window->findChildren<QWidget*>())
        if (auto* group = dynamic_cast<GroupTabs*>(widget); group && group->isVisible() && group->property("groupId").toUuid() == id) return group;
    return nullptr;
}
bool inSystemMove()
{
#ifdef Q_OS_WIN
    GUITHREADINFO info{}; info.cbSize = sizeof(info);
    return GetGUIThreadInfo(GetCurrentThreadId(), &info) && (info.flags & GUI_INMOVESIZE);
#else
    return false;
#endif
}
}

class NativeDragTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); TabBar::setAnimationsEnabled(false); }
    void realDragKeepsFollowingAcrossNativeMoveAndStripCapture();
};

void NativeDragTest::realDragKeepsFollowingAcrossNativeMoveAndStripCapture()
{
    WindowManager manager;
    auto appearance = manager.tabAppearance(); appearance.multiline = true; appearance.stickyMultiline = true;
    appearance.stickyTabsOnSeparateRow = true; appearance.showNewTabButton = false;
    QVERIFY(manager.setTabAppearance(appearance));
    const auto available = qApp->primaryScreen()->availableGeometry();
    const auto window = manager.createWindow(WindowKind::Workbench,
        {"Native drag", {std::min(1100, available.width() - 24), std::min(600, available.height() - 24)}});
    const auto movingTab = manager.addTab(window, new QLabel("Moving content"), {"Moving tab", "test", {}});
    auto* content = manager.paneWidget(manager.model().tab(movingTab)->activePane);
    const auto source = manager.model().groupForTab(movingTab);
    manager.addTab(window, new QLabel("Source"), {"Source", "test", {}});
    const auto firstTab = manager.addTabToNewGroup(window, new QLabel("Target 0"), {"Target tab 0", "test", {}}, source, Placement::Right);
    const auto target = manager.model().groupForTab(firstTab); QVERIFY(!target.isNull());
    QVERIFY(manager.setTabSticky(firstTab, true));
    for (int i = 1; i < 8; ++i) {
        const auto tab = manager.addTab(window, new QLabel(QString("Target %1").arg(i)),
            {QString("Target tab %1").arg(i), "test", {}}, target);
        if (i < 3) QVERIFY(manager.setTabSticky(tab, true));
    }
    auto* host = static_cast<WorkspaceWindow*>(manager.windowWidget(window));
    host->move(available.topLeft() + QPoint(12, 12)); manager.showWindow(window); host->raise(); host->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(host));
    QPointer<GroupTabs> sourceView = viewFor(host, source), targetView = viewFor(host, target);
    QVERIFY(sourceView && targetView);
    QPointer<TabBar> sourceBar = sourceView->tabBar(), targetBar = targetView->tabBar();
    const auto sourceRect = sourceBar->tabRect(sourceBar->indexOf(movingTab));
    const auto start = sourceBar->mapToGlobal(QPoint(sourceRect.left() + 16, sourceRect.center().y()));
    const auto firstTarget = targetBar->tabRect(3); const auto lastTarget = targetBar->tabRect(7);
    QVERIFY(!firstTarget.isEmpty() && !lastTarget.isEmpty());
    const auto atFirst = targetBar->mapToGlobal(QPoint(firstTarget.left() + 4, firstTarget.center().y()));
    const auto atLast = targetBar->mapToGlobal(QPoint(lastTarget.left() + 4, lastTarget.center().y()));
    const auto outside = sourceView->mapToGlobal(QPoint(sourceView->width() / 2, sourceView->height() - 80));
    QPointer<WorkspaceWindow> carrier;
    int stage = 0; bool done = false; QString error; QRect firstGap;
    QTimer drive; drive.setInterval(10); drive.setSingleShot(true); QElapsedTimer clock; clock.start();
    const auto moveTo = [&](QPoint global) { native_input::mouse(host, host->mapFromGlobal(global)); };
    const auto finish = [&] {
        drive.stop(); native_input::leftButton(host, false); done = true;
    };
    const auto cleanup = qScopeGuard([&] { if (!done) finish(); });
    connect(&drive, &QTimer::timeout, host, [&] {
        if (done) return;
        // Each action can synchronously enter Windows' move loop. A newly
        // armed single-shot timer can drive that loop while this call waits.
        drive.start();
        if (stage >= 3) targetView = viewFor(host, target);
        if (!targetView || (stage == 2 && !sourceBar)) { error = "The fixture lost its visible tab strip"; finish(); return; }
        if (stage >= 4 && !carrier) { error = "The carried window disappeared before release"; finish(); return; }
        if (clock.elapsed() > 6000) { error = QString("Timed out at stage %1; native=%2, grab=%3")
            .arg(stage).arg(inSystemMove()).arg(QWidget::mouseGrabber() ? QWidget::mouseGrabber()->objectName() : QString("none"));
            if (carrier) error += QString("; position=%1,%2 globalAnchor=%3,%4 cursor=%5,%6 presentation=%7 gap=%8,%9 %10x%11")
                .arg(carrier->x()).arg(carrier->y()).arg(carrier->mapToGlobal(carrier->dragLabelAnchor()).x()).arg(carrier->mapToGlobal(carrier->dragLabelAnchor()).y())
                .arg(QCursor::pos().x()).arg(QCursor::pos().y()).arg(int(carrier->dragPresentation()))
                .arg(targetView->insertionPreviewRectGlobal().x()).arg(targetView->insertionPreviewRectGlobal().y())
                .arg(targetView->insertionPreviewRectGlobal().width()).arg(targetView->insertionPreviewRectGlobal().height());
            finish(); return; }
        switch (stage) {
        case 0: ++stage; moveTo(start); native_input::leftButton(host, true); break;
        case 1:
            if (!QApplication::mouseButtons().testFlag(Qt::LeftButton)) return;
            ++stage; moveTo(start + QPoint(QApplication::startDragDistance() + 4, 0)); break;
        case 2:
            if (sourceBar->liftedRect().isEmpty()) return;
            ++stage; moveTo(outside); break;
        case 3:
            if (manager.model().windowOrder().size() != 2 || !inSystemMove()) return;
            for (const auto id : manager.model().windowOrder()) if (id != window)
                carrier = static_cast<WorkspaceWindow*>(manager.windowWidget(id));
            ++stage; moveTo(atFirst); break;
        case 4:
            if (!carrier || carrier->dragPresentation() != WorkspaceWindow::DragPresentation::Tab
                || inSystemMove() || QWidget::mouseGrabber() != carrier) return;
            firstGap = targetView->insertionPreviewRectGlobal();
            if (firstGap.isEmpty()) { error = "Reentering the strip has no gap"; finish(); return; }
            ++stage; moveTo(atLast); break;
        case 5:
            if (carrier->mapToGlobal(carrier->dragLabelAnchor()) != QCursor::pos()) return;
            if (targetView->insertionPreviewRectGlobal() == firstGap) { error = "The gap stopped following the pointer"; finish(); return; }
            ++stage; moveTo(outside); break;
        case 6:
            if (!carrier || carrier->dragPresentation() != WorkspaceWindow::DragPresentation::Window || !inSystemMove()) return;
            ++stage; moveTo(atFirst); break;
        case 7:
            if (!carrier || carrier->dragPresentation() != WorkspaceWindow::DragPresentation::Tab
                || inSystemMove() || QWidget::mouseGrabber() != carrier) return;
            ++stage; moveTo(atLast); break;
        case 8:
            if (carrier->mapToGlobal(carrier->dragLabelAnchor()) != QCursor::pos()) return;
            if (targetView->insertionPreviewRectGlobal().isEmpty()) { error = "The second reentry lost its gap"; finish(); return; }
            finish(); break;
        }
    });
    drive.start(); QTRY_VERIFY_WITH_TIMEOUT(done, 8000);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QTRY_COMPARE(manager.model().groupForTab(movingTab), target);
    QVERIFY(!manager.isTabSticky(movingTab));
    QCOMPARE(manager.model().windowOrder().size(), 1);
    QCOMPARE(manager.paneWidget(manager.model().tab(movingTab)->activePane), content);
    QVERIFY(!QWidget::mouseGrabber()); QVERIFY(manager.model().validate());
}

QTEST_MAIN(NativeDragTest)
#include "native_drag_test.moc"
