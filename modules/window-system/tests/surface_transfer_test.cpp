// SPDX-License-Identifier: MIT
// Transfer commands activate the destination; this is a desktop suite.
#include <window_system/window_manager.hpp>
#include <window_system/quick_workspace.hpp>
#include <QApplication>
#include <QPointer>
#include <QQuickItem>
#include <QTest>
#include <QWidget>
using namespace window_system;
class SurfaceTransferTest final : public QObject {
    Q_OBJECT
private slots:
    void aFloatingWindowCanTransferItsLastTabAndBeReleased()
    {
        WindowManager manager;
        const auto root = manager.createWindow();
        manager.addTab(root, new QWidget, {"Root"});
        const auto floating = manager.createWindow(WindowKind::Floating, {}, root);
        const auto tab = manager.addTab(floating, new QWidget, {"Moving"});
        QPointer<QWidget> source = manager.windowWidget(floating);
        QVERIFY(source && !source->isVisible());
        QVERIFY(manager.moveTabToWindow(tab, root));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!source);
        QCOMPARE(manager.model().group(manager.model().groupForTab(tab))->windowId, root);
    }
    void edgeTransfersLeaveNoCallbacksIntoReleasedSurfaces()
    {
        WindowManager manager(WindowManager::Presentation::External);
        QuickWorkspace workspace(manager);
        workspace.setContentFactory([](const TabOptions&, PaneId) { return new QQuickItem; });
        const auto root = manager.createWindow();
        manager.addTab(root, {"Root"});
        const auto tab = manager.addTab(root, {"Moving"});
        QCoreApplication::sendPostedEvents(&workspace, QEvent::MetaCall);
        for (const auto edge : {Edge::Left, Edge::Right, Edge::Top, Edge::Bottom}) {
            QVERIFY(manager.moveTabToEdge(tab, edge));
            QCoreApplication::sendPostedEvents(&workspace, QEvent::MetaCall);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QVERIFY(manager.moveTabToWindow(tab, root));
            QCoreApplication::sendPostedEvents(&workspace, QEvent::MetaCall);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
    }
};
QTEST_MAIN(SurfaceTransferTest)
#include "surface_transfer_test.moc"
