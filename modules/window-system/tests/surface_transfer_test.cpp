// SPDX-License-Identifier: MIT
// Transfer commands activate the destination; this is a desktop suite.
#include <window_system/window_manager.hpp>
#include <QApplication>
#include <QPointer>
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
        WindowManager manager;
        const auto root = manager.createWindow();
        manager.addTab(root, new QWidget, {"Root"});
        const auto tab = manager.addTab(root, new QWidget, {"Moving"});
        for (const auto edge : {Edge::Left, Edge::Right, Edge::Top, Edge::Bottom}) {
            QVERIFY(manager.moveTabToEdge(tab, edge));
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QVERIFY(manager.moveTabToWindow(tab, root));
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
    }
};
QTEST_MAIN(SurfaceTransferTest)
#include "surface_transfer_test.moc"
